// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack methods: the sum's domain applied to the streaming finalization,
// and the sample-history combinations written column by column into the output.
//
// Two things in this file are decisions rather than mechanics.
//
// **The sum's peak is reduced deterministically and applied inside the finalization.** The peak is
// a whole-output reduction over the sums of the elements that received a sample, run through the
// task scheduler's fixed merge tree so the answer is the same at every thread count; it is then
// handed to the accumulators' finalization as the divisor every value goes through before it is
// narrowed once. The output is therefore normalized and narrowed in the order the contract states,
// and the finalization loop exists once, in the accumulators' unit.
//
// **A band is partitioned by row and channel, an element belongs to one worker, and a worker judges
// its columns on its own slice of scratch.** A run is one row of one channel over every frame's
// band, and the runs are spread across the admitted workers through the scheduler's stable worker
// slots, each addressing the scratch slice the working set handed out for that slot. No two runs
// share an element or a slice, so there is no merge across threads and the answer at sixteen
// threads is the answer at one. The scalar row is compiled here as the authority; a history whose
// every band is planar runs the instruction set's compiled row when this build has one, and
// everything else - the reference backend, a build without that row, an interleaved band - runs
// the authority, and the two are held to the same bits by the differential tests.

#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethodRules.h>

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejection.h>

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"
#include "algorithms/stacking/StackHistory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace an
{
namespace
{

// The scalar authority every backend is held to.
#include "kernels/reference/StackMethodRow.inl"

static_assert(kStackColumnRunLanes == kStackSampleColumnLanes,
              "the compiled column row and the scratch contract disagree about how many columns a run judges");
static_assert(sizeof(StackColumnFrame) <= kStackColumnFrameRecordBytes);
static_assert(kStackScratchAlignmentBytes % alignof(StackColumnFrame) == 0u);

constexpr const char* kCheckDomain = "an::checkStackSumDomain";
constexpr const char* kFinalize = "an::finalizeStackMethod";
constexpr const char* kCombine = "an::combineStackSampleHistory";

using stacking::checkHistory;
using stacking::checkScratch;
using stacking::checkStackWindowTargets;
using stacking::failure;
using stacking::frameRecordOf;
using stacking::HistoryGeometry;
using stacking::pointerIsAligned;
using stacking::refuse;
using stacking::windowHistory;

/// Checks a sum domain against a model, under a stated operation.
///
/// @param model the checked stack.
/// @param sumDomain what a sum's sums mean.
/// @param operation the operation the refusal is reported under.
/// @return success, or the refusal.
[[nodiscard]] Status checkSumDomainAs(const StackModel& model, StackSumDomain sumDomain, const char* operation)
{
    if (!isStackSumDomain(sumDomain))
    {
        return refuse(ErrorCode::InvalidArgument, operation, "sumDomain",
                      "sumDomain is not a sum domain this build defines");
    }
    if (!stackMethodOutputDomain(model, sumDomain).has_value())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "sumDomain",
                      std::string("sumDomain asks for the raw sum of samples in the ") +
                          numericDomainName(model.request().input.descriptor.numericDomain()) +
                          " domain, which is neither an encoded fraction nor a linear quantity; normalize the sum "
                          "to its peak or supply a linear input");
    }
    return Status::success();
}

/// A number spelled for a diagnostic sentence.
///
/// @param value the number.
/// @return its shortest round-trip spelling.
[[nodiscard]] std::string spelled(double value)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return std::string(buffer);
}

/// The public combination, spelled the way the column row reads it.
///
/// @param combination the public combination.
/// @return the private one.
[[nodiscard]] StackColumnRunCombination runCombinationOf(StackSampleHistoryCombination combination) noexcept
{
    switch (combination)
    {
    case StackSampleHistoryCombination::Median:
        return StackColumnRunCombination::Median;
    case StackSampleHistoryCombination::AutoAdaptiveWeightedAverage:
        return StackColumnRunCombination::AutoAdaptiveWeightedAverage;
    }
    return StackColumnRunCombination::Median;
}

/// Spreads the band's rows and channels across the workers and combines each row's columns.
///
/// @tparam SampleT the bands' representation.
/// @param execution the operation.
/// @param model the checked stack.
/// @param geometry the checked history's geometry.
/// @param frames the checked frame bands.
/// @param workerScratch the slices.
/// @param targets where the rows go.
/// @param compiled the compiled row, or null.
/// @param outcome receives the counts.
/// @return success, or the first failure in row order.
template <typename SampleT>
[[nodiscard]] Status
combineAs(const ExecutionContext& execution, const StackModel& model, const HistoryGeometry& geometry,
          std::span<const StackFrameBand> frames, std::span<const std::span<std::byte>> workerScratch,
          const StackFinalizeTargets& targets,
          StackColumnRunOutcome (*compiled)(std::uint64_t, std::uint64_t, const StackColumnRunParameters&,
                                            const StackColumnRunScratch&, const StackColumnRunTargets&) noexcept,
          StackSampleHistoryOutcome& outcome)
{
    const StackRequest& request = model.request();
    StackColumnRunParameters base;
    base.combination = runCombinationOf(*stackSampleHistoryCombinationOf(model));
    base.frameCount = static_cast<std::uint32_t>(frames.size());
    base.sampleScale = kAutoAdaptiveSampleScale;
    base.iterations = kAutoAdaptiveIterations;
    base.smallSigma = kAutoAdaptiveSmallSigma;
    base.minimumSamples = kAutoAdaptiveMinimumSamples;
    base.outputFloat64 = request.output.sampleType == SampleType::Float64;
    const std::uint64_t outputBytes = sampleTypeSizeBytes(request.output.sampleType);
    const std::uint64_t columnTermBytes =
        alignedStackScratchTerm(static_cast<std::uint64_t>(base.frameCount) * sizeof(double));
    const std::uint64_t recordsOffset = stackScratchBytesPerWorker(model) - stackSourceScratchBytesPerWorker(model) -
                                        alignedStackScratchTerm(base.frameCount * kStackColumnFrameRecordBytes);

    std::atomic<std::uint64_t> admitted{0u};
    std::atomic<std::uint64_t> excludedByValidity{0u};
    std::atomic<std::uint64_t> withSamples{0u};
    std::atomic<bool> compiledUsed{false};

    const std::uint64_t items = static_cast<std::uint64_t>(geometry.rows) * geometry.channels;
    const std::uint64_t perRow = static_cast<std::uint64_t>(geometry.width) * base.frameCount;
    const TaskCost cost{.bytesPerItem = perRow * 24u, .operationsPerItem = perRow * 12u};
    const Status status = parallelForWorkers(execution, items, cost, [&](WorkerTask task) -> Status {
        const std::span<std::byte>& slice = workerScratch[task.workerSlot];
        const StackColumnRunScratch scratch{
            .column = reinterpret_cast<double*>(slice.data()),
            .workspace = reinterpret_cast<double*>(slice.data() + static_cast<std::size_t>(columnTermBytes))};
        // This reserved tail is disjoint from both the column workspace and source scratch.
        // memcpy starts the lifetime of the trivial records in the working set's byte storage.
        auto* records = reinterpret_cast<StackColumnFrame*>(slice.data() + recordsOffset);
        for (std::size_t frame = 0u; frame < frames.size(); ++frame)
        {
            const StackColumnFrame record = frameRecordOf(frames[frame]);
            std::memcpy(records + frame, &record, sizeof(record));
        }
        StackColumnRunOutcome local{};
        for (std::uint64_t item = task.range.begin; item < task.range.end; ++item)
        {
            const std::uint32_t channel = static_cast<std::uint32_t>(item / geometry.rows);
            const std::uint32_t y = static_cast<std::uint32_t>(item % geometry.rows);
            const std::uint32_t outputRow = geometry.firstOutputRow + y;
            const std::uint64_t element =
                channel * geometry.planeElements + static_cast<std::uint64_t>(outputRow) * geometry.width;

            StackColumnRunParameters parameters = base;
            parameters.frames = records;
            parameters.channel = channel;
            parameters.y = y;
            StackColumnRunTargets rowTargets;
            rowTargets.output = targets.outputImage.data() + static_cast<std::size_t>(element * outputBytes);
            if (targets.outputMask != nullptr)
            {
                rowTargets.maskRow = targets.outputMask->data() +
                                     static_cast<std::uint64_t>(channel) * targets.outputMask->planeStrideBytes() +
                                     static_cast<std::uint64_t>(outputRow) * targets.outputMask->rowStrideBytes();
                std::memset(rowTargets.maskRow, 0, static_cast<std::size_t>(targets.outputMask->minimumRowBytes()));
            }
            rowTargets.coverage = targets.coverage.empty() ? nullptr : targets.coverage.data() + element;
            rowTargets.variance = targets.variance.empty() ? nullptr : targets.variance.data() + element;
            rowTargets.gain = targets.signalToNoiseGain.empty() ? nullptr : targets.signalToNoiseGain.data() + element;
            rowTargets.weightSum = targets.weightSum.empty() ? nullptr : targets.weightSum.data() + element;

            StackColumnRunOutcome run{};
            if (compiled != nullptr)
            {
                run = compiled(0u, geometry.width, parameters, scratch, rowTargets);
                compiledUsed.store(true, std::memory_order_relaxed);
            }
            else
            {
                run = stackColumnRowOf<SampleT>(0u, geometry.width, parameters, scratch, rowTargets);
            }
            local.admitted += run.admitted;
            local.excludedByValidity += run.excludedByValidity;
            local.withSamples += run.withSamples;
            if (!run.finite)
            {
                return failure(ErrorCode::InvalidData, kCombine,
                               "a column left the finite range: an admitted sample is not finite, which a validity "
                               "rule should exclude, or a combined value overflowed the output representation");
            }
        }
        admitted.fetch_add(local.admitted, std::memory_order_relaxed);
        excludedByValidity.fetch_add(local.excludedByValidity, std::memory_order_relaxed);
        withSamples.fetch_add(local.withSamples, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    outcome.samplesAdmitted = admitted.load();
    outcome.samplesExcludedByValidity = excludedByValidity.load();
    outcome.elementsCombined = items * geometry.width;
    outcome.elementsWithSamples = withSamples.load();
    outcome.elementsWithoutSamples = outcome.elementsCombined - outcome.elementsWithSamples;
    outcome.compiledRowUsed = compiledUsed.load();
    return Status::success();
}

} // namespace

const char* stackSumDomainName(StackSumDomain domain) noexcept
{
    switch (domain)
    {
    case StackSumDomain::NormalizedToPeak:
        return "NormalizedToPeak";
    case StackSumDomain::Raw:
        return "Raw";
    }
    return "UnknownStackSumDomain";
}

const char* stackSampleHistoryCombinationName(StackSampleHistoryCombination combination) noexcept
{
    switch (combination)
    {
    case StackSampleHistoryCombination::Median:
        return "Median";
    case StackSampleHistoryCombination::AutoAdaptiveWeightedAverage:
        return "AutoAdaptiveWeightedAverage";
    }
    return "UnknownStackSampleHistoryCombination";
}

Status checkStackSumDomain(const StackModel& model, StackSumDomain sumDomain)
{
    return checkSumDomainAs(model, sumDomain, kCheckDomain);
}

Result<StackMethodFinalizeOutcome> finalizeStackMethod(const ExecutionContext& execution, const StackModel& model,
                                                       StackSumDomain sumDomain, const StackAccumulatorSet& set,
                                                       const StackFinalizeTargets& targets)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kFinalize, "the finalization was given an empty execution context");
    }
    const Status checkpoint = execution.checkCancellation();
    if (checkpoint.failed())
    {
        return checkpoint;
    }
    const Status domain = checkSumDomainAs(model, sumDomain, kFinalize);
    if (domain.failed())
    {
        return domain;
    }
    const Result<std::uint64_t> elements = checkStackAccumulatorSet(model, set);
    if (!elements.hasValue())
    {
        return elements.status();
    }
    const Result<std::uint64_t> described = checkStackFinalizeTargets(model, targets);
    if (!described.hasValue())
    {
        return described.status();
    }

    StackMethodFinalizeOutcome outcome;
    outcome.outputDomain = *stackMethodOutputDomain(model, sumDomain);
    outcome.sumDomain = sumDomain;
    double divisor = 1.0;
    if (model.request().combination == StackCombination::Sum && sumDomain == StackSumDomain::NormalizedToPeak)
    {
        const double nothing = -std::numeric_limits<double>::infinity();
        const TaskCost cost{.bytesPerItem = 12u, .operationsPerItem = 2u};
        const Result<double> peak = parallelReduce(
            execution, *elements, cost, nothing,
            [&set](TaskRange range, double& partial) -> Status {
                for (std::uint64_t index = range.begin; index < range.end; ++index)
                {
                    if (set.counts[static_cast<std::size_t>(index)] != 0u)
                    {
                        const double sum = set.sums[static_cast<std::size_t>(index)];
                        partial = partial < sum ? sum : partial;
                    }
                }
                return Status::success();
            },
            [](double left, double right) noexcept -> double { return left < right ? right : left; });
        if (!peak.hasValue())
        {
            return peak.status();
        }
        const bool anySample = *peak != nothing;
        outcome.peakSum = anySample ? *peak : 0.0;
        if (anySample && *peak > 0.0)
        {
            divisor = *peak;
            outcome.normalizedToPeak = true;
            const std::string said = "Sum normalized to its peak of " + spelled(*peak) +
                                     ": the brightest element is one and the output is a fraction of full scale";
            execution.reportDiagnostic(DiagnosticSeverity::Info, ErrorDomain::Algorithms, kFinalize, said.c_str());
        }
        else
        {
            const std::string said = std::string("Sum not normalized: ") +
                                     (anySample ? "no element's sum is positive" : "no element received a sample") +
                                     ", so there is no peak to normalize to and the sums are written as they are";
            execution.reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Algorithms, kFinalize, said.c_str());
        }
    }
    const Result<StackFinalizeOutcome> finalized = finalizeStackAccumulators(execution, model, set, targets, divisor);
    if (!finalized.hasValue())
    {
        return finalized.status();
    }
    outcome.finalize = *finalized;
    return outcome;
}

namespace
{
/// The sample-history combination without a rejection, into targets that hold the whole output or,
/// given a window, that window's rows alone.
///
/// @param execution the operation.
/// @param model the checked stack.
/// @param frames one band per frame.
/// @param workerScratch one slice per admitted worker.
/// @param targets where the rows go.
/// @param window the output rows the targets hold, or null for every row.
/// @return what the combination did, or the first refusal.
[[nodiscard]] Result<StackSampleHistoryOutcome>
combineHistoryInto(const ExecutionContext& execution, const StackModel& model, std::span<const StackFrameBand> frames,
                   std::span<const std::span<std::byte>> workerScratch, const StackFinalizeTargets& targets,
                   const stacking::StackAccumulatorWindow* window)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kCombine, "the combination was given an empty execution context");
    }
    const Status checkpoint = execution.checkCancellation();
    if (checkpoint.failed())
    {
        return checkpoint;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    const std::optional<StackSampleHistoryCombination> combination = stackSampleHistoryCombinationOf(model);
    if (!combination.has_value())
    {
        return refuse(ErrorCode::IncompatibleArguments, kCombine, "model",
                      "the model's combination is a streaming statistic: its samples are combined by the "
                      "whole-output accumulators, not from a sample history");
    }
    Result<HistoryGeometry> geometry = checkHistory(model, frames, kCombine);
    if (!geometry.hasValue())
    {
        return geometry.status();
    }
    if (window != nullptr)
    {
        geometry = windowHistory(*geometry, *window, kCombine);
        if (!geometry.hasValue())
        {
            return geometry.status();
        }
    }
    const Result<std::uint64_t> described = window != nullptr
                                                ? checkStackWindowTargets(model, targets, *window, kCombine)
                                                : checkStackFinalizeTargets(model, targets);
    if (!described.hasValue())
    {
        return described.status();
    }
    const Status scratch = checkScratch(execution, model, workerScratch, kCombine);
    if (scratch.failed())
    {
        return scratch;
    }

    StackSampleHistoryOutcome outcome;
    outcome.backend = *selected;
    const bool reference = selected->backend == ComputeBackend::Reference || !geometry->planar;
    Status status = Status::success();
    if (frames[0].sampleType == SampleType::Float64)
    {
        status = combineAs<double>(execution, model, *geometry, frames, workerScratch, targets,
                                   reference ? nullptr : stackColumnFloat64RunFor(execution), outcome);
    }
    else
    {
        status = combineAs<float>(execution, model, *geometry, frames, workerScratch, targets,
                                  reference ? nullptr : stackColumnFloat32RunFor(execution), outcome);
    }
    if (status.failed())
    {
        return status;
    }
    return outcome;
}
} // namespace

Result<StackSampleHistoryOutcome> combineStackSampleHistory(const ExecutionContext& execution, const StackModel& model,
                                                            std::span<const StackFrameBand> frames,
                                                            std::span<const std::span<std::byte>> workerScratch,
                                                            const StackFinalizeTargets& targets)
{
    if (model.usesRejection())
    {
        const auto rejected = combineRejectedStackSampleHistory(execution, model, frames, workerScratch, targets);
        return rejected.hasValue() ? Result<StackSampleHistoryOutcome>{rejected->history}
                                   : Result<StackSampleHistoryOutcome>{rejected.status()};
    }
    return combineHistoryInto(execution, model, frames, workerScratch, targets, nullptr);
}

namespace stacking
{
Result<StackSampleHistoryOutcome> combineStackSampleHistoryWindow(const ExecutionContext& execution,
                                                                  const StackModel& model,
                                                                  std::span<const StackFrameBand> frames,
                                                                  std::span<const std::span<std::byte>> workerScratch,
                                                                  const StackFinalizeTargets& targets,
                                                                  const StackAccumulatorWindow& window)
{
    if (model.usesRejection())
    {
        return refuse(ErrorCode::IncompatibleArguments, kCombine, "model",
                      "a rejection writes its window through combineRejectedStackSampleHistoryWindow");
    }
    return combineHistoryInto(execution, model, frames, workerScratch, targets, &window);
}
} // namespace stacking

} // namespace an
