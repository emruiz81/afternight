// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack executor's measurement stage: the frame weights, the
// normalization measurement pass and the local-weight measurement pass.
//
// Both passes read every frame whole, in frame order, into regions of the one measurement
// workspace the planner laid out. The normalization pass converts through the model with no
// normalization scope, because the coefficients it measures do not exist yet; it stages the
// reference frame first, fits or matches every other frame against it, and fills the local
// document, the working set's global normalization table, or both. The local-weight pass converts
// through the whole model, because the lattice is measured on what the combine reads, then
// finishes the surface in place.
#include "algorithms/stacking/StackExecutionState.h"

#include <AfterNightEngine/Algorithms/Stacking/LocalWeights.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPreparedState.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "runtime/ProgressRange.h"
#include "algorithms/stacking/StackHistory.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <utility>

namespace an::stacking
{

Status planComposedMemory(const StackExecutionState& run, StackPlanningRequest& planning)
{
    // What the composed operations allocate for themselves is counted here, before the plan, so
    // the budget sees it: the full residency of a document the stack fits - its frame records,
    // `frames + 1` grids and, when a scale grid is fitted, one more per non-reference frame - and
    // the largest scratch of the background fit, the scale estimation, the scale-grid fit and the
    // apply, which run one at a time.
    if (run.localScope && !run.stages.normalizationPrepared)
    {
        const auto plan = planLocalNormalization(run.request->localNormalization, run.width, run.height, run.channels);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        const auto& shape = plan->shape();
        const auto& request = plan->request();
        const std::uint64_t workers = (std::max)(run.execution->admittedConcurrency(), 1u);
        const std::uint64_t nodes = shape.nodeCount();
        const std::uint64_t grids =
            static_cast<std::uint64_t>(run.frames) + 1u + (run.request->fitLocalScaleGrid ? run.frames - 1u : 0u);
        const CheckedSize state = CheckedSize{sizeof(LocalNormalizationDocument)} +
                                  CheckedSize{run.frames} * sizeof(LocalNormalizationFrameCoefficients) +
                                  CheckedSize{grids} * shape.valueCount() * sizeof(double);
        if (!state.isValid())
        {
            return failure(ErrorCode::LimitExceeded, kExecute, "the fitted normalization document is too large");
        }
        planning.fittedNormalizationBytes = state.valueOr(0u);
        const auto aligned = [](std::uint64_t bytes) { return (bytes + 63u) & ~std::uint64_t{63u}; };
        const auto smoothing = [&](std::uint32_t radius) {
            const std::uint32_t r = (std::min)(radius, shape.gridSize);
            return r == 0u ? std::uint64_t{0} : (2u * nodes + 2u * r + 1u) * sizeof(double);
        };
        const std::uint64_t nodeScratch = aligned(shape.valueCount()) + aligned(nodes * sizeof(double));
        const std::uint64_t background = (std::max)(workers * aligned(plan->fitWorkingBytesPerWorker()) + nodeScratch,
                                                    smoothing(request.backgroundSmoothingRadius));
        const std::uint64_t scaleSamples = (std::min)(static_cast<std::uint64_t>(request.scaleSamples),
                                                      static_cast<std::uint64_t>(run.width) * run.height);
        const std::uint64_t scale = aligned(2u * scaleSamples * sizeof(double));
        const std::uint64_t scaleGrid = run.request->fitLocalScaleGrid
                                            ? workers * aligned(4ull * plan->samplesPerScaleNode() * sizeof(double)) +
                                                  nodeScratch + smoothing(request.localScaleSmoothingRadius)
                                            : 0u;
        const std::uint64_t apply = aligned(static_cast<std::uint64_t>(run.width) * sizeof(double)) +
                                    aligned(static_cast<std::uint64_t>(shape.gridSize + 2u) * sizeof(std::uint32_t)) +
                                    workers * aligned(kStackSourceRowArrays * shape.nodesPerAxis() * sizeof(double));
        planning.normalizationScratchBytes = (std::max)({background, scale, scaleGrid, apply});
    }
    // The cluster analysis takes one bit per pixel, rounded up to eight bytes, and one
    // sixty-four-bit traversal index per pixel.
    if (run.model->usesRejection() && run.request->clusterAnalysis.has_value() && !run.outputs->clusters.empty())
    {
        const std::uint64_t pixels = static_cast<std::uint64_t>(run.width) * run.height;
        const std::uint64_t marks = pixels / 8u + (pixels % 8u != 0u ? 1u : 0u);
        const CheckedSize bytes =
            CheckedSize{(marks + 7u) & ~std::uint64_t{7u}} + CheckedSize{pixels} * sizeof(std::uint64_t);
        if (!bytes.isValid())
        {
            return failure(ErrorCode::LimitExceeded, kExecute, "the rejection cluster workspace is too large");
        }
        planning.diagnosticsScratchBytes = bytes.valueOr(0u);
    }
    return Status::success();
}

/// A staged frame as the normalization operations read it.
///
/// @param run the run.
/// @param staged the frame.
/// @return the view, or its refusal.
[[nodiscard]] Result<NormalizationFrameView> normalizationView(const StackExecutionState& run,
                                                               const StagedStackFrame& staged)
{
    const ImageDescriptor& descriptor =
        staged.sampleType == run.tileType ? *run.tileDescriptor : run.model->request().input.descriptor;
    return NormalizationFrameView::create(
        staged.samples, staged.layout->totalBytes(), *staged.layout, descriptor,
        KernelValidityView{.descriptor = staged.validity, .mask = staged.mask, .maskPlane = 0u});
}

/// Writes one frame's match into the table and the caller's record.
///
/// @param run the run.
/// @param frame which frame.
/// @param match the match.
/// @return success or the table's refusal.
[[nodiscard]] Status recordMatch(StackExecutionState& run, std::uint32_t frame, const FrameNormalization& match)
{
    const Status stored = storeStackFrameNormalization(*run.model, frame, match, run.set.frameNormalization());
    if (stored.failed())
    {
        return stored;
    }
    if (!run.outputs->frameNormalization.empty())
    {
        run.outputs->frameNormalization[frame] = match;
    }
    return Status::success();
}

/// Copies the prepared matches into the table before the passes run.
///
/// @param run the run.
/// @return success or the table's refusal.
[[nodiscard]] Status storePreparedMatches(StackExecutionState& run)
{
    for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
    {
        const FrameNormalization* match = nullptr;
        if (run.contract.localNormalization != nullptr)
        {
            const LocalNormalizationDocument& document = *run.contract.localNormalization;
            if (frame < document.frames.size() && document.frames[frame].global.has_value())
            {
                match = &*document.frames[frame].global;
            }
        }
        else if (frame < run.contract.normalization.size())
        {
            match = &run.contract.normalization[frame];
        }
        if (match == nullptr)
        {
            return refuse(ErrorCode::IncompatibleArguments, kExecute, "contract",
                          "contract states prepared coefficients and holds no match for every frame");
        }
        const Status recorded = recordMatch(run, frame, *match);
        if (recorded.failed())
        {
            return recorded;
        }
    }
    return Status::success();
}

/// Derives or checks the frame weights into the table, and says what the table is.
///
/// @param run the run.
/// @return success or the weighting's refusal.
[[nodiscard]] Status prepareWeights(StackExecutionState& run)
{
    const StackRequest& request = run.model->request();
    const std::span<double> table = run.set.frameWeights();
    StackFrameWeightingResult& result = run.report.weighting;
    result.source = request.weighting.method;
    result.frameCount = run.frames;
    result.channelCount = run.channels;
    if (request.weighting.method == StackWeighting::Uniform)
    {
        std::fill(table.begin(), table.end(), 1.0);
        result.disposition = StackFrameWeightingDisposition::Uniform;
    }
    else if (request.weighting.method == StackWeighting::Explicit)
    {
        const StackFrameWeights& weights = run.request->explicitWeights;
        if (weights.perFrame.empty() && weights.perFrameChannel.empty())
        {
            return refuse(ErrorCode::IncompatibleArguments, kExecute, "request.explicitWeights",
                          "request.explicitWeights holds no weights and the stack weighs its frames explicitly");
        }
        const Status checked = checkStackFrameWeights(*run.model, weights);
        if (checked.failed())
        {
            return checked;
        }
        for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
        {
            for (std::uint32_t channel = 0u; channel < run.channels; ++channel)
            {
                table[static_cast<std::size_t>(frame) * run.channels + channel] =
                    weights.perFrame.empty()
                        ? weights.perFrameChannel[static_cast<std::size_t>(frame) * run.channels + channel]
                        : weights.perFrame[frame];
            }
        }
        result.disposition = StackFrameWeightingDisposition::Supplied;
        result.measuredFrameCount = run.frames;
    }
    else
    {
        if (run.request->measurements.size() != run.frames)
        {
            return failure(ErrorCode::IncompatibleArguments, kExecute,
                           "request.measurements does not hold one record per frame and the stack derives its "
                           "weights from them",
                           {StatusContextField::ofText(ContextKey::Parameter, "request.measurements"),
                            StatusContextField::ofCount(ContextKey::Requested, run.request->measurements.size()),
                            StatusContextField::ofCount(ContextKey::Limit, run.frames)});
        }
        Result<StackFrameWeightingResult> derived =
            computeStackFrameWeights(*run.execution, *run.model, run.request->measurements, table);
        if (!derived.hasValue())
        {
            return derived.status();
        }
        result = std::move(*derived);
        run.report.diagnosticsWorkingBytes += result.workingBytes;
        recordComposedMemory(run, result.workingBytes);
    }
    if (request.weighting.method == StackWeighting::Uniform || request.weighting.method == StackWeighting::Explicit)
    {
        result.summary = summarizeStackFrameWeights(table);
        result.effectiveFrameCount = result.summary.efficiency * static_cast<double>(run.frames);
    }
    if (!run.outputs->frameWeights.empty())
    {
        std::copy(table.begin(), table.end(), run.outputs->frameWeights.begin());
    }
    return Status::success();
}

/// Measures one frame that is not the reference: fitted against the reference under a local
/// scope, matched to it under a global one, or both.
///
/// @param run the run.
/// @param frame which frame.
/// @param frameSlot the workspace slot for a widened frame, or empty.
/// @param referenceView the reference frame as the normalization operations read it.
/// @param combined the reference every frame is matched to, under a global scope.
/// @param document the document being fitted, under a local scope.
/// @return success or the first refusal.
[[nodiscard]] Status measureNormalizationFrame(StackExecutionState& run, std::uint32_t frame,
                                               std::span<std::byte> frameSlot,
                                               const NormalizationFrameView& referenceView,
                                               const std::optional<NormalizationReference>& combined,
                                               LocalNormalizationDocument* document)
{
    const StackRequest& request = run.model->request();
    const bool global = run.stages.normalizeGlobally;
    const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u, run.progressTotal);
    StackSourceBand band{};
    StagedStackFrame staged{};
    const Status stagedFrame =
        stageWholeFrame(run, StackPassKind::MeasureNormalization, frame, false, frameSlot, {}, band, staged);
    if (stagedFrame.failed())
    {
        return stagedFrame;
    }
    Result<NormalizationFrameView> view = normalizationView(run, staged);
    if (!view.hasValue())
    {
        return view.status();
    }
    const NormalizationFrameView* measured = &*view;
    std::optional<NormalizationFrameView> normalizedView{};
    if (run.localScope)
    {
        const Result<StackLocalNormalizationFit> fitted = fitStackLocalNormalizationFrame(
            *run.execution, *run.model, *view, frame, referenceView, run.request->fitLocalScaleGrid, *document);
        if (!fitted.hasValue())
        {
            return fitted.status();
        }
        recordComposedMemory(run, (std::max)({fitted->background.workingBytes,
                                              fitted->scale.has_value() ? fitted->scale->workingBytes : 0u,
                                              fitted->scaleGrid.has_value() ? fitted->scaleGrid->workingBytes : 0u}));
        if (global)
        {
            // The global match is measured over the locally normalized frame, which is written
            // where the frame sits when that is the executor's memory and into the read slot
            // otherwise.
            const LocalNormalizationFrameCoefficients& record = document->frames[frame];
            const LocalNormalizationApplyRequest apply{.shape = document->shape,
                                                       .interpolation = document->request.interpolation,
                                                       .targetBackground = record.background,
                                                       .referenceBackground = document->referenceBackground,
                                                       .scale =
                                                           std::span<const double>(record.scale).first(run.channels),
                                                       .scaleGrid = record.scaleGrid};
            void* destination = staged.owned ? const_cast<void*>(staged.samples) : readSlot(run, run.height).data();
            const ImageLayout* destinationLayout = staged.owned ? staged.layout : &*run.wholeTileLayout;
            const Result<LocalNormalizationApplyResult> applied = applyLocalNormalization(
                *run.execution, apply, *view, destination, destinationLayout->totalBytes(), *destinationLayout);
            if (!applied.hasValue())
            {
                return applied.status();
            }
            run.report.diagnosticsWorkingBytes += applied->workingBytes;
            recordComposedMemory(run, applied->workingBytes);
            StagedStackFrame normalized = staged;
            normalized.samples = destination;
            normalized.layout = destinationLayout;
            normalized.owned = true;
            Result<NormalizationFrameView> made = normalizationView(run, normalized);
            if (!made.hasValue())
            {
                return made.status();
            }
            normalizedView = *made;
            measured = &*normalizedView;
        }
    }
    if (global)
    {
        const Result<NormalizationFrameStatistics> statistics =
            measureGlobalNormalization(*run.execution, *measured, request.normalization.scaleEstimator);
        if (!statistics.hasValue())
        {
            return statistics.status();
        }
        recordComposedMemory(run, statistics->peakScratchBytes);
        const Result<FrameNormalization> match =
            estimateGlobalNormalization(*combined, *statistics, *request.normalization.globalMethod);
        if (!match.hasValue())
        {
            return match.status();
        }
        if (document != nullptr)
        {
            document->frames[frame].global = *match;
        }
        const Status recorded = recordMatch(run, frame, *match);
        if (recorded.failed())
        {
            return recorded;
        }
    }
    return Status::success();
}

/// The normalization measurement: the global match against the reference, the local surface
/// fitted against it, or both.
///
/// @param run the run.
/// @return success or the first refusal.
[[nodiscard]] Status measureNormalization(StackExecutionState& run)
{
    const StackRequest& request = run.model->request();
    const std::uint32_t reference = request.normalization.referenceFrame;
    const std::span<std::byte> workspace = run.set.measurementWorkspace();
    const std::span<std::byte> frameSlot =
        run.widens ? workspace.subspan(static_cast<std::size_t>(run.workspaceLayout.normalizationFrameOffset),
                                       static_cast<std::size_t>(run.tileFrameBytes))
                   : std::span<std::byte>{};
    const bool global = run.stages.normalizeGlobally;
    std::optional<NormalizationReference> combined{};
    std::optional<FrameNormalization> identity{};
    if (global)
    {
        Result<FrameNormalization> made = identityNormalization(*request.normalization.globalMethod, run.channels,
                                                                request.normalization.scaleEstimator);
        if (!made.hasValue())
        {
            return made.status();
        }
        identity = *made;
    }

    // The reference frame first: it is what everything else is measured against.
    StackSourceBand referenceBand{};
    StagedStackFrame referenceStaged{};
    std::optional<NormalizationFrameView> referenceView{};
    LocalNormalizationDocument* document = nullptr;
    if (run.localScope)
    {
        const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u, run.progressTotal);
        // The reference lives in its own slot for the whole pass, so it is copied there whatever
        // the source did with it.
        bool inSlot = false;
        const Status read =
            readFrame(run, StackPassKind::MeasureNormalization, reference, 0u, run.height, referenceBand, inSlot);
        if (read.failed())
        {
            return read;
        }
        const std::span<std::byte> referenceSlot =
            workspace.subspan(static_cast<std::size_t>(run.workspaceLayout.normalizationReferenceOffset),
                              static_cast<std::size_t>(run.tileFrameBytes));
        const StackTileBand tile{
            .sampleType = run.tileType, .samples = referenceSlot.data(), .layout = &*run.wholeTileLayout};
        const Result<StackSourceOutcome> outcome =
            convert(run, *run.preNormalization, run.preContract, referenceBand, tile, {});
        if (!outcome.hasValue())
        {
            return outcome.status();
        }
        referenceStaged = StagedStackFrame{
            .samples = referenceSlot.data(),
            .layout = &*run.wholeTileLayout,
            .sampleType = run.tileType,
            .validity = outcome->validity,
            .mask = outcome->validity.rule() == ValidityRule::ExplicitMask ? referenceBand.validity.mask : nullptr,
            .owned = true};
        Result<NormalizationFrameView> view = normalizationView(run, referenceStaged);
        if (!view.hasValue())
        {
            return view.status();
        }
        referenceView = *view;
        document = run.outputs->localNormalization != nullptr ? run.outputs->localNormalization : &run.fittedDocument;
        const Result<LocalBackgroundFitResult> started = beginStackLocalNormalization(
            *run.execution, *run.model, run.request->localNormalization, *referenceView, *document);
        if (!started.hasValue())
        {
            return started.status();
        }
        run.document = document;
        recordComposedMemory(run, started->workingBytes);
    }
    else
    {
        const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u, run.progressTotal);
        const Status staged = stageWholeFrame(run, StackPassKind::MeasureNormalization, reference, false, frameSlot, {},
                                              referenceBand, referenceStaged);
        if (staged.failed())
        {
            return staged;
        }
        Result<NormalizationFrameView> view = normalizationView(run, referenceStaged);
        if (!view.hasValue())
        {
            return view.status();
        }
        referenceView = *view;
    }
    if (global)
    {
        const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u, run.progressTotal);
        const Result<NormalizationFrameStatistics> statistics =
            measureGlobalNormalization(*run.execution, *referenceView, request.normalization.scaleEstimator);
        if (!statistics.hasValue())
        {
            return statistics.status();
        }
        recordComposedMemory(run, statistics->peakScratchBytes);
        const NormalizationFrameStatistics members[] = {*statistics};
        Result<NormalizationReference> made = combineNormalizationReferences(members);
        if (!made.hasValue())
        {
            return made.status();
        }
        combined = *made;
        if (document != nullptr && reference < document->frames.size())
        {
            document->frames[reference].global = *identity;
        }
        const Status recorded = recordMatch(run, reference, *identity);
        if (recorded.failed())
        {
            return recorded;
        }
    }
    completeUnit(run, StackPassKind::MeasureNormalization);

    // Every other frame, in order: fitted against the reference and matched to it.
    for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
    {
        if (frame == reference)
        {
            continue;
        }
        const Status measuredFrame =
            measureNormalizationFrame(run, frame, frameSlot, *referenceView, combined, document);
        if (measuredFrame.failed())
        {
            return measuredFrame;
        }
        completeUnit(run, StackPassKind::MeasureNormalization);
    }
    if (document != nullptr)
    {
        run.document = document;
        run.contract.localNormalization = document;
        run.report.localNormalizationFitted = true;
        run.report.localNormalizationBytes = preparedStateBytesOf(*document);
    }
    run.report.normalizationMeasured = true;
    return Status::success();
}

/// The local-weight measurement: every frame's lattice, then the finished surface.
///
/// @param run the run.
/// @return success or the first refusal.
[[nodiscard]] Status measureLocalWeights(StackExecutionState& run)
{
    const std::span<std::byte> workspace = run.set.measurementWorkspace();
    const StackMeasurementWorkspaceLayout& layout = run.workspaceLayout;
    const std::span<std::byte> integral = workspace.subspan(static_cast<std::size_t>(layout.localWeightIntegralOffset),
                                                            static_cast<std::size_t>(run.localWeightWorkspaceBytes));
    const std::span<std::byte> frameSlot =
        run.widens ? workspace.subspan(static_cast<std::size_t>(layout.localWeightFrameOffset),
                                       static_cast<std::size_t>(run.tileFrameBytes))
                   : std::span<std::byte>{};
    std::span<const std::span<std::byte>> scratch{};
    if (run.localScope)
    {
        pointWorkerSlices(run, workspace.subspan(static_cast<std::size_t>(layout.localWeightScratchOffset)));
        scratch = run.workerSlices;
    }
    const std::span<float> grids = run.set.localWeightGrids();
    const std::size_t perFrame = static_cast<std::size_t>(run.localWeightShape->valuesPerFrame());
    for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
    {
        {
            const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u,
                                     run.progressTotal);
            StackSourceBand band{};
            StagedStackFrame staged{};
            const Status stagedFrame =
                stageWholeFrame(run, StackPassKind::MeasureLocalWeights, frame, true, frameSlot, scratch, band, staged);
            if (stagedFrame.failed())
            {
                return stagedFrame;
            }
            const LocalWeightFrameSource source{
                .sampleType = staged.sampleType,
                .samples = staged.samples,
                .layout = staged.layout,
                .validity = StackBandValidity{.descriptor = staged.validity, .mask = staged.mask}};
            const Result<LocalWeightMeasurement> measured = measureLocalWeightGrid(
                *run.execution, *run.model, source, integral, grids.subspan(frame * perFrame, perFrame));
            if (!measured.hasValue())
            {
                return measured.status();
            }
        }
        completeUnit(run, StackPassKind::MeasureLocalWeights);
    }
    const ProgressRange finishing(*run.execution, run.progressCompleted, run.progressCompleted, run.progressTotal);
    const Result<LocalWeightingSummary> finished =
        finalizeLocalWeightGrids(*run.execution, *run.model, grids, integral);
    if (!finished.hasValue())
    {
        return finished.status();
    }
    run.report.localWeighting = *finished;
    run.report.localWeightsMeasured = true;
    return Status::success();
}

} // namespace an::stacking
