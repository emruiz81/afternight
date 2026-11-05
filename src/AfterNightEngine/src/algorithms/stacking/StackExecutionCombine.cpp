// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack executor's combine stage, under sample history and under
// whole-output accumulators.
//
// A sample-history stack reads every frame's band of one tile into the tile, then runs the column
// kernels or the rejection estimators over it, writing the output rows directly. A streaming stack
// folds each band into the accumulator terms: one frame at a time, tile by tile, under whole-frame
// access, and every frame's band of a tile at once under region access, finalizing each tile as it
// is combined when the terms hold one tile. Every tile covers output rows no other tile covers, so
// no element is written twice.
//
// When the caller takes the output tile by tile, the working set holds one tile's output and each
// tile is written from its start: the combinations and the window finalization are addressed to the
// tile's rows, and the tile goes to the caller's sink as soon as it is finished, before the next
// tile is read and overwrites it.
#include "algorithms/stacking/StackExecutionState.h"

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejection.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "runtime/ProgressRange.h"
#include "algorithms/stacking/StackAccumulatorWindow.h"
#include "algorithms/stacking/StackHistory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <array>
#include <optional>
#include <span>
#include <utility>

namespace an::stacking
{

/// The targets every combination writes: the working set's image, mask and maps.
///
/// @param run the run.
/// @param maskView the mask view, kept alive by the caller.
/// @return the targets.
[[nodiscard]] StackFinalizeTargets targetsOf(StackExecutionState& run, const ValidityMaskView<std::uint8_t>* maskView)
{
    return StackFinalizeTargets{.outputSampleType = run.model->request().output.sampleType,
                                .outputImage = run.set.outputImage(),
                                .outputMask = maskView,
                                .coverage = run.set.outputCountMap(StackOutputMap::Coverage),
                                .variance = run.set.outputValueMap(StackOutputMap::Variance),
                                .signalToNoiseGain = run.set.outputValueMap(StackOutputMap::SignalToNoiseGain),
                                .weightSum = run.set.outputValueMap(StackOutputMap::WeightSum)};
}

/// The targets one tile writes when the caller takes the output tile by tile.
///
/// @param run the run.
/// @param rows the tile's rows.
/// @param maskView receives the mask's view over those rows.
/// @return the targets, each the first of the working set's buffer that the tile's rows fill.
[[nodiscard]] StackFinalizeTargets tileTargetsOf(StackExecutionState& run, std::uint32_t rows,
                                                 std::optional<ValidityMaskView<std::uint8_t>>& maskView)
{
    const std::size_t elements = static_cast<std::size_t>(static_cast<std::uint64_t>(run.width) * rows * run.channels);
    const auto first = [elements]<typename T>(std::span<T> held) { return held.empty() ? held : held.first(elements); };
    maskView.reset();
    if (const std::optional<ValidityMaskView<std::uint8_t>> held = run.set.outputMask(); held.has_value())
    {
        const std::optional<ImageExtent> extent =
            ImageExtent::create(run.width, rows, run.channels, run.engine->limits());
        const std::uint64_t bytes = packedMaskRowBytes(run.width) * rows * run.channels;
        if (extent.has_value())
        {
            maskView = ValidityMaskView<std::uint8_t>::createTight(
                std::span<std::uint8_t>(held->data(), static_cast<std::size_t>(bytes)), *extent);
        }
    }
    const SampleType outputType = run.model->request().output.sampleType;
    return StackFinalizeTargets{.outputSampleType = outputType,
                                .outputImage = run.set.outputImage().first(
                                    elements * static_cast<std::size_t>(sampleTypeSizeBytes(outputType))),
                                .outputMask = maskView.has_value() ? &*maskView : nullptr,
                                .coverage = first(run.set.outputCountMap(StackOutputMap::Coverage)),
                                .variance = first(run.set.outputValueMap(StackOutputMap::Variance)),
                                .signalToNoiseGain = first(run.set.outputValueMap(StackOutputMap::SignalToNoiseGain)),
                                .weightSum = first(run.set.outputValueMap(StackOutputMap::WeightSum))};
}

/// Hands one finished tile to the request's sink, its coverage and gain folded first.
///
/// @param run the run.
/// @param targets what the tile wrote.
/// @param firstRow the tile's first row.
/// @param rows how many rows the tile holds.
/// @return success, a fold's cancellation, or the sink's failure.
[[nodiscard]] Status handOverTile(StackExecutionState& run, const StackFinalizeTargets& targets, std::uint32_t firstRow,
                                  std::uint32_t rows)
{
    if (run.history && !targets.coverage.empty())
    {
        const Status folded = foldCoverage(run, targets.coverage, run.coverageTally);
        if (folded.failed())
        {
            return folded;
        }
    }
    if (!targets.signalToNoiseGain.empty())
    {
        const Status folded = foldTileGain(run, targets.signalToNoiseGain);
        if (folded.failed())
        {
            return folded;
        }
    }
    const std::optional<ImageExtent> extent = ImageExtent::create(run.width, rows, run.channels, run.engine->limits());
    const std::optional<ImageLayout> layout =
        extent.has_value() ? ImageLayout::create({.extent = *extent,
                                                  .elementSizeBytes = sampleTypeSizeBytes(targets.outputSampleType),
                                                  .samplesPerPixel = 1u,
                                                  .planeCount = run.channels,
                                                  .rowAlignmentBytes = 1u},
                                                 run.engine->limits())
                           : std::nullopt;
    if (!layout.has_value())
    {
        return failure(ErrorCode::Internal, kExecute, "a tile of the output has no layout under the engine's limits");
    }
    std::optional<ValidityMaskView<const std::uint8_t>> mask{};
    if (targets.outputMask != nullptr)
    {
        mask = *targets.outputMask;
    }
    StackOutputRows tile{.firstRow = firstRow,
                         .rows = rows,
                         .image = targets.outputImage,
                         .layout = &*layout,
                         .mask = mask.has_value() ? &*mask : nullptr,
                         .maps = {}};
    const std::size_t elements = static_cast<std::size_t>(static_cast<std::uint64_t>(run.width) * rows * run.channels);
    for (std::size_t index = 0u; index < kStackOutputMapCount; ++index)
    {
        const auto map = static_cast<StackOutputMap>(index);
        if (!stackOutputMapRequested(run.model->request().output, map))
        {
            continue;
        }
        const std::span<const std::byte> whole =
            stackOutputMapSampleType(map) == SampleType::UInt32
                ? std::as_bytes(std::span<const std::uint32_t>(run.set.outputCountMap(map)))
                : std::as_bytes(std::span<const float>(run.set.outputValueMap(map)));
        tile.maps[index] = whole.first(elements * kStackMapSampleBytes);
    }
    constexpr ExternalBoundary kBoundary{
        .library = "StackOutputSink", .operation = kExecute, .domain = ErrorDomain::Algorithms};
    Status written;
    const Status crossed =
        callAcrossBoundary(kBoundary, [&] { written = run.request->outputSink->write(*run.execution, tile); });
    return crossed.failed() ? crossed : written;
}

/// The whole-output terms of a streaming stack.
///
/// @param run the run.
/// @return the set.
[[nodiscard]] StackAccumulatorSet accumulatorsOf(StackExecutionState& run)
{
    return StackAccumulatorSet{.sums = run.set.accumulatorSums(),
                               .counts = run.set.accumulatorCounts(),
                               .weightSums = run.set.accumulatorWeightSums(),
                               .weightSquareSums = run.set.accumulatorWeightSquareSums(),
                               .varianceMoments = run.set.accumulatorVarianceMoments()};
}

/// Fills one frame's weight band for a run of rows from its finished grid.
///
/// @param run the run.
/// @param frame which frame.
/// @param firstRow the band's first row.
/// @param rows how many rows.
/// @param weights where the band goes.
/// @return success or the fill's refusal.
[[nodiscard]] Status fillWeights(StackExecutionState& run, std::uint32_t frame, std::uint32_t firstRow,
                                 std::uint32_t rows, std::span<float> weights)
{
    const std::size_t perFrame = static_cast<std::size_t>(run.localWeightShape->valuesPerFrame());
    return fillLocalWeightBand(*run.localWeightShape, run.set.localWeightGrids().subspan(frame * perFrame, perFrame),
                               firstRow, rows, weights);
}

/// Reads one frame's rows for one tile and records the band the column kernels will read.
/// An unchanged resident rejection region is borrowed; other inputs are converted into the tile.
///
/// @param run the run.
/// @param tile the tile.
/// @param bandLayout the layout of one frame's band of this tile.
/// @param bandBytes the bytes of one frame's band of this tile.
/// @param weightsOffset where the weight bands begin in the tile.
/// @param weightBandBytes the bytes of one frame's weight band of this tile.
/// @param frame which frame.
/// @param firstRow the tile's first row.
/// @param rows how many rows the tile holds.
/// @return success or the first refusal.
[[nodiscard]] Status readHistoryBand(StackExecutionState& run, std::span<std::byte> tile, const ImageLayout& bandLayout,
                                     std::uint64_t bandBytes, std::uint64_t weightsOffset,
                                     std::uint64_t weightBandBytes, std::uint32_t frame, std::uint32_t firstRow,
                                     std::uint32_t rows)
{
    const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u, run.progressTotal);
    const bool region = isStackSourceRegionAccess(run.plan.io.access);
    StackSourceBand stored{};
    StackSourceRows window{};
    bool inSlot = false;
    if (region)
    {
        const Status read = readFrame(run, StackPassKind::Combine, frame, firstRow, rows, stored, inSlot);
        if (read.failed())
        {
            return read;
        }
    }
    else
    {
        const Status read = readFrame(run, StackPassKind::Combine, frame, 0u, run.height, stored, inSlot);
        if (read.failed())
        {
            return read;
        }
        Result<StackSourceRows> cut = stackSourceRowsOf(stored, firstRow, rows);
        if (!cut.hasValue())
        {
            return cut.status();
        }
        window = std::move(*cut);
        stored = window.band();
    }
    const std::span<std::byte> destination =
        tile.subspan(static_cast<std::size_t>(frame * bandBytes), static_cast<std::size_t>(bandBytes));
    // A resident region remains valid until this frame is read again, after the tile is
    // combined. Rejection already checks and counts every admitted sample, so a no-op source
    // pass would only copy the same floating samples and schedule another set of row tasks.
    const bool direct = region && !inSlot && !run.stages.any() && !run.widens && run.model->usesRejection();
    const void* samples = stored.samples;
    const ImageLayout* layout = stored.layout;
    ValidityDescriptor validity = stored.validity.descriptor;
    if (!direct)
    {
        const StackTileBand tileBand{.sampleType = run.tileType, .samples = destination.data(), .layout = &bandLayout};
        const Result<StackSourceOutcome> outcome =
            convert(run, *run.model, run.contract, stored, tileBand, run.workerSlices);
        if (!outcome.hasValue())
            return outcome.status();
        if (!run.model->usesRejection())
            run.admittedCounts[frame] += outcome->samplesTransformed - outcome->nonFiniteOutputs;
        samples = destination.data();
        layout = &bandLayout;
        validity = outcome->validity;
    }
    std::span<const float> weights{};
    if (run.model->usesLocalWeights())
    {
        const std::span<std::byte> weightBytes =
            tile.subspan(static_cast<std::size_t>(weightsOffset + frame * weightBandBytes),
                         static_cast<std::size_t>(weightBandBytes));
        const std::span<float> writable(reinterpret_cast<float*>(weightBytes.data()),
                                        static_cast<std::size_t>(rows) * run.width);
        const Status filled = fillWeights(run, frame, firstRow, rows, writable);
        if (filled.failed())
        {
            return filled;
        }
        weights = writable;
    }
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;
    if (validity.rule() == ValidityRule::ExplicitMask)
    {
        // A whole-frame window owns its view object only until this read returns. The bytes
        // remain borrowed from the source; keep the view in the planned per-frame bookkeeping.
        mask = std::construct_at(run.maskRecords + frame, *stored.validity.mask);
    }
    std::construct_at(run.bandRecords + frame,
                      StackFrameBand{.sampleType = run.tileType,
                                     .samples = samples,
                                     .layout = layout,
                                     .frameIndex = frame,
                                     .firstOutputRow = firstRow,
                                     .validity = StackBandValidity{.descriptor = validity, .mask = mask},
                                     .localWeights = weights});
    return Status::success();
}

/// The combine pass under sample history: every frame's band into the tile, then the column
/// kernels or the estimators over it.
///
/// @param run the run.
/// @param targets where the output goes.
/// @return success or the first refusal.
[[nodiscard]] Status combineHistory(StackExecutionState& run, const StackFinalizeTargets& targets)
{
    const std::span<std::byte> tile = run.set.tile();
    const std::span<double> frameWeights = run.model->usesFrameWeights() ? run.set.frameWeights() : std::span<double>{};
    const StackRejectionMaps maps{.low = run.set.outputCountMap(StackOutputMap::RejectionLow),
                                  .high = run.set.outputCountMap(StackOutputMap::RejectionHigh),
                                  .merged = run.set.outputCountMap(StackOutputMap::RejectionMerged)};
    const StackRejectionFrameCounters counters{
        .admitted = run.admittedCounts, .rejected = run.rejectedCounts, .workerRows = run.workerCounters};
    if (run.outputByTile)
    {
        // Each tile's coverage is folded as it leaves, so the tallies start from nothing.
        std::fill(run.histogram.begin(), run.histogram.end(), 0u);
        run.coverageTally = StackCoverageTally{};
    }
    for (std::uint32_t tileIndex = 0u; tileIndex < run.plan.tile.tileCount; ++tileIndex)
    {
        const std::uint32_t firstRow = tileIndex * run.plan.tile.rows;
        const std::uint32_t rows = (std::min)(run.plan.tile.rows, run.height - firstRow);
        const Result<ImageLayout> bandLayout = stackTileBandLayout(*run.model, rows);
        if (!bandLayout.hasValue())
        {
            return bandLayout.status();
        }
        const std::uint64_t bandBytes = stackTileFrameBytes(*run.model, rows);
        const std::uint64_t weightBandBytes = static_cast<std::uint64_t>(rows) * run.width * sizeof(float);
        const std::uint64_t weightsOffset = bandBytes * run.frames;
        for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
        {
            const Status read = readHistoryBand(run, tile, *bandLayout, bandBytes, weightsOffset, weightBandBytes,
                                                frame, firstRow, rows);
            if (read.failed())
            {
                return read;
            }
            completeUnit(run, StackPassKind::Combine);
        }
        const std::span<const StackFrameBand> bands(run.bandRecords, run.frames);
        // A tile the caller takes writes the tile's rows of the working set's output, from its start.
        std::optional<ValidityMaskView<std::uint8_t>> tileMask{};
        const StackFinalizeTargets tileTargets = run.outputByTile ? tileTargetsOf(run, rows, tileMask) : targets;
        const std::size_t tileElements =
            static_cast<std::size_t>(static_cast<std::uint64_t>(run.width) * rows * run.channels);
        const auto first = [&](std::span<std::uint32_t> map) {
            return run.outputByTile && !map.empty() ? map.first(tileElements) : map;
        };
        const StackRejectionMaps tileMaps{
            .low = first(maps.low), .high = first(maps.high), .merged = first(maps.merged)};
        const StackAccumulatorWindow window{.firstRow = firstRow, .rows = rows};
        if (run.model->usesRejection())
        {
            const Result<StackRejectionOutcome> outcome =
                run.outputByTile
                    ? combineRejectedStackSampleHistoryWindow(*run.execution, *run.model, bands, run.workerSlices,
                                                              tileTargets, frameWeights, tileMaps,
                                                              run.request->rejectionPolicy, counters, window)
                    : combineRejectedStackSampleHistory(*run.execution, *run.model, bands, run.workerSlices, targets,
                                                        frameWeights, maps, run.request->rejectionPolicy, counters);
            if (!outcome.hasValue())
            {
                return outcome.status();
            }
            run.report.samplesAdmitted += outcome->history.samplesAdmitted;
            run.report.samplesExcludedByValidity += outcome->history.samplesExcludedByValidity;
            run.report.samplesExcludedByWeight += outcome->samplesExcludedByWeight;
            run.report.elementsWithSamples += outcome->history.elementsWithSamples;
            run.report.elementsWithoutSamples += outcome->history.elementsWithoutSamples;
            run.report.rejectedLow += outcome->rejectedLow;
            run.report.rejectedHigh += outcome->rejectedHigh;
            run.report.mappedLow += outcome->mappedLow;
            run.report.mappedHigh += outcome->mappedHigh;
            run.report.compiledRowUsed = run.report.compiledRowUsed || outcome->history.compiledRowUsed;
        }
        else
        {
            const Result<StackSampleHistoryOutcome> outcome =
                run.outputByTile
                    ? combineStackSampleHistoryWindow(*run.execution, *run.model, bands, run.workerSlices, tileTargets,
                                                      window)
                    : combineStackSampleHistory(*run.execution, *run.model, bands, run.workerSlices, targets);
            if (!outcome.hasValue())
            {
                return outcome.status();
            }
            run.report.samplesAdmitted += outcome->samplesAdmitted;
            run.report.samplesExcludedByValidity += outcome->samplesExcludedByValidity;
            run.report.elementsWithSamples += outcome->elementsWithSamples;
            run.report.elementsWithoutSamples += outcome->elementsWithoutSamples;
            run.report.compiledRowUsed = run.report.compiledRowUsed || outcome->compiledRowUsed;
        }
        ++run.report.tilesCombined;
        if (run.outputByTile)
        {
            const Status handed = handOverTile(run, tileTargets, firstRow, rows);
            if (handed.failed())
            {
                return handed;
            }
        }
    }
    return Status::success();
}

/// Accumulates one stored band of one frame into the whole-output terms, converting it first when
/// a stage applies.
///
/// @param run the run.
/// @param set the terms.
/// @param stored the band as stored.
/// @param inSlot whether it lives in the executor's read slot.
/// @param firstRow the band's first row.
/// @param rows how many rows.
/// @return success or the first refusal.
[[nodiscard]] Status accumulateBand(StackExecutionState& run, const StackAccumulatorSet& set,
                                    const StackSourceBand& stored, bool inSlot, std::uint32_t firstRow,
                                    std::uint32_t rows)
{
    const std::span<std::byte> tile = run.set.tile();
    const std::uint64_t bandBytes = stackTileFrameBytes(*run.model, rows);
    const std::uint64_t weightBandBytes = static_cast<std::uint64_t>(rows) * run.width * sizeof(float);
    StackFrameBand band{.sampleType = stored.sampleType,
                        .samples = stored.samples,
                        .layout = stored.layout,
                        .frameIndex = stored.frameIndex,
                        .firstOutputRow = firstRow,
                        .validity = stored.validity,
                        .localWeights = {}};
    std::optional<ImageLayout> bandLayout{};
    if (run.stages.any() && !run.storedSamples)
    {
        Result<ImageLayout> made = stackTileBandLayout(*run.model, rows);
        if (!made.hasValue())
        {
            return made.status();
        }
        bandLayout = *made;
        StackTileBand tileBand{.sampleType = run.tileType, .samples = nullptr, .layout = nullptr};
        if (run.widens)
        {
            tileBand.samples = tile.subspan(0u, static_cast<std::size_t>(bandBytes)).data();
            tileBand.layout = &*bandLayout;
        }
        else if (inSlot)
        {
            tileBand.samples = const_cast<void*>(stored.samples);
            tileBand.layout = stored.layout;
        }
        else
        {
            tileBand.samples = readSlot(run, rows).data();
            tileBand.layout = &*bandLayout;
        }
        const Result<StackSourceOutcome> outcome =
            convert(run, *run.model, run.contract, stored, tileBand, run.workerSlices);
        if (!outcome.hasValue())
        {
            return outcome.status();
        }
        band.sampleType = run.tileType;
        band.samples = tileBand.samples;
        band.layout = tileBand.layout;
        band.validity = StackBandValidity{
            .descriptor = outcome->validity,
            .mask = outcome->validity.rule() == ValidityRule::ExplicitMask ? stored.validity.mask : nullptr};
    }
    if (run.model->usesLocalWeights())
    {
        const std::uint64_t offset = run.widens ? bandBytes : 0u;
        const std::span<std::byte> weightBytes =
            tile.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(weightBandBytes));
        const std::span<float> writable(reinterpret_cast<float*>(weightBytes.data()),
                                        static_cast<std::size_t>(rows) * run.width);
        const Status filled = fillWeights(run, stored.frameIndex, firstRow, rows, writable);
        if (filled.failed())
        {
            return filled;
        }
        band.localWeights = writable;
    }
    const std::span<const double> frameWeights =
        run.model->usesFrameWeights() ? run.set.frameWeights() : std::span<const double>{};
    std::uint64_t admitted = 0u;
    const Result<StackAccumulationOutcome> outcome =
        run.storedSamples ? accumulateStackWindow(*run.execution, *run.model, set,
                                                  StackAccumulatorWindow{.firstRow = 0u, .rows = run.height},
                                                  std::span<const StackFrameBand>(&band, 1u), frameWeights,
                                                  std::span<std::uint64_t>(&admitted, 1u), true)
                          : accumulateStackFrame(*run.execution, *run.model, set, band, frameWeights);
    if (!outcome.hasValue())
    {
        return outcome.status();
    }
    run.admittedCounts[stored.frameIndex] += outcome->samplesAccumulated + outcome->samplesExcludedByWeight;
    run.report.samplesAdmitted += outcome->samplesAccumulated + outcome->samplesExcludedByWeight;
    run.report.samplesExcludedByValidity += outcome->samplesExcludedByValidity;
    run.report.samplesExcludedByWeight += outcome->samplesExcludedByWeight;
    run.report.compiledRowUsed = run.report.compiledRowUsed || outcome->compiledRowUsed;
    return Status::success();
}

/// Brings one frame's band of one tile, as the source produced it, to where the accumulators will
/// read it, and records it.
///
/// A band read straight into the frame's band of the tile, or one the source keeps resident with
/// nothing to transform it, is recorded where it sits; any other is converted from where it was read
/// into the frame's band of the tile.
///
/// @param run the run.
/// @param tile the tile.
/// @param bandLayout the layout of one frame's band of this tile.
/// @param bandBytes the bytes of one frame's band of this tile.
/// @param weightsOffset where the weight bands begin in the tile.
/// @param weightBandBytes the bytes of one frame's weight band of this tile.
/// @param frame which frame.
/// @param firstRow the tile's first row.
/// @param rows how many rows the tile holds.
/// @param stored the band the source produced for this frame and these rows.
/// @return success or the first refusal.
[[nodiscard]] Status landStreamingBand(StackExecutionState& run, std::span<std::byte> tile,
                                       const ImageLayout& bandLayout, std::uint64_t bandBytes,
                                       std::uint64_t weightsOffset, std::uint64_t weightBandBytes, std::uint32_t frame,
                                       std::uint32_t firstRow, std::uint32_t rows, const StackSourceBand& stored)
{
    const std::span<std::byte> destination =
        tile.subspan(static_cast<std::size_t>(frame * bandBytes), static_cast<std::size_t>(bandBytes));
    const bool direct = (!run.stages.any() && !run.widens) || run.storedSamples;
    StackFrameBand record{.sampleType = stored.sampleType,
                          .samples = stored.samples,
                          .layout = stored.layout,
                          .frameIndex = frame,
                          .firstOutputRow = firstRow,
                          .validity = stored.validity,
                          .localWeights = {}};
    if (!direct)
    {
        const StackTileBand tileBand{.sampleType = run.tileType, .samples = destination.data(), .layout = &bandLayout};
        const Result<StackSourceOutcome> outcome =
            convert(run, *run.model, run.contract, stored, tileBand, run.workerSlices);
        if (!outcome.hasValue())
        {
            return outcome.status();
        }
        record.sampleType = run.tileType;
        record.samples = destination.data();
        record.layout = &bandLayout;
        record.validity = StackBandValidity{
            .descriptor = outcome->validity,
            .mask = outcome->validity.rule() == ValidityRule::ExplicitMask ? stored.validity.mask : nullptr};
    }
    if (run.model->usesLocalWeights())
    {
        const std::span<std::byte> weightBytes =
            tile.subspan(static_cast<std::size_t>(weightsOffset + frame * weightBandBytes),
                         static_cast<std::size_t>(weightBandBytes));
        const std::span<float> writable(reinterpret_cast<float*>(weightBytes.data()),
                                        static_cast<std::size_t>(rows) * run.width);
        const Status filled = fillWeights(run, frame, firstRow, rows, writable);
        if (filled.failed())
        {
            return filled;
        }
        record.localWeights = writable;
    }
    std::construct_at(run.bandRecords + frame, record);
    return Status::success();
}

/// Reads one frame's rows for one tile of a streaming stack under region access and records the
/// band the accumulators will read.
///
/// A source that decodes reads straight into the frame's band of the tile when nothing widens or
/// transforms the samples, and into the read slot otherwise, from where the source path writes the
/// tile's band; a band the source keeps resident is recorded where it sits when nothing transforms
/// it, because such a band stays valid until the same frame is read again, which is after the
/// tile is done.
///
/// @param run the run.
/// @param tile the tile.
/// @param bandLayout the layout of one frame's band of this tile.
/// @param bandBytes the bytes of one frame's band of this tile.
/// @param weightsOffset where the weight bands begin in the tile.
/// @param weightBandBytes the bytes of one frame's weight band of this tile.
/// @param frame which frame.
/// @param firstRow the tile's first row.
/// @param rows how many rows the tile holds.
/// @param together whether the read is one of several running at once, which installs no progress
///        range of its own and leaves the report's counts to the caller.
/// @return success or the first refusal.
[[nodiscard]] Status readStreamingBand(StackExecutionState& run, std::span<std::byte> tile,
                                       const ImageLayout& bandLayout, std::uint64_t bandBytes,
                                       std::uint64_t weightsOffset, std::uint64_t weightBandBytes, std::uint32_t frame,
                                       std::uint32_t firstRow, std::uint32_t rows, bool together)
{
    std::optional<ProgressRange> unit;
    if (!together)
    {
        unit.emplace(*run.execution, run.progressCompleted, run.progressCompleted + 1u, run.progressTotal);
    }
    const std::span<std::byte> destination =
        tile.subspan(static_cast<std::size_t>(frame * bandBytes), static_cast<std::size_t>(bandBytes));
    const bool direct = (!run.stages.any() && !run.widens) || run.storedSamples;
    StackSourceBand stored{};
    bool inSlot = false;
    const Status read = readFrameInto(run, StackPassKind::Combine, frame, firstRow, rows,
                                      direct ? destination : readSlot(run, rows), stored, inSlot, !together);
    if (read.failed())
    {
        return read;
    }
    return landStreamingBand(run, tile, bandLayout, bandBytes, weightsOffset, weightBandBytes, frame, firstRow, rows,
                             stored);
}

/// Whether a tile's bands are read several at once: a source that allows it, a request that asks for
/// more than one read in flight, and reads that each land in their frame's own band of the tile, with
/// nothing done to them there that shares memory between frames.
///
/// @param run the run.
/// @return true when `readStreamingBandsTogether` may read the tile.
[[nodiscard]] bool readsTogether(const StackExecutionState& run) noexcept
{
    const bool direct = (!run.stages.any() && !run.widens) || run.storedSamples;
    return run.plan.io.access == StackSourceAccess::ConcurrentRegion && run.plan.io.concurrentFrameReads > 1u &&
           direct && !run.model->usesLocalWeights();
}

/// Reads every frame's band of one tile, up to the plan's concurrent reads at once on the
/// operation's workers, each into its frame's own band of the tile.
///
/// A group of reads shares one progress share of no width, installed before its workers start and
/// removed after they join, and its units are completed in frame order once it has joined; the
/// report counts them then. A failure is the first in frame order.
///
/// @param run the run.
/// @param tile the tile.
/// @param bandLayout the layout of one frame's band of this tile.
/// @param bandBytes the bytes of one frame's band of this tile.
/// @param weightsOffset where the weight bands begin in the tile.
/// @param weightBandBytes the bytes of one frame's weight band of this tile.
/// @param firstRow the tile's first row.
/// @param rows how many rows the tile holds.
/// @return success or the first refusal.
[[nodiscard]] Status readStreamingBandsTogether(StackExecutionState& run, std::span<std::byte> tile,
                                                const ImageLayout& bandLayout, std::uint64_t bandBytes,
                                                std::uint64_t weightsOffset, std::uint64_t weightBandBytes,
                                                std::uint32_t firstRow, std::uint32_t rows)
{
    const std::uint32_t group = (std::min)(run.plan.io.concurrentFrameReads, run.frames);
    const std::uint64_t bandInputBytes =
        static_cast<std::uint64_t>(run.width) * rows * run.channels * run.inputSampleBytes;
    // A read's cost is the source's latency, not the band's bytes: each read is a task of its own
    // whatever the band holds, so a group's reads are all in flight together.
    const TaskCost cost{.bytesPerItem = (std::max)(bandInputBytes, kMinimumTaskBytes),
                        .operationsPerItem = kMinimumTaskOperations};
    for (std::uint32_t first = 0u; first < run.frames; first += group)
    {
        const std::uint32_t count = (std::min)(group, run.frames - first);
        {
            const ProgressRange quiet(*run.execution, run.progressCompleted, run.progressCompleted, run.progressTotal);
            const Status read = parallelFor(*run.execution, count, cost, [&](TaskRange range) -> Status {
                for (std::uint64_t item = range.begin; item < range.end; ++item)
                {
                    const Status band =
                        readStreamingBand(run, tile, bandLayout, bandBytes, weightsOffset, weightBandBytes,
                                          first + static_cast<std::uint32_t>(item), firstRow, rows, true);
                    if (band.failed())
                    {
                        return band;
                    }
                }
                return Status::success();
            });
            if (read.failed())
            {
                return read;
            }
        }
        run.report.frameReads += count;
        run.report.inputBytesRead += bandInputBytes * count;
        for (std::uint32_t frame = 0u; frame < count; ++frame)
        {
            completeUnit(run, StackPassKind::Combine);
        }
    }
    return Status::success();
}

/// Whether a tile's bands are read one frame ahead: a source that allows concurrent reads, a request
/// with a second read slot, a band that is converted rather than read into place, and more than one
/// worker.
///
/// A band a stage calibrates or widens is read into a slot and then converted into the tile on the
/// operation's workers, so reading one frame at a time leaves the workers idle while the source reads
/// and the source idle while they convert. Reading the next frame's band into the second slot on one
/// worker while the others convert this one keeps both busy, with one read in flight at a time, in
/// frame order.
///
/// @param run the run.
/// @return true when `readStreamingBandsAhead` may read the tile.
[[nodiscard]] bool readsAhead(const StackExecutionState& run) noexcept
{
    const bool direct = (!run.stages.any() && !run.widens) || run.storedSamples;
    return run.plan.io.access == StackSourceAccess::ConcurrentRegion && run.plan.io.concurrentFrameReads > 1u &&
           !direct && !run.model->usesLocalWeights() && run.frames > 1u && run.execution->admittedConcurrency() > 1u;
}

/// Reads every frame's band of one tile one frame ahead: each frame's band is read into one of two
/// read slots while the band before it is converted from the other into the tile.
///
/// Each frame is one progress unit, installed on the calling thread around both the conversion and
/// the read beside it and completed once both have joined, in frame order; its read is counted in the
/// report then. A conversion's failure is reported before the failure of the read that ran beside it,
/// so the failure is the first in frame order, as one read at a time reports it.
///
/// @param run the run.
/// @param tile the tile.
/// @param bandLayout the layout of one frame's band of this tile.
/// @param bandBytes the bytes of one frame's band of this tile.
/// @param weightsOffset where the weight bands begin in the tile.
/// @param weightBandBytes the bytes of one frame's weight band of this tile.
/// @param firstRow the tile's first row.
/// @param rows how many rows the tile holds.
/// @return success or the first refusal.
[[nodiscard]] Status readStreamingBandsAhead(StackExecutionState& run, std::span<std::byte> tile,
                                             const ImageLayout& bandLayout, std::uint64_t bandBytes,
                                             std::uint64_t weightsOffset, std::uint64_t weightBandBytes,
                                             std::uint32_t firstRow, std::uint32_t rows)
{
    const std::uint64_t bandInputBytes =
        static_cast<std::uint64_t>(run.width) * rows * run.channels * run.inputSampleBytes;
    const std::span<std::byte> buffer = run.set.readBuffer();
    const std::array<std::span<std::byte>, 2u> slots{
        buffer.subspan(0u, static_cast<std::size_t>(bandInputBytes)),
        buffer.subspan(static_cast<std::size_t>(bandInputBytes), static_cast<std::size_t>(bandInputBytes))};
    std::array<StackSourceBand, 2u> stored{};
    std::array<bool, 2u> inSlot{};
    {
        const ProgressRange quiet(*run.execution, run.progressCompleted, run.progressCompleted, run.progressTotal);
        const Status first =
            readFrameInto(run, StackPassKind::Combine, 0u, firstRow, rows, slots[0], stored[0], inSlot[0], false);
        if (first.failed())
        {
            return first;
        }
    }
    // A read's cost is the source's latency, not the band's bytes, so the read and the conversion beside it
    // are each a task of their own.
    const TaskCost cost{.bytesPerItem = (std::max)(bandInputBytes, kMinimumTaskBytes),
                        .operationsPerItem = kMinimumTaskOperations};
    for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
    {
        const std::uint32_t current = frame % 2u;
        const std::uint32_t next = 1u - current;
        Status landed = Status::success();
        Status ahead = Status::success();
        {
            const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u,
                                     run.progressTotal);
            const auto land = [&] {
                return landStreamingBand(run, tile, bandLayout, bandBytes, weightsOffset, weightBandBytes, frame,
                                         firstRow, rows, stored[current]);
            };
            if (frame + 1u < run.frames)
            {
                const Status both = parallelFor(*run.execution, 2u, cost, [&](TaskRange range) -> Status {
                    for (std::uint64_t item = range.begin; item < range.end; ++item)
                    {
                        if (item == 0u)
                        {
                            ahead = readFrameInto(run, StackPassKind::Combine, frame + 1u, firstRow, rows, slots[next],
                                                  stored[next], inSlot[next], false);
                        }
                        else
                        {
                            landed = land();
                        }
                    }
                    return Status::success();
                });
                if (both.failed())
                {
                    return both;
                }
            }
            else
            {
                landed = land();
            }
        }
        if (landed.failed())
        {
            return landed;
        }
        ++run.report.frameReads;
        run.report.inputBytesRead += bandInputBytes;
        completeUnit(run, StackPassKind::Combine);
        if (ahead.failed())
        {
            return ahead;
        }
    }
    return Status::success();
}

/// The leading `elements` of every kept term, cleared: the terms of one tile of `elements`.
///
/// @param set the terms, sized for the plan's tile.
/// @param elements the elements of the tile about to be combined, which a last short tile makes
///        fewer than the terms hold.
/// @return the tile's terms, zeroed as the working set zeroes whole-output terms.
[[nodiscard]] StackAccumulatorSet clearedTileTerms(const StackAccumulatorSet& set, std::uint64_t elements) noexcept
{
    const auto cut = [elements]<typename TermT>(std::span<TermT> term, std::uint64_t perElement) {
        if (term.empty())
        {
            return term;
        }
        const std::span<TermT> leading = term.first(static_cast<std::size_t>(elements * perElement));
        std::memset(leading.data(), 0, leading.size_bytes());
        return leading;
    };
    return StackAccumulatorSet{.sums = cut(set.sums, 1u),
                               .counts = cut(set.counts, 1u),
                               .weightSums = cut(set.weightSums, 1u),
                               .weightSquareSums = cut(set.weightSquareSums, 1u),
                               .varianceMoments = cut(set.varianceMoments, 2u)};
}

/// Writes one combined tile's output rows from terms that hold only that tile, and folds its
/// coverage and what its finalization wrote into the run.
///
/// @param run the run.
/// @param terms the tile's terms.
/// @param targets where the output goes.
/// @param firstRow the tile's first row.
/// @param rows how many rows the tile holds.
/// @param targetsCoverTile whether the targets hold the tile's rows alone.
/// @return success or the first refusal.
[[nodiscard]] Status finalizeTile(StackExecutionState& run, const StackAccumulatorSet& terms,
                                  const StackFinalizeTargets& targets, std::uint32_t firstRow, std::uint32_t rows,
                                  bool targetsCoverTile)
{
    const Result<StackFinalizeOutcome> finalized =
        finalizeStackWindow(*run.execution, *run.model, terms,
                            StackAccumulatorWindow{.firstRow = firstRow, .rows = rows}, targets, targetsCoverTile);
    if (!finalized.hasValue())
    {
        return finalized.status();
    }
    run.tileFinalize.elementsWritten += finalized->elementsWritten;
    run.tileFinalize.elementsWithSamples += finalized->elementsWithSamples;
    run.tileFinalize.elementsWithoutSamples += finalized->elementsWithoutSamples;
    run.tileFinalize.nonFiniteOutputs += finalized->nonFiniteOutputs;
    return foldCoverage(run, terms.counts, run.coverageTally);
}

/// The combine pass under whole-output accumulators and region access: every frame's band of a
/// tile into the tile, then all of them folded into the terms in one pass. When the terms hold one
/// tile, the tile's rows are finalized before the next tile clears them.
///
/// @param run the run.
/// @param set the terms.
/// @return success or the first refusal.
[[nodiscard]] Status combineStreamingRegion(StackExecutionState& run, const StackAccumulatorSet& set)
{
    const std::span<std::byte> tile = run.set.tile();
    const std::span<const double> frameWeights =
        run.model->usesFrameWeights() ? run.set.frameWeights() : std::span<const double>{};
    std::optional<ValidityMaskView<std::uint8_t>> maskView{};
    if (run.tileTerms)
    {
        maskView = run.set.outputMask();
        std::fill(run.histogram.begin(), run.histogram.end(), 0u);
        run.coverageTally = StackCoverageTally{};
        run.tileFinalize = StackFinalizeOutcome{};
    }
    const StackFinalizeTargets targets = targetsOf(run, maskView.has_value() ? &*maskView : nullptr);
    const bool together = readsTogether(run);
    const bool ahead = !together && readsAhead(run);
    for (std::uint32_t tileIndex = 0u; tileIndex < run.plan.tile.tileCount; ++tileIndex)
    {
        const std::uint32_t firstRow = tileIndex * run.plan.tile.rows;
        const std::uint32_t rows = (std::min)(run.plan.tile.rows, run.height - firstRow);
        const Result<ImageLayout> bandLayout = stackTileBandLayout(*run.model, rows);
        if (!bandLayout.hasValue())
        {
            return bandLayout.status();
        }
        const std::uint64_t bandBytes = stackTileFrameBytes(*run.model, rows);
        const std::uint64_t weightBandBytes = static_cast<std::uint64_t>(rows) * run.width * sizeof(float);
        const std::uint64_t weightsOffset = bandBytes * run.frames;
        if (together)
        {
            const Status read = readStreamingBandsTogether(run, tile, *bandLayout, bandBytes, weightsOffset,
                                                           weightBandBytes, firstRow, rows);
            if (read.failed())
            {
                return read;
            }
        }
        else if (ahead)
        {
            const Status read = readStreamingBandsAhead(run, tile, *bandLayout, bandBytes, weightsOffset,
                                                        weightBandBytes, firstRow, rows);
            if (read.failed())
            {
                return read;
            }
        }
        else
        {
            for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
            {
                const Status read = readStreamingBand(run, tile, *bandLayout, bandBytes, weightsOffset, weightBandBytes,
                                                      frame, firstRow, rows);
                if (read.failed())
                {
                    return read;
                }
                completeUnit(run, StackPassKind::Combine);
            }
        }
        const std::span<const StackFrameBand> bands(run.bandRecords, run.frames);
        const StackAccumulatorSet terms =
            run.tileTerms ? clearedTileTerms(set, static_cast<std::uint64_t>(rows) * run.width * run.channels) : set;
        const Result<StackAccumulationOutcome> outcome =
            run.tileTerms ? accumulateStackWindow(*run.execution, *run.model, terms,
                                                  StackAccumulatorWindow{.firstRow = firstRow, .rows = rows}, bands,
                                                  frameWeights, run.admittedCounts, run.storedSamples)
            : run.storedSamples
                ? accumulateStackWindow(*run.execution, *run.model, terms,
                                        StackAccumulatorWindow{.firstRow = 0u, .rows = run.height}, bands, frameWeights,
                                        run.admittedCounts, true)
                : accumulateStackFrames(*run.execution, *run.model, terms, bands, frameWeights, run.admittedCounts);
        if (!outcome.hasValue())
        {
            return outcome.status();
        }
        run.report.samplesAdmitted += outcome->samplesAccumulated + outcome->samplesExcludedByWeight;
        run.report.samplesExcludedByValidity += outcome->samplesExcludedByValidity;
        run.report.samplesExcludedByWeight += outcome->samplesExcludedByWeight;
        run.report.compiledRowUsed = run.report.compiledRowUsed || outcome->compiledRowUsed;
        if (run.tileTerms)
        {
            std::optional<ValidityMaskView<std::uint8_t>> tileMask{};
            const StackFinalizeTargets tileTargets = run.outputByTile ? tileTargetsOf(run, rows, tileMask) : targets;
            const Status finalized = finalizeTile(run, terms, tileTargets, firstRow, rows, run.outputByTile);
            if (finalized.failed())
            {
                return finalized;
            }
            ++run.report.tilesCombined;
            if (run.outputByTile)
            {
                const Status handed = handOverTile(run, tileTargets, firstRow, rows);
                if (handed.failed())
                {
                    return handed;
                }
            }
            continue;
        }
        ++run.report.tilesCombined;
    }
    return Status::success();
}

/// The combine pass under whole-output accumulators: each frame folded into the terms, band by
/// band under whole-frame access, and every frame's band of a tile at once under region access.
///
/// @param run the run.
/// @return success or the first refusal.
[[nodiscard]] Status combineStreaming(StackExecutionState& run)
{
    const StackAccumulatorSet set = accumulatorsOf(run);
    if (isStackSourceRegionAccess(run.plan.io.access))
    {
        return combineStreamingRegion(run, set);
    }
    // Whole frames: each is read once and walked tile by tile from where it was decoded.
    for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
    {
        {
            const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u,
                                     run.progressTotal);
            StackSourceBand stored{};
            bool inSlot = false;
            const Status read = readFrame(run, StackPassKind::Combine, frame, 0u, run.height, stored, inSlot);
            if (read.failed())
            {
                return read;
            }
            for (std::uint32_t tileIndex = 0u; tileIndex < run.plan.tile.tileCount; ++tileIndex)
            {
                const std::uint32_t firstRow = tileIndex * run.plan.tile.rows;
                const std::uint32_t rows = (std::min)(run.plan.tile.rows, run.height - firstRow);
                Result<StackSourceRows> cut = stackSourceRowsOf(stored, firstRow, rows);
                if (!cut.hasValue())
                {
                    return cut.status();
                }
                const StackSourceRows window = std::move(*cut);
                const Status accumulated = accumulateBand(run, set, window.band(), inSlot, firstRow, rows);
                if (accumulated.failed())
                {
                    return accumulated;
                }
            }
        }
        completeUnit(run, StackPassKind::Combine);
    }
    run.report.tilesCombined = run.plan.tile.tileCount;
    return Status::success();
}

} // namespace an::stacking
