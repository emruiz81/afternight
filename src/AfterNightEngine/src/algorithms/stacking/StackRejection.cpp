// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - rejection over a band of sample histories: every column of a tile judged
// by the estimators and its survivors combined into the output, its mask and its maps.
//
// Three things in this file are decisions rather than mechanics.
//
// **Everything that can fail is checked before anything is written.** The bands, the targets, the
// maps, the weights, the per-frame counters and every pairwise overlap between what is read and
// what is written are checked, and the deviate test's critical table is prepared into every
// worker's slice, before the first output element. After that only cancellation, an admitted
// sample that is not finite and arithmetic that leaves the finite range can stop a band, and the
// published contract says such a band is discarded.
//
// **A band is partitioned by row and channel, and an element belongs to one worker.** Each work
// item is one row of one channel; its columns are judged on the worker's own slice by the shared
// estimators in `RejectionEstimators.h`, in frame order, and written by that worker alone. The
// counts are integers added through atomics and the per-frame counters are summed in worker order
// after the band, so neither the output nor any count depends on the worker count.
//
// **The common case takes a shorter road with the same bits.** An unweighted combination under the
// Siril-compatible policy with no variance map folds a column's survivors in one pass, which is
// the general combination's arithmetic with every weight one. A uniform Float32 band on that road
// is judged and combined a row at a time by a compiled rejection row when the dispatch table has
// one for its family and frame count - two to eight frames, and every family but the median
// absolute deviation's - and the differential tests hold that row to the one-column road bit for
// bit. Everything else, and every column the one-pass road leaves with no survivor, takes the
// general combination.

#include <AfterNightEngine/Algorithms/Stacking/StackRejection.h>

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>

#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include "algorithms/stacking/StackHistory.h"
#include "algorithms/stacking/RejectionEstimators.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <type_traits>

namespace an
{
namespace
{
constexpr const char* kCombine = "an::combineRejectedStackSampleHistory";
static_assert(sizeof(StackColumnFrame) <= kStackRejectionRowRecordBytes);
static_assert(kStackScratchAlignmentBytes % alignof(StackColumnFrame) == 0u);
using stacking::checkHistory;
using stacking::checkScratch;
using stacking::failure;
using stacking::frameRecordOf;
using stacking::HistoryGeometry;
using stacking::pointerIsAligned;
using stacking::refuse;

#include "kernels/reference/StackMethodRow.inl"

/// How a compiled rejection row spells a rejection method.
///
/// @return false when no compiled row runs the method.
[[nodiscard]] bool rowFamilyOf(StackRejection method, StackRejectionRowFamily& family) noexcept
{
    switch (method)
    {
    case StackRejection::SigmaClipping:
        family = StackRejectionRowFamily::SigmaClipping;
        return true;
    case StackRejection::MedianSigmaReplacement:
        family = StackRejectionRowFamily::MedianSigmaReplacement;
        return true;
    case StackRejection::WinsorizedSigmaClipping:
        family = StackRejectionRowFamily::WinsorizedSigmaClipping;
        return true;
    case StackRejection::LinearFitClipping:
        family = StackRejectionRowFamily::LinearFitClipping;
        return true;
    case StackRejection::PercentileClipping:
        family = StackRejectionRowFamily::PercentileClipping;
        return true;
    case StackRejection::GeneralizedEsdTest:
        family = StackRejectionRowFamily::GeneralizedEsdTest;
        return true;
    default:
        return false;
    }
}

/// Checks the buffers the band geometry and the targets' own check do not reach, before anything
/// is written.
///
/// The per-frame counters, when counted, must hold the stated lengths and be aligned for their
/// counts; every rejection map the model requests must hold one aligned count per output element
/// and the others must be empty; the frame weights must be one per frame and channel exactly when
/// the model weighs its frames, each finite and non-negative. Then no two written buffers may
/// overlap, no worker slice may overlap another or anything written, and nothing read - the
/// weights, every band's samples, local weights and mask - may overlap anything written or any
/// slice. Intervals are compared by distance, so an invalid one cannot overflow the address
/// arithmetic.
///
/// @return success; `ErrorCode::IncompatibleArguments` naming the counter, the map or
///         `frameWeights`; or `ErrorCode::InvalidArgument` naming `frameWeights` for a weight that
///         is not finite and non-negative, or naming `buffers` for any overlap.
[[nodiscard]] Status checkRejectionBuffers(const ExecutionContext& execution, const StackModel& model,
                                           const stacking::HistoryGeometry& geometry, std::uint64_t elements,
                                           std::span<const StackFrameBand> frames,
                                           std::span<const std::span<std::byte>> workerScratch,
                                           const StackFinalizeTargets& targets, std::span<const double> frameWeights,
                                           const StackRejectionMaps& maps, const StackRejectionFrameCounters* counters)
{
    const auto& request = model.request();
    const auto mapForm = request.output.rejectionMaps;
    if (counters != nullptr)
    {
        const auto perFrame = static_cast<std::size_t>(frames.size());
        const auto rows = static_cast<std::size_t>(execution.admittedConcurrency()) * perFrame * 2u;
        const auto checkCounter = [&](std::span<std::uint64_t> counter, std::size_t expected,
                                      const char* name) -> Status {
            if (counter.size() != expected || !pointerIsAligned(counter.data(), alignof(std::uint64_t)))
                return refuse(ErrorCode::IncompatibleArguments, kCombine, name,
                              std::string(name) + " does not hold the stated counts");
            return Status::success();
        };
        for (const Status& checked : {checkCounter(counters->admitted, perFrame, "counters.admitted"),
                                      checkCounter(counters->rejected, perFrame, "counters.rejected"),
                                      checkCounter(counters->workerRows, rows, "counters.workerRows")})
            if (checked.failed())
                return checked;
    }
    const auto checkMap = [&](std::span<std::uint32_t> map, bool required, const char* name) -> Status {
        if (map.size() != (required ? elements : 0u) || !pointerIsAligned(map.data(), alignof(std::uint32_t)))
            return refuse(ErrorCode::IncompatibleArguments, kCombine, name,
                          std::string(name) + " does not describe the requested rejection map");
        return Status::success();
    };
    for (const Status& checked : {checkMap(maps.low, mapForm == RejectionMapForm::LowAndHigh, "maps.low"),
                                  checkMap(maps.high, mapForm == RejectionMapForm::LowAndHigh, "maps.high"),
                                  checkMap(maps.merged, mapForm == RejectionMapForm::Merged, "maps.merged")})
        if (checked.failed())
            return checked;
    const auto weightCount = model.usesFrameWeights() ? frames.size() * geometry.channels : 0u;
    if (frameWeights.size() != weightCount)
        return refuse(ErrorCode::IncompatibleArguments, kCombine, "frameWeights", "frameWeights has the wrong length");
    for (const double weight : frameWeights)
        if (!std::isfinite(weight) || weight < 0.0)
            return refuse(ErrorCode::InvalidArgument, kCombine, "frameWeights",
                          "frameWeights must be finite and non-negative");

    // Buffer intervals are checked once, before any scratch or output is touched. Comparing by
    // distance avoids overflowing the address space when a caller supplies an invalid interval.
    const auto overlaps = [](const void* left, std::size_t leftBytes, const void* right, std::size_t rightBytes) {
        if (leftBytes == 0u || rightBytes == 0u)
            return false;
        const auto a = reinterpret_cast<std::uintptr_t>(left);
        const auto b = reinterpret_cast<std::uintptr_t>(right);
        return a <= b ? b - a < leftBytes : a - b < rightBytes;
    };
    const auto outputMask = targets.outputMask;
    const std::span<const std::byte> maskBytes =
        outputMask == nullptr ? std::span<const std::byte>{}
                              : std::span<const std::byte>{
                                    reinterpret_cast<const std::byte*>(outputMask->data()),
                                    static_cast<std::size_t>(outputMask->planeStrideBytes() * (geometry.channels - 1u) +
                                                             outputMask->rowStrideBytes() * (geometry.targetRows - 1u) +
                                                             outputMask->minimumRowBytes())};
    const std::span<const std::byte> outputs[] = {
        targets.outputImage,
        maskBytes,
        std::as_bytes(targets.coverage),
        std::as_bytes(targets.variance),
        std::as_bytes(targets.signalToNoiseGain),
        std::as_bytes(targets.weightSum),
        std::as_bytes(maps.low),
        std::as_bytes(maps.high),
        std::as_bytes(maps.merged),
        counters == nullptr ? std::span<const std::byte>{} : std::as_bytes(counters->admitted),
        counters == nullptr ? std::span<const std::byte>{} : std::as_bytes(counters->rejected),
        counters == nullptr ? std::span<const std::byte>{} : std::as_bytes(counters->workerRows)};
    const auto rejectOverlap = [&]() {
        return refuse(ErrorCode::InvalidArgument, kCombine, "buffers",
                      "buffers overlap writable output or workerScratch");
    };
    const auto conflictsWithWrites = [&](const void* data, std::size_t bytes, bool includeScratch) {
        for (const auto output : outputs)
            if (overlaps(data, bytes, output.data(), output.size()))
                return true;
        if (includeScratch)
            for (std::uint32_t worker = 0u; worker < execution.admittedConcurrency(); ++worker)
                if (overlaps(data, bytes, workerScratch[worker].data(), workerScratch[worker].size()))
                    return true;
        return false;
    };
    for (std::size_t i = 0u; i < std::size(outputs); ++i)
        for (std::size_t j = 0u; j < i; ++j)
            if (overlaps(outputs[i].data(), outputs[i].size(), outputs[j].data(), outputs[j].size()))
                return rejectOverlap();
    for (std::uint32_t i = 0u; i < execution.admittedConcurrency(); ++i)
    {
        if (conflictsWithWrites(workerScratch[i].data(), workerScratch[i].size(), false))
            return rejectOverlap();
        for (std::uint32_t j = 0u; j < i; ++j)
            if (overlaps(workerScratch[i].data(), workerScratch[i].size(), workerScratch[j].data(),
                         workerScratch[j].size()))
                return rejectOverlap();
    }
    if (conflictsWithWrites(frameWeights.data(), frameWeights.size_bytes(), true))
        return rejectOverlap();
    for (const auto& frame : frames)
    {
        if (conflictsWithWrites(frame.samples, static_cast<std::size_t>(frame.layout->totalBytes()), true) ||
            conflictsWithWrites(frame.localWeights.data(), frame.localWeights.size_bytes(), true))
            return rejectOverlap();
        if (frame.validity.mask != nullptr)
        {
            const auto& mask = *frame.validity.mask;
            const auto bytes = mask.planeStrideBytes() * (mask.planeCount() - 1u) +
                               mask.rowStrideBytes() * (geometry.rows - 1u) + mask.minimumRowBytes();
            if (conflictsWithWrites(mask.data(), static_cast<std::size_t>(bytes), true))
                return rejectOverlap();
        }
    }
    return Status::success();
}

/// The band driver behind both public entry points and the window form; `counters` is null when
/// nothing is counted per frame, and `window` null when the targets and the maps hold every output
/// row.
template <typename SampleT>
[[nodiscard]] Result<StackRejectionOutcome>
combineRejectedAs(const ExecutionContext& execution, const StackModel& model, std::span<const StackFrameBand> frames,
                  std::span<const std::span<std::byte>> workerScratch, const StackFinalizeTargets& targets,
                  std::span<const double> frameWeights, const StackRejectionMaps& maps,
                  StackRejectionCombinationPolicy combinationPolicy, const StackRejectionFrameCounters* counters,
                  const stacking::StackAccumulatorWindow* window)
{
    if (!execution.valid())
        return failure(ErrorCode::NotInitialized, kCombine, "an empty execution cannot judge a sample history");
    const Status checkpoint = execution.checkCancellation();
    if (checkpoint.failed())
        return checkpoint;
    if (!model.usesRejection())
        return refuse(ErrorCode::IncompatibleArguments, kCombine, "model", "model does not request rejection");
    if (combinationPolicy != StackRejectionCombinationPolicy::SirilCompatible &&
        combinationPolicy != StackRejectionCombinationPolicy::SurvivorValues)
        return refuse(ErrorCode::InvalidArgument, kCombine, "combinationPolicy",
                      "combinationPolicy is not a defined policy");
    const bool compatible = combinationPolicy == StackRejectionCombinationPolicy::SirilCompatible;
    auto geometry = checkHistory(model, frames, kCombine);
    if (!geometry.hasValue())
        return geometry.status();
    if (window != nullptr)
    {
        geometry = stacking::windowHistory(*geometry, *window, kCombine);
        if (!geometry.hasValue())
            return geometry.status();
    }
    const auto described = window != nullptr ? stacking::checkStackWindowTargets(model, targets, *window, kCombine)
                                             : checkStackFinalizeTargets(model, targets);
    if (!described.hasValue())
        return described.status();
    const Status scratch = checkScratch(execution, model, workerScratch, kCombine);
    if (scratch.failed())
        return scratch;
    const auto selected = selectBackend(execution);
    if (!selected.hasValue())
        return selected.status();
    const auto outputMask = targets.outputMask;
    const auto buffers = checkRejectionBuffers(execution, model, *geometry, *described, frames, workerScratch, targets,
                                               frameWeights, maps, counters);
    if (buffers.failed())
        return buffers;
    // Every worker's row starts from nothing on every band; the rows are summed in worker order
    // once the band is done, so the per-frame counts do not depend on which worker ran which run.
    if (counters != nullptr)
        std::memset(counters->workerRows.data(), 0, counters->workerRows.size_bytes());
    // Prepared tables occupy the planner's per-worker scratch. Do all fallible preparation before
    // any output is written, including significance values too small for the adopted Float32 alpha.
    for (std::uint32_t worker = 0u; worker < execution.admittedConcurrency(); ++worker)
    {
        auto workspace = rejectionWorkspace(model, workerScratch[worker]);
        const auto prepared = prepareRejectionWorkspace(execution, model, workspace);
        if (prepared.failed())
        {
            if (prepared.code() == ErrorCode::Cancelled || prepared.code() == ErrorCode::DeadlineExceeded)
                return prepared;
            return refuse(prepared.code(), kCombine, "rejection.gesdt.significance",
                          "rejection.gesdt.significance cannot form a finite compatibility critical table");
        }
    }

    std::atomic<std::uint64_t> admitted{0u}, excluded{0u}, combined{0u}, withoutWeight{0u}, withSamples{0u};
    std::atomic<std::uint64_t> lowEvents{0u}, highEvents{0u}, mappedLow{0u}, mappedHigh{0u};
    std::atomic<bool> compiledUsed{false};
    const auto items = static_cast<std::uint64_t>(geometry->rows) * geometry->channels;
    const auto frameCount = static_cast<std::uint32_t>(frames.size());
    // Two decisions are made once for the band rather than once per sample. Every frame's row is
    // read through one comparison when every frame is planar, carries no mask and states the same
    // sample-by-value rule, which a stack of calibrated frames always does. And an unweighted
    // compatible combination without a variance map folds its survivors in a single pass, which
    // gives the general pass's bits: every weight is one, so the weighted sum is the plain sum and
    // the weight sum, the square-weight sum and the gain are all the count. A column left with no
    // survivor takes the general pass, whose median fallback the single pass does not repeat.
    const ValidityRule sharedRule = frames.empty() ? ValidityRule::AllValid : frames[0].validity.descriptor.rule();
    bool uniformRows = sharedRule == ValidityRule::AllValid || sharedRule == ValidityRule::FiniteOnly ||
                       sharedRule == ValidityRule::ZeroIsNoData;
    for (const StackFrameBand& band : frames)
    {
        const StackColumnFrame record = frameRecordOf(band);
        uniformRows =
            uniformRows && record.samplesPerPixel == 1u && record.maskBits == nullptr && record.rule == sharedRule;
    }
    const bool plainCombination =
        compatible && !model.usesFrameWeights() && !model.usesLocalWeights() && targets.variance.empty();
    // A uniform Float32 band whose columns take the single pass is judged and combined a row at a
    // time by the compiled row, when the method has one, the execution's table has one for its
    // frame count and the output is aligned to its sample; the reference table never has one, so
    // it keeps the one-column path.
    const StackRejectionRequest& rejection = model.request().rejection;
    StackRejectionRowParameters rowParameters{.lowFactor = static_cast<float>(rejection.sigma.low),
                                              .highFactor = static_cast<float>(rejection.sigma.high),
                                              .lowFraction = rejection.medianFractions.low,
                                              .highFraction = rejection.medianFractions.high};
    const StackRejectionFloat32RowFn compiledRow =
        std::is_same_v<SampleT, float> && rowFamilyOf(rejection.method, rowParameters.family)
            ? stackRejectionFloat32RowFor(execution, frameCount)
            : nullptr;
    const bool compiledRows =
        uniformRows && plainCombination && compiledRow != nullptr &&
        pointerIsAligned(targets.outputImage.data(), sampleTypeSizeBytes(targets.outputSampleType));
    for (std::uint32_t samples = 1u; samples <= kStackRejectionRowFrames; ++samples)
        rowParameters.maximumOutliers[samples - 1u] =
            static_cast<std::int32_t>(stackGesdtMaximumOutliers(samples, rejection.gesdt.maximumOutlierFraction));
    const auto status = parallelForWorkers(
        execution, items, TaskCost{.operationsPerItem = geometry->width * frames.size() * 64u},
        [&](WorkerTask task) -> Status {
            auto workspace = rejectionWorkspace(model, workerScratch[task.workerSlot]);
            // Rank statistics are constant over columns and retain the full-frame rank population.
            // The mean rank and the inverse rank variance live in the workspace record rather
            // than in the slice, so a record cut again is prepared again; the critical values the
            // deviate test prepared before the band, for every count from three, are still in the
            // slice. The compiled row reads both from the worker's workspace.
            StackRejectionRowParameters taskRowParameters = rowParameters;
            if (workspace.reciprocals != nullptr)
            {
                const auto ready = prepareRejectionWorkspace(execution, model, workspace);
                if (ready.failed())
                    return ready;
                taskRowParameters.meanRank = static_cast<float>(workspace.meanRank);
                taskRowParameters.inverseRankVariance = static_cast<float>(workspace.inverseRankVariance);
                for (std::uint32_t i = 0u; i < frameCount && i < kStackRejectionRowFrames; ++i)
                    taskRowParameters.reciprocals[i] = static_cast<float>(workspace.reciprocals[i]);
            }
            if (workspace.critical != nullptr)
                for (std::uint32_t i = 2u; i < frameCount && i < kStackRejectionRowFrames; ++i)
                    taskRowParameters.critical[i] = static_cast<float>(workspace.critical[i]);
            StackRejectionOutcome local;
            std::uint64_t* const counterRow =
                counters == nullptr
                    ? nullptr
                    : counters->workerRows.data() + static_cast<std::size_t>(task.workerSlot) * frames.size() * 2u;
            // Writes one combined element and its maps; shared by both passes.
            const auto store = [&](std::uint64_t element, std::uint8_t* maskRow, std::uint32_t x, double value,
                                   float narrowed, std::uint32_t count, float variance, float gain, float weightSum,
                                   std::uint32_t low, std::uint32_t high) {
                if (targets.outputSampleType == SampleType::Float64)
                    std::memcpy(targets.outputImage.data() + element * sizeof(double), &value, sizeof(value));
                else
                    std::memcpy(targets.outputImage.data() + element * sizeof(float), &narrowed, sizeof(narrowed));
                if (maskRow != nullptr && count != 0u)
                    maskRow[x / 8u] |= static_cast<std::uint8_t>(1u << (x % 8u));
                if (!targets.coverage.empty())
                    targets.coverage[element] = count;
                if (!targets.variance.empty())
                    targets.variance[element] = variance;
                if (!targets.signalToNoiseGain.empty())
                    targets.signalToNoiseGain[element] = gain;
                if (!targets.weightSum.empty())
                    targets.weightSum[element] = weightSum;
                if (!maps.low.empty())
                    maps.low[element] = low;
                if (!maps.high.empty())
                    maps.high[element] = high;
                if (!maps.merged.empty())
                    maps.merged[element] = low + high;
                local.mappedLow += low;
                local.mappedHigh += high;
                local.samplesCombined += count;
                local.history.elementsWithSamples += count != 0u ? 1u : 0u;
            };
            // The general combination of one judged column: weights, the compatible weighted range,
            // the median fallback and the variance map. The maps count first rejections before
            // anything else. Under the Siril-compatible policy every survivor counts towards the
            // coverage and the variance whatever its weight; a weighted stack then re-reads the
            // original values inside the survivors' closed range and averages them with their
            // weights, or takes the positive ones over the survivor count when that sum or weight
            // total is zero; and a column with no survivor has its admitted samples restored and
            // takes their median. Under `SurvivorValues` only a survivor of positive finite weight
            // contributes, and a column with none is written as zero with its mask bit clear.
            const auto combineGeneral = [&](std::uint32_t x, std::uint32_t channel, std::uint32_t outputY,
                                            std::uint8_t* maskRow) -> Status {
                std::uint32_t low = 0u, high = 0u, count = 0u;
                std::uint32_t survivors = 0u;
                double minimum = std::numeric_limits<double>::infinity();
                double maximum = -std::numeric_limits<double>::infinity();
                for (std::uint32_t i = 0u; i < frameCount; ++i)
                {
                    const auto flag = workspace.flags[i];
                    const bool firstLow = flag == RejectionFlag::Low || flag == RejectionFlag::ReplacedLow;
                    const bool firstHigh = flag == RejectionFlag::High || flag == RejectionFlag::ReplacedHigh;
                    low += firstLow ? 1u : 0u;
                    high += firstHigh ? 1u : 0u;
                    if (counterRow != nullptr && (firstLow || firstHigh))
                        ++counterRow[static_cast<std::size_t>(i) * 2u + 1u];
                    if (rejectionSurvivor(flag))
                    {
                        ++survivors;
                        minimum = (std::min)(minimum, workspace.column[i]);
                        maximum = (std::max)(maximum, workspace.column[i]);
                    }
                }
                const bool medianFallback = compatible && survivors == 0u;
                if (medianFallback)
                    for (std::uint32_t i = 0u; i < frameCount; ++i)
                        if (workspace.flags[i] != RejectionFlag::Missing)
                            workspace.flags[i] = RejectionFlag::Kept;
                double sum = 0.0, weightSum = 0.0, squareWeights = 0.0, plainSum = 0.0;
                for (std::uint32_t i = 0u; i < frameCount; ++i)
                {
                    const auto flag = workspace.flags[i];
                    if (!rejectionSurvivor(flag))
                        continue;
                    if (compatible)
                    {
                        ++count;
                        plainSum += workspace.column[i];
                    }
                    const double weight = workspace.weights == nullptr ? 1.0 : workspace.weights[i];
                    if (!(weight > 0.0) || !std::isfinite(weight))
                    {
                        ++local.samplesExcludedByWeight;
                        continue;
                    }
                    if (!compatible)
                    {
                        ++count;
                        plainSum += workspace.column[i];
                    }
                    sum += workspace.column[i] * weight;
                    weightSum += weight;
                    squareWeights += weight * weight;
                }
                double value = weightSum == 0.0 ? 0.0 : sum / weightSum;
                const double gain = squareWeights == 0.0 ? 0.0 : weightSum * weightSum / squareWeights;
                if (medianFallback && count != 0u)
                {
                    auto* gathered = reinterpret_cast<double*>(workspace.work);
                    std::uint32_t size = 0u;
                    for (std::uint32_t i = 0u; i < frameCount; ++i)
                        if (rejectionSurvivor(workspace.flags[i]))
                            gathered[size++] = workspace.column[i];
                    value = stackColumnMedianOf(gathered, size);
                }
                else if (compatible && workspace.weights != nullptr && survivors != 0u)
                {
                    double rangeSum = 0.0, rangeWeights = 0.0, positiveSum = 0.0;
                    for (std::uint32_t i = 0u; i < frameCount; ++i)
                    {
                        if (workspace.flags[i] == RejectionFlag::Missing)
                            continue;
                        const auto& record = workspace.frameRows[i];
                        const auto* row = record.samples;
                        const double original = stackColumnSampleAt<SampleT>(row, x, record.samplesPerPixel);
                        if (original < minimum || original > maximum)
                            continue;
                        if (original > 0.0)
                            positiveSum += original;
                        const double weight = workspace.weights[i];
                        if (weight > 0.0 && std::isfinite(weight))
                        {
                            rangeSum += original * weight;
                            rangeWeights += weight;
                        }
                    }
                    value = rangeWeights != 0.0 && rangeSum != 0.0 ? rangeSum / rangeWeights : positiveSum / survivors;
                }
                double variance = 0.0;
                if (!targets.variance.empty() && count > 1u)
                {
                    const double mean = plainSum / count;
                    for (std::uint32_t i = 0u; i < frameCount; ++i)
                    {
                        const double weight = workspace.weights == nullptr ? 1.0 : workspace.weights[i];
                        if (rejectionSurvivor(workspace.flags[i]) &&
                            (compatible || (weight > 0.0 && std::isfinite(weight))))
                        {
                            const double difference = workspace.column[i] - mean;
                            variance += difference * difference;
                        }
                    }
                    variance /= count - 1u;
                }
                const float narrowed = static_cast<float>(value);
                if (!std::isfinite(value) || !std::isfinite(weightSum) || !std::isfinite(gain) ||
                    !std::isfinite(variance) || !std::isfinite(plainSum) ||
                    (targets.outputSampleType == SampleType::Float32 && !std::isfinite(narrowed)) ||
                    (!targets.variance.empty() && !std::isfinite(static_cast<float>(variance))) ||
                    (!targets.weightSum.empty() && !std::isfinite(static_cast<float>(weightSum))))
                    return failure(ErrorCode::InvalidData, kCombine, "a rejection combination left the finite range");
                const auto element =
                    channel * geometry->planeElements + static_cast<std::uint64_t>(outputY) * geometry->width + x;
                store(element, maskRow, x, value, narrowed, count, static_cast<float>(variance),
                      static_cast<float>(gain), static_cast<float>(weightSum), low, high);
                return Status::success();
            };
            // One uniform row judged and combined by the compiled row, which admits every sample as
            // one column's are admitted, combines every column as the single pass combines one, and
            // gives a column its removals emptied the general pass's median fallback. The frame
            // rows are those `runRows` recorded.
            const auto combineCompiledRow = [&](std::uint32_t channel, std::uint32_t outputY,
                                                std::uint8_t* maskRow) -> Status {
                const Status stopped = execution.checkCancellation();
                if (stopped.failed())
                    return stopped;
                StackRejectionRowSource source{.rule = sharedRule, .width = geometry->width};
                for (std::uint32_t i = 0u; i < frameCount; ++i)
                    source.rows[i] = reinterpret_cast<const float*>(workspace.frameRows[i].samples);
                const std::uint64_t rowElement =
                    channel * geometry->planeElements + static_cast<std::uint64_t>(outputY) * geometry->width;
                const auto rowOf = [rowElement]<typename T>(std::span<T> target) {
                    return target.empty() ? nullptr : target.data() + rowElement;
                };
                const StackRejectionRowTargets rowTargets{
                    .output = targets.outputImage.data() + rowElement * sampleTypeSizeBytes(targets.outputSampleType),
                    .outputFloat64 = targets.outputSampleType == SampleType::Float64,
                    .maskRow = maskRow,
                    .coverage = rowOf(targets.coverage),
                    .gain = rowOf(targets.signalToNoiseGain),
                    .weightSum = rowOf(targets.weightSum),
                    .low = rowOf(maps.low),
                    .high = rowOf(maps.high),
                    .merged = rowOf(maps.merged),
                    .frameCounters = counterRow};
                const StackRejectionRowOutcome row = compiledRow(taskRowParameters, source, rowTargets);
                switch (row.failure)
                {
                case StackRejectionRowFailure::None:
                    break;
                case StackRejectionRowFailure::NonFiniteSample:
                    return failure(ErrorCode::InvalidData, kCombine, "an admitted sample is not finite");
                case StackRejectionRowFailure::NonFiniteStatistics:
                    return Status::failure(ErrorCode::InvalidData);
                case StackRejectionRowFailure::NonFiniteCombination:
                    return failure(ErrorCode::InvalidData, kCombine, "a rejection combination left the finite range");
                }
                local.history.samplesExcludedByValidity += row.excludedByValidity;
                local.history.samplesAdmitted -= row.excludedByValidity;
                local.history.elementsWithSamples += row.withSamples;
                local.samplesCombined += row.samplesCombined;
                local.rejectedLow += row.rejectedLow;
                local.rejectedHigh += row.rejectedHigh;
                local.mappedLow += row.mappedLow;
                local.mappedHigh += row.mappedHigh;
                compiledUsed.store(true, std::memory_order_relaxed);
                return Status::success();
            };
            // The rows of this task. `Uniform` reads every frame's row directly under `Rule`;
            // otherwise each sample is read and judged through its own frame's record.
            const auto runRows = [&]<ValidityRule Rule, bool Uniform>() -> Status {
                for (auto item = task.range.begin; item < task.range.end; ++item)
                {
                    const auto channel = static_cast<std::uint32_t>(item / geometry->rows);
                    const auto y = static_cast<std::uint32_t>(item % geometry->rows);
                    const auto outputY = geometry->firstOutputRow + y;
                    auto* maskRow = outputMask == nullptr
                                        ? nullptr
                                        : outputMask->data() + channel * outputMask->planeStrideBytes() +
                                              outputY * outputMask->rowStrideBytes();
                    if (maskRow != nullptr)
                        std::memset(maskRow, 0, static_cast<std::size_t>(outputMask->minimumRowBytes()));
                    for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
                    {
                        StackColumnFrame record = frameRecordOf(frames[frame]);
                        record.samples = stackColumnFrameRow(record, channel, y);
                        record.maskBits = stackColumnFrameMaskRow(record, channel, y);
                        std::construct_at(workspace.frameRows + frame, record);
                        if (counterRow != nullptr)
                            counterRow[static_cast<std::size_t>(frame) * 2u] += geometry->width;
                    }
                    // Start from the dense row and subtract missing samples. Most astronomical
                    // rows are dense, so their admission counters need one update per row/frame.
                    local.history.samplesAdmitted += static_cast<std::uint64_t>(geometry->width) * frameCount;
                    if constexpr (Uniform)
                    {
                        if (compiledRows)
                        {
                            const Status combinedRow = combineCompiledRow(channel, outputY, maskRow);
                            if (combinedRow.failed())
                                return combinedRow;
                            continue;
                        }
                    }
                    for (std::uint32_t x = 0u; x < geometry->width; ++x)
                    {
                        const Status stopped = execution.checkCancellation();
                        if (stopped.failed())
                            return stopped;
                        for (std::uint32_t i = 0u; i < frameCount; ++i)
                        {
                            const auto& record = workspace.frameRows[i];
                            double sample = 0.0;
                            bool valid = true;
                            if constexpr (Uniform)
                            {
                                SampleT stored;
                                std::memcpy(&stored, record.samples + static_cast<std::size_t>(x) * sizeof(SampleT),
                                            sizeof(SampleT));
                                // Adding zero makes a negative zero positive, as `stackColumnSampleAt`
                                // reads it, so both reads hand the estimators the same bits.
                                sample = static_cast<double>(stored) + 0.0;
                                if constexpr (Rule == ValidityRule::FiniteOnly)
                                    valid = std::isfinite(sample);
                                else if constexpr (Rule == ValidityRule::ZeroIsNoData)
                                    valid = sample != 0.0;
                            }
                            else
                            {
                                sample = stackColumnSampleAt<SampleT>(record.samples, x, record.samplesPerPixel);
                                valid = stackColumnSampleAdmitted(sample, x, record, record.maskBits);
                            }
                            workspace.flags[i] = valid ? RejectionFlag::Kept : RejectionFlag::Missing;
                            workspace.column[i] = sample;
                            if (!valid)
                            {
                                ++local.history.samplesExcludedByValidity;
                                --local.history.samplesAdmitted;
                                if (counterRow != nullptr)
                                    --counterRow[static_cast<std::size_t>(i) * 2u];
                            }
                            else
                            {
                                if (!std::isfinite(sample))
                                    return failure(ErrorCode::InvalidData, kCombine,
                                                   "an admitted sample is not finite");
                            }
                            if (workspace.weights != nullptr)
                            {
                                double weight =
                                    model.usesFrameWeights() ? frameWeights[i * geometry->channels + channel] : 1.0;
                                if (model.usesLocalWeights())
                                    weight *= frames[i].localWeights[static_cast<std::size_t>(y) * geometry->width + x];
                                workspace.weights[i] = weight;
                            }
                        }
                        RejectionCounts events;
                        const auto estimated = estimateRejection(execution, model, workspace, events);
                        if (estimated.failed())
                            return estimated;
                        local.rejectedLow += events.low;
                        local.rejectedHigh += events.high;
                        if (plainCombination)
                        {
                            std::uint32_t low = 0u, high = 0u, count = 0u;
                            double sum = 0.0;
                            for (std::uint32_t i = 0u; i < frameCount; ++i)
                            {
                                const auto flag = workspace.flags[i];
                                low += flag == RejectionFlag::Low || flag == RejectionFlag::ReplacedLow ? 1u : 0u;
                                high += flag == RejectionFlag::High || flag == RejectionFlag::ReplacedHigh ? 1u : 0u;
                                if (rejectionSurvivor(flag))
                                {
                                    ++count;
                                    sum += workspace.column[i];
                                }
                            }
                            if (count != 0u)
                            {
                                if (counterRow != nullptr && low + high != 0u)
                                    for (std::uint32_t i = 0u; i < frameCount; ++i)
                                    {
                                        const auto flag = workspace.flags[i];
                                        if (flag == RejectionFlag::Low || flag == RejectionFlag::ReplacedLow ||
                                            flag == RejectionFlag::High || flag == RejectionFlag::ReplacedHigh)
                                            ++counterRow[static_cast<std::size_t>(i) * 2u + 1u];
                                    }
                                const double value = sum / static_cast<double>(count);
                                const float narrowed = static_cast<float>(value);
                                if (!std::isfinite(sum) ||
                                    (targets.outputSampleType == SampleType::Float32 && !std::isfinite(narrowed)))
                                    return failure(ErrorCode::InvalidData, kCombine,
                                                   "a rejection combination left the finite range");
                                const auto element = channel * geometry->planeElements +
                                                     static_cast<std::uint64_t>(outputY) * geometry->width + x;
                                store(element, maskRow, x, value, narrowed, count, 0.0f, static_cast<float>(count),
                                      static_cast<float>(count), low, high);
                                continue;
                            }
                        }
                        const Status general = combineGeneral(x, channel, outputY, maskRow);
                        if (general.failed())
                            return general;
                    }
                }
                return Status::success();
            };
            Status ran = Status::success();
            if (!uniformRows)
                ran = runRows.template operator()<ValidityRule::AllValid, false>();
            else if (sharedRule == ValidityRule::FiniteOnly)
                ran = runRows.template operator()<ValidityRule::FiniteOnly, true>();
            else if (sharedRule == ValidityRule::ZeroIsNoData)
                ran = runRows.template operator()<ValidityRule::ZeroIsNoData, true>();
            else
                ran = runRows.template operator()<ValidityRule::AllValid, true>();
            if (ran.failed())
                return ran;
            admitted.fetch_add(local.history.samplesAdmitted, std::memory_order_relaxed);
            excluded.fetch_add(local.history.samplesExcludedByValidity, std::memory_order_relaxed);
            combined.fetch_add(local.samplesCombined, std::memory_order_relaxed);
            withoutWeight.fetch_add(local.samplesExcludedByWeight, std::memory_order_relaxed);
            withSamples.fetch_add(local.history.elementsWithSamples, std::memory_order_relaxed);
            lowEvents.fetch_add(local.rejectedLow, std::memory_order_relaxed);
            highEvents.fetch_add(local.rejectedHigh, std::memory_order_relaxed);
            mappedLow.fetch_add(local.mappedLow, std::memory_order_relaxed);
            mappedHigh.fetch_add(local.mappedHigh, std::memory_order_relaxed);
            return Status::success();
        });
    if (status.failed())
        return status;
    if (counters != nullptr)
    {
        for (std::uint32_t worker = 0u; worker < execution.admittedConcurrency(); ++worker)
        {
            const std::uint64_t* const row =
                counters->workerRows.data() + static_cast<std::size_t>(worker) * frames.size() * 2u;
            for (std::size_t i = 0u; i < frames.size(); ++i)
            {
                counters->admitted[i] += row[i * 2u];
                counters->rejected[i] += row[i * 2u + 1u];
            }
        }
    }
    StackRejectionOutcome outcome;
    outcome.history.samplesAdmitted = admitted.load();
    outcome.history.samplesExcludedByValidity = excluded.load();
    outcome.history.elementsCombined = items * geometry->width;
    outcome.history.elementsWithSamples = withSamples.load();
    outcome.history.elementsWithoutSamples = outcome.history.elementsCombined - outcome.history.elementsWithSamples;
    outcome.history.backend = *selected;
    outcome.history.compiledRowUsed = compiledUsed.load(std::memory_order_relaxed);
    outcome.rejectedLow = lowEvents.load();
    outcome.rejectedHigh = highEvents.load();
    outcome.mappedLow = mappedLow.load();
    outcome.mappedHigh = mappedHigh.load();
    outcome.samplesCombined = combined.load();
    outcome.samplesExcludedByWeight = withoutWeight.load();
    outcome.scratchBytesPerWorker = stackScratchBytesPerWorker(model);
    return outcome;
}

[[nodiscard]] Result<StackRejectionOutcome>
combineRejected(const ExecutionContext& execution, const StackModel& model, std::span<const StackFrameBand> frames,
                std::span<const std::span<std::byte>> workerScratch, const StackFinalizeTargets& targets,
                std::span<const double> frameWeights, const StackRejectionMaps& maps,
                StackRejectionCombinationPolicy combinationPolicy, const StackRejectionFrameCounters* counters,
                const stacking::StackAccumulatorWindow* window = nullptr)
{
    // checkHistory proves that every band has the model's tile type before either loop reads it.
    return stackTileSampleTypeOf(model.request().input.descriptor.sampleType()) == SampleType::Float64
               ? combineRejectedAs<double>(execution, model, frames, workerScratch, targets, frameWeights, maps,
                                           combinationPolicy, counters, window)
               : combineRejectedAs<float>(execution, model, frames, workerScratch, targets, frameWeights, maps,
                                          combinationPolicy, counters, window);
}

} // namespace

Result<StackRejectionOutcome>
combineRejectedStackSampleHistory(const ExecutionContext& execution, const StackModel& model,
                                  std::span<const StackFrameBand> frames,
                                  std::span<const std::span<std::byte>> workerScratch,
                                  const StackFinalizeTargets& targets, std::span<const double> frameWeights,
                                  const StackRejectionMaps& maps, StackRejectionCombinationPolicy combinationPolicy)
{
    return combineRejected(execution, model, frames, workerScratch, targets, frameWeights, maps, combinationPolicy,
                           nullptr);
}

Result<StackRejectionOutcome> combineRejectedStackSampleHistory(
    const ExecutionContext& execution, const StackModel& model, std::span<const StackFrameBand> frames,
    std::span<const std::span<std::byte>> workerScratch, const StackFinalizeTargets& targets,
    std::span<const double> frameWeights, const StackRejectionMaps& maps,
    StackRejectionCombinationPolicy combinationPolicy, const StackRejectionFrameCounters& counters)
{
    return combineRejected(execution, model, frames, workerScratch, targets, frameWeights, maps, combinationPolicy,
                           &counters);
}

namespace stacking
{
Result<StackRejectionOutcome> combineRejectedStackSampleHistoryWindow(
    const ExecutionContext& execution, const StackModel& model, std::span<const StackFrameBand> frames,
    std::span<const std::span<std::byte>> workerScratch, const StackFinalizeTargets& targets,
    std::span<const double> frameWeights, const StackRejectionMaps& maps,
    StackRejectionCombinationPolicy combinationPolicy, const StackRejectionFrameCounters& counters,
    const StackAccumulatorWindow& window)
{
    return combineRejected(execution, model, frames, workerScratch, targets, frameWeights, maps, combinationPolicy,
                           &counters, &window);
}
} // namespace stacking

} // namespace an
