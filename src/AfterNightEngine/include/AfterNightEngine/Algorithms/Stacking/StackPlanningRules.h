// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Checked stack scratch and bookkeeping layouts, shared by planning and execution.
///
/// The planner counts bytes and the executor cuts buffers, and a stack is only safe when both do
/// it by the same arithmetic. This header is that arithmetic, as constant expressions over a
/// checked model: which strategy a model's samples combine under and why, how many candidates the
/// deviate test may hold, how many nodes a local-weight lattice has, what one worker's scratch slice
/// holds - the column, the estimators' working copies, the frame records and the source path's row
/// scratch - and where the executor's per-frame records and the measurement passes' regions sit.
/// `planStack` sizes the working set from these figures, `StackWorkingSet` hands out buffers of
/// these sizes, and the executor and the kernels address them by these offsets, so a record that
/// outgrows its reservation fails to compile where the implementation proves that it fits.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>

namespace an
{

/// Where a model's samples are while they combine.
///
/// Sample history for every rejection, for the median and for the auto-adaptive mean; whole-output
/// accumulators for everything else.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @return the strategy.
[[nodiscard]] constexpr StackAccumulationStrategy stackAccumulationStrategyOf(const StackModel& model) noexcept
{
    const StackRequest& request = model.request();
    if (request.rejection.method != StackRejection::None || request.combination == StackCombination::Median ||
        request.combination == StackCombination::AutoAdaptiveWeightedAverage)
    {
        return StackAccumulationStrategy::TileSampleHistory;
    }
    return StackAccumulationStrategy::WholeOutputAccumulators;
}

/// Why a model's samples are where `stackAccumulationStrategyOf` puts them.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @return the reason.
[[nodiscard]] constexpr StackStrategyReason stackStrategyReasonOf(const StackModel& model) noexcept
{
    const StackRequest& request = model.request();
    if (request.rejection.method != StackRejection::None)
    {
        return StackStrategyReason::RejectionNeedsSampleHistory;
    }
    if (request.combination == StackCombination::Median)
    {
        return StackStrategyReason::MedianNeedsSampleHistory;
    }
    if (request.combination == StackCombination::AutoAdaptiveWeightedAverage)
    {
        return StackStrategyReason::AutoAdaptiveNeedsSampleHistory;
    }
    return StackStrategyReason::StreamingCombination;
}

/// The most samples the generalized extreme Studentized deviate test may reject from one pixel.
///
/// The frame count times the outlier fraction, rounded up, and at least one so the test can reject
/// something and at most every frame.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param frames how many samples a pixel has.
/// @param fraction the stated `GesdtParameters::maximumOutlierFraction`.
/// @return the candidate count, between one and `frames`.
[[nodiscard]] constexpr std::uint64_t stackGesdtMaximumOutliers(std::uint32_t frames, double fraction) noexcept
{
    const double raw = static_cast<double>(frames) * fraction;
    std::uint64_t whole = raw > 0.0 ? static_cast<std::uint64_t>(raw) : 0u;
    if (static_cast<double>(whole) < raw)
    {
        ++whole;
    }
    if (whole < 1u)
    {
        whole = 1u;
    }
    if (whole > frames)
    {
        whole = frames;
    }
    return whole;
}

/// How many local-weight grid nodes lie along one axis of a frame.
///
/// Nodes sit at the first pixel and then every `spacing` pixels, with one more at the far edge
/// when the extent does not land on a node.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param extent the axis length in pixels.
/// @param spacing the grid spacing in pixels; zero is read as the whole axis.
/// @return the node count, at least one.
[[nodiscard]] constexpr std::uint64_t localWeightGridNodesAlong(std::uint32_t extent, std::uint32_t spacing) noexcept
{
    if (extent <= 1u || spacing == 0u)
    {
        return 1u;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(extent) - 1u;
    return (span + spacing - 1u) / spacing + 1u;
}

/// One per-worker scratch term, rounded up to `kStackScratchAlignmentBytes`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param bytes the term's exact size.
/// @return the term's aligned size.
[[nodiscard]] constexpr std::uint64_t alignedStackScratchTerm(std::uint64_t bytes) noexcept
{
    return alignUp(CheckedSize{bytes}, kStackScratchAlignmentBytes).valueOr(0u);
}

/// What one worker holds while reading one row of a band through the local-normalization surface.
///
/// The source path mixes each grid's contributing node rows into per-column values and bounds
/// once per row, and prepares every column's cell fraction and every cell's first column once per
/// band, in the worker's own scratch rather than in an allocation of its own: the
/// `kStackSourceRowArrays` mixed arrays at `kStackSourceGridCeiling + 1` doubles each, one double
/// per column of the frame, and `kStackSourceGridCeiling + 2` thirty-two-bit cell starts. A stack
/// whose normalization scope applies no local surface reads no grid and reports zero.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @return the bytes one worker needs, a multiple of `kStackScratchAlignmentBytes`.
[[nodiscard]] constexpr std::uint64_t stackSourceScratchBytesPerWorker(const StackModel& model) noexcept
{
    const StackNormalizationScope scope = model.request().normalization.scope;
    if (scope != StackNormalizationScope::Local && scope != StackNormalizationScope::LocalThenGlobal)
    {
        return 0u;
    }
    const std::uint64_t nodes = static_cast<std::uint64_t>(kStackSourceGridCeiling) + 1u;
    const std::uint64_t width = model.request().input.descriptor.extent().width();
    return alignedStackScratchTerm(kStackSourceRowArrays * nodes * sizeof(double) + width * sizeof(double) +
                                   (nodes + 1u) * sizeof(std::uint32_t));
}

/// What one worker holds while combining one pixel's column of samples, and while reading a band.
///
/// The contract the rejection estimators, the combination kernels and the source path are written
/// against: an implementation that needs more than this states so here, not in an allocation of
/// its own. Every term is one array over the frames, aligned separately, so a worker's buffers
/// each begin on a vector boundary: the column of samples at the accumulation width; under a
/// rejection, one checked row record and one flag per sample and one working copy, plus
/// two vectors for the linear fit, and the
/// deviations, index tables and candidate records of the generalized deviate test; a selection
/// workspace of `kStackSampleColumnLanes` columns for the median, which a compiled row sorts one
/// pixel per lane; the samples and the weights of `kStackSampleColumnLanes` columns for the
/// auto-adaptive mean; and one effective weight per sample when the stack weighs its frames or
/// applies a local surface. A median or auto-adaptive worker also holds one checked frame record
/// per frame, so no unaccounted heap allocation is needed to describe its columns. A streaming
/// strategy holds no column. Last in every slice, whatever the strategy, comes
/// `stackSourceScratchBytesPerWorker`, the row scratch of the local-normalization surface the
/// source path reads a band through.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @return the bytes one worker needs, a multiple of `kStackScratchAlignmentBytes`.
[[nodiscard]] constexpr std::uint64_t stackScratchBytesPerWorker(const StackModel& model) noexcept
{
    if (stackAccumulationStrategyOf(model) == StackAccumulationStrategy::WholeOutputAccumulators)
    {
        return stackSourceScratchBytesPerWorker(model);
    }
    const StackRequest& request = model.request();
    const std::uint64_t frames = request.input.frameCount;
    const std::uint64_t sampleBytes = sizeof(double);
    std::uint64_t bytes = alignedStackScratchTerm(frames * sampleBytes);
    if (model.usesRejection())
    {
        bytes += alignedStackScratchTerm(frames * kStackRejectionRowRecordBytes);
        bytes += alignedStackScratchTerm(frames);
        bytes += alignedStackScratchTerm(frames * sampleBytes);
        if (request.rejection.method == StackRejection::LinearFitClipping)
        {
            bytes += alignedStackScratchTerm(2u * frames * sampleBytes);
        }
        if (request.rejection.method == StackRejection::GeneralizedEsdTest)
        {
            bytes += alignedStackScratchTerm(frames * sampleBytes);
            bytes += alignedStackScratchTerm(2u * frames * sizeof(std::uint32_t));
            bytes += alignedStackScratchTerm(
                stackGesdtMaximumOutliers(request.input.frameCount, request.rejection.gesdt.maximumOutlierFraction) *
                kStackGesdtCandidateBytes);
        }
    }
    else if (request.combination == StackCombination::Median)
    {
        bytes += alignedStackScratchTerm(kStackSampleColumnLanes * frames * sampleBytes);
    }
    else if (request.combination == StackCombination::AutoAdaptiveWeightedAverage)
    {
        bytes += alignedStackScratchTerm(2u * kStackSampleColumnLanes * frames * sampleBytes);
    }
    if (model.usesFrameWeights() || model.usesLocalWeights())
    {
        bytes += alignedStackScratchTerm(frames * sampleBytes);
    }
    if (!model.usesRejection())
    {
        bytes += alignedStackScratchTerm(frames * kStackColumnFrameRecordBytes);
    }
    return bytes + stackSourceScratchBytesPerWorker(model);
}

/// What one row of the tile holds of the local weight surface.
///
/// The executor reads each frame's weight band beside its sample band, so a tile that holds
/// sample history, or every frame's band under region access, holds one Float32 weight per pixel
/// per frame, and a whole-frame streaming tile holds one frame's. A stack that applies no
/// surface holds none.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param access how the source is read, which decides whether a streaming tile holds every
///        frame's band.
/// @return the bytes one row of weight bands costs, or zero.
[[nodiscard]] constexpr std::uint64_t stackLocalWeightTileBytesPerRow(const StackModel& model,
                                                                      StackSourceAccess access) noexcept
{
    if (!model.usesLocalWeights())
    {
        return 0u;
    }
    const StackRequest& request = model.request();
    const bool everyFrame = stackAccumulationStrategyOf(model) == StackAccumulationStrategy::TileSampleHistory ||
                            isStackSourceRegionAccess(access);
    const std::uint64_t bands = everyFrame ? request.input.frameCount : 1u;
    return static_cast<std::uint64_t>(request.input.descriptor.extent().width()) * bands * sizeof(float);
}

/// The local weight surface's bytes per tile row under whole-frame access.
///
/// @param model the checked stack.
/// @return `stackLocalWeightTileBytesPerRow(model, StackSourceAccess::WholeFrame)`.
[[nodiscard]] constexpr std::uint64_t stackLocalWeightTileBytesPerRow(const StackModel& model) noexcept
{
    return stackLocalWeightTileBytesPerRow(model, StackSourceAccess::WholeFrame);
}

/// Where each of the executor's per-frame records sits inside the frame-state block, after the
/// weight and coefficient tables.
///
/// Every offset is relative to the start of the bookkeeping region and a multiple of
/// `kStackFrameCounterBytes`; a region a stack does not keep has zero bytes and an offset equal
/// to the one after it. The sample counters are two per frame, the admitted samples of every
/// frame first and then the rejected; the coverage histogram has one bin for every sample count
/// from zero to the frame count; the worker slices are one `kStackWorkerSliceRecordBytes` record
/// per worker; the worker counters are one row of two counts per frame for every worker, kept
/// under a rejection; the band records are one `kStackBandRecordBytes` record per frame, kept
/// under every strategy, including stable mask views beside the bands; the rate records are one
/// `kStackFrameRateRecordBytes` record per frame and the flagged frames one thirty-two-bit index
/// per frame, both kept under a rejection.
struct StackFrameBookkeepingLayout
{
    /// Where the two sample counters per frame begin.
    std::uint64_t sampleCountersOffset = 0u;

    /// Where the coverage histogram begins.
    std::uint64_t coverageHistogramOffset = 0u;

    /// Where the worker scratch-slice records begin.
    std::uint64_t workerSlicesOffset = 0u;

    /// How many bytes the worker scratch-slice records hold: one record per worker.
    std::uint64_t workerSlicesBytes = 0u;

    /// Where the per-worker counter rows begin.
    std::uint64_t workerCountersOffset = 0u;

    /// How many bytes the per-worker counter rows hold; zero without a rejection.
    std::uint64_t workerCountersBytes = 0u;

    /// Where the band records begin.
    std::uint64_t bandRecordsOffset = 0u;

    /// How many bytes the band and stable mask-view records hold, under every strategy.
    std::uint64_t bandRecordsBytes = 0u;

    /// Where the rate records begin.
    std::uint64_t rateRecordsOffset = 0u;

    /// How many bytes the rate records hold; zero without a rejection.
    std::uint64_t rateRecordsBytes = 0u;

    /// Where the flagged frame indices begin.
    std::uint64_t flaggedFramesOffset = 0u;

    /// How many bytes the flagged frame indices hold, rounded up to a whole counter; zero
    /// without a rejection.
    std::uint64_t flaggedFramesBytes = 0u;

    /// The whole region.
    std::uint64_t totalBytes = 0u;

    /// Whether two layouts place every record the same way.
    ///
    /// @param left one layout.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackFrameBookkeepingLayout& left,
                                                   const StackFrameBookkeepingLayout& right) noexcept = default;
};

/// Lays out the executor's per-frame bookkeeping for a stack on a stated number of workers.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param workers how many workers the combine runs on, at least one.
/// @return the layout.
[[nodiscard]] constexpr StackFrameBookkeepingLayout stackFrameBookkeepingLayoutOf(const StackModel& model,
                                                                                  std::uint32_t workers) noexcept
{
    const std::uint64_t frames = model.request().input.frameCount;
    StackFrameBookkeepingLayout layout;
    layout.sampleCountersOffset = 0u;
    layout.coverageHistogramOffset = layout.sampleCountersOffset + 2u * frames * kStackFrameCounterBytes;
    layout.workerSlicesOffset = layout.coverageHistogramOffset + (frames + 1u) * kStackFrameCounterBytes;
    layout.workerSlicesBytes = static_cast<std::uint64_t>(workers) * kStackWorkerSliceRecordBytes;
    layout.workerCountersOffset = layout.workerSlicesOffset + layout.workerSlicesBytes;
    layout.workerCountersBytes =
        model.usesRejection() ? static_cast<std::uint64_t>(workers) * frames * 2u * kStackFrameCounterBytes : 0u;
    layout.bandRecordsOffset = layout.workerCountersOffset + layout.workerCountersBytes;
    // One band record per frame under every strategy: a sample-history tile and a region-access
    // streaming tile both hand every frame's band of a tile to one kernel call.
    layout.bandRecordsBytes = frames * kStackBandRecordBytes;
    layout.rateRecordsOffset = layout.bandRecordsOffset + layout.bandRecordsBytes;
    layout.rateRecordsBytes = model.usesRejection() ? frames * kStackFrameRateRecordBytes : 0u;
    layout.flaggedFramesOffset = layout.rateRecordsOffset + layout.rateRecordsBytes;
    layout.flaggedFramesBytes =
        model.usesRejection()
            ? ((frames * sizeof(std::uint32_t) + kStackFrameCounterBytes - 1u) / kStackFrameCounterBytes) *
                  kStackFrameCounterBytes
            : 0u;
    layout.totalBytes = layout.flaggedFramesOffset + layout.flaggedFramesBytes;
    return layout;
}

/// The bytes the frame-state block reserves for the executor's per-frame bookkeeping.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param workers how many workers the combine runs on, at least one.
/// @return `stackFrameBookkeepingLayoutOf(model, workers).totalBytes`.
[[nodiscard]] constexpr std::uint64_t stackFrameBookkeepingBytes(const StackModel& model,
                                                                 std::uint32_t workers) noexcept
{
    return stackFrameBookkeepingLayoutOf(model, workers).totalBytes;
}

/// Where the two measurement passes keep what they hold beside one decoded frame, inside the
/// plan's measurement workspace.
///
/// Both passes share one buffer, `StackWorkingSet::measurementWorkspace`, sized for the larger.
/// The normalization measurement holds the reference frame at the tile width for the whole pass
/// under a local scope, because the fit reads it beside every other frame, and then the widened
/// frame when the storage is not the tile width. The local-weight measurement holds its integral
/// images first, then the widened frame, and then - under a local scope - one scratch slice per
/// worker, because the source path reads a band through the local surface on the way to the
/// lattice and that surface's row scratch lives in the worker's slice. A frame follows the region
/// before it directly, because a frame at the tile width and a whole number of doubles are both
/// multiples of the sample's alignment; the scratch, which vector rows read, is rounded up to
/// `kStackScratchAlignmentBytes`.
struct StackMeasurementWorkspaceLayout
{
    /// Where the reference frame at the tile width sits during the normalization measurement.
    std::uint64_t normalizationReferenceOffset = 0u;

    /// Where the widened frame sits during the normalization measurement.
    std::uint64_t normalizationFrameOffset = 0u;

    /// The normalization measurement's whole workspace.
    std::uint64_t normalizationBytes = 0u;

    /// Where the integral images sit during the local-weight measurement.
    std::uint64_t localWeightIntegralOffset = 0u;

    /// Where the widened frame sits during the local-weight measurement.
    std::uint64_t localWeightFrameOffset = 0u;

    /// Where the first worker's scratch slice sits during the local-weight measurement.
    std::uint64_t localWeightScratchOffset = 0u;

    /// The local-weight measurement's whole workspace.
    std::uint64_t localWeightBytes = 0u;

    /// Whether two layouts place every region the same way.
    ///
    /// @param left one layout.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackMeasurementWorkspaceLayout& left,
                                                   const StackMeasurementWorkspaceLayout& right) noexcept = default;
};

/// Lays out the measurement passes' workspaces for a stack on a stated number of workers.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param workers how many workers the passes run on, at least one.
/// @param localWeightWorkspaceBytes what the local-weight lattice's measurement states it needs
///        beside the frame, which `Algorithms/Stacking/LocalWeights.h` computes from the
///        model's lattice; zero when the stack applies no local weighting.
/// @return the layout, whose byte figures are zero for a pass the stack does not run.
[[nodiscard]] constexpr StackMeasurementWorkspaceLayout
stackMeasurementWorkspaceLayoutOf(const StackModel& model, std::uint32_t workers,
                                  std::uint64_t localWeightWorkspaceBytes) noexcept
{
    const StackRequest& request = model.request();
    const ImageExtent& extent = request.input.descriptor.extent();
    const SampleType inputType = request.input.descriptor.sampleType();
    const std::uint64_t tileFrameBytes = static_cast<std::uint64_t>(extent.width()) * extent.height() *
                                         extent.channelCount() * sampleTypeSizeBytes(stackTileSampleTypeOf(inputType));
    const bool widens = inputType != stackTileSampleTypeOf(inputType);
    const StackNormalizationScope scope = request.normalization.scope;
    const bool localScope =
        scope == StackNormalizationScope::Local || scope == StackNormalizationScope::LocalThenGlobal;
    StackMeasurementWorkspaceLayout layout;
    layout.normalizationReferenceOffset = 0u;
    layout.normalizationFrameOffset = localScope ? tileFrameBytes : 0u;
    layout.normalizationBytes = layout.normalizationFrameOffset + (widens ? tileFrameBytes : 0u);
    layout.localWeightIntegralOffset = 0u;
    layout.localWeightFrameOffset = localWeightWorkspaceBytes;
    const std::uint64_t frameEnd = layout.localWeightFrameOffset + (widens ? tileFrameBytes : 0u);
    layout.localWeightScratchOffset = localScope ? alignedStackScratchTerm(frameEnd) : frameEnd;
    layout.localWeightBytes =
        layout.localWeightScratchOffset +
        (localScope ? static_cast<std::uint64_t>(workers) * stackScratchBytesPerWorker(model) : 0u);
    return layout;
}

} // namespace an
