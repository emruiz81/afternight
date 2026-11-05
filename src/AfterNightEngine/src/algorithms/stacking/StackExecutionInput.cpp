// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack executor's input stage: source reads, the checks on what a
// source hands back, and conversion through the source path into the planned buffers.
//
// A source is caller code, so every read crosses an exception boundary and everything it returns
// is checked against what was asked for before a sample is used; a failed read becomes the
// stack's failure with the read as its cause. Where a band lands is decided here and nowhere
// else: in the read slot the plan counted, in a band of the tile, or where the source keeps it.
// This unit also cuts the frame-state block into the run's records and owns the diagnostic and
// progress helpers the other stages share.
#include "algorithms/stacking/StackExecutionState.h"

#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "runtime/ProgressRange.h"
#include "algorithms/stacking/StackHistory.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace an::stacking
{

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
    execution.reportDiagnostic(severity, ErrorDomain::Algorithms, kExecute, buffer);
}

/// A read's failure, reported as the stack's with the read as its cause.
///
/// @param frame which frame.
/// @param cause what the source said.
/// @return the failure, under the source's own code.
[[nodiscard]] Status readFailure(std::uint32_t frame, const Status& cause)
{
    return Status::failure(
        cause.code(),
        StatusDetail{.message = "frame " + std::to_string(frame) + " could not be read: " + cause.message(),
                     .operation = kExecute,
                     .domain = ErrorDomain::Algorithms,
                     .context = {StatusContextField::ofCount(ContextKey::Requested, frame)}},
        cause);
}

/// Whether a pointer lies inside a buffer.
///
/// @param buffer the buffer.
/// @param pointer the address.
/// @return true when the address is one of the buffer's bytes.
[[nodiscard]] bool inside(std::span<const std::byte> buffer, const void* pointer) noexcept
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto begin = reinterpret_cast<std::uintptr_t>(buffer.data());
    return !buffer.empty() && address >= begin && address < begin + buffer.size();
}

/// A typed view over one region of the bookkeeping block.
///
/// @tparam ElementT what the region holds.
/// @param block the block.
/// @param offset where the region begins.
/// @param bytes how many bytes it holds.
/// @return the elements, or an empty span for an empty region.
template <typename ElementT>
[[nodiscard]] std::span<ElementT> region(std::span<std::byte> block, std::uint64_t offset, std::uint64_t bytes) noexcept
{
    if (bytes == 0u)
    {
        return {};
    }
    return std::span<ElementT>(reinterpret_cast<ElementT*>(block.data() + offset),
                               static_cast<std::size_t>(bytes / sizeof(ElementT)));
}

/// Cuts the bookkeeping block into its records.
///
/// @param run the run.
void cutBookkeeping(StackExecutionState& run)
{
    const std::span<std::byte> block = run.set.frameBookkeeping();
    const StackFrameBookkeepingLayout& layout = run.bookkeeping;
    run.admittedCounts = region<std::uint64_t>(block, layout.sampleCountersOffset,
                                               static_cast<std::uint64_t>(run.frames) * kStackFrameCounterBytes);
    run.rejectedCounts = region<std::uint64_t>(
        block, layout.sampleCountersOffset + static_cast<std::uint64_t>(run.frames) * kStackFrameCounterBytes,
        static_cast<std::uint64_t>(run.frames) * kStackFrameCounterBytes);
    run.histogram = region<std::uint64_t>(block, layout.coverageHistogramOffset,
                                          (static_cast<std::uint64_t>(run.frames) + 1u) * kStackFrameCounterBytes);
    run.workerCounters = region<std::uint64_t>(block, layout.workerCountersOffset, layout.workerCountersBytes);
    std::span<std::byte>* const slices =
        reinterpret_cast<std::span<std::byte>*>(block.data() + layout.workerSlicesOffset);
    for (std::uint32_t worker = 0u; worker < run.workers; ++worker)
    {
        std::construct_at(slices + worker, std::span<std::byte>{});
    }
    run.workerSlices = std::span<std::span<std::byte>>(slices, run.workers);
    if (layout.bandRecordsBytes != 0u)
    {
        run.bandRecords = reinterpret_cast<StackFrameBand*>(block.data() + layout.bandRecordsOffset);
        run.maskRecords = reinterpret_cast<ValidityMaskView<const std::uint8_t>*>(
            block.data() + layout.bandRecordsOffset + static_cast<std::size_t>(run.frames) * sizeof(StackFrameBand));
        for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
        {
            std::construct_at(run.bandRecords + frame, StackFrameBand{});
        }
    }
    if (layout.rateRecordsBytes != 0u)
    {
        run.rateRecords = reinterpret_cast<FrameRejectionRate*>(block.data() + layout.rateRecordsOffset);
        for (std::uint32_t frame = 0u; frame < run.frames; ++frame)
        {
            std::construct_at(run.rateRecords + frame, FrameRejectionRate{});
        }
        run.flaggedRecords = region<std::uint32_t>(block, layout.flaggedFramesOffset,
                                                   static_cast<std::uint64_t>(run.frames) * sizeof(std::uint32_t));
    }
}

/// Points every worker's slice record at the combine pass's scratch, or at a region of the
/// measurement workspace.
///
/// @param run the run.
/// @param fromWorkspace the workspace region the slices are cut from, or empty for the working
///        set's own scratch.
void pointWorkerSlices(StackExecutionState& run, std::span<std::byte> fromWorkspace)
{
    const std::uint64_t perWorker = run.plan.memory.scratchBytesPerWorker;
    for (std::uint32_t worker = 0u; worker < run.workers; ++worker)
    {
        if (fromWorkspace.empty())
        {
            run.workerSlices[worker] = run.set.workerScratch(worker);
        }
        else
        {
            run.workerSlices[worker] = fromWorkspace.subspan(static_cast<std::size_t>(worker * perWorker),
                                                             static_cast<std::size_t>(perWorker));
        }
    }
}

/// Where a frame's bytes for a run of rows sit in the read slot.
///
/// @param run the run.
/// @param rows how many rows.
/// @return the slot, which is the first stored frame or band of the read buffer.
[[nodiscard]] std::span<std::byte> readSlot(StackExecutionState& run, std::uint32_t rows)
{
    const std::span<std::byte> buffer = run.set.readBuffer();
    const std::uint64_t bytes = static_cast<std::uint64_t>(run.width) * rows * run.channels * run.inputSampleBytes;
    return buffer.subspan(0u, static_cast<std::size_t>(bytes < buffer.size() ? bytes : buffer.size()));
}

/// Reads a run of rows of one frame through the source into a stated slot, and checks what came
/// back.
///
/// @param run the run.
/// @param kind the pass, for the progress stage.
/// @param frame which frame.
/// @param firstRow the first row.
/// @param rows how many rows.
/// @param slot where a source that decodes puts the band: the read slot, or a band of the tile.
/// @param band receives the band.
/// @param inSlot receives whether the band lives in `slot`.
/// @param counted whether this read adds to the report's reads and bytes; reads made together leave
///        that to the caller, once they have all joined.
/// @return success, the read's failure as the stack's, or a refusal of what the source produced.
[[nodiscard]] Status readFrameInto(StackExecutionState& run, StackPassKind kind, std::uint32_t frame,
                                   std::uint32_t firstRow, std::uint32_t rows, std::span<std::byte> slot,
                                   StackSourceBand& band, bool& inSlot, bool counted)
{
    static_cast<void>(kind);
    const Status before = run.execution->checkCancellation();
    if (before.failed())
    {
        return before;
    }
    constexpr ExternalBoundary kBoundary{
        .library = "StackFrameSource", .operation = kExecute, .domain = ErrorDomain::Algorithms};
    auto callback = evaluateAcrossBoundary(
        kBoundary, [&] { return run.source->read(*run.execution, frame, firstRow, rows, slot); });
    if (!callback.hasValue())
    {
        return readFailure(frame, callback.status());
    }
    Result<StackSourceBand> read = std::move(*callback);
    if (!read.hasValue())
    {
        return readFailure(frame, read.status());
    }
    band = *read;
    if (band.samples == nullptr || band.layout == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kExecute, "source",
                      "source produced a band with no samples or no layout");
    }
    if (band.sampleType != run.inputType)
    {
        return refuse(ErrorCode::IncompatibleArguments, kExecute, "source",
                      "source produced a band at another representation than the stack's frames");
    }
    const ImageExtent& extent = band.layout->extent();
    if (extent.width() != run.width || extent.channelCount() != run.channels || extent.height() != rows)
    {
        return refuse(ErrorCode::IncompatibleArguments, kExecute, "source",
                      "source produced a band of another geometry than the rows asked for");
    }
    if (band.frameIndex != frame || band.firstRow != firstRow)
    {
        return refuse(ErrorCode::IncompatibleArguments, kExecute, "source",
                      "source produced a band of another frame or another first row than asked for");
    }
    const bool needsMask = band.validity.descriptor.rule() == ValidityRule::ExplicitMask;
    if (needsMask != (band.validity.mask != nullptr))
    {
        return refuse(ErrorCode::InvalidArgument, kExecute, "source",
                      needsMask ? "source produced a band under the explicit-mask rule with no mask"
                                : "source produced a band with a mask under a rule that reads none");
    }
    inSlot = inside(slot, band.samples);
    if (counted)
    {
        ++run.report.frameReads;
        run.report.inputBytesRead += static_cast<std::uint64_t>(run.width) * rows * run.channels * run.inputSampleBytes;
    }
    return run.execution->checkCancellation();
}

/// Reads a run of rows of one frame through the source into the read slot, and checks what came
/// back.
///
/// @param run the run.
/// @param kind the pass, for the progress stage.
/// @param frame which frame.
/// @param firstRow the first row.
/// @param rows how many rows.
/// @param band receives the band.
/// @param inSlot receives whether the band lives in the executor's read slot.
/// @return success, the read's failure as the stack's, or a refusal of what the source produced.
[[nodiscard]] Status readFrame(StackExecutionState& run, StackPassKind kind, std::uint32_t frame,
                               std::uint32_t firstRow, std::uint32_t rows, StackSourceBand& band, bool& inSlot)
{
    return readFrameInto(run, kind, frame, firstRow, rows, readSlot(run, rows), band, inSlot);
}

/// Marks one unit of the run - one read and what was done with it, or the finalization - as done,
/// on the plan's scale.
///
/// Called outside any nested range, so the report lands on the scale the caller sees.
///
/// @param run the run.
/// @param kind the pass, for the stage.
void completeUnit(StackExecutionState& run, StackPassKind kind)
{
    ++run.progressCompleted;
    run.execution->reportProgress(run.progressCompleted, run.progressTotal, stackPassKindName(kind));
}

/// Converts one band through the source path, recording what it did.
///
/// @param run the run.
/// @param model the model the conversion runs under.
/// @param contract the contract it reads.
/// @param band the stored band.
/// @param tile where it goes.
/// @param scratch the worker slices, when a local stage runs.
/// @return the outcome, or the conversion's refusal.
[[nodiscard]] Result<StackSourceOutcome> convert(StackExecutionState& run, const StackModel& model,
                                                 const StackSourceContract& contract, const StackSourceBand& band,
                                                 const StackTileBand& tile,
                                                 std::span<const std::span<std::byte>> scratch)
{
    // The table is read under a global stage and refused otherwise, so a conversion through the
    // model with no normalization scope is given none.
    const std::span<const double> table = stackNormalizationScopeUsesGlobalMethod(model.request().normalization.scope)
                                              ? std::span<const double>(run.set.frameNormalization())
                                              : std::span<const double>{};
    Result<StackSourceOutcome> outcome =
        convertStackSourceBand(*run.execution, model, contract, band, table, scratch, tile);
    if (outcome.hasValue())
    {
        run.report.compiledRowUsed = run.report.compiledRowUsed || outcome->compiledRowUsed;
    }
    return outcome;
}

/// Reads one frame whole and brings it to where a measurement reads it.
///
/// Under the model with no normalization scope the frame is widened and calibrated only; under
/// the whole model it is brought to the reference as the combine will read it. A frame no stage
/// applies to is left where the source put it. Otherwise a widened frame is converted into
/// `owned`, the workspace's frame slot; a frame the source decoded into the read slot is
/// transformed in place; and a borrowed frame is converted into the read slot.
///
/// @param run the run.
/// @param kind the pass.
/// @param frame which frame.
/// @param whole whether to convert through the whole model or the pre-normalization one.
/// @param owned the workspace slot for a widened frame, or empty.
/// @param scratch the worker slices, when the whole model applies a local stage.
/// @param bandStorage keeps the stored band alive for the caller.
/// @param staged receives where the frame now sits.
/// @return success or the first refusal.
[[nodiscard]] Status stageWholeFrame(StackExecutionState& run, StackPassKind kind, std::uint32_t frame, bool whole,
                                     std::span<std::byte> owned, std::span<const std::span<std::byte>> scratch,
                                     StackSourceBand& bandStorage, StagedStackFrame& staged)
{
    bool inSlot = false;
    const Status read = readFrame(run, kind, frame, 0u, run.height, bandStorage, inSlot);
    if (read.failed())
    {
        return read;
    }
    const StackModel& model = whole ? *run.model : *run.preNormalization;
    const StackSourceContract& contract = whole ? run.contract : run.preContract;
    const StackSourceStages stages = stackSourceStagesOf(model, contract);
    staged = StagedStackFrame{.samples = bandStorage.samples,
                              .layout = bandStorage.layout,
                              .sampleType = bandStorage.sampleType,
                              .validity = bandStorage.validity.descriptor,
                              .mask = bandStorage.validity.mask,
                              .owned = inSlot};
    if (!stages.any())
    {
        return Status::success();
    }
    StackTileBand tile{.sampleType = run.tileType, .samples = nullptr, .layout = nullptr};
    if (run.widens)
    {
        tile.samples = owned.data();
        tile.layout = &*run.wholeTileLayout;
    }
    else if (inSlot)
    {
        tile.samples = const_cast<void*>(bandStorage.samples);
        tile.layout = bandStorage.layout;
    }
    else
    {
        tile.samples = readSlot(run, run.height).data();
        tile.layout = &*run.wholeTileLayout;
    }
    const Result<StackSourceOutcome> outcome = convert(run, model, contract, bandStorage, tile, scratch);
    if (!outcome.hasValue())
    {
        return outcome.status();
    }
    staged.samples = tile.samples;
    staged.layout = tile.layout;
    staged.sampleType = run.tileType;
    staged.validity = outcome->validity;
    staged.mask = outcome->validity.rule() == ValidityRule::ExplicitMask ? bandStorage.validity.mask : nullptr;
    staged.owned = true;
    return Status::success();
}

} // namespace an::stacking
