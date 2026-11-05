// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack quality diagnostics over weights, rejection counts and maps.
//
// The weight summary normalizes by the largest weight before its moments, the frame-outlier rule
// uses the floors and multipliers below, and the cluster analysis is a connected-component search.
// The working memory is one engine-context block per call, and every order is fixed: medians are
// exact selections, and clusters are ranked by a total order so a truncated report keeps the same
// clusters on every run. The flood fill uses an explicit stack rather than recursion, and marks a
// pixel when it is pushed, so the stack never holds more than one entry per pixel of the plane. The
// map scan counts rows on the operation's threads into one slot per worker; its counts are integers,
// so their sum does not depend on how many there were.

#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/stacking/StackHistory.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace an
{
namespace
{

using stacking::failure;
using stacking::refuse;

constexpr const char* kSummarizeRejection = "an::summarizeFrameRejection";
constexpr const char* kDetectOutliers = "an::detectRejectionFrameOutliers";
constexpr const char* kAnalyzeClusters = "an::analyzeRejectionClusters";
constexpr const char* kCountMaps = "an::countRejectionMaps";

/// The floors and multipliers of the frame-outlier rule.
constexpr double kOutlierSigmaFloor = 0.0025;
constexpr double kOutlierRateFloor = 0.015;
constexpr double kOutlierRobustSigmas = 4.0;
constexpr double kOutlierMedianMultiple = 2.5;
constexpr double kOutlierBaselineSigmas = 2.5;
constexpr std::uint32_t kOutlierMinimumBaseline = 3u;
constexpr double kMedianAbsoluteDeviationScale = 1.4826;

/// How many seed rows a cluster analysis walks between cancellation checkpoints.
constexpr std::uint32_t kClusterCheckpointRows = 64u;

/// One worker's counts in a map scan, alone on its cache line so two workers never share one.
struct alignas(64) MapCountSlot
{
    std::uint64_t lowElements = 0u;
    std::uint64_t highElements = 0u;
    std::uint64_t eitherElements = 0u;
    std::uint64_t lowRejections = 0u;
    std::uint64_t highRejections = 0u;
};

/// Counts one row of a low and a high map into a worker's slot.
///
/// Every choice is computed on values, so the loop has no branch and the compiler may widen it.
///
/// @param low the low map's row.
/// @param high the high map's row.
/// @param width how many elements the row holds.
/// @param minimum the fewest rejections an element needs to be counted.
/// @param slot the worker's counts.
void countPairedRow(const std::uint32_t* low, const std::uint32_t* high, std::uint64_t width, std::uint32_t minimum,
                    MapCountSlot& slot) noexcept
{
    std::uint64_t lowElements = 0u;
    std::uint64_t highElements = 0u;
    std::uint64_t eitherElements = 0u;
    std::uint64_t lowRejections = 0u;
    std::uint64_t highRejections = 0u;
    for (std::uint64_t x = 0u; x < width; ++x)
    {
        const std::uint32_t lowCount = low[x];
        const std::uint32_t highCount = high[x];
        const std::uint32_t lowReached = lowCount >= minimum ? 1u : 0u;
        const std::uint32_t highReached = highCount >= minimum ? 1u : 0u;
        lowElements += lowReached;
        highElements += highReached;
        eitherElements += lowReached | highReached;
        lowRejections += lowCount;
        highRejections += highCount;
    }
    slot.lowElements += lowElements;
    slot.highElements += highElements;
    slot.eitherElements += eitherElements;
    slot.lowRejections += lowRejections;
    slot.highRejections += highRejections;
}

/// Counts one row of a merged map into a worker's slot.
///
/// @param merged the map's row.
/// @param width how many elements the row holds.
/// @param minimum the fewest rejections an element needs to be counted.
/// @param slot the worker's counts.
void countMergedRow(const std::uint32_t* merged, std::uint64_t width, std::uint32_t minimum,
                    MapCountSlot& slot) noexcept
{
    std::uint64_t reached = 0u;
    std::uint64_t rejections = 0u;
    for (std::uint64_t x = 0u; x < width; ++x)
    {
        const std::uint32_t count = merged[x];
        reached += count >= minimum ? 1u : 0u;
        rejections += count;
    }
    slot.lowElements += reached;
    slot.eitherElements += reached;
    slot.lowRejections += rejections;
}

/// A failure naming the frame a count or a rate is about.
///
/// @param code why.
/// @param operation which operation.
/// @param message what a person reads.
/// @param frame the frame.
/// @param frames how many there are.
/// @return the failure.
[[nodiscard]] Status frameFailure(ErrorCode code, const char* operation, std::string message, std::uint64_t frame,
                                  std::uint64_t frames)
{
    return failure(code, operation, std::move(message),
                   {StatusContextField::ofCount(ContextKey::Requested, frame),
                    StatusContextField::ofCount(ContextKey::Limit, frames)});
}

/// The exact median of a non-empty range, reordering it.
///
/// @param values the values.
/// @param count how many, at least one.
/// @return the middle value, or the mean of the two middle values for an even count.
[[nodiscard]] double medianOf(double* values, std::uint64_t count) noexcept
{
    const std::uint64_t middle = count / 2u;
    std::nth_element(values, values + middle, values + count);
    if ((count % 2u) != 0u)
    {
        return values[middle];
    }
    return (*(std::max_element)(values, values + middle) + values[middle]) * 0.5;
}

/// Whether one detection ranks before another: more pixels first, then more rejections, then a
/// higher peak, then the lower channel, then the top-left corner.
///
/// @param left one detection.
/// @param right the other.
/// @return true when `left` is reported before `right`.
[[nodiscard]] bool ranksBefore(const RejectionClusterDetection& left, const RejectionClusterDetection& right) noexcept
{
    if (left.pixelCount != right.pixelCount)
    {
        return left.pixelCount > right.pixelCount;
    }
    if (left.integratedRejections != right.integratedRejections)
    {
        return left.integratedRejections > right.integratedRejections;
    }
    if (left.peakRejections != right.peakRejections)
    {
        return left.peakRejections > right.peakRejections;
    }
    if (left.channel != right.channel)
    {
        return left.channel < right.channel;
    }
    if (left.minY != right.minY)
    {
        return left.minY < right.minY;
    }
    return left.minX < right.minX;
}

/// Places a detection among the reported ones, dropping the last when the output is full.
///
/// @param clusters the output, whose leading `reported` entries are ordered.
/// @param reported how many entries are meaningful; raised when there was room.
/// @param detection the detection.
void placeDetection(std::span<RejectionClusterDetection> clusters, std::uint64_t& reported,
                    const RejectionClusterDetection& detection) noexcept
{
    const bool full = reported == clusters.size();
    if (full && !ranksBefore(detection, clusters[static_cast<std::size_t>(reported - 1u)]))
    {
        return;
    }
    std::uint64_t position = full ? reported - 1u : reported;
    while (position > 0u && ranksBefore(detection, clusters[static_cast<std::size_t>(position - 1u)]))
    {
        clusters[static_cast<std::size_t>(position)] = clusters[static_cast<std::size_t>(position - 1u)];
        --position;
    }
    clusters[static_cast<std::size_t>(position)] = detection;
    if (!full)
    {
        ++reported;
    }
}

} // namespace

StackWeightSummary summarizeStackFrameWeights(std::span<const double> weights) noexcept
{
    StackWeightSummary summary;
    double scale = 0.0;
    for (const double weight : weights)
    {
        if (std::isfinite(weight) && weight > 0.0)
        {
            ++summary.positiveCount;
            scale = (std::max)(scale, weight);
        }
    }
    if (summary.positiveCount == 0u)
    {
        return summary;
    }

    double sum = 0.0;
    double sumSquares = 0.0;
    double minimum = (std::numeric_limits<double>::max)();
    for (const double weight : weights)
    {
        if (!(std::isfinite(weight) && weight > 0.0))
        {
            continue;
        }
        const double normalized = weight / scale;
        sum += normalized;
        sumSquares += normalized * normalized;
        minimum = (std::min)(minimum, weight);
    }
    const double count = static_cast<double>(summary.positiveCount);
    const double mean = sum / count;
    double variance = 0.0;
    for (const double weight : weights)
    {
        if (!(std::isfinite(weight) && weight > 0.0))
        {
            continue;
        }
        const double delta = weight / scale - mean;
        variance += delta * delta;
    }
    variance /= count;

    summary.minimum = minimum;
    summary.maximum = scale;
    summary.standardDeviation = std::sqrt((std::max)(variance, 0.0)) * scale;
    summary.efficiency = sumSquares > 0.0 ? (sum * sum) / (count * sumSquares) : 0.0;
    return summary;
}

Result<FrameRejectionSummary> summarizeFrameRejection(const ExecutionContext& execution,
                                                      std::span<const std::uint64_t> frameValidSamples,
                                                      std::span<const std::uint64_t> frameRejectedSamples,
                                                      std::uint64_t rejectedLow, std::uint64_t rejectedHigh,
                                                      std::span<FrameRejectionRate> frameRates)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kSummarizeRejection,
                       "summarizing frame rejection requires an execution with an engine-context lease");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const std::uint64_t frames = frameValidSamples.size();
    if (frameRejectedSamples.size() != frames)
    {
        return refuse(ErrorCode::IncompatibleArguments, kSummarizeRejection, "frameRejectedSamples",
                      "frameRejectedSamples does not hold one count per frame of frameValidSamples");
    }
    if (frameRates.size() != frames)
    {
        return refuse(ErrorCode::IncompatibleArguments, kSummarizeRejection, "frameRates",
                      "frameRates does not hold one record per frame of frameValidSamples");
    }

    FrameRejectionSummary summary;
    summary.frameCount = static_cast<std::uint32_t>(frames);
    summary.rejectedLow = rejectedLow;
    summary.rejectedHigh = rejectedHigh;
    for (std::uint64_t frame = 0u; frame < frames; ++frame)
    {
        if (frameRejectedSamples[frame] > frameValidSamples[frame])
        {
            return frameFailure(ErrorCode::InvalidArgument, kSummarizeRejection,
                                "a frame rejected more samples than it held valid", frame, frames);
        }
        summary.validSamples += frameValidSamples[frame];
        if (frameValidSamples[frame] != 0u)
        {
            ++summary.ratedFrameCount;
        }
    }
    if (summary.ratedFrameCount == 0u)
    {
        return failure(ErrorCode::EmptySelection, kSummarizeRejection,
                       "no frame held a valid sample, so there is no rejection rate to summarize");
    }
    if (rejectedLow > summary.validSamples || rejectedHigh > summary.validSamples - rejectedLow)
    {
        return failure(ErrorCode::InvalidArgument, kSummarizeRejection,
                       "the rejected totals exceed the valid samples over every frame");
    }

    Result<plane_detail::ScopedContextMemory> copy = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(summary.ratedFrameCount) * sizeof(double), alignof(double),
        kSummarizeRejection);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    auto* const ranked = static_cast<double*>(copy->data());
    std::uint64_t rated = 0u;
    double sum = 0.0;
    double minimum = (std::numeric_limits<double>::max)();
    double maximum = std::numeric_limits<double>::lowest();
    for (std::uint64_t frame = 0u; frame < frames; ++frame)
    {
        if (frameValidSamples[frame] == 0u)
        {
            frameRates[frame] = FrameRejectionRate{};
            continue;
        }
        const double rate =
            static_cast<double>(frameRejectedSamples[frame]) / static_cast<double>(frameValidSamples[frame]);
        frameRates[frame] = FrameRejectionRate{.rate = rate, .rated = true};
        ranked[rated++] = rate;
        sum += rate;
        minimum = (std::min)(minimum, rate);
        maximum = (std::max)(maximum, rate);
    }
    summary.globalRate = static_cast<double>(rejectedLow + rejectedHigh) / static_cast<double>(summary.validSamples);
    summary.minimumRate = minimum;
    summary.maximumRate = maximum;
    summary.meanRate = sum / static_cast<double>(rated);
    summary.medianRate = medianOf(ranked, rated);
    return summary;
}

Result<RejectionFrameOutlierReport> detectRejectionFrameOutliers(const ExecutionContext& execution,
                                                                 std::span<const FrameRejectionRate> rates,
                                                                 std::span<std::uint32_t> flaggedFrames)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kDetectOutliers,
                       "detecting frame outliers requires an execution with an engine-context lease");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const std::uint64_t frames = rates.size();
    if (flaggedFrames.size() < frames)
    {
        return failure(ErrorCode::IncompatibleArguments, kDetectOutliers,
                       "flaggedFrames cannot hold one index per frame, which every frame may need",
                       {StatusContextField::ofText(ContextKey::Parameter, "flaggedFrames"),
                        StatusContextField::ofCount(ContextKey::Requested, flaggedFrames.size()),
                        StatusContextField::ofCount(ContextKey::Limit, frames)});
    }
    RejectionFrameOutlierReport report;
    report.frameCount = static_cast<std::uint32_t>(frames);
    for (std::uint64_t frame = 0u; frame < frames; ++frame)
    {
        if (!rates[frame].rated)
        {
            continue;
        }
        if (!std::isfinite(rates[frame].rate) || rates[frame].rate < 0.0)
        {
            return frameFailure(ErrorCode::InvalidArgument, kDetectOutliers,
                                "a rated frame's rejection rate must be finite and non-negative", frame, frames);
        }
        ++report.ratedFrameCount;
    }
    if (report.ratedFrameCount == 0u)
    {
        return failure(ErrorCode::EmptySelection, kDetectOutliers, "no frame is rated, so none can be an outlier");
    }

    // Two ranked copies of the rated rates: the rates, then their deviations from the median.
    Result<plane_detail::ScopedContextMemory> workspace = plane_detail::ScopedContextMemory::take(
        execution, 2u * static_cast<std::uint64_t>(report.ratedFrameCount) * sizeof(double), alignof(double),
        kDetectOutliers);
    if (!workspace.hasValue())
    {
        return workspace.status();
    }
    report.workingBytes = workspace->bytes();
    auto* const ranked = static_cast<double*>(workspace->data());
    double* const deviations = ranked + report.ratedFrameCount;
    std::uint64_t rated = 0u;
    for (const FrameRejectionRate& rate : rates)
    {
        if (rate.rated)
        {
            ranked[rated++] = rate.rate;
        }
    }
    const double median = medianOf(ranked, rated);
    for (std::uint64_t index = 0u; index < rated; ++index)
    {
        deviations[index] = std::abs(ranked[index] - median);
    }
    const double robustSigma =
        (std::max)(kMedianAbsoluteDeviationScale * medianOf(deviations, rated), kOutlierSigmaFloor);
    const double robustThreshold =
        (std::max)({kOutlierRateFloor, median + kOutlierRobustSigmas * robustSigma, median * kOutlierMedianMultiple});

    // The baseline: the rated frames at or below the robust threshold, or every rated frame when
    // fewer than three are, read in frame order.
    std::uint64_t baselineCount = 0u;
    double baselineSum = 0.0;
    for (const FrameRejectionRate& rate : rates)
    {
        if (rate.rated && rate.rate <= robustThreshold)
        {
            ++baselineCount;
            baselineSum += rate.rate;
        }
    }
    const bool everyFrame = baselineCount < kOutlierMinimumBaseline;
    if (everyFrame)
    {
        baselineCount = 0u;
        baselineSum = 0.0;
        for (const FrameRejectionRate& rate : rates)
        {
            if (rate.rated)
            {
                ++baselineCount;
                baselineSum += rate.rate;
            }
        }
    }
    const double mean = baselineSum / static_cast<double>(baselineCount);
    double variance = 0.0;
    for (const FrameRejectionRate& rate : rates)
    {
        if (rate.rated && (everyFrame || rate.rate <= robustThreshold))
        {
            variance += (rate.rate - mean) * (rate.rate - mean);
        }
    }
    variance /= static_cast<double>(baselineCount);
    const double deviation = std::sqrt((std::max)(0.0, variance));
    report.medianRate = median;
    report.robustSigma = robustSigma;
    report.robustThreshold = robustThreshold;
    report.baselineFrameCount = static_cast<std::uint32_t>(baselineCount);
    report.threshold =
        (std::max)(robustThreshold, mean + kOutlierBaselineSigmas * (std::max)(deviation, kOutlierSigmaFloor));

    for (std::uint64_t frame = 0u; frame < frames; ++frame)
    {
        if (rates[frame].rated && rates[frame].rate >= report.threshold)
        {
            flaggedFrames[static_cast<std::size_t>(report.flaggedFrameCount)] = static_cast<std::uint32_t>(frame);
            ++report.flaggedFrameCount;
        }
    }
    return report;
}

const char* rejectionClusterSourceName(RejectionClusterSource source) noexcept
{
    switch (source)
    {
    case RejectionClusterSource::Merged:
        return "Merged";
    case RejectionClusterSource::Low:
        return "Low";
    case RejectionClusterSource::High:
        return "High";
    }
    return "UnknownRejectionClusterSource";
}

Result<RejectionClusterReport> analyzeRejectionClusters(const ExecutionContext& execution,
                                                        std::span<const std::uint32_t> map, const ImageExtent& extent,
                                                        const RejectionClusterAnalysisRequest& request,
                                                        std::span<RejectionClusterDetection> clusters)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kAnalyzeClusters,
                       "analyzing rejection clusters requires an execution with an engine-context lease");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const std::uint64_t width = extent.width();
    const std::uint64_t height = extent.height();
    const std::uint64_t channels = extent.channelCount();
    const std::uint64_t planeSize = width * height;
    if (map.size() != planeSize * channels)
    {
        return failure(ErrorCode::IncompatibleArguments, kAnalyzeClusters,
                       "map does not hold one count per element of the extent",
                       {StatusContextField::ofText(ContextKey::Parameter, "map"),
                        StatusContextField::ofCount(ContextKey::Requested, map.size()),
                        StatusContextField::ofCount(ContextKey::Limit, planeSize * channels)});
    }
    if (clusters.empty())
    {
        return refuse(ErrorCode::IncompatibleArguments, kAnalyzeClusters, "clusters",
                      "clusters has no room for a detection");
    }
    if (request.minimumRejections == 0u)
    {
        return refuse(ErrorCode::InvalidArgument, kAnalyzeClusters, "request.minimumRejections",
                      "request.minimumRejections must be at least one, or every pixel is a cluster");
    }
    if (request.minimumClusterPixels == 0u)
    {
        return refuse(ErrorCode::InvalidArgument, kAnalyzeClusters, "request.minimumClusterPixels",
                      "request.minimumClusterPixels must be at least one");
    }
    if (!isRejectionClusterSource(request.source))
    {
        return refuse(ErrorCode::InvalidArgument, kAnalyzeClusters, "request.source",
                      "request.source is not one of the rejection map sources");
    }

    RejectionClusterReport report;
    report.source = request.source;
    report.width = extent.width();
    report.height = extent.height();
    report.channels = extent.channelCount();
    if (planeSize == 0u || channels == 0u)
    {
        return report;
    }

    // One visit mark per pixel and a fill stack that holds every pixel once at most, because a
    // pixel is marked when it is pushed.
    const std::uint64_t markBytes = planeSize / 8u + (planeSize % 8u != 0u ? 1u : 0u);
    using ClusterIndex = std::uint64_t;
    static_assert(std::numeric_limits<ClusterIndex>::digits >= std::numeric_limits<decltype(planeSize)>::digits);
    const std::uint64_t alignedMarkBytes =
        (markBytes + alignof(ClusterIndex) - 1u) / alignof(ClusterIndex) * alignof(ClusterIndex);
    if (planeSize > ((std::numeric_limits<std::uint64_t>::max)() - alignedMarkBytes) / sizeof(ClusterIndex))
        return failure(ErrorCode::LimitExceeded, kAnalyzeClusters, "the traversal workspace byte count overflows");
    const std::uint64_t stackBytes = planeSize * sizeof(ClusterIndex);
    Result<plane_detail::ScopedContextMemory> workspace = plane_detail::ScopedContextMemory::take(
        execution, alignedMarkBytes + stackBytes, alignof(ClusterIndex), kAnalyzeClusters);
    if (!workspace.hasValue())
    {
        return workspace.status();
    }
    report.workingBytes = workspace->bytes();
    auto* const marks = static_cast<std::uint8_t*>(workspace->data());
    auto* const stack = reinterpret_cast<ClusterIndex*>(marks + alignedMarkBytes);
    const auto visited = [marks](std::uint64_t index) noexcept {
        return ((marks[index / 8u] >> (index % 8u)) & 1u) != 0u;
    };
    const auto visit = [marks](std::uint64_t index) noexcept {
        marks[index / 8u] = static_cast<std::uint8_t>(marks[index / 8u] | (1u << (index % 8u)));
    };

    std::uint64_t reported = 0u;
    for (std::uint64_t channel = 0u; channel < channels; ++channel)
    {
        std::fill(marks, marks + markBytes, static_cast<std::uint8_t>(0u));
        const std::uint32_t* const plane = map.data() + channel * planeSize;
        for (std::uint64_t seedY = 0u; seedY < height; ++seedY)
        {
            if (seedY % kClusterCheckpointRows == 0u)
            {
                status = execution.checkCancellation();
                if (status.failed())
                {
                    return status;
                }
            }
            for (std::uint64_t seedX = 0u; seedX < width; ++seedX)
            {
                const std::uint64_t seed = seedY * width + seedX;
                if (visited(seed))
                {
                    continue;
                }
                visit(seed);
                if (plane[seed] < request.minimumRejections)
                {
                    continue;
                }

                RejectionClusterDetection detection;
                detection.source = request.source;
                detection.channel = static_cast<std::uint32_t>(channel);
                detection.minX = static_cast<std::uint32_t>(seedX);
                detection.maxX = static_cast<std::uint32_t>(seedX);
                detection.minY = static_cast<std::uint32_t>(seedY);
                detection.maxY = static_cast<std::uint32_t>(seedY);
                double weightedX = 0.0;
                double weightedY = 0.0;
                std::uint64_t depth = 0u;
                stack[depth++] = seed;
                while (depth != 0u)
                {
                    const std::uint64_t index = stack[--depth];
                    const std::uint64_t x = index % width;
                    const std::uint64_t y = index / width;
                    const std::uint32_t count = plane[index];
                    ++detection.pixelCount;
                    detection.integratedRejections += count;
                    detection.peakRejections = (std::max)(detection.peakRejections, count);
                    detection.minX = (std::min)(detection.minX, static_cast<std::uint32_t>(x));
                    detection.maxX = (std::max)(detection.maxX, static_cast<std::uint32_t>(x));
                    detection.minY = (std::min)(detection.minY, static_cast<std::uint32_t>(y));
                    detection.maxY = (std::max)(detection.maxY, static_cast<std::uint32_t>(y));
                    weightedX += static_cast<double>(x) * static_cast<double>(count);
                    weightedY += static_cast<double>(y) * static_cast<double>(count);

                    const std::uint64_t y0 = y == 0u ? 0u : y - 1u;
                    const std::uint64_t y1 = y + 1u < height ? y + 1u : y;
                    const std::uint64_t x0 = x == 0u ? 0u : x - 1u;
                    const std::uint64_t x1 = x + 1u < width ? x + 1u : x;
                    for (std::uint64_t ny = y0; ny <= y1; ++ny)
                    {
                        for (std::uint64_t nx = x0; nx <= x1; ++nx)
                        {
                            const bool diagonal = nx != x && ny != y;
                            if ((nx == x && ny == y) || (diagonal && !request.includeDiagonalNeighbours))
                            {
                                continue;
                            }
                            const std::uint64_t neighbour = ny * width + nx;
                            if (visited(neighbour))
                            {
                                continue;
                            }
                            visit(neighbour);
                            if (plane[neighbour] >= request.minimumRejections)
                            {
                                stack[depth++] = neighbour;
                            }
                        }
                    }
                }

                if (detection.pixelCount < request.minimumClusterPixels)
                {
                    continue;
                }
                detection.centroidX = weightedX / static_cast<double>(detection.integratedRejections);
                detection.centroidY = weightedY / static_cast<double>(detection.integratedRejections);
                detection.meanRejections =
                    static_cast<double>(detection.integratedRejections) / static_cast<double>(detection.pixelCount);
                ++report.clustersFound;
                placeDetection(clusters, reported, detection);
            }
        }
    }

    report.clustersReported = reported;
    for (std::uint64_t index = 0u; index < reported; ++index)
    {
        const RejectionClusterDetection& detection = clusters[static_cast<std::size_t>(index)];
        report.reportedClusterPixels += detection.pixelCount;
        report.largestClusterPixels = (std::max)(report.largestClusterPixels, detection.pixelCount);
    }
    const double elements = static_cast<double>(planeSize) * static_cast<double>(channels);
    report.clusteredPixelFraction = static_cast<double>(report.reportedClusterPixels) / elements;
    report.largestClusterFraction = static_cast<double>(report.largestClusterPixels) / elements;
    return report;
}

Result<RejectionMapCounts> countRejectionMaps(const ExecutionContext& execution, std::span<const std::uint32_t> low,
                                              std::span<const std::uint32_t> high, const ImageExtent& extent,
                                              const RejectionMapCountRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kCountMaps,
                       "counting rejection maps requires an execution with an engine-context lease");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const std::uint64_t width = extent.width();
    const std::uint64_t rows = static_cast<std::uint64_t>(extent.height()) * extent.channelCount();
    const std::uint64_t elements = width * rows;
    if (low.size() != elements)
    {
        return failure(ErrorCode::IncompatibleArguments, kCountMaps,
                       "low does not hold one count per element of the extent",
                       {StatusContextField::ofText(ContextKey::Parameter, "low"),
                        StatusContextField::ofCount(ContextKey::Requested, low.size()),
                        StatusContextField::ofCount(ContextKey::Limit, elements)});
    }
    if (!high.empty() && high.size() != elements)
    {
        return failure(ErrorCode::IncompatibleArguments, kCountMaps,
                       "high does not hold one count per element of the extent",
                       {StatusContextField::ofText(ContextKey::Parameter, "high"),
                        StatusContextField::ofCount(ContextKey::Requested, high.size()),
                        StatusContextField::ofCount(ContextKey::Limit, elements)});
    }
    if (request.minimumRejections == 0u)
    {
        return refuse(ErrorCode::InvalidArgument, kCountMaps, "request.minimumRejections",
                      "request.minimumRejections must be at least one, or every element is counted");
    }

    RejectionMapCounts counts;
    counts.elements = elements;
    const bool paired = !high.empty();
    const TaskCost cost{.bytesPerItem = width * (paired ? 2u : 1u) * sizeof(std::uint32_t),
                        .operationsPerItem = width * (paired ? 7u : 3u)};
    const std::uint32_t workers = plannedWorkerCount(execution, rows, cost);
    Result<plane_detail::ScopedContextMemory> block = plane_detail::ScopedContextMemory::take(
        execution, static_cast<std::uint64_t>(workers) * sizeof(MapCountSlot), alignof(MapCountSlot), kCountMaps);
    if (!block.hasValue())
    {
        return block.status();
    }
    auto* const slots = static_cast<MapCountSlot*>(block->data());
    for (std::uint32_t slot = 0u; slot < workers; ++slot)
    {
        std::construct_at(slots + slot);
    }
    const std::uint32_t minimum = request.minimumRejections;
    status = parallelForWorkers(execution, rows, cost, [&](const WorkerTask& task) -> Status {
        MapCountSlot& slot = slots[task.workerSlot];
        for (std::uint64_t row = task.range.begin; row < task.range.end; ++row)
        {
            const std::uint64_t offset = row * width;
            if (paired)
            {
                countPairedRow(low.data() + offset, high.data() + offset, width, minimum, slot);
            }
            else
            {
                countMergedRow(low.data() + offset, width, minimum, slot);
            }
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    for (std::uint32_t slot = 0u; slot < workers; ++slot)
    {
        counts.lowElements += slots[slot].lowElements;
        counts.highElements += slots[slot].highElements;
        counts.eitherElements += slots[slot].eitherElements;
        counts.lowRejections += slots[slot].lowRejections;
        counts.highRejections += slots[slot].highRejections;
    }
    counts.eitherFraction =
        elements > 0u ? static_cast<double>(counts.eitherElements) / static_cast<double>(elements) : 0.0;
    return counts;
}

} // namespace an
