// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a stack's weights, rejections and maps say about the stack.
///
/// The figures are a summary of the weight table, the per-frame rejection rates and their spread, a
/// robust flag on the frames rejected far more often than the rest, and a connected-component
/// analysis of a rejection map. They read counts and weights the executor already holds and write no
/// sample, so they are four operations over spans, each with one stated rule, that an executor calls
/// when it has the counts and a caller can call again later on the maps it was handed. A fifth counts
/// from the maps the elements a rejection reached on each side and on either, and the samples it
/// rejected.
///
/// Nothing here allocates per element. The operations that need working memory - a median over
/// the frame rates, a flood fill over a map and the per-worker counts of a map scan - take it once
/// from the engine context and return it before they return. Every order is fixed: rates are ranked by
/// value and frame index, clusters by size, integrated count, peak, channel and position, so two
/// runs of the same map produce the same records.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// What the finished surface looks like, and what it was built from.
///
/// The local-weighting report figures: the request that shaped the surface and the
/// moments of every frame's weight at every node.
struct LocalWeightingSummary
{
    /// The window the ratio was measured over, in pixels.
    std::uint32_t windowSize = 0u;

    /// The node spacing, in pixels.
    std::uint32_t gridSpacing = 0u;

    /// The exponent the ratio was raised to.
    double snrExponent = 0.0;

    /// How many frames were judged.
    std::uint32_t frameCount = 0u;

    /// How many nodes the lattice has.
    std::uint64_t nodeCount = 0u;

    /// How many nodes had at least `kMinimumLocalWeightActiveFrames` frames measured and were
    /// judged; at the others every frame keeps weight one.
    std::uint64_t activeNodes = 0u;

    /// How many frame-and-node entries received a judged weight.
    std::uint64_t weightedFrameNodes = 0u;

    /// The mean of every frame's weight at every node.
    double meanWeight = 0.0;

    /// The smallest weight.
    double minimumWeight = 0.0;

    /// The largest weight.
    double maximumWeight = 0.0;

    /// The population standard deviation of the weights.
    double weightStandardDeviation = 0.0;
};

/// What a table of frame weights looks like as a whole.
///
/// The weighting metrics: the weights are read on their own scale for the extremes, normalized by
/// the largest for the moments so the deviation is scale-free before it is put back on the weights'
/// scale, and the efficiency is the Kish effective count over the entries divided by their number -
/// one for uniform weights, lower when a few frames dominate. Only positive finite entries take
/// part; a zero excludes a frame and says nothing about how the others compare.
struct StackWeightSummary
{
    /// How many entries were positive and finite.
    std::uint64_t positiveCount = 0u;

    /// The smallest positive entry, or zero when there is none.
    double minimum = 0.0;

    /// The largest positive entry, or zero when there is none.
    double maximum = 0.0;

    /// The population standard deviation of the entries, on their own scale, or zero.
    double standardDeviation = 0.0;

    /// `(sum(w))^2 / (count * sum(w^2))`, in `(0, 1]`, or zero when there is no entry.
    double efficiency = 0.0;
};

/// Summarizes a table of frame weights.
///
/// Thread-safe, reentrant and allocation-free. Reads the span in index order, so the sums are the
/// same on every host.
///
/// @param weights the entries, per frame or per frame and channel. Non-positive and non-finite
///        entries are skipped and counted out.
/// @return the summary, which is all zeros for an empty or wholly non-positive table.
[[nodiscard]] AFTERNIGHTENGINE_API StackWeightSummary
summarizeStackFrameWeights(std::span<const double> weights) noexcept;

/// One frame's share of rejected samples, or the fact that it had none to reject.
///
/// A frame that contributed no valid sample has no rate, and the record says so rather than
/// carrying a zero that would read as a perfectly kept frame.
struct FrameRejectionRate
{
    /// Rejected samples over valid samples, in `[0, 1]`, meaningful only when `rated`.
    double rate = 0.0;

    /// Whether the frame contributed at least one valid sample.
    bool rated = false;
};

/// The rejection counts of a stack, as rates.
struct FrameRejectionSummary
{
    /// How many frames the stack had.
    std::uint32_t frameCount = 0u;

    /// How many of them contributed a valid sample and therefore have a rate.
    std::uint32_t ratedFrameCount = 0u;

    /// The valid samples over every frame.
    std::uint64_t validSamples = 0u;

    /// The samples rejected below the estimator's centre, over every frame.
    std::uint64_t rejectedLow = 0u;

    /// The samples rejected above it.
    std::uint64_t rejectedHigh = 0u;

    /// `(rejectedLow + rejectedHigh) / validSamples`.
    double globalRate = 0.0;

    /// The smallest rate over the rated frames.
    double minimumRate = 0.0;

    /// Their arithmetic mean.
    double meanRate = 0.0;

    /// Their median, the mean of the two middle rates for an even count.
    double medianRate = 0.0;

    /// The largest rate.
    double maximumRate = 0.0;
};

/// Turns per-frame valid and rejected sample counts into rates and their spread.
///
/// The frame rates are written for every frame, rated or not, in frame order; the summary's
/// extremes, mean and median are over the rated ones. The median is an exact rank taken from a
/// copy of the rates held in engine-context memory for the call and released before it returns.
///
/// Thread-safe with respect to the counts, which are only read. Allocates the rate copy from the
/// engine context and a failure it has to describe.
///
/// @param execution the operation the summary belongs to. Must hold something.
/// @param frameValidSamples one count per frame.
/// @param frameRejectedSamples one count per frame, at most the frame's valid count.
/// @param rejectedLow the low rejections over every frame.
/// @param rejectedHigh the high rejections over every frame.
/// @param frameRates receives one record per frame; exactly as long as the counts.
/// @return the summary; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint on entry;
///         `ErrorCode::IncompatibleArguments` when the spans are not the same length, naming the
///         one that differs; `ErrorCode::InvalidArgument` when a frame rejected more than it held,
///         naming the frame as the requested count, or when the low and high totals exceed the
///         valid count; `ErrorCode::EmptySelection` when no frame held a valid sample; and
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when the engine
///         context refuses the rate copy.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameRejectionSummary>
summarizeFrameRejection(const ExecutionContext& execution, std::span<const std::uint64_t> frameValidSamples,
                        std::span<const std::uint64_t> frameRejectedSamples, std::uint64_t rejectedLow,
                        std::uint64_t rejectedHigh, std::span<FrameRejectionRate> frameRates);

/// Which frames were rejected far more often than the rest, and the threshold that decided it.
///
/// The rule: a robust threshold from the median rate and its median absolute deviation - the
/// larger of 1.5 per cent, the median plus four deviations, and two and a half times the median -
/// then, over the frames at or below it, a mean-and-deviation threshold at two and a half
/// deviations, and the larger of the two flags a frame. Fewer than three frames below the robust
/// threshold makes every rated frame the baseline.
struct RejectionFrameOutlierReport
{
    /// How many frames the rates described.
    std::uint32_t frameCount = 0u;

    /// How many of them were rated and therefore judged.
    std::uint32_t ratedFrameCount = 0u;

    /// How many rated frames formed the mean-and-deviation baseline.
    std::uint32_t baselineFrameCount = 0u;

    /// How many frames were flagged.
    std::uint32_t flaggedFrameCount = 0u;

    /// The median of the rated rates.
    double medianRate = 0.0;

    /// The scaled median absolute deviation of the rated rates, floored at a quarter of a per cent.
    double robustSigma = 0.0;

    /// The first threshold, from the median and the deviation.
    double robustThreshold = 0.0;

    /// The threshold a frame's rate had to reach to be flagged.
    double threshold = 0.0;

    /// The engine-context bytes held for the ranked copies during the call.
    std::uint64_t workingBytes = 0u;
};

/// Flags the frames whose rejection rate stands far above the robust baseline.
///
/// Unrated frames are skipped. The flagged frame indices are written in frame order into the
/// leading `flaggedFrameCount` entries of `flaggedFrames`. The ranked copies live in engine-context
/// memory for the call.
///
/// Thread-safe with respect to the rates, which are only read. Allocates the copies from the engine
/// context and a failure it has to describe.
///
/// @param execution the operation the detection belongs to. Must hold something.
/// @param rates one record per frame, as `summarizeFrameRejection` wrote them.
/// @param flaggedFrames receives the flagged indices; at least as long as `rates`.
/// @return the report; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint on entry;
///         `ErrorCode::IncompatibleArguments` when `flaggedFrames` is shorter than `rates`;
///         `ErrorCode::InvalidArgument` for a rated rate that is not finite or is negative, naming
///         the frame; `ErrorCode::EmptySelection` when no frame is rated; and
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when the engine
///         context refuses the ranked copies.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RejectionFrameOutlierReport>
detectRejectionFrameOutliers(const ExecutionContext& execution, std::span<const FrameRejectionRate> rates,
                             std::span<std::uint32_t> flaggedFrames);

/// Which rejection map a cluster analysis read.
///
/// The analysis is the same over each; the source travels with every detection so a report of
/// clusters on the high side is not mistaken for one on the low side.
enum class RejectionClusterSource : std::uint8_t
{
    /// The merged map, both sides counted together.
    Merged,

    /// The map of rejections below the estimator's centre.
    Low,

    /// The map of rejections above it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    High
};

/// Whether a value names a source this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param source the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRejectionClusterSource(RejectionClusterSource source) noexcept
{
    return static_cast<std::uint8_t>(source) <= static_cast<std::uint8_t>(RejectionClusterSource::High);
}

/// The source's own name.
///
/// Thread-safe and allocation-free.
///
/// @param source the source to name. A value outside the enumeration is named
///        `"UnknownRejectionClusterSource"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* rejectionClusterSourceName(RejectionClusterSource source) noexcept;

/// How a rejection map is searched for clusters.
struct RejectionClusterAnalysisRequest
{
    /// Which map the analysis reads; recorded on the report and every detection.
    RejectionClusterSource source = RejectionClusterSource::Merged;

    /// The fewest rejections a pixel needs to belong to a cluster. At least one.
    std::uint32_t minimumRejections = 1u;

    /// The fewest pixels a component needs to be reported. At least one.
    std::uint64_t minimumClusterPixels = 8u;

    /// Whether the eight neighbours of a pixel connect it, or only the four sharing an edge.
    bool includeDiagonalNeighbours = true;
};

/// One connected component of rejected pixels.
struct RejectionClusterDetection
{
    /// Which map it was found on.
    RejectionClusterSource source = RejectionClusterSource::Merged;

    /// Which channel's plane.
    std::uint32_t channel = 0u;

    /// How many pixels it holds.
    std::uint64_t pixelCount = 0u;

    /// The leftmost column it reaches.
    std::uint32_t minX = 0u;

    /// The topmost row it reaches.
    std::uint32_t minY = 0u;

    /// The rightmost column it reaches, inclusive.
    std::uint32_t maxX = 0u;

    /// The bottommost row it reaches, inclusive.
    std::uint32_t maxY = 0u;

    /// The centroid's column, weighted by each pixel's rejection count.
    double centroidX = 0.0;

    /// The centroid's row, weighted the same way.
    double centroidY = 0.0;

    /// The sum of the rejection counts over its pixels.
    std::uint64_t integratedRejections = 0u;

    /// The largest rejection count of any of its pixels.
    std::uint32_t peakRejections = 0u;

    /// `integratedRejections / pixelCount`.
    double meanRejections = 0.0;
};

/// What a cluster analysis found, over the clusters it could report.
struct RejectionClusterReport
{
    /// Which map was read.
    RejectionClusterSource source = RejectionClusterSource::Merged;

    /// The map's width, in pixels.
    std::uint32_t width = 0u;

    /// The map's height, in pixels.
    std::uint32_t height = 0u;

    /// How many planes the map has.
    std::uint32_t channels = 0u;

    /// How many components reached the minimum size, whether or not they fit the output.
    std::uint64_t clustersFound = 0u;

    /// How many were written, the largest first.
    std::uint64_t clustersReported = 0u;

    /// The pixels of the reported clusters together.
    std::uint64_t reportedClusterPixels = 0u;

    /// The pixels of the largest reported cluster.
    std::uint64_t largestClusterPixels = 0u;

    /// `reportedClusterPixels` over the map's elements.
    double clusteredPixelFraction = 0.0;

    /// `largestClusterPixels` over the map's elements.
    double largestClusterFraction = 0.0;

    /// The engine-context bytes held for the visit marks and the fill stack during the call.
    std::uint64_t workingBytes = 0u;
};

/// Finds the connected components of a rejection count map.
///
/// The map is read plane by plane in packed planar order, one thirty-two-bit count per element, as
/// the rejection maps are written. A pixel belongs to a component when its count reaches the
/// minimum; components are grown by a flood fill over the requested neighbourhood and reported when
/// they reach the minimum size, the largest first - by pixel count, then integrated count, then
/// peak, then channel, then the top-left corner - so the order is the same on every run. When more
/// components qualify than `clusters` can hold, the largest are kept and the report's totals and
/// fractions describe those.
///
/// The visit marks and the fill stack are taken once from the engine context for the call: one
/// bit and one 64-bit traversal index per pixel of a single plane, with alignment padding.
/// Cancellation is checked on entry and every 64 rows of seeds.
///
/// Thread-safe with respect to the map, which is only read. Allocates the working memory from the
/// engine context and a failure it has to describe.
///
/// @param execution the operation the analysis belongs to. Must hold something.
/// @param map the counts, `width * height * channels` of them.
/// @param extent the map's width, height and channel count.
/// @param request the minimums, the neighbourhood and the source to record.
/// @param clusters receives the detections; its length is the most that are reported and must be
///        at least one.
/// @return the report; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::IncompatibleArguments` when the map does not hold one count per element of
///         the extent or `clusters` is empty; `ErrorCode::InvalidArgument` for a minimum of zero or
///         an unknown source; `ErrorCode::LimitExceeded` when the workspace byte count overflows;
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed`
///         when the engine context refuses the working memory; and `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` from a checkpoint.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RejectionClusterReport>
analyzeRejectionClusters(const ExecutionContext& execution, std::span<const std::uint32_t> map,
                         const ImageExtent& extent, const RejectionClusterAnalysisRequest& request,
                         std::span<RejectionClusterDetection> clusters);

/// How a stack's rejection maps are counted.
struct RejectionMapCountRequest
{
    /// The fewest rejections an element needs to be counted. At least one.
    std::uint32_t minimumRejections = 1u;
};

/// What a scan of a stack's rejection maps counted.
///
/// A map element counts how many of the stack's samples at that position the rejection removed or
/// replaced, so a map says where the rejection worked and how hard, not which frame lost the
/// samples; the per-frame counts are the executor's, kept while it combined. What a map does say
/// is the elements a rejection reached on each side and on either, and the samples it rejected in
/// all.
struct RejectionMapCounts
{
    /// How many elements one map holds: the width, times the height, times the channels.
    std::uint64_t elements = 0u;

    /// How many elements of the first map - the low map, or the merged one - reach the minimum.
    std::uint64_t lowElements = 0u;

    /// How many elements of the high map reach the minimum; zero without one.
    std::uint64_t highElements = 0u;

    /// How many elements reach it on either map, each counted once.
    std::uint64_t eitherElements = 0u;

    /// The first map's counts summed: every sample rejected below the estimator's centre, or on
    /// either side for a merged map.
    std::uint64_t lowRejections = 0u;

    /// The high map's counts summed: every sample rejected above the centre.
    std::uint64_t highRejections = 0u;

    /// `eitherElements / elements`, or zero for a map with no element.
    double eitherFraction = 0.0;
};

/// Counts the elements of a stack's rejection maps that reach a minimum, and the samples they
/// rejected.
///
/// The maps are read in packed planar order, one thirty-two-bit count per element, as the stack
/// writes them; a merged map is passed as `low` with `high` empty. The two sums are the low and
/// high totals `summarizeFrameRejection` takes, for a caller that holds the maps a stack wrote and
/// not the report it returned.
///
/// Runs on the operation's admitted threads, each counting its own rows, and the counts are exact
/// whatever their number. Cancellation is checked on entry and once per chunk of rows. Takes one
/// small block of engine-context memory for the per-worker counts.
///
/// Thread-safe with respect to the maps, which are only read.
///
/// @param execution the operation the scan belongs to. Must hold something.
/// @param low the low map, or the merged map: `width * height * channels` counts.
/// @param high the high map, of the same extent, or empty.
/// @param extent the maps' width, height and channel count.
/// @param request the minimum.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::IncompatibleArguments` when `low`, or a `high` that is not empty, does not
///         hold one count per element of the extent, naming it; `ErrorCode::InvalidArgument` for a
///         minimum of zero; `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed`
///         when the engine context refuses the per-worker counts; and `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` from a checkpoint.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RejectionMapCounts>
countRejectionMaps(const ExecutionContext& execution, std::span<const std::uint32_t> low,
                   std::span<const std::uint32_t> high, const ImageExtent& extent,
                   const RejectionMapCountRequest& request = {});

} // namespace an
