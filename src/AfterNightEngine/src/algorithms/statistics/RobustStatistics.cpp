// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - public robust and clipped estimators over an image selection.
//
// Every estimator here is a call on the order-statistic substrate. The request and the selection are
// checked first; a compaction asked for by name then counts the admitted samples, refuses them above
// the stated ceiling, and gathers them into one Float64 plane whose padded final row holds quiet NaNs
// under `ValidityRule::FiniteOnly`. One ranking produces the extremes, the quantiles, the median and,
// when asked, the median absolute deviation. Clipping then re-ranks through a value window rather
// than a copy: each iteration's interval is the current median plus or minus the stated widths of
// the current scale, and the iteration stops when the retained count repeats or the ceiling is
// reached. The clipped mean and standard deviation are one deterministic reduction over the final
// interval, which is why they are accumulations where every other number is a rank. A hold is carried
// to the substrate, which reads each admitted sample as held; a compacted selection is held once after
// it is gathered instead, so the ranking over it reads the plane as it lies.

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/statistics/StatisticsSelectionInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::computeImageRobustStatistics";

/// How wide a compacted selection is laid out before it wraps onto a further row.
///
/// A compacted selection is a plane like any other, so it needs a width and a height rather than a
/// length. Sixty-five thousand five hundred and thirty-six keeps a small selection to one row, is
/// far below any configured maximum width, and leaves a large one with enough rows for the
/// partition to divide.
constexpr std::uint32_t kCompactRowSamples = 65536u;

[[nodiscard]] Status robustFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

/// Whether the request names rules this build implements and spans that agree.
///
/// @param request the estimators, rules and strategy asked for.
/// @param quantileValues where the requested quantiles are to be written.
/// @return success, or the first reason the request cannot be served.
[[nodiscard]] Status validateRequest(const RobustStatisticsRequest& request, std::span<double> quantileValues)
{
    if (request.quantiles.size() != quantileValues.size())
    {
        return robustFailure(ErrorCode::IncompatibleArguments,
                             "the robust output span is not as long as its quantile span");
    }
    if (!isQuantileInterpolation(request.interpolation))
    {
        return robustFailure(ErrorCode::InvalidArgument, "the robust request names no known interpolation rule");
    }
    if (!isRoiSelectionStrategy(request.roiStrategy))
    {
        return robustFailure(ErrorCode::InvalidArgument, "the robust request names no known selection strategy");
    }
    if (request.sigmaClipping.has_value())
    {
        const SigmaClippingRequest& clipping = *request.sigmaClipping;
        if (!std::isfinite(clipping.lowSigma) || !std::isfinite(clipping.highSigma) || clipping.lowSigma < 0.0 ||
            clipping.highSigma < 0.0)
        {
            return robustFailure(ErrorCode::InvalidArgument,
                                 "a clipping width is not a finite non-negative number of scales");
        }
        if (clipping.maximumIterations == 0u || clipping.maximumIterations > kMaxSigmaClippingIterations)
        {
            return robustFailure(ErrorCode::OutOfRange,
                                 "the clipping iteration count is zero or above the stated ceiling");
        }
    }
    if (request.hold.has_value())
    {
        if (!std::isfinite(request.hold->lowest) || !std::isfinite(request.hold->highest) ||
            request.hold->lowest > request.hold->highest)
        {
            return robustFailure(ErrorCode::InvalidArgument, "the hold is not a finite ordered closed interval");
        }
        if (request.sigmaClipping.has_value())
        {
            return robustFailure(ErrorCode::IncompatibleArguments,
                                 "a clipping interval is read on the stored values, so it cannot clip a held "
                                 "selection; hold the samples first or clip them unheld");
        }
    }
    return Status::success();
}

/// The plane a compacted selection is read as, and the memory holding it.
struct CompactedSelection
{
    /// The gathered samples, released when this leaves scope.
    plane_detail::ScopedContextMemory memory;

    /// Their geometry: one Float64 plane wide enough to hold them with a padded final row.
    ImageLayout layout;

    /// The rule that excludes that padding, which is the only non-finite thing in the plane.
    ValidityDescriptor validity;

    /// How many samples were gathered, which is the selection this plane stands for.
    std::uint64_t count = 0u;
};

/// Gathers every admitted sample of a selection into one contiguous Float64 plane.
///
/// The final row is padded to the plane width with quiet NaNs and the plane carries
/// `ValidityRule::FiniteOnly`, so the padding is excluded by the same rule every later pass already
/// evaluates rather than by a second geometry nothing else knows about.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the selected channel of the caller's image.
/// @param validity its value rule and optional packed mask.
/// @param region the rectangle to gather.
/// @param count how many samples the counting pass admitted.
/// @return the compacted plane, or an allocation, geometry, or gather failure.
[[nodiscard]] Result<CompactedSelection> compactSelection(const ExecutionContext& execution,
                                                          const KernelPlaneSource& source,
                                                          const KernelValidityView& validity,
                                                          const KernelPlaneSelection& region, std::uint64_t count)
{
    const std::uint32_t width = static_cast<std::uint32_t>((
        std::min)(count, static_cast<std::uint64_t>((std::min)(kCompactRowSamples, execution.limits().maxImageWidth))));
    const std::uint64_t height = (count + width - 1u) / width;
    const std::optional<ImageExtent> extent =
        ImageExtent::create(width, static_cast<std::uint32_t>(height), 1u, execution.limits());
    if (!extent.has_value() || height > execution.limits().maxImageHeight)
    {
        return robustFailure(ErrorCode::InvalidArgument,
                             "the compacted selection cannot be represented within the engine context limits");
    }
    const std::optional<ImageLayout> layout = ImageLayout::create(ImageLayoutRequest{.extent = *extent,
                                                                                     .elementSizeBytes = sizeof(double),
                                                                                     .samplesPerPixel = 1u,
                                                                                     .planeCount = 1u,
                                                                                     .rowAlignmentBytes = 1u},
                                                                  execution.limits());
    const std::optional<ValidityDescriptor> finiteOnly =
        ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::FiniteOnly});
    if (!layout.has_value() || !finiteOnly.has_value())
    {
        return robustFailure(ErrorCode::InvalidArgument, "the compacted selection has no valid plane description");
    }

    Result<plane_detail::ScopedContextMemory> memory =
        plane_detail::ScopedContextMemory::take(execution, layout->totalBytes(), alignof(double), kOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    double* const gathered = static_cast<double*>(memory->data());
    const Result<PlaneCompactionResult> compacted =
        compactSelectedPlaneSamples(execution, source, validity, region, std::span<double>{gathered, count});
    if (!compacted.hasValue())
    {
        return compacted.status();
    }
    const std::uint64_t padded = static_cast<std::uint64_t>(width) * height;
    std::fill_n(gathered + count, static_cast<std::size_t>(padded - count), std::numeric_limits<double>::quiet_NaN());
    return CompactedSelection{.memory = std::move(*memory), .layout = *layout, .validity = *finiteOnly, .count = count};
}

/// The samples a robust estimator actually reads, however the caller asked for them to be read.
struct ReadableSelection
{
    /// The plane every pass is run against.
    KernelPlaneSource source{};

    /// Its value rule and optional packed mask.
    KernelValidityView validity{};

    /// The rectangle within it, which is the whole of a compacted plane.
    KernelPlaneSelection region{};
};

} // namespace

Result<RobustStatistics> computeImageRobustStatistics(const ExecutionContext& execution, SampleType sampleType,
                                                      const void* samples, const ImageLayout& layout,
                                                      std::uint32_t channelIndex, const StatisticsSelection& selection,
                                                      const RobustStatisticsRequest& request,
                                                      std::span<double> quantileValues)
{
    if (!execution.valid())
    {
        return robustFailure(ErrorCode::NotInitialized, "robust statistics were given an empty execution context");
    }
    const Status cancellation = execution.checkCancellation();
    if (cancellation.failed())
    {
        return cancellation;
    }
    const Status requested = validateRequest(request, quantileValues);
    if (requested.failed())
    {
        return requested;
    }
    const Result<statistics_detail::ResolvedSelection> resolved =
        statistics_detail::resolveStatisticsSelection(sampleType, samples, layout, channelIndex, selection, kOperation);
    if (!resolved.hasValue())
    {
        return resolved.status();
    }
    if (statistics_detail::rangesOverlap(samples, layout.totalBytes(), quantileValues.data(),
                                         static_cast<std::uint64_t>(quantileValues.size_bytes())))
    {
        return robustFailure(ErrorCode::InvalidArgument, "the robust output overlaps the source samples");
    }
    if (resolved->region.width == 0u || resolved->region.height == 0u)
    {
        return requireSelection(ValidityCount{.valid = 0u, .total = 0u}, kOperation);
    }

    const bool wantsDeviation = request.medianAbsoluteDeviation || request.sigmaClipping.has_value();
    const std::uint64_t regionPixels = static_cast<std::uint64_t>(resolved->region.width) * resolved->region.height;
    // The automatic choice is to read the samples where they lie. Compaction was measured against
    // it on the two shapes that differ in the thing it is supposed to pay for - a masked region read
    // five times, and a whole frame read twenty-seven times by a clipping iteration - and it was
    // slower on both, because a rectangle of a planar frame is already close to a sequential read
    // and gathering it is one further pass over all of it. It stays available, and it is asked for
    // by name rather than arrived at.
    const bool compacts = request.roiStrategy == RoiSelectionStrategy::CompactSelection;

    RobustStatistics statistics;
    statistics.roiStrategy = compacts ? RoiSelectionStrategy::CompactSelection : RoiSelectionStrategy::StridedSource;

    ReadableSelection readable{.source = resolved->source, .validity = resolved->validity, .region = resolved->region};
    std::optional<CompactedSelection> compacted;
    std::uint64_t admitted = 0u;
    if (compacts)
    {
        const Result<PlaneCompactionResult> counted = compactSelectedPlaneSamples(
            execution, resolved->source, resolved->validity, resolved->region, std::span<double>{});
        if (!counted.hasValue())
        {
            return counted.status();
        }
        admitted = counted->run.selection.valid;
        if (admitted > kMaxCompactSelectionBytes / sizeof(double))
        {
            return robustFailure(ErrorCode::OutOfRange,
                                 "the selection to compact is larger than the stated compaction ceiling");
        }
        Result<CompactedSelection> gathered =
            compactSelection(execution, resolved->source, resolved->validity, resolved->region, admitted);
        if (!gathered.hasValue())
        {
            return gathered.status();
        }
        compacted.emplace(std::move(*gathered));
        if (request.hold.has_value())
        {
            // The compaction gathers the stored values, so the gathered plane is held here, once, and the ranking
            // over it reads it as it lies: the same held values the strided read orders, bit for bit.
            double* const values = static_cast<double*>(compacted->memory.data());
            const double lowest = request.hold->lowest;
            const double highest = request.hold->highest;
            for (std::uint64_t index = 0u; index < admitted; ++index)
            {
                values[index] = (std::min)((std::max)(values[index], lowest), highest);
            }
        }
        readable.source = KernelPlaneSource{.sampleType = SampleType::Float64,
                                            .samples = compacted->memory.data(),
                                            .layout = &compacted->layout,
                                            .plane = 0u,
                                            .sampleInPixel = 0u};
        readable.validity = KernelValidityView{.descriptor = compacted->validity, .mask = nullptr, .maskPlane = 0u};
        readable.region = KernelPlaneSelection{.originX = 0u,
                                               .originY = 0u,
                                               .width = compacted->layout.extent().width(),
                                               .height = compacted->layout.extent().height()};
        statistics.planeScans += 2u;
        statistics.peakScratchBytes += compacted->memory.bytes();
    }

    const PlaneOrderRequest orderRequest{.interpolation = request.interpolation,
                                         .medianAbsoluteDeviation = wantsDeviation,
                                         .hold = compacts ? std::nullopt : request.hold};
    const Result<PlaneOrderStatistics> ranked =
        selectPlaneOrderStatisticsOver(execution, readable.source, request.quantiles, quantileValues, orderRequest,
                                       readable.validity, readable.region);
    if (!ranked.hasValue())
    {
        return ranked.status();
    }
    std::uint64_t peakOrderScratch = ranked->peakScratchBytes;
    statistics.planeScans += ranked->planeScans;
    statistics.selection = compacts ? ValidityCount{.valid = admitted, .total = regionPixels} : ranked->run.selection;
    statistics.minimum = ranked->minimum;
    statistics.maximum = ranked->maximum;
    statistics.median = ranked->median;
    statistics.medianAbsoluteDeviation =
        wantsDeviation ? ranked->medianAbsoluteDeviation : std::numeric_limits<double>::quiet_NaN();
    statistics.robustScale = wantsDeviation ? statistics.medianAbsoluteDeviation * kMedianAbsoluteDeviationToSigma
                                            : std::numeric_limits<double>::quiet_NaN();

    if (request.sigmaClipping.has_value())
    {
        const SigmaClippingRequest& clipping = *request.sigmaClipping;
        SigmaClippedStatistics clipped;
        double location = statistics.median;
        double deviation = statistics.medianAbsoluteDeviation;
        double scale = statistics.robustScale;
        std::uint64_t retained = statistics.selection.valid;
        double lowest = statistics.minimum;
        double highest = statistics.maximum;
        for (std::uint32_t iteration = 0u; iteration < clipping.maximumIterations; ++iteration)
        {
            // The interval is clamped to the extremes the counting pass already found. No sample
            // lies outside them, so clamping removes nothing, and it is what keeps the endpoints
            // finite when a width and a scale multiply to an infinity.
            lowest = (std::max)(statistics.minimum, location - clipping.lowSigma * scale);
            highest = (std::min)(statistics.maximum, location + clipping.highSigma * scale);
            KernelPlaneSelection clippedRegion = readable.region;
            clippedRegion.valueWindow = KernelValueWindow{.lowest = lowest, .highest = highest};
            const Result<PlaneOrderStatistics> narrowed = selectPlaneOrderStatisticsOver(
                execution, readable.source, std::span<const double>{}, std::span<double>{},
                PlaneOrderRequest{.interpolation = request.interpolation, .medianAbsoluteDeviation = true},
                readable.validity, clippedRegion);
            if (!narrowed.hasValue())
            {
                if (narrowed.status().code() == ErrorCode::EmptySelection)
                {
                    return robustFailure(ErrorCode::EmptySelection,
                                         "the clipping interval retained no samples; widen it or ask for fewer scales");
                }
                return narrowed.status();
            }
            peakOrderScratch = (std::max)(peakOrderScratch, narrowed->peakScratchBytes);
            statistics.planeScans += narrowed->planeScans;
            ++clipped.iterations;
            const std::uint64_t nowRetained = narrowed->run.selection.valid;
            location = narrowed->median;
            deviation = narrowed->medianAbsoluteDeviation;
            scale = deviation * kMedianAbsoluteDeviationToSigma;
            const bool stable = nowRetained == retained;
            retained = nowRetained;
            if (stable)
            {
                clipped.converged = true;
                break;
            }
        }

        KernelPlaneSelection clippedRegion = readable.region;
        clippedRegion.valueWindow = KernelValueWindow{.lowest = lowest, .highest = highest};
        const Result<ReductionStatistics> moments =
            reducePlaneStatisticsOver(execution, readable.source, readable.validity, clippedRegion);
        if (!moments.hasValue())
        {
            return moments.status();
        }
        statistics.planeScans += 2u;
        clipped.selection = ValidityCount{.valid = retained, .total = statistics.selection.valid};
        clipped.lowest = lowest;
        clipped.highest = highest;
        clipped.location = location;
        clipped.scale = scale;
        clipped.medianAbsoluteDeviation = deviation;
        clipped.mean = moments->mean;
        clipped.standardDeviation = std::sqrt(moments->populationVariance);
        statistics.clipped = clipped;
    }

    statistics.peakScratchBytes += peakOrderScratch;
    return statistics;
}

} // namespace an
