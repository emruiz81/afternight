// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack accumulators: one frame's band into the running terms, and the
// terms into the result, over the whole output or over one tile's rows.
//
// Two things in this file are decisions rather than mechanics.
//
// **The band is partitioned by row and channel, and an element belongs to one worker.** A run is
// one row of one channel, which is contiguous in a planar layout and strided in an interleaved
// one; the runs are spread across the admitted workers, no two runs share an element, and a run
// folds every band of the call into its elements in the order the bands are listed. So there is no
// merge across threads, each element receives its frames in one order, and the answer at sixteen
// threads is the answer at one. The counts the operation reports are integers added through
// atomics, whose order cannot change a sum.
//
// **The scalar row is included here as the authority, and the compiled row is asked for.** The
// row body in `kernels/reference/StackAccumulatorRow.inl` is compiled into this unit inside an
// unnamed namespace; a planar row on a CPU backend runs the instruction set's compiled row when
// this build has one, and everything else - the reference backend, a build without that row, an
// interleaved band - runs the authority. The two are held to the same bits by the differential
// tests, which is what lets an outcome say which ran without the answer depending on it.

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMemoryRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackAccumulatorRules.h>

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/stacking/StackAccumulatorWindow.h"
#include "kernels/reference/AlgorithmKernels.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

// The scalar authority every backend is held to.
#include "kernels/reference/StackAccumulatorRow.inl"

constexpr const char* kCheck = "an::checkStackAccumulatorSet";
constexpr const char* kCheckBand = "an::checkStackFrameBand";
constexpr const char* kCheckTargets = "an::checkStackFinalizeTargets";
constexpr const char* kAccumulate = "an::accumulateStackFrame";
constexpr const char* kFinalize = "an::finalizeStackAccumulators";

/// A failure of one of this unit's operations.
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

/// A refusal naming the parameter that does not describe the stack.
///
/// @param code why.
/// @param operation which operation.
/// @param parameter the parameter, spelled as a caller reads it.
/// @param message what is wrong with it.
/// @return the failure, with the parameter as a context field.
[[nodiscard]] Status refuse(ErrorCode code, const char* operation, const char* parameter, std::string message)
{
    return failure(code, operation, std::move(message), {StatusContextField::ofText(ContextKey::Parameter, parameter)});
}

/// Whether a pointer begins on a boundary its element type can be read from.
///
/// @param pointer the address.
/// @param alignment the element's alignment.
/// @return true when the address is a multiple of the alignment.
[[nodiscard]] bool pointerIsAligned(const void* pointer, std::size_t alignment) noexcept
{
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0u;
}

/// The public update, spelled the way the compiled row reads it.
///
/// @param update the public update.
/// @return the private one.
[[nodiscard]] StackAccumulateRunUpdate runUpdateOf(StackAccumulatorUpdate update) noexcept
{
    switch (update)
    {
    case StackAccumulatorUpdate::WeightedSum:
        return StackAccumulateRunUpdate::WeightedSum;
    case StackAccumulatorUpdate::Minimum:
        return StackAccumulateRunUpdate::Minimum;
    case StackAccumulatorUpdate::Maximum:
        return StackAccumulateRunUpdate::Maximum;
    }
    return StackAccumulateRunUpdate::WeightedSum;
}

/// Checks one term of a set against what the model keeps.
///
/// @param model the checked stack.
/// @param accumulator which term.
/// @param present how many entries the caller offered.
/// @param aligned whether the entries begin on their element boundary.
/// @param elements the output elements every kept term covers.
/// @param name the term, spelled as a caller reads it.
/// @return success, or the refusal naming the term.
[[nodiscard]] Status checkTerm(const StackModel& model, StackAccumulator accumulator, std::uint64_t present,
                               bool aligned, std::uint64_t elements, const char* name)
{
    const std::uint64_t perElement = accumulator == StackAccumulator::VarianceMoments ? 2u : 1u;
    if (!stackAccumulatorKept(model, accumulator))
    {
        if (present != 0u)
        {
            return refuse(ErrorCode::IncompatibleArguments, kCheck, name,
                          std::string("the accumulator set offers ") + name +
                              ", which this stack does not keep; a term that is offered would be believed read");
        }
        return Status::success();
    }
    if (present != elements * perElement)
    {
        return refuse(ErrorCode::IncompatibleArguments, kCheck, name,
                      std::string("the accumulator set's ") + name + " does not hold one entry" +
                          (perElement == 2u ? " pair" : "") + " per output element");
    }
    if (!aligned)
    {
        return refuse(ErrorCode::IncompatibleArguments, kCheck, name,
                      std::string("the accumulator set's ") + name + " does not begin on its element boundary");
    }
    return Status::success();
}

/// Checks a set against a model and a stated element count, under a stated operation.
///
/// @param model the checked stack.
/// @param set the terms.
/// @param operation the operation the refusal is reported under.
/// @param elements how many elements every kept term covers: the output's, or one window's.
/// @return the element count, or the refusal.
[[nodiscard]] Result<std::uint64_t> checkSetOver(const StackModel& model, const StackAccumulatorSet& set,
                                                 const char* operation, std::uint64_t elements)
{
    if (stackAccumulationStrategyOf(model) != StackAccumulationStrategy::WholeOutputAccumulators)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "model",
                      "the stack holds sample history rather than whole-output accumulators; its samples are "
                      "combined by the estimators over a tile, not by these terms");
    }
    const std::array<Status, kStackAccumulatorCount> checks{
        checkTerm(model, StackAccumulator::Sums, set.sums.size(), pointerIsAligned(set.sums.data(), alignof(double)),
                  elements, "sums"),
        checkTerm(model, StackAccumulator::Counts, set.counts.size(),
                  pointerIsAligned(set.counts.data(), alignof(std::uint32_t)), elements, "counts"),
        checkTerm(model, StackAccumulator::WeightSums, set.weightSums.size(),
                  pointerIsAligned(set.weightSums.data(), alignof(double)), elements, "weightSums"),
        checkTerm(model, StackAccumulator::WeightSquareSums, set.weightSquareSums.size(),
                  pointerIsAligned(set.weightSquareSums.data(), alignof(double)), elements, "weightSquareSums"),
        checkTerm(model, StackAccumulator::VarianceMoments, set.varianceMoments.size(),
                  pointerIsAligned(set.varianceMoments.data(), alignof(double)), elements, "varianceMoments")};
    for (const Status& check : checks)
    {
        if (check.failed())
        {
            return check;
        }
    }
    return elements;
}

/// Checks a set against a model and reports the output element count, under a stated operation.
///
/// @param model the checked stack.
/// @param set the terms.
/// @param operation the operation the refusal is reported under.
/// @return the output element count, or the refusal.
[[nodiscard]] Result<std::uint64_t> checkSet(const StackModel& model, const StackAccumulatorSet& set,
                                             const char* operation)
{
    return checkSetOver(model, set, operation, model.request().input.descriptor.extent().sampleCount());
}

/// What a band's layout says about where its samples are, once it has been checked.
struct BandGeometry
{
    /// The band's width, which is the output's.
    std::uint32_t width = 0u;
    /// The band's rows.
    std::uint32_t rows = 0u;
    /// The band's channels, which are the output's.
    std::uint32_t channels = 0u;
    /// Components per pixel of a plane: one for a planar band, more for an interleaved or mixed
    /// one.
    std::uint32_t samplesPerPixel = 1u;
    /// The output's rows.
    std::uint64_t outputRows = 0u;
    /// The output's pixels per channel plane.
    std::uint64_t planeElements = 0u;
    /// The mask's bit-planes: zero under a rule that reads no mask, else one shared or one per
    /// channel.
    std::uint32_t maskPlanes = 0u;
};

/// Checks a band against the model and the output, and describes its geometry, under a stated
/// operation.
///
/// Every refusal begins with the spelling of the member that does not describe the stack, so a
/// caller reading the message finds the parameter without the context field.
///
/// @param model the checked stack.
/// @param band the band.
/// @param operation the operation the refusal is reported under.
/// @return the geometry, or the first refusal.
[[nodiscard]] Result<BandGeometry> checkBandAs(const StackModel& model, const StackFrameBand& band,
                                               const char* operation, bool storedSamples = false)
{
    const StackRequest& request = model.request();
    const ImageExtent& output = request.input.descriptor.extent();
    const SampleType tileType = stackTileSampleTypeOf(request.input.descriptor.sampleType());
    const SampleType expected = storedSamples ? request.input.descriptor.sampleType() : tileType;
    if (storedSamples && !stacking::stackAccumulatesStoredSamples(model))
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "band.sampleType",
                      "band.sampleType is the stored representation and this stack's widening of it is not the "
                      "identity");
    }
    if (band.sampleType != expected)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "band.sampleType",
                      std::string("band.sampleType names ") + sampleTypeName(band.sampleType) +
                          " and the plan's tile width for this stack is " + sampleTypeName(expected));
    }
    if (band.samples == nullptr || band.layout == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, operation, "band", "band has a null sample pointer or layout");
    }
    const ImageLayout& layout = *band.layout;
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(band.sampleType))
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "band.layout",
                      "band.layout does not describe elements of the band's sample type");
    }
    if (!pointerIsAligned(band.samples, layout.elementSizeBytes()))
    {
        return refuse(ErrorCode::InvalidArgument, operation, "band.samples",
                      "band.samples do not begin on their element boundary");
    }
    const ImageExtent& extent = layout.extent();
    if (extent.width() != output.width() || extent.channelCount() != output.channelCount())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "band.layout",
                      "band.layout does not have the output's width or channel count");
    }
    if (static_cast<std::uint64_t>(layout.planeCount()) * layout.samplesPerPixel() != extent.channelCount())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "band.layout",
                      "band.layout's planes and components do not address every channel exactly once");
    }
    if (band.frameIndex >= request.input.frameCount)
    {
        return failure(ErrorCode::OutOfRange, operation, "band.frameIndex is not below the stack's frame count",
                       {StatusContextField::ofText(ContextKey::Parameter, "band.frameIndex"),
                        StatusContextField::ofCount(ContextKey::Requested, band.frameIndex),
                        StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount)});
    }
    if (band.firstOutputRow >= output.height() ||
        static_cast<std::uint64_t>(band.firstOutputRow) + extent.height() > output.height())
    {
        return failure(ErrorCode::OutOfRange, operation,
                       "band.firstOutputRow and the band's rows do not lie inside the output",
                       {StatusContextField::ofText(ContextKey::Parameter, "band.firstOutputRow"),
                        StatusContextField::ofCount(ContextKey::Requested,
                                                    static_cast<std::uint64_t>(band.firstOutputRow) + extent.height()),
                        StatusContextField::ofCount(ContextKey::Limit, output.height())});
    }

    BandGeometry geometry;
    geometry.width = extent.width();
    geometry.rows = extent.height();
    geometry.channels = extent.channelCount();
    geometry.samplesPerPixel = layout.samplesPerPixel();
    geometry.outputRows = output.height();
    geometry.planeElements = output.pixelCount();

    const ValidityDescriptor& rule = band.validity.descriptor;
    if (!rule.needsMask())
    {
        if (band.validity.mask != nullptr)
        {
            return refuse(ErrorCode::InvalidArgument, operation, "band.validity.mask",
                          "band.validity.mask was supplied under a validity rule that reads no packed mask");
        }
    }
    else
    {
        if (band.validity.mask == nullptr)
        {
            return refuse(ErrorCode::InvalidArgument, operation, "band.validity.mask",
                          "band.validity.mask is required by the explicit-mask rule and none was given");
        }
        const ValidityMaskView<const std::uint8_t>& mask = *band.validity.mask;
        if (mask.width() != geometry.width || mask.height() != geometry.rows ||
            (mask.planeCount() != 1u && mask.planeCount() != geometry.channels))
        {
            return refuse(ErrorCode::IncompatibleArguments, operation, "band.validity.mask",
                          "band.validity.mask does not cover the band's extent with one shared bit-plane or one "
                          "per channel");
        }
        geometry.maskPlanes = mask.planeCount();
    }

    const std::uint64_t pixels = static_cast<std::uint64_t>(geometry.width) * geometry.rows;
    if (model.usesLocalWeights())
    {
        if (band.localWeights.size() != pixels)
        {
            return refuse(ErrorCode::IncompatibleArguments, operation, "band.localWeights",
                          "band.localWeights do not hold one weight per pixel of a stack that applies a local "
                          "weighting");
        }
    }
    else if (!band.localWeights.empty())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "band.localWeights",
                      "band.localWeights were supplied for a stack that applies no local weighting");
    }
    return geometry;
}

/// Checks the frame's row of the borrowed weight table.
///
/// @param model the checked stack.
/// @param band the band, for its frame.
/// @param channels how many weights the row holds.
/// @param frameWeights the table.
/// @return success, or the refusal.
[[nodiscard]] Status checkFrameWeights(const StackModel& model, const StackFrameBand& band, std::uint32_t channels,
                                       std::span<const double> frameWeights)
{
    if (!model.usesFrameWeights())
    {
        return Status::success();
    }
    const std::uint64_t expected = static_cast<std::uint64_t>(model.request().input.frameCount) * channels;
    if (frameWeights.size() != expected)
    {
        return refuse(ErrorCode::IncompatibleArguments, kAccumulate, "frameWeights",
                      "the frameWeights table does not hold one weight per frame and channel");
    }
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const std::uint64_t index = static_cast<std::uint64_t>(band.frameIndex) * channels + channel;
        const double weight = frameWeights[static_cast<std::size_t>(index)];
        if (!std::isfinite(weight) || weight < 0.0)
        {
            return failure(ErrorCode::InvalidArgument, kAccumulate,
                           "a frameWeights entry of this frame is not finite and non-negative",
                           {StatusContextField::ofText(ContextKey::Parameter, "frameWeights"),
                            StatusContextField::ofCount(ContextKey::Requested, index)});
        }
    }
    return Status::success();
}

/// Accumulates one band row of one channel into its terms: through the compiled row when there is
/// one and the band is planar, and through the scalar authority otherwise, setting `compiledUsed`
/// when the compiled row ran.
template <typename SampleT>
[[nodiscard]] StackAccumulateRunOutcome
accumulateRow(const StackFrameBand& band, const BandGeometry& geometry, std::uint32_t channel, std::uint32_t y,
              const StackAccumulateRunParameters& parameters, const StackAccumulatorRunTargets& targets,
              StackAccumulateRunOutcome (*compiled)(const SampleT*, std::uint64_t, const StackAccumulateRunParameters&,
                                                    const StackAccumulatorRunTargets&) noexcept,
              bool& compiledUsed) noexcept
{
    const std::uint32_t plane = channel / geometry.samplesPerPixel;
    const std::uint32_t component = channel % geometry.samplesPerPixel;
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(band.samples);
    const SampleT* const row =
        reinterpret_cast<const SampleT*>(bytes + *band.layout->sampleByteOffset(plane, y, 0u, component));
    if (compiled != nullptr && geometry.samplesPerPixel == 1u)
    {
        compiledUsed = true;
        return compiled(row, geometry.width, parameters, targets);
    }
    return stackAccumulateRowOf<SampleT>(row, geometry.samplesPerPixel, geometry.width, parameters, targets);
}

/// The operation's tallies, combined once per worker range.
struct BandTally
{
    std::atomic<std::uint64_t> accumulated{0u};
    std::atomic<std::uint64_t> excludedByValidity{0u};
    std::atomic<std::uint64_t> excludedByWeight{0u};
    std::atomic<bool> compiled{false};
};

/// How many bands one call of the multi-band row takes; a stack of more takes them in groups, in order.
constexpr std::size_t kBandsPerRun = 256u;

/// Whether a run is plain enough for the multi-band row: a sum with no weight and no term beyond sums
/// and counts, every band planar, unmasked and under the first band's rule.
///
/// @param bands the bands.
/// @param weighted whether samples carry an effective weight.
/// @param update how a sample changes its term.
/// @param set the terms.
/// @return true when every band of a row can be taken at once.
[[nodiscard]] bool plainRunOf(std::span<const StackFrameBand> bands, bool weighted, StackAccumulateRunUpdate update,
                              const StackAccumulatorSet& set) noexcept
{
    if (weighted || update != StackAccumulateRunUpdate::WeightedSum || !set.weightSums.empty() ||
        !set.weightSquareSums.empty() || !set.varianceMoments.empty() || bands.empty())
    {
        return false;
    }
    const ValidityDescriptor& first = bands[0].validity.descriptor;
    for (const StackFrameBand& band : bands)
    {
        const ValidityDescriptor& rule = band.validity.descriptor;
        if (band.validity.mask != nullptr || band.layout->samplesPerPixel() != 1u || rule.needsMask() ||
            rule.rule() != first.rule() || rule.lowestValid() != first.lowestValid() ||
            rule.highestValid() != first.highestValid())
        {
            return false;
        }
    }
    return true;
}

/// Accumulates a plain run's bands a row at a time, every band of the row in one call of the
/// multi-band row - the compiled one, or the scalar authority when the table has none.
///
/// @tparam SampleT the bands' representation.
/// @param execution the operation.
/// @param set the terms.
/// @param bands the bands, all of one geometry.
/// @param geometry the common checked geometry.
/// @param window the output rows the terms hold.
/// @param cost the work of one row.
/// @param bandsRun the compiled multi-band row, or null.
/// @param tally the operation's tallies.
/// @param outcome receives the totals.
/// @param admittedPerBand receives each band's counts.
/// @return success, or the first failure in range order.
template <typename SampleT>
[[nodiscard]] Status accumulateBandsAs(const ExecutionContext& execution, const StackAccumulatorSet& set,
                                       std::span<const StackFrameBand> bands, const BandGeometry& geometry,
                                       const stacking::StackAccumulatorWindow& window, const TaskCost& cost,
                                       StackAccumulateBandsRunFn<SampleT> bandsRun, BandTally& tally,
                                       StackAccumulationOutcome& outcome, std::span<std::uint64_t> admittedPerBand)
{
    const ValidityDescriptor& rule = bands[0].validity.descriptor;
    StackAccumulateRunParameters parameters;
    parameters.update = StackAccumulateRunUpdate::WeightedSum;
    parameters.rule = rule.rule();
    parameters.lowestValid = rule.lowestValid().value_or(0.0);
    parameters.highestValid = rule.highestValid().value_or(0.0);
    const std::uint64_t items = static_cast<std::uint64_t>(geometry.rows) * geometry.channels;
    const Status status = parallelFor(execution, items, cost, [&](TaskRange range) -> Status {
        std::array<const SampleT*, kBandsPerRun> rows{};
        std::array<std::uint64_t, kBandsPerRun> admitted{};
        StackAccumulateRunOutcome local{};
        for (std::uint64_t item = range.begin; item < range.end; ++item)
        {
            const std::uint32_t channel = static_cast<std::uint32_t>(item / geometry.rows);
            const std::uint32_t y = static_cast<std::uint32_t>(item % geometry.rows);
            const std::uint64_t termsPlane = static_cast<std::uint64_t>(window.rows) * geometry.width;
            const std::uint64_t element =
                channel * termsPlane +
                (static_cast<std::uint64_t>(bands[0].firstOutputRow - window.firstRow) + y) * geometry.width;
            const StackAccumulatorRunTargets targets{.sums = set.sums.data() + element,
                                                     .counts = set.counts.data() + element,
                                                     .weightSums = nullptr,
                                                     .weightSquareSums = nullptr,
                                                     .varianceMoments = nullptr};
            for (std::size_t first = 0u; first < bands.size(); first += kBandsPerRun)
            {
                const std::size_t group = (std::min)(kBandsPerRun, bands.size() - first);
                for (std::size_t index = 0u; index < group; ++index)
                {
                    const StackFrameBand& band = bands[first + index];
                    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(band.samples);
                    rows[index] =
                        reinterpret_cast<const SampleT*>(bytes + *band.layout->sampleByteOffset(channel, y, 0u, 0u));
                    admitted[index] = 0u;
                }
                const StackAccumulateRunOutcome run =
                    bandsRun != nullptr
                        ? bandsRun(rows.data(), static_cast<std::uint32_t>(group), geometry.width, parameters, targets,
                                   admitted.data())
                        : stackAccumulateBandsRowOf<SampleT>(rows.data(), static_cast<std::uint32_t>(group),
                                                             geometry.width, parameters, targets, admitted.data());
                local.accumulated += run.accumulated;
                local.excludedByValidity += run.excludedByValidity;
                for (std::size_t index = 0u; index < group; ++index)
                {
                    std::atomic_ref<std::uint64_t>(admittedPerBand[first + index])
                        .fetch_add(admitted[index], std::memory_order_relaxed);
                }
                if (!run.finite)
                {
                    return failure(ErrorCode::InvalidData, kAccumulate,
                                   "an accumulator term left the finite range: an admitted sample is not finite, "
                                   "which a validity rule should exclude, or a sum overflowed Float64");
                }
            }
        }
        tally.accumulated.fetch_add(local.accumulated, std::memory_order_relaxed);
        tally.excludedByValidity.fetch_add(local.excludedByValidity, std::memory_order_relaxed);
        if (bandsRun != nullptr)
            tally.compiled.store(true, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    outcome.samplesAccumulated = tally.accumulated.load();
    outcome.samplesExcludedByValidity = tally.excludedByValidity.load();
    outcome.samplesExcludedByWeight = 0u;
    outcome.compiledRowUsed = tally.compiled.load();
    outcome.elementsCovered = items * geometry.width;
    return Status::success();
}

/// Spreads a tile's rows and channels across the workers and accumulates every band of it, with
/// the band loop inside the row loop.
///
/// @tparam SampleT the bands' representation.
/// @param execution the operation.
/// @param model the checked stack.
/// @param set the terms.
/// @param bands the bands, all of one geometry.
/// @param geometry the common checked geometry; each band retains its own layout and mask.
/// @param window the output rows the terms hold: every row of the output, or one tile's.
/// @param frameWeights the borrowed frame weight table, or empty under uniform weighting.
/// @param compiled the compiled row, or null.
/// @param outcome receives the totals.
/// @param admittedPerBand receives each band's counts, added to atomically by the workers.
/// @return success, or the first failure in range order, which runs channel by channel and row by
///         row within a channel.
template <typename SampleT>
[[nodiscard]] Status
accumulateAs(const ExecutionContext& execution, const StackModel& model, const StackAccumulatorSet& set,
             std::span<const StackFrameBand> bands, const BandGeometry& geometry,
             const stacking::StackAccumulatorWindow& window, std::span<const double> frameWeights,
             StackAccumulateRunOutcome (*compiled)(const SampleT*, std::uint64_t, const StackAccumulateRunParameters&,
                                                   const StackAccumulatorRunTargets&) noexcept,
             StackAccumulateBandsRunFn<SampleT> bandsRun, StackAccumulationOutcome& outcome,
             std::span<std::uint64_t> admittedPerBand)
{
    static_assert(std::atomic_ref<std::uint64_t>::required_alignment <= alignof(std::uint64_t));
    const bool weighted = model.usesFrameWeights() || model.usesLocalWeights();
    const StackAccumulateRunUpdate update = runUpdateOf(stackAccumulatorUpdateOf(model));
    BandTally tally;
    const std::uint64_t items = static_cast<std::uint64_t>(geometry.rows) * geometry.channels;
    const std::uint64_t bandCount = bands.size();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(geometry.width) * 48u * bandCount,
                        .operationsPerItem = static_cast<std::uint64_t>(geometry.width) * 16u * bandCount};
    if (plainRunOf(bands, weighted, update, set))
    {
        return accumulateBandsAs<SampleT>(execution, set, bands, geometry, window, cost, bandsRun, tally, outcome,
                                          admittedPerBand);
    }
    const Status status = parallelFor(execution, items, cost, [&](TaskRange range) -> Status {
        StackAccumulateRunOutcome local{};
        bool localCompiled = false;
        for (std::uint64_t item = range.begin; item < range.end; ++item)
        {
            const std::uint32_t channel = static_cast<std::uint32_t>(item / geometry.rows);
            const std::uint32_t y = static_cast<std::uint32_t>(item % geometry.rows);
            const std::uint64_t termsPlane = static_cast<std::uint64_t>(window.rows) * geometry.width;
            const std::uint64_t element =
                channel * termsPlane +
                (static_cast<std::uint64_t>(bands[0].firstOutputRow - window.firstRow) + y) * geometry.width;
            const std::uint64_t pixel = static_cast<std::uint64_t>(y) * geometry.width;
            const StackAccumulatorRunTargets targets{
                .sums = set.sums.data() + element,
                .counts = set.counts.data() + element,
                .weightSums = set.weightSums.empty() ? nullptr : set.weightSums.data() + element,
                .weightSquareSums = set.weightSquareSums.empty() ? nullptr : set.weightSquareSums.data() + element,
                .varianceMoments = set.varianceMoments.empty() ? nullptr : set.varianceMoments.data() + 2u * element};
            for (std::size_t index = 0u; index < bandCount; ++index)
            {
                const StackFrameBand& band = bands[index];
                BandGeometry own = geometry;
                own.samplesPerPixel = band.layout->samplesPerPixel();
                own.maskPlanes = band.validity.mask == nullptr ? 0u : band.validity.mask->planeCount();
                const ValidityDescriptor& rule = band.validity.descriptor;
                StackAccumulateRunParameters parameters;
                parameters.update = update;
                parameters.rule = rule.rule();
                parameters.lowestValid = rule.lowestValid().value_or(0.0);
                parameters.highestValid = rule.highestValid().value_or(0.0);
                if (own.maskPlanes != 0u)
                {
                    const ValidityMaskView<const std::uint8_t>& mask = *band.validity.mask;
                    const std::uint32_t maskPlane = own.maskPlanes == 1u ? 0u : channel;
                    parameters.maskBits = mask.data() +
                                          static_cast<std::uint64_t>(maskPlane) * mask.planeStrideBytes() +
                                          static_cast<std::uint64_t>(y) * mask.rowStrideBytes();
                    parameters.maskBitOffset = 0u;
                }
                parameters.frameWeight =
                    model.usesFrameWeights()
                        ? frameWeights[static_cast<std::size_t>(band.frameIndex) * geometry.channels + channel]
                        : 1.0;
                parameters.localWeights =
                    band.localWeights.empty() ? nullptr : band.localWeights.data() + static_cast<std::size_t>(pixel);
                parameters.weighted = weighted;
                const StackAccumulateRunOutcome run =
                    accumulateRow<SampleT>(band, own, channel, y, parameters, targets, compiled, localCompiled);
                local.accumulated += run.accumulated;
                local.excludedByValidity += run.excludedByValidity;
                local.excludedByWeight += run.excludedByWeight;
                std::atomic_ref<std::uint64_t>(admittedPerBand[index])
                    .fetch_add(run.accumulated + run.excludedByWeight, std::memory_order_relaxed);
                if (!run.finite)
                {
                    return failure(ErrorCode::InvalidData, kAccumulate,
                                   "an accumulator term left the finite range: an admitted sample is not finite, "
                                   "which a validity rule should exclude, or a sum overflowed Float64");
                }
            }
        }
        tally.accumulated.fetch_add(local.accumulated, std::memory_order_relaxed);
        tally.excludedByValidity.fetch_add(local.excludedByValidity, std::memory_order_relaxed);
        tally.excludedByWeight.fetch_add(local.excludedByWeight, std::memory_order_relaxed);
        if (localCompiled)
            tally.compiled.store(true, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    outcome.samplesAccumulated = tally.accumulated.load();
    outcome.samplesExcludedByValidity = tally.excludedByValidity.load();
    outcome.samplesExcludedByWeight = tally.excludedByWeight.load();
    outcome.compiledRowUsed = tally.compiled.load();
    outcome.elementsCovered = items * geometry.width;
    return Status::success();
}

/// How an element's value is formed from its terms.
enum class Finalization : std::uint8_t
{
    /// The sum over the weight sum: an average whose stack weighs its samples.
    WeightedMean,
    /// The sum over the count: an average that keeps no weight sums.
    PlainMean,
    /// The sum itself.
    Sum,
    /// The minimum or maximum the sum term holds.
    Extremum
};

/// Checks one map target against what the model requests, under a stated operation.
///
/// @param requested whether the model writes the map.
/// @param present how many entries the caller offered.
/// @param elements the output elements.
/// @param name the map, spelled as a caller reads it.
/// @param operation the operation the refusal is reported under.
/// @return success, or the refusal naming the map.
[[nodiscard]] Status checkMap(bool requested, std::uint64_t present, std::uint64_t elements, const char* name,
                              const char* operation)
{
    if (!requested)
    {
        if (present != 0u)
        {
            return refuse(ErrorCode::IncompatibleArguments, operation, name,
                          std::string("the ") + name + " map was supplied and the stack was not asked to write it");
        }
        return Status::success();
    }
    if (present != elements)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, name,
                      std::string("the ") + name + " map does not hold one entry per output element");
    }
    return Status::success();
}

/// Checks finalization targets against the model's output, or against a window of its rows, under a
/// stated operation.
///
/// @param model the checked stack.
/// @param targets where the image, the mask and the maps go.
/// @param operation the operation the refusal is reported under.
/// @param targetRows how many output rows the targets hold: zero for every row of the output.
/// @return the element count the targets hold, or the first refusal.
[[nodiscard]] Result<std::uint64_t> checkTargetsAs(const StackModel& model, const StackFinalizeTargets& targets,
                                                   const char* operation, std::uint32_t targetRows = 0u)
{
    const StackRequest& request = model.request();
    const ImageExtent& extent = request.input.descriptor.extent();
    const std::uint32_t rows = targetRows == 0u ? extent.height() : targetRows;
    const std::uint64_t elements = static_cast<std::uint64_t>(extent.width()) * rows * extent.channelCount();
    if (targets.outputSampleType != request.output.sampleType)
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "outputSampleType",
                      std::string("outputSampleType names ") + sampleTypeName(targets.outputSampleType) +
                          " and the output is written as " + sampleTypeName(request.output.sampleType));
    }
    if (targets.outputImage.size() != elements * sampleTypeSizeBytes(targets.outputSampleType))
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "outputImage",
                      "the outputImage does not hold one sample per output element at the output representation");
    }
    if (targets.outputMask != nullptr &&
        (targets.outputMask->width() != extent.width() || targets.outputMask->height() != rows ||
         targets.outputMask->planeCount() != extent.channelCount()))
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "outputMask",
                      "the outputMask does not cover the output with one bit-plane per channel");
    }
    const std::array<Status, 4u> maps{
        checkMap(request.output.coverageMap, targets.coverage.size(), elements, "coverage", operation),
        checkMap(request.output.varianceMap, targets.variance.size(), elements, "variance", operation),
        checkMap(request.output.signalToNoiseGainMap, targets.signalToNoiseGain.size(), elements, "signalToNoiseGain",
                 operation),
        checkMap(request.output.weightSumMap, targets.weightSum.size(), elements, "weightSum", operation)};
    for (const Status& map : maps)
    {
        if (map.failed())
        {
            return map;
        }
    }
    return elements;
}

/// Writes one output row of one channel from its terms.
///
/// @param set the terms.
/// @param targets where the row goes.
/// @param finalization how a value is formed.
/// @param divisor what every value is divided by before it is narrowed; one leaves it as it is.
/// @param element the row's first output element.
/// @param term the row's first element of the terms, which is `element` when they cover the output.
/// @param width the row's elements.
/// @param channel the channel, which is the mask's bit-plane.
/// @param y the output row.
/// @param withSamples receives how many elements carried a value.
/// @param nonFinite receives how many values were not finite.
void finalizeRow(const StackAccumulatorSet& set, const StackFinalizeTargets& targets, Finalization finalization,
                 double divisor, std::uint64_t element, std::uint64_t term, std::uint32_t width, std::uint32_t channel,
                 std::uint32_t y, std::uint64_t& withSamples, std::uint64_t& nonFinite) noexcept
{
    const bool wide = targets.outputSampleType == SampleType::Float64;
    std::byte* const rowBytes = targets.outputImage.data() + element * (wide ? sizeof(double) : sizeof(float));
    std::uint8_t* maskRow = nullptr;
    if (targets.outputMask != nullptr)
    {
        maskRow = targets.outputMask->data() +
                  static_cast<std::uint64_t>(channel) * targets.outputMask->planeStrideBytes() +
                  static_cast<std::uint64_t>(y) * targets.outputMask->rowStrideBytes();
        std::memset(maskRow, 0, static_cast<std::size_t>(targets.outputMask->minimumRowBytes()));
    }
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const std::size_t index = static_cast<std::size_t>(element + x);
        const std::size_t at = static_cast<std::size_t>(term + x);
        const std::uint32_t count = set.counts[at];
        double value = 0.0;
        double weightSum = 0.0;
        double weightSquareSum = 0.0;
        double variance = 0.0;
        if (count != 0u)
        {
            const double sum = set.sums[at];
            weightSum = set.weightSums.empty() ? static_cast<double>(count) : set.weightSums[at];
            weightSquareSum = set.weightSquareSums.empty() ? static_cast<double>(count) : set.weightSquareSums[at];
            switch (finalization)
            {
            case Finalization::WeightedMean:
                value = sum / weightSum;
                break;
            case Finalization::PlainMean:
                value = sum / static_cast<double>(count);
                break;
            case Finalization::Sum:
            case Finalization::Extremum:
                value = sum;
                break;
            }
            value = value / divisor;
            if (!set.varianceMoments.empty() && count > 1u)
            {
                variance = set.varianceMoments[2u * at + 1u] / static_cast<double>(count - 1u);
            }
            ++withSamples;
            if (maskRow != nullptr)
            {
                maskRow[x / 8u] |= static_cast<std::uint8_t>(1u << (x % 8u));
            }
        }
        if (wide)
        {
            nonFinite += !std::isfinite(value) ? 1u : 0u;
            std::memcpy(rowBytes + static_cast<std::size_t>(x) * sizeof(double), &value, sizeof(double));
        }
        else
        {
            const float narrowed = static_cast<float>(value);
            nonFinite += !std::isfinite(narrowed) ? 1u : 0u;
            std::memcpy(rowBytes + static_cast<std::size_t>(x) * sizeof(float), &narrowed, sizeof(float));
        }
        if (!targets.coverage.empty())
        {
            targets.coverage[index] = count;
        }
        if (!targets.variance.empty())
        {
            targets.variance[index] = static_cast<float>(variance);
        }
        if (!targets.signalToNoiseGain.empty())
        {
            targets.signalToNoiseGain[index] =
                count == 0u ? 0.0f : static_cast<float>(weightSum * weightSum / weightSquareSum);
        }
        if (!targets.weightSum.empty())
        {
            targets.weightSum[index] = static_cast<float>(weightSum);
        }
    }
}

/// How a model's elements are formed from their terms.
///
/// @param model the checked stack.
/// @param set the terms, whose weight sums say whether the mean is weighted.
/// @return the finalization.
[[nodiscard]] Finalization finalizationOf(const StackModel& model, const StackAccumulatorSet& set) noexcept
{
    switch (model.request().combination)
    {
    case StackCombination::Average:
        return set.weightSums.empty() ? Finalization::PlainMean : Finalization::WeightedMean;
    case StackCombination::Sum:
        return Finalization::Sum;
    case StackCombination::Minimum:
    case StackCombination::Maximum:
    case StackCombination::Median:
    case StackCombination::AutoAdaptiveWeightedAverage:
        break;
    }
    return Finalization::Extremum;
}

/// Writes a window's output rows from terms that hold exactly the window, or the whole output, into
/// targets that hold exactly the window, or the whole output.
///
/// @param execution the operation.
/// @param model the checked stack.
/// @param set the terms.
/// @param targets where the rows go.
/// @param divisor what every value is divided by before it is narrowed.
/// @param window the output rows to write.
/// @param termsCoverWindow whether the terms hold only the window's rows rather than every row.
/// @param targetsCoverWindow whether the targets hold only the window's rows rather than every row.
/// @return what was written, or cancellation.
[[nodiscard]] Result<StackFinalizeOutcome> finalizeRows(const ExecutionContext& execution, const StackModel& model,
                                                        const StackAccumulatorSet& set,
                                                        const StackFinalizeTargets& targets, double divisor,
                                                        const stacking::StackAccumulatorWindow& window,
                                                        bool termsCoverWindow, bool targetsCoverWindow = false)
{
    const ImageExtent& extent = model.request().input.descriptor.extent();
    const Finalization finalization = finalizationOf(model, set);
    const std::uint64_t termsPlane =
        static_cast<std::uint64_t>(termsCoverWindow ? window.rows : extent.height()) * extent.width();
    const std::uint32_t termsFirstRow = termsCoverWindow ? window.firstRow : 0u;
    const std::uint64_t targetsPlane =
        static_cast<std::uint64_t>(targetsCoverWindow ? window.rows : extent.height()) * extent.width();
    const std::uint32_t targetsFirstRow = targetsCoverWindow ? window.firstRow : 0u;
    std::atomic<std::uint64_t> withSamples{0u};
    std::atomic<std::uint64_t> nonFinite{0u};
    const std::uint64_t items = static_cast<std::uint64_t>(window.rows) * extent.channelCount();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(extent.width()) * 40u,
                        .operationsPerItem = static_cast<std::uint64_t>(extent.width()) * 8u};
    const Status status = parallelFor(execution, items, cost, [&](TaskRange range) -> Status {
        std::uint64_t localWithSamples = 0u;
        std::uint64_t localNonFinite = 0u;
        for (std::uint64_t item = range.begin; item < range.end; ++item)
        {
            const std::uint32_t channel = static_cast<std::uint32_t>(item / window.rows);
            const std::uint32_t y = window.firstRow + static_cast<std::uint32_t>(item % window.rows);
            const std::uint64_t element =
                channel * targetsPlane + static_cast<std::uint64_t>(y - targetsFirstRow) * extent.width();
            const std::uint64_t term =
                channel * termsPlane + static_cast<std::uint64_t>(y - termsFirstRow) * extent.width();
            finalizeRow(set, targets, finalization, divisor, element, term, extent.width(), channel,
                        y - targetsFirstRow, localWithSamples, localNonFinite);
        }
        withSamples.fetch_add(localWithSamples, std::memory_order_relaxed);
        nonFinite.fetch_add(localNonFinite, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    StackFinalizeOutcome outcome;
    outcome.elementsWritten = items * extent.width();
    outcome.elementsWithSamples = withSamples.load();
    outcome.elementsWithoutSamples = outcome.elementsWritten - outcome.elementsWithSamples;
    outcome.nonFiniteOutputs = nonFinite.load();
    return outcome;
}

/// Accumulates the bands of one tile into terms that hold the stated window, under a stated
/// operation: `accumulateStackFrames` when the window is the whole output.
///
/// @param execution the operation.
/// @param model the checked stack.
/// @param set the terms.
/// @param window the output rows the terms hold.
/// @param elements how many elements every kept term covers.
/// @param bands one band per frame, all over the same rows inside the window.
/// @param frameWeights the frame weight table, or empty.
/// @param admittedPerBand receives each band's admitted count.
/// @return what the accumulation did, or the first refusal.
[[nodiscard]] Result<StackAccumulationOutcome>
accumulateInto(const ExecutionContext& execution, const StackModel& model, const StackAccumulatorSet& set,
               const stacking::StackAccumulatorWindow& window, std::uint64_t elements,
               std::span<const StackFrameBand> bands, std::span<const double> frameWeights,
               std::span<std::uint64_t> admittedPerBand, bool storedSamples = false)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kAccumulate, "the accumulation was given an empty execution context");
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
    const Result<std::uint64_t> checked = checkSetOver(model, set, kAccumulate, elements);
    if (!checked.hasValue())
    {
        return checked.status();
    }
    if (admittedPerBand.size() != bands.size())
    {
        return refuse(ErrorCode::IncompatibleArguments, kAccumulate, "admittedPerBand",
                      "admittedPerBand does not hold one entry per band");
    }
    StackAccumulationOutcome outcome;
    outcome.backend = *selected;
    if (bands.empty())
    {
        return outcome;
    }
    // Every band is checked before a term is written, and every band must be the same tile.
    BandGeometry common;
    for (const StackFrameBand& band : bands)
    {
        const Result<BandGeometry> geometry = checkBandAs(model, band, kAccumulate, storedSamples);
        if (!geometry.hasValue())
        {
            return geometry.status();
        }
        const Status weights = checkFrameWeights(model, band, geometry->channels, frameWeights);
        if (weights.failed())
        {
            return weights;
        }
        const bool sameTile = band.firstOutputRow == bands[0].firstOutputRow &&
                              band.sampleType == bands[0].sampleType &&
                              geometry->rows == bands[0].layout->extent().height();
        if (!sameTile)
        {
            return refuse(ErrorCode::IncompatibleArguments, kAccumulate, "bands",
                          "bands do not all describe the same rows at the same first output row and representation");
        }
        common = *geometry;
    }
    if (bands[0].firstOutputRow < window.firstRow || static_cast<std::uint64_t>(bands[0].firstOutputRow) + common.rows >
                                                         static_cast<std::uint64_t>(window.firstRow) + window.rows)
    {
        return refuse(ErrorCode::IncompatibleArguments, kAccumulate, "bands",
                      "bands describe rows outside the window the terms hold");
    }
    const bool reference = selected->backend == ComputeBackend::Reference;
    const StackAccumulateBandsRuns* const bandsRuns = reference ? nullptr : stackAccumulateBandsRunsFor(execution);
    const auto bandsRun = [bandsRuns](auto member) { return bandsRuns == nullptr ? nullptr : bandsRuns->*member; };
    Status status = Status::success();
    switch (bands[0].sampleType)
    {
    case SampleType::Float64:
        status = accumulateAs<double>(execution, model, set, bands, common, window, frameWeights,
                                      reference ? nullptr : stackAccumulateFloat64RunFor(execution),
                                      bandsRun(&StackAccumulateBandsRuns::float64), outcome, admittedPerBand);
        break;
    case SampleType::UInt16:
        status = accumulateAs<std::uint16_t>(execution, model, set, bands, common, window, frameWeights, nullptr,
                                             bandsRun(&StackAccumulateBandsRuns::uint16), outcome, admittedPerBand);
        break;
    case SampleType::Int16:
        status = accumulateAs<std::int16_t>(execution, model, set, bands, common, window, frameWeights, nullptr,
                                            bandsRun(&StackAccumulateBandsRuns::int16), outcome, admittedPerBand);
        break;
    case SampleType::UInt8:
        status = accumulateAs<std::uint8_t>(execution, model, set, bands, common, window, frameWeights, nullptr,
                                            bandsRun(&StackAccumulateBandsRuns::uint8), outcome, admittedPerBand);
        break;
    case SampleType::Int8:
        status = accumulateAs<std::int8_t>(execution, model, set, bands, common, window, frameWeights, nullptr,
                                           bandsRun(&StackAccumulateBandsRuns::int8), outcome, admittedPerBand);
        break;
    default:
        status = accumulateAs<float>(execution, model, set, bands, common, window, frameWeights,
                                     reference ? nullptr : stackAccumulateFloat32RunFor(execution),
                                     bandsRun(&StackAccumulateBandsRuns::float32), outcome, admittedPerBand);
        break;
    }
    if (status.failed())
    {
        return status;
    }
    return outcome;
}

} // namespace

const char* stackAccumulatorUpdateName(StackAccumulatorUpdate update) noexcept
{
    switch (update)
    {
    case StackAccumulatorUpdate::WeightedSum:
        return "WeightedSum";
    case StackAccumulatorUpdate::Minimum:
        return "Minimum";
    case StackAccumulatorUpdate::Maximum:
        return "Maximum";
    }
    return "UnknownStackAccumulatorUpdate";
}

Result<std::uint64_t> checkStackAccumulatorSet(const StackModel& model, const StackAccumulatorSet& set)
{
    return checkSet(model, set, kCheck);
}

Result<StackAccumulationOutcome> accumulateStackFrame(const ExecutionContext& execution, const StackModel& model,
                                                      const StackAccumulatorSet& set, const StackFrameBand& band,
                                                      std::span<const double> frameWeights)
{
    std::uint64_t admitted = 0u;
    return accumulateStackFrames(execution, model, set, std::span<const StackFrameBand>(&band, 1u), frameWeights,
                                 std::span<std::uint64_t>(&admitted, 1u));
}

Result<StackAccumulationOutcome> accumulateStackFrames(const ExecutionContext& execution, const StackModel& model,
                                                       const StackAccumulatorSet& set,
                                                       std::span<const StackFrameBand> bands,
                                                       std::span<const double> frameWeights,
                                                       std::span<std::uint64_t> admittedPerBand)
{
    const ImageExtent& extent = model.request().input.descriptor.extent();
    return accumulateInto(execution, model, set,
                          stacking::StackAccumulatorWindow{.firstRow = 0u, .rows = extent.height()},
                          extent.sampleCount(), bands, frameWeights, admittedPerBand);
}

Status checkStackFrameBand(const StackModel& model, const StackFrameBand& band)
{
    const Result<BandGeometry> geometry = checkBandAs(model, band, kCheckBand);
    return geometry.hasValue() ? Status::success() : geometry.status();
}

Result<std::uint64_t> checkStackFinalizeTargets(const StackModel& model, const StackFinalizeTargets& targets)
{
    return checkTargetsAs(model, targets, kCheckTargets);
}

Result<StackFinalizeOutcome> finalizeStackAccumulators(const ExecutionContext& execution, const StackModel& model,
                                                       const StackAccumulatorSet& set,
                                                       const StackFinalizeTargets& targets)
{
    return finalizeStackAccumulators(execution, model, set, targets, 1.0);
}

Result<StackFinalizeOutcome> finalizeStackAccumulators(const ExecutionContext& execution, const StackModel& model,
                                                       const StackAccumulatorSet& set,
                                                       const StackFinalizeTargets& targets, double valueDivisor)
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
    if (!(valueDivisor > 0.0) || !std::isfinite(valueDivisor))
    {
        return refuse(ErrorCode::InvalidArgument, kFinalize, "valueDivisor", "valueDivisor is not positive and finite");
    }
    const Result<std::uint64_t> checked = checkSet(model, set, kFinalize);
    if (!checked.hasValue())
    {
        return checked.status();
    }
    const Result<std::uint64_t> described = checkTargetsAs(model, targets, kFinalize);
    if (!described.hasValue())
    {
        return described.status();
    }
    const ImageExtent& extent = model.request().input.descriptor.extent();
    return finalizeRows(execution, model, set, targets, valueDivisor,
                        stacking::StackAccumulatorWindow{.firstRow = 0u, .rows = extent.height()}, false);
}

namespace stacking
{

bool stackAccumulatesStoredSamples(const StackModel& model) noexcept
{
    const ImageDescriptor& input = model.request().input.descriptor;
    switch (input.sampleType())
    {
    case SampleType::UInt8:
    case SampleType::Int8:
    case SampleType::UInt16:
    case SampleType::Int16:
        break;
    default:
        return false;
    }
    return stackTileSampleTypeOf(input.sampleType()) == SampleType::Float32 &&
           conversionPolicyFor(input.numericDomain()).domain == DomainMapping::CodeValue;
}

Result<StackAccumulationOutcome> accumulateStackWindow(const ExecutionContext& execution, const StackModel& model,
                                                       const StackAccumulatorSet& set,
                                                       const StackAccumulatorWindow& window,
                                                       std::span<const StackFrameBand> bands,
                                                       std::span<const double> frameWeights,
                                                       std::span<std::uint64_t> admittedPerBand, bool storedSamples)
{
    const ImageExtent& extent = model.request().input.descriptor.extent();
    if (window.rows == 0u || window.firstRow >= extent.height() || window.rows > extent.height() - window.firstRow)
    {
        return refuse(ErrorCode::OutOfRange, kAccumulate, "window", "the window does not lie inside the output");
    }
    const std::uint64_t elements = static_cast<std::uint64_t>(window.rows) * extent.width() * extent.channelCount();
    return accumulateInto(execution, model, set, window, elements, bands, frameWeights, admittedPerBand, storedSamples);
}

Result<std::uint64_t> checkStackWindowTargets(const StackModel& model, const StackFinalizeTargets& targets,
                                              const StackAccumulatorWindow& window, const char* operation)
{
    const ImageExtent& extent = model.request().input.descriptor.extent();
    if (window.rows == 0u || window.firstRow >= extent.height() || window.rows > extent.height() - window.firstRow)
    {
        return refuse(ErrorCode::OutOfRange, operation, "window", "the window does not lie inside the output");
    }
    return checkTargetsAs(model, targets, operation, window.rows);
}

Result<StackFinalizeOutcome> finalizeStackWindow(const ExecutionContext& execution, const StackModel& model,
                                                 const StackAccumulatorSet& set, const StackAccumulatorWindow& window,
                                                 const StackFinalizeTargets& targets, bool targetsCoverWindow)
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
    const ImageExtent& extent = model.request().input.descriptor.extent();
    if (window.rows == 0u || window.firstRow >= extent.height() || window.rows > extent.height() - window.firstRow)
    {
        return refuse(ErrorCode::OutOfRange, kFinalize, "window", "the window does not lie inside the output");
    }
    const Result<std::uint64_t> checked = checkSetOver(
        model, set, kFinalize, static_cast<std::uint64_t>(window.rows) * extent.width() * extent.channelCount());
    if (!checked.hasValue())
    {
        return checked.status();
    }
    const Result<std::uint64_t> described =
        checkTargetsAs(model, targets, kFinalize, targetsCoverWindow ? window.rows : 0u);
    if (!described.hasValue())
    {
        return described.status();
    }
    return finalizeRows(execution, model, set, targets, 1.0, window, true, targetsCoverWindow);
}

} // namespace stacking

} // namespace an
