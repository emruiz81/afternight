// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack planner: every buffer counted, every pass listed, every decision
// said out loud.
//
// The constant-expression half of the planner - the strategy, the tile width and the per-worker
// scratch contract - lives in the header so a kernel can be written against it. This unit holds
// what needs an execution context: the budget the engine context offers, the workers the operation
// was admitted, the dispatch table's answer, and the diagnostic sink the decisions are explained
// through. Every byte figure goes through `CheckedSize`, and a figure that does not fit sixty-four
// bits is a returned failure rather than a small, plausible, wrong number.

#include <AfterNightEngine/Algorithms/Stacking/StackPlanner.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>

#include <AfterNightEngine/Algorithms/Stacking/StackMemoryRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>

#include <AfterNightEngine/Algorithms/Stacking/LocalWeights.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::planStack";

/// The bytes of one running variance: a mean and a second moment.
constexpr std::uint64_t kVarianceMomentBytes = 2u * kStackAccumulatorBytes;

/// The bytes of one global normalization coefficient pair: an offset and a scale, both doubles.
constexpr std::uint64_t kNormalizationCoefficientBytes = 2u * sizeof(double);

/// The bytes of one frame weight, a double.
constexpr std::uint64_t kFrameWeightBytes = sizeof(double);

/// What `std::printf` wants a sixty-four-bit count as.
using Count = unsigned long long;

/// A failure of this operation.
///
/// @param code why.
/// @param message what a person reads.
/// @param context the typed facts, if any.
/// @return the failure.
[[nodiscard]] Status planFailure(ErrorCode code, const char* message, std::vector<StatusContextField> context = {})
{
    return Status::failure(code, StatusDetail{.message = message,
                                              .operation = kOperation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = std::move(context)});
}

/// A refusal because something does not fit the budget, with the three numbers a caller acts on.
///
/// @param message what does not fit.
/// @param requested the bytes that were needed.
/// @param available the bytes the plan was allowed to reach.
/// @param limit the bound the plan was held to.
/// @return the failure.
[[nodiscard]] Status budgetFailure(const char* message, std::uint64_t requested, std::uint64_t available,
                                   std::uint64_t limit)
{
    return planFailure(ErrorCode::MemoryBudgetExceeded, message,
                       {StatusContextField::ofCount(ContextKey::Requested, requested),
                        StatusContextField::ofCount(ContextKey::Available, available),
                        StatusContextField::ofCount(ContextKey::Limit, limit)});
}

/// A refusal because a figure does not fit sixty-four bits.
///
/// @return the failure.
[[nodiscard]] Status overflowFailure()
{
    return planFailure(ErrorCode::LimitExceeded,
                       "the plan's byte or read accounting is not representable in sixty-four bits");
}

/// Reports one formatted sentence to the operation's diagnostic sink, if it has one.
///
/// @param execution the operation.
/// @param severity how much it matters.
/// @param format a `printf` format; counts are passed as `Count`.
void say(const ExecutionContext& execution, DiagnosticSeverity severity, const char* format, ...)
{
    if (execution.diagnosticSink() == nullptr)
    {
        return;
    }
    char buffer[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer, sizeof buffer, format, arguments);
    va_end(arguments);
    execution.reportDiagnostic(severity, ErrorDomain::Algorithms, kOperation, buffer);
}

/// The sentence a person reads for a strategy reason.
///
/// @param reason the reason.
/// @return a string literal.
[[nodiscard]] const char* strategySentence(StackStrategyReason reason) noexcept
{
    switch (reason)
    {
    case StackStrategyReason::StreamingCombination:
        return "a mean, a sum or an extremum updates an accumulator with one sample at a time, so the frame "
               "count never enters the memory";
    case StackStrategyReason::RejectionNeedsSampleHistory:
        return "a rejection judges every sample of a pixel against the others, so every frame's samples for a "
               "tile are held at once";
    case StackStrategyReason::MedianNeedsSampleHistory:
        return "a median is a rank over every sample of a pixel, so every frame's samples for a tile are held "
               "at once";
    case StackStrategyReason::AutoAdaptiveNeedsSampleHistory:
        return "the auto-adaptive weighted mean iterates its weights over every sample of a pixel, so every "
               "frame's samples for a tile are held at once";
    }
    return "the strategy was chosen for a reason this build cannot name";
}

/// The sentence a person reads for a tile reason.
///
/// @param reason the reason.
/// @return a string literal.
[[nodiscard]] const char* tileSentence(StackTileReason reason) noexcept
{
    switch (reason)
    {
    case StackTileReason::WholeFrameUnbounded:
        return "nothing bounds the working set, so the tile is the whole frame";
    case StackTileReason::WholeFrameFits:
        return "the whole frame fits the available budget";
    case StackTileReason::BudgetBound:
        return "the largest band of rows that fits the available budget";
    case StackTileReason::CacheBand:
        return "one cache-sized row band per admitted worker under region access";
    case StackTileReason::SourceGranularity:
        return "the rows the smallest piece the source decodes whole spans, so each piece is decoded about once";
    case StackTileReason::CallerRowCap:
        return "the caller's row cap is smaller than the budget, the cache band and the source's granularity allow";
    }
    return "the tile was chosen for a reason this build cannot name";
}

/// The sentence a person reads for a precision reason.
///
/// @param reason the reason.
/// @return a string literal.
[[nodiscard]] const char* precisionSentence(StackPrecisionReason reason) noexcept
{
    switch (reason)
    {
    case StackPrecisionReason::DefaultFloat32:
        return "storage up to thirty-two bits defaults to Float32 under the stack's lossy contract";
    case StackPrecisionReason::StoredInFloat64:
        return "Float64 storage would be truncated by Float32";
    }
    return "the width was chosen for a reason this build cannot name";
}

/// Says every decision of a plan, one sentence each, and the warning a multiplied read deserves.
///
/// @param execution the operation, whose sink receives the sentences.
/// @param plan the plan.
/// @param frames how many frames the stack combines.
void explainPlan(const ExecutionContext& execution, const StackPlan& plan, std::uint32_t frames)
{
    if (execution.diagnosticSink() == nullptr)
    {
        return;
    }
    say(execution, DiagnosticSeverity::Info, "Accumulation: %s - %s.",
        stackAccumulationStrategyName(plan.strategy.strategy), strategySentence(plan.strategy.reason));
    say(execution, DiagnosticSeverity::Info,
        "Precision: the tile holds %s samples and the accumulation is %s under %s - %s.",
        sampleTypeName(plan.precision.tileSampleType), computePrecisionName(plan.precision.accumulation),
        float64JustificationName(plan.precision.justification), precisionSentence(plan.precision.reason));
    if (plan.tile.tileCount == 1u)
    {
        say(execution, DiagnosticSeverity::Info, "Tiles: 1 tile of %u rows by %u columns (%s) - %s.", plan.tile.rows,
            plan.tile.width, stackTileReasonName(plan.tile.reason), tileSentence(plan.tile.reason));
    }
    else
    {
        say(execution, DiagnosticSeverity::Info, "Tiles: %u tiles of %u rows by %u columns (%s) - %s.",
            plan.tile.tileCount, plan.tile.rows, plan.tile.width, stackTileReasonName(plan.tile.reason),
            tileSentence(plan.tile.reason));
    }
    for (std::size_t index = 0u; index < plan.passes.count; ++index)
    {
        const StackPass& pass = plan.passes.passes[index];
        if (pass.tileCount > 0u)
        {
            say(execution, DiagnosticSeverity::Info,
                "Pass %u of %u, %s: %llu frame reads across %u tiles, %llu input bytes, %llu bytes working set.",
                static_cast<unsigned>(index + 1u), static_cast<unsigned>(plan.passes.count),
                stackPassKindName(pass.kind), static_cast<Count>(pass.frameReads), pass.tileCount,
                static_cast<Count>(pass.inputBytes), static_cast<Count>(pass.workingSetBytes));
        }
        else
        {
            say(execution, DiagnosticSeverity::Info,
                "Pass %u of %u, %s: %llu frame reads, %llu input bytes, %llu bytes working set.",
                static_cast<unsigned>(index + 1u), static_cast<unsigned>(plan.passes.count),
                stackPassKindName(pass.kind), static_cast<Count>(pass.frameReads), static_cast<Count>(pass.inputBytes),
                static_cast<Count>(pass.workingSetBytes));
        }
    }
    say(execution, DiagnosticSeverity::Info,
        "Memory: %llu bytes persistent, %llu bytes scratch (%u workers x %llu), %llu bytes tile, %llu bytes read "
        "buffer, %llu bytes measurement workspace, %llu bytes fitted normalization, %llu bytes normalization "
        "scratch, %llu bytes diagnostic scratch, %llu bytes initialization scratch; peak %llu against %llu "
        "available (%s).",
        static_cast<Count>(plan.memory.persistentBytes), static_cast<Count>(plan.memory.scratchBytes),
        plan.memory.workers, static_cast<Count>(plan.memory.scratchBytesPerWorker),
        static_cast<Count>(plan.memory.tileBytes), static_cast<Count>(plan.memory.readBufferBytes),
        static_cast<Count>(plan.memory.measurementWorkspaceBytes),
        static_cast<Count>(plan.memory.fittedNormalizationBytes),
        static_cast<Count>(plan.memory.normalizationScratchBytes),
        static_cast<Count>(plan.memory.diagnosticsScratchBytes),
        static_cast<Count>(plan.memory.initializationScratchBytes), static_cast<Count>(plan.memory.peakBytes),
        static_cast<Count>(plan.memory.availableBytes),
        plan.memory.budgetBounded ? "bounded budget" : "no budget bound");
    if (plan.backend.fellBack)
    {
        say(execution, DiagnosticSeverity::Info,
            "Backend: %s at %s on %u workers, after the requested %s could not be served.",
            computeBackendName(plan.backend.selection.backend),
            instructionSetName(plan.backend.selection.instructionSet), plan.backend.workers,
            computeBackendName(plan.backend.requestedBackend));
    }
    else
    {
        say(execution, DiagnosticSeverity::Info, "Backend: %s at %s on %u workers.",
            computeBackendName(plan.backend.selection.backend),
            instructionSetName(plan.backend.selection.instructionSet), plan.backend.workers);
    }
    if (plan.io.inputMultiplier > 1u)
    {
        say(execution, DiagnosticSeverity::Warning,
            "Whole-frame reads are multiplied by the tile count: %u tiles x %u frames = %llu full reads, %llu "
            "bytes of input against %llu distinct; region access or a larger budget cuts the tile count.",
            plan.tile.tileCount, frames, static_cast<Count>(static_cast<std::uint64_t>(plan.tile.tileCount) * frames),
            static_cast<Count>(plan.io.logicalInputBytes), static_cast<Count>(plan.io.distinctInputBytes));
    }
}

/// How many output maps a request writes.
///
/// @param output the output request.
/// @return the map count.
[[nodiscard]] std::uint64_t outputMapCount(const StackOutputRequest& output) noexcept
{
    std::uint64_t count = 0u;
    switch (output.rejectionMaps)
    {
    case RejectionMapForm::None:
        break;
    case RejectionMapForm::Merged:
        count += 1u;
        break;
    case RejectionMapForm::LowAndHigh:
        count += 2u;
        break;
    }
    count += output.coverageMap ? 1u : 0u;
    count += output.varianceMap ? 1u : 0u;
    count += output.signalToNoiseGainMap ? 1u : 0u;
    count += output.weightSumMap ? 1u : 0u;
    return count;
}

/// Whether every one of a set of checked sizes carries a number.
///
/// @param sizes the sizes.
/// @return true when none overflowed.
[[nodiscard]] bool allValid(std::initializer_list<CheckedSize> sizes) noexcept
{
    for (const CheckedSize& size : sizes)
    {
        if (!size.isValid())
        {
            return false;
        }
    }
    return true;
}

} // namespace

const char* stackSourceAccessName(StackSourceAccess access) noexcept
{
    switch (access)
    {
    case StackSourceAccess::WholeFrame:
        return "WholeFrame";
    case StackSourceAccess::Region:
        return "Region";
    case StackSourceAccess::ConcurrentRegion:
        return "ConcurrentRegion";
    }
    return "UnknownStackSourceAccess";
}

const char* stackAccumulationStrategyName(StackAccumulationStrategy strategy) noexcept
{
    switch (strategy)
    {
    case StackAccumulationStrategy::WholeOutputAccumulators:
        return "WholeOutputAccumulators";
    case StackAccumulationStrategy::TileSampleHistory:
        return "TileSampleHistory";
    }
    return "UnknownStackAccumulationStrategy";
}

const char* stackStrategyReasonName(StackStrategyReason reason) noexcept
{
    switch (reason)
    {
    case StackStrategyReason::StreamingCombination:
        return "StreamingCombination";
    case StackStrategyReason::RejectionNeedsSampleHistory:
        return "RejectionNeedsSampleHistory";
    case StackStrategyReason::MedianNeedsSampleHistory:
        return "MedianNeedsSampleHistory";
    case StackStrategyReason::AutoAdaptiveNeedsSampleHistory:
        return "AutoAdaptiveNeedsSampleHistory";
    }
    return "UnknownStackStrategyReason";
}

const char* stackTileReasonName(StackTileReason reason) noexcept
{
    switch (reason)
    {
    case StackTileReason::WholeFrameUnbounded:
        return "WholeFrameUnbounded";
    case StackTileReason::WholeFrameFits:
        return "WholeFrameFits";
    case StackTileReason::BudgetBound:
        return "BudgetBound";
    case StackTileReason::CacheBand:
        return "CacheBand";
    case StackTileReason::SourceGranularity:
        return "SourceGranularity";
    case StackTileReason::CallerRowCap:
        return "CallerRowCap";
    }
    return "UnknownStackTileReason";
}

const char* stackPrecisionReasonName(StackPrecisionReason reason) noexcept
{
    switch (reason)
    {
    case StackPrecisionReason::DefaultFloat32:
        return "DefaultFloat32";
    case StackPrecisionReason::StoredInFloat64:
        return "StoredInFloat64";
    }
    return "UnknownStackPrecisionReason";
}

const char* stackPassKindName(StackPassKind kind) noexcept
{
    switch (kind)
    {
    case StackPassKind::MeasureNormalization:
        return "MeasureNormalization";
    case StackPassKind::MeasureLocalWeights:
        return "MeasureLocalWeights";
    case StackPassKind::Combine:
        return "Combine";
    case StackPassKind::Finalize:
        return "Finalize";
    }
    return "UnknownStackPassKind";
}

Result<StackPlan> planStack(const ExecutionContext& execution, const StackModel& model,
                            const StackPlanningRequest& request)
{
    if (!execution.valid())
    {
        return planFailure(ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (!isStackSourceAccess(request.access))
    {
        return planFailure(ErrorCode::InvalidArgument, "the source access is not one this build defines",
                           {StatusContextField::ofText(ContextKey::Parameter, "access")});
    }
    if (request.concurrentFrameReads == 0u)
    {
        return planFailure(ErrorCode::InvalidArgument, "a source holds at least one frame decoded at once",
                           {StatusContextField::ofText(ContextKey::Parameter, "concurrentFrameReads")});
    }

    const Result<BackendSelection> selection = selectBackend(execution);
    if (!selection.hasValue())
    {
        return selection.status();
    }

    const StackRequest& stack = model.request();
    const ImageExtent& extent = stack.input.descriptor.extent();
    const std::uint64_t width = extent.width();
    const std::uint64_t height = extent.height();
    const std::uint64_t channels = extent.channelCount();
    const std::uint32_t frames = stack.input.frameCount;
    const SampleType inputType = stack.input.descriptor.sampleType();
    const SampleType tileType = stackTileSampleTypeOf(inputType);
    const std::uint64_t inputSampleBytes = sampleTypeSizeBytes(inputType);
    const std::uint64_t tileSampleBytes = sampleTypeSizeBytes(tileType);
    const std::uint64_t outputSampleBytes = sampleTypeSizeBytes(stack.output.sampleType);
    const StackAccumulationStrategy strategy = stackAccumulationStrategyOf(model);
    const bool history = strategy == StackAccumulationStrategy::TileSampleHistory;
    const bool weighted = model.usesFrameWeights() || model.usesLocalWeights();
    const bool region = isStackSourceRegionAccess(request.access);
    const std::uint32_t workers = execution.admittedConcurrency() > 0u ? execution.admittedConcurrency() : 1u;
    const std::uint64_t concurrent = request.concurrentFrameReads;

    // The persistent state: what lives from the first pass to the last.
    const CheckedSize elements = CheckedSize{width} * height * channels;
    const CheckedSize frameBytes = elements * inputSampleBytes;
    // An output the caller takes tile by tile is sized with the tile, below, as terms that hold one
    // tile are; otherwise it is the whole output's.
    if (request.outputByTile && !stackOutputCanCoverOneTile(model, request.access))
    {
        return planFailure(ErrorCode::IncompatibleArguments,
                           "the output can be taken tile by tile only when every tile is finished as it is combined: "
                           "under sample history, or with terms that hold one tile",
                           {StatusContextField::ofText(ContextKey::Parameter, "outputByTile")});
    }
    const bool tileOutput = request.outputByTile;
    const CheckedSize outputImageRow = CheckedSize{width} * channels * outputSampleBytes;
    const CheckedSize outputMaskRow = CheckedSize{packedMaskRowBytes(extent.width())} * channels;
    const CheckedSize outputMapsRow =
        CheckedSize{width} * channels * kStackMapSampleBytes * outputMapCount(stack.output);
    const CheckedSize outputImage = tileOutput ? CheckedSize{0u} : outputImageRow * height;
    const CheckedSize outputMask = tileOutput ? CheckedSize{0u} : outputMaskRow * height;
    const CheckedSize outputMaps = tileOutput ? CheckedSize{0u} : outputMapsRow * height;
    const CheckedSize outputPerRow = tileOutput ? outputImageRow + outputMaskRow + outputMapsRow : CheckedSize{0u};
    // Terms that hold one tile are sized with the tile, below; everything else is sized here.
    const bool tileTerms = stackAccumulatorsCoverOneTile(model, request.access);
    const CheckedSize termsPerRow{tileTerms ? stackAccumulatorBytesPerRow(model) : 0u};
    CheckedSize accumulators{0u};
    if (!history && !tileTerms)
    {
        accumulators = elements * kStackAccumulatorBytes + elements * kStackCountBytes;
        if (weighted)
        {
            accumulators = accumulators + elements * kStackAccumulatorBytes;
        }
        if (stack.output.signalToNoiseGainMap)
        {
            accumulators = accumulators + elements * kStackAccumulatorBytes;
        }
        if (stack.output.varianceMap)
        {
            accumulators = accumulators + elements * kVarianceMomentBytes;
        }
    }
    CheckedSize frameState = CheckedSize{frames} * channels * kFrameWeightBytes;
    if (stackNormalizationScopeUsesGlobalMethod(stack.normalization.scope))
    {
        frameState = frameState + CheckedSize{frames} * channels * kNormalizationCoefficientBytes;
    }
    // The executor's per-frame bookkeeping lives beside the tables: the sample counters, the
    // coverage histogram, and the records a rejection or a sample history keeps per frame.
    frameState = frameState + CheckedSize{stackFrameBookkeepingBytes(model, workers)};
    CheckedSize localWeightGrid{0u};
    if (model.usesLocalWeights())
    {
        const std::uint32_t spacing = stack.localWeighting.gridSpacing;
        localWeightGrid = CheckedSize{frames} * localWeightGridNodesAlong(extent.width(), spacing) *
                          localWeightGridNodesAlong(extent.height(), spacing) * kLocalWeightNodeBytes;
    }
    const CheckedSize prepared{request.preparedStateBytes};
    const CheckedSize persistent = outputImage + outputMask + outputMaps + accumulators + frameState + localWeightGrid +
                                   prepared + request.fittedNormalizationBytes;

    // The combine pass: per-worker scratch, the tile and the read buffer.
    const std::uint64_t scratchPerWorker = stackScratchBytesPerWorker(model);
    const CheckedSize scratch = CheckedSize{scratchPerWorker} * workers;
    // Under whole-frame access a frame stored at the tile width is read by the accumulators where
    // it was decoded, and any other storage is widened into the tile, so a streaming strategy holds
    // one frame's band there. Under region access every frame's band of a tile is held at once,
    // as a sample history is, so that one call folds them all into the terms.
    const bool widens = inputType != tileType;
    // A tile that applies a local weight surface holds each frame's weight band beside its
    // sample band, so the surface is read once per band rather than once per sample.
    const CheckedSize tileSampleRow = (history || region) ? CheckedSize{width} * channels * frames * tileSampleBytes
                                      : widens            ? CheckedSize{width} * channels * tileSampleBytes
                                                          : CheckedSize{0u};
    const CheckedSize tilePerRow = tileSampleRow + CheckedSize{stackLocalWeightTileBytesPerRow(model, request.access)};
    const CheckedSize readPerRow = CheckedSize{width} * channels * inputSampleBytes * concurrent;
    const CheckedSize wholeRead = frameBytes * concurrent;
    const CheckedSize fixedRead = region ? CheckedSize{0u} : wholeRead;
    const CheckedSize variableRead = region ? readPerRow : CheckedSize{0u};

    // The measurement passes: one whole frame at a time, read through the source path as the
    // combine reads it - so one converted frame when the storage is widened - plus the
    // estimator's workspace.
    const bool measureNormalization =
        stack.normalization.scope != StackNormalizationScope::None && !request.normalizationPrepared;
    const bool measureLocalWeights = model.usesLocalWeights() && !request.localWeightsPrepared;
    std::uint64_t normalizationScratchBytes = measureNormalization ? request.normalizationScratchBytes : 0u;
    if (measureNormalization && stackNormalizationScopeUsesGlobalMethod(stack.normalization.scope))
    {
        const auto measured = globalNormalizationWorkingBytes(extent.width(), extent.height(), tileType, workers);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        normalizationScratchBytes = (std::max)(normalizationScratchBytes, *measured);
    }
    const std::uint64_t diagnosticsScratchBytes =
        (std::max)(request.diagnosticsScratchBytes,
                   model.usesRejection() ? std::uint64_t{2u} * frames * sizeof(double) : std::uint64_t{0u});
    const CheckedSize tileFrame = elements * tileSampleBytes;
    // Both measurement workspaces are laid out by one rule the executor follows: the reference
    // frame at the tile width beside the widened frame for a local fit, and the local-weight
    // lattice's integral images beside the widened frame and, under a local scope, the row
    // scratch the source path reads a band through the surface with.
    const std::optional<LocalWeightGridShape> localWeightShape = LocalWeightGridShape::of(model);
    const StackMeasurementWorkspaceLayout workspaceLayout = stackMeasurementWorkspaceLayoutOf(
        model, workers,
        localWeightShape.has_value() ? localWeightMeasurementWorkspaceBytes(*localWeightShape, frames) : 0u);
    const CheckedSize normalizationWorkspace =
        measureNormalization ? CheckedSize{workspaceLayout.normalizationBytes} : CheckedSize{0u};
    const CheckedSize localWeightWorkspace = measureLocalWeights && localWeightShape.has_value()
                                                 ? CheckedSize{workspaceLayout.localWeightBytes}
                                                 : CheckedSize{0u};

    if (!allValid({elements,
                   frameBytes,
                   outputImageRow,
                   outputMaskRow,
                   outputMapsRow,
                   outputImage,
                   outputMask,
                   outputMaps,
                   outputPerRow,
                   accumulators,
                   frameState,
                   localWeightGrid,
                   persistent,
                   scratch,
                   tilePerRow,
                   readPerRow,
                   wholeRead,
                   tileFrame,
                   normalizationWorkspace,
                   localWeightWorkspace}))
    {
        return overflowFailure();
    }
    const std::uint64_t persistentBytes = persistent.valueOr(0u);

    // The budget: what the engine context has left, narrowed by the caller's own limit.
    const EngineLimits limits = execution.limits();
    const MemoryResource* resource = execution.memoryResource();
    const std::uint64_t outstanding = resource != nullptr ? resource->counters().liveBytes : 0u;
    bool bounded = false;
    std::uint64_t budget = kDefaultMemoryBudgetBytes;
    std::uint64_t available = kDefaultMemoryBudgetBytes;
    if (limits.memoryBudgetBytes != kDefaultMemoryBudgetBytes)
    {
        bounded = true;
        budget = limits.memoryBudgetBytes;
        available = budget > outstanding ? budget - outstanding : 0u;
    }
    if (request.workingSetLimitBytes != 0u)
    {
        bounded = true;
        budget = request.workingSetLimitBytes < budget ? request.workingSetLimitBytes : budget;
        available = request.workingSetLimitBytes < available ? request.workingSetLimitBytes : available;
    }
    if (bounded && persistentBytes > available)
    {
        return budgetFailure("the persistent state alone - the output, its mask and maps, the accumulators and the "
                             "per-frame tables - exceeds the available budget",
                             persistentBytes, available, budget);
    }

    StackPlan plan;
    plan.passes.count = 0u;
    const auto addPass = [&plan](const StackPass& pass) { plan.passes.passes[plan.passes.count++] = pass; };
    const CheckedSize distinctInput = CheckedSize{frames} * frameBytes;
    std::uint64_t measurementWorkspaceBytes = 0u;

    if (measureNormalization)
    {
        const CheckedSize working = persistent + wholeRead + normalizationWorkspace + normalizationScratchBytes;
        if (!allValid({working, distinctInput}))
        {
            return overflowFailure();
        }
        if (bounded && working.valueOr(0u) > available)
        {
            return budgetFailure("the MeasureNormalization pass holds one whole frame and its workspace beside the "
                                 "persistent state, and they exceed the available budget",
                                 working.valueOr(0u), available, budget);
        }
        addPass(StackPass{.kind = StackPassKind::MeasureNormalization,
                          .tileCount = 0u,
                          .frameReads = frames,
                          .inputBytes = distinctInput.valueOr(0u),
                          .workingSetBytes = working.valueOr(0u)});
        const std::uint64_t workspace = normalizationWorkspace.valueOr(0u);
        measurementWorkspaceBytes = workspace > measurementWorkspaceBytes ? workspace : measurementWorkspaceBytes;
    }
    if (measureLocalWeights)
    {
        const CheckedSize working = persistent + wholeRead + localWeightWorkspace;
        if (!allValid({working, distinctInput}))
        {
            return overflowFailure();
        }
        if (bounded && working.valueOr(0u) > available)
        {
            return budgetFailure("the MeasureLocalWeights pass holds one whole frame and its integral images beside "
                                 "the persistent state, and they exceed the available budget",
                                 working.valueOr(0u), available, budget);
        }
        addPass(StackPass{.kind = StackPassKind::MeasureLocalWeights,
                          .tileCount = 0u,
                          .frameReads = frames,
                          .inputBytes = distinctInput.valueOr(0u),
                          .workingSetBytes = working.valueOr(0u)});
        const std::uint64_t workspace = localWeightWorkspace.valueOr(0u);
        measurementWorkspaceBytes = workspace > measurementWorkspaceBytes ? workspace : measurementWorkspaceBytes;
    }

    // The tile: the whole frame, the largest band that fits, a cache band per worker for a sample
    // history under region access, or the caller's cap, whichever is smallest.
    const CheckedSize fixed = persistent + scratch + fixedRead;
    const CheckedSize perRow = tilePerRow + variableRead + termsPerRow + outputPerRow;
    if (!allValid({fixed, perRow, fixed + perRow}))
    {
        return overflowFailure();
    }
    const std::uint64_t fixedBytes = fixed.valueOr(0u);
    const std::uint64_t perRowBytes = perRow.valueOr(0u);
    std::uint32_t rows = extent.height();
    StackTileReason reason = StackTileReason::WholeFrameUnbounded;
    if (perRowBytes == 0u)
    {
        if (bounded && fixedBytes > available)
        {
            return budgetFailure("a streaming stack over whole frames stored at the tile width holds the "
                                 "whole-output accumulators and one decoded frame, and they exceed the available "
                                 "budget; region access bounds the read to a band",
                                 fixedBytes, available, budget);
        }
        reason = bounded ? StackTileReason::WholeFrameFits : StackTileReason::WholeFrameUnbounded;
    }
    else if (bounded)
    {
        const std::uint64_t oneRow = fixedBytes + perRowBytes;
        if (oneRow > available)
        {
            return budgetFailure("the combine pass cannot hold one row of every frame beside the persistent state, "
                                 "the scratch and the read buffer",
                                 oneRow, available, budget);
        }
        const std::uint64_t fit = (available - fixedBytes) / perRowBytes;
        if (fit >= height)
        {
            reason = StackTileReason::WholeFrameFits;
        }
        else
        {
            rows = static_cast<std::uint32_t>(fit);
            reason = StackTileReason::BudgetBound;
        }
    }
    // What the budget allows, before the bands below narrow it for the cache.
    const std::uint32_t affordableRows = rows;
    const StackTileReason affordableReason = reason;
    // A sample-history tile under region access is written by the source path and read back by
    // the column kernels. Each admitted worker needs a cache-sized band: a single band shared
    // by every worker can leave only a few rows of work and repeatedly wake a wide arena for
    // them. Keep complete rows, with one row per worker when a row exceeds the cache target;
    // the available-memory bound above and the caller cap below still decide smaller tiles.
    // A streaming tile gets no band here: its terms are folded in one pass whose frame loop is
    // inside the row loop, so an output row's terms stay cached across the frames however wide the
    // tile is. Its frames' bands are another matter when the source decodes them into the tile: the
    // fold reads them back after the last one has landed, so a tile wider than the last-level cache
    // is read from memory, and a caller whose source does that caps the tile.
    if (region && history)
    {
        const std::uint64_t persistentPerRow =
            (accumulators.valueOr(0u) + outputImage.valueOr(0u) + outputMaps.valueOr(0u)) / height +
            (tileOutput ? (outputImageRow + outputMapsRow).valueOr(0u) : 0u);
        const std::uint64_t touched = persistentPerRow + tilePerRow.valueOr(0u);
        const std::uint64_t perWorker = touched == 0u ? 0u : kStackCacheBandBytes / touched;
        // The target is one mebibyte and the worker count is 32-bit, so the product fits 64 bits.
        const std::uint64_t band = (perWorker == 0u ? 1u : perWorker) * workers;
        if (band < rows)
        {
            rows = static_cast<std::uint32_t>(band);
            reason = StackTileReason::CacheBand;
        }
    }
    // A source that decodes pieces spanning more rows than the tile - the subblocks of a compressed
    // file - decodes a piece again for every tile that reaches it, which costs more than the cache
    // band saves, so the tile is widened to a piece's rows, or to what the budget allows.
    if (region && request.sourceGranularityRows > rows)
    {
        if (request.sourceGranularityRows >= affordableRows)
        {
            rows = affordableRows;
            reason = affordableReason;
        }
        else
        {
            rows = request.sourceGranularityRows;
            reason = StackTileReason::SourceGranularity;
        }
    }
    if (request.maximumTileRows != 0u && request.maximumTileRows < rows)
    {
        rows = request.maximumTileRows;
        reason = StackTileReason::CallerRowCap;
    }
    const std::uint32_t tiles = static_cast<std::uint32_t>((height + rows - 1u) / rows);

    const CheckedSize tileBytes = tilePerRow * rows;
    const CheckedSize readBuffer = fixedRead + variableRead * rows;
    // The terms of one tile live from the first pass to the last like any other term; with no
    // measurement pass ahead of the combine, nothing before this point needed them counted.
    const CheckedSize tileTermBytes = termsPerRow * rows;
    const CheckedSize terms = accumulators + tileTermBytes;
    // So does the output's tile, which the working set takes with everything else before any pass;
    // a measurement pass already planned holds it too.
    const CheckedSize tileOutputBytes = outputPerRow * rows;
    const CheckedSize held = persistent + tileTermBytes + tileOutputBytes;
    if (!tileOutputBytes.isValid())
    {
        return overflowFailure();
    }
    for (std::size_t index = 0u; tileOutput && index < plan.passes.count; ++index)
    {
        StackPass& pass = plan.passes.passes[index];
        const CheckedSize working = CheckedSize{pass.workingSetBytes} + tileOutputBytes;
        if (!working.isValid())
        {
            return overflowFailure();
        }
        if (bounded && working.valueOr(0u) > available)
        {
            return budgetFailure("a measurement pass holds one whole frame and its workspace beside the persistent "
                                 "state and the output's tile, and they exceed the available budget",
                                 working.valueOr(0u), available, budget);
        }
        pass.workingSetBytes = working.valueOr(0u);
    }
    const CheckedSize combineWorking = held + scratch + tileBytes + readBuffer;
    const bool multiplied = history && !region;
    const CheckedSize combineReads = (history || region) ? CheckedSize{tiles} * frames : CheckedSize{frames};
    const CheckedSize combineInput = multiplied ? CheckedSize{tiles} * distinctInput : distinctInput;
    if (!allValid({tileBytes, readBuffer, tileTermBytes, terms, held, combineWorking, combineReads, combineInput,
                   distinctInput}))
    {
        return overflowFailure();
    }
    addPass(StackPass{.kind = StackPassKind::Combine,
                      .tileCount = tiles,
                      .frameReads = combineReads.valueOr(0u),
                      .inputBytes = combineInput.valueOr(0u),
                      .workingSetBytes = combineWorking.valueOr(0u)});
    const CheckedSize finalizeWorking = held + diagnosticsScratchBytes;
    if (!finalizeWorking.isValid())
    {
        return overflowFailure();
    }
    if (bounded && finalizeWorking.valueOr(0u) > available)
    {
        return budgetFailure("the final diagnostics and persistent state exceed the available budget",
                             finalizeWorking.valueOr(0u), available, budget);
    }
    addPass(StackPass{.kind = StackPassKind::Finalize,
                      .tileCount = 0u,
                      .frameReads = 0u,
                      .inputBytes = 0u,
                      .workingSetBytes = finalizeWorking.valueOr(0u)});

    // The totals.
    const std::uint64_t initializationScratchBytes = stackFrameWeightingWorkingBytes(model);
    const CheckedSize initialization = held + initializationScratchBytes;
    if (!initialization.isValid())
    {
        return overflowFailure();
    }
    if (bounded && initialization.valueOr(0u) > available)
    {
        return budgetFailure("frame weighting and persistent state exceed the available budget",
                             initialization.valueOr(0u), available, budget);
    }
    std::uint64_t peak = initialization.valueOr(0u);
    std::uint32_t inputPasses = 0u;
    CheckedSize totalReads{0u};
    CheckedSize totalInput{0u};
    for (std::size_t index = 0u; index < plan.passes.count; ++index)
    {
        const StackPass& pass = plan.passes.passes[index];
        peak = pass.workingSetBytes > peak ? pass.workingSetBytes : peak;
        inputPasses += pass.frameReads > 0u ? 1u : 0u;
        totalReads = totalReads + CheckedSize{pass.frameReads};
        totalInput = totalInput + CheckedSize{pass.inputBytes};
    }
    if (!allValid({totalReads, totalInput}))
    {
        return overflowFailure();
    }

    plan.strategy = StackStrategyPlan{.strategy = strategy, .reason = stackStrategyReasonOf(model)};
    plan.precision =
        StackPrecisionPlan{.inputSampleType = inputType,
                           .tileSampleType = tileType,
                           .outputSampleType = stack.output.sampleType,
                           .accumulation = model.accumulationPrecision(),
                           .justification = model.accumulationJustification(),
                           .reason = tileType == SampleType::Float64 ? StackPrecisionReason::StoredInFloat64
                                                                     : StackPrecisionReason::DefaultFloat32};
    plan.tile = StackTilePlan{.width = extent.width(),
                              .rows = rows,
                              .tileCount = tiles,
                              .requestedMaximumRows = request.maximumTileRows,
                              .reason = reason};
    plan.memory = StackMemoryPlan{.outputImageBytes = (tileOutput ? outputImageRow * rows : outputImage).valueOr(0u),
                                  .outputMaskBytes = (tileOutput ? outputMaskRow * rows : outputMask).valueOr(0u),
                                  .outputMapBytes = (tileOutput ? outputMapsRow * rows : outputMaps).valueOr(0u),
                                  .accumulatorBytes = terms.valueOr(0u),
                                  .frameStateBytes = frameState.valueOr(0u),
                                  .localWeightGridBytes = localWeightGrid.valueOr(0u),
                                  .preparedStateBytes = request.preparedStateBytes,
                                  .fittedNormalizationBytes = request.fittedNormalizationBytes,
                                  .persistentBytes = held.valueOr(0u),
                                  .scratchBytesPerWorker = scratchPerWorker,
                                  .workers = workers,
                                  .scratchBytes = scratch.valueOr(0u),
                                  .tileBytesPerRow = tilePerRow.valueOr(0u),
                                  .tileBytes = tileBytes.valueOr(0u),
                                  .readBufferBytes = readBuffer.valueOr(0u),
                                  .measurementWorkspaceBytes = measurementWorkspaceBytes,
                                  .normalizationScratchBytes = normalizationScratchBytes,
                                  .diagnosticsScratchBytes = diagnosticsScratchBytes,
                                  .initializationScratchBytes = initializationScratchBytes,
                                  .peakBytes = peak,
                                  .availableBytes = available,
                                  .budgetBytes = budget,
                                  .budgetBounded = bounded,
                                  .outputCoversOneTile = tileOutput};
    plan.io = StackIoPlan{.access = request.access,
                          .concurrentFrameReads = request.concurrentFrameReads,
                          .inputPasses = inputPasses,
                          .frameReads = totalReads.valueOr(0u),
                          .frameBytes = frameBytes.valueOr(0u),
                          .distinctInputBytes = distinctInput.valueOr(0u),
                          .logicalInputBytes = totalInput.valueOr(0u),
                          .readsPerFrame = (history || region) ? tiles : 1u,
                          .inputMultiplier = multiplied ? tiles : 1u};
    const ComputeBackend requested = execution.requestedBackend();
    plan.backend =
        StackBackendPlan{.requestedBackend = requested,
                         .selection = *selection,
                         .fellBack = requested != ComputeBackend::Automatic && requested != selection->backend,
                         .workers = workers,
                         .reductionDeterminism = execution.reductionDeterminism()};

    explainPlan(execution, plan, frames);
    return plan;
}

} // namespace an
