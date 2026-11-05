// AfterNightEngine - deciding for itself which samples of a frame are its background.
//
// Each channel is estimated alone, in one workspace of working-picture planes the whole operation
// shares. The channel is reduced by block averages into the working picture, and the chosen surface
// is fitted to every working sample. Each round then measures the residual against the surface,
// takes its robust centre and scale - the median and the scaled median absolute deviation of the
// residuals the last round accepted - and accepts what lies inside the stated interval, less
// whatever the structure protection grows and excludes. A round that leaves too little accepted
// falls back to the darkest fraction. The surface is refitted to what was accepted, and the rounds
// stop when the accepted set stops moving or the ceiling is reached. An optional final smoothing
// follows, and the surface leaves the working picture only through a bilinear interpolation between
// block centres - into the caller's planes for an estimate, or straight into the corrected samples
// for a removal.
//
// Derived from AutoGradientRemoval 1.0.0, a Siril script by Cyril Richard -
// Copyright (C) 2026 Cyril Richard, GPL-3.0-or-later - and distributed under the same licence. The
// rounds above, the inpainted low-pass, the polynomial model and the structure protection are the
// script's method;
// `Algorithms/Background/AutoBackground.h` states what is this engine's, and
// `docs/dev/AUTO_GRADIENT_REMOVAL_PROVENANCE.md` records the revision that was reviewed.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Background/AutoBackground.h>

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/background/BackgroundInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/SpatialPlane.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace an
{
namespace
{

using background_detail::PolynomialTerm;
using spatial_detail::SpatialGeometry;
using spatial_detail::SpatialPlaneBuffer;

constexpr const char* kPlanOperation = "an::planAutoBackground";
constexpr const char* kEstimateOperation = "an::estimateImageAutoBackground";
constexpr const char* kRemoveOperation = "an::removeImageAutoBackground";

/// What a background fit says it is doing: the first fit, then one report per rejection round.
constexpr const char* kFitStage = "automatic background: fitting rounds";

/// The smallest scale a robust deviation is allowed to be.
///
/// A frame whose accepted residuals are all one number has no scale, and a rejection interval of
/// zero width would then reject everything on the next iteration.
constexpr double kMinimumRobustScale = 1.0e-12;

/// One failure from this domain.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status backgroundFailure(ErrorCode code, const char* operation, const char* message)
{
    return spatial_detail::spatialFailure(code, operation, message);
}

/// One failure carrying what was asked for and what was allowed.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @param requested the number the caller supplied.
/// @param limit the number the contract allows.
/// @return the failure, carrying both counts.
[[nodiscard]] Status backgroundLimitFailure(ErrorCode code, const char* operation, const char* message,
                                            std::uint64_t requested, std::uint64_t limit)
{
    return spatial_detail::spatialLimitFailure(code, operation, message, requested, limit);
}

/// How much work one row of a working-picture pass is.
///
/// @param width how many samples one row holds.
/// @param operationsPerSample roughly what one sample costs.
/// @return the cost.
[[nodiscard]] TaskCost rowCost(std::uint32_t width, std::uint64_t operationsPerSample) noexcept
{
    const auto samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * sizeof(float) * 2u, .operationsPerItem = samples * operationsPerSample};
}

/// The working picture a plan reduces a frame to.
///
/// @param plan the validated request.
/// @return the geometry one working plane covers.
[[nodiscard]] SpatialGeometry workingGeometry(const AutoBackgroundPlan& plan) noexcept
{
    SpatialGeometry geometry{};
    geometry.width = plan.workingWidth();
    geometry.height = plan.workingHeight();
    geometry.channelCount = 1u;
    geometry.planeBytes = static_cast<std::uint64_t>(geometry.width) * geometry.height * sizeof(float);
    return geometry;
}

/// Whether a selection can reject any sample at all.
///
/// @param validity the rule and optional packed mask.
/// @return true when every sample is admitted whatever its value.
[[nodiscard]] bool admitsEverySample(const KernelValidityView& validity) noexcept
{
    return validity.mask == nullptr && validity.descriptor.rule() == ValidityRule::AllValid;
}

/// The mean of one resident channel after the Float32 arithmetic boundary and validity rule.
///
/// @tparam SampleT the channel's stored representation.
/// @param execution the operation policy and engine-context lease.
/// @param source where the resident image starts.
/// @param layout where its samples sit.
/// @param channel which channel to read.
/// @param validity the rule and optional packed mask.
/// @param operation the name carried by any failure.
/// @param mean where the mean goes.
/// @return success; `ErrorCode::EmptySelection` when nothing was accepted; or an execution failure.
template <typename SampleT>
[[nodiscard]] Status acceptedResidentMeanTyped(const ExecutionContext& execution, const void* source,
                                               const ImageLayout& layout, std::uint32_t channel,
                                               const KernelValidityView& validity, const char* operation, double& mean)
{
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t columnStride = layout.samplesPerPixel();
    std::atomic<std::uint64_t> counted{0u};
    const auto add = [](double left, double right) noexcept { return left + right; };
    const Result<double> total = parallelReduce(
        execution, layout.extent().height(), rowCost(width, 2u), 0.0,
        [&](TaskRange range, double& partial) {
            std::uint64_t here = 0u;
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const auto* const row =
                    reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(source) +
                                                     plane_detail::channelRowByteOffset(layout, channel, index));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const float sample = static_cast<float>(row[static_cast<std::size_t>(x) * columnStride]);
                    if (!sampleIsValidByValue(validity.descriptor, sample) || !plane_detail::maskAllows(validity, x, y))
                    {
                        continue;
                    }
                    partial += static_cast<double>(sample);
                    ++here;
                }
            }
            counted.fetch_add(here, std::memory_order_relaxed);
            return Status::success();
        },
        add);
    if (!total.hasValue())
    {
        return total.status();
    }
    const std::uint64_t accepted = counted.load(std::memory_order_relaxed);
    if (accepted == 0u)
    {
        return backgroundFailure(ErrorCode::EmptySelection, operation,
                                 "the validity rule leaves this channel with no sample to estimate from");
    }
    mean = total.value() / static_cast<double>(accepted);
    return Status::success();
}

/// Dispatches a resident-channel mean to its stored representation.
[[nodiscard]] Status acceptedResidentMean(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                          const ImageLayout& layout, std::uint32_t channel,
                                          const KernelValidityView& validity, const char* operation, double& mean)
{
    Status measured = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        measured = acceptedResidentMeanTyped<SampleT>(execution, source, layout, channel, validity, operation, mean);
    });
    return known ? measured
                 : backgroundFailure(ErrorCode::InvalidArgument, operation,
                                     "the representation is not one of the eight this engine stores");
}

/// Reduces one resident channel directly into the working picture.
///
/// Each working sample is the mean of the admitted samples of its block, a partial block at the
/// right or bottom edge averaging only what it covers. A block with no admitted sample takes the
/// channel's mean of the admitted samples rather than a zero that would pull the surface down.
///
/// @tparam SampleT the channel's stored representation.
template <typename SampleT>
[[nodiscard]] Status reduceResidentChannelTyped(const ExecutionContext& execution, const void* source,
                                                const ImageLayout& layout, std::uint32_t channel,
                                                const KernelValidityView& validity, PlaneView<float> destination,
                                                std::uint32_t factor, const char* operation)
{
    double fallback = 0.0;
    if (!admitsEverySample(validity))
    {
        const Status measured =
            acceptedResidentMeanTyped<SampleT>(execution, source, layout, channel, validity, operation, fallback);
        if (measured.failed())
        {
            return measured;
        }
    }
    const std::uint32_t sourceWidth = layout.extent().width();
    const std::uint32_t sourceHeight = layout.extent().height();
    const std::uint32_t columnStride = layout.samplesPerPixel();
    const bool everySampleMeansSomething = admitsEverySample(validity);
    const auto substitute = static_cast<float>(fallback);
    return parallelFor(
        execution, destination.height(), rowCost(sourceWidth, static_cast<std::uint64_t>(factor)),
        [&](TaskRange range) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const std::span<float> to = *destination.row(y);
                const std::uint64_t firstRow = index * factor;
                const std::uint64_t lastRow = (std::min)(firstRow + factor, static_cast<std::uint64_t>(sourceHeight));
                for (std::uint32_t x = 0u; x < destination.width(); ++x)
                {
                    const std::uint64_t firstColumn = static_cast<std::uint64_t>(x) * factor;
                    const std::uint64_t lastColumn =
                        (std::min)(firstColumn + factor, static_cast<std::uint64_t>(sourceWidth));
                    double sum = 0.0;
                    std::uint32_t counted = 0u;
                    for (std::uint64_t rowIndex = firstRow; rowIndex < lastRow; ++rowIndex)
                    {
                        const auto* const row = reinterpret_cast<const SampleT*>(
                            static_cast<const std::uint8_t*>(source) +
                            plane_detail::channelRowByteOffset(layout, channel, rowIndex));
                        for (std::uint64_t column = firstColumn; column < lastColumn; ++column)
                        {
                            const float sample =
                                static_cast<float>(row[static_cast<std::size_t>(column) * columnStride]);
                            if (!everySampleMeansSomething &&
                                (!sampleIsValidByValue(validity.descriptor, sample) ||
                                 !plane_detail::maskAllows(validity, static_cast<std::uint32_t>(column),
                                                           static_cast<std::uint32_t>(rowIndex))))
                            {
                                continue;
                            }
                            sum += static_cast<double>(sample);
                            ++counted;
                        }
                    }
                    to[x] = counted == 0u ? substitute : static_cast<float>(sum / static_cast<double>(counted));
                }
            }
            return Status::success();
        });
}

/// Dispatches a resident-channel reduction to its stored representation.
[[nodiscard]] Status reduceResidentChannel(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                           const ImageLayout& layout, std::uint32_t channel,
                                           const KernelValidityView& validity, PlaneView<float> destination,
                                           std::uint32_t factor, const char* operation)
{
    Status reduced = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        reduced = reduceResidentChannelTyped<SampleT>(execution, source, layout, channel, validity, destination, factor,
                                                      operation);
    });
    return known ? reduced
                 : backgroundFailure(ErrorCode::InvalidArgument, operation,
                                     "the representation is not one of the eight this engine stores");
}

/// Everything one channel's estimate reads and writes, taken once for the whole operation.
///
/// The planes are of the working picture rather than of the frame, which is what keeps an estimate's
/// memory a fraction of the frame's whatever the reduction factor is set to.
struct Workspace
{
    /// The reduced source.
    SpatialPlaneBuffer small{};

    /// The surface as it currently stands.
    ///
    /// The structure protection borrows it for the grown seed map, which the refit that follows
    /// every protection always overwrites.
    SpatialPlaneBuffer model{};

    /// The inpainting's filled picture, and a scratch plane for the structure protection's seeds
    /// and the final smoothing under either model.
    SpatialPlaneBuffer filled{};

    /// One low-pass working plane.
    SpatialPlaneBuffer first{};

    /// The other.
    SpatialPlaneBuffer second{};

    /// The current residual of the reduced source against the surface.
    SpatialPlaneBuffer residual{};

    /// One byte per working sample: whether the rejection accepted it.
    plane_detail::ScopedContextMemory accepted{};

    /// The same for the iteration being decided.
    plane_detail::ScopedContextMemory nextAccepted{};

    /// One byte per working sample: whether the structure protection ever excluded it.
    plane_detail::ScopedContextMemory protection{};

    /// Room for one double per working sample, for the exact rank statistics.
    plane_detail::ScopedContextMemory values{};

    /// Per-chunk normal-equation accumulators, for the polynomial model.
    plane_detail::ScopedContextMemory accumulators{};

    /// How many bytes of context memory all of it holds.
    ///
    /// @return the byte count.
    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return small.bytes() + model.bytes() + filled.bytes() + first.bytes() + second.bytes() + residual.bytes() +
               accepted.bytes() + nextAccepted.bytes() + protection.bytes() + values.bytes() + accumulators.bytes();
    }
};

/// What one channel's estimate did.
struct ChannelCounters
{
    /// How many iterations ran.
    std::uint32_t iterationsRun = 0u;

    /// Whether the accepted set settled before the iteration count ran out.
    bool converged = false;

    /// How many working samples the last iteration accepted.
    std::uint64_t acceptedSamples = 0u;

    /// How many the structure protection excluded.
    std::uint64_t protectedSamples = 0u;

    /// How many times the rejection fell back to the darkest fraction.
    std::uint32_t rejectionFallbacks = 0u;

    /// How many low-pass and fit passes the estimate actually ran over the working picture.
    std::uint64_t workingPasses = 0u;
};

/// Fills a byte plane with one value.
///
/// @param bytes the plane.
/// @param value what to write.
void fillBytes(std::span<std::uint8_t> bytes, std::uint8_t value) noexcept
{
    std::fill(bytes.begin(), bytes.end(), value);
}

/// How many acceptance decisions changed membership between two iterations.
[[nodiscard]] constexpr std::uint64_t acceptanceChanges(std::span<const std::uint8_t> current,
                                                        std::span<const std::uint8_t> next) noexcept
{
    std::uint64_t changed = 0u;
    const std::size_t count = (std::min)(current.size(), next.size());
    for (std::size_t index = 0u; index < count; ++index)
    {
        changed += current[index] != next[index] ? 1u : 0u;
    }
    return changed;
}

constexpr std::array<std::uint8_t, 4u> kAcceptanceBefore{1u, 0u, 1u, 0u};
constexpr std::array<std::uint8_t, 4u> kAcceptanceAfter{0u, 1u, 1u, 0u};
static_assert(acceptanceChanges(kAcceptanceBefore, kAcceptanceAfter) == 2u,
              "equal accepted counts can still describe a changed set");

/// Smooths the accepted samples of a plane, filling in the rejected ones from around them.
///
/// The rejected samples are first replaced by the accepted mean, then repeatedly by the smoothing of
/// the picture so far, so a hole left by a rejected object closes with the illumination around it
/// rather than with the object.
///
/// @param execution the operation policy and engine-context lease.
/// @param workspace the planes to work in.
/// @param accepted one byte per working sample.
/// @param radius the low-pass half-window.
/// @param passes how many low-pass passes each smoothing applies.
/// @param counters where the pass count is added.
/// @return success, or an execution, cancellation or argument failure.
[[nodiscard]] Status inpaintedLowpass(const ExecutionContext& execution, Workspace& workspace,
                                      std::span<const std::uint8_t> accepted, std::uint32_t radius,
                                      std::uint32_t passes, ChannelCounters& counters)
{
    const PlaneView<const float> small = workspace.small.constView();
    const PlaneView<float> model = workspace.model.view();
    const PlaneView<float> filled = workspace.filled.view();
    const std::uint32_t width = small.width();
    const std::uint32_t height = small.height();

    std::uint64_t acceptedCount = 0u;
    double acceptedSum = 0.0;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const std::span<const float> row = *small.row(y);
        const std::uint8_t* const flags = accepted.data() + static_cast<std::size_t>(y) * width;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            if (flags[x] == 0u)
            {
                continue;
            }
            ++acceptedCount;
            acceptedSum += static_cast<double>(row[x]);
        }
    }

    if (acceptedCount == 0u)
    {
        return parallelFor(execution, height, rowCost(width, 1u), [&](TaskRange range) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const std::span<float> row = *model.row(static_cast<std::uint32_t>(index));
                std::fill(row.begin(), row.end(), 0.0f);
            }
            return Status::success();
        });
    }

    counters.workingPasses += 2u * passes;
    if (acceptedCount == static_cast<std::uint64_t>(width) * height)
    {
        return background_detail::lowpassPlane(execution, small, model, workspace.first.view(), workspace.second.view(),
                                               radius, passes);
    }

    const auto mean = static_cast<float>(acceptedSum / static_cast<double>(acceptedCount));
    const Status seeded = parallelFor(execution, height, rowCost(width, 2u), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<const float> from = *small.row(y);
            const std::span<float> to = *filled.row(y);
            const std::uint8_t* const flags = accepted.data() + static_cast<std::size_t>(y) * width;
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                to[x] = flags[x] != 0u ? from[x] : mean;
            }
        }
        return Status::success();
    });
    if (seeded.failed())
    {
        return seeded;
    }

    for (std::uint32_t fill = 0u; fill < kAutoBackgroundFillIterations; ++fill)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        const Status smoothed = background_detail::lowpassPlane(
            execution, filled.constView(), model, workspace.first.view(), workspace.second.view(), radius, passes);
        if (smoothed.failed())
        {
            return smoothed;
        }
        counters.workingPasses += 2u * passes;
        const Status merged = parallelFor(execution, height, rowCost(width, 2u), [&](TaskRange range) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const std::span<const float> from = *small.row(y);
                const std::span<const float> smooth = *model.constView().row(y);
                const std::span<float> to = *filled.row(y);
                const std::uint8_t* const flags = accepted.data() + static_cast<std::size_t>(y) * width;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    to[x] = flags[x] != 0u ? from[x] : smooth[x];
                }
            }
            return Status::success();
        });
        if (merged.failed())
        {
            return merged;
        }
    }

    return background_detail::lowpassPlane(execution, filled.constView(), model, workspace.first.view(),
                                           workspace.second.view(), radius, passes);
}

/// Fits a polynomial to the accepted samples of the working picture and draws it into the model.
///
/// The normal equations are accumulated over a partition that does not know how many threads there
/// are and merged in chunk order, so the surface is the same at any thread count.
///
/// @param execution the operation policy and engine-context lease.
/// @param workspace the planes to work in.
/// @param accepted one byte per working sample.
/// @param degree the polynomial's total degree.
/// @param counters where the pass count is added.
/// @return success; `ErrorCode::InvalidData` when the accepted samples determine no surface; or an
///         execution or cancellation failure.
[[nodiscard]] Status densePolynomial(const ExecutionContext& execution, Workspace& workspace,
                                     std::span<const std::uint8_t> accepted, std::uint32_t degree,
                                     ChannelCounters& counters)
{
    const PlaneView<const float> small = workspace.small.constView();
    const PlaneView<float> model = workspace.model.view();
    const std::uint32_t width = small.width();
    const std::uint32_t height = small.height();

    std::array<PolynomialTerm, background_detail::kMaxPolynomialTerms> terms{};
    const std::uint32_t termCount = background_detail::writePolynomialTerms(degree, terms);
    const std::uint32_t order = termCount;
    const std::uint64_t perChunk = static_cast<std::uint64_t>(order) * order + order;
    const std::uint64_t chunks =
        (std::max)(std::uint64_t{1},
                   deterministicChunkCount(height, rowCost(width, static_cast<std::uint64_t>(order) * 2u)));
    const std::span<double> accumulators{static_cast<double*>(workspace.accumulators.data()),
                                         static_cast<std::size_t>(chunks * perChunk)};
    std::fill(accumulators.begin(), accumulators.end(), 0.0);

    const Status walked = parallelFor(
        execution, chunks,
        TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * height * sizeof(float) / chunks,
                 .operationsPerItem = static_cast<std::uint64_t>(width) * height * order * order / (2u * chunks) + 1u},
        [&](TaskRange range) {
            std::array<double, background_detail::kMaxPolynomialTerms> basis{};
            for (std::uint64_t chunk = range.begin; chunk < range.end; ++chunk)
            {
                const TaskRange rows = plane_detail::deterministicRange(chunk, height, chunks);
                double* const into = accumulators.data() + static_cast<std::size_t>(chunk * perChunk);
                for (std::uint64_t index = rows.begin; index < rows.end; ++index)
                {
                    const auto y = static_cast<std::uint32_t>(index);
                    const std::span<const float> row = *small.row(y);
                    const std::uint8_t* const flags = accepted.data() + static_cast<std::size_t>(y) * width;
                    const double ny = background_detail::normalisedCoordinate(static_cast<double>(y), height);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        if (flags[x] == 0u)
                        {
                            continue;
                        }
                        const double nx = background_detail::normalisedCoordinate(static_cast<double>(x), width);
                        background_detail::evaluatePolynomialBasis(std::span<const PolynomialTerm>{terms.data(), order},
                                                                   nx, ny, basis);
                        const auto value = static_cast<double>(row[x]);
                        for (std::uint32_t left = 0u; left < order; ++left)
                        {
                            into[static_cast<std::size_t>(order) * order + left] += basis[left] * value;
                            double* const target = into + static_cast<std::size_t>(left) * order;
                            for (std::uint32_t right = 0u; right <= left; ++right)
                            {
                                target[right] += basis[left] * basis[right];
                            }
                        }
                    }
                }
            }
            return Status::success();
        });
    if (walked.failed())
    {
        return walked;
    }
    counters.workingPasses += 1u;

    for (std::uint64_t chunk = 1u; chunk < chunks; ++chunk)
    {
        const double* const from = accumulators.data() + static_cast<std::size_t>(chunk * perChunk);
        double* const into = accumulators.data();
        for (std::uint64_t index = 0u; index < perChunk; ++index)
        {
            into[static_cast<std::size_t>(index)] += from[static_cast<std::size_t>(index)];
        }
    }

    const std::span<double> matrix = accumulators.subspan(0u, static_cast<std::size_t>(order) * order);
    const std::span<double> rhs =
        accumulators.subspan(static_cast<std::size_t>(order) * order, static_cast<std::size_t>(order));
    const background_detail::SymmetricSolveResult solved = background_detail::solveSymmetric(matrix, rhs, order);
    if (!solved.solved)
    {
        return backgroundFailure(ErrorCode::InvalidData, kEstimateOperation,
                                 "the accepted samples determine no background surface at all");
    }

    std::array<double, background_detail::kMaxPolynomialTerms> coefficients{};
    for (std::uint32_t index = 0u; index < order; ++index)
    {
        coefficients[index] = rhs[index];
    }
    return parallelFor(execution, height, rowCost(width, static_cast<std::uint64_t>(order) * 2u), [&](TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<float> row = *model.row(y);
            const double ny = background_detail::normalisedCoordinate(static_cast<double>(y), height);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double nx = background_detail::normalisedCoordinate(static_cast<double>(x), width);
                row[x] = static_cast<float>(
                    background_detail::evaluatePolynomial(std::span<const PolynomialTerm>{terms.data(), order},
                                                          std::span<const double>{coefficients.data(), order}, nx, ny));
            }
        }
        return Status::success();
    });
}

/// Fits whichever surface the plan names to the accepted samples.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the validated request.
/// @param workspace the planes to work in.
/// @param accepted one byte per working sample.
/// @param counters where the pass count is added.
/// @return success, or whatever the chosen model reported.
[[nodiscard]] Status fitSurface(const ExecutionContext& execution, const AutoBackgroundPlan& plan, Workspace& workspace,
                                std::span<const std::uint8_t> accepted, ChannelCounters& counters)
{
    if (plan.request().model == AutoBackgroundModel::Polynomial)
    {
        return densePolynomial(execution, workspace, accepted, plan.request().polynomialDegree, counters);
    }
    return inpaintedLowpass(execution, workspace, accepted, plan.modelRadius(), kAutoBackgroundLowpassPasses, counters);
}

/// Grows the samples a threshold calls structure and excludes them.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the validated request.
/// @param workspace the planes to work in.
/// @param centre what the residual's own centre is, which the threshold is measured from.
/// @param nextAccepted the acceptance being decided, from which structure is removed.
/// @param protection the union of everything protection has ever excluded.
/// @param counters where the pass and protection counts are added.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status protectStructures(const ExecutionContext& execution, const AutoBackgroundPlan& plan,
                                       Workspace& workspace, double centre, std::span<std::uint8_t> nextAccepted,
                                       std::span<std::uint8_t> protection, ChannelCounters& counters)
{
    const PlaneView<const float> residual = workspace.residual.constView();
    const PlaneView<float> detected = workspace.filled.view();
    const std::uint32_t width = residual.width();
    const std::uint32_t height = residual.height();
    const double threshold = plan.request().protectionThreshold;

    std::atomic<std::uint64_t> seeds{0u};
    const Status marked = parallelFor(execution, height, rowCost(width, 2u), [&](TaskRange range) {
        std::uint64_t here = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<const float> from = *residual.row(y);
            const std::span<float> to = *detected.row(y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const bool structure = (static_cast<double>(from[x]) - centre) > threshold;
                to[x] = structure ? 1.0f : 0.0f;
                here += structure ? 1u : 0u;
            }
        }
        seeds.fetch_add(here, std::memory_order_relaxed);
        return Status::success();
    });
    if (marked.failed())
    {
        return marked;
    }
    const bool hasSeeds = seeds.load(std::memory_order_relaxed) != 0u;

    const double amount = plan.request().protectionAmount;
    if (hasSeeds)
    {
        const auto growth = static_cast<std::uint32_t>(
            (std::max)(1LL, std::llround(static_cast<double>(plan.modelRadius()) * (0.5 + amount))));
        const Status grown =
            background_detail::lowpassPlane(execution, detected.constView(), workspace.model.view(),
                                            workspace.first.view(), workspace.second.view(), growth, 2u);
        if (grown.failed())
        {
            return grown;
        }
        counters.workingPasses += 4u;
    }

    // The cutoff falls as the amount rises, so one number moves the protection from the bright cores
    // to the object and its halo. The small offset keeps an amount of one from protecting a sample
    // the growth never reached.
    const double cutoff = (1.0 - amount) * 0.5 + 1.0e-3;
    const PlaneView<const float> spread = workspace.model.constView();
    std::atomic<std::uint64_t> newlyProtected{0u};
    const Status applied = parallelFor(execution, height, rowCost(width, 2u), [&](TaskRange range) {
        std::uint64_t here = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            const auto y = static_cast<std::uint32_t>(index);
            const std::span<const float> from = *spread.row(y);
            std::uint8_t* const accept = nextAccepted.data() + static_cast<std::size_t>(y) * width;
            std::uint8_t* const protect =
                protection.empty() ? nullptr : protection.data() + static_cast<std::size_t>(y) * width;
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const bool detectedNow = hasSeeds && static_cast<double>(from[x]) > cutoff;
                const bool protectedBefore = protect != nullptr && protect[x] != 0u;
                if (detectedNow && protect != nullptr && !protectedBefore)
                {
                    protect[x] = 1u;
                    ++here;
                }
                if (detectedNow || protectedBefore)
                {
                    accept[x] = 0u;
                }
            }
        }
        newlyProtected.fetch_add(here, std::memory_order_relaxed);
        return Status::success();
    });
    if (applied.failed())
    {
        return applied;
    }
    counters.protectedSamples += newlyProtected.load(std::memory_order_relaxed);
    return Status::success();
}

/// Estimates the background of one already-reduced channel.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the validated request.
/// @param workspace the planes to work in, whose reduced source is already filled.
/// @param counters what the estimate did.
/// @return success, or the first failure a fit, a low-pass or a rejection reported.
[[nodiscard]] Status estimateChannel(const ExecutionContext& execution, const AutoBackgroundPlan& plan,
                                     Workspace& workspace, ChannelCounters& counters)
{
    const PlaneView<const float> small = workspace.small.constView();
    const PlaneView<float> residual = workspace.residual.view();
    const std::uint32_t width = small.width();
    const std::uint32_t height = small.height();
    const auto sampleCount = static_cast<std::uint64_t>(width) * height;
    const std::span<std::uint8_t> accepted{static_cast<std::uint8_t*>(workspace.accepted.data()),
                                           static_cast<std::size_t>(sampleCount)};
    const std::span<std::uint8_t> nextAccepted{static_cast<std::uint8_t*>(workspace.nextAccepted.data()),
                                               static_cast<std::size_t>(sampleCount)};
    const std::span<std::uint8_t> protection =
        plan.request().protectStructures
            ? std::span<std::uint8_t>{static_cast<std::uint8_t*>(workspace.protection.data()),
                                      static_cast<std::size_t>(sampleCount)}
            : std::span<std::uint8_t>{};
    const std::span<double> values{static_cast<double*>(workspace.values.data()),
                                   static_cast<std::size_t>(sampleCount)};

    fillBytes(accepted, 1u);
    if (!protection.empty())
    {
        fillBytes(protection, 0u);
    }
    Status fitted = fitSurface(execution, plan, workspace, accepted, counters);
    if (fitted.failed())
    {
        return fitted;
    }
    // The first fit and every rejection round after it are one unit each; the ceiling on rounds is
    // the total, and a fit that converges early ends the channel's share full.
    const std::uint64_t fittingRounds = static_cast<std::uint64_t>(plan.request().maxIterations) + 1u;
    execution.reportProgress(1u, fittingRounds, kFitStage);

    const auto minimumKept =
        (std::max)(static_cast<std::uint64_t>(kMinAutoBackgroundKeptSamples),
                   static_cast<std::uint64_t>(kMinAutoBackgroundKeptFraction * static_cast<double>(sampleCount)));
    counters.acceptedSamples = sampleCount;

    for (std::uint32_t iteration = 0u; iteration < plan.request().maxIterations; ++iteration)
    {
        const Status running = execution.checkCancellation();
        if (running.failed())
        {
            return running;
        }
        const PlaneView<const float> model = workspace.model.constView();
        const Status differenced = parallelFor(execution, height, rowCost(width, 2u), [&](TaskRange range) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const std::span<const float> from = *small.row(y);
                const std::span<const float> take = *model.row(y);
                const std::span<float> to = *residual.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    to[x] = from[x] - take[x];
                }
            }
            return Status::success();
        });
        if (differenced.failed())
        {
            return differenced;
        }
        counters.workingPasses += 1u;

        std::uint64_t consideredHere = 0u;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const std::span<const float> row = *residual.constView().row(y);
            const std::uint8_t* const flags = accepted.data() + static_cast<std::size_t>(y) * width;
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (flags[x] == 0u)
                {
                    continue;
                }
                values[static_cast<std::size_t>(consideredHere)] = static_cast<double>(row[x]);
                ++consideredHere;
            }
        }
        if (consideredHere == 0u)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const std::span<const float> row = *residual.constView().row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    values[static_cast<std::size_t>(y) * width + x] = static_cast<double>(row[x]);
                }
            }
            consideredHere = sampleCount;
        }
        const std::span<double> considered = values.subspan(0u, static_cast<std::size_t>(consideredHere));
        const double centre = background_detail::medianOf(considered);
        for (double& value : considered)
        {
            value = std::abs(value - centre);
        }
        const double scale =
            kMedianAbsoluteDeviationToSigma * background_detail::medianOf(considered) + kMinimumRobustScale;
        const double lowest = centre - plan.request().lowRejection * scale;
        const double highest = centre + plan.request().highRejection * scale;

        const Status decided = parallelFor(execution, height, rowCost(width, 2u), [&](TaskRange range) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const std::span<const float> row = *residual.constView().row(y);
                std::uint8_t* const flags = nextAccepted.data() + static_cast<std::size_t>(y) * width;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const auto value = static_cast<double>(row[x]);
                    const bool keep = value <= highest && value >= lowest;
                    flags[x] = keep ? std::uint8_t{1} : std::uint8_t{0};
                }
            }
            return Status::success();
        });
        if (decided.failed())
        {
            return decided;
        }

        if (plan.request().protectStructures)
        {
            const Status protectedStructures =
                protectStructures(execution, plan, workspace, centre, nextAccepted, protection, counters);
            if (protectedStructures.failed())
            {
                return protectedStructures;
            }
        }

        std::uint64_t kept = 0u;
        for (std::uint64_t index = 0u; index < sampleCount; ++index)
        {
            kept += nextAccepted[static_cast<std::size_t>(index)] != 0u ? 1u : 0u;
        }
        if (kept < minimumKept)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const std::span<const float> row = *residual.constView().row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    values[static_cast<std::size_t>(y) * width + x] = static_cast<double>(row[x]);
                }
            }
            const double fraction = 100.0 * static_cast<double>(minimumKept) /
                                    static_cast<double>((std::max)(sampleCount, std::uint64_t{1}));
            const double darkest = background_detail::percentileOf(values, fraction);
            kept = 0u;
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const std::span<const float> row = *residual.constView().row(y);
                std::uint8_t* const flags = nextAccepted.data() + static_cast<std::size_t>(y) * width;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const bool keep = static_cast<double>(row[x]) <= darkest;
                    const std::size_t flat = static_cast<std::size_t>(y) * width + x;
                    const bool unprotected = protection.empty() || protection[flat] == 0u;
                    flags[x] = keep && unprotected ? std::uint8_t{1} : std::uint8_t{0};
                    kept += keep && unprotected ? 1u : 0u;
                }
            }
            ++counters.rejectionFallbacks;
        }

        if (kept == 0u)
        {
            return backgroundFailure(ErrorCode::EmptySelection, kEstimateOperation,
                                     "structure protection leaves no working sample to fit");
        }

        const std::uint64_t changed = acceptanceChanges(accepted, nextAccepted);
        fitted = fitSurface(execution, plan, workspace, nextAccepted, counters);
        if (fitted.failed())
        {
            return fitted;
        }
        std::copy(nextAccepted.begin(), nextAccepted.end(), accepted.begin());
        const double change =
            static_cast<double>(changed) / static_cast<double>((std::max)(sampleCount, std::uint64_t{1}));
        counters.acceptedSamples = kept;
        counters.iterationsRun = iteration + 1u;
        execution.reportProgress(static_cast<std::uint64_t>(iteration) + 2u, fittingRounds, kFitStage);
        if (iteration > 0u && change < plan.request().convergence)
        {
            counters.converged = true;
            break;
        }
    }
    execution.reportProgress(fittingRounds, fittingRounds, kFitStage);

    if (plan.smoothingRadius() > 0u)
    {
        const Status smoothed = background_detail::lowpassPlane(
            execution, workspace.model.constView(), workspace.filled.view(), workspace.first.view(),
            workspace.second.view(), plan.smoothingRadius(), kAutoBackgroundLowpassPasses);
        if (smoothed.failed())
        {
            return smoothed;
        }
        counters.workingPasses += 2u * kAutoBackgroundLowpassPasses;
        const Status copied =
            background_detail::copyPlaneSamples(execution, workspace.filled.constView(), workspace.model.view());
        if (copied.failed())
        {
            return copied;
        }
    }
    return Status::success();
}

/// Takes every working plane and buffer one estimate needs.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the validated request.
/// @param operation the name carried by any failure.
/// @param workspace where the planes go.
/// @return success, or the memory resource's refusal.
[[nodiscard]] Status takeWorkspace(const ExecutionContext& execution, const AutoBackgroundPlan& plan,
                                   const char* operation, Workspace& workspace)
{
    const SpatialGeometry geometry = workingGeometry(plan);
    SpatialPlaneBuffer* const planes[] = {&workspace.small, &workspace.model,  &workspace.filled,
                                          &workspace.first, &workspace.second, &workspace.residual};
    for (SpatialPlaneBuffer* const plane : planes)
    {
        Result<SpatialPlaneBuffer> taken = SpatialPlaneBuffer::own(execution, geometry, operation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        *plane = std::move(taken).value();
    }

    const std::uint64_t samples = static_cast<std::uint64_t>(geometry.width) * geometry.height;
    Result<plane_detail::ScopedContextMemory> accepted =
        plane_detail::ScopedContextMemory::take(execution, samples, alignof(std::uint8_t), operation);
    if (!accepted.hasValue())
    {
        return accepted.status();
    }
    workspace.accepted = std::move(accepted).value();
    Result<plane_detail::ScopedContextMemory> nextAccepted =
        plane_detail::ScopedContextMemory::take(execution, samples, alignof(std::uint8_t), operation);
    if (!nextAccepted.hasValue())
    {
        return nextAccepted.status();
    }
    workspace.nextAccepted = std::move(nextAccepted).value();
    if (plan.request().protectStructures)
    {
        Result<plane_detail::ScopedContextMemory> protection =
            plane_detail::ScopedContextMemory::take(execution, samples, alignof(std::uint8_t), operation);
        if (!protection.hasValue())
        {
            return protection.status();
        }
        workspace.protection = std::move(protection).value();
    }
    Result<plane_detail::ScopedContextMemory> values =
        plane_detail::ScopedContextMemory::take(execution, samples * sizeof(double), alignof(double), operation);
    if (!values.hasValue())
    {
        return values.status();
    }
    workspace.values = std::move(values).value();

    if (plan.request().model == AutoBackgroundModel::Polynomial)
    {
        const std::uint64_t order = plan.termCount();
        const std::uint64_t perChunk = order * order + order;
        const std::uint64_t chunks =
            (std::max)(std::uint64_t{1}, deterministicChunkCount(geometry.height, rowCost(geometry.width, order * 2u)));
        Result<plane_detail::ScopedContextMemory> accumulators = plane_detail::ScopedContextMemory::take(
            execution, chunks * perChunk * sizeof(double), alignof(double), operation);
        if (!accumulators.hasValue())
        {
            return accumulators.status();
        }
        workspace.accumulators = std::move(accumulators).value();
    }
    return Status::success();
}

/// Counts from writing one corrected resident channel.
struct CorrectionCounters
{
    /// Divisors raised to the declared guard.
    std::uint64_t guarded = 0u;

    /// Corrected values held inside the declared interval.
    std::uint64_t clamped = 0u;

    /// Source samples the validity rule admitted.
    std::uint64_t accepted = 0u;

    /// Stored answers that differ from their source samples.
    std::uint64_t changed = 0u;
};

/// Corrects one resident channel without a full-frame gather or scatter.
template <typename SampleT>
[[nodiscard]] Status correctResidentChannelTyped(const ExecutionContext& execution, const void* source,
                                                 const ImageLayout& sourceLayout, void* destination,
                                                 const ImageLayout& destinationLayout, std::uint32_t channel,
                                                 const KernelValidityView& validity, const AutoBackgroundPlan& plan,
                                                 const Workspace& workspace, double level, CorrectionCounters& counters)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t sourceStride = sourceLayout.samplesPerPixel();
    const std::uint32_t destinationStride = destinationLayout.samplesPerPixel();
    const PlaneView<const float> model = workspace.model.constView();
    const std::uint32_t factor = plan.downsample();
    const bool dividing = plan.request().apply.correction == BackgroundCorrection::Divide;
    const double ditherWidth = plan.request().apply.dither;
    const bool dithering = ditherWidth > 0.0;
    const bool clamping = plan.request().apply.clamp.has_value();
    const double clampLow = clamping ? plan.request().apply.clamp->lowest : 0.0;
    const double clampHigh = clamping ? plan.request().apply.clamp->highest : 0.0;
    const double guard = plan.request().apply.guard;
    const ConversionPolicy& conversion = plan.request().apply.output.writeBack;
    std::atomic<std::uint64_t> guarded{0u};
    std::atomic<std::uint64_t> clamped{0u};
    std::atomic<std::uint64_t> accepted{0u};
    std::atomic<std::uint64_t> changed{0u};
    const Status corrected =
        parallelFor(execution, sourceLayout.extent().height(), rowCost(width, 12u), [&](TaskRange range) {
            std::uint64_t guardedHere = 0u;
            std::uint64_t clampedHere = 0u;
            std::uint64_t acceptedHere = 0u;
            std::uint64_t changedHere = 0u;
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const auto* const from =
                    reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(source) +
                                                     plane_detail::channelRowByteOffset(sourceLayout, channel, index));
                auto* const to =
                    reinterpret_cast<SampleT*>(static_cast<std::uint8_t*>(destination) +
                                               plane_detail::channelRowByteOffset(destinationLayout, channel, index));
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const SampleT original = from[static_cast<std::size_t>(x) * sourceStride];
                    const float stored = static_cast<float>(original);
                    if (!sampleIsValidByValue(validity.descriptor, stored) || !plane_detail::maskAllows(validity, x, y))
                    {
                        continue;
                    }
                    ++acceptedHere;
                    const double surface = background_detail::interpolateReduced(model, factor, static_cast<double>(x),
                                                                                 static_cast<double>(y));
                    const double sample = dithering ? static_cast<double>(stored) +
                                                          ditherWidth * background_detail::ditherFraction(
                                                                            static_cast<std::uint64_t>(y) * width + x)
                                                    : static_cast<double>(stored);
                    double value = 0.0;
                    if (dividing)
                    {
                        double divisor = surface;
                        if (!(divisor >= guard))
                        {
                            divisor = guard;
                            ++guardedHere;
                        }
                        value = sample / divisor * level;
                    }
                    else
                    {
                        value = sample - surface + level;
                    }
                    if (clamping)
                    {
                        const double held = std::clamp(value, clampLow, clampHigh);
                        if (held != value)
                        {
                            ++clampedHere;
                            value = held;
                        }
                    }
                    const SampleT written = convertSample<float, SampleT>(static_cast<float>(value), conversion).value;
                    to[static_cast<std::size_t>(x) * destinationStride] = written;
                    changedHere += written != original ? 1u : 0u;
                }
            }
            guarded.fetch_add(guardedHere, std::memory_order_relaxed);
            clamped.fetch_add(clampedHere, std::memory_order_relaxed);
            accepted.fetch_add(acceptedHere, std::memory_order_relaxed);
            changed.fetch_add(changedHere, std::memory_order_relaxed);
            return Status::success();
        });
    if (corrected.failed())
    {
        return corrected;
    }
    counters.guarded = guarded.load(std::memory_order_relaxed);
    counters.clamped = clamped.load(std::memory_order_relaxed);
    counters.accepted = accepted.load(std::memory_order_relaxed);
    counters.changed = changed.load(std::memory_order_relaxed);
    return Status::success();
}

/// Dispatches a direct resident correction to its stored representation.
[[nodiscard]] Status correctResidentChannel(const ExecutionContext& execution, SampleType sampleType,
                                            const void* source, const ImageLayout& sourceLayout, void* destination,
                                            const ImageLayout& destinationLayout, std::uint32_t channel,
                                            const KernelValidityView& validity, const AutoBackgroundPlan& plan,
                                            const Workspace& workspace, double level, CorrectionCounters& counters)
{
    Status corrected = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        corrected =
            correctResidentChannelTyped<SampleT>(execution, source, sourceLayout, destination, destinationLayout,
                                                 channel, validity, plan, workspace, level, counters);
    });
    return known ? corrected
                 : backgroundFailure(ErrorCode::InvalidArgument, kRemoveOperation,
                                     "the representation is not one of the eight this engine stores");
}

/// The level one channel's correction puts back.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan the validated request.
/// @param workspace the planes, whose model is the estimated surface.
/// @param sampleType the source representation.
/// @param source where the frame starts.
/// @param sourceLayout where its samples sit.
/// @param channel which channel is being corrected.
/// @param validity which of its samples mean something.
/// @param level where the level goes.
/// @return success, or an execution, cancellation or empty-selection failure.
[[nodiscard]] Status resolveLevel(const ExecutionContext& execution, const AutoBackgroundPlan& plan,
                                  const Workspace& workspace, SampleType sampleType, const void* source,
                                  const ImageLayout& sourceLayout, std::uint32_t channel,
                                  const KernelValidityView& validity, double& level)
{
    switch (plan.request().apply.level)
    {
    case BackgroundLevel::Stated:
        level = plan.request().apply.statedLevel;
        return Status::success();
    case BackgroundLevel::SourceMean:
        return acceptedResidentMean(execution, sampleType, source, sourceLayout, channel, validity, kRemoveOperation,
                                    level);
    case BackgroundLevel::ModelMedian:
    default: {
        const PlaneView<const float> model = workspace.model.constView();
        const std::uint32_t width = model.width();
        const std::span<double> values{static_cast<double*>(workspace.values.data()),
                                       static_cast<std::size_t>(width) * model.height()};
        for (std::uint32_t y = 0u; y < model.height(); ++y)
        {
            const std::span<const float> row = *model.row(y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                values[static_cast<std::size_t>(y) * width + x] = static_cast<double>(row[x]);
            }
        }
        level = background_detail::medianOf(values);
        return Status::success();
    }
    }
}

} // namespace

const char* autoBackgroundModelName(AutoBackgroundModel model) noexcept
{
    switch (model)
    {
    case AutoBackgroundModel::InpaintedLowpass:
        return "inpainted-lowpass";
    case AutoBackgroundModel::Polynomial:
        return "polynomial";
    }
    return "unknown";
}

AutoBackgroundPlan::AutoBackgroundPlan(const AutoBackgroundRequest& request, std::uint32_t width, std::uint32_t height,
                                       std::uint32_t downsample) noexcept
    : m_request(request), m_width(width), m_height(height), m_downsample(downsample), m_valid(true)
{
    m_workingWidth = (width + downsample - 1u) / downsample;
    m_workingHeight = (height + downsample - 1u) / downsample;
    const auto shorter = static_cast<double>((std::min)(m_workingWidth, m_workingHeight));
    m_modelRadius = static_cast<std::uint32_t>((std::max)(1LL, std::llround(request.scale / 100.0 * shorter)));
    m_smoothingRadius = request.smoothness > 0.0
                            ? static_cast<std::uint32_t>((
                                  std::max)(1LL, std::llround(static_cast<double>(m_modelRadius) * request.smoothness)))
                            : 0u;
    m_termCount = request.model == AutoBackgroundModel::Polynomial
                      ? background_detail::polynomialTermCount(request.polynomialDegree)
                      : 0u;
}

const AutoBackgroundRequest& AutoBackgroundPlan::request() const noexcept
{
    return m_request;
}

std::uint32_t AutoBackgroundPlan::workingWidth() const noexcept
{
    return m_workingWidth;
}

std::uint32_t AutoBackgroundPlan::workingHeight() const noexcept
{
    return m_workingHeight;
}

std::uint32_t AutoBackgroundPlan::width() const noexcept
{
    return m_width;
}

std::uint32_t AutoBackgroundPlan::height() const noexcept
{
    return m_height;
}

std::uint32_t AutoBackgroundPlan::downsample() const noexcept
{
    return m_downsample;
}

std::uint32_t AutoBackgroundPlan::modelRadius() const noexcept
{
    return m_modelRadius;
}

std::uint32_t AutoBackgroundPlan::smoothingRadius() const noexcept
{
    return m_smoothingRadius;
}

std::uint32_t AutoBackgroundPlan::termCount() const noexcept
{
    return m_termCount;
}

std::uint64_t AutoBackgroundPlan::workingPasses() const noexcept
{
    if (!m_valid)
    {
        return 0u;
    }
    const std::uint64_t perFit = m_request.model == AutoBackgroundModel::Polynomial
                                     ? 2u
                                     : 2u * kAutoBackgroundLowpassPasses * (kAutoBackgroundFillIterations + 2u);
    const std::uint64_t fits = static_cast<std::uint64_t>(m_request.maxIterations) + 1u;
    const std::uint64_t protection = m_request.protectStructures ? 4u * m_request.maxIterations : 0u;
    const std::uint64_t smoothing = m_smoothingRadius > 0u ? 2u * kAutoBackgroundLowpassPasses : 0u;
    return perFit * fits + protection + m_request.maxIterations + smoothing;
}

std::uint64_t AutoBackgroundPlan::workingBytes() const noexcept
{
    if (!m_valid)
    {
        return 0u;
    }
    const std::uint64_t samples = static_cast<std::uint64_t>(m_workingWidth) * m_workingHeight;
    const std::uint64_t planes = samples * sizeof(float) * 6u;
    const std::uint64_t flags = samples * (m_request.protectStructures ? 3u : 2u);
    const std::uint64_t values = samples * sizeof(double);
    std::uint64_t accumulators = 0u;
    if (m_request.model == AutoBackgroundModel::Polynomial)
    {
        const std::uint64_t order = m_termCount;
        const std::uint64_t chunks =
            (std::max)(std::uint64_t{1}, deterministicChunkCount(m_workingHeight, rowCost(m_workingWidth, order * 2u)));
        accumulators = chunks * (order * order + order) * sizeof(double);
    }
    return planes + flags + values + accumulators;
}

Result<AutoBackgroundPlan> planAutoBackground(const AutoBackgroundRequest& request, std::uint32_t width,
                                              std::uint32_t height)
{
    if (width == 0u || height == 0u)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "an automatic background estimate needs a picture of some size");
    }
    if (!isAutoBackgroundModel(request.model))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the model is not one this engine implements");
    }
    if (!isBackgroundCorrection(request.apply.correction) || !isBackgroundLevel(request.apply.level))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the correction or the level rule is not one this engine implements");
    }
    if (request.apply.level == BackgroundLevel::SampleMean)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "an automatic estimate places no sites, so it has no sample mean to put back");
    }
    if (!std::isfinite(request.scale) || !std::isfinite(request.smoothness) ||
        !std::isfinite(request.protectionThreshold) || !std::isfinite(request.protectionAmount) ||
        !std::isfinite(request.convergence) || !std::isfinite(request.lowRejection) ||
        !std::isfinite(request.highRejection) || !std::isfinite(request.apply.statedLevel) ||
        !std::isfinite(request.apply.guard))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "an automatic background estimate takes finite numbers only");
    }
    if (request.protectionThreshold < 0.0 || request.convergence < 0.0)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the protection threshold and the convergence bound cannot be negative");
    }
    if (request.lowRejection <= 0.0 || request.highRejection <= 0.0)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "a rejection interval needs a positive multiplier on each side");
    }
    if (request.apply.guard <= 0.0)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation, "the division guard has to be above zero");
    }
    if (!std::isfinite(request.apply.dither) || request.apply.dither < 0.0)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "the dither width has to be finite and not negative");
    }
    if (request.apply.clamp.has_value() &&
        (!std::isfinite(request.apply.clamp->lowest) || !std::isfinite(request.apply.clamp->highest) ||
         request.apply.clamp->lowest > request.apply.clamp->highest))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                 "a correction clamp needs two finite endpoints in order");
    }
    if (request.scale < kMinAutoBackgroundScale)
    {
        return backgroundFailure(ErrorCode::OutOfRange, kPlanOperation,
                                 "the model scale is below the interval this estimate accepts");
    }
    if (request.scale > kMaxAutoBackgroundScale)
    {
        return backgroundLimitFailure(
            ErrorCode::OutOfRange, kPlanOperation, "the model scale is outside the interval this estimate accepts",
            static_cast<std::uint64_t>(request.scale), static_cast<std::uint64_t>(kMaxAutoBackgroundScale));
    }
    if (request.smoothness < 0.0)
    {
        return backgroundFailure(ErrorCode::OutOfRange, kPlanOperation, "the final smoothing cannot be negative");
    }
    if (request.smoothness > kMaxAutoBackgroundSmoothness)
    {
        return backgroundLimitFailure(
            ErrorCode::OutOfRange, kPlanOperation, "the final smoothing is outside the interval this estimate accepts",
            static_cast<std::uint64_t>(request.smoothness), static_cast<std::uint64_t>(kMaxAutoBackgroundSmoothness));
    }
    if (request.protectionAmount < 0.0 || request.protectionAmount > 1.0)
    {
        return backgroundFailure(ErrorCode::OutOfRange, kPlanOperation,
                                 "the protection amount lives in the unit interval");
    }
    if (request.maxIterations == 0u || request.maxIterations > kMaxAutoBackgroundIterations)
    {
        return backgroundLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                      "the iteration count is outside its limits", request.maxIterations,
                                      kMaxAutoBackgroundIterations);
    }
    if (request.model == AutoBackgroundModel::Polynomial &&
        (request.polynomialDegree == 0u || request.polynomialDegree > kMaxBackgroundPolynomialDegree))
    {
        return backgroundLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                      "the polynomial degree is outside what this domain accepts",
                                      request.polynomialDegree, kMaxBackgroundPolynomialDegree);
    }
    if (request.downsample == 0u || request.downsample > kMaxAutoBackgroundDownsample ||
        (request.downsample & (request.downsample - 1u)) != 0u)
    {
        return backgroundLimitFailure(ErrorCode::InvalidArgument, kPlanOperation,
                                      "the reduction factor has to be a power of two within its limits",
                                      request.downsample, kMaxAutoBackgroundDownsample);
    }

    // A frame smaller than the requested reduction gets the largest factor that still leaves a
    // working picture, rather than a refusal: the estimate means the same thing either way and a
    // preview of a thumbnail is a real thing to ask for.
    std::uint32_t downsample = request.downsample;
    while (downsample > 1u &&
           ((width + downsample - 1u) / downsample == 0u || (height + downsample - 1u) / downsample == 0u ||
            width < downsample || height < downsample))
    {
        downsample /= 2u;
    }

    const AutoBackgroundPlan plan{request, width, height, downsample};
    if (plan.modelRadius() > kMaxBackgroundEvaluationStride * 16u)
    {
        return backgroundLimitFailure(ErrorCode::OutOfRange, kPlanOperation,
                                      "the model radius this scale implies is larger than the working picture can "
                                      "usefully carry; raise the reduction factor or lower the scale",
                                      plan.modelRadius(), kMaxBackgroundEvaluationStride * 16u);
    }
    return plan;
}

Result<AutoBackgroundResult> estimateImageAutoBackground(const ExecutionContext& execution, SampleType sampleType,
                                                         const void* source, const ImageLayout& sourceLayout,
                                                         const AutoBackgroundPlan& plan,
                                                         std::span<const PlaneView<float>> models,
                                                         std::span<const PlaneView<float>> protection,
                                                         const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImage(execution, sampleType, source, sourceLayout, plan.request().apply.output,
                                          selection, kEstimateOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (plan.downsample() == 0u)
    {
        return backgroundFailure(ErrorCode::NotInitialized, kEstimateOperation,
                                 "an automatic estimate needs a plan produced by planAutoBackground");
    }
    const Result<AutoBackgroundPlan> revalidated = planAutoBackground(plan.request(), plan.width(), plan.height());
    if (!revalidated.hasValue())
    {
        return revalidated.status();
    }
    if (revalidated->workingWidth() != plan.workingWidth() || revalidated->workingHeight() != plan.workingHeight() ||
        revalidated->modelRadius() != plan.modelRadius())
    {
        return backgroundFailure(ErrorCode::NotInitialized, kEstimateOperation,
                                 "the plan was not produced by planAutoBackground");
    }
    if (geometry.width != plan.width() || geometry.height != plan.height())
    {
        return backgroundFailure(ErrorCode::IncompatibleArguments, kEstimateOperation,
                                 "the image is not the picture this plan was made for");
    }
    if (geometry.channelCount > kMaxBackgroundChannels)
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kEstimateOperation,
                                      "an automatic estimate carries a level for at most this many channels",
                                      geometry.channelCount, kMaxBackgroundChannels);
    }
    if (models.size() != geometry.channelCount)
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kEstimateOperation,
                                      "an estimate writes one model plane per channel", models.size(),
                                      geometry.channelCount);
    }
    if (!protection.empty() && protection.size() != geometry.channelCount)
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kEstimateOperation,
                                      "a protection mask is one plane per channel or none at all", protection.size(),
                                      geometry.channelCount);
    }
    for (const PlaneView<float>& plane : models)
    {
        if (plane.width() != geometry.width || plane.height() != geometry.height)
        {
            return backgroundFailure(ErrorCode::IncompatibleArguments, kEstimateOperation,
                                     "a model plane is not the picture being estimated");
        }
    }
    for (const PlaneView<float>& plane : protection)
    {
        if (plane.width() != geometry.width || plane.height() != geometry.height)
        {
            return backgroundFailure(ErrorCode::IncompatibleArguments, kEstimateOperation,
                                     "a protection plane is not the picture being estimated");
        }
    }

    const bool writesProtection = !protection.empty() && plan.request().protectStructures;
    Workspace workspace{};
    const Status taken = takeWorkspace(execution, plan, kEstimateOperation, workspace);
    if (taken.failed())
    {
        return taken;
    }

    AutoBackgroundResult result{};
    result.channelCount = geometry.channelCount;
    result.workingWidth = plan.workingWidth();
    result.workingHeight = plan.workingHeight();
    result.plannedWorkers = execution.admittedConcurrency();
    result.precision = ComputePrecision::Float32;
    result.workingBytes = workspace.bytes();
    result.converged = true;
    const std::uint64_t workingSamples = static_cast<std::uint64_t>(plan.workingWidth()) * plan.workingHeight();

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status reduced = reduceResidentChannel(execution, sampleType, source, sourceLayout, channel, validity,
                                                     workspace.small.view(), plan.downsample(), kEstimateOperation);
        if (reduced.failed())
        {
            return reduced;
        }
        result.imagePasses += 1u;

        ChannelCounters counters{};
        Status estimated = Status::success();
        {
            // Each channel's fit is one share of the operation's progress, measured in fitting rounds, so
            // every round moves the bar.
            const std::uint64_t rounds = static_cast<std::uint64_t>(plan.request().maxIterations) + 1u;
            const ProgressRange share(execution, channel * rounds, (channel + 1u) * rounds,
                                      geometry.channelCount * rounds);
            estimated = estimateChannel(execution, plan, workspace, counters);
        }
        if (estimated.failed())
        {
            return estimated;
        }
        result.iterationsRun = (std::max)(result.iterationsRun, counters.iterationsRun);
        result.converged = result.converged && counters.converged;
        result.consideredSamples += workingSamples;
        result.acceptedSamples += counters.acceptedSamples;
        result.protectedSamples += counters.protectedSamples;
        result.rejectionFallbacks += counters.rejectionFallbacks;
        result.workingPasses += counters.workingPasses;

        const PlaneView<const float> model = workspace.model.constView();
        const PlaneView<float> into = models[channel];
        const std::uint32_t factor = plan.downsample();
        const std::uint32_t width = geometry.width;
        const Status enlarged = parallelFor(execution, geometry.height, rowCost(width, 8u), [&](TaskRange range) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                const auto y = static_cast<std::uint32_t>(index);
                const std::span<float> row = *into.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    row[x] = static_cast<float>(background_detail::interpolateReduced(
                        model, factor, static_cast<double>(x), static_cast<double>(y)));
                }
            }
            return Status::success();
        });
        if (enlarged.failed())
        {
            return enlarged;
        }
        result.imagePasses += 1u;

        if (writesProtection)
        {
            const std::span<const std::uint8_t> flags{static_cast<const std::uint8_t*>(workspace.protection.data()),
                                                      static_cast<std::size_t>(workingSamples)};
            const PlaneView<float> mask = protection[channel];
            const std::uint32_t workingWidth = plan.workingWidth();
            const std::uint32_t workingHeight = plan.workingHeight();
            const Status written = parallelFor(execution, geometry.height, rowCost(width, 4u), [&](TaskRange range) {
                for (std::uint64_t index = range.begin; index < range.end; ++index)
                {
                    const auto y = static_cast<std::uint32_t>(index);
                    const std::span<float> row = *mask.row(y);
                    const std::uint32_t sourceRow =
                        (std::min)(y / factor, workingHeight > 0u ? workingHeight - 1u : 0u);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const std::uint32_t sourceColumn =
                            (std::min)(x / factor, workingWidth > 0u ? workingWidth - 1u : 0u);
                        row[x] = flags[static_cast<std::size_t>(sourceRow) * workingWidth + sourceColumn] != 0u ? 1.0f
                                                                                                                : 0.0f;
                    }
                }
                return Status::success();
            });
            if (written.failed())
            {
                return written;
            }
            result.imagePasses += 1u;
        }
    }

    const std::uint64_t total = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    result.run.selection = ValidityCount{.valid = total, .total = total};
    result.run.outputsWritten = static_cast<std::uint64_t>(geometry.width) * geometry.height * models.size();
    return result;
}

Result<AutoBackgroundResult> removeImageAutoBackground(const ExecutionContext& execution, SampleType sampleType,
                                                       const void* source, const ImageLayout& sourceLayout,
                                                       void* destination, const ImageLayout& destinationLayout,
                                                       const AutoBackgroundPlan& plan,
                                                       const SpatialSelection& selection)
{
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImage(execution, sampleType, source, sourceLayout, plan.request().apply.output,
                                          selection, kRemoveOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    if (plan.downsample() == 0u)
    {
        return backgroundFailure(ErrorCode::NotInitialized, kRemoveOperation,
                                 "an automatic estimate needs a plan produced by planAutoBackground");
    }
    const Result<AutoBackgroundPlan> revalidated = planAutoBackground(plan.request(), plan.width(), plan.height());
    if (!revalidated.hasValue())
    {
        return revalidated.status();
    }
    if (revalidated->workingWidth() != plan.workingWidth() || revalidated->workingHeight() != plan.workingHeight() ||
        revalidated->modelRadius() != plan.modelRadius())
    {
        return backgroundFailure(ErrorCode::NotInitialized, kRemoveOperation,
                                 "the plan was not produced by planAutoBackground");
    }
    if (destination == nullptr)
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kRemoveOperation,
                                 "a correction needs both a source and a destination");
    }
    if (destinationLayout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return backgroundFailure(ErrorCode::InvalidArgument, kRemoveOperation,
                                 "a layout's element size is not the stated representation's");
    }
    if (geometry.width != plan.width() || geometry.height != plan.height() ||
        destinationLayout.extent().width() != geometry.width ||
        destinationLayout.extent().height() != geometry.height ||
        destinationLayout.extent().channelCount() != geometry.channelCount)
    {
        return backgroundFailure(ErrorCode::IncompatibleArguments, kRemoveOperation,
                                 "the source, the destination and the plan are not one picture");
    }
    if (geometry.channelCount > kMaxBackgroundChannels)
    {
        return backgroundLimitFailure(ErrorCode::IncompatibleArguments, kRemoveOperation,
                                      "an automatic estimate carries a level for at most this many channels",
                                      geometry.channelCount, kMaxBackgroundChannels);
    }
    const bool exactlyAliased = source == destination && sourceLayout == destinationLayout;
    if (!exactlyAliased &&
        regionsOverlap(static_cast<const std::uint8_t*>(source), sourceLayout.totalBytes(),
                       static_cast<const std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return backgroundFailure(ErrorCode::Aliasing, kRemoveOperation,
                                 "a correction source overlaps its destination without being the same image");
    }

    Workspace workspace{};
    const Status taken = takeWorkspace(execution, plan, kRemoveOperation, workspace);
    if (taken.failed())
    {
        return taken;
    }

    AutoBackgroundResult result{};
    result.channelCount = geometry.channelCount;
    result.workingWidth = plan.workingWidth();
    result.workingHeight = plan.workingHeight();
    result.plannedWorkers = execution.admittedConcurrency();
    result.precision = ComputePrecision::Float32;
    result.workingBytes = workspace.bytes();
    result.converged = true;

    const std::uint64_t workingSamples = static_cast<std::uint64_t>(plan.workingWidth()) * plan.workingHeight();
    std::uint64_t accepted = 0u;

    for (std::uint32_t channel = 0u; channel < geometry.channelCount; ++channel)
    {
        const KernelValidityView validity = spatial_detail::channelValidity(selection, channel);
        const Status reduced = reduceResidentChannel(execution, sampleType, source, sourceLayout, channel, validity,
                                                     workspace.small.view(), plan.downsample(), kRemoveOperation);
        if (reduced.failed())
        {
            return reduced;
        }
        result.imagePasses += 1u;

        ChannelCounters counters{};
        Status estimated = Status::success();
        {
            // Each channel's fit is one share of the operation's progress, measured in fitting rounds, so
            // every round moves the bar.
            const std::uint64_t rounds = static_cast<std::uint64_t>(plan.request().maxIterations) + 1u;
            const ProgressRange share(execution, channel * rounds, (channel + 1u) * rounds,
                                      geometry.channelCount * rounds);
            estimated = estimateChannel(execution, plan, workspace, counters);
        }
        if (estimated.failed())
        {
            return estimated;
        }
        result.iterationsRun = (std::max)(result.iterationsRun, counters.iterationsRun);
        result.converged = result.converged && counters.converged;
        result.consideredSamples += workingSamples;
        result.acceptedSamples += counters.acceptedSamples;
        result.protectedSamples += counters.protectedSamples;
        result.rejectionFallbacks += counters.rejectionFallbacks;
        result.workingPasses += counters.workingPasses;

        double level = 0.0;
        const Status resolved =
            resolveLevel(execution, plan, workspace, sampleType, source, sourceLayout, channel, validity, level);
        if (resolved.failed())
        {
            return resolved;
        }
        result.appliedLevel[channel] = level;
        CorrectionCounters correctedHere{};
        const Status corrected =
            correctResidentChannel(execution, sampleType, source, sourceLayout, destination, destinationLayout, channel,
                                   validity, plan, workspace, level, correctedHere);
        if (corrected.failed())
        {
            return corrected;
        }
        result.guardedSamples += correctedHere.guarded;
        result.clampedSamples += correctedHere.clamped;
        result.samplesChanged += correctedHere.changed;
        accepted += correctedHere.accepted;
        result.imagePasses += 1u;
        if (plan.request().apply.level == BackgroundLevel::SourceMean)
        {
            result.imagePasses += 1u;
        }
    }

    const std::uint64_t total = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    result.run.selection = ValidityCount{.valid = accepted, .total = total};
    result.run.outputsWritten = accepted;
    return result;
}

} // namespace an
