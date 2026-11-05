// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the streaming stack executor: the plan's passes, run in order, over the
// memory the working set holds.
//
// This unit checks the call, plans the run, takes the working set, runs the passes in the plan's
// order and hands the result over; the stages themselves live in the four `StackExecution*.cpp`
// units, which share the run through `StackExecutionState.h`.
//
// Three things in the executor are decisions rather than mechanics.
//
// **The executor allocates nothing of its own.** Every buffer it writes is the working set's, and
// the records it needs beside them - the per-frame counters, the coverage histogram, the worker
// slices, the band records, the rejection records - live in the frame-state block the planner
// counted for them, cut by `stackFrameBookkeepingLayoutOf`. A measurement pass carves the widened
// frame, the reference frame, the lattice's integral images and the surface's row scratch out of
// the one workspace the planner counted, by `stackMeasurementWorkspaceLayoutOf`. The plan also
// reserves the scratch allocated by composed measurements and diagnostics and the full residency
// of a fitted local-normalization document. These owners allocate their own memory once; the
// working set does not allocate duplicate reservations.
//
// **A band is written where it can be and nowhere else.** A band the source decoded into the
// executor's read slot is transformed in place; a band the source keeps resident is never written
// and is either accumulated where it sits, when no stage applies, or converted into the tile or
// into the read slot, which the plan counted for exactly that. The measurement passes convert
// through a model with no normalization scope, because the coefficients they exist to measure do
// not exist yet, and the local-weight measurement converts through the whole model, because the
// lattice is measured on what the combine reads.
//
// **Every figure in the report is a composed operation's own.** The executor adds counts and
// never estimates: the conversion says what it admitted, the accumulators and the kernels say what
// they combined, the weighting says what it derived, the estimators say what they rejected per
// frame, and the working set says what it held.

#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMemoryRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethodRules.h>

#include "algorithms/stacking/StackExecutionState.h"
#include "algorithms/stacking/StackAccumulatorWindow.h"

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Stacking/LocalWeights.h>
#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanner.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPreparedState.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejection.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "algorithms/stacking/StackHistory.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace an
{
using namespace stacking;
namespace
{

using stacking::failure;
using stacking::refuse;

// The records the executor keeps in the frame-state block have to fit what the planner reserved.
static_assert(sizeof(StackFrameBand) + sizeof(ValidityMaskView<const std::uint8_t>) <= kStackBandRecordBytes,
              "a band and its retained mask view outgrew what the planner reserves");
static_assert(sizeof(StackFrameBand) % alignof(ValidityMaskView<const std::uint8_t>) == 0u);
static_assert(alignof(StackFrameBand) <= kStackFrameCounterBytes, "a band record needs a stricter boundary");
static_assert(sizeof(FrameRejectionRate) <= kStackFrameRateRecordBytes, "a rate record outgrew what is reserved");
static_assert(alignof(FrameRejectionRate) <= kStackFrameCounterBytes, "a rate record needs a stricter boundary");
static_assert(sizeof(std::span<std::byte>) <= kStackWorkerSliceRecordBytes, "a slice record outgrew what is reserved");
static_assert(alignof(std::span<std::byte>) <= kStackFrameCounterBytes, "a slice record needs a stricter boundary");

/// Runs one pass of the plan between its begin and its end.
///
/// @param run the run.
/// @param kind which pass.
/// @return success or the first refusal.
[[nodiscard]] Status runPass(StackExecutionState& run, StackPassKind kind)
{
    const Status begun = run.set.beginPass(*run.execution, kind);
    if (begun.failed())
    {
        return begun;
    }
    run.execution->reportProgress(run.progressCompleted, run.progressTotal, stackPassKindName(kind));
    Status status = Status::success();
    switch (kind)
    {
    case StackPassKind::MeasureNormalization:
        status = measureNormalization(run);
        break;
    case StackPassKind::MeasureLocalWeights:
        status = measureLocalWeights(run);
        break;
    case StackPassKind::Combine: {
        pointWorkerSlices(run, {});
        std::optional<ValidityMaskView<std::uint8_t>> maskView = run.set.outputMask();
        const ValidityMaskView<std::uint8_t>* mask = maskView.has_value() ? &*maskView : nullptr;
        status = run.history ? combineHistory(run, targetsOf(run, mask)) : combineStreaming(run);
        break;
    }
    case StackPassKind::Finalize: {
        std::optional<ValidityMaskView<std::uint8_t>> maskView = run.set.outputMask();
        const ValidityMaskView<std::uint8_t>* mask = maskView.has_value() ? &*maskView : nullptr;
        {
            const ProgressRange unit(*run.execution, run.progressCompleted, run.progressCompleted + 1u,
                                     run.progressTotal);
            status = finalize(run, mask);
        }
        if (status.ok())
        {
            completeUnit(run, StackPassKind::Finalize);
        }
        break;
    }
    }
    recordComposedMemory(run, 0u);
    run.set.endPass();
    if (status.failed())
    {
        return status;
    }
    ++run.report.passesRun;
    say(*run.execution, DiagnosticSeverity::Info,
        "Pass %u of %u, %s: %llu frame reads so far of %llu planned, %llu tiles combined, %llu bytes read.",
        run.report.passesRun, static_cast<unsigned>(run.plan.passes.count), stackPassKindName(kind),
        static_cast<Count>(run.report.frameReads), static_cast<Count>(run.plan.io.frameReads),
        static_cast<Count>(run.report.tilesCombined), static_cast<Count>(run.report.inputBytesRead));
    return Status::success();
}

/// Checks the caller's output records against the stack.
///
/// @param run the run.
/// @return success or the first span of the wrong length, named.
[[nodiscard]] Status checkOutputs(const StackExecutionState& run)
{
    const StackExecutionOutputs& outputs = *run.outputs;
    const std::size_t frames = run.frames;
    if (!outputs.frameWeights.empty() && outputs.frameWeights.size() != frames * run.channels)
    {
        return refuse(ErrorCode::IncompatibleArguments, kExecute, "outputs.frameWeights",
                      "outputs.frameWeights is neither empty nor one weight per frame and channel");
    }
    if (!outputs.frameNormalization.empty() && outputs.frameNormalization.size() != frames)
    {
        return refuse(ErrorCode::IncompatibleArguments, kExecute, "outputs.frameNormalization",
                      "outputs.frameNormalization is neither empty nor one match per frame");
    }
    if (!outputs.frameRejectionRates.empty() && outputs.frameRejectionRates.size() != frames)
    {
        return refuse(ErrorCode::IncompatibleArguments, kExecute, "outputs.frameRejectionRates",
                      "outputs.frameRejectionRates is neither empty nor one rate per frame");
    }
    if (!outputs.flaggedFrames.empty() && outputs.flaggedFrames.size() < frames)
    {
        return refuse(ErrorCode::IncompatibleArguments, kExecute, "outputs.flaggedFrames",
                      "outputs.flaggedFrames is neither empty nor at least one index per frame");
    }
    return Status::success();
}

} // namespace

Result<StackResult> executeStack(const EngineContext& engine, const ExecutionContext& execution,
                                 const StackModel& model, StackFrameSource& source, const StackSourceContract& contract,
                                 const StackExecutionRequest& request, const StackExecutionOutputs& outputs)
{
    if (!engine.valid())
    {
        return failure(ErrorCode::NotInitialized, kExecute, "the engine context holds nothing");
    }
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kExecute, "the execution context holds nothing");
    }
    if (engine.id() != execution.engineContextId())
    {
        return refuse(ErrorCode::InvalidArgument, kExecute, "execution",
                      "execution runs in another engine context than the one the stack's memory would come from");
    }
    const Status alive = execution.checkCancellation();
    if (alive.failed())
    {
        return alive;
    }

    StackExecutionState run;
    run.engine = &engine;
    run.execution = &execution;
    run.model = &model;
    run.source = &source;
    run.request = &request;
    run.outputs = &outputs;
    run.contract = contract;
    const StackRequest& stack = model.request();
    const ImageExtent& extent = stack.input.descriptor.extent();
    run.width = extent.width();
    run.height = extent.height();
    run.channels = extent.channelCount();
    run.frames = stack.input.frameCount;
    run.inputType = stack.input.descriptor.sampleType();
    run.tileType = stackTileSampleTypeOf(run.inputType);
    run.inputSampleBytes = sampleTypeSizeBytes(run.inputType);
    run.tileFrameBytes = stackTileFrameBytes(model, run.height);
    run.history = stackAccumulationStrategyOf(model) == StackAccumulationStrategy::TileSampleHistory;
    run.widens = run.inputType != run.tileType;
    run.localScope = stack.normalization.scope == StackNormalizationScope::Local ||
                     stack.normalization.scope == StackNormalizationScope::LocalThenGlobal;
    run.tileDescriptor = stackSourceCalibrationDescriptor(model);
    run.localWeightShape = LocalWeightGridShape::of(model);
    run.localWeightWorkspaceBytes =
        run.localWeightShape.has_value() ? localWeightMeasurementWorkspaceBytes(*run.localWeightShape, run.frames) : 0u;

    const Status checkedOutputs = checkOutputs(run);
    if (checkedOutputs.failed())
    {
        return checkedOutputs;
    }
    // The cluster search reads the whole rejection map, which an output taken tile by tile never holds.
    if (request.outputSink != nullptr && request.clusterAnalysis.has_value() && !outputs.clusters.empty())
    {
        return refuse(ErrorCode::IncompatibleArguments, kExecute, "clusterAnalysis",
                      "a rejection cluster search reads the whole map, and an output sink takes it tile by tile");
    }
    const Result<StackSourceStages> stages = checkStackSourceContract(model, contract);
    if (!stages.hasValue())
    {
        return stages.status();
    }
    run.stages = *stages;
    run.storedSamples = !run.history && run.widens && stackAccumulatesStoredSamples(model) && run.stages.convert &&
                        !run.stages.calibrate && !run.stages.normalizeLocally && !run.stages.normalizeGlobally;
    const Status sumDomain = checkStackSumDomain(model, request.sumDomain);
    if (sumDomain.failed())
    {
        return sumDomain;
    }
    if (!isStackSourceAccess(source.access()))
    {
        return refuse(ErrorCode::InvalidArgument, kExecute, "source",
                      "source reports an access this build does not define");
    }

    // The measurement passes convert through the model with no normalization scope; the contract
    // they read carries only what precedes a normalization.
    StackRequest preRequest = stack;
    preRequest.normalization.scope = StackNormalizationScope::None;
    preRequest.normalization.globalMethod.reset();
    run.preNormalization = StackModel::create(preRequest);
    if (!run.preNormalization.has_value())
    {
        return failure(ErrorCode::Internal, kExecute,
                       "the model with its normalization removed does not describe a stack");
    }
    run.preContract = StackSourceContract{.state = contract.state,
                                          .calibration = contract.calibration,
                                          .masters = contract.masters,
                                          .normalization = {},
                                          .localNormalization = nullptr};
    if (run.stages.calibrate && !contract.calibration.empty())
    {
        run.report.biasSubtracted = contract.calibration[0].usesBias();
        run.report.darkSubtracted = contract.calibration[0].usesDark();
        run.report.flatFielded = contract.calibration[0].usesFlat();
    }
    Result<ImageLayout> wholeLayout = stackTileBandLayout(model, run.height);
    if (!wholeLayout.hasValue())
    {
        return wholeLayout.status();
    }
    run.wholeTileLayout = *wholeLayout;

    // The plan, from the model, the source's access and what the contract holds prepared.
    StackPlanningRequest planning{
        .access = source.access(),
        .concurrentFrameReads = request.concurrentFrameReads,
        .normalizationPrepared = run.stages.normalizationPrepared,
        .localWeightsPrepared = false,
        .preparedStateBytes = preparedStateBytesOf(StackPreparedState{.normalization = contract.normalization,
                                                                      .localNormalization = contract.localNormalization,
                                                                      .photometricWeights = {}}),
        .workingSetLimitBytes = request.workingSetLimitBytes,
        .maximumTileRows = request.maximumTileRows,
        .sourceGranularityRows = request.sourceGranularityRows,
        .outputByTile = request.outputSink != nullptr};
    const Status composed = planComposedMemory(run, planning);
    if (composed.failed())
    {
        return composed;
    }
    Result<StackPlan> planned = planStack(execution, model, planning);
    if (!planned.hasValue())
    {
        return planned.status();
    }
    run.plan = *planned;
    run.tileTerms = stackAccumulatorsCoverOneTile(model, run.plan.io.access);
    run.outputByTile = run.plan.memory.outputCoversOneTile;
    run.workers = run.plan.memory.workers;
    run.bookkeeping = stackFrameBookkeepingLayoutOf(model, run.workers);
    run.workspaceLayout = stackMeasurementWorkspaceLayoutOf(model, run.workers, run.localWeightWorkspaceBytes);
    run.progressTotal = run.plan.io.frameReads + 1u;
    Result<StackWorkingSet> created = StackWorkingSet::create(engine, execution, model, run.plan);
    if (!created.hasValue())
    {
        return created.status();
    }
    run.set = std::move(*created);
    cutBookkeeping(run);
    run.report.plan = run.plan;
    run.report.stages = run.stages;
    run.report.backend = run.plan.backend.selection;
    run.report.sumDomain = request.sumDomain;
    run.document = contract.localNormalization;

    {
        const ProgressRange unit(execution, 0u, 0u, run.progressTotal);
        const Status weights = prepareWeights(run);
        if (weights.failed())
        {
            return weights;
        }
    }
    if (run.stages.normalizeGlobally && run.stages.normalizationPrepared)
    {
        const Status stored = storePreparedMatches(run);
        if (stored.failed())
        {
            return stored;
        }
    }
    for (std::size_t index = 0u; index < run.plan.passes.count; ++index)
    {
        const Status pass = runPass(run, run.plan.passes.passes[index].kind);
        if (pass.failed())
        {
            return pass;
        }
    }
    run.report.frameSampleCountsComplete = true;
    execution.reportTrace(TraceEventKind::Counter, "stacking", "frameReads", static_cast<double>(run.report.frameReads),
                          "reads");
    execution.reportTrace(TraceEventKind::Counter, "stacking", "tilesCombined",
                          static_cast<double>(run.report.tilesCombined), "tiles");
    execution.reportTrace(TraceEventKind::Counter, "stacking", "inputBytesRead",
                          static_cast<double>(run.report.inputBytesRead), "bytes");
    const Status beforeHandover = execution.checkCancellation();
    if (beforeHandover.failed())
        return beforeHandover;

    // The handover: the image, the mask and each requested map leave the set with no copy - or, when
    // the sink took them tile by tile, nothing leaves but the description and the report.
    const std::optional<ImageDescriptor> descriptor = stackMethodOutputDescriptor(model, request.sumDomain);
    if (!descriptor.has_value())
    {
        return failure(ErrorCode::Internal, kExecute, "the method's output has no description");
    }
    if (run.outputByTile)
    {
        run.report.memory = run.set.account();
        run.report.memory.peakHeldBytes = (std::max)(run.report.memory.peakHeldBytes, run.composedPeakBytes);
        say(execution, DiagnosticSeverity::Info,
            "Stacked %u frames in %u passes: %llu frame reads, %llu tiles handed to the output sink as they were "
            "finished, %llu samples admitted and %llu excluded; held a peak of %llu bytes against a planned %llu.",
            run.frames, run.report.passesRun, static_cast<Count>(run.report.frameReads),
            static_cast<Count>(run.report.tilesCombined), static_cast<Count>(run.report.samplesAdmitted),
            static_cast<Count>(run.report.samplesExcludedByValidity),
            static_cast<Count>(run.report.memory.peakHeldBytes), static_cast<Count>(run.plan.memory.peakBytes));
        StackResult handed{
            .image = {}, .descriptor = *descriptor, .mask = {}, .maps = {}, .report = std::move(run.report)};
        const Status beforePublication = execution.checkCancellation();
        if (beforePublication.failed())
            return beforePublication;
        return handed;
    }
    Result<ImageStorageBytes> image = run.set.releaseOutputImage();
    if (!image.hasValue())
    {
        return image.status();
    }
    Result<ValidityMask> mask = run.set.releaseOutputMask();
    if (!mask.hasValue())
    {
        return mask.status();
    }
    StackResult result{
        .image = std::move(*image), .descriptor = *descriptor, .mask = std::move(*mask), .maps = {}, .report = {}};
    for (std::size_t index = 0u; index < kStackOutputMapCount; ++index)
    {
        const auto map = static_cast<StackOutputMap>(index);
        if (!stackOutputMapRequested(stack.output, map))
        {
            continue;
        }
        Result<ImageStorageBytes> released = run.set.releaseOutputMap(map);
        if (!released.hasValue())
        {
            return released.status();
        }
        result.maps[index] = std::move(*released);
    }
    run.report.memory = run.set.account();
    run.report.memory.peakHeldBytes = (std::max)(run.report.memory.peakHeldBytes, run.composedPeakBytes);
    say(execution, DiagnosticSeverity::Info,
        "Stacked %u frames in %u passes: %llu frame reads, %llu tiles, %llu samples admitted and %llu excluded, "
        "%llu elements with samples and %llu without; held a peak of %llu bytes against a planned %llu and handed "
        "over %llu.",
        run.frames, run.report.passesRun, static_cast<Count>(run.report.frameReads),
        static_cast<Count>(run.report.tilesCombined), static_cast<Count>(run.report.samplesAdmitted),
        static_cast<Count>(run.report.samplesExcludedByValidity), static_cast<Count>(run.report.elementsWithSamples),
        static_cast<Count>(run.report.elementsWithoutSamples), static_cast<Count>(run.report.memory.peakHeldBytes),
        static_cast<Count>(run.plan.memory.peakBytes), static_cast<Count>(run.report.memory.handedOverBytes));
    result.report = std::move(run.report);
    const Status beforePublication = execution.checkCancellation();
    if (beforePublication.failed())
        return beforePublication;
    return result;
}

Result<MetadataTransition> stackTransition(const StackModel& model, const StackExecutionReport& report,
                                           const ImageSemantics& source)
{
    const StackRequest& request = model.request();
    ImageSemanticsRequest semanticsRequest = source.toRequest();
    semanticsRequest.processingState.normalized = semanticsRequest.processingState.normalized ||
                                                  report.stages.normalizeLocally || report.stages.normalizeGlobally;
    semanticsRequest.processingState.biasSubtracted =
        semanticsRequest.processingState.biasSubtracted || report.biasSubtracted;
    semanticsRequest.processingState.darkSubtracted =
        semanticsRequest.processingState.darkSubtracted || report.darkSubtracted;
    semanticsRequest.processingState.flatFielded = semanticsRequest.processingState.flatFielded || report.flatFielded;
    semanticsRequest.validity = kStackOperationEffect.validity.produced;
    Result<ImageSemantics> semantics = ImageSemantics::create(semanticsRequest);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    std::string detail = "combination=";
    detail += stackCombinationName(request.combination);
    detail += ",rejection=";
    detail += stackRejectionName(request.rejection.method);
    detail += ",weighting=";
    detail += stackWeightingName(request.weighting.method);
    detail += ",localWeighting=";
    detail += request.localWeighting.enabled ? "enabled" : "disabled";
    detail += ",normalization=";
    detail += stackNormalizationScopeName(request.normalization.scope);
    detail += ",frames=" + std::to_string(request.input.frameCount);
    detail += ",sumDomain=";
    detail += stackSumDomainName(report.sumDomain);
    Result<MetadataTransition> transition = operationTransition(
        kStackOperationEffect,
        OperationOutcome{.operation = kExecute, .detail = std::move(detail), .semantics = std::move(*semantics)});
    if (!transition.hasValue())
    {
        return Status::failure(transition.status().code(),
                               StatusDetail{.message = transition.status().message(),
                                            .operation = kTransition,
                                            .domain = ErrorDomain::Algorithms},
                               transition.status());
    }
    return transition;
}

} // namespace an
