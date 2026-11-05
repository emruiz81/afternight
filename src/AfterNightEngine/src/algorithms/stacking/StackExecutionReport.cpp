// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack executor's finalization stage: the streaming output, the
// report's summaries and the memory the composed operations held.
//
// Every summary is computed from what the stack kept - the counts, the weight sums, the
// signal-to-noise gain map, the per-frame rejection records - and every reduction over the output
// is split into chunks that depend on the element count alone and added in chunk order, so the
// report is the same at every worker count.
#include "algorithms/stacking/StackExecutionState.h"

#include <AfterNightEngine/Algorithms/Stacking/StackMethodRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPreparedState.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "runtime/ProgressRange.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

namespace an::stacking
{

void recordComposedMemory(StackExecutionState& run, std::uint64_t scratchBytes)
{
    const std::uint64_t fitted = run.plan.memory.fittedNormalizationBytes != 0u && run.document != nullptr
                                     ? preparedStateBytesOf(*run.document)
                                     : 0u;
    run.composedPeakBytes = (std::max)(run.composedPeakBytes, run.set.account().heldBytes + fitted + scratchBytes);
}

namespace
{

/// The coverage bins one worker holds before adding them to the shared histogram: a direct-mapped
/// table keyed by the bin's low bits, so a chunk whose counts take few values - a dense stack,
/// where every count is the frame count - adds to the shared bins once per value rather than once
/// per element.
constexpr std::size_t kCoverageCachedBins = 64u;

/// One chunk's sums over the elements it covers.
struct ChunkSums
{
    double sum;
    double squares;
};

/// The elements chunk `index` of an even split of `elements` into `chunks` covers, the split a
/// deterministic reduction uses.
///
/// @param index which chunk.
/// @param elements how many elements.
/// @param chunks how many chunks, at least one.
/// @return the chunk's range.
[[nodiscard]] TaskRange chunkOf(std::uint64_t index, std::uint64_t elements, std::uint64_t chunks) noexcept
{
    const std::uint64_t base = elements / chunks;
    const std::uint64_t remainder = elements % chunks;
    const std::uint64_t begin = index * base + (index < remainder ? index : remainder);
    return TaskRange{.begin = begin, .end = begin + base + (index < remainder ? 1u : 0u)};
}

/// Lowers an atomic minimum and raises an atomic maximum to cover one worker's extremes.
///
/// @param minimum the shared minimum.
/// @param maximum the shared maximum.
/// @param low the worker's minimum.
/// @param high the worker's maximum.
void widen(std::atomic<std::uint32_t>& minimum, std::atomic<std::uint32_t>& maximum, std::uint32_t low,
           std::uint32_t high) noexcept
{
    std::uint32_t seen = minimum.load(std::memory_order_relaxed);
    while (low < seen && !minimum.compare_exchange_weak(seen, low, std::memory_order_relaxed))
    {
    }
    seen = maximum.load(std::memory_order_relaxed);
    while (high > seen && !maximum.compare_exchange_weak(seen, high, std::memory_order_relaxed))
    {
    }
}

/// The sum of one term over the elements that report one, and how many do, walked on the
/// operation's workers in chunks that depend on the element count alone and added in chunk order,
/// so the sum is the same at every width.
///
/// @tparam TermT a callable taking an element index and a double to write, returning whether the
///         element contributes.
/// @param execution the operation.
/// @param elements how many elements.
/// @param term the term.
/// @param total receives the sum.
/// @param contributing receives how many elements contributed.
/// @return success or cancellation.
template <typename TermT>
[[nodiscard]] Status sumByChunks(const ExecutionContext& execution, std::uint64_t elements, const TermT& term,
                                 double& total, std::uint64_t& contributing)
{
    const TaskCost elementCost{.bytesPerItem = 3u * sizeof(double), .operationsPerItem = 16u};
    const std::uint64_t chunks = deterministicChunkCount(elements, elementCost);
    std::array<double, kMaxDeterministicChunks> partials;
    std::atomic<std::uint64_t> sharedReached{0u};
    const TaskCost chunkCost{.bytesPerItem = elementCost.bytesPerItem * (elements / chunks + 1u),
                             .operationsPerItem = elementCost.operationsPerItem * (elements / chunks + 1u)};
    const Status walked = parallelFor(execution, chunks, chunkCost, [&](TaskRange range) -> Status {
        std::uint64_t reached = 0u;
        for (std::uint64_t chunk = range.begin; chunk < range.end; ++chunk)
        {
            const TaskRange items = chunkOf(chunk, elements, chunks);
            double sum = 0.0;
            for (std::uint64_t index = items.begin; index < items.end; ++index)
            {
                double value = 0.0;
                if (term(index, value))
                {
                    sum += value;
                    ++reached;
                }
            }
            partials[chunk] = sum;
        }
        sharedReached.fetch_add(reached, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    double sum = 0.0;
    for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
    {
        sum += partials[chunk];
    }
    total = sum;
    contributing = sharedReached.load(std::memory_order_relaxed);
    return Status::success();
}

/// The mean of one term over the elements that report one, as `sumByChunks` adds them.
///
/// @tparam TermT a callable taking an element index and a double to write, returning whether the
///         element contributes.
/// @param execution the operation.
/// @param elements how many elements.
/// @param term the term.
/// @param mean receives the mean, or zero when no element contributes.
/// @return success or cancellation.
template <typename TermT>
[[nodiscard]] Status meanByChunks(const ExecutionContext& execution, std::uint64_t elements, const TermT& term,
                                  double& mean)
{
    double sum = 0.0;
    std::uint64_t reached = 0u;
    const Status summed = sumByChunks(execution, elements, term, sum, reached);
    if (summed.failed())
    {
        return summed;
    }
    mean = reached != 0u ? sum / static_cast<double>(reached) : 0.0;
    return Status::success();
}

} // namespace

/// Adds one tile's signal-to-noise gain into the run's tallies, as `summarizeIntegration` reads the
/// whole gain map: the elements whose gain is positive, each tile's sum taken in chunks that depend
/// on its elements alone and the tiles added in row order.
///
/// @param run the run.
/// @param gain the tile's gain map.
/// @return success or cancellation.
[[nodiscard]] Status foldTileGain(StackExecutionState& run, std::span<const float> gain)
{
    double sum = 0.0;
    std::uint64_t reached = 0u;
    const Status summed = sumByChunks(
        *run.execution, gain.size(),
        [&](std::uint64_t index, double& value) {
            value = gain[index];
            return gain[index] > 0.0f;
        },
        sum, reached);
    if (summed.failed())
    {
        return summed;
    }
    run.tileGainSum += sum;
    run.tileGainReached += reached;
    return Status::success();
}

/// Folds a run of coverage counts into the run's histogram and a tally.
///
/// The elements are walked on the operation's workers in chunks that depend on the element count
/// alone, and the chunks' sums are added in chunk order. The sums are of whole numbers and exact
/// while they stay below 2^53, which is every stack the engine can hold; the histogram and the
/// extremes are integers, added exactly in any order. So a stack folded tile by tile tallies what
/// the same counts folded in one walk tally, at every width.
///
/// @param run the run, whose histogram receives the counts.
/// @param counts the counts to fold.
/// @param tally the tallies to add them to.
/// @return success or cancellation.
[[nodiscard]] Status foldCoverage(StackExecutionState& run, std::span<const std::uint32_t> counts,
                                  StackCoverageTally& tally)
{
    if (counts.empty())
    {
        return Status::success();
    }
    const TaskCost elementCost{.bytesPerItem = sizeof(std::uint32_t), .operationsPerItem = 8u};
    const std::uint64_t chunks = deterministicChunkCount(counts.size(), elementCost);
    std::array<ChunkSums, kMaxDeterministicChunks> partials;
    std::atomic<std::uint32_t> sharedMinimum{(std::numeric_limits<std::uint32_t>::max)()};
    std::atomic<std::uint32_t> sharedMaximum{0u};
    std::atomic<std::uint64_t> sharedReached{0u};
    const std::uint32_t frames = run.frames;
    const std::span<std::uint64_t> histogram = run.histogram;
    const TaskCost chunkCost{.bytesPerItem = elementCost.bytesPerItem * (counts.size() / chunks + 1u),
                             .operationsPerItem = elementCost.operationsPerItem * (counts.size() / chunks + 1u)};
    const Status walked = parallelFor(*run.execution, chunks, chunkCost, [&](TaskRange range) -> Status {
        std::array<std::uint32_t, kCoverageCachedBins> cachedBin;
        std::array<std::uint64_t, kCoverageCachedBins> cachedCount{};
        cachedBin.fill(0u);
        const auto flush = [&](std::size_t slot) {
            if (cachedCount[slot] != 0u)
            {
                std::atomic_ref<std::uint64_t>(histogram[cachedBin[slot]])
                    .fetch_add(cachedCount[slot], std::memory_order_relaxed);
                cachedCount[slot] = 0u;
            }
        };
        std::uint32_t low = (std::numeric_limits<std::uint32_t>::max)();
        std::uint32_t high = 0u;
        std::uint64_t reached = 0u;
        for (std::uint64_t chunk = range.begin; chunk < range.end; ++chunk)
        {
            const TaskRange elements = chunkOf(chunk, counts.size(), chunks);
            double sum = 0.0;
            double squares = 0.0;
            for (std::uint64_t index = elements.begin; index < elements.end; ++index)
            {
                const std::uint32_t count = counts[index];
                const std::uint32_t bin = count < frames ? count : frames;
                const std::size_t slot = bin % kCoverageCachedBins;
                if (cachedBin[slot] != bin)
                {
                    flush(slot);
                    cachedBin[slot] = bin;
                }
                ++cachedCount[slot];
                low = count < low ? count : low;
                high = count > high ? count : high;
                sum += count;
                squares += static_cast<double>(count) * count;
                reached += count != 0u ? 1u : 0u;
            }
            partials[chunk] = ChunkSums{.sum = sum, .squares = squares};
        }
        for (std::size_t slot = 0u; slot < kCoverageCachedBins; ++slot)
        {
            flush(slot);
        }
        widen(sharedMinimum, sharedMaximum, low, high);
        sharedReached.fetch_add(reached, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    double sum = 0.0;
    double squares = 0.0;
    for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
    {
        sum += partials[chunk].sum;
        squares += partials[chunk].squares;
    }
    tally.sum += sum;
    tally.squares += squares;
    tally.minimum = (std::min)(tally.minimum, sharedMinimum.load(std::memory_order_relaxed));
    tally.maximum = (std::max)(tally.maximum, sharedMaximum.load(std::memory_order_relaxed));
    tally.reached += sharedReached.load(std::memory_order_relaxed);
    tally.elements += counts.size();
    return Status::success();
}

/// Summarizes how many samples reached each output element, from the counts a streaming stack
/// kept or the coverage map a sample-history stack was asked for; a stack whose terms hold one
/// tile folded its counts as each tile was finalized, and one that handed its output over tile by
/// tile folded each tile's map before it left.
///
/// @param run the run.
/// @return success or cancellation.
[[nodiscard]] Status summarizeCoverage(StackExecutionState& run)
{
    StackCoverageSummary& coverage = run.report.coverage;
    StackCoverageTally tally = run.coverageTally;
    // A stack that handed its output over tile by tile folded each tile's counts before the tile left.
    if (!run.tileTerms && !run.outputByTile)
    {
        const std::span<const std::uint32_t> counts =
            run.history ? std::span<const std::uint32_t>(run.set.outputCountMap(StackOutputMap::Coverage))
                        : std::span<const std::uint32_t>(run.set.accumulatorCounts());
        tally = StackCoverageTally{};
        std::fill(run.histogram.begin(), run.histogram.end(), 0u);
        const Status folded = foldCoverage(run, counts, tally);
        if (folded.failed())
        {
            return folded;
        }
    }
    if (tally.elements == 0u)
    {
        coverage.available = false;
        return Status::success();
    }
    const auto elements = static_cast<double>(tally.elements);
    coverage.available = true;
    coverage.elements = tally.elements;
    coverage.elementsWithSamples = tally.reached;
    coverage.minimum = tally.minimum;
    coverage.maximum = tally.maximum;
    coverage.mean = tally.sum / elements;
    const double variance = tally.squares / elements - coverage.mean * coverage.mean;
    coverage.standardDeviation = variance > 0.0 ? std::sqrt(variance) : 0.0;
    // The median from the histogram: the middle element, or the mean of the two middle ones.
    const std::uint64_t total = tally.elements;
    const std::uint64_t lowerRank = (total - 1u) / 2u;
    const std::uint64_t upperRank = total / 2u;
    std::uint64_t seen = 0u;
    std::optional<double> lower{};
    std::optional<double> upper{};
    for (std::size_t bin = 0u; bin < run.histogram.size(); ++bin)
    {
        seen += run.histogram[bin];
        if (!lower.has_value() && seen > lowerRank)
        {
            lower = static_cast<double>(bin);
        }
        if (!upper.has_value() && seen > upperRank)
        {
            upper = static_cast<double>(bin);
            break;
        }
    }
    coverage.median = (lower.value_or(0.0) + upper.value_or(0.0)) * 0.5;
    return Status::success();
}

/// Derives the effective frame count and the gains from what the stack kept.
///
/// @param run the run.
/// @return success or cancellation.
[[nodiscard]] Status summarizeIntegration(StackExecutionState& run)
{
    double effective = 0.0;
    const std::span<const float> gain = run.set.outputValueMap(StackOutputMap::SignalToNoiseGain);
    const std::span<const double> weightSums = run.set.accumulatorWeightSums();
    const std::span<const double> weightSquares = run.set.accumulatorWeightSquareSums();
    const std::span<const std::uint32_t> counts = run.set.accumulatorCounts();
    Status summed = Status::success();
    if (run.outputByTile && run.model->request().output.signalToNoiseGainMap)
    {
        // The gain map left tile by tile; each tile's sum was folded before it did.
        effective = run.tileGainReached != 0u ? run.tileGainSum / static_cast<double>(run.tileGainReached) : 0.0;
    }
    else if (!gain.empty())
    {
        summed = meanByChunks(
            *run.execution, gain.size(),
            [&](std::uint64_t index, double& value) {
                value = gain[index];
                return gain[index] > 0.0f;
            },
            effective);
    }
    else if (!weightSums.empty() && !weightSquares.empty() && !counts.empty())
    {
        summed = meanByChunks(
            *run.execution, counts.size(),
            [&](std::uint64_t index, double& value) {
                if (counts[index] == 0u || !(weightSquares[index] > 0.0))
                {
                    return false;
                }
                value = weightSums[index] * weightSums[index] / weightSquares[index];
                return true;
            },
            effective);
    }
    else if (run.report.coverage.available && run.report.coverage.elementsWithSamples != 0u)
    {
        effective = run.report.coverage.mean * static_cast<double>(run.report.coverage.elements) /
                    static_cast<double>(run.report.coverage.elementsWithSamples);
    }
    if (summed.failed())
    {
        return summed;
    }
    run.report.effectiveFrameCount = effective;
    run.report.theoreticalSnrGain = std::sqrt(static_cast<double>(run.frames));
    run.report.actualSnrGain = std::sqrt(effective);
    run.report.integrationEfficiency = effective / static_cast<double>(run.frames);
    return Status::success();
}

/// Summarizes the rejection per frame, flags the frames that stood out, and searches the map
/// for clusters when asked.
///
/// @param run the run.
/// @return success or the first refusal that is not an empty selection.
[[nodiscard]] Status summarizeRejection(StackExecutionState& run)
{
    if (!run.model->usesRejection())
    {
        return Status::success();
    }
    const std::span<FrameRejectionRate> rates(run.rateRecords, run.frames);
    Result<FrameRejectionSummary> summary = summarizeFrameRejection(
        *run.execution, run.admittedCounts, run.rejectedCounts, run.report.mappedLow, run.report.mappedHigh, rates);
    if (!summary.hasValue())
    {
        if (summary.status().code() == ErrorCode::EmptySelection)
        {
            return Status::success();
        }
        return summary.status();
    }
    run.report.rejection = *summary;
    run.report.rejectionSummarized = true;
    if (!run.outputs->frameRejectionRates.empty())
    {
        std::copy(rates.begin(), rates.end(), run.outputs->frameRejectionRates.begin());
    }
    const Result<RejectionFrameOutlierReport> outliers =
        detectRejectionFrameOutliers(*run.execution, rates, run.flaggedRecords);
    if (!outliers.hasValue())
    {
        return outliers.status();
    }
    run.report.frameOutliers = *outliers;
    run.report.frameOutliersDetected = true;
    run.report.diagnosticsWorkingBytes += outliers->workingBytes;
    recordComposedMemory(run, outliers->workingBytes);
    if (!run.outputs->flaggedFrames.empty())
    {
        std::copy_n(run.flaggedRecords.begin(), outliers->flaggedFrameCount, run.outputs->flaggedFrames.begin());
    }
    if (!run.request->clusterAnalysis.has_value() || run.outputs->clusters.empty())
    {
        return Status::success();
    }
    const RejectionClusterAnalysisRequest& analysis = *run.request->clusterAnalysis;
    StackOutputMap which = StackOutputMap::RejectionMerged;
    switch (analysis.source)
    {
    case RejectionClusterSource::Merged:
        which = StackOutputMap::RejectionMerged;
        break;
    case RejectionClusterSource::Low:
        which = StackOutputMap::RejectionLow;
        break;
    case RejectionClusterSource::High:
        which = StackOutputMap::RejectionHigh;
        break;
    }
    const std::span<const std::uint32_t> map = run.set.outputCountMap(which);
    if (map.empty())
    {
        return Status::success();
    }
    const Result<RejectionClusterReport> clusters = analyzeRejectionClusters(
        *run.execution, map, run.model->request().input.descriptor.extent(), analysis, run.outputs->clusters);
    if (!clusters.hasValue())
    {
        return clusters.status();
    }
    run.report.clusters = *clusters;
    run.report.clustersAnalyzed = true;
    run.report.diagnosticsWorkingBytes += clusters->workingBytes;
    recordComposedMemory(run, clusters->workingBytes);
    return Status::success();
}

/// The finalization: a streaming stack's terms into the output, then the report's summaries.
///
/// @param run the run.
/// @param maskView the mask view.
/// @return success or the first refusal.
[[nodiscard]] Status finalize(StackExecutionState& run, const ValidityMaskView<std::uint8_t>* maskView)
{
    if (run.tileTerms)
    {
        // Every tile was finalized as it was combined; what remains is what they wrote together.
        run.report.finalize = StackMethodFinalizeOutcome{
            .finalize = run.tileFinalize,
            .outputDomain = stackMethodOutputDomain(*run.model, run.request->sumDomain).value_or(NumericDomain{}),
            .sumDomain = run.request->sumDomain,
            .peakSum = 0.0,
            .normalizedToPeak = false};
        run.report.elementsWithSamples = run.tileFinalize.elementsWithSamples;
        run.report.elementsWithoutSamples = run.tileFinalize.elementsWithoutSamples;
        run.report.nonFiniteOutputs = run.tileFinalize.nonFiniteOutputs;
    }
    else if (!run.history)
    {
        const Result<StackMethodFinalizeOutcome> outcome = finalizeStackMethod(
            *run.execution, *run.model, run.request->sumDomain, accumulatorsOf(run), targetsOf(run, maskView));
        if (!outcome.hasValue())
        {
            return outcome.status();
        }
        run.report.finalize = *outcome;
        run.report.elementsWithSamples = outcome->finalize.elementsWithSamples;
        run.report.elementsWithoutSamples = outcome->finalize.elementsWithoutSamples;
        run.report.nonFiniteOutputs = outcome->finalize.nonFiniteOutputs;
    }
    const Status coverage = summarizeCoverage(run);
    if (coverage.failed())
    {
        return coverage;
    }
    const Status integration = summarizeIntegration(run);
    if (integration.failed())
    {
        return integration;
    }
    const Status rejection = summarizeRejection(run);
    if (rejection.failed())
    {
        return rejection;
    }
    return Status::success();
}

} // namespace an::stacking
