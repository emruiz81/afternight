// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack working set: every planned buffer taken by name, refused before
// it is taken when it cannot fit, and given back with its pass.
//
// Three things in this file are decisions rather than mechanics.
//
// **The plan is recomputed before it is trusted.** A plan is a value anybody can build or edit,
// and the executor that follows one writes through the buffers this set hands out at the sizes
// the plan states. So every figure - the six persistent buffers, the scratch, the tile, the read
// buffer, the passes and their working sets, the peak - is derived again from the model and the
// execution and compared with what the plan says: exactly, except that a measurement pass's
// working set is held to a floor - the persistent state, the whole-frame reads and, for the
// normalization pass, its scratch - because its workspace is whatever the pass states above that
// floor. The first disagreement is refused by name. That is what turns "the executor allocates
// against the plan" from a convention into a check.
//
// **The budget is checked before the resource is asked, and again by the resource.** The first
// check refuses a peak that does not fit what the context has left, with nothing taken; it is a
// snapshot, and a concurrent operation can still take memory between it and the allocations, so
// the resource's own refusal is handled too, by returning what this call took and naming the
// buffer. Neither path leaves a partial set behind.
//
// **Persistent buffers are zeroed and pass buffers are not.** An accumulator, a count, a map and
// a mask are added to, so they start from nothing; the output image starts from nothing so a
// sample no frame reached is a zero rather than the last thing that lived there. A tile, a read
// buffer, a worker's scratch and a measurement workspace are written by the pass that reads them,
// on every pass, and zeroing them would cost a memset of the whole working set per pass for no
// reader. The zeroing runs on the operation's workers: the persistent buffers are the largest
// fresh memory the stack takes, and clearing them on one thread would serialize their page faults
// and stores ahead of the first pass while the other workers wait.

#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMemoryRules.h>

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>

#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kCreate = "StackWorkingSet::create";
constexpr const char* kBeginPass = "StackWorkingSet::beginPass";
constexpr const char* kReleaseOutputImage = "StackWorkingSet::releaseOutputImage";
constexpr const char* kReleaseOutputMask = "StackWorkingSet::releaseOutputMask";
constexpr const char* kReleaseOutputMap = "StackWorkingSet::releaseOutputMap";

/// The bytes of one frame weight, a double, as the planner counts it.
constexpr std::uint64_t kFrameWeightBytes = sizeof(double);

/// The bytes of one global normalization coefficient pair, an offset and a scale, both doubles.
constexpr std::uint64_t kNormalizationCoefficientBytes = 2u * sizeof(double);

/// The bytes one worker clears at a time when the persistent buffers are zeroed: large enough that
/// scheduling a chunk costs nothing beside clearing it, small enough that a few megabytes still
/// spread over every worker.
constexpr std::uint64_t kZeroChunkBytes = 256u * 1024u;

/// What `std::printf` wants a sixty-four-bit count as.
using Count = unsigned long long;

/// A failure of one of this type's operations.
///
/// @param code why.
/// @param operation which operation.
/// @param message what a person reads.
/// @param context the typed facts, if any.
/// @return the failure.
[[nodiscard]] Status failure(ErrorCode code, const char* operation, std::string message,
                             std::vector<StatusContextField> context = {})
{
    return Status::failure(code, StatusDetail{.message = std::move(message),
                                              .operation = operation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = std::move(context)});
}

/// A refusal because something does not fit the budget, with the three numbers a caller acts on.
///
/// @param operation which operation.
/// @param message what does not fit.
/// @param requested the bytes that were needed.
/// @param available the bytes the engine context had left.
/// @param limit the configured budget.
/// @return the failure.
[[nodiscard]] Status budgetRefusal(const char* operation, std::string message, std::uint64_t requested,
                                   std::uint64_t available, std::uint64_t limit)
{
    return failure(ErrorCode::MemoryBudgetExceeded, operation, std::move(message),
                   {StatusContextField::ofCount(ContextKey::Requested, requested),
                    StatusContextField::ofCount(ContextKey::Available, available),
                    StatusContextField::ofCount(ContextKey::Limit, limit)});
}

/// A refusal because a figure of the plan is not what the model implies.
///
/// @param figure the plan member that disagrees, spelled as a caller reads it.
/// @return the failure, naming the member in its message and the plan as the parameter.
[[nodiscard]] Status planDisagrees(const std::string& figure)
{
    return failure(ErrorCode::IncompatibleArguments, kCreate,
                   "the plan does not describe this model's stack on this execution: " + figure +
                       " is not what the model implies",
                   {StatusContextField::ofText(ContextKey::Parameter, "plan")});
}

/// A refusal because a figure does not fit sixty-four bits.
///
/// @param operation which operation.
/// @return the failure.
[[nodiscard]] Status overflow(const char* operation)
{
    return failure(ErrorCode::LimitExceeded, operation,
                   "the working set's byte accounting is not representable in sixty-four bits");
}

/// The failure for a buffer the resource refused, carrying the resource's numbers and its refusal
/// as the cause.
///
/// @param operation which operation was taking it.
/// @param buffer which buffer.
/// @param cause what the resource said.
/// @return the failure, under the resource's own code.
[[nodiscard]] Status refusedBuffer(const char* operation, StackBuffer buffer, const Status& cause)
{
    std::vector<StatusContextField> context;
    if (cause.detail() != nullptr)
    {
        context = cause.detail()->context;
    }
    return Status::failure(
        cause.code(),
        StatusDetail{.message = std::string("the ") + stackBufferName(buffer) +
                                " buffer was refused and nothing of this call is held: " + cause.message(),
                     .operation = operation,
                     .domain = ErrorDomain::Algorithms,
                     .context = std::move(context)},
        cause);
}

/// Reports one formatted sentence to the operation's diagnostic sink, if it has one.
///
/// @param execution the operation.
/// @param operation which of this type's operations is speaking.
/// @param format a `printf` format; counts are passed as `Count`.
void say(const ExecutionContext& execution, const char* operation, const char* format, ...)
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
    execution.reportDiagnostic(DiagnosticSeverity::Info, ErrorDomain::Algorithms, operation, buffer);
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

/// The figures a model implies, recomputed from it so the plan can be checked against them.
struct StackFigures
{
    /// Columns in a frame.
    std::uint32_t width = 0u;

    /// Rows in a frame.
    std::uint32_t height = 0u;

    /// Channels in a frame.
    std::uint32_t channels = 0u;

    /// Frames in the stack.
    std::uint32_t frames = 0u;

    /// Samples in the output.
    std::uint64_t elements = 0u;

    /// What the frames are stored as.
    SampleType inputType = SampleType::Float32;

    /// What the tile holds a frame's samples as.
    SampleType tileType = SampleType::Float32;

    /// What the output is written as.
    SampleType outputType = SampleType::Float32;

    /// Bytes of one stored sample.
    std::uint64_t inputSampleBytes = 0u;

    /// Whether the samples of a pixel are held together for the combination.
    bool history = false;

    /// Whether the stack's normalization scope names a global method.
    bool globalMethod = false;

    /// Whether the stack applies a local weight surface.
    bool local = false;

    /// The output image's bytes.
    std::uint64_t outputImage = 0u;

    /// The output mask's bytes.
    std::uint64_t outputMask = 0u;

    /// Every requested map's bytes together.
    std::uint64_t outputMaps = 0u;

    /// Every kept accumulator's bytes together.
    std::uint64_t accumulators = 0u;

    /// Each accumulator's bytes, zero for one that is not kept.
    std::array<std::uint64_t, kStackAccumulatorCount> accumulatorTerms{};

    /// The per-frame weight table's bytes.
    std::uint64_t frameWeights = 0u;

    /// The per-frame coefficient table's bytes, zero without a global method.
    std::uint64_t frameCoefficients = 0u;

    /// The executor's per-frame bookkeeping bytes, which follow the two tables.
    std::uint64_t frameBookkeeping = 0u;

    /// The local-weight grids' bytes.
    std::uint64_t localWeightGrids = 0u;

    /// One decoded frame's bytes.
    std::uint64_t frameBytes = 0u;

    /// One row of the tile.
    std::uint64_t tilePerRow = 0u;

    /// One worker's scratch.
    std::uint64_t scratchPerWorker = 0u;
};

/// Recomputes what a model implies.
///
/// @param model the checked stack.
/// @param workers how many workers the execution was admitted, at least one, which sizes the
///        per-worker rows of the bookkeeping.
/// @param access how the plan's source serves frames.
/// @param tileRows the plan's tile rows, which size the accumulators when they hold one tile, and the
///        output when the caller takes it tile by tile.
/// @param outputOneTile whether the plan says the caller takes the output tile by tile.
/// @return the figures, or `ErrorCode::LimitExceeded` when one is not representable.
[[nodiscard]] Result<StackFigures> computeFigures(const StackModel& model, std::uint32_t workers,
                                                  StackSourceAccess access, std::uint32_t tileRows, bool outputOneTile)
{
    const StackRequest& request = model.request();
    const ImageExtent& extent = request.input.descriptor.extent();
    StackFigures figures;
    figures.width = extent.width();
    figures.height = extent.height();
    figures.channels = extent.channelCount();
    figures.frames = request.input.frameCount;
    figures.inputType = request.input.descriptor.sampleType();
    figures.tileType = stackTileSampleTypeOf(figures.inputType);
    figures.outputType = request.output.sampleType;
    figures.inputSampleBytes = sampleTypeSizeBytes(figures.inputType);
    figures.history = stackAccumulationStrategyOf(model) == StackAccumulationStrategy::TileSampleHistory;
    figures.globalMethod = stackNormalizationScopeUsesGlobalMethod(request.normalization.scope);
    figures.local = model.usesLocalWeights();
    figures.scratchPerWorker = stackScratchBytesPerWorker(model);

    const CheckedSize elements = CheckedSize{figures.width} * figures.height * figures.channels;
    // An output taken tile by tile is held for one tile's rows; otherwise for the whole output.
    const std::uint32_t outputRows = outputOneTile ? tileRows : figures.height;
    const CheckedSize outputElements = CheckedSize{figures.width} * outputRows * figures.channels;
    const CheckedSize outputImage = outputElements * sampleTypeSizeBytes(figures.outputType);
    const CheckedSize outputMask = CheckedSize{packedMaskRowBytes(figures.width)} * outputRows * figures.channels;
    const CheckedSize outputMaps = outputElements * kStackMapSampleBytes * stackOutputMapCount(request.output);
    CheckedSize accumulators{0u};
    std::array<CheckedSize, kStackAccumulatorCount> terms{};
    const CheckedSize termElements = stackAccumulatorsCoverOneTile(model, access)
                                         ? CheckedSize{figures.width} * tileRows * figures.channels
                                         : elements;
    for (std::size_t index = 0u; index < kStackAccumulatorCount; ++index)
    {
        const auto accumulator = static_cast<StackAccumulator>(index);
        if (stackAccumulatorKept(model, accumulator))
        {
            terms[index] = termElements * stackAccumulatorBytesPerElement(accumulator);
            accumulators = accumulators + terms[index];
        }
    }
    const CheckedSize frameWeights = CheckedSize{figures.frames} * figures.channels * kFrameWeightBytes;
    const CheckedSize frameCoefficients =
        figures.globalMethod ? CheckedSize{figures.frames} * figures.channels * kNormalizationCoefficientBytes
                             : CheckedSize{0u};
    CheckedSize localWeightGrids{0u};
    if (figures.local)
    {
        const std::uint32_t spacing = request.localWeighting.gridSpacing;
        localWeightGrids = CheckedSize{figures.frames} * localWeightGridNodesAlong(figures.width, spacing) *
                           localWeightGridNodesAlong(figures.height, spacing) * kLocalWeightNodeBytes;
    }
    const CheckedSize frameBytes = elements * figures.inputSampleBytes;
    // Every frame's row under sample history and under region access; one frame's widened row
    // under a whole-frame streaming strategy whose storage is not the tile width; nothing when the
    // accumulators read the decoded frame; plus the local weight surface's row of every band the
    // tile holds.
    const CheckedSize tileSampleRow =
        figures.history || isStackSourceRegionAccess(access)
            ? CheckedSize{figures.width} * figures.channels * figures.frames * sampleTypeSizeBytes(figures.tileType)
        : figures.inputType != figures.tileType
            ? CheckedSize{figures.width} * figures.channels * sampleTypeSizeBytes(figures.tileType)
            : CheckedSize{0u};
    const CheckedSize tilePerRow = tileSampleRow + CheckedSize{stackLocalWeightTileBytesPerRow(model, access)};
    const CheckedSize frameBookkeeping{stackFrameBookkeepingBytes(model, workers)};
    if (!allValid({elements, outputImage, outputMask, outputMaps, accumulators, frameWeights, frameCoefficients,
                   frameWeights + frameCoefficients + frameBookkeeping, localWeightGrids, frameBytes, tilePerRow}))
    {
        return overflow(kCreate);
    }

    figures.elements = elements.valueOr(0u);
    figures.outputImage = outputImage.valueOr(0u);
    figures.outputMask = outputMask.valueOr(0u);
    figures.outputMaps = outputMaps.valueOr(0u);
    figures.accumulators = accumulators.valueOr(0u);
    for (std::size_t index = 0u; index < kStackAccumulatorCount; ++index)
    {
        figures.accumulatorTerms[index] = terms[index].valueOr(0u);
    }
    figures.frameWeights = frameWeights.valueOr(0u);
    figures.frameCoefficients = frameCoefficients.valueOr(0u);
    figures.frameBookkeeping = frameBookkeeping.valueOr(0u);
    figures.localWeightGrids = localWeightGrids.valueOr(0u);
    figures.frameBytes = frameBytes.valueOr(0u);
    figures.tilePerRow = tilePerRow.valueOr(0u);
    return figures;
}

/// The name of one pass's member, for a refusal.
///
/// @param index which pass.
/// @param member which member.
/// @return `passes[index].member`.
[[nodiscard]] std::string passFigure(std::size_t index, const char* member)
{
    return "passes[" + std::to_string(index) + "]." + member;
}

/// Whether a plan describes a model's stack on an execution.
///
/// Every figure the plan states is compared with what the model implies, and the passes are
/// checked for order, kind and working set. The first disagreement is refused by name.
///
/// @param figures what the model implies.
/// @param model the checked stack.
/// @param plan the plan to check.
/// @param admitted the workers the execution was admitted.
/// @return success, or `ErrorCode::IncompatibleArguments` naming the figure, or
///         `ErrorCode::LimitExceeded` when a figure of the check is not representable.
[[nodiscard]] Status checkPlan(const StackFigures& figures, const StackModel& model, const StackPlan& plan,
                               std::uint32_t admitted)
{
    if (plan.strategy.strategy != stackAccumulationStrategyOf(model))
    {
        return planDisagrees("strategy.strategy");
    }
    if (plan.precision.inputSampleType != figures.inputType)
    {
        return planDisagrees("precision.inputSampleType");
    }
    if (plan.precision.tileSampleType != figures.tileType)
    {
        return planDisagrees("precision.tileSampleType");
    }
    if (plan.precision.outputSampleType != figures.outputType)
    {
        return planDisagrees("precision.outputSampleType");
    }
    if (plan.tile.width != figures.width)
    {
        return planDisagrees("tile.width");
    }
    if (plan.tile.rows == 0u || plan.tile.rows > figures.height)
    {
        return planDisagrees("tile.rows");
    }
    const std::uint64_t tiles = (static_cast<std::uint64_t>(figures.height) + plan.tile.rows - 1u) / plan.tile.rows;
    if (plan.tile.tileCount != tiles)
    {
        return planDisagrees("tile.tileCount");
    }
    const std::uint32_t workers = admitted > 0u ? admitted : 1u;
    if (plan.memory.workers != workers)
    {
        return planDisagrees("memory.workers");
    }
    if (plan.backend.workers != plan.memory.workers)
    {
        return planDisagrees("backend.workers");
    }
    if (!isStackSourceAccess(plan.io.access))
    {
        return planDisagrees("io.access");
    }
    if (plan.io.concurrentFrameReads == 0u)
    {
        return planDisagrees("io.concurrentFrameReads");
    }
    if (plan.io.frameBytes != figures.frameBytes)
    {
        return planDisagrees("io.frameBytes");
    }
    if (plan.memory.outputCoversOneTile && !stackOutputCanCoverOneTile(model, plan.io.access))
    {
        return planDisagrees("memory.outputCoversOneTile");
    }

    const StackMemoryPlan& memory = plan.memory;
    if (model.usesRejection() && memory.diagnosticsScratchBytes <
                                     static_cast<std::uint64_t>(model.request().input.frameCount) * 2u * sizeof(double))
    {
        return planDisagrees("memory.diagnosticsScratchBytes");
    }
    if (memory.outputImageBytes != figures.outputImage)
    {
        return planDisagrees("memory.outputImageBytes");
    }
    if (memory.outputMaskBytes != figures.outputMask)
    {
        return planDisagrees("memory.outputMaskBytes");
    }
    if (memory.outputMapBytes != figures.outputMaps)
    {
        return planDisagrees("memory.outputMapBytes");
    }
    if (memory.accumulatorBytes != figures.accumulators)
    {
        return planDisagrees("memory.accumulatorBytes");
    }
    if (memory.frameStateBytes != figures.frameWeights + figures.frameCoefficients + figures.frameBookkeeping)
    {
        return planDisagrees("memory.frameStateBytes");
    }
    if (memory.localWeightGridBytes != figures.localWeightGrids)
    {
        return planDisagrees("memory.localWeightGridBytes");
    }
    const CheckedSize persistent = CheckedSize{figures.outputImage} + figures.outputMask + figures.outputMaps +
                                   figures.accumulators + figures.frameWeights + figures.frameCoefficients +
                                   figures.frameBookkeeping + figures.localWeightGrids + memory.preparedStateBytes +
                                   memory.fittedNormalizationBytes;
    const CheckedSize scratch = CheckedSize{figures.scratchPerWorker} * workers;
    const CheckedSize tile = CheckedSize{figures.tilePerRow} * plan.tile.rows;
    const CheckedSize wholeRead = CheckedSize{figures.frameBytes} * plan.io.concurrentFrameReads;
    const CheckedSize readBuffer = isStackSourceRegionAccess(plan.io.access)
                                       ? CheckedSize{figures.width} * figures.channels * figures.inputSampleBytes *
                                             plan.io.concurrentFrameReads * plan.tile.rows
                                       : wholeRead;
    const CheckedSize combineWorking = persistent + scratch + tile + readBuffer;
    const CheckedSize measureFloor = persistent + wholeRead;
    if (!allValid({persistent, scratch, tile, wholeRead, readBuffer, combineWorking, measureFloor}))
    {
        return overflow(kCreate);
    }
    if (memory.persistentBytes != persistent.valueOr(0u))
    {
        return planDisagrees("memory.persistentBytes");
    }
    if (memory.scratchBytesPerWorker != figures.scratchPerWorker)
    {
        return planDisagrees("memory.scratchBytesPerWorker");
    }
    if (memory.scratchBytes != scratch.valueOr(0u))
    {
        return planDisagrees("memory.scratchBytes");
    }
    if (memory.tileBytesPerRow != figures.tilePerRow)
    {
        return planDisagrees("memory.tileBytesPerRow");
    }
    if (memory.tileBytes != tile.valueOr(0u))
    {
        return planDisagrees("memory.tileBytes");
    }
    if (memory.readBufferBytes != readBuffer.valueOr(0u))
    {
        return planDisagrees("memory.readBufferBytes");
    }

    const StackPassPlan& passes = plan.passes;
    if (passes.count < 2u || passes.count > kMaxStackPasses)
    {
        return planDisagrees("passes.count");
    }
    const std::size_t last = passes.count - 1u;
    const std::size_t combineIndex = passes.count - 2u;
    if (passes.passes[last].kind != StackPassKind::Finalize)
    {
        return planDisagrees(passFigure(last, "kind"));
    }
    if (passes.passes[last].workingSetBytes != (persistent + memory.diagnosticsScratchBytes).valueOr(0u))
    {
        return planDisagrees(passFigure(last, "workingSetBytes"));
    }
    if (passes.passes[combineIndex].kind != StackPassKind::Combine)
    {
        return planDisagrees(passFigure(combineIndex, "kind"));
    }
    if (passes.passes[combineIndex].workingSetBytes != combineWorking.valueOr(0u))
    {
        return planDisagrees(passFigure(combineIndex, "workingSetBytes"));
    }
    unsigned previous = 0u;
    for (std::size_t index = 0u; index < combineIndex; ++index)
    {
        const StackPass& pass = passes.passes[index];
        const bool measurement =
            pass.kind == StackPassKind::MeasureNormalization || pass.kind == StackPassKind::MeasureLocalWeights;
        const unsigned order = static_cast<unsigned>(pass.kind) + 1u;
        if (!measurement || order <= previous)
        {
            return planDisagrees(passFigure(index, "kind"));
        }
        previous = order;
        if (pass.kind == StackPassKind::MeasureNormalization &&
            stackNormalizationScopeUsesGlobalMethod(model.request().normalization.scope))
        {
            const auto required = globalNormalizationWorkingBytes(
                figures.width, figures.height, stackTileSampleTypeOf(model.request().input.descriptor.sampleType()),
                workers);
            if (!required.hasValue() || memory.normalizationScratchBytes < *required)
            {
                return planDisagrees("memory.normalizationScratchBytes");
            }
        }
        const CheckedSize floor =
            measureFloor + (pass.kind == StackPassKind::MeasureNormalization ? memory.normalizationScratchBytes : 0u);
        if (!floor.isValid() || pass.workingSetBytes < floor.valueOr(0u))
        {
            return planDisagrees(passFigure(index, "workingSetBytes"));
        }
    }
    const CheckedSize initialization = persistent + memory.initializationScratchBytes;
    if (!initialization.isValid() || memory.initializationScratchBytes != stackFrameWeightingWorkingBytes(model))
    {
        return planDisagrees("memory.initializationScratchBytes");
    }
    std::uint64_t peak = initialization.valueOr(0u);
    for (std::size_t index = 0u; index < passes.count; ++index)
    {
        const std::uint64_t working = passes.passes[index].workingSetBytes;
        peak = working > peak ? working : peak;
    }
    if (memory.peakBytes != peak)
    {
        return planDisagrees("memory.peakBytes");
    }
    return Status::success();
}

/// One allocation from the engine context's resource.
struct RawBuffer
{
    /// The memory, or null when nothing is held.
    void* data = nullptr;

    /// How many bytes it is.
    std::uint64_t bytes = 0u;
};

/// A typed span over a raw buffer.
///
/// @tparam ElementT the type the bytes are read as.
/// @param raw the buffer.
/// @return the span, empty when nothing is held.
template <typename ElementT> [[nodiscard]] std::span<ElementT> spanOf(const RawBuffer& raw) noexcept
{
    if (raw.data == nullptr)
    {
        return {};
    }
    return std::span<ElementT>(static_cast<ElementT*>(raw.data),
                               static_cast<std::size_t>(raw.bytes / sizeof(ElementT)));
}

/// What one pass takes.
struct PassNeeds
{
    /// Every worker's scratch together.
    std::uint64_t scratch = 0u;

    /// The tile.
    std::uint64_t tile = 0u;

    /// The read buffer.
    std::uint64_t read = 0u;

    /// The measurement workspace.
    std::uint64_t workspace = 0u;
};

} // namespace

const char* stackBufferName(StackBuffer buffer) noexcept
{
    switch (buffer)
    {
    case StackBuffer::OutputImage:
        return "OutputImage";
    case StackBuffer::OutputMask:
        return "OutputMask";
    case StackBuffer::OutputMaps:
        return "OutputMaps";
    case StackBuffer::Accumulators:
        return "Accumulators";
    case StackBuffer::FrameState:
        return "FrameState";
    case StackBuffer::LocalWeightGrids:
        return "LocalWeightGrids";
    case StackBuffer::WorkerScratch:
        return "WorkerScratch";
    case StackBuffer::Tile:
        return "Tile";
    case StackBuffer::ReadBuffer:
        return "ReadBuffer";
    case StackBuffer::MeasurementWorkspace:
        return "MeasurementWorkspace";
    }
    return "UnknownStackBuffer";
}

const char* stackBufferLifetimeName(StackBufferLifetime lifetime) noexcept
{
    switch (lifetime)
    {
    case StackBufferLifetime::Persistent:
        return "Persistent";
    case StackBufferLifetime::Pass:
        return "Pass";
    }
    return "UnknownStackBufferLifetime";
}

const char* stackOutputMapName(StackOutputMap map) noexcept
{
    switch (map)
    {
    case StackOutputMap::RejectionLow:
        return "RejectionLow";
    case StackOutputMap::RejectionHigh:
        return "RejectionHigh";
    case StackOutputMap::RejectionMerged:
        return "RejectionMerged";
    case StackOutputMap::Coverage:
        return "Coverage";
    case StackOutputMap::Variance:
        return "Variance";
    case StackOutputMap::SignalToNoiseGain:
        return "SignalToNoiseGain";
    case StackOutputMap::WeightSum:
        return "WeightSum";
    }
    return "UnknownStackOutputMap";
}

const char* stackAccumulatorName(StackAccumulator accumulator) noexcept
{
    switch (accumulator)
    {
    case StackAccumulator::Sums:
        return "Sums";
    case StackAccumulator::Counts:
        return "Counts";
    case StackAccumulator::WeightSums:
        return "WeightSums";
    case StackAccumulator::WeightSquareSums:
        return "WeightSquareSums";
    case StackAccumulator::VarianceMoments:
        return "VarianceMoments";
    }
    return "UnknownStackAccumulator";
}

/// The buffers, the accounting and the hold on the engine context.
///
/// Defined here rather than in the header so that the published surface says nothing about how a
/// set is laid out. The lease is the first member so that it is the last destroyed: every storage
/// and every raw buffer goes back to the resource before the hold on the context that owns it is
/// released.
class StackWorkingSet::State
{
public:
    /// Takes ownership of the hold and remembers the plan, taking nothing yet.
    ///
    /// @param lease the hold on the engine context.
    /// @param resource the context's memory resource.
    /// @param contextId the context's identity.
    /// @param limits the context's configured limits, whose budget is the bound.
    /// @param model the checked stack.
    /// @param plan the plan, already checked against the model.
    /// @param figures what the model implies, already checked against the plan.
    /// @param baselineLive what the resource had outstanding before this set took anything.
    /// @param available what the resource had left, or the unbounded default.
    State(EngineContextLease lease, MemoryResource& resource, std::uint64_t contextId, const EngineLimits& limits,
          const StackModel& model, const StackPlan& plan, const StackFigures& figures, std::uint64_t baselineLive,
          std::uint64_t available) noexcept
        : m_lease(std::move(lease)), m_resource(&resource), m_contextId(contextId), m_limits(limits),
          m_bounded(limits.memoryBudgetBytes != kDefaultMemoryBudgetBytes), m_output(model.request().output),
          m_outputDescriptor(model.outputDescriptor()), m_plan(plan), m_figures(figures),
          m_wholeReadBytes(plan.io.frameBytes * plan.io.concurrentFrameReads), m_baselineLiveBytes(baselineLive),
          m_availableBytes(available)
    {
    }

    /// Returns every buffer still held.
    ~State()
    {
        returnPassBuffers();
        for (std::size_t index = 0u; index < kStackAccumulatorCount; ++index)
        {
            give(StackBuffer::Accumulators, m_accumulators[index]);
        }
        give(StackBuffer::FrameState, m_frameState);
        give(StackBuffer::LocalWeightGrids, m_localWeightGrids);
    }

    /// Neither copied nor moved: the handle moves, and this stays where it is.
    State(const State&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State& operator=(const State&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State(State&&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State& operator=(State&&) = delete;

    /// Takes the persistent buffers, in `StackBuffer` order, leaving all but the mask for
    /// `zeroPersistent` to clear.
    ///
    /// @param engine the context the storages are built from.
    /// @return success, or the first refusal, naming its buffer; what was taken before it is
    ///         returned by the destructor.
    [[nodiscard]] Status takePersistent(const EngineContext& engine)
    {
        // An output the caller takes tile by tile is held for one tile's rows, packed the same way.
        const ImageExtent& whole = m_outputDescriptor.extent();
        const std::optional<ImageExtent> tileExtent =
            m_plan.memory.outputCoversOneTile
                ? ImageExtent::create(whole.width(), m_plan.tile.rows, whole.channelCount(), m_limits)
                : std::optional<ImageExtent>(whole);
        std::optional<ImageLayoutRequest> imageRequest =
            layoutRequestFor(m_outputDescriptor, StorageArrangement::Planar, 1u);
        if (!imageRequest.has_value() || !tileExtent.has_value())
        {
            return failure(ErrorCode::Internal, kCreate, "the output description cannot be laid out plane by plane");
        }
        const ImageExtent& extent = *tileExtent;
        imageRequest->extent = extent;
        const std::optional<ImageLayout> imageLayout = ImageLayout::create(*imageRequest, m_limits);
        if (!imageLayout.has_value() || imageLayout->totalBytes() != m_plan.memory.outputImageBytes)
        {
            return failure(ErrorCode::LimitExceeded, kCreate,
                           "the output image's packed layout is not representable under the engine's limits");
        }
        Result<ImageStorageBytes> image = ImageStorageBytes::create(
            engine, *imageLayout, StorageInitialization::Uninitialized, kDefaultBufferAlignmentBytes);
        if (!image.hasValue())
        {
            ++m_refusals;
            return refusedBuffer(kCreate, StackBuffer::OutputImage, image.status());
        }
        m_outputImage = std::move(*image);
        record(StackBuffer::OutputImage, m_outputImage.capacityBytes(), 1u);

        Result<ValidityMask> mask = ValidityMask::create(engine, extent);
        if (!mask.hasValue())
        {
            ++m_refusals;
            return refusedBuffer(kCreate, StackBuffer::OutputMask, mask.status());
        }
        m_outputMask = std::move(*mask);
        record(StackBuffer::OutputMask, m_outputMask.capacityBytes(), 1u);

        const ImageLayoutRequest mapRequest{.extent = extent,
                                            .elementSizeBytes = static_cast<std::uint32_t>(kStackMapSampleBytes),
                                            .samplesPerPixel = 1u,
                                            .planeCount = extent.channelCount(),
                                            .rowAlignmentBytes = 1u};
        const std::optional<ImageLayout> mapLayout = ImageLayout::create(mapRequest, m_limits);
        for (std::size_t index = 0u; index < kStackOutputMapCount; ++index)
        {
            if (!stackOutputMapRequested(m_output, static_cast<StackOutputMap>(index)))
            {
                continue;
            }
            if (!mapLayout.has_value())
            {
                return failure(ErrorCode::LimitExceeded, kCreate,
                               "a map's packed layout is not representable under the engine's limits");
            }
            Result<ImageStorageBytes> map = ImageStorageBytes::create(
                engine, *mapLayout, StorageInitialization::Uninitialized, kDefaultBufferAlignmentBytes);
            if (!map.hasValue())
            {
                ++m_refusals;
                return refusedBuffer(kCreate, StackBuffer::OutputMaps, map.status());
            }
            m_maps[index] = std::move(*map);
            record(StackBuffer::OutputMaps, m_maps[index].capacityBytes(), 1u);
        }

        for (std::size_t index = 0u; index < kStackAccumulatorCount; ++index)
        {
            const Status taken =
                take(kCreate, StackBuffer::Accumulators, m_figures.accumulatorTerms[index], m_accumulators[index]);
            if (taken.failed())
            {
                return taken;
            }
        }
        const Status frameState =
            take(kCreate, StackBuffer::FrameState,
                 m_figures.frameWeights + m_figures.frameCoefficients + m_figures.frameBookkeeping, m_frameState);
        if (frameState.failed())
        {
            return frameState;
        }
        return take(kCreate, StackBuffer::LocalWeightGrids, m_figures.localWeightGrids, m_localWeightGrids);
    }

    /// Zeroes every persistent buffer `takePersistent` left uncleared, on the execution's workers.
    ///
    /// The buffers are cut into chunks of `kZeroChunkBytes` counted from each buffer's start, so
    /// where a buffer is cut does not depend on how many workers there are. Every byte of each
    /// allocation is cleared, padding included, which is what a zeroed storage promises.
    ///
    /// @param execution the operation whose workers clear the buffers.
    /// @return success, or the execution's cancellation; what was taken is returned by the
    ///         destructor.
    [[nodiscard]] Status zeroPersistent(const ExecutionContext& execution)
    {
        std::array<std::span<std::byte>, 3u + kStackOutputMapCount + kStackAccumulatorCount> ranges{};
        std::size_t count = 0u;
        const auto add = [&](void* data, std::uint64_t bytes) {
            if (data != nullptr && bytes != 0u)
            {
                ranges[count++] = std::span<std::byte>(static_cast<std::byte*>(data), static_cast<std::size_t>(bytes));
            }
        };
        add(m_outputImage.data(), m_outputImage.capacityBytes());
        for (ImageStorageBytes& map : m_maps)
        {
            if (map.valid())
            {
                add(map.data(), map.capacityBytes());
            }
        }
        for (const RawBuffer& accumulator : m_accumulators)
        {
            add(accumulator.data, accumulator.bytes);
        }
        add(m_frameState.data, m_frameState.bytes);
        add(m_localWeightGrids.data, m_localWeightGrids.bytes);

        // The first chunk of every range, and one past the last chunk of the last.
        std::array<std::uint64_t, ranges.size() + 1u> firstChunk{};
        for (std::size_t index = 0u; index < count; ++index)
        {
            firstChunk[index + 1u] =
                firstChunk[index] + (ranges[index].size() + kZeroChunkBytes - 1u) / kZeroChunkBytes;
        }
        const TaskCost cost{.bytesPerItem = kZeroChunkBytes, .operationsPerItem = kZeroChunkBytes / sizeof(double)};
        return parallelFor(execution, firstChunk[count], cost, [&](TaskRange chunks) -> Status {
            std::size_t range = 0u;
            for (std::uint64_t chunk = chunks.begin; chunk < chunks.end; ++chunk)
            {
                while (chunk >= firstChunk[range + 1u])
                {
                    ++range;
                }
                const std::uint64_t offset = (chunk - firstChunk[range]) * kZeroChunkBytes;
                const std::uint64_t bytes = (std::min)(kZeroChunkBytes, ranges[range].size() - offset);
                std::memset(ranges[range].data() + offset, 0, static_cast<std::size_t>(bytes));
            }
            return Status::success();
        });
    }

    /// Says what the creation took.
    ///
    /// @param execution the operation, whose sink receives the sentence.
    void explainCreation(const ExecutionContext& execution) const
    {
        if (m_bounded)
        {
            say(execution, kCreate,
                "Working set: took %llu persistent bytes in %u allocations (%llu planned, of which %llu prepared "
                "and held by the caller) against %llu available; the plan's peak of %llu fits.",
                static_cast<Count>(m_heldBytes), totalAllocations(), static_cast<Count>(m_plan.memory.persistentBytes),
                static_cast<Count>(m_plan.memory.preparedStateBytes), static_cast<Count>(m_availableBytes),
                static_cast<Count>(m_plan.memory.peakBytes));
        }
        else
        {
            say(execution, kCreate,
                "Working set: took %llu persistent bytes in %u allocations (%llu planned, of which %llu prepared "
                "and held by the caller) with no budget bound; the plan's peak is %llu.",
                static_cast<Count>(m_heldBytes), totalAllocations(), static_cast<Count>(m_plan.memory.persistentBytes),
                static_cast<Count>(m_plan.memory.preparedStateBytes), static_cast<Count>(m_plan.memory.peakBytes));
        }
    }

    /// Takes the next planned pass's buffers. See `StackWorkingSet::beginPass`.
    ///
    /// @param execution the operation.
    /// @param kind the pass to begin.
    /// @return success or the refusal.
    [[nodiscard]] Status beginPass(const ExecutionContext& execution, StackPassKind kind)
    {
        if (!execution.valid())
        {
            return failure(ErrorCode::NotInitialized, kBeginPass, "the execution context holds nothing");
        }
        if (execution.engineContextId() != m_contextId)
        {
            return failure(ErrorCode::InvalidArgument, kBeginPass,
                           "the execution runs in another engine context than the one this set's buffers come from",
                           {StatusContextField::ofText(ContextKey::Parameter, "execution")});
        }
        if (m_passOpen)
        {
            return failure(ErrorCode::InvalidState, kBeginPass,
                           std::string("the ") + stackPassKindName(m_currentPass) +
                               " pass is open; end it before beginning another");
        }
        if (m_passIndex >= m_plan.passes.count)
        {
            return failure(ErrorCode::InvalidState, kBeginPass, "every planned pass has run");
        }
        const StackPassKind expected = m_plan.passes.passes[m_passIndex].kind;
        if (kind != expected)
        {
            return failure(ErrorCode::InvalidState, kBeginPass,
                           std::string("the plan's next pass is ") + stackPassKindName(expected) + ", not " +
                               stackPassKindName(kind),
                           {StatusContextField::ofText(ContextKey::Parameter, "kind")});
        }
        const Status alive = execution.checkCancellation();
        if (alive.failed())
        {
            return alive;
        }

        const PassNeeds needs = needsOf(m_passIndex);
        const CheckedSize total = CheckedSize{needs.scratch} + needs.tile + needs.read + needs.workspace;
        if (!total.isValid())
        {
            return overflow(kBeginPass);
        }
        if (m_bounded)
        {
            const std::uint64_t live = m_resource->counters().liveBytes;
            const std::uint64_t available = m_limits.memoryBudgetBytes > live ? m_limits.memoryBudgetBytes - live : 0u;
            if (total.valueOr(0u) > available)
            {
                ++m_refusals;
                return budgetRefusal(kBeginPass,
                                     std::string("the ") + stackPassKindName(kind) +
                                         " pass's buffers do not fit what the engine context has left; nothing "
                                         "was taken",
                                     total.valueOr(0u), available, m_limits.memoryBudgetBytes);
            }
        }

        Status taken = take(kBeginPass, StackBuffer::WorkerScratch, needs.scratch, m_scratch);
        if (taken.ok())
        {
            taken = take(kBeginPass, StackBuffer::Tile, needs.tile, m_tile);
        }
        if (taken.ok())
        {
            taken = take(kBeginPass, StackBuffer::ReadBuffer, needs.read, m_readBuffer);
        }
        if (taken.ok())
        {
            taken = take(kBeginPass, StackBuffer::MeasurementWorkspace, needs.workspace, m_workspace);
        }
        if (taken.failed())
        {
            returnPassBuffers();
            return taken;
        }
        m_passOpen = true;
        m_currentPass = kind;
        say(execution, kBeginPass,
            "Pass %u of %u, %s: took %llu bytes in %u allocations - %llu scratch, %llu tile, %llu read buffer, %llu "
            "workspace; %llu bytes held of a planned %llu for this pass.",
            static_cast<unsigned>(m_passIndex + 1u), static_cast<unsigned>(m_plan.passes.count),
            stackPassKindName(kind), static_cast<Count>(total.valueOr(0u)), passAllocations(),
            static_cast<Count>(needs.scratch), static_cast<Count>(needs.tile), static_cast<Count>(needs.read),
            static_cast<Count>(needs.workspace), static_cast<Count>(m_heldBytes),
            static_cast<Count>(m_plan.passes.passes[m_passIndex].workingSetBytes - m_plan.memory.preparedStateBytes));
        return Status::success();
    }

    /// Returns the open pass's buffers. See `StackWorkingSet::endPass`.
    void endPass() noexcept
    {
        if (!m_passOpen)
        {
            return;
        }
        returnPassBuffers();
        m_passOpen = false;
        ++m_passIndex;
    }

    /// Which pass is open.
    ///
    /// @return the pass, or nothing.
    [[nodiscard]] std::optional<StackPassKind> currentPass() const noexcept
    {
        if (!m_passOpen)
        {
            return std::nullopt;
        }
        return m_currentPass;
    }

    /// How many passes have begun and ended.
    ///
    /// @return the count.
    [[nodiscard]] std::uint32_t passesCompleted() const noexcept
    {
        return static_cast<std::uint32_t>(m_passIndex);
    }

    /// Which context's budget the buffers belong to.
    ///
    /// @return the identity.
    [[nodiscard]] std::uint64_t contextId() const noexcept
    {
        return m_contextId;
    }

    /// The plan the set was built against.
    ///
    /// @return the plan.
    [[nodiscard]] const StackPlan& plan() const noexcept
    {
        return m_plan;
    }

    /// What this set has taken, against what its plan said.
    ///
    /// @return the account.
    [[nodiscard]] StackMemoryAccount account() const noexcept
    {
        StackMemoryAccount account;
        const std::size_t passIndex = m_passOpen ? m_passIndex : m_plan.passes.count - 2u;
        const PassNeeds needs = needsOf(passIndex);
        const std::array<std::uint64_t, kStackBufferCount> planned{
            m_plan.memory.outputImageBytes,
            m_plan.memory.outputMaskBytes,
            m_plan.memory.outputMapBytes,
            m_plan.memory.accumulatorBytes,
            m_plan.memory.frameStateBytes,
            m_plan.memory.localWeightGridBytes,
            needs.scratch,
            needs.tile,
            needs.read,
            needs.workspace,
        };
        for (std::size_t index = 0u; index < kStackBufferCount; ++index)
        {
            const auto buffer = static_cast<StackBuffer>(index);
            account.buffers[index] = StackBufferAccount{.buffer = buffer,
                                                        .lifetime = stackBufferLifetimeOf(buffer),
                                                        .plannedBytes = planned[index],
                                                        .heldBytes = m_held[index],
                                                        .allocations = m_allocations[index]};
        }
        account.plannedPersistentBytes = m_plan.memory.persistentBytes;
        account.plannedPeakBytes = m_plan.memory.peakBytes;
        account.preparedStateBytes = m_plan.memory.preparedStateBytes;
        account.baselineLiveBytes = m_baselineLiveBytes;
        account.availableBytes = m_availableBytes;
        account.heldBytes = m_heldBytes;
        account.peakHeldBytes = m_peakHeldBytes;
        account.handedOverBytes = m_handedOverBytes;
        account.refusals = m_refusals;
        account.passOpen = m_passOpen;
        account.currentPass = m_passOpen ? m_currentPass : StackPassKind::Combine;
        account.passesCompleted = static_cast<std::uint32_t>(m_passIndex);
        return account;
    }

    /// The combined image's samples.
    ///
    /// @return the bytes, or nothing.
    [[nodiscard]] std::span<std::byte> outputImage() noexcept
    {
        if (!m_outputImage.valid())
        {
            return {};
        }
        return std::span<std::byte>(static_cast<std::byte*>(m_outputImage.data()),
                                    static_cast<std::size_t>(m_outputImage.capacityBytes()));
    }

    /// Where the combined image's samples sit.
    ///
    /// @return the layout, or nothing.
    [[nodiscard]] std::optional<ImageLayout> outputImageLayout() const noexcept
    {
        return m_outputImage.layout();
    }

    /// The output's validity mask.
    ///
    /// @return a view, or nothing.
    [[nodiscard]] std::optional<ValidityMaskView<std::uint8_t>> outputMask() noexcept
    {
        return m_outputMask.view();
    }

    /// One map's storage, when it is requested, held and of the asked-for class.
    ///
    /// @param map the map.
    /// @param sampleType the class the caller wants it as.
    /// @return the storage, or null.
    [[nodiscard]] ImageStorageBytes* mapStorage(StackOutputMap map, SampleType sampleType) noexcept
    {
        if (!isStackOutputMap(map) || stackOutputMapSampleType(map) != sampleType)
        {
            return nullptr;
        }
        ImageStorageBytes& storage = m_maps[static_cast<std::size_t>(map)];
        return storage.valid() ? &storage : nullptr;
    }

    /// One accumulator.
    ///
    /// @param accumulator which.
    /// @return the raw buffer.
    [[nodiscard]] const RawBuffer& accumulator(StackAccumulator accumulator) const noexcept
    {
        return m_accumulators[static_cast<std::size_t>(accumulator)];
    }

    /// The per-frame weights.
    ///
    /// @return the table, or nothing.
    [[nodiscard]] std::span<double> frameWeights() noexcept
    {
        if (m_frameState.data == nullptr)
        {
            return {};
        }
        return spanOf<double>(m_frameState)
            .subspan(0u, static_cast<std::size_t>(m_figures.frameWeights / sizeof(double)));
    }

    /// The per-frame normalization coefficients.
    ///
    /// @return the table, or nothing.
    [[nodiscard]] std::span<double> frameNormalization() noexcept
    {
        if (m_frameState.data == nullptr || m_figures.frameCoefficients == 0u)
        {
            return {};
        }
        return spanOf<double>(m_frameState)
            .subspan(static_cast<std::size_t>(m_figures.frameWeights / sizeof(double)),
                     static_cast<std::size_t>(m_figures.frameCoefficients / sizeof(double)));
    }

    /// The executor's per-frame bookkeeping, after the two tables.
    ///
    /// @return the bytes, or nothing.
    [[nodiscard]] std::span<std::byte> frameBookkeeping() noexcept
    {
        if (m_frameState.data == nullptr || m_figures.frameBookkeeping == 0u)
        {
            return {};
        }
        return spanOf<std::byte>(m_frameState)
            .subspan(static_cast<std::size_t>(m_figures.frameWeights + m_figures.frameCoefficients),
                     static_cast<std::size_t>(m_figures.frameBookkeeping));
    }

    /// The local-weight grids.
    ///
    /// @return the grids, or nothing.
    [[nodiscard]] std::span<float> localWeightGrids() noexcept
    {
        return spanOf<float>(m_localWeightGrids);
    }

    /// One worker's scratch.
    ///
    /// @param worker which worker.
    /// @return the bytes, or nothing.
    [[nodiscard]] std::span<std::byte> workerScratch(std::uint32_t worker) noexcept
    {
        const std::uint64_t perWorker = m_plan.memory.scratchBytesPerWorker;
        if (m_scratch.data == nullptr || worker >= m_plan.memory.workers || perWorker == 0u)
        {
            return {};
        }
        return spanOf<std::byte>(m_scratch).subspan(static_cast<std::size_t>(worker * perWorker),
                                                    static_cast<std::size_t>(perWorker));
    }

    /// The tile.
    ///
    /// @return the bytes, or nothing.
    [[nodiscard]] std::span<std::byte> tile() noexcept
    {
        return spanOf<std::byte>(m_tile);
    }

    /// The read buffer.
    ///
    /// @return the bytes, or nothing.
    [[nodiscard]] std::span<std::byte> readBuffer() noexcept
    {
        return spanOf<std::byte>(m_readBuffer);
    }

    /// The measurement workspace.
    ///
    /// @return the bytes, or nothing.
    [[nodiscard]] std::span<std::byte> measurementWorkspace() noexcept
    {
        return spanOf<std::byte>(m_workspace);
    }

    /// Hands the output image over.
    ///
    /// @return the storage, or the refusal.
    [[nodiscard]] Result<ImageStorageBytes> releaseOutputImage()
    {
        if (!m_outputImage.valid())
        {
            return failure(ErrorCode::InvalidState, kReleaseOutputImage, "the output image was already handed over");
        }
        handOver(StackBuffer::OutputImage, m_outputImage.capacityBytes());
        return std::move(m_outputImage);
    }

    /// Hands the output mask over.
    ///
    /// @return the mask, or the refusal.
    [[nodiscard]] Result<ValidityMask> releaseOutputMask()
    {
        if (!m_outputMask.valid())
        {
            return failure(ErrorCode::InvalidState, kReleaseOutputMask, "the output mask was already handed over");
        }
        handOver(StackBuffer::OutputMask, m_outputMask.capacityBytes());
        return std::move(m_outputMask);
    }

    /// Hands one map over.
    ///
    /// @param map the map.
    /// @return the storage, or the refusal.
    [[nodiscard]] Result<ImageStorageBytes> releaseOutputMap(StackOutputMap map)
    {
        if (!isStackOutputMap(map) || !stackOutputMapRequested(m_output, map))
        {
            return failure(ErrorCode::InvalidArgument, kReleaseOutputMap,
                           std::string("the ") + stackOutputMapName(map) + " map was not requested",
                           {StatusContextField::ofText(ContextKey::Parameter, "map")});
        }
        ImageStorageBytes& storage = m_maps[static_cast<std::size_t>(map)];
        if (!storage.valid())
        {
            return failure(ErrorCode::InvalidState, kReleaseOutputMap,
                           std::string("the ") + stackOutputMapName(map) + " map was already handed over");
        }
        handOver(StackBuffer::OutputMaps, storage.capacityBytes());
        return std::move(storage);
    }

private:
    /// What a planned pass takes.
    ///
    /// @param index which pass.
    /// @return its needs; nothing for the finalization.
    [[nodiscard]] PassNeeds needsOf(std::size_t index) const noexcept
    {
        PassNeeds needs;
        const StackPass& pass = m_plan.passes.passes[index];
        switch (pass.kind)
        {
        case StackPassKind::MeasureNormalization:
            needs.read = m_wholeReadBytes;
            needs.workspace = pass.workingSetBytes - m_plan.memory.persistentBytes - m_wholeReadBytes -
                              m_plan.memory.normalizationScratchBytes;
            break;
        case StackPassKind::MeasureLocalWeights:
            needs.read = m_wholeReadBytes;
            needs.workspace = pass.workingSetBytes - m_plan.memory.persistentBytes - m_wholeReadBytes;
            break;
        case StackPassKind::Combine:
            needs.scratch = m_plan.memory.scratchBytes;
            needs.tile = m_plan.memory.tileBytes;
            needs.read = m_plan.memory.readBufferBytes;
            break;
        case StackPassKind::Finalize:
            break;
        }
        return needs;
    }

    /// Takes one raw buffer from the resource, uncleared.
    ///
    /// @param operation which operation is taking it.
    /// @param buffer which buffer it is.
    /// @param bytes how many; zero takes nothing and succeeds.
    /// @param into where to keep it.
    /// @return success, or the resource's refusal naming the buffer.
    [[nodiscard]] Status take(const char* operation, StackBuffer buffer, std::uint64_t bytes, RawBuffer& into)
    {
        into = RawBuffer{};
        if (bytes == 0u)
        {
            return Status::success();
        }
        Result<void*> block = m_resource->allocate(bytes, kDefaultBufferAlignmentBytes);
        if (!block.hasValue())
        {
            ++m_refusals;
            return refusedBuffer(operation, buffer, block.status());
        }
        into = RawBuffer{.data = *block, .bytes = bytes};
        record(buffer, bytes, 1u);
        return Status::success();
    }

    /// Gives one raw buffer back.
    ///
    /// @param buffer which buffer it is.
    /// @param raw the buffer; left holding nothing.
    void give(StackBuffer buffer, RawBuffer& raw) noexcept
    {
        if (raw.data == nullptr)
        {
            return;
        }
        m_resource->deallocate(raw.data, raw.bytes, kDefaultBufferAlignmentBytes);
        forget(buffer, raw.bytes, 1u);
        raw = RawBuffer{};
    }

    /// Gives every pass buffer back.
    void returnPassBuffers() noexcept
    {
        give(StackBuffer::WorkerScratch, m_scratch);
        give(StackBuffer::Tile, m_tile);
        give(StackBuffer::ReadBuffer, m_readBuffer);
        give(StackBuffer::MeasurementWorkspace, m_workspace);
    }

    /// Counts memory taken.
    ///
    /// @param buffer which buffer.
    /// @param bytes how many.
    /// @param allocations in how many allocations.
    void record(StackBuffer buffer, std::uint64_t bytes, std::uint32_t allocations) noexcept
    {
        const auto index = static_cast<std::size_t>(buffer);
        m_held[index] += bytes;
        m_allocations[index] += allocations;
        m_heldBytes += bytes;
        if (m_heldBytes > m_peakHeldBytes)
        {
            m_peakHeldBytes = m_heldBytes;
        }
    }

    /// Counts memory given back.
    ///
    /// @param buffer which buffer.
    /// @param bytes how many.
    /// @param allocations in how many allocations.
    void forget(StackBuffer buffer, std::uint64_t bytes, std::uint32_t allocations) noexcept
    {
        const auto index = static_cast<std::size_t>(buffer);
        m_held[index] -= bytes;
        m_allocations[index] -= allocations;
        m_heldBytes -= bytes;
    }

    /// Counts memory handed to the caller, which stays outstanding in the context.
    ///
    /// @param buffer which buffer.
    /// @param bytes how many.
    void handOver(StackBuffer buffer, std::uint64_t bytes) noexcept
    {
        forget(buffer, bytes, 1u);
        m_handedOverBytes += bytes;
    }

    /// How many allocations the set is made of right now.
    ///
    /// @return the count.
    [[nodiscard]] unsigned totalAllocations() const noexcept
    {
        unsigned count = 0u;
        for (const std::uint32_t allocations : m_allocations)
        {
            count += allocations;
        }
        return count;
    }

    /// How many allocations the open pass's buffers are made of.
    ///
    /// @return the count.
    [[nodiscard]] unsigned passAllocations() const noexcept
    {
        unsigned count = 0u;
        for (std::size_t index = static_cast<std::size_t>(StackBuffer::WorkerScratch); index < kStackBufferCount;
             ++index)
        {
            count += m_allocations[index];
        }
        return count;
    }

    /// The hold that stops the owning context shutting down. First, so that it is released last.
    EngineContextLease m_lease;

    /// Where every raw buffer comes from and goes back to. Never null.
    MemoryResource* m_resource;

    /// Which context's budget the buffers belong to.
    std::uint64_t m_contextId;

    /// The context's configured limits, whose budget is the bound.
    EngineLimits m_limits;

    /// Whether the budget bounds anything.
    bool m_bounded;

    /// What the stack was asked to write.
    StackOutputRequest m_output;

    /// What the output image is.
    ImageDescriptor m_outputDescriptor;

    /// The plan the set follows.
    StackPlan m_plan;

    /// What the model implies, checked against the plan.
    StackFigures m_figures;

    /// One whole frame times the concurrent reads, which a measurement pass holds.
    std::uint64_t m_wholeReadBytes;

    /// What the resource had outstanding before this set took anything.
    std::uint64_t m_baselineLiveBytes;

    /// What the resource had left when the set was created.
    std::uint64_t m_availableBytes;

    /// The combined image, or nothing after it was handed over.
    ImageStorageBytes m_outputImage;

    /// The output's validity mask, or nothing after it was handed over.
    ValidityMask m_outputMask;

    /// Every map, in `StackOutputMap` order; one that was not requested or was handed over holds
    /// nothing.
    std::array<ImageStorageBytes, kStackOutputMapCount> m_maps{};

    /// Every accumulator, in `StackAccumulator` order; one that is not kept holds nothing.
    std::array<RawBuffer, kStackAccumulatorCount> m_accumulators{};

    /// The frame-state block: the weights, then the coefficients, then the executor's bookkeeping.
    RawBuffer m_frameState{};

    /// The local-weight grids.
    RawBuffer m_localWeightGrids{};

    /// Every worker's scratch, during the combine pass.
    RawBuffer m_scratch{};

    /// The tile, during the combine pass.
    RawBuffer m_tile{};

    /// The read buffer, during a pass that reads.
    RawBuffer m_readBuffer{};

    /// The measurement workspace, during a measurement pass.
    RawBuffer m_workspace{};

    /// What is held per buffer.
    std::array<std::uint64_t, kStackBufferCount> m_held{};

    /// How many allocations each buffer is made of.
    std::array<std::uint32_t, kStackBufferCount> m_allocations{};

    /// What is held over every buffer.
    std::uint64_t m_heldBytes = 0u;

    /// The most ever held at once.
    std::uint64_t m_peakHeldBytes = 0u;

    /// What was handed to the caller.
    std::uint64_t m_handedOverBytes = 0u;

    /// How many requests were refused.
    std::uint64_t m_refusals = 0u;

    /// Whether a pass is open.
    bool m_passOpen = false;

    /// Which pass is open.
    StackPassKind m_currentPass = StackPassKind::Combine;

    /// Which planned pass is next, or open.
    std::size_t m_passIndex = 0u;
};

StackWorkingSet::StackWorkingSet() noexcept = default;

StackWorkingSet::~StackWorkingSet() = default;

StackWorkingSet::StackWorkingSet(StackWorkingSet&& other) noexcept = default;

StackWorkingSet& StackWorkingSet::operator=(StackWorkingSet&& other) noexcept
{
    if (this != &other)
    {
        m_state = std::move(other.m_state);
    }
    return *this;
}

StackWorkingSet::StackWorkingSet(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

Result<StackWorkingSet> StackWorkingSet::create(const EngineContext& engine, const ExecutionContext& execution,
                                                const StackModel& model, const StackPlan& plan)
{
    if (!engine.valid())
    {
        return failure(ErrorCode::NotInitialized, kCreate, "the engine context holds nothing");
    }
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kCreate, "the execution context holds nothing");
    }
    if (engine.id() != execution.engineContextId())
    {
        return failure(ErrorCode::InvalidArgument, kCreate,
                       "the execution runs in another engine context than the one the buffers would come from",
                       {StatusContextField::ofText(ContextKey::Parameter, "execution")});
    }
    MemoryResource* const resource = engine.memoryResource();
    if (resource == nullptr)
    {
        return failure(ErrorCode::NotInitialized, kCreate, "the engine context holds no memory resource");
    }

    const std::uint32_t admitted = execution.admittedConcurrency();
    const Result<StackFigures> figures = computeFigures(model, admitted > 0u ? admitted : 1u, plan.io.access,
                                                        plan.tile.rows, plan.memory.outputCoversOneTile);
    if (!figures.hasValue())
    {
        return figures.status();
    }
    const Status describes = checkPlan(*figures, model, plan, admitted);
    if (describes.failed())
    {
        return describes;
    }

    // The budget, as it stands now rather than as it stood when the plan was made. The prepared
    // bytes are the caller's and are not taken here, so they are left out of what is asked for.
    const EngineLimits limits = engine.limits();
    const std::uint64_t baseline = resource->counters().liveBytes;
    const bool bounded = limits.memoryBudgetBytes != kDefaultMemoryBudgetBytes;
    const std::uint64_t available =
        bounded ? (limits.memoryBudgetBytes > baseline ? limits.memoryBudgetBytes - baseline : 0u)
                : kDefaultMemoryBudgetBytes;
    const std::uint64_t own = plan.memory.peakBytes - plan.memory.preparedStateBytes;
    if (bounded && own > available)
    {
        return budgetRefusal(kCreate,
                             "the plan's peak working set does not fit what the engine context has left now; "
                             "nothing was taken",
                             own, available, limits.memoryBudgetBytes);
    }

    // The buffers are cleared on the execution's workers, so a stopped execution would be refused
    // there after taking them; it is refused here instead, before anything is taken.
    const Status checkpoint = execution.checkCancellation();
    if (checkpoint.failed())
    {
        return checkpoint;
    }
    Result<EngineContextLease> lease = engine.acquireLease();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    std::unique_ptr<State> state;
    try
    {
        state = std::make_unique<State>(std::move(*lease), *resource, engine.id(), limits, model, plan, *figures,
                                        baseline, available);
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    const Status taken = state->takePersistent(engine);
    if (taken.failed())
    {
        return taken;
    }
    const Status zeroed = state->zeroPersistent(execution);
    if (zeroed.failed())
    {
        return zeroed;
    }
    state->explainCreation(execution);
    return StackWorkingSet{std::move(state)};
}

bool StackWorkingSet::valid() const noexcept
{
    return m_state != nullptr;
}

std::uint64_t StackWorkingSet::contextId() const noexcept
{
    return m_state != nullptr ? m_state->contextId() : 0u;
}

std::optional<StackPlan> StackWorkingSet::plan() const noexcept
{
    if (m_state == nullptr)
    {
        return std::nullopt;
    }
    return m_state->plan();
}

Status StackWorkingSet::beginPass(const ExecutionContext& execution, StackPassKind kind)
{
    if (m_state == nullptr)
    {
        return failure(ErrorCode::NotInitialized, kBeginPass, "the working set holds nothing");
    }
    return m_state->beginPass(execution, kind);
}

void StackWorkingSet::endPass() noexcept
{
    if (m_state != nullptr)
    {
        m_state->endPass();
    }
}

std::optional<StackPassKind> StackWorkingSet::currentPass() const noexcept
{
    if (m_state == nullptr)
    {
        return std::nullopt;
    }
    return m_state->currentPass();
}

std::uint32_t StackWorkingSet::passesCompleted() const noexcept
{
    return m_state != nullptr ? m_state->passesCompleted() : 0u;
}

StackMemoryAccount StackWorkingSet::account() const noexcept
{
    if (m_state == nullptr)
    {
        return StackMemoryAccount{};
    }
    return m_state->account();
}

std::span<std::byte> StackWorkingSet::outputImage() noexcept
{
    return m_state != nullptr ? m_state->outputImage() : std::span<std::byte>{};
}

std::optional<ImageLayout> StackWorkingSet::outputImageLayout() const noexcept
{
    if (m_state == nullptr)
    {
        return std::nullopt;
    }
    return m_state->outputImageLayout();
}

std::optional<ValidityMaskView<std::uint8_t>> StackWorkingSet::outputMask() noexcept
{
    if (m_state == nullptr)
    {
        return std::nullopt;
    }
    return m_state->outputMask();
}

std::span<std::uint32_t> StackWorkingSet::outputCountMap(StackOutputMap map) noexcept
{
    ImageStorageBytes* const storage = m_state != nullptr ? m_state->mapStorage(map, SampleType::UInt32) : nullptr;
    if (storage == nullptr)
    {
        return {};
    }
    return std::span<std::uint32_t>(static_cast<std::uint32_t*>(storage->data()),
                                    static_cast<std::size_t>(storage->capacityBytes() / sizeof(std::uint32_t)));
}

std::span<float> StackWorkingSet::outputValueMap(StackOutputMap map) noexcept
{
    ImageStorageBytes* const storage = m_state != nullptr ? m_state->mapStorage(map, SampleType::Float32) : nullptr;
    if (storage == nullptr)
    {
        return {};
    }
    return std::span<float>(static_cast<float*>(storage->data()),
                            static_cast<std::size_t>(storage->capacityBytes() / sizeof(float)));
}

std::span<double> StackWorkingSet::accumulatorSums() noexcept
{
    return m_state != nullptr ? spanOf<double>(m_state->accumulator(StackAccumulator::Sums)) : std::span<double>{};
}

std::span<std::uint32_t> StackWorkingSet::accumulatorCounts() noexcept
{
    return m_state != nullptr ? spanOf<std::uint32_t>(m_state->accumulator(StackAccumulator::Counts))
                              : std::span<std::uint32_t>{};
}

std::span<double> StackWorkingSet::accumulatorWeightSums() noexcept
{
    return m_state != nullptr ? spanOf<double>(m_state->accumulator(StackAccumulator::WeightSums))
                              : std::span<double>{};
}

std::span<double> StackWorkingSet::accumulatorWeightSquareSums() noexcept
{
    return m_state != nullptr ? spanOf<double>(m_state->accumulator(StackAccumulator::WeightSquareSums))
                              : std::span<double>{};
}

std::span<double> StackWorkingSet::accumulatorVarianceMoments() noexcept
{
    return m_state != nullptr ? spanOf<double>(m_state->accumulator(StackAccumulator::VarianceMoments))
                              : std::span<double>{};
}

std::span<double> StackWorkingSet::frameWeights() noexcept
{
    return m_state != nullptr ? m_state->frameWeights() : std::span<double>{};
}

std::span<double> StackWorkingSet::frameNormalization() noexcept
{
    return m_state != nullptr ? m_state->frameNormalization() : std::span<double>{};
}

std::span<std::byte> StackWorkingSet::frameBookkeeping() noexcept
{
    return m_state != nullptr ? m_state->frameBookkeeping() : std::span<std::byte>{};
}

std::span<float> StackWorkingSet::localWeightGrids() noexcept
{
    return m_state != nullptr ? m_state->localWeightGrids() : std::span<float>{};
}

std::span<std::byte> StackWorkingSet::workerScratch(std::uint32_t worker) noexcept
{
    return m_state != nullptr ? m_state->workerScratch(worker) : std::span<std::byte>{};
}

std::span<std::byte> StackWorkingSet::tile() noexcept
{
    return m_state != nullptr ? m_state->tile() : std::span<std::byte>{};
}

std::span<std::byte> StackWorkingSet::readBuffer() noexcept
{
    return m_state != nullptr ? m_state->readBuffer() : std::span<std::byte>{};
}

std::span<std::byte> StackWorkingSet::measurementWorkspace() noexcept
{
    return m_state != nullptr ? m_state->measurementWorkspace() : std::span<std::byte>{};
}

Result<ImageStorageBytes> StackWorkingSet::releaseOutputImage()
{
    if (m_state == nullptr)
    {
        return failure(ErrorCode::NotInitialized, kReleaseOutputImage, "the working set holds nothing");
    }
    return m_state->releaseOutputImage();
}

Result<ValidityMask> StackWorkingSet::releaseOutputMask()
{
    if (m_state == nullptr)
    {
        return failure(ErrorCode::NotInitialized, kReleaseOutputMask, "the working set holds nothing");
    }
    return m_state->releaseOutputMask();
}

Result<ImageStorageBytes> StackWorkingSet::releaseOutputMap(StackOutputMap map)
{
    if (m_state == nullptr)
    {
        return failure(ErrorCode::NotInitialized, kReleaseOutputMap, "the working set holds nothing");
    }
    return m_state->releaseOutputMap(map);
}

void StackWorkingSet::release() noexcept
{
    m_state.reset();
}

} // namespace an
