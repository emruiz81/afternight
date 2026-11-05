// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - box, detail, edge-tightening, morphology and adaptive-histogram filters.
//
// Every operation here is a thin layer over the spatial family. The box average is two uniform tap
// arrays handed to `convolveImageSeparable`; the detail filters run the Gaussian's taps through
// `convolveSeparablePlane` into the destination plane and then combine it with the source in place;
// edge tightening runs four fixed three-by-three stencils through `convolveDensePlane` and sums their
// magnitudes in one working plane. Only morphology and equalization read neighbourhoods of their
// own: morphology resolves a coordinate outside the image through `kernelBorderIndex`, and
// equalization reads no sample outside the image at all, so no operation carries a border
// convention the stencil family does not.
//
// Morphology is a direct scan of the footprint for every output rather than a separable or running
// minimum, because a named or caller-supplied footprint is not in general separable; its cost is
// the footprint's area per output. Equalization builds one clipped cumulative table per tile and
// interpolates between the four tiles around each pixel, with the clipped counts returned by the
// classic even rule rather than piled onto the lowest bins.

#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>

#include <AfterNightEngine/Algorithms/Spatial/Convolution.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/ReferenceKernelInternal.h"
#include "kernels/reference/SpatialPlane.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace an
{
namespace
{

using spatial_detail::filterEveryChannel;
using spatial_detail::spatialFailure;
using spatial_detail::SpatialGeometry;
using spatial_detail::spatialLimitFailure;
using spatial_detail::SpatialPlaneBuffer;

constexpr const char* kBoxOperation = "an::filterImageBox";
constexpr const char* kDetailOperation = "an::filterImageDetail";
constexpr const char* kEdgeTightenOperation = "an::filterImageEdgeTighten";
constexpr const char* kMorphologyOperation = "an::filterImageMorphology";
constexpr const char* kClaheOperation = "an::equalizeImageAdaptiveHistogram";

[[nodiscard]] Status checkOutputPolicy(const SpatialOutputPolicy& output, const char* operation)
{
    if (!isOutputPrecisionContract(output.contract))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation, "the output contract is outside its enumeration");
    }
    const Status conversion = checkConversionPolicy(output.writeBack, operation);
    if (conversion.failed())
    {
        return conversion;
    }
    if (output.writeBack.domain != DomainMapping::CodeValue)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a local filter works in stored code values and cannot also perform a domain mapping");
    }
    return Status::success();
}

[[nodiscard]] Status checkBorder(const StencilBorderPolicy& border, const char* operation)
{
    if (!isKernelBorderMode(border.border) || border.border == KernelBorderMode::NotApplicable ||
        !std::isfinite(border.constantBorder) || !std::isfinite(border.invalidNeighbor))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a filter needs a known border rule and finite substitute values");
    }
    return Status::success();
}

[[nodiscard]] TaskCost rowCost(std::uint32_t width, std::uint64_t workPerPixel) noexcept
{
    return TaskCost{.bytesPerItem = static_cast<std::uint64_t>(width) * 8u,
                    .operationsPerItem = static_cast<std::uint64_t>(width) * workPerPixel};
}

[[nodiscard]] bool centreIsValid(PlaneView<const float> input, const KernelValidityView& validity, std::uint32_t x,
                                 std::uint32_t y) noexcept
{
    return sampleIsValidByValue(validity.descriptor, (*input.row(y))[x]) && plane_detail::maskAllows(validity, x, y);
}

/// Whether one offset from the centre belongs to the request's footprint.
///
/// The named shapes are evaluated from the offset rather than stored, so a plan holds no mask; a
/// custom footprint is read from the caller's row-major bytes.
[[nodiscard]] bool footprintContains(const MorphologyRequest& request, std::int64_t dx, std::int64_t dy) noexcept
{
    const std::int64_t ax = std::abs(dx);
    const std::int64_t ay = std::abs(dy);
    switch (request.shape)
    {
    case MorphologyShape::Rectangle:
        return true;
    case MorphologyShape::Ellipse:
        if (request.radiusX == 0u)
        {
            return dx == 0;
        }
        if (request.radiusY == 0u)
        {
            return dy == 0;
        }
        return static_cast<double>(dx * dx) / static_cast<double>(request.radiusX * request.radiusX) +
                   static_cast<double>(dy * dy) / static_cast<double>(request.radiusY * request.radiusY) <=
               1.0;
    case MorphologyShape::Diamond:
        return ax + ay <= static_cast<std::int64_t>(request.radiusX);
    case MorphologyShape::Cross:
        return dx == 0 || dy == 0;
    case MorphologyShape::DiagonalCross:
        return ax == ay;
    case MorphologyShape::Custom: {
        const std::uint64_t footprintWidth = 2u * static_cast<std::uint64_t>(request.radiusX) + 1u;
        const std::uint64_t x = static_cast<std::uint64_t>(dx + static_cast<std::int64_t>(request.radiusX));
        const std::uint64_t y = static_cast<std::uint64_t>(dy + static_cast<std::int64_t>(request.radiusY));
        return request.customFootprint[static_cast<std::size_t>(y * footprintWidth + x)] != 0u;
    }
    }
    return false;
}

/// One erosion, dilation or hit-miss pass over a plane.
///
/// `dilate` selects the maximum rather than the minimum, and `hitMiss` writes one where every
/// footprint sample is nonzero and zero elsewhere. Every footprint position is visited for every
/// accepted centre. A coordinate outside the plane follows the border mode, and one the validity
/// rule rejects - judged on this pass's own input - reads the request's constant. A rejected centre
/// is skipped and its output sample is not written. `written`, when not null, gains the outputs
/// produced; an intermediate pass passes null.
[[nodiscard]] Status morphologyPrimitive(const ExecutionContext& execution, PlaneView<const float> input,
                                         PlaneView<float> output, const KernelValidityView& validity,
                                         const MorphologyRequest& request, bool dilate, bool hitMiss,
                                         std::atomic<std::uint64_t>* written)
{
    const std::int64_t width = input.width();
    const std::int64_t height = input.height();
    const std::int64_t rx = request.radiusX;
    const std::int64_t ry = request.radiusY;
    const std::uint64_t footprint = (2u * static_cast<std::uint64_t>(request.radiusX) + 1u) *
                                    (2u * static_cast<std::uint64_t>(request.radiusY) + 1u);
    return parallelFor(execution, input.height(), rowCost(input.width(), footprint * 3u), [&](TaskRange range) {
        std::uint64_t localWritten = 0u;
        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
            std::span<float> destination = *output.row(y);
            for (std::uint32_t x = 0u; x < input.width(); ++x)
            {
                if (!centreIsValid(input, validity, x, y))
                {
                    continue;
                }
                float answer = dilate ? -(std::numeric_limits<float>::max)() : (std::numeric_limits<float>::max)();
                bool foreground = true;
                for (std::int64_t dy = -ry; dy <= ry; ++dy)
                {
                    for (std::int64_t dx = -rx; dx <= rx; ++dx)
                    {
                        if (!footprintContains(request, dx, dy))
                        {
                            continue;
                        }
                        const std::int64_t sy =
                            kernelBorderIndex(static_cast<std::int64_t>(y) + dy, height, request.border);
                        const std::int64_t sx =
                            kernelBorderIndex(static_cast<std::int64_t>(x) + dx, width, request.border);
                        float sample = request.constantBorder;
                        if (sx != width && sy != height)
                        {
                            sample = (*input.row(static_cast<std::uint32_t>(sy)))[static_cast<std::size_t>(sx)];
                            if (!sampleIsValidByValue(validity.descriptor, sample) ||
                                !plane_detail::maskAllows(validity, static_cast<std::uint32_t>(sx),
                                                          static_cast<std::uint32_t>(sy)))
                            {
                                sample = request.constantBorder;
                            }
                        }
                        answer = dilate ? (std::max)(answer, sample) : (std::min)(answer, sample);
                        foreground = foreground && sample != 0.0f;
                    }
                }
                destination[x] = hitMiss ? (foreground ? 1.0f : 0.0f) : answer;
                ++localWritten;
            }
        }
        if (written != nullptr)
        {
            written->fetch_add(localWritten, std::memory_order_relaxed);
        }
        return Status::success();
    });
}

/// Writes the difference of two planes at every centre `validityInput` accepts.
///
/// `subtractLeft` writes `right - left` rather than `left - right`. `written`, when not null, gains
/// the outputs produced.
[[nodiscard]] Status combineMorphology(const ExecutionContext& execution, PlaneView<const float> left,
                                       PlaneView<const float> right, PlaneView<const float> validityInput,
                                       PlaneView<float> output, const KernelValidityView& validity, bool subtractLeft,
                                       std::atomic<std::uint64_t>* written)
{
    return parallelFor(execution, output.height(), rowCost(output.width(), 3u), [&](TaskRange range) {
        std::uint64_t localWritten = 0u;
        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
            const std::span<const float> a = *left.row(y);
            const std::span<const float> b = *right.row(y);
            std::span<float> destination = *output.row(y);
            for (std::uint32_t x = 0u; x < output.width(); ++x)
            {
                if (!centreIsValid(validityInput, validity, x, y))
                {
                    continue;
                }
                destination[x] = subtractLeft ? b[x] - a[x] : a[x] - b[x];
                ++localWritten;
            }
        }
        if (written != nullptr)
        {
            written->fetch_add(localWritten, std::memory_order_relaxed);
        }
        return Status::success();
    });
}

/// The equalization bin a sample falls in; a sample outside the stated range lands in an end bin.
[[nodiscard]] std::uint32_t histogramBin(float sample, const AdaptiveHistogramRequest& request) noexcept
{
    // Equal-width bins over the stated range, the last of them closed at the top: a code value
    // `k / (binCount - 1)` lands in bin `k` with a margin of `k / (binCount - 1)` of a bin, so the
    // rounding of its float representation cannot move it into the bin below, which scaling by
    // `binCount - 1` allowed for about half of all code values.
    const double normalized = (static_cast<double>(sample) - request.lowest) / (request.highest - request.lowest);
    // A NaN has no place on the scale and counts in the first bin, as a sample below the range does: clamping leaves it
    // a NaN, and converting that to an integer is undefined, so where it landed was the compiler's choice.
    if (std::isnan(normalized))
    {
        return 0u;
    }
    const double scaled = (std::clamp)(normalized, 0.0, 1.0) * static_cast<double>(request.binCount);
    return (std::min)(static_cast<std::uint32_t>(scaled), request.binCount - 1u);
}

} // namespace

BoxFilterPlan::BoxFilterPlan(const BoxFilterRequest& request) noexcept : m_request(request) {}

const BoxFilterRequest& BoxFilterPlan::request() const noexcept
{
    return m_request;
}

std::uint32_t BoxFilterPlan::horizontalRadius() const noexcept
{
    return m_request.radiusX;
}

std::uint32_t BoxFilterPlan::verticalRadius() const noexcept
{
    return m_request.radiusY;
}

Result<BoxFilterPlan> planBoxFilter(const BoxFilterRequest& request)
{
    if (request.radiusX > kMaxStencilRadius || request.radiusY > kMaxStencilRadius)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kBoxOperation, "a box reaches farther than one stencil may",
                                   (std::max)(request.radiusX, request.radiusY), kMaxStencilRadius);
    }
    Status ready = checkBorder(request.borders, kBoxOperation);
    if (ready.failed())
    {
        return ready;
    }
    ready = checkOutputPolicy(request.output, kBoxOperation);
    if (ready.failed())
    {
        return ready;
    }
    return BoxFilterPlan{request};
}

Result<SpatialFilterResult> filterImageBox(const ExecutionContext& execution, SampleType sampleType, const void* source,
                                           const ImageLayout& sourceLayout, void* destination,
                                           const ImageLayout& destinationLayout, const BoxFilterPlan& plan,
                                           const SpatialSelection& selection)
{
    const Result<BoxFilterPlan> checked = planBoxFilter(plan.request());
    if (!checked.hasValue())
    {
        return checked.status();
    }
    std::array<double, kMaxStencilTaps> horizontal{};
    std::array<double, kMaxStencilTaps> vertical{};
    const std::uint32_t horizontalCount = 2u * plan.horizontalRadius() + 1u;
    const std::uint32_t verticalCount = 2u * plan.verticalRadius() + 1u;
    std::fill_n(horizontal.begin(), horizontalCount, 1.0 / static_cast<double>(horizontalCount));
    std::fill_n(vertical.begin(), verticalCount, 1.0 / static_cast<double>(verticalCount));
    const SeparableConvolutionRequest request{.horizontalTaps =
                                                  std::span<const double>{horizontal.data(), horizontalCount},
                                              .verticalTaps = std::span<const double>{vertical.data(), verticalCount},
                                              .borders = plan.request().borders,
                                              .output = plan.request().output};
    return convolveImageSeparable(execution, sampleType, source, sourceLayout, destination, destinationLayout, request,
                                  selection);
}

DetailFilterPlan::DetailFilterPlan(const DetailFilterRequest& request, const GaussianPlan& gaussian) noexcept
    : m_request(request), m_gaussian(gaussian)
{
}

const DetailFilterRequest& DetailFilterPlan::request() const noexcept
{
    return m_request;
}

const GaussianPlan& DetailFilterPlan::gaussian() const noexcept
{
    return m_gaussian;
}

Result<DetailFilterPlan> planDetailFilter(const DetailFilterRequest& request)
{
    if (!isDetailFilterMethod(request.method))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kDetailOperation,
                              "the detail-filter method is outside its enumeration");
    }
    if (!std::isfinite(request.amount) || request.amount < 0.0 || !std::isfinite(request.offset) ||
        !std::isfinite(request.lowest) || !std::isfinite(request.highest) || request.lowest > request.highest)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kDetailOperation,
                              "detail arithmetic needs a non-negative finite amount, a finite offset and ordered "
                              "finite clamp bounds");
    }
    GaussianRequest gaussian{.sigmaX = request.sigmaX,
                             .sigmaY = request.sigmaY,
                             .method = GaussianMethod::SeparableTaps,
                             .borders = request.borders,
                             .output = request.output};
    Result<GaussianPlan> planned = planGaussianBlur(gaussian);
    if (!planned.hasValue())
    {
        return planned.status();
    }
    return DetailFilterPlan{request, *planned};
}

Result<SpatialFilterResult> filterImageDetail(const ExecutionContext& execution, SampleType sampleType,
                                              const void* source, const ImageLayout& sourceLayout, void* destination,
                                              const ImageLayout& destinationLayout, const DetailFilterPlan& plan,
                                              const SpatialSelection& selection)
{
    const Result<DetailFilterPlan> checked = planDetailFilter(plan.request());
    if (!checked.hasValue())
    {
        return checked.status();
    }
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kDetailOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }

    std::array<double, kMaxStencilTaps> horizontal{};
    std::array<double, kMaxStencilTaps> vertical{};
    const std::uint32_t horizontalCount = 2u * plan.gaussian().horizontalRadius() + 1u;
    const std::uint32_t verticalCount = 2u * plan.gaussian().verticalRadius() + 1u;
    Result<std::uint32_t> horizontalWritten =
        writeGaussianTaps(plan.request().sigmaX, plan.gaussian().horizontalRadius(),
                          std::span<double>{horizontal.data(), horizontalCount});
    Result<std::uint32_t> verticalWritten = writeGaussianTaps(plan.request().sigmaY, plan.gaussian().verticalRadius(),
                                                              std::span<double>{vertical.data(), verticalCount});
    if (!horizontalWritten.hasValue())
    {
        return horizontalWritten.status();
    }
    if (!verticalWritten.hasValue())
    {
        return verticalWritten.status();
    }

    SpatialFilterResult result{};
    const SeparableStencilRequest lowPass{.horizontalTaps = std::span<const double>{horizontal.data(), horizontalCount},
                                          .verticalTaps = std::span<const double>{vertical.data(), verticalCount},
                                          .borders = plan.request().borders};
    std::uint64_t scratchBytes = 0u;
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        plan.request().output.writeBack, geometry, kDetailOperation, result,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            const Result<StencilExecutionResult> blurred =
                convolveSeparablePlane(execution, input, output, lowPass, validity);
            if (!blurred.hasValue())
            {
                return blurred.status();
            }
            scratchBytes = (std::max)(scratchBytes, blurred->scratchBytes);
            result.run.selection.total += blurred->run.selection.total;
            result.run.selection.valid += blurred->run.selection.valid;
            result.run.outputsWritten += blurred->run.outputsWritten;
            result.plannedWorkers = (std::max)(result.plannedWorkers, blurred->plannedWorkers);
            const DetailFilterRequest& request = plan.request();
            return parallelFor(execution, input.height(), rowCost(input.width(), 7u), [&](TaskRange range) {
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                    const std::span<const float> original = *input.row(y);
                    std::span<float> answer = *output.row(y);
                    for (std::uint32_t x = 0u; x < input.width(); ++x)
                    {
                        if (!centreIsValid(input, validity, x, y))
                        {
                            continue;
                        }
                        const double sourceValue = original[x];
                        const double lowPassValue = answer[x];
                        const double value =
                            request.method == DetailFilterMethod::HighPass
                                ? sourceValue - lowPassValue + request.offset
                                : sourceValue + request.amount * (sourceValue - lowPassValue) + request.offset;
                        answer[x] = static_cast<float>((std::clamp)(value, request.lowest, request.highest));
                    }
                }
                return Status::success();
            });
        });
    if (filtered.failed())
    {
        return filtered;
    }
    result.horizontalRadius = plan.gaussian().horizontalRadius();
    result.verticalRadius = plan.gaussian().verticalRadius();
    result.imagePasses = 3u;
    result.workingBytes += scratchBytes;
    result.precision = ComputePrecision::Float32;
    return result;
}

EdgeTightenPlan::EdgeTightenPlan(const EdgeTightenRequest& request) noexcept : m_request(request) {}

const EdgeTightenRequest& EdgeTightenPlan::request() const noexcept
{
    return m_request;
}

Result<EdgeTightenPlan> planEdgeTighten(const EdgeTightenRequest& request)
{
    if (!std::isfinite(request.amount) || request.amount < 0.0 || !std::isfinite(request.offset) ||
        !std::isfinite(request.lowest) || !std::isfinite(request.highest) || request.lowest > request.highest)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kEdgeTightenOperation,
                              "edge tightening needs a non-negative finite amount, a finite offset and ordered "
                              "finite clamp bounds");
    }
    const Status border = checkBorder(request.borders, kEdgeTightenOperation);
    if (border.failed())
    {
        return border;
    }
    const Status output = checkOutputPolicy(request.output, kEdgeTightenOperation);
    if (output.failed())
    {
        return output;
    }
    return EdgeTightenPlan{request};
}

namespace
{

/// The four directional kernels of the edge tightening, row-major from the top-left tap.
constexpr std::array<std::array<double, 9>, 4> kEdgeDirectionalKernels{{
    {1.0, 2.0, 1.0, 0.0, 0.0, 0.0, -1.0, -2.0, -1.0},
    {-1.0, 0.0, 1.0, -2.0, 0.0, 2.0, -1.0, 0.0, 1.0},
    {2.0, 1.0, 0.0, 1.0, 0.0, -1.0, 0.0, -1.0, -2.0},
    {0.0, 1.0, 2.0, -1.0, 0.0, 1.0, -2.0, -1.0, 0.0},
}};

/// One plane's edge tightening in one pass, writing the bits the eight-pass composition writes.
///
/// Each valid centre's nine neighbours are read once, the outside ones resolved by the border rule and
/// the invalid ones replaced by `invalidNeighbor`, exactly as the dense stencil reads them; each of the
/// four responses is then the stencil's own sum - every tap, zero coefficients included, in order, in
/// Float64 - narrowed to Float32, and the magnitudes, the scale, the subtraction, the offset and the
/// clamp follow the accumulation passes' order. A neighbourhood wholly inside a plane whose rule admits
/// every sample is read straight from its three rows, and a row's run of them goes to the compiled
/// interior run where the execution's table has one - the same arithmetic, lane for lane.
///
/// @return success, or the execution's stop.
[[nodiscard]] Status tightenEdgesInOnePass(const ExecutionContext& execution, PlaneView<const float> input,
                                           PlaneView<float> output, const KernelValidityView& validity,
                                           const EdgeTightenRequest& request, std::uint64_t& valid)
{
    const std::uint32_t width = input.width();
    const std::uint32_t height = input.height();
    const reference_detail::ValidityAccessor accepts(validity);
    const bool everySample = validity.descriptor.isAllValid() && validity.mask == nullptr;
    const EdgeTightenRowRunFn compiledRow = everySample && width >= 3u ? edgeTightenRowRunFor(execution) : nullptr;
    const StencilBorderPolicy& borders = request.borders;
    const auto neighbour = [&](std::int64_t x, std::int64_t y) -> double {
        const std::int64_t column = kernelBorderIndex(x, static_cast<std::int64_t>(width), borders.border);
        const std::int64_t row = kernelBorderIndex(y, static_cast<std::int64_t>(height), borders.border);
        if (column == static_cast<std::int64_t>(width) || row == static_cast<std::int64_t>(height))
        {
            return static_cast<double>(borders.constantBorder);
        }
        const auto atColumn = static_cast<std::uint32_t>(column);
        const auto atRow = static_cast<std::uint32_t>(row);
        const float sample = (*input.row(atRow))[atColumn];
        return static_cast<double>(accepts.accepts(sample, atColumn, atRow) ? sample : borders.invalidNeighbor);
    };
    std::atomic<std::uint64_t> written{0u};
    const Status ran = parallelFor(execution, height, rowCost(width, 80u), [&](TaskRange range) -> Status {
        std::uint64_t producedHere = 0u;
        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
        {
            const Status stopped = execution.checkCancellation();
            if (stopped.failed())
            {
                return stopped;
            }
            const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
            const std::span<const float> current = *input.row(y);
            const std::span<float> response = *output.row(y);
            const bool innerRow = y >= 1u && y + 1u < height;
            const float* const above = innerRow ? input.row(y - 1u)->data() : nullptr;
            const float* const below = innerRow ? input.row(y + 1u)->data() : nullptr;
            const bool compiled = compiledRow != nullptr && innerRow;
            if (compiled)
            {
                compiledRow(above + 1u, current.data() + 1u, below + 1u, width - 2u, kEdgeDirectionalKernels.data(),
                            request.amount, request.offset, request.lowest, request.highest, response.data() + 1u);
                producedHere += width - 2u;
            }
            for (std::uint32_t x = 0u; x < width; x = compiled && x == 0u ? width - 1u : x + 1u)
            {
                if (!accepts.accepts(current[x], x, y))
                {
                    continue;
                }
                std::array<double, 9> taps{};
                if (everySample && innerRow && x >= 1u && x + 1u < width)
                {
                    taps = {static_cast<double>(above[x - 1u]), static_cast<double>(above[x]),
                            static_cast<double>(above[x + 1u]), static_cast<double>(current[x - 1u]),
                            static_cast<double>(current[x]),    static_cast<double>(current[x + 1u]),
                            static_cast<double>(below[x - 1u]), static_cast<double>(below[x]),
                            static_cast<double>(below[x + 1u])};
                }
                else
                {
                    std::size_t tap = 0u;
                    for (std::int64_t offsetY = -1; offsetY <= 1; ++offsetY)
                    {
                        for (std::int64_t offsetX = -1; offsetX <= 1; ++offsetX, ++tap)
                        {
                            taps[tap] = neighbour(static_cast<std::int64_t>(x) + offsetX,
                                                  static_cast<std::int64_t>(y) + offsetY);
                        }
                    }
                }
                float magnitude = 0.0f;
                for (std::size_t direction = 0u; direction < kEdgeDirectionalKernels.size(); ++direction)
                {
                    double sum = 0.0;
                    for (std::size_t tap = 0u; tap < taps.size(); ++tap)
                    {
                        sum += kEdgeDirectionalKernels[direction][tap] * taps[tap];
                    }
                    const float directional = static_cast<float>(sum);
                    if (direction == 0u)
                    {
                        magnitude = std::abs(directional);
                    }
                    else if (direction + 1u < kEdgeDirectionalKernels.size())
                    {
                        magnitude += std::abs(directional);
                    }
                    else
                    {
                        const double edgeValue = 0.05 * static_cast<double>(magnitude + std::abs(directional));
                        const double value =
                            static_cast<double>(current[x]) - request.amount * edgeValue + request.offset;
                        response[x] = static_cast<float>((std::clamp)(value, request.lowest, request.highest));
                    }
                }
                ++producedHere;
            }
        }
        written.fetch_add(producedHere, std::memory_order_relaxed);
        return Status::success();
    });
    valid = written.load(std::memory_order_relaxed);
    return ran;
}

} // namespace

Result<SpatialFilterResult> filterImageEdgeTighten(const ExecutionContext& execution, SampleType sampleType,
                                                   const void* source, const ImageLayout& sourceLayout,
                                                   void* destination, const ImageLayout& destinationLayout,
                                                   const EdgeTightenPlan& plan, const SpatialSelection& selection)
{
    const Result<EdgeTightenPlan> checked = planEdgeTighten(plan.request());
    if (!checked.hasValue())
    {
        return checked.status();
    }
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kEdgeTightenOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    SpatialFilterResult result{};
    if (execution.backend() != ComputeBackend::Reference)
    {
        const Status fused = filterEveryChannel(
            execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
            plan.request().output.writeBack, geometry, kEdgeTightenOperation, result,
            [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
                std::uint64_t valid = 0u;
                const Status tightened =
                    tightenEdgesInOnePass(execution, input, output, validity, plan.request(), valid);
                if (tightened.failed())
                {
                    return tightened;
                }
                result.run.selection.total += static_cast<std::uint64_t>(input.width()) * input.height();
                result.run.selection.valid += valid;
                result.run.outputsWritten += valid;
                return Status::success();
            });
        if (fused.failed())
        {
            return fused;
        }
        result.horizontalRadius = 1u;
        result.verticalRadius = 1u;
        result.imagePasses = 1u;
        result.plannedWorkers = (std::max)(result.plannedWorkers, execution.admittedConcurrency());
        result.precision = ComputePrecision::Float32;
        return result;
    }

    Result<SpatialPlaneBuffer> edgeMemory = SpatialPlaneBuffer::own(execution, geometry, kEdgeTightenOperation);
    if (!edgeMemory.hasValue())
    {
        return edgeMemory.status();
    }
    SpatialPlaneBuffer edge = std::move(edgeMemory).value();
    const auto& kDirectionalKernels = kEdgeDirectionalKernels;

    result.workingBytes += edge.bytes();
    std::uint64_t scratchBytes = 0u;
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        plan.request().output.writeBack, geometry, kEdgeTightenOperation, result,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            for (std::size_t direction = 0u; direction < kDirectionalKernels.size(); ++direction)
            {
                const DenseStencilRequest stencil{.coefficients = kDirectionalKernels[direction],
                                                  .width = 3u,
                                                  .height = 3u,
                                                  .borders = plan.request().borders};
                const Result<StencilExecutionResult> convolved =
                    convolveDensePlane(execution, input, output, stencil, validity);
                if (!convolved.hasValue())
                {
                    return convolved.status();
                }
                scratchBytes = (std::max)(scratchBytes, convolved->scratchBytes);
                result.plannedWorkers = (std::max)(result.plannedWorkers, convolved->plannedWorkers);
                if (direction == 0u)
                {
                    result.run.selection.total += convolved->run.selection.total;
                    result.run.selection.valid += convolved->run.selection.valid;
                    result.run.outputsWritten += convolved->run.outputsWritten;
                }

                const bool finalDirection = direction + 1u == kDirectionalKernels.size();
                const Status accumulated = parallelFor(
                    execution, input.height(), rowCost(input.width(), finalDirection ? 10u : 4u), [&](TaskRange range) {
                        for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                        {
                            const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                            const std::span<const float> original = *input.row(y);
                            std::span<float> response = *output.row(y);
                            std::span<float> magnitude = *edge.view().row(y);
                            for (std::uint32_t x = 0u; x < input.width(); ++x)
                            {
                                if (!centreIsValid(input, validity, x, y))
                                {
                                    continue;
                                }
                                if (direction == 0u)
                                {
                                    magnitude[x] = std::abs(response[x]);
                                }
                                else if (!finalDirection)
                                {
                                    magnitude[x] += std::abs(response[x]);
                                }
                                else
                                {
                                    const double edgeValue =
                                        0.05 * static_cast<double>(magnitude[x] + std::abs(response[x]));
                                    const double value = static_cast<double>(original[x]) -
                                                         plan.request().amount * edgeValue + plan.request().offset;
                                    response[x] = static_cast<float>(
                                        (std::clamp)(value, plan.request().lowest, plan.request().highest));
                                }
                            }
                        }
                        return Status::success();
                    });
                if (accumulated.failed())
                {
                    return accumulated;
                }
            }
            return Status::success();
        });
    if (filtered.failed())
    {
        return filtered;
    }
    result.horizontalRadius = 1u;
    result.verticalRadius = 1u;
    result.imagePasses = 8u;
    result.workingBytes += scratchBytes;
    result.precision = ComputePrecision::Float32;
    return result;
}

MorphologyPlan::MorphologyPlan(const MorphologyRequest& request) noexcept : m_request(request) {}

const MorphologyRequest& MorphologyPlan::request() const noexcept
{
    return m_request;
}

std::uint64_t MorphologyPlan::imagePasses() const noexcept
{
    std::uint64_t passes = 1u;
    if (m_request.operation == MorphologyOperation::Open || m_request.operation == MorphologyOperation::Close)
    {
        passes = 2u;
    }
    else if (m_request.operation == MorphologyOperation::Gradient ||
             m_request.operation == MorphologyOperation::TopHat || m_request.operation == MorphologyOperation::BlackHat)
    {
        passes = 3u;
    }
    return static_cast<std::uint64_t>(m_request.iterations) * passes;
}

Result<MorphologyPlan> planMorphology(const MorphologyRequest& request)
{
    if (!isMorphologyOperation(request.operation) || !isMorphologyShape(request.shape))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kMorphologyOperation,
                              "the morphology operation or footprint is outside its enumeration");
    }
    if (!isKernelBorderMode(request.border) || request.border == KernelBorderMode::NotApplicable ||
        !std::isfinite(request.constantBorder))
    {
        return spatialFailure(ErrorCode::InvalidArgument, kMorphologyOperation,
                              "morphology needs a known border and a finite constant");
    }
    if (request.iterations == 0u || request.iterations > kMaxMorphologyIterations)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kMorphologyOperation,
                                   "morphology runs at least once and no more than its iteration ceiling",
                                   request.iterations, kMaxMorphologyIterations);
    }
    if (request.radiusX > kMaxStencilRadius || request.radiusY > kMaxStencilRadius)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kMorphologyOperation,
                                   "a morphology footprint reaches farther than one stencil may",
                                   (std::max)(request.radiusX, request.radiusY), kMaxStencilRadius);
    }
    if ((request.shape == MorphologyShape::Diamond || request.shape == MorphologyShape::DiagonalCross) &&
        request.radiusX != request.radiusY)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kMorphologyOperation,
                              "a diamond or diagonal cross has one radius, so both stated radii must agree");
    }
    const std::uint64_t footprintWidth = 2u * static_cast<std::uint64_t>(request.radiusX) + 1u;
    const std::uint64_t footprintHeight = 2u * static_cast<std::uint64_t>(request.radiusY) + 1u;
    const std::uint64_t expectedFootprint = footprintWidth * footprintHeight;
    if (request.shape == MorphologyShape::Custom)
    {
        if (request.customFootprint.size() != expectedFootprint)
        {
            return spatialFailure(ErrorCode::IncompatibleArguments, kMorphologyOperation,
                                  "a custom morphology footprint has exactly one byte per position");
        }
        if (std::ranges::none_of(request.customFootprint, [](std::uint8_t value) { return value != 0u; }))
        {
            return spatialFailure(ErrorCode::InvalidArgument, kMorphologyOperation,
                                  "a custom morphology footprint includes at least one position");
        }
    }
    else if (!request.customFootprint.empty())
    {
        return spatialFailure(ErrorCode::InvalidArgument, kMorphologyOperation,
                              "a named morphology shape does not also accept custom footprint bytes");
    }
    const Status output = checkOutputPolicy(request.output, kMorphologyOperation);
    if (output.failed())
    {
        return output;
    }
    return MorphologyPlan{request};
}

Result<SpatialFilterResult> filterImageMorphology(const ExecutionContext& execution, SampleType sampleType,
                                                  const void* source, const ImageLayout& sourceLayout,
                                                  void* destination, const ImageLayout& destinationLayout,
                                                  const MorphologyPlan& plan, const SpatialSelection& selection)
{
    const Result<MorphologyPlan> checked = planMorphology(plan.request());
    if (!checked.hasValue())
    {
        return checked.status();
    }
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kMorphologyOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }

    std::array<spatial_detail::SpatialPlaneBuffer, 4> work{};
    for (spatial_detail::SpatialPlaneBuffer& buffer : work)
    {
        Result<spatial_detail::SpatialPlaneBuffer> taken =
            spatial_detail::SpatialPlaneBuffer::own(execution, geometry, kMorphologyOperation);
        if (!taken.hasValue())
        {
            return taken.status();
        }
        buffer = std::move(*taken);
    }

    SpatialFilterResult result{};
    for (const spatial_detail::SpatialPlaneBuffer& buffer : work)
    {
        result.workingBytes += buffer.bytes();
    }
    std::atomic<std::uint64_t> writes{0u};
    const MorphologyRequest& request = plan.request();
    // A pass skips a centre its validity rule rejects and leaves that sample of its output unwritten, and
    // the working planes come from the engine context uninitialised. So when anything can be rejected,
    // a working plane that a later pass reads is first seeded with what its own pass reads: a skipped
    // centre then carries the rejected sample forward, which the next pass rejects again under a rule
    // that judges values, instead of carrying whatever the memory held into the answer.
    const bool gatesWrites = selection.mask != nullptr || selection.validity.rule() != ValidityRule::AllValid;
    const auto seed = [&](PlaneView<const float> from, PlaneView<float> to) -> Status {
        return gatesWrites ? spatial_detail::copyPlane(execution, from, to, kMorphologyOperation) : Status::success();
    };
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        request.output.writeBack, geometry, kMorphologyOperation, result,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            PlaneView<const float> current = input;
            for (std::uint32_t iteration = 0u; iteration < request.iterations; ++iteration)
            {
                const bool last = iteration + 1u == request.iterations;
                const PlaneView<float> target = last ? output : work[iteration % 2u].view();
                const bool countThisPass = last;
                Status status = Status::success();
                switch (request.operation)
                {
                case MorphologyOperation::Erode:
                case MorphologyOperation::Dilate:
                case MorphologyOperation::HitMiss:
                    if (!last)
                    {
                        status = seed(current, target);
                    }
                    if (status.failed())
                    {
                        break;
                    }
                    status = morphologyPrimitive(
                        execution, current, target, validity, request, request.operation == MorphologyOperation::Dilate,
                        request.operation == MorphologyOperation::HitMiss, countThisPass ? &writes : nullptr);
                    break;
                case MorphologyOperation::Open:
                case MorphologyOperation::Close: {
                    const bool firstDilates = request.operation == MorphologyOperation::Close;
                    status = seed(current, work[2].view());
                    if (!status.failed())
                    {
                        status = morphologyPrimitive(execution, current, work[2].view(), validity, request,
                                                     firstDilates, false, nullptr);
                    }
                    if (!status.failed() && !last)
                    {
                        status = seed(work[2].constView(), target);
                    }
                    if (!status.failed())
                    {
                        status = morphologyPrimitive(execution, work[2].constView(), target, validity, request,
                                                     !firstDilates, false, countThisPass ? &writes : nullptr);
                    }
                    break;
                }
                case MorphologyOperation::Gradient:
                    status = seed(current, work[2].view());
                    if (!status.failed())
                    {
                        status = seed(current, work[3].view());
                    }
                    if (!status.failed())
                    {
                        status = morphologyPrimitive(execution, current, work[2].view(), validity, request, false,
                                                     false, nullptr);
                    }
                    if (!status.failed())
                    {
                        status = morphologyPrimitive(execution, current, work[3].view(), validity, request, true, false,
                                                     nullptr);
                    }
                    if (!status.failed() && !last)
                    {
                        status = seed(current, target);
                    }
                    if (!status.failed())
                    {
                        status = combineMorphology(execution, work[2].constView(), work[3].constView(), current, target,
                                                   validity, true, countThisPass ? &writes : nullptr);
                    }
                    break;
                case MorphologyOperation::TopHat:
                case MorphologyOperation::BlackHat: {
                    const bool closing = request.operation == MorphologyOperation::BlackHat;
                    status = seed(current, work[2].view());
                    if (!status.failed())
                    {
                        status = morphologyPrimitive(execution, current, work[2].view(), validity, request, closing,
                                                     false, nullptr);
                    }
                    if (!status.failed())
                    {
                        status = seed(work[2].constView(), work[3].view());
                    }
                    if (!status.failed())
                    {
                        status = morphologyPrimitive(execution, work[2].constView(), work[3].view(), validity, request,
                                                     !closing, false, nullptr);
                    }
                    if (!status.failed() && !last)
                    {
                        status = seed(current, target);
                    }
                    if (!status.failed())
                    {
                        status = combineMorphology(execution, current, work[3].constView(), current, target, validity,
                                                   closing, countThisPass ? &writes : nullptr);
                    }
                    break;
                }
                }
                if (status.failed())
                {
                    return status;
                }
                current = target;
            }
            return Status::success();
        });
    if (filtered.failed())
    {
        return filtered;
    }
    result.run.outputsWritten = writes.load(std::memory_order_relaxed);
    result.run.selection.valid = result.run.outputsWritten;
    result.run.selection.total = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    result.horizontalRadius = request.radiusX * request.iterations;
    result.verticalRadius = request.radiusY * request.iterations;
    result.imagePasses = plan.imagePasses();
    result.plannedWorkers = plannedWorkerCount(
        execution, geometry.height, rowCost(geometry.width, (2u * request.radiusX + 1u) * (2u * request.radiusY + 1u)));
    result.precision = ComputePrecision::Float32;
    return result;
}

AdaptiveHistogramPlan::AdaptiveHistogramPlan(const AdaptiveHistogramRequest& request) noexcept : m_request(request) {}

const AdaptiveHistogramRequest& AdaptiveHistogramPlan::request() const noexcept
{
    return m_request;
}

Result<AdaptiveHistogramPlan> planAdaptiveHistogramEqualization(const AdaptiveHistogramRequest& request)
{
    if (request.tileColumns == 0u || request.tileRows == 0u)
    {
        return spatialFailure(ErrorCode::OutOfRange, kClaheOperation, "a CLAHE tile grid is non-empty");
    }
    if (request.binCount < 2u || request.binCount > 65536u)
    {
        return spatialLimitFailure(ErrorCode::OutOfRange, kClaheOperation,
                                   "a CLAHE histogram has between two and 65536 bins", request.binCount, 65536u);
    }
    if (!std::isfinite(request.clipLimit) || request.clipLimit <= 0.0 || !std::isfinite(request.lowest) ||
        !std::isfinite(request.highest) || request.lowest >= request.highest)
    {
        return spatialFailure(ErrorCode::InvalidArgument, kClaheOperation,
                              "CLAHE needs a positive finite clip limit and a finite increasing value range");
    }
    const Status output = checkOutputPolicy(request.output, kClaheOperation);
    if (output.failed())
    {
        return output;
    }
    return AdaptiveHistogramPlan{request};
}

Result<AdaptiveHistogramResult> equalizeImageAdaptiveHistogram(const ExecutionContext& execution, SampleType sampleType,
                                                               const void* source, const ImageLayout& sourceLayout,
                                                               void* destination, const ImageLayout& destinationLayout,
                                                               const AdaptiveHistogramPlan& plan,
                                                               const SpatialSelection& selection)
{
    const Result<AdaptiveHistogramPlan> checked = planAdaptiveHistogramEqualization(plan.request());
    if (!checked.hasValue())
    {
        return checked.status();
    }
    SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kClaheOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    const AdaptiveHistogramRequest& request = plan.request();
    if (request.tileColumns > geometry.width || request.tileRows > geometry.height)
    {
        return spatialFailure(ErrorCode::OutOfRange, kClaheOperation,
                              "a CLAHE grid cannot have more tiles than the image has pixels on an axis");
    }
    const std::uint64_t tileCount = static_cast<std::uint64_t>(request.tileColumns) * request.tileRows;
    if (tileCount > (std::numeric_limits<std::uint64_t>::max)() / request.binCount)
    {
        return spatialFailure(ErrorCode::LimitExceeded, kClaheOperation, "the CLAHE table size overflows");
    }
    const std::uint64_t entryCount = tileCount * request.binCount;
    if (entryCount > (std::numeric_limits<std::uint64_t>::max)() / (sizeof(std::uint32_t) + sizeof(float)))
    {
        return spatialFailure(ErrorCode::LimitExceeded, kClaheOperation, "the CLAHE table size overflows");
    }
    const std::uint64_t histogramBytes = entryCount * sizeof(std::uint32_t);
    const std::uint64_t tableBytes = entryCount * sizeof(float);
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, histogramBytes + tableBytes, kDefaultBufferAlignmentBytes, kClaheOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    auto* const histograms = static_cast<std::uint32_t*>(memory->data());
    auto* const mappings = reinterpret_cast<float*>(static_cast<std::uint8_t*>(memory->data()) + histogramBytes);

    AdaptiveHistogramResult answer{};
    answer.tileCount = tileCount;
    answer.binCount = request.binCount;
    answer.filter.workingBytes += memory->bytes();
    std::atomic<std::uint64_t> totalValid{0u};
    const Status filtered = filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        request.output.writeBack, geometry, kClaheOperation, answer.filter,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            std::fill_n(histograms, static_cast<std::size_t>(entryCount), 0u);
            // An empty tile has no distribution to impose on its neighbours. Its sentinel
            // resolves to the sample itself during interpolation, without bin quantization.
            std::fill_n(mappings, static_cast<std::size_t>(entryCount), std::numeric_limits<float>::quiet_NaN());
            Status status = parallelFor(
                execution, tileCount,
                TaskCost{.bytesPerItem = geometry.planeBytes / tileCount,
                         .operationsPerItem = geometry.planeBytes / tileCount},
                [&](TaskRange range) {
                    for (std::uint64_t tile = range.begin; tile < range.end; ++tile)
                    {
                        const std::uint32_t tx = static_cast<std::uint32_t>(tile % request.tileColumns);
                        const std::uint32_t ty = static_cast<std::uint32_t>(tile / request.tileColumns);
                        const std::uint32_t x0 = tx * geometry.width / request.tileColumns;
                        const std::uint32_t x1 = (tx + 1u) * geometry.width / request.tileColumns;
                        const std::uint32_t y0 = ty * geometry.height / request.tileRows;
                        const std::uint32_t y1 = (ty + 1u) * geometry.height / request.tileRows;
                        std::uint32_t* const histogram = histograms + tile * request.binCount;
                        std::uint64_t validCount = 0u;
                        for (std::uint32_t y = y0; y < y1; ++y)
                        {
                            const std::span<const float> row = *input.row(y);
                            for (std::uint32_t x = x0; x < x1; ++x)
                            {
                                if (!centreIsValid(input, validity, x, y))
                                {
                                    continue;
                                }
                                ++histogram[histogramBin(row[x], request)];
                                ++validCount;
                            }
                        }
                        if (validCount == 0u)
                        {
                            continue;
                        }
                        const std::uint64_t clip =
                            (std::max)(std::uint64_t{1}, static_cast<std::uint64_t>(std::floor(
                                                             request.clipLimit * static_cast<double>(validCount) /
                                                             static_cast<double>(request.binCount))));
                        std::uint64_t excess = 0u;
                        for (std::uint32_t bin = 0u; bin < request.binCount; ++bin)
                        {
                            if (histogram[bin] > clip)
                            {
                                excess += histogram[bin] - clip;
                                histogram[bin] = static_cast<std::uint32_t>(clip);
                            }
                        }
                        // The clipped counts go back evenly: every bin receives the same share, and
                        // the remainder is spread across the whole range in equal steps rather than
                        // piled onto the lowest bins, which would brighten every tile whose excess
                        // is large against its counts. The step rule is the classic one, which
                        // OpenCV's equalization also applies.
                        const std::uint32_t quotient = static_cast<std::uint32_t>(excess / request.binCount);
                        std::uint32_t remainder = static_cast<std::uint32_t>(excess % request.binCount);
                        for (std::uint32_t bin = 0u; bin < request.binCount; ++bin)
                        {
                            histogram[bin] += quotient;
                        }
                        const std::uint32_t remainderStep =
                            remainder == 0u ? request.binCount : (std::max)(request.binCount / remainder, 1u);
                        for (std::uint32_t bin = 0u; bin < request.binCount && remainder > 0u;
                             bin += remainderStep, --remainder)
                        {
                            ++histogram[bin];
                        }
                        std::uint64_t cumulative = 0u;
                        float* const mapping = mappings + tile * request.binCount;
                        for (std::uint32_t bin = 0u; bin < request.binCount; ++bin)
                        {
                            cumulative += histogram[bin];
                            mapping[bin] = static_cast<float>(request.lowest + (request.highest - request.lowest) *
                                                                                   static_cast<double>(cumulative) /
                                                                                   static_cast<double>(validCount));
                        }
                    }
                    return Status::success();
                });
            if (status.failed())
            {
                return status;
            }
            return parallelFor(execution, geometry.height, rowCost(geometry.width, 16u), [&](TaskRange range) {
                std::uint64_t localValid = 0u;
                for (std::uint64_t rowIndex = range.begin; rowIndex < range.end; ++rowIndex)
                {
                    const std::uint32_t y = static_cast<std::uint32_t>(rowIndex);
                    const std::span<const float> sourceRow = *input.row(y);
                    std::span<float> destinationRow = *output.row(y);
                    const double gy = (static_cast<double>(y) + 0.5) * request.tileRows / geometry.height - 0.5;
                    // The two tiles a pixel lies between are the ones either side of its position in
                    // tile units, each clamped to the grid on its own: outside the first or last
                    // tile centre both are the border tile, so the border half-tile holds that
                    // tile's mapping rather than leaning toward its neighbour.
                    const std::int64_t rawY0 = static_cast<std::int64_t>(std::floor(gy));
                    const std::int64_t lastRow = static_cast<std::int64_t>(request.tileRows) - std::int64_t{1};
                    const std::uint32_t y0 = static_cast<std::uint32_t>((std::clamp)(rawY0, std::int64_t{0}, lastRow));
                    const std::uint32_t y1 =
                        static_cast<std::uint32_t>((std::clamp)(rawY0 + std::int64_t{1}, std::int64_t{0}, lastRow));
                    const double fy = gy - static_cast<double>(rawY0);
                    for (std::uint32_t x = 0u; x < geometry.width; ++x)
                    {
                        if (!centreIsValid(input, validity, x, y))
                        {
                            continue;
                        }
                        const double gx = (static_cast<double>(x) + 0.5) * request.tileColumns / geometry.width - 0.5;
                        const std::int64_t rawX0 = static_cast<std::int64_t>(std::floor(gx));
                        const std::int64_t lastColumn =
                            static_cast<std::int64_t>(request.tileColumns) - std::int64_t{1};
                        const std::uint32_t x0 =
                            static_cast<std::uint32_t>((std::clamp)(rawX0, std::int64_t{0}, lastColumn));
                        const std::uint32_t x1 = static_cast<std::uint32_t>(
                            (std::clamp)(rawX0 + std::int64_t{1}, std::int64_t{0}, lastColumn));
                        const double fx = gx - static_cast<double>(rawX0);
                        const std::uint32_t bin = histogramBin(sourceRow[x], request);
                        const auto at = [&](std::uint32_t tx, std::uint32_t ty) {
                            const float mapped = mappings[(static_cast<std::uint64_t>(ty) * request.tileColumns + tx) *
                                                              request.binCount +
                                                          bin];
                            return std::isnan(mapped) ? (std::clamp)(static_cast<double>(sourceRow[x]), request.lowest,
                                                                     request.highest)
                                                      : static_cast<double>(mapped);
                        };
                        const double top = at(x0, y0) + fx * (at(x1, y0) - at(x0, y0));
                        const double bottom = at(x0, y1) + fx * (at(x1, y1) - at(x0, y1));
                        destinationRow[x] = static_cast<float>(
                            (std::clamp)(top + fy * (bottom - top), request.lowest, request.highest));
                        ++localValid;
                    }
                }
                totalValid.fetch_add(localValid, std::memory_order_relaxed);
                return Status::success();
            });
        });
    if (filtered.failed())
    {
        return filtered;
    }
    answer.filter.run.selection.total =
        static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    answer.filter.run.selection.valid = totalValid.load(std::memory_order_relaxed);
    answer.filter.run.outputsWritten = answer.filter.run.selection.valid;
    answer.filter.imagePasses = 2u;
    answer.filter.plannedWorkers = plannedWorkerCount(execution, geometry.height, rowCost(geometry.width, 16u));
    answer.filter.precision = ComputePrecision::Float32;
    return answer;
}

} // namespace an
