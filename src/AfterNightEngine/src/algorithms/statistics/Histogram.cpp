// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - public histogram, cumulative counts and percentiles over an image selection.
//
// The counting is `accumulatePlaneHistogramOver` behind the statistics surface's shared selection
// translation; this file adds the empty-selection report and the two serial reads of the caller's
// counters, which run on the calling thread and take no execution because they hold no memory and
// read no image.

#include <AfterNightEngine/Algorithms/Statistics/Histogram.h>

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/statistics/StatisticsSelectionInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::computeImageHistogram";
constexpr const char* kCumulativeOperation = "an::accumulateCumulativeCounts";
constexpr const char* kQuantileOperation = "an::histogramQuantileValue";

[[nodiscard]] Status histogramFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

} // namespace

Result<ImageHistogram> computeImageHistogram(const ExecutionContext& execution, SampleType sampleType,
                                             const void* samples, const ImageLayout& layout, std::uint32_t channelIndex,
                                             const StatisticsSelection& selection, const ImageHistogramRequest& request,
                                             std::span<std::uint64_t> bins)
{
    if (!execution.valid())
    {
        return histogramFailure(ErrorCode::NotInitialized, kOperation,
                                "the histogram was given an empty execution context");
    }
    const Status cancellation = execution.checkCancellation();
    if (cancellation.failed())
    {
        return cancellation;
    }
    const Result<statistics_detail::ResolvedSelection> resolved =
        statistics_detail::resolveStatisticsSelection(sampleType, samples, layout, channelIndex, selection, kOperation);
    if (!resolved.hasValue())
    {
        return resolved.status();
    }
    if (resolved->region.width == 0u || resolved->region.height == 0u)
    {
        return requireSelection(ValidityCount{.valid = 0u, .total = 0u}, kOperation);
    }

    const PlaneHistogramRequest planeRequest{
        .domain = request.domain, .lowest = request.lowest, .highest = request.highest};
    const Result<PlaneHistogramResult> counted = accumulatePlaneHistogramOver(
        execution, resolved->source, bins, planeRequest, resolved->validity, resolved->region);
    if (!counted.hasValue())
    {
        return counted.status();
    }
    if (counted->run.selection.valid == 0u)
    {
        // The bins are already cleared, which is the truthful histogram of nothing, and the caller
        // is told so the same way every operation on this surface tells it.
        return requireSelection(counted->run.selection, kOperation);
    }

    ImageHistogram histogram;
    histogram.selection = counted->run.selection;
    histogram.counted = counted->run.outputsWritten;
    histogram.belowRange = counted->belowRange;
    histogram.aboveRange = counted->aboveRange;
    histogram.notANumber = counted->notANumber;
    histogram.lowest = counted->lowest;
    histogram.highest = counted->highest;
    histogram.binCount = static_cast<std::uint64_t>(bins.size());
    histogram.deterministicChunks = counted->deterministicChunks;
    histogram.plannedWorkers = counted->plannedWorkers;
    histogram.partialBytes = counted->partialBytes;
    return histogram;
}

Status accumulateCumulativeCounts(std::span<const std::uint64_t> bins, std::span<std::uint64_t> cumulative)
{
    if (bins.empty())
    {
        return histogramFailure(ErrorCode::InvalidArgument, kCumulativeOperation,
                                "a cumulative histogram needs at least one bin");
    }
    if (bins.size() != cumulative.size())
    {
        return histogramFailure(ErrorCode::IncompatibleArguments, kCumulativeOperation,
                                "the cumulative span is not as long as its bin span");
    }
    // Accumulating in place is the ordinary use and is exact, because entry i is written after
    // entry i has been read. Any other overlap would read a count this has already replaced.
    if (bins.data() != cumulative.data() &&
        statistics_detail::rangesOverlap(bins.data(), static_cast<std::uint64_t>(bins.size_bytes()), cumulative.data(),
                                         static_cast<std::uint64_t>(cumulative.size_bytes())))
    {
        return histogramFailure(ErrorCode::InvalidArgument, kCumulativeOperation,
                                "the cumulative span partially overlaps its bin span");
    }

    std::uint64_t running = 0u;
    for (std::size_t index = 0u; index < bins.size(); ++index)
    {
        const std::uint64_t count = bins[index];
        if (count > (std::numeric_limits<std::uint64_t>::max)() - running)
        {
            return histogramFailure(ErrorCode::InvalidData, kCumulativeOperation,
                                    "the cumulative total does not fit an unsigned sixty-four-bit count");
        }
        running += count;
        cumulative[index] = running;
    }
    return Status::success();
}

Result<double> histogramQuantileValue(const ImageHistogram& histogram, std::span<const std::uint64_t> cumulative,
                                      double quantile, HistogramQuantileRule rule)
{
    if (histogram.binCount == 0u || static_cast<std::uint64_t>(cumulative.size()) != histogram.binCount)
    {
        return histogramFailure(ErrorCode::IncompatibleArguments, kQuantileOperation,
                                "the cumulative span is not as long as the histogram has bins");
    }
    if (!isHistogramQuantileRule(rule))
    {
        return histogramFailure(ErrorCode::InvalidArgument, kQuantileOperation,
                                "the quantile request names no known histogram rule");
    }
    if (!std::isfinite(histogram.lowest) || !std::isfinite(histogram.highest) ||
        !(histogram.highest > histogram.lowest))
    {
        return histogramFailure(ErrorCode::InvalidArgument, kQuantileOperation,
                                "the histogram interval must be finite and increasing");
    }
    if (!(quantile >= 0.0) || !(quantile <= 1.0))
    {
        return histogramFailure(ErrorCode::OutOfRange, kQuantileOperation,
                                "the requested quantile is outside the closed unit interval");
    }
    const std::uint64_t total = cumulative[static_cast<std::size_t>(histogram.binCount - 1u)];
    if (total == 0u)
    {
        return histogramFailure(ErrorCode::EmptySelection, kQuantileOperation,
                                "no sample entered a bin, so the distribution has no quantile");
    }

    const double span = histogram.highest - histogram.lowest;
    const double width = span / static_cast<double>(histogram.binCount);
    // The two ends are the interval itself under every rule: a convention about where inside a bin
    // a sample sits cannot move the endpoint the histogram was told to count up to.
    if (quantile == 0.0)
    {
        return histogram.lowest;
    }
    if (quantile == 1.0)
    {
        return histogram.highest;
    }

    const double rank = quantile * static_cast<double>(total);
    std::uint64_t bin = histogram.binCount - 1u;
    for (std::uint64_t index = 0u; index < histogram.binCount; ++index)
    {
        if (static_cast<double>(cumulative[static_cast<std::size_t>(index)]) >= rank)
        {
            bin = index;
            break;
        }
    }
    const double lowerEdge = histogram.lowest + static_cast<double>(bin) * width;
    if (rule == HistogramQuantileRule::LowerEdge)
    {
        return (std::min)(lowerEdge, histogram.highest);
    }
    if (rule == HistogramQuantileRule::BinCenter)
    {
        return (std::min)(lowerEdge + 0.5 * width, histogram.highest);
    }
    const std::uint64_t below = bin == 0u ? 0u : cumulative[static_cast<std::size_t>(bin - 1u)];
    const std::uint64_t held = cumulative[static_cast<std::size_t>(bin)] - below;
    if (held == 0u)
    {
        return (std::min)(lowerEdge, histogram.highest);
    }
    const double within = (rank - static_cast<double>(below)) / static_cast<double>(held);
    return (std::min)(lowerEdge + within * width, histogram.highest);
}

} // namespace an
