// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>

#include <AfterNightEngine/Algorithms/Spatial/Convolution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/SpatialPlane.h"
#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/ReferenceKernelInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

// Two ways to blur, and the reason both are here is that they are not the same picture.
//
// The exact method is the taps and the stencil family: this file produces the coefficients and
// `convolveImageSeparable` applies them. What is interesting about its accuracy is in
// `writeGaussianTaps`, which is the whole of what an engine gets wrong about Gaussians. The one
// exception is a planar Float32 image in which every sample means something, which runs the
// symmetric Float32 row kernels of the CPU dispatch table directly, pairing the two samples at each
// distance before weighting them; it falls back to the stencil family when its intermediate plane is
// refused.
//
// The approximation is the part that needs an implementation of its own. A box average over a
// moving window is a running sum - one sample enters, one leaves - so its cost per output does not
// depend on the window, and three of them along each axis is within a few parts in a thousand of a
// Gaussian. Expressing it as three separable convolutions with uniform taps would be the same
// picture and would throw away the entire point, because a tap array of width `2r + 1` costs `2r + 1`
// multiply-accumulates per output however uniform its coefficients are. So the running sum is
// written here, and it is what makes the disposition a measurement rather than a formality.
//
// The general running sum is accumulated in Float64 and the window is divided once per output. Over
// a row of sixty-five thousand samples the accumulated rounding is a few parts in ten to the
// eleventh, which is far below the quantum of every representation this engine stores. The fused
// form a planar Float32 preview takes instead accumulates in Float32 in a fixed evaluation order,
// because it is a latency path.

namespace an
{
namespace
{

using spatial_detail::filterEveryChannel;
using spatial_detail::spatialFailure;
using spatial_detail::SpatialGeometry;
using spatial_detail::spatialLimitFailure;

constexpr const char* kPlanOperation = "an::planGaussianBlur";
constexpr const char* kPreviewPlanOperation = "an::planRealtimePreviewGaussianBlur";
constexpr const char* kTapsOperation = "an::writeGaussianTaps";
constexpr const char* kBlurOperation = "an::blurImageGaussian";
constexpr const char* kPreviewBlurOperation = "an::blurImageRealtimePreviewGaussian";

/// How many columns one vertical box pass carries running sums for at once.
///
/// A vertical running sum needs one accumulator per column, and walking the whole width at once
/// would need an allocation proportional to the picture. A band of a hundred and twenty-eight
/// columns is one kibibyte of accumulators on the stack and half a cache line of source per row, so
/// the pass reads the plane in row order and still holds its state in registers and L1.
constexpr std::uint32_t kBoxColumnBand = 128u;

/// What one row of a horizontal box pass costs, for the grain policy.
///
/// The loop walks rows, so its item is a row: one read and one write of every sample, and a handful
/// of operations each. A cost stated per sample would make a whole picture one chunk and leave the
/// pass on one thread.
///
/// @param width how many pixels one row holds.
/// @return the cost of one row.
[[nodiscard]] TaskCost boxRowCost(std::uint32_t width) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * 8u, .operationsPerItem = samples * 8u};
}

/// What one band of a vertical box pass costs, for the grain policy.
///
/// A band is a column range walked down the whole picture, so its item is that whole strip.
///
/// @param columns how many columns the band covers.
/// @param height how many rows the picture holds.
/// @return the cost of one band.
[[nodiscard]] TaskCost boxBandCost(std::uint32_t columns, std::uint32_t height) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(columns) * height;
    return TaskCost{.bytesPerItem = samples * 8u, .operationsPerItem = samples * 8u};
}

/// The three box widths whose repeated average comes closest to a Gaussian of this sigma.
///
/// The standard construction: the ideal width is the one whose `n`-fold convolution has the wanted
/// variance, the two nearest odd widths bracket it, and the count of the narrower one is chosen so
/// that the realized variance is nearest. Evaluated in Float64.
///
/// @param sigma the standard deviation to realize, in pixels.
/// @param radii where the three radii go.
/// @return the sigma the chosen widths actually realize.
[[nodiscard]] double boxRadiiForSigma(double sigma, std::array<std::uint32_t, kGaussianBoxPasses>& radii) noexcept
{
    const double passes = static_cast<double>(kGaussianBoxPasses);
    const double ideal = std::sqrt((12.0 * sigma * sigma / passes) + 1.0);
    double lower = std::floor(ideal);
    if (std::fmod(lower, 2.0) == 0.0)
    {
        lower -= 1.0;
    }
    lower = (std::max)(lower, 1.0);
    const double upper = lower + 2.0;
    const double idealCount =
        (12.0 * sigma * sigma - passes * lower * lower - 4.0 * passes * lower - 3.0 * passes) / (-4.0 * lower - 4.0);
    const double count = std::floor(idealCount + 0.5);
    for (std::uint32_t index = 0u; index < kGaussianBoxPasses; ++index)
    {
        const double width = static_cast<double>(index) < count ? lower : upper;
        radii[index] = static_cast<std::uint32_t>((width - 1.0) / 2.0);
    }
    const double narrow = (std::max)(0.0, (std::min)(count, passes));
    const double variance = (narrow * lower * lower + (passes - narrow) * upper * upper - passes) / 12.0;
    return std::sqrt((std::max)(variance, 0.0));
}

/// The radius the exact method truncates at.
///
/// @param sigma the standard deviation, in pixels.
/// @param truncation how many sigmas to keep.
/// @return the radius, at least one so that a plan always names a real neighbourhood.
[[nodiscard]] std::uint64_t truncatedRadius(double sigma, double truncation) noexcept
{
    const double reach = std::ceil(sigma * truncation);
    if (!std::isfinite(reach) || reach < 1.0)
    {
        return 1u;
    }
    if (reach > static_cast<double>(kMaxStencilRadius) * 1024.0)
    {
        return static_cast<std::uint64_t>(kMaxStencilRadius) * 1024u;
    }
    return static_cast<std::uint64_t>(reach);
}

/// One border-resolved, validity-resolved sample of a plane.
///
/// @param row the row the sample lies on, already border-resolved, or null for a constant border.
/// @param column the column, already border-resolved.
/// @param sourceRow which row of the plane it came from, for a mask lookup.
/// @param accepts the validity rule, or null when every sample participates.
/// @param constantBorder the value a coordinate outside the plane reads.
/// @param invalidNeighbor the value a rejected sample contributes.
/// @return the sample as a double.
[[nodiscard]] double resolvedSample(const float* row, std::uint32_t column, std::uint32_t sourceRow,
                                    const reference_detail::ValidityAccessor* accepts, float constantBorder,
                                    float invalidNeighbor) noexcept
{
    if (row == nullptr)
    {
        return static_cast<double>(constantBorder);
    }
    const float sample = row[column];
    if (accepts != nullptr && !accepts->accepts(sample, column, sourceRow))
    {
        return static_cast<double>(invalidNeighbor);
    }
    return static_cast<double>(sample);
}

/// What one box pass reads, writes and substitutes.
struct BoxPass
{
    /// Half the window width. A radius of zero is the identity.
    std::uint32_t radius = 0u;

    /// How a coordinate outside the plane is resolved.
    KernelBorderMode border = KernelBorderMode::ReflectExcludingEdge;

    /// The value a coordinate outside the plane reads under a constant border.
    float constantBorder = 0.0f;

    /// The value a rejected sample contributes.
    float invalidNeighbor = 0.0f;

    /// The rule rejecting input samples, or null when every sample participates. Read by the first
    /// pass alone: every later pass reads an intermediate this family produced, in which every
    /// sample is defined.
    const KernelValidityView* readValidity = nullptr;

    /// The plane whose samples decide whether an output is written, or null to write every output.
    /// Read by the last pass alone.
    const PlaneView<const float>* writeGate = nullptr;

    /// The rule applied to `writeGate`, or null when there is no gate.
    const KernelValidityView* writeValidity = nullptr;
};

/// Whether one output position is written.
///
/// @param pass what the pass gates on.
/// @param gate the accessor over the gating plane, or null when every output is written.
/// @param x the column.
/// @param y the row.
/// @return true when the output is written.
[[nodiscard]] bool passWrites(const BoxPass& pass, const reference_detail::ValidityAccessor* gate, std::uint32_t x,
                              std::uint32_t y) noexcept
{
    if (gate == nullptr || pass.writeGate == nullptr)
    {
        return true;
    }
    return gate->accepts((*pass.writeGate->row(y))[x], x, y);
}

/// Averages a moving horizontal window over a plane, as a running sum.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the plane to read.
/// @param output the plane to write, of the same extent and disjoint from the input.
/// @param pass the window, border, validity and gate.
/// @param written how many outputs were produced, added to.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status boxPassHorizontal(const ExecutionContext& execution, PlaneView<const float> input,
                                       PlaneView<float> output, const BoxPass& pass,
                                       std::atomic<std::uint64_t>& written)
{
    const std::int64_t width = static_cast<std::int64_t>(input.width());
    const std::int64_t radius = static_cast<std::int64_t>(pass.radius);
    const double inverseWindow = 1.0 / static_cast<double>(2 * radius + 1);
    return parallelFor(execution, input.height(), boxRowCost(input.width()), [&](TaskRange range) {
        std::optional<reference_detail::ValidityAccessor> accepts;
        if (pass.readValidity != nullptr)
        {
            accepts.emplace(*pass.readValidity);
        }
        std::optional<reference_detail::ValidityAccessor> gate;
        if (pass.writeValidity != nullptr)
        {
            gate.emplace(*pass.writeValidity);
        }
        const bool gated = pass.writeGate != nullptr && gate.has_value();
        std::uint64_t produced = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<const float> row = *input.row(y);
            const std::span<float> answer = *output.row(y);
            const reference_detail::ValidityAccessor* const accepting = accepts.has_value() ? &*accepts : nullptr;
            const auto sampleInside = [&](std::int64_t x) {
                return resolvedSample(row.data(), static_cast<std::uint32_t>(x), y, accepting, pass.constantBorder,
                                      pass.invalidNeighbor);
            };
            const auto sampleAt = [&](std::int64_t x) {
                const std::int64_t resolved = kernelBorderIndex(x, width, pass.border);
                if (resolved == width)
                {
                    return static_cast<double>(pass.constantBorder);
                }
                return sampleInside(resolved);
            };

            double accumulator = 0.0;
            for (std::int64_t offset = -radius; offset <= radius; ++offset)
            {
                accumulator += sampleAt(offset);
            }
            // The border rule is asked only where a coordinate is actually outside the row. Testing
            // that here rather than inside the resolver is what keeps the interior of the pass a
            // predictable branch and an indexed read, which is the whole reason a running sum is
            // cheaper than a tap array in the first place.
            for (std::int64_t x = 0; x < width; ++x)
            {
                if (!gated || passWrites(pass, gate.has_value() ? &*gate : nullptr, static_cast<std::uint32_t>(x), y))
                {
                    answer[static_cast<std::size_t>(x)] = static_cast<float>(accumulator * inverseWindow);
                    ++produced;
                }
                const std::int64_t entering = x + radius + 1;
                const std::int64_t leaving = x - radius;
                accumulator += (entering < width ? sampleInside(entering) : sampleAt(entering)) -
                               (leaving >= 0 ? sampleInside(leaving) : sampleAt(leaving));
            }
        }
        written.fetch_add(produced, std::memory_order_relaxed);
        return Status::success();
    });
}

/// Averages a moving vertical window over a plane, as a running sum over a band of columns.
///
/// @param execution the operation policy and engine-context lease.
/// @param input the plane to read.
/// @param output the plane to write, of the same extent and disjoint from the input.
/// @param pass the window, border, validity and gate.
/// @param written how many outputs were produced, added to.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status boxPassVertical(const ExecutionContext& execution, PlaneView<const float> input,
                                     PlaneView<float> output, const BoxPass& pass, std::atomic<std::uint64_t>& written)
{
    const std::uint32_t width = input.width();
    const std::int64_t height = static_cast<std::int64_t>(input.height());
    const std::int64_t radius = static_cast<std::int64_t>(pass.radius);
    const double inverseWindow = 1.0 / static_cast<double>(2 * radius + 1);
    const std::uint64_t bandCount = (static_cast<std::uint64_t>(width) + kBoxColumnBand - 1u) / kBoxColumnBand;
    return parallelFor(
        execution, bandCount, boxBandCost((std::min)(width, kBoxColumnBand), input.height()), [&](TaskRange range) {
            std::optional<reference_detail::ValidityAccessor> accepts;
            if (pass.readValidity != nullptr)
            {
                accepts.emplace(*pass.readValidity);
            }
            std::optional<reference_detail::ValidityAccessor> gate;
            if (pass.writeValidity != nullptr)
            {
                gate.emplace(*pass.writeValidity);
            }
            const bool gated = pass.writeGate != nullptr && gate.has_value();
            std::array<double, kBoxColumnBand> accumulators{};
            std::uint64_t produced = 0u;
            for (std::uint64_t band = range.begin; band < range.end; ++band)
            {
                const std::uint32_t first = static_cast<std::uint32_t>(band * kBoxColumnBand);
                const std::uint32_t last = (std::min)(width, first + kBoxColumnBand);
                const auto rowAt = [&](std::int64_t y) -> const float* {
                    const std::int64_t resolved = kernelBorderIndex(y, height, pass.border);
                    if (resolved == height)
                    {
                        return nullptr;
                    }
                    return input.row(static_cast<std::uint32_t>(resolved))->data();
                };
                const auto rowIndexAt = [&](std::int64_t y) -> std::uint32_t {
                    const std::int64_t resolved = kernelBorderIndex(y, height, pass.border);
                    return resolved == height ? 0u : static_cast<std::uint32_t>(resolved);
                };

                for (std::uint32_t column = first; column < last; ++column)
                {
                    accumulators[column - first] = 0.0;
                }
                for (std::int64_t offset = -radius; offset <= radius; ++offset)
                {
                    const float* const row = rowAt(offset);
                    const std::uint32_t sourceRow = rowIndexAt(offset);
                    for (std::uint32_t column = first; column < last; ++column)
                    {
                        accumulators[column - first] +=
                            resolvedSample(row, column, sourceRow, accepts.has_value() ? &*accepts : nullptr,
                                           pass.constantBorder, pass.invalidNeighbor);
                    }
                }

                const reference_detail::ValidityAccessor* const accepting = accepts.has_value() ? &*accepts : nullptr;
                for (std::int64_t y = 0; y < height; ++y)
                {
                    const std::span<float> answer = *output.row(static_cast<std::uint32_t>(y));
                    if (gated)
                    {
                        for (std::uint32_t column = first; column < last; ++column)
                        {
                            if (passWrites(pass, gate.has_value() ? &*gate : nullptr, column,
                                           static_cast<std::uint32_t>(y)))
                            {
                                answer[column] = static_cast<float>(accumulators[column - first] * inverseWindow);
                                ++produced;
                            }
                        }
                    }
                    else
                    {
                        for (std::uint32_t column = first; column < last; ++column)
                        {
                            answer[column] = static_cast<float>(accumulators[column - first] * inverseWindow);
                        }
                        produced += last - first;
                    }
                    // Both rows are resolved once for the whole band rather than once per column, so the
                    // border rule costs a row of the pass rather than a sample of it, and the ordinary
                    // case is two indexed reads and an add.
                    const float* const entering = rowAt(y + radius + 1);
                    const std::uint32_t enteringRow = rowIndexAt(y + radius + 1);
                    const float* const leaving = rowAt(y - radius);
                    const std::uint32_t leavingRow = rowIndexAt(y - radius);
                    if (accepting == nullptr && entering != nullptr && leaving != nullptr)
                    {
                        for (std::uint32_t column = first; column < last; ++column)
                        {
                            accumulators[column - first] +=
                                static_cast<double>(entering[column]) - static_cast<double>(leaving[column]);
                        }
                    }
                    else
                    {
                        for (std::uint32_t column = first; column < last; ++column)
                        {
                            accumulators[column - first] += resolvedSample(entering, column, enteringRow, accepting,
                                                                           pass.constantBorder, pass.invalidNeighbor) -
                                                            resolvedSample(leaving, column, leavingRow, accepting,
                                                                           pass.constantBorder, pass.invalidNeighbor);
                        }
                    }
                }
            }
            written.fetch_add(produced, std::memory_order_relaxed);
            return Status::success();
        });
}

/// One Float32 box pass over a contiguous line.
///
/// The accumulator and the reciprocal are Float32 on purpose, in a fixed evaluation order: this is a
/// latency path, and widening them would spend the time it saves.
///
/// Both ends mirror about the edge sample without repeating it, as
/// `KernelBorderMode::ReflectExcludingEdge` does, and read the mirrored index directly, so the
/// caller must hold `radius` below half of `length`; `fusedRealtimePreviewApplicable` does.
void fusedBoxPassLine(const float* input, float* output, int length, int radius) noexcept
{
    const double inverse = 1.0f / static_cast<float>(radius + radius + 1);
    float accumulator = 0.0f;
    int target = 0;
    int left = -radius - 1;
    int right = radius;
    for (int index = left; index < 0; ++index)
    {
        accumulator += input[-index];
    }
    for (int index = 0; index < right; ++index)
    {
        accumulator += input[index];
    }
    for (; left < 0; ++right, ++target, ++left)
    {
        accumulator += input[right] - input[-left];
        output[target] = static_cast<float>(accumulator * inverse);
    }
    for (; right < length; ++right, ++target, ++left)
    {
        accumulator += input[right] - input[left];
        output[target] = static_cast<float>(accumulator * inverse);
    }
    for (; target < length; ++right, ++target, ++left)
    {
        accumulator += input[2 * length - 2 - right] - input[left];
        output[target] = static_cast<float>(accumulator * inverse);
    }
}

/// State of one cache-resident vertical box pass over one band.
struct FusedColumnPass
{
    /// Window radius.
    int radius = 0;

    /// Image height.
    int height = 0;

    /// Columns in this band.
    int columns = 0;

    /// Next output row.
    int target = 0;

    /// Reciprocal window width.
    double inverse = 0.0;

    /// One Float32 running sum per column.
    float* accumulators = nullptr;

    /// Initializes the top reflected window.
    ///
    /// @tparam RowAccessor callable returning the first sample of a requested band row.
    /// @param rowAt the source-row accessor.
    template <typename RowAccessor> void initialize(RowAccessor rowAt) noexcept
    {
        std::fill_n(accumulators, columns, 0.0f);
        for (int row = -radius - 1; row < 0; ++row)
        {
            const float* values = rowAt(-row);
            for (int column = 0; column < columns; ++column)
            {
                accumulators[column] += values[column];
            }
        }
        for (int row = 0; row < radius; ++row)
        {
            const float* values = rowAt(row);
            for (int column = 0; column < columns; ++column)
            {
                accumulators[column] += values[column];
            }
        }
        target = 0;
    }

    /// Produces the next row.
    ///
    /// @tparam RowAccessor callable returning the first sample of a requested band row.
    /// @param rowAt the source-row accessor.
    /// @param output where this band of the next row goes.
    template <typename RowAccessor> void step(RowAccessor rowAt, float* output) noexcept
    {
        const int rightIndex = target + radius;
        const int leftIndex = target - radius - 1;
        const float* right = rowAt(rightIndex < height ? rightIndex : 2 * height - 2 - rightIndex);
        const float* left = rowAt(leftIndex < 0 ? -leftIndex : leftIndex);
        for (int column = 0; column < columns; ++column)
        {
            accumulators[column] += right[column] - left[column];
            output[column] = static_cast<float>(accumulators[column] * inverse);
        }
        ++target;
    }
};

/// Scratch floats needed by one fused vertical worker for the widest band.
[[nodiscard]] std::uint64_t fusedVerticalScratchFloats(std::span<const std::uint32_t> radii) noexcept
{
    const std::uint64_t firstRing = 2u * radii[1] + 3u;
    const std::uint64_t secondRing = 2u * radii[2] + 3u;
    return static_cast<std::uint64_t>(kBoxColumnBand) * (3u + firstRing + secondRing);
}

/// Runs all three vertical passes while their intermediate rows remain in cache.
///
/// The first pass writes into a ring of `2 * radii[1] + 3` rows and the second into a ring of
/// `2 * radii[2] + 3`: a ring holds the rows the pass after it can still read. A later pass
/// produces a row as soon as the pass before it has produced the last row its window needs, so one
/// band of one channel flows through all three passes in a single walk down the image. `scratch`
/// holds the three accumulator rows and both rings, `fusedVerticalScratchFloats` samples in all,
/// and the caller holds every radius below half the height, as the mirrored rows require.
void fusedVerticalPasses(const float* input, float* destination, int width, int height, int firstColumn, int columns,
                         std::span<const std::uint32_t> radii, float* scratch) noexcept
{
    const int firstRadius = static_cast<int>(radii[0]);
    const int secondRadius = static_cast<int>(radii[1]);
    const int thirdRadius = static_cast<int>(radii[2]);
    const int firstRingRows = 2 * secondRadius + 3;
    const int secondRingRows = 2 * thirdRadius + 3;
    const std::size_t bandColumns = static_cast<std::size_t>(columns);
    float* firstAccumulators = scratch;
    float* secondAccumulators = firstAccumulators + bandColumns;
    float* thirdAccumulators = secondAccumulators + bandColumns;
    float* firstRing = thirdAccumulators + bandColumns;
    float* secondRing = firstRing + bandColumns * static_cast<std::size_t>(firstRingRows);

    FusedColumnPass first{firstRadius,      height, columns, 0, 1.0f / static_cast<float>(2 * firstRadius + 1),
                          firstAccumulators};
    FusedColumnPass second{secondRadius,      height, columns, 0, 1.0f / static_cast<float>(2 * secondRadius + 1),
                           secondAccumulators};
    FusedColumnPass third{thirdRadius,      height, columns, 0, 1.0f / static_cast<float>(2 * thirdRadius + 1),
                          thirdAccumulators};

    const auto sourceRow = [&](int row) {
        return input + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + firstColumn;
    };
    const auto firstRingRow = [&](int row) {
        return firstRing + static_cast<std::size_t>(row % firstRingRows) * bandColumns;
    };
    const auto secondRingRow = [&](int row) {
        return secondRing + static_cast<std::size_t>(row % secondRingRows) * bandColumns;
    };

    first.initialize(sourceRow);
    int secondRow = 0;
    int thirdRow = 0;
    bool secondReady = false;
    bool thirdReady = false;
    for (int firstRow = 0; firstRow < height; ++firstRow)
    {
        first.step(sourceRow, firstRingRow(firstRow));
        while (secondRow < height && (secondRow + secondRadius + 1 <= firstRow || firstRow == height - 1))
        {
            if (!secondReady)
            {
                second.initialize(firstRingRow);
                secondReady = true;
            }
            second.step(firstRingRow, secondRingRow(secondRow));
            ++secondRow;
            while (thirdRow < height && (thirdRow + thirdRadius + 1 <= secondRow - 1 || secondRow == height))
            {
                if (!thirdReady)
                {
                    third.initialize(secondRingRow);
                    thirdReady = true;
                }
                third.step(secondRingRow, destination +
                                              static_cast<std::size_t>(thirdRow) * static_cast<std::size_t>(width) +
                                              firstColumn);
                ++thirdRow;
            }
        }
    }
}

/// Whether the common planar preview can use the fused Float32 implementation.
[[nodiscard]] bool fusedRealtimePreviewApplicable(SampleType sampleType, const ImageLayout& sourceLayout,
                                                  const ImageLayout& destinationLayout, const GaussianPlan& plan,
                                                  const SpatialSelection& selection) noexcept
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    if (sampleType != SampleType::Float32 || selection.mask != nullptr ||
        selection.validity.rule() != ValidityRule::AllValid ||
        plan.request().borders.border != KernelBorderMode::ReflectExcludingEdge ||
        plan.request().output.writeBack != ConversionPolicy{.domain = DomainMapping::CodeValue} ||
        sourceLayout.samplesPerPixel() != 1u || destinationLayout.samplesPerPixel() != 1u ||
        sourceLayout.rowStrideBytes() != static_cast<std::uint64_t>(width) * sizeof(float) ||
        destinationLayout.rowStrideBytes() != static_cast<std::uint64_t>(width) * sizeof(float) ||
        width > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
        height > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
    {
        return false;
    }
    for (const std::uint32_t radius : plan.horizontalBoxRadii())
    {
        if (radius >= width / 2u)
        {
            return false;
        }
    }
    for (const std::uint32_t radius : plan.verticalBoxRadii())
    {
        if (radius >= height / 2u)
        {
            return false;
        }
    }
    return true;
}

/// Whether the common production blur can use the symmetric Float32 convolution.
///
/// The row kernels exist only in the vector dispatch tables, read a tightly packed Float32 plane,
/// mirror without consulting a border rule or a validity rule, and read the mirrored index
/// directly, which is why each radius has to stay below half its extent.
[[nodiscard]] bool symmetricExactGaussianApplicable(const ExecutionContext& execution, SampleType sampleType,
                                                    const ImageLayout& sourceLayout,
                                                    const ImageLayout& destinationLayout, const GaussianPlan& plan,
                                                    const SpatialSelection& selection) noexcept
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    return symmetricGaussianHorizontalRunFor(execution) != nullptr &&
           symmetricGaussianVerticalRunFor(execution) != nullptr && sampleType == SampleType::Float32 &&
           selection.mask == nullptr && selection.validity.rule() == ValidityRule::AllValid &&
           plan.request().borders.border == KernelBorderMode::ReflectExcludingEdge &&
           plan.request().output.writeBack == ConversionPolicy{.domain = DomainMapping::CodeValue} &&
           sourceLayout.samplesPerPixel() == 1u && destinationLayout.samplesPerPixel() == 1u &&
           sourceLayout.rowStrideBytes() == static_cast<std::uint64_t>(width) * sizeof(float) &&
           destinationLayout.rowStrideBytes() == static_cast<std::uint64_t>(width) * sizeof(float) &&
           plan.horizontalRadius() < width / 2u && plan.verticalRadius() < height / 2u;
}

/// Cost of one row of a symmetric Gaussian pass.
[[nodiscard]] TaskCost symmetricGaussianRowCost(std::uint32_t width, std::uint32_t radius) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * (2u * radius + 2u) * sizeof(float),
                    .operationsPerItem = samples * (3u * radius + 1u)};
}

/// Applies one symmetric horizontal Gaussian pass without materialising a padded row.
[[nodiscard]] Status symmetricGaussianHorizontal(const ExecutionContext& execution, PlaneView<const float> input,
                                                 PlaneView<float> output, std::span<const float> taps,
                                                 std::uint32_t radius)
{
    const std::uint32_t width = input.width();
    const SymmetricGaussianHorizontalRunFn run = symmetricGaussianHorizontalRunFor(execution);
    return parallelFor(execution, input.height(), symmetricGaussianRowCost(width, radius),
                       [&](TaskRange range) -> Status {
                           for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
                           {
                               const std::span<const float> source = *input.row(static_cast<std::uint32_t>(y64));
                               const std::span<float> destination = *output.row(static_cast<std::uint32_t>(y64));
                               run(source.data(), destination.data(), width, taps.data(), radius);
                           }
                           return Status::success();
                       });
}

/// Applies one symmetric vertical Gaussian pass, resolving each pair of rows once.
[[nodiscard]] Status symmetricGaussianVertical(const ExecutionContext& execution, PlaneView<const float> input,
                                               PlaneView<float> output, std::span<const float> taps,
                                               std::uint32_t radius)
{
    const std::uint32_t width = input.width();
    const std::uint32_t height = input.height();
    const SymmetricGaussianVerticalRunFn run = symmetricGaussianVerticalRunFor(execution);
    const float* const source = input.row(0u)->data();
    return parallelFor(execution, input.height(), symmetricGaussianRowCost(width, radius),
                       [&](TaskRange range) -> Status {
                           for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
                           {
                               const auto y = static_cast<std::uint32_t>(y64);
                               const std::span<float> destination = *output.row(y);
                               run(source, destination.data(), width, height, y, taps.data(), radius);
                           }
                           return Status::success();
                       });
}

/// Runs the exact sampled Gaussian through a Float32 symmetric implementation.
[[nodiscard]] Result<SpatialFilterResult>
runSymmetricExactGaussian(const ExecutionContext& execution, const void* source, const ImageLayout& sourceLayout,
                          void* destination, const ImageLayout& destinationLayout, const GaussianPlan& plan,
                          const SpatialGeometry& geometry, std::span<const double> horizontal,
                          std::span<const double> vertical, const char* operation)
{
    std::array<float, kMaxStencilTaps> horizontalFloat{};
    std::array<float, kMaxStencilTaps> verticalFloat{};
    std::transform(horizontal.begin(), horizontal.end(), horizontalFloat.begin(),
                   [](double coefficient) { return static_cast<float>(coefficient); });
    std::transform(vertical.begin(), vertical.end(), verticalFloat.begin(),
                   [](double coefficient) { return static_cast<float>(coefficient); });

    Result<spatial_detail::SpatialPlaneBuffer> intermediate =
        spatial_detail::SpatialPlaneBuffer::own(execution, geometry, operation);
    if (!intermediate.hasValue())
    {
        return intermediate.status();
    }

    const auto* sourceBytes = static_cast<const std::uint8_t*>(source);
    auto* destinationBytes = static_cast<std::uint8_t*>(destination);
    const PlaneView<float> temporary = intermediate->view();
    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const auto* channelSource = reinterpret_cast<const float*>(sourceBytes + static_cast<std::uint64_t>(channel) *
                                                                                     sourceLayout.planeStrideBytes());
        auto* channelDestination = reinterpret_cast<float*>(
            destinationBytes + static_cast<std::uint64_t>(channel) * destinationLayout.planeStrideBytes());
        const std::span<const float> sourceSamples{channelSource,
                                                   static_cast<std::size_t>(geometry.width) * geometry.height};
        const std::span<float> destinationSamples{channelDestination,
                                                  static_cast<std::size_t>(geometry.width) * geometry.height};
        const std::optional<PlaneView<const float>> sourcePlane = PlaneView<const float>::create(
            sourceSamples, geometry.width, geometry.height, 1u, sourceLayout.rowStrideBytes());
        const std::optional<PlaneView<float>> destinationPlane = PlaneView<float>::create(
            destinationSamples, geometry.width, geometry.height, 1u, destinationLayout.rowStrideBytes());
        if (!sourcePlane.has_value() || !destinationPlane.has_value())
        {
            return spatialFailure(ErrorCode::InvalidArgument, operation,
                                  "a planar Gaussian channel could not be described as a plane");
        }
        Status status = symmetricGaussianHorizontal(execution, *sourcePlane, temporary,
                                                    std::span<const float>{horizontalFloat.data(), horizontal.size()},
                                                    plan.horizontalRadius());
        if (status.failed())
        {
            return status;
        }
        status = symmetricGaussianVertical(execution, temporary, *destinationPlane,
                                           std::span<const float>{verticalFloat.data(), vertical.size()},
                                           plan.verticalRadius());
        if (status.failed())
        {
            return status;
        }
    }

    const std::uint64_t samples = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    SpatialFilterResult result{};
    result.run.selection = ValidityCount{.valid = samples, .total = samples};
    result.run.outputsWritten = samples;
    result.channelCount = geometry.channelCount;
    result.horizontalRadius = plan.horizontalRadius();
    result.verticalRadius = plan.verticalRadius();
    result.imagePasses = 2u;
    result.workingBytes = intermediate->bytes();
    result.plannedWorkers =
        (std::max)(plannedWorkerCount(execution, geometry.height,
                                      symmetricGaussianRowCost(
                                          geometry.width, (std::max)(plan.horizontalRadius(), plan.verticalRadius()))),
                   1u);
    result.precision = ComputePrecision::Float32;
    result.readInPlace = true;
    result.wroteInPlace = true;
    return result;
}

/// Runs the fused Float32 preview over every planar channel.
[[nodiscard]] Result<SpatialFilterResult>
runFusedRealtimePreviewGaussian(const ExecutionContext& execution, const void* source, const ImageLayout& sourceLayout,
                                void* destination, const ImageLayout& destinationLayout, const GaussianPlan& plan,
                                const SpatialGeometry& geometry, const char* operation)
{
    Result<spatial_detail::SpatialPlaneBuffer> intermediate =
        spatial_detail::SpatialPlaneBuffer::own(execution, geometry, operation);
    if (!intermediate.hasValue())
    {
        return intermediate.status();
    }

    const std::uint64_t bandCount = (static_cast<std::uint64_t>(geometry.width) + kBoxColumnBand - 1u) / kBoxColumnBand;
    const std::uint32_t horizontalWorkers =
        (std::max)(plannedWorkerCount(execution, geometry.height, boxRowCost(geometry.width)), 1u);
    const std::uint32_t verticalWorkers =
        (std::max)(plannedWorkerCount(execution, bandCount,
                                      boxBandCost((std::min)(geometry.width, kBoxColumnBand), geometry.height)),
                   1u);
    const std::uint64_t horizontalScratch = 2u * static_cast<std::uint64_t>(geometry.width);
    const std::uint64_t verticalScratch = fusedVerticalScratchFloats(plan.verticalBoxRadii());
    const std::uint64_t scratchStride = (std::max)(horizontalScratch, verticalScratch);
    const std::uint32_t scratchWorkers = (std::max)(horizontalWorkers, verticalWorkers);
    if (scratchStride == 0u || scratchStride > (std::numeric_limits<std::uint32_t>::max)() ||
        scratchStride > (std::numeric_limits<std::uint64_t>::max)() / scratchWorkers / sizeof(float))
    {
        return spatialFailure(ErrorCode::OutOfRange, operation,
                              "the fused preview's worker-private scratch cannot be represented");
    }
    const SpatialGeometry scratchGeometry{.width = static_cast<std::uint32_t>(scratchStride),
                                          .height = scratchWorkers,
                                          .channelCount = 1u,
                                          .planeBytes = scratchStride * scratchWorkers * sizeof(float)};
    Result<spatial_detail::SpatialPlaneBuffer> workerScratch =
        spatial_detail::SpatialPlaneBuffer::own(execution, scratchGeometry, operation);
    if (!workerScratch.hasValue())
    {
        return workerScratch.status();
    }

    const int width = static_cast<int>(geometry.width);
    const int height = static_cast<int>(geometry.height);
    const auto* sourceBytes = static_cast<const std::uint8_t*>(source);
    auto* destinationBytes = static_cast<std::uint8_t*>(destination);
    const std::span<const std::uint32_t> horizontalRadii = plan.horizontalBoxRadii();
    const std::span<const std::uint32_t> verticalRadii = plan.verticalBoxRadii();
    const PlaneView<float> scratchPlane = workerScratch->view();
    const PlaneView<float> horizontalPlane = intermediate->view();
    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const auto* channelSource = reinterpret_cast<const float*>(sourceBytes + static_cast<std::uint64_t>(channel) *
                                                                                     sourceLayout.planeStrideBytes());
        auto* channelDestination = reinterpret_cast<float*>(
            destinationBytes + static_cast<std::uint64_t>(channel) * destinationLayout.planeStrideBytes());
        const Status horizontal =
            parallelForWorkers(execution, geometry.height, boxRowCost(geometry.width), [&](WorkerTask task) -> Status {
                std::span<float> scratch = *scratchPlane.row(task.workerSlot);
                float* first = scratch.data();
                float* second = first + geometry.width;
                for (std::uint64_t y = task.range.begin; y < task.range.end; ++y)
                {
                    const float* input = channelSource + static_cast<std::size_t>(y) * geometry.width;
                    float* output = horizontalPlane.row(static_cast<std::uint32_t>(y))->data();
                    fusedBoxPassLine(input, first, width, static_cast<int>(horizontalRadii[0]));
                    fusedBoxPassLine(first, second, width, static_cast<int>(horizontalRadii[1]));
                    fusedBoxPassLine(second, output, width, static_cast<int>(horizontalRadii[2]));
                }
                return Status::success();
            });
        if (horizontal.failed())
        {
            return horizontal;
        }

        const Status vertical = parallelForWorkers(
            execution, bandCount, boxBandCost((std::min)(geometry.width, kBoxColumnBand), geometry.height),
            [&](WorkerTask task) -> Status {
                float* scratch = scratchPlane.row(task.workerSlot)->data();
                for (std::uint64_t band = task.range.begin; band < task.range.end; ++band)
                {
                    const int firstColumn = static_cast<int>(band * kBoxColumnBand);
                    const int columns = (std::min)(static_cast<int>(kBoxColumnBand), width - firstColumn);
                    fusedVerticalPasses(horizontalPlane.row(0u)->data(), channelDestination, width, height, firstColumn,
                                        columns, verticalRadii, scratch);
                }
                return Status::success();
            });
        if (vertical.failed())
        {
            return vertical;
        }
    }

    const std::uint64_t samples = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    SpatialFilterResult result{};
    result.run.selection.total = samples;
    result.run.selection.valid = samples;
    result.run.outputsWritten = samples;
    result.channelCount = geometry.channelCount;
    result.horizontalRadius = plan.horizontalRadius();
    result.verticalRadius = plan.verticalRadius();
    result.imagePasses = 2u * kGaussianBoxPasses;
    result.workingBytes = intermediate->bytes() + workerScratch->bytes();
    result.plannedWorkers = (std::max)(horizontalWorkers, verticalWorkers);
    result.precision = ComputePrecision::Float32;
    result.readInPlace = true;
    result.wroteInPlace = true;
    return result;
}

} // namespace

const char* gaussianMethodName(GaussianMethod method) noexcept
{
    switch (method)
    {
    case GaussianMethod::SeparableTaps:
        return "SeparableTaps";
    case GaussianMethod::RepeatedBox:
        return "RepeatedBox";
    }
    return "UnknownGaussianMethod";
}

GaussianPlan::GaussianPlan(const GaussianRequest& request) noexcept : m_request(request)
{
    if (request.method == GaussianMethod::RepeatedBox)
    {
        m_realizedSigmaX = boxRadiiForSigma(request.sigmaX, m_horizontalBoxRadii);
        m_realizedSigmaY = boxRadiiForSigma(request.sigmaY, m_verticalBoxRadii);
        for (const std::uint32_t radius : m_horizontalBoxRadii)
        {
            m_horizontalRadius += radius;
        }
        for (const std::uint32_t radius : m_verticalBoxRadii)
        {
            m_verticalRadius += radius;
        }
        return;
    }
    m_realizedSigmaX = request.sigmaX;
    m_realizedSigmaY = request.sigmaY;
    const std::uint64_t horizontal = truncatedRadius(request.sigmaX, request.truncation);
    const std::uint64_t vertical = truncatedRadius(request.sigmaY, request.truncation);
    m_horizontalRadius = static_cast<std::uint32_t>((std::min)(horizontal, std::uint64_t{0xFFFFFFFFu}));
    m_verticalRadius = static_cast<std::uint32_t>((std::min)(vertical, std::uint64_t{0xFFFFFFFFu}));
}

const GaussianRequest& GaussianPlan::request() const noexcept
{
    return m_request;
}

GaussianMethod GaussianPlan::method() const noexcept
{
    return m_request.method;
}

std::uint32_t GaussianPlan::horizontalRadius() const noexcept
{
    return m_horizontalRadius;
}

std::uint32_t GaussianPlan::verticalRadius() const noexcept
{
    return m_verticalRadius;
}

std::span<const std::uint32_t> GaussianPlan::horizontalBoxRadii() const noexcept
{
    return std::span<const std::uint32_t>{m_horizontalBoxRadii};
}

std::span<const std::uint32_t> GaussianPlan::verticalBoxRadii() const noexcept
{
    return std::span<const std::uint32_t>{m_verticalBoxRadii};
}

double GaussianPlan::realizedSigmaX() const noexcept
{
    return m_realizedSigmaX;
}

double GaussianPlan::realizedSigmaY() const noexcept
{
    return m_realizedSigmaY;
}

std::uint64_t GaussianPlan::imagePasses() const noexcept
{
    return m_request.method == GaussianMethod::RepeatedBox ? 2u * kGaussianBoxPasses : 2u;
}

namespace
{

/// The checks both plan builders share, applied to a request whose method each builder has fixed.
///
/// @param request the request, carrying the method the calling builder allows.
/// @param operation the entry point named in any failure.
/// @return the plan, or the numeric, border, output-policy or radius refusal.
[[nodiscard]] Result<GaussianPlan> planGaussianForMethod(const GaussianRequest& request, const char* operation)
{
    if (!isGaussianMethod(request.method))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "the blur method is not one this build implements");
    }
    if (!std::isfinite(request.sigmaX) || !std::isfinite(request.sigmaY) || request.sigmaX <= kMinimumGaussianSigma ||
        request.sigmaY <= kMinimumGaussianSigma)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a sigma is a finite width above the identity threshold; a blur that does nothing is "
                              "asked for by not blurring");
    }
    if (!std::isfinite(request.truncation) || request.truncation < 1.0)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a truncation keeps at least one sigma of the density");
    }
    if (!isKernelBorderMode(request.borders.border) || request.borders.border == KernelBorderMode::NotApplicable)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a blur needs a border rule that answers a coordinate outside the image");
    }
    if (!std::isfinite(request.borders.constantBorder) || !std::isfinite(request.borders.invalidNeighbor))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a border or substitute value that is not finite would spread through the whole "
                              "neighbourhood it touches");
    }
    if (!isOutputPrecisionContract(request.output.contract))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "the output contract is not one this engine knows");
    }
    const Status policy = checkConversionPolicy(request.output.writeBack, operation);
    if (policy.failed())
    {
        return policy;
    }
    if (request.output.writeBack.domain != DomainMapping::CodeValue)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a blur carries its samples across as the numbers they are; a full-scale domain "
                              "mapping would make it a blur and a domain change at once");
    }

    const GaussianPlan plan{request};
    const std::uint32_t widest = (std::max)(plan.horizontalRadius(), plan.verticalRadius());
    if (widest > kMaxStencilRadius)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, operation,
                                   "the sigma this blur was asked for reaches further than one pass may", widest,
                                   kMaxStencilRadius);
    }
    return plan;
}

} // namespace

Result<GaussianPlan> planGaussianBlur(const GaussianRequest& request)
{
    if (request.method != GaussianMethod::SeparableTaps)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, kPlanOperation,
                              "production Gaussian blur uses sampled taps; the repeated-box approximation is "
                              "available only through planRealtimePreviewGaussianBlur");
    }
    return planGaussianForMethod(request, kPlanOperation);
}

Result<GaussianPlan> planRealtimePreviewGaussianBlur(const RealtimePreviewGaussianRequest& request)
{
    const GaussianRequest retained{.sigmaX = request.sigmaX,
                                   .sigmaY = request.sigmaY,
                                   .truncation = kDefaultGaussianTruncation,
                                   .method = GaussianMethod::RepeatedBox,
                                   .borders = request.borders,
                                   .output = request.output};
    return planGaussianForMethod(retained, kPreviewPlanOperation);
}

Result<std::uint32_t> writeGaussianTaps(double sigma, std::uint32_t radius, std::span<double> taps)
{
    if (!std::isfinite(sigma) || sigma <= kMinimumGaussianSigma)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kTapsOperation,
                              "a sigma is a finite width above the identity threshold");
    }
    if (radius > kMaxStencilRadius)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kTapsOperation, "a pass is wider than one may be", radius,
                                   kMaxStencilRadius);
    }
    const std::uint32_t tapCount = 2u * radius + 1u;
    if (taps.size() != tapCount)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, kTapsOperation,
                              "a tap array holds exactly one coefficient per sample of the kernel");
    }

    const double scale = -0.5 / (sigma * sigma);
    double sum = 0.0;
    for (std::uint32_t index = 0u; index < tapCount; ++index)
    {
        const double offset = static_cast<double>(index) - static_cast<double>(radius);
        const double density = std::exp(scale * offset * offset);
        taps[index] = density;
        sum += density;
    }
    // Renormalizing after truncation is what makes the filter preserve a flat surface exactly. The
    // sum is never zero: the centre tap of a Gaussian is one.
    for (std::uint32_t index = 0u; index < tapCount; ++index)
    {
        taps[index] /= sum;
    }
    return tapCount;
}

namespace
{

/// Runs a plan its public entry point has already checked: the exact taps, through the symmetric
/// Float32 path where it applies and the stencil family otherwise, or the repeated boxes, through
/// the fused form where it applies and the general running sums otherwise.
[[nodiscard]] Result<SpatialFilterResult>
blurImageGaussianImplementation(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                const ImageLayout& sourceLayout, void* destination,
                                const ImageLayout& destinationLayout, const GaussianPlan& plan,
                                const SpatialSelection& selection, const char* operation)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, operation, geometry);
    if (ready.failed())
    {
        return ready;
    }

    if (plan.method() == GaussianMethod::RepeatedBox &&
        fusedRealtimePreviewApplicable(sampleType, sourceLayout, destinationLayout, plan, selection))
    {
        return runFusedRealtimePreviewGaussian(execution, source, sourceLayout, destination, destinationLayout, plan,
                                               geometry, operation);
    }

    if (plan.method() == GaussianMethod::SeparableTaps)
    {
        std::array<double, kMaxStencilTaps> horizontal{};
        std::array<double, kMaxStencilTaps> vertical{};
        const std::uint32_t horizontalCount = 2u * plan.horizontalRadius() + 1u;
        const std::uint32_t verticalCount = 2u * plan.verticalRadius() + 1u;
        const Result<std::uint32_t> alongRow = writeGaussianTaps(plan.request().sigmaX, plan.horizontalRadius(),
                                                                 std::span<double>{horizontal.data(), horizontalCount});
        if (!alongRow.hasValue())
        {
            return alongRow.status();
        }
        const Result<std::uint32_t> downColumn = writeGaussianTaps(plan.request().sigmaY, plan.verticalRadius(),
                                                                   std::span<double>{vertical.data(), verticalCount});
        if (!downColumn.hasValue())
        {
            return downColumn.status();
        }
        if (symmetricExactGaussianApplicable(execution, sampleType, sourceLayout, destinationLayout, plan, selection))
        {
            Result<SpatialFilterResult> accelerated =
                runSymmetricExactGaussian(execution, source, sourceLayout, destination, destinationLayout, plan,
                                          geometry, std::span<const double>{horizontal.data(), horizontalCount},
                                          std::span<const double>{vertical.data(), verticalCount}, operation);
            if (accelerated.hasValue())
            {
                return accelerated;
            }
            if (accelerated.status().code() != ErrorCode::MemoryBudgetExceeded &&
                accelerated.status().code() != ErrorCode::AllocationFailed)
            {
                return accelerated.status();
            }
            // The optimized path keeps one full Float32 plane. The generic separable path below
            // tiles its Float64 working set, so a constrained context can still complete rather
            // than turning an optimization's preferred allocation into a new functional limit.
        }
        const SeparableConvolutionRequest request{
            .horizontalTaps = std::span<const double>{horizontal.data(), horizontalCount},
            .verticalTaps = std::span<const double>{vertical.data(), verticalCount},
            .borders = plan.request().borders,
            .output = plan.request().output};
        return convolveImageSeparable(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                      request, selection);
    }

    // The approximation. Six passes need two buffers, and where a sample the rule rejects has to
    // leave its destination untouched, both of them have to be working memory: the destination
    // cannot be half of the ping-pong when five of the six passes would write every position of it.
    // Where every sample means something, the destination is the second buffer and the operation
    // holds one plane instead of two.
    const bool gatesWrites = selection.mask != nullptr || selection.validity.rule() != ValidityRule::AllValid;
    SpatialFilterResult result{};
    Result<spatial_detail::SpatialPlaneBuffer> even =
        spatial_detail::SpatialPlaneBuffer::own(execution, geometry, operation);
    if (!even.hasValue())
    {
        return even.status();
    }
    const PlaneView<float> evenPlane = even.value().view();
    result.workingBytes += even.value().bytes();

    spatial_detail::SpatialPlaneBuffer oddBuffer;
    if (gatesWrites)
    {
        Result<spatial_detail::SpatialPlaneBuffer> odd =
            spatial_detail::SpatialPlaneBuffer::own(execution, geometry, operation);
        if (!odd.hasValue())
        {
            return odd.status();
        }
        oddBuffer = std::move(odd).value();
        result.workingBytes += oddBuffer.bytes();
    }

    std::atomic<std::uint64_t> written{0u};
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        plan.request().output.writeBack, geometry, operation, result,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            const std::span<const std::uint32_t> horizontal = plan.horizontalBoxRadii();
            const std::span<const std::uint32_t> vertical = plan.verticalBoxRadii();
            const PlaneView<float> oddPlane = gatesWrites ? oddBuffer.view() : output;
            PlaneView<const float> current = input;
            for (std::uint32_t index = 0u; index < 2u * kGaussianBoxPasses; ++index)
            {
                const bool horizontalPass = index < kGaussianBoxPasses;
                const bool first = index == 0u;
                const bool last = index + 1u == 2u * kGaussianBoxPasses;
                const PlaneView<float> destinationPlane = last ? output : ((index % 2u == 0u) ? evenPlane : oddPlane);
                BoxPass pass{.radius = horizontalPass ? horizontal[index] : vertical[index - kGaussianBoxPasses],
                             .border = plan.request().borders.border,
                             .constantBorder = plan.request().borders.constantBorder,
                             .invalidNeighbor = plan.request().borders.invalidNeighbor,
                             .readValidity = first ? &validity : nullptr,
                             .writeGate = last ? &input : nullptr,
                             .writeValidity = last ? &validity : nullptr};
                const Status ran = horizontalPass
                                       ? boxPassHorizontal(execution, current, destinationPlane, pass, written)
                                       : boxPassVertical(execution, current, destinationPlane, pass, written);
                if (ran.failed())
                {
                    return ran;
                }
                current = destinationPlane;
            }
            result.imagePasses += 2u * kGaussianBoxPasses;
            result.horizontalRadius = plan.horizontalRadius();
            result.verticalRadius = plan.verticalRadius();
            result.run.selection.total += static_cast<std::uint64_t>(input.width()) * input.height();
            result.plannedWorkers = (std::max)(result.plannedWorkers, plannedWorkerCount(execution, input.height(),
                                                                                         boxRowCost(input.width())));
            return Status::success();
        });
    if (filtered.failed())
    {
        return filtered;
    }
    // Only the last pass of each channel gates its writes, so what it produced is what the operation
    // wrote; the five passes before it wrote every position of an intermediate.
    const std::uint64_t everyPosition =
        static_cast<std::uint64_t>(geometry.width) * geometry.height * (2u * kGaussianBoxPasses - 1u);
    result.run.outputsWritten = written.load(std::memory_order_relaxed) - everyPosition * geometry.channelCount;
    result.run.selection.valid = result.run.outputsWritten;
    result.precision = ComputePrecision::Float32;
    return result;
}

} // namespace

Result<SpatialFilterResult> blurImageGaussian(const ExecutionContext& execution, SampleType sampleType,
                                              const void* source, const ImageLayout& sourceLayout, void* destination,
                                              const ImageLayout& destinationLayout, const GaussianPlan& plan,
                                              const SpatialSelection& selection)
{
    const Result<GaussianPlan> checked = planGaussianBlur(plan.request());
    if (!checked.hasValue())
    {
        return checked.status();
    }
    return blurImageGaussianImplementation(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan, selection, kBlurOperation);
}

Result<SpatialFilterResult> blurImageRealtimePreviewGaussian(const ExecutionContext& execution, SampleType sampleType,
                                                             const void* source, const ImageLayout& sourceLayout,
                                                             void* destination, const ImageLayout& destinationLayout,
                                                             const GaussianPlan& plan,
                                                             const SpatialSelection& selection)
{
    if (plan.method() != GaussianMethod::RepeatedBox)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, kPreviewBlurOperation,
                              "a real-time-preview Gaussian execution requires the repeated-box preview plan");
    }
    const RealtimePreviewGaussianRequest request{.sigmaX = plan.request().sigmaX,
                                                 .sigmaY = plan.request().sigmaY,
                                                 .borders = plan.request().borders,
                                                 .output = plan.request().output};
    const Result<GaussianPlan> checked = planRealtimePreviewGaussianBlur(request);
    if (!checked.hasValue())
    {
        return checked.status();
    }
    return blurImageGaussianImplementation(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan, selection, kPreviewBlurOperation);
}

} // namespace an
