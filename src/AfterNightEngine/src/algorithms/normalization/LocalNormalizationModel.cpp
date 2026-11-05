// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - local normalization: the model and fit stages.
//
// A node's samples become one background estimate here, a frame's nodes become its background
// grid, and two frames' residuals become the scale between them - as one number per channel or
// as a surface of its own. Every node is estimated independently on the operation's threads, so
// the grid does not depend on how many there were; the one pass that reads fitted neighbours runs
// afterwards, once, in node order.

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/normalization/LocalNormalizationInternal.h"
#include "algorithms/normalization/LocalNormalizationSampling.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>

namespace an
{

namespace
{

using local_normalization_detail::failure;
using local_normalization_detail::FrameReader;
using local_normalization_detail::parameterFailure;
using local_normalization_detail::Region;

constexpr const char* kEstimateOperation = "an::estimateLocalBackground";
constexpr const char* kFitOperation = "an::fitLocalBackgroundGrid";
constexpr const char* kScaleOperation = "an::estimateLocalScale";
constexpr const char* kScaleGridOperation = "an::fitLocalScaleGrid";

/// The fewest samples a sigma-clipping pass keeps going with.
constexpr std::size_t kMinClippingSamples = 8u;

/// How many samples the bright-tail rejection needs before it looks for a tail.
constexpr std::size_t kMinTailSamples = 2u * kMinLocalNormalizationValidSamples;

/// A node state the neighbour pass reads: not fitted, fitted, or recovered from neighbours.
enum class NodeState : std::uint8_t
{
    Unresolved = 0u,
    Fitted = 1u,
    Recovered = 2u
};

/// Rounds a byte count up to the engine's buffer alignment.
[[nodiscard]] std::uint64_t alignedBytes(std::uint64_t bytes) noexcept
{
    return (bytes + kDefaultBufferAlignmentBytes - 1u) & ~(kDefaultBufferAlignmentBytes - 1u);
}

/// A stated percentile of a run, reordering it.
[[nodiscard]] double percentileInPlace(double* values, std::size_t count, double percentile) noexcept
{
    const std::size_t rank =
        (std::min)(count - 1u, static_cast<std::size_t>(std::llround(percentile * static_cast<double>(count - 1u))));
    std::nth_element(values, values + rank, values + count);
    return values[rank];
}

/// The median or mode of the samples that survive iterated sigma clipping, reordering them.
[[nodiscard]] double sigmaClippedCentre(double* values, std::size_t count, double sigmaClip, std::uint32_t iterations,
                                        bool mode, double* scratch) noexcept
{
    for (std::uint32_t iteration = 0u; iteration < iterations; ++iteration)
    {
        if (count < kMinClippingSamples)
        {
            break;
        }
        std::copy(values, values + count, scratch);
        const double median = local_normalization_detail::medianInPlace(scratch, count);
        const double sigma = local_normalization_detail::robustDeviation(values, count, scratch);
        if (!std::isfinite(median) || !std::isfinite(sigma) || sigma <= 0.0)
        {
            break;
        }
        const double lower = median - sigmaClip * sigma;
        const double upper = median + sigmaClip * sigma;
        std::size_t kept = 0u;
        for (std::size_t index = 0u; index < count; ++index)
        {
            const double value = values[index];
            if (value >= lower && value <= upper)
            {
                scratch[kept++] = value;
            }
        }
        if (kept < kMinClippingSamples || kept == count)
        {
            break;
        }
        std::copy_n(scratch, kept, values);
        count = kept;
    }
    if (count == 0u)
    {
        return 0.0;
    }
    std::copy(values, values + count, scratch);
    const double median = local_normalization_detail::medianInPlace(scratch, count);
    if (!std::isfinite(median))
    {
        return 0.0;
    }
    if (!mode)
    {
        return median;
    }
    const double mean = std::accumulate(values, values + count, 0.0) / static_cast<double>(count);
    if (!std::isfinite(mean))
    {
        return median;
    }
    double centre = 3.0 * median - 2.0 * mean;
    const auto [lowest, highest] = std::minmax_element(values, values + count);
    centre = std::clamp(centre, *lowest, *highest);
    return std::isfinite(centre) ? centre : median;
}

/// The requested estimator over a run, with no fallback and no validation.
[[nodiscard]] double estimateRun(const LocalNormalizationRequest& request, double* values, std::size_t count,
                                 double* scratch) noexcept
{
    switch (request.estimator)
    {
    case LocalBackgroundEstimator::SigmaClippedMedian:
        return sigmaClippedCentre(values, count, request.sigmaClip, request.sigmaClipIterations, false, scratch);
    case LocalBackgroundEstimator::SigmaClippedMode:
        return sigmaClippedCentre(values, count, request.sigmaClip, request.sigmaClipIterations, true, scratch);
    case LocalBackgroundEstimator::Percentile:
        break;
    }
    return percentileInPlace(values, count, request.backgroundPercentile);
}

/// Drops a bright tail from a run when its sorted values show one clear gap in the lower part.
///
/// The run is sorted. A tail is rejected only when the largest gap between two adjacent values in
/// the lower two fifths reaches both a fifth of the whole range and 1e-5, which is what separates a
/// star's wing from the sky's own spread; everything above the gap is dropped.
///
/// @param values the run, sorted on return.
/// @param count how many values; replaced by the kept count when a tail is dropped.
/// @return true when a tail was dropped.
[[nodiscard]] bool rejectBrightTail(double* values, std::size_t& count) noexcept
{
    if (count < kMinTailSamples)
    {
        return false;
    }
    std::sort(values, values + count);
    const double range = values[count - 1u] - values[0];
    if (!std::isfinite(range) || range <= 1.0e-8)
    {
        return false;
    }
    const std::size_t maxKeep = (std::min)(count - 1u, (count * 2u) / 5u);
    if (maxKeep < kMinLocalNormalizationValidSamples)
    {
        return false;
    }
    std::size_t bestKeep = 0u;
    double bestGap = 0.0;
    for (std::size_t keep = kMinLocalNormalizationValidSamples; keep <= maxKeep; ++keep)
    {
        const double gap = values[keep] - values[keep - 1u];
        if (std::isfinite(gap) && gap > bestGap)
        {
            bestGap = gap;
            bestKeep = keep;
        }
    }
    const double minimumGap = (std::max)(1.0e-5, range * 0.2);
    if (bestKeep == 0u || bestGap < minimumGap)
    {
        return false;
    }
    count = bestKeep;
    return true;
}

/// The checks every two-frame estimator shares.
[[nodiscard]] Status checkPair(const char* operation, const ExecutionContext& execution,
                               const NormalizationFrameView& target, std::span<const double> targetBackground,
                               const NormalizationFrameView& reference, std::span<const double> referenceBackground,
                               const LocalNormalizationPlan& plan)
{
    if (!execution.valid())
    {
        return failure(operation, ErrorCode::NotInitialized,
                       "local normalization needs an execution holding an engine-context lease");
    }
    if (!plan.valid())
    {
        return failure(operation, ErrorCode::NotInitialized, "local normalization needs a validated plan");
    }
    if (!target.valid() || !reference.valid())
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument, "local normalization needs two checked frames",
                                "target");
    }
    if (!local_normalization_detail::frameMatchesPlan(plan, target) ||
        !local_normalization_detail::frameMatchesPlan(plan, reference))
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "a frame does not have the plan's extent and channel count");
    }
    const std::uint64_t valueCount = plan.shape().valueCount();
    if (targetBackground.size() != valueCount || referenceBackground.size() != valueCount)
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "a background grid does not hold exactly the shape's values");
    }
    if (!local_normalization_detail::allFinite(targetBackground) ||
        !local_normalization_detail::allFinite(referenceBackground))
    {
        return failure(operation, ErrorCode::InvalidData, "a background grid node is not finite");
    }
    return execution.checkCancellation();
}

} // namespace

Result<double> estimateLocalBackground(const LocalNormalizationRequest& request, std::span<double> samples,
                                       std::span<double> scratch)
{
    if (!isLocalBackgroundEstimator(request.estimator))
    {
        return parameterFailure(kEstimateOperation, ErrorCode::InvalidArgument,
                                "the background estimator is not one this build implements", "estimator");
    }
    if (samples.empty())
    {
        return parameterFailure(kEstimateOperation, ErrorCode::InvalidArgument,
                                "a background estimate needs at least one sample", "samples");
    }
    if (!std::isfinite(request.backgroundPercentile) || request.backgroundPercentile < 0.0 ||
        request.backgroundPercentile > 1.0 || !std::isfinite(request.sigmaClip) ||
        request.sigmaClip < kMinLocalNormalizationSigmaClip || request.sigmaClip > kMaxLocalNormalizationSigmaClip ||
        request.sigmaClipIterations == 0u || request.sigmaClipIterations > kMaxLocalNormalizationSigmaClipIterations)
    {
        return parameterFailure(kEstimateOperation, ErrorCode::InvalidArgument,
                                "the percentile or clipping fields lie outside their stated intervals",
                                "backgroundPercentile");
    }
    if (scratch.size() < samples.size())
    {
        return parameterFailure(kEstimateOperation, ErrorCode::InvalidArgument,
                                "the scratch span is shorter than the samples", "scratch");
    }
    if (!local_normalization_detail::allFinite(samples))
    {
        return failure(kEstimateOperation, ErrorCode::InvalidData, "a sample is not finite");
    }
    return estimateRun(request, samples.data(), samples.size(), scratch.data());
}

Result<LocalBackgroundFitResult> fitLocalBackgroundGrid(const ExecutionContext& execution,
                                                        const NormalizationFrameView& frame,
                                                        const LocalNormalizationPlan& plan, std::span<double> values)
{
    if (!execution.valid())
    {
        return failure(kFitOperation, ErrorCode::NotInitialized,
                       "a background fit needs an execution holding an engine-context lease");
    }
    if (!plan.valid())
    {
        return failure(kFitOperation, ErrorCode::NotInitialized, "a background fit needs a validated plan");
    }
    if (!frame.valid())
    {
        return parameterFailure(kFitOperation, ErrorCode::InvalidArgument, "a background fit needs a checked frame",
                                "frame");
    }
    if (!local_normalization_detail::frameMatchesPlan(plan, frame))
    {
        return failure(kFitOperation, ErrorCode::IncompatibleArguments,
                       "the frame does not have the plan's extent and channel count");
    }
    const LocalNormalizationGridShape& shape = plan.shape();
    if (values.size() != shape.valueCount())
    {
        return failure(kFitOperation, ErrorCode::IncompatibleArguments,
                       "the grid does not hold exactly the shape's values");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }

    const LocalNormalizationRequest& request = plan.request();
    const std::uint32_t nodes = shape.nodesPerAxis();
    const std::uint32_t nodeCount = shape.nodeCount();
    const std::uint32_t channels = shape.channelCount;
    const std::uint32_t samplesPerNode = plan.samplesPerNode();
    const std::uint64_t runLength = (std::max)(samplesPerNode, plan.maxExpandedSamplesPerNode());
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(nodes) * samplesPerNode * channels * sizeof(float),
                        .operationsPerItem = static_cast<std::uint64_t>(nodes) * samplesPerNode * channels * 8u};
    const std::uint32_t slots = (std::max)(plannedWorkerCount(execution, nodes, cost), 1u);
    const std::uint64_t perWorkerBytes = alignedBytes(plan.fitWorkingBytesPerWorker());
    const std::uint64_t stateBytes = alignedBytes(shape.valueCount());
    const std::uint64_t medianBytes = alignedBytes(static_cast<std::uint64_t>(nodeCount) * sizeof(double));
    const std::uint64_t totalBytes = perWorkerBytes * slots + stateBytes + medianBytes;
    MemoryResource* const memory = execution.memoryResource();
    Result<void*> taken = memory->allocate(totalBytes, kDefaultBufferAlignmentBytes);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    auto* const block = static_cast<std::byte*>(*taken);
    auto* const states = reinterpret_cast<NodeState*>(block + perWorkerBytes * slots);
    auto* const medianScratch = reinterpret_cast<double*>(block + perWorkerBytes * slots + stateBytes);
    std::fill(states, states + shape.valueCount(), NodeState::Unresolved);

    const FrameReader reader(frame);
    std::atomic<std::uint64_t> samplesRead{0u};
    std::atomic<std::uint32_t> lowSampleNodes{0u};
    std::atomic<std::uint32_t> sourceRejectionNodes{0u};
    std::atomic<std::uint32_t> expansionFallbacks{0u};
    std::atomic<std::uint32_t> robustCentreFallbacks{0u};
    std::atomic<std::uint32_t> nonFinite{0u};
    std::atomic<std::uint32_t> rowsDone{0u};

    const Status fitted = parallelForWorkers(execution, nodes, cost, [&](WorkerTask task) {
        auto* const worker = reinterpret_cast<double*>(block + perWorkerBytes * task.workerSlot);
        double* const nodeSamples = worker;
        double* const estimatorRun = nodeSamples + static_cast<std::uint64_t>(channels) * samplesPerNode;
        double* const scratch = estimatorRun + runLength;
        std::array<std::uint32_t, kMaxNormalizationChannels> written{};
        std::uint64_t read = 0u;
        std::uint32_t localLow = 0u;
        std::uint32_t localRejection = 0u;
        std::uint32_t localExpansion = 0u;
        std::uint32_t localRobust = 0u;
        std::uint32_t localNonFinite = 0u;
        for (std::uint64_t row = task.range.begin; row < task.range.end; ++row)
        {
            const Status stillRunning = execution.checkCancellation();
            if (stillRunning.failed())
            {
                return stillRunning;
            }
            const auto ny = static_cast<std::uint32_t>(row);
            for (std::uint32_t nx = 0u; nx < nodes; ++nx)
            {
                const std::uint32_t nodeIndex = ny * nodes + nx;
                const Region region = local_normalization_detail::nodeRegion(plan, nx, ny);
                local_normalization_detail::sampleRegionAllChannels(
                    reader, request, region, samplesPerNode, local_normalization_detail::nodeSeed(shape, nodeIndex),
                    channels, samplesPerNode, nodeSamples, written.data(), read);
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    double* const run = nodeSamples + static_cast<std::uint64_t>(channel) * samplesPerNode;
                    std::size_t count = written[channel];
                    double value = 0.0;
                    bool resolved = false;
                    if (count >= kMinLocalNormalizationValidSamples)
                    {
                        if (rejectBrightTail(run, count))
                        {
                            ++localRejection;
                        }
                        value = estimateRun(request, run, count, scratch);
                        resolved = std::isfinite(value);
                        localNonFinite += resolved ? 0u : 1u;
                    }
                    else
                    {
                        ++localLow;
                        for (std::uint32_t step = 1u; step <= kLocalNormalizationExpansionSteps && !resolved; ++step)
                        {
                            const Region expanded = local_normalization_detail::expandedNodeRegion(plan, nx, ny, step);
                            const std::uint64_t wanted = local_normalization_detail::expandedSampleCount(expanded);
                            std::size_t expandedCount = local_normalization_detail::sampleRegionChannel(
                                reader, request, expanded, (std::min)(wanted, runLength),
                                local_normalization_detail::expansionSeed(shape, nodeIndex, channel, step), channel,
                                estimatorRun, read);
                            if (expandedCount < kMinLocalNormalizationValidSamples)
                            {
                                continue;
                            }
                            if (rejectBrightTail(estimatorRun, expandedCount))
                            {
                                ++localRejection;
                            }
                            value = estimateRun(request, estimatorRun, expandedCount, scratch);
                            resolved = std::isfinite(value);
                            localNonFinite += resolved ? 0u : 1u;
                            ++localExpansion;
                        }
                        if (!resolved && count >= kMinLocalNormalizationRobustCentreSamples)
                        {
                            value = sigmaClippedCentre(run, count, 3.0, 2u, false, scratch);
                            resolved = std::isfinite(value);
                            localNonFinite += resolved ? 0u : 1u;
                            ++localRobust;
                        }
                    }
                    const std::uint64_t index = localNormalizationNodeIndex(shape, channel, nx, ny);
                    values[index] = resolved ? value : 0.0;
                    states[index] = resolved ? NodeState::Fitted : NodeState::Unresolved;
                }
            }
            execution.reportProgress(rowsDone.fetch_add(1u, std::memory_order_relaxed) + 1u,
                                     nodes + (request.backgroundSmoothingRadius > 0u ? channels : 0u),
                                     "fitting local background");
        }
        samplesRead.fetch_add(read, std::memory_order_relaxed);
        lowSampleNodes.fetch_add(localLow, std::memory_order_relaxed);
        sourceRejectionNodes.fetch_add(localRejection, std::memory_order_relaxed);
        expansionFallbacks.fetch_add(localExpansion, std::memory_order_relaxed);
        robustCentreFallbacks.fetch_add(localRobust, std::memory_order_relaxed);
        nonFinite.fetch_add(localNonFinite, std::memory_order_relaxed);
        return Status::success();
    });
    if (fitted.failed())
    {
        memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);
        return fitted;
    }
    if (nonFinite.load(std::memory_order_relaxed) != 0u)
    {
        memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);
        return failure(kFitOperation, ErrorCode::InvalidData, "an admitted sample produced a non-finite estimate");
    }

    // The neighbour pass, once, in node order, over what the parallel pass fitted.
    std::uint32_t neighbourFallbacks = 0u;
    std::uint32_t unresolvedNodes = 0u;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t ny = 0u; ny < nodes; ++ny)
        {
            for (std::uint32_t nx = 0u; nx < nodes; ++nx)
            {
                const std::uint64_t index = localNormalizationNodeIndex(shape, channel, nx, ny);
                if (states[index] != NodeState::Unresolved)
                {
                    continue;
                }
                std::array<double, 4> neighbours{};
                std::size_t found = 0u;
                const auto consider = [&](std::uint32_t x, std::uint32_t y) {
                    const std::uint64_t at = localNormalizationNodeIndex(shape, channel, x, y);
                    if (states[at] != NodeState::Unresolved)
                    {
                        neighbours[found++] = values[at];
                    }
                };
                if (nx > 0u)
                {
                    consider(nx - 1u, ny);
                }
                if (ny > 0u)
                {
                    consider(nx, ny - 1u);
                }
                if (nx > 0u && ny > 0u)
                {
                    consider(nx - 1u, ny - 1u);
                }
                if (ny > 0u && nx + 1u < nodes)
                {
                    consider(nx + 1u, ny - 1u);
                }
                if (found == 0u)
                {
                    continue;
                }
                values[index] = local_normalization_detail::medianInPlace(neighbours.data(), found);
                states[index] = NodeState::Recovered;
                ++neighbourFallbacks;
            }
        }
        // Whatever is still unresolved takes the median of the channel's resolved nodes, which is
        // the level the frame has where it was measured rather than the marker it has where it was
        // not.
        std::size_t resolvedCount = 0u;
        for (std::uint32_t node = 0u; node < nodeCount; ++node)
        {
            const std::uint64_t index = static_cast<std::uint64_t>(channel) * nodeCount + node;
            if (states[index] != NodeState::Unresolved)
            {
                medianScratch[resolvedCount++] = values[index];
            }
        }
        if (resolvedCount == 0u)
        {
            memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);
            return failure(kFitOperation, ErrorCode::EmptySelection,
                           "no node of a channel admitted enough samples to estimate a background");
        }
        if (resolvedCount < nodeCount)
        {
            const double level = local_normalization_detail::medianInPlace(medianScratch, resolvedCount);
            for (std::uint32_t node = 0u; node < nodeCount; ++node)
            {
                const std::uint64_t index = static_cast<std::uint64_t>(channel) * nodeCount + node;
                if (states[index] == NodeState::Unresolved)
                {
                    values[index] = level;
                    ++unresolvedNodes;
                }
            }
        }
    }
    memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);

    LocalBackgroundFitResult result{};
    result.nodeCount = nodeCount;
    result.channelCount = channels;
    result.samplesRead = samplesRead.load(std::memory_order_relaxed);
    result.lowSampleNodes = lowSampleNodes.load(std::memory_order_relaxed);
    result.sourceRejectionNodes = sourceRejectionNodes.load(std::memory_order_relaxed);
    result.expansionFallbacks = expansionFallbacks.load(std::memory_order_relaxed);
    result.robustCentreFallbacks = robustCentreFallbacks.load(std::memory_order_relaxed);
    result.neighbourFallbacks = neighbourFallbacks;
    result.unresolvedNodes = unresolvedNodes;
    result.workingBytes = totalBytes;
    result.plannedWorkers = slots;
    if (request.backgroundSmoothingRadius > 0u)
    {
        const ProgressRange progress(execution, nodes, nodes + channels, nodes + channels);
        const Status smoothed =
            smoothLocalGrid(execution, shape, values, request.backgroundSmoothingRadius, LocalGridSmoothing::Gaussian);
        if (smoothed.failed())
        {
            return smoothed;
        }
        result.smoothed = true;
        const std::uint64_t radius = (std::min)(request.backgroundSmoothingRadius, shape.gridSize);
        result.workingBytes =
            (std::max)(result.workingBytes,
                       (2u * static_cast<std::uint64_t>(nodeCount) + 2u * radius + 1u) * sizeof(double));
    }
    return result;
}

Result<LocalScaleEstimate> estimateLocalScale(const ExecutionContext& execution, const NormalizationFrameView& target,
                                              std::span<const double> targetBackground,
                                              const NormalizationFrameView& reference,
                                              std::span<const double> referenceBackground,
                                              const LocalNormalizationPlan& plan)
{
    const Status ready =
        checkPair(kScaleOperation, execution, target, targetBackground, reference, referenceBackground, plan);
    if (ready.failed())
    {
        return ready;
    }
    const LocalNormalizationGridShape& shape = plan.shape();
    const LocalNormalizationRequest& request = plan.request();
    const bool bicubic = request.interpolation == LocalGridInterpolation::Bicubic;
    const std::uint64_t total = static_cast<std::uint64_t>(shape.width) * shape.height;
    const std::uint64_t count = (std::min)(static_cast<std::uint64_t>(request.scaleSamples), total);
    const std::uint64_t bytes = alignedBytes(2ull * count * sizeof(double));
    MemoryResource* const memory = execution.memoryResource();
    Result<void*> taken = memory->allocate(bytes, kDefaultBufferAlignmentBytes);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    auto* const ratios = static_cast<double*>(*taken);
    double* const scratch = ratios + count;
    const FrameReader targetReader(target);
    const FrameReader referenceReader(reference);
    const std::uint64_t base = local_normalization_detail::kSamplingSeed ^ static_cast<std::uint64_t>(shape.width) ^
                               (static_cast<std::uint64_t>(shape.height) << 32u);

    LocalScaleEstimate estimate{};
    estimate.channelCount = shape.channelCount;
    const auto forChannel = [&](std::uint32_t channel, double& scale, std::uint32_t& used) {
        std::uint64_t state = base ^ (static_cast<std::uint64_t>(channel) + 1u);
        std::size_t ratioCount = 0u;
        for (std::uint64_t sample = 0u; sample < count; ++sample)
        {
            const std::uint64_t index = local_normalization_detail::nextLcg(state) % total;
            const auto x = static_cast<std::uint32_t>(index % shape.width);
            const auto y = static_cast<std::uint32_t>(index / shape.width);
            double referenceValue = 0.0;
            double targetValue = 0.0;
            if (!referenceReader.read(x, y, channel, referenceValue) || !targetReader.read(x, y, channel, targetValue))
            {
                continue;
            }
            if (!(referenceValue >= request.lowClip && referenceValue < request.highClip &&
                  targetValue >= request.lowClip && targetValue < request.highClip))
            {
                continue;
            }
            const double referenceLevel = local_normalization_detail::evaluateGridUnchecked(
                shape, referenceBackground, bicubic, channel, static_cast<double>(x), static_cast<double>(y));
            const double targetLevel = local_normalization_detail::evaluateGridUnchecked(
                shape, targetBackground, bicubic, channel, static_cast<double>(x), static_cast<double>(y));
            const double r = referenceValue - referenceLevel;
            const double t = targetValue - targetLevel;
            if (!(r > kLocalNormalizationResidualEpsilon && t > kLocalNormalizationResidualEpsilon))
            {
                continue;
            }
            const double ratio = r / t;
            if (!std::isfinite(ratio) || ratio <= 0.0 || ratio > kMaxLocalNormalizationRatio)
            {
                continue;
            }
            ratios[ratioCount++] = ratio;
        }
        used = static_cast<std::uint32_t>(ratioCount);
        if (ratioCount < kMinLocalNormalizationScaleRatios)
        {
            scale = 1.0;
            ++estimate.unityFallbacks;
            return;
        }
        scale = local_normalization_detail::robustMedianRatio(ratios, ratioCount, scratch);
    };

    if (request.scaleMode == LocalScaleMode::Linked && shape.channelCount > 1u)
    {
        const std::uint32_t measured = plan.scaleChannel();
        double scale = 1.0;
        std::uint32_t used = 0u;
        forChannel(measured, scale, used);
        estimate.measuredChannel = measured;
        for (std::uint32_t channel = 0u; channel < shape.channelCount; ++channel)
        {
            estimate.scale[channel] = scale;
            estimate.ratiosUsed[channel] = used;
        }
    }
    else
    {
        for (std::uint32_t channel = 0u; channel < shape.channelCount; ++channel)
        {
            const Status stillRunning = execution.checkCancellation();
            if (stillRunning.failed())
            {
                memory->deallocate(*taken, bytes, kDefaultBufferAlignmentBytes);
                return stillRunning;
            }
            forChannel(channel, estimate.scale[channel], estimate.ratiosUsed[channel]);
        }
    }
    memory->deallocate(*taken, bytes, kDefaultBufferAlignmentBytes);
    estimate.workingBytes = bytes;
    return estimate;
}

Result<LocalScaleGridResult> fitLocalScaleGrid(const ExecutionContext& execution, const NormalizationFrameView& target,
                                               std::span<const double> targetBackground,
                                               const NormalizationFrameView& reference,
                                               std::span<const double> referenceBackground,
                                               const LocalNormalizationPlan& plan, std::span<const double> globalScale,
                                               std::span<double> values)
{
    const Status ready =
        checkPair(kScaleGridOperation, execution, target, targetBackground, reference, referenceBackground, plan);
    if (ready.failed())
    {
        return ready;
    }
    const LocalNormalizationGridShape& shape = plan.shape();
    if (globalScale.size() != shape.channelCount)
    {
        return failure(kScaleGridOperation, ErrorCode::IncompatibleArguments,
                       "the global scale does not hold one value per channel");
    }
    for (const double scale : globalScale)
    {
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return parameterFailure(kScaleGridOperation, ErrorCode::InvalidArgument,
                                    "a global scale is finite and positive", "globalScale");
        }
    }
    if (values.size() != shape.valueCount())
    {
        return failure(kScaleGridOperation, ErrorCode::IncompatibleArguments,
                       "the scale grid does not hold exactly the shape's values");
    }
    const LocalNormalizationRequest& request = plan.request();
    const bool bicubic = request.interpolation == LocalGridInterpolation::Bicubic;
    const std::uint32_t nodes = shape.nodesPerAxis();
    const std::uint32_t nodeCount = shape.nodeCount();
    const std::uint32_t channels = shape.channelCount;
    const std::uint32_t samples = plan.samplesPerScaleNode();
    const std::uint32_t roiWidth = plan.scaleRoiWidth();
    const std::uint32_t roiHeight = plan.scaleRoiHeight();
    const std::uint64_t area = static_cast<std::uint64_t>(roiWidth) * roiHeight;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(nodes) * samples * channels * sizeof(float) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(nodes) * samples * channels * 64u};
    const std::uint32_t slots = (std::max)(plannedWorkerCount(execution, nodes, cost), 1u);
    const std::uint64_t perWorkerBytes = alignedBytes(4ull * samples * sizeof(double));
    const std::uint64_t maskBytes = alignedBytes(shape.valueCount());
    const std::uint64_t medianBytes = alignedBytes(static_cast<std::uint64_t>(nodeCount) * sizeof(double));
    const std::uint64_t totalBytes = perWorkerBytes * slots + maskBytes + medianBytes;
    MemoryResource* const memory = execution.memoryResource();
    Result<void*> taken = memory->allocate(totalBytes, kDefaultBufferAlignmentBytes);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    auto* const block = static_cast<std::byte*>(*taken);
    auto* const fallbackMask = reinterpret_cast<std::uint8_t*>(block + perWorkerBytes * slots);
    auto* const medianScratch = reinterpret_cast<double*>(block + perWorkerBytes * slots + maskBytes);
    std::fill(fallbackMask, fallbackMask + shape.valueCount(), std::uint8_t{0u});

    const FrameReader targetReader(target);
    const FrameReader referenceReader(reference);
    const std::uint64_t base = local_normalization_detail::geometrySeed(shape);
    const bool linked = request.scaleMode == LocalScaleMode::Linked && channels > 1u;
    std::atomic<std::uint32_t> confidenceFallbacks{0u};
    std::atomic<std::uint32_t> rowsDone{0u};

    const Status fitted = parallelForWorkers(execution, nodes, cost, [&](WorkerTask task) {
        auto* const worker = reinterpret_cast<double*>(block + perWorkerBytes * task.workerSlot);
        double* const residualReference = worker;
        double* const residualTarget = residualReference + samples;
        double* const ratios = residualTarget + samples;
        double* const scratch = ratios + samples;
        std::uint32_t localFallbacks = 0u;
        const auto estimateNode = [&](std::uint32_t roiX, std::uint32_t roiY, std::uint32_t nodeIndex,
                                      std::uint32_t channel, bool& fellBack) -> double {
            fellBack = false;
            std::uint64_t state =
                base ^ (static_cast<std::uint64_t>(nodeIndex) + 1u) * local_normalization_detail::kNodeSeedMultiplier ^
                (static_cast<std::uint64_t>(channel) + 1u) * local_normalization_detail::kChannelSeedMultiplier;
            std::size_t residualCount = 0u;
            for (std::uint32_t sample = 0u; sample < samples; ++sample)
            {
                const std::uint64_t index = local_normalization_detail::nextLcg(state) % area;
                const auto x = static_cast<std::uint32_t>(roiX + index % roiWidth);
                const auto y = static_cast<std::uint32_t>(roiY + index / roiWidth);
                double referenceValue = 0.0;
                double targetValue = 0.0;
                if (!referenceReader.read(x, y, channel, referenceValue) ||
                    !targetReader.read(x, y, channel, targetValue))
                {
                    continue;
                }
                if (!(referenceValue >= request.lowClip && referenceValue < request.highClip &&
                      targetValue >= request.lowClip && targetValue < request.highClip))
                {
                    continue;
                }
                const double referenceLevel = local_normalization_detail::evaluateGridUnchecked(
                    shape, referenceBackground, bicubic, channel, static_cast<double>(x), static_cast<double>(y));
                const double targetLevel = local_normalization_detail::evaluateGridUnchecked(
                    shape, targetBackground, bicubic, channel, static_cast<double>(x), static_cast<double>(y));
                residualReference[residualCount] = referenceValue - referenceLevel;
                residualTarget[residualCount] = targetValue - targetLevel;
                ++residualCount;
            }
            if (residualCount < kMinLocalNormalizationValidSamples)
            {
                fellBack = true;
                return globalScale[channel];
            }
            const double sigmaReference =
                local_normalization_detail::robustDeviation(residualReference, residualCount, scratch);
            const double sigmaTarget =
                local_normalization_detail::robustDeviation(residualTarget, residualCount, scratch);
            const double threshold =
                (std::max)(kLocalNormalizationResidualEpsilon,
                           kLocalNormalizationScaleSigmaThreshold * (std::max)(sigmaReference, sigmaTarget));
            std::size_t ratioCount = 0u;
            for (std::size_t index = 0u; index < residualCount; ++index)
            {
                const double r = residualReference[index];
                const double t = residualTarget[index];
                if (!(r > threshold && t > threshold))
                {
                    continue;
                }
                const double ratio = r / t;
                if (!std::isfinite(ratio) || ratio <= 0.0 || ratio > kMaxLocalNormalizationRatio)
                {
                    continue;
                }
                ratios[ratioCount++] = ratio;
            }
            const std::size_t minimumRatios =
                (std::min)(static_cast<std::size_t>(samples), (std::max)(std::size_t{48u}, residualCount / 16u));
            if (ratioCount < minimumRatios)
            {
                fellBack = true;
                return globalScale[channel];
            }
            return local_normalization_detail::robustMedianRatio(ratios, ratioCount, scratch);
        };
        for (std::uint64_t row = task.range.begin; row < task.range.end; ++row)
        {
            const Status stillRunning = execution.checkCancellation();
            if (stillRunning.failed())
            {
                return stillRunning;
            }
            const auto ny = static_cast<std::uint32_t>(row);
            const std::uint32_t y = plan.nodeY(ny);
            const std::uint32_t roiY = local_normalization_detail::regionOrigin(y, roiHeight, shape.height);
            for (std::uint32_t nx = 0u; nx < nodes; ++nx)
            {
                const std::uint32_t x = plan.nodeX(nx);
                const std::uint32_t roiX = local_normalization_detail::regionOrigin(x, roiWidth, shape.width);
                const std::uint32_t nodeIndex = ny * nodes + nx;
                if (linked)
                {
                    bool fellBack = false;
                    const double value = estimateNode(roiX, roiY, nodeIndex, plan.scaleChannel(), fellBack);
                    localFallbacks += fellBack ? 1u : 0u;
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        const std::uint64_t index = localNormalizationNodeIndex(shape, channel, nx, ny);
                        values[index] = fellBack ? globalScale[channel] : value;
                        fallbackMask[index] = fellBack ? 1u : 0u;
                    }
                    continue;
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    bool fellBack = false;
                    const double value = estimateNode(roiX, roiY, nodeIndex, channel, fellBack);
                    localFallbacks += fellBack ? 1u : 0u;
                    const std::uint64_t index = localNormalizationNodeIndex(shape, channel, nx, ny);
                    values[index] = value;
                    fallbackMask[index] = fellBack ? 1u : 0u;
                }
            }
            execution.reportProgress(rowsDone.fetch_add(1u, std::memory_order_relaxed) + 1u,
                                     nodes + (request.localScaleSmoothingRadius > 0u ? channels : 0u),
                                     "fitting local scale");
        }
        confidenceFallbacks.fetch_add(localFallbacks, std::memory_order_relaxed);
        return Status::success();
    });
    if (fitted.failed())
    {
        memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);
        return fitted;
    }

    LocalScaleGridResult result{};
    result.nodeCount = nodeCount;
    result.channelCount = channels;
    result.confidenceFallbacks = confidenceFallbacks.load(std::memory_order_relaxed);
    if (request.localScaleSmoothingRadius > 0u)
    {
        const ProgressRange progress(execution, nodes, nodes + channels, nodes + channels);
        const Status smoothed =
            smoothLocalGrid(execution, shape, values, request.localScaleSmoothingRadius, LocalGridSmoothing::Box);
        if (smoothed.failed())
        {
            memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);
            return smoothed;
        }
        result.smoothed = true;
    }
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        double* const plane = values.data() + static_cast<std::uint64_t>(channel) * nodeCount;
        std::copy(plane, plane + nodeCount, medianScratch);
        const double median = local_normalization_detail::medianInPlace(medianScratch, nodeCount);
        if (!(std::isfinite(median) && median > 0.0))
        {
            continue;
        }
        const double factor = globalScale[channel] / median;
        for (std::uint32_t node = 0u; node < nodeCount; ++node)
        {
            plane[node] = std::clamp(plane[node] * factor, kMinLocalNormalizationScale, kMaxLocalNormalizationScale);
        }
        ++result.normalizedChannels;
    }
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t node = 0u; node < nodeCount; ++node)
        {
            const std::uint64_t index = static_cast<std::uint64_t>(channel) * nodeCount + node;
            if (fallbackMask[index] != 0u)
            {
                values[index] = globalScale[channel];
            }
        }
    }
    memory->deallocate(*taken, totalBytes, kDefaultBufferAlignmentBytes);
    result.workingBytes = totalBytes;
    if (result.smoothed)
    {
        const std::uint64_t radius = (std::min)(request.localScaleSmoothingRadius, shape.gridSize);
        result.workingBytes += (2u * static_cast<std::uint64_t>(nodeCount) + 2u * radius + 1u) * sizeof(double);
    }
    result.plannedWorkers = slots;
    return result;
}

} // namespace an
