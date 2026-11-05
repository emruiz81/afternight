// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The memory a stack holds while it runs, taken against its plan.
///
/// A stack plan says what a stack will cost: every buffer by name, what each pass holds, and the
/// peak. This header is where those numbers become memory, so the plan is a promise the run keeps
/// rather than a prediction, under three rules.
///
/// **Every buffer is taken from the engine context, by name, at exactly the size the plan states.**
/// The output image, its mask and its maps are engine storages; the accumulators, the per-frame
/// tables, the local-weight grids, the per-worker scratch, the tile, the read buffer and the
/// measurement workspace are one allocation each from the context's memory resource. The account
/// lists all of them beside their planned figure, so "held equals planned" is a test rather than a
/// hope, and a buffer the plan does not name cannot be taken through this type at all.
///
/// **A refusal comes before anything is taken, and never leaves a partial set behind.** The plan's
/// peak is checked against what the engine context has left at the moment the set is created - not
/// at the moment it was planned, because what is outstanding moves - and a pass's buffers are
/// checked the same way when the pass begins, after the cancellation checkpoint. A refusal at that
/// check has allocated nothing. A refusal from the resource itself, which can still happen when
/// another operation takes memory between the check and the allocation, releases what this call
/// took and reports which buffer was refused with the resource's own numbers.
///
/// **Pass memory lives with its pass.** The plan's peak is the largest working set of any pass,
/// which is only true if a pass's buffers go back before the next pass takes its own. `beginPass`
/// takes them, `endPass` returns them, and the passes are followed in the plan's order because the
/// plan already decided it. The persistent buffers live with the set, and the output image, its
/// mask and each map can be handed over to the caller with no copy, which is what lets the result
/// leave the set without an allocation the plan never counted.
///
/// What the set does not do is decide anything: the plan decided, and the executor follows it.
/// What is in a buffer is the executor's, and every pass buffer is uninitialised on purpose - a
/// pass writes what it reads - while every persistent buffer starts zeroed, because an
/// accumulator, a count and a mask are added to and must start from nothing.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace an
{

class StackModel;

/// The engine context whose memory budget owns a working set. Defined in `Runtime/EngineContext.h`.
///
/// Named here rather than included: the set is built from one and keeps a lease on it, and a
/// caller reaching this header holds an engine context and has therefore already included that
/// header.
class EngineContext;

/// One of the buffers a stack holds, by name.
///
/// The persistent six live from the set's creation to its destruction or their handover; the
/// four pass buffers live from `StackWorkingSet::beginPass` to `StackWorkingSet::endPass`. The
/// order is the order the set takes them in.
enum class StackBuffer : std::uint8_t
{
    /// The combined image, at the output representation, in a packed planar layout, over the whole
    /// output or one tile's rows as the plan's `StackMemoryPlan::outputCoversOneTile` says.
    OutputImage,

    /// The output's explicit validity mask, packed one bit per pixel per channel, over the image's
    /// rows.
    OutputMask,

    /// Every requested map, one packed plane per channel each, over the image's rows.
    OutputMaps,

    /// The accumulators of a streaming strategy, one allocation per kept term, over the whole
    /// output or one tile's rows as `stackAccumulatorsCoverOneTile` decides.
    Accumulators,

    /// The per-frame tables - one weight per frame and channel, and one offset and one scale per
    /// frame and channel under a global normalization - followed by the executor's per-frame
    /// bookkeeping.
    FrameState,

    /// The local-weight grids of every frame.
    LocalWeightGrids,

    /// What every worker holds while combining one pixel's column of samples and while reading a
    /// band through the local surface, one slice per worker in one allocation. Taken by the combine
    /// pass.
    WorkerScratch,

    /// The frames' samples for one band of rows at the tile width - every frame's, or one frame's
    /// under whole-frame streaming - and the local weight surface's bands beside them when the
    /// stack applies one. Taken by the combine pass.
    Tile,

    /// The decoded input the pass holds at once: whole frames or bands, times the concurrent
    /// reads. Taken by every pass that reads.
    ReadBuffer,

    /// The estimator's workspace beside one whole frame. Taken by a measurement pass.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    MeasurementWorkspace
};

/// How many buffers `StackBuffer` names.
inline constexpr std::size_t kStackBufferCount = 10u;

/// Whether a value names a buffer this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param buffer the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackBuffer(StackBuffer buffer) noexcept
{
    return static_cast<std::uint8_t>(buffer) <= static_cast<std::uint8_t>(StackBuffer::MeasurementWorkspace);
}

/// The buffer's own name.
///
/// Thread-safe and allocation-free.
///
/// @param buffer the buffer to name. A value outside the enumeration is named
///        `"UnknownStackBuffer"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackBufferName(StackBuffer buffer) noexcept;

/// When a buffer lives.
enum class StackBufferLifetime : std::uint8_t
{
    /// From the set's creation until the set is destroyed or the buffer is handed over.
    Persistent,

    /// From `StackWorkingSet::beginPass` until `StackWorkingSet::endPass`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Pass
};

/// Whether a value names a lifetime this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param lifetime the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackBufferLifetime(StackBufferLifetime lifetime) noexcept
{
    return static_cast<std::uint8_t>(lifetime) <= static_cast<std::uint8_t>(StackBufferLifetime::Pass);
}

/// The lifetime's own name.
///
/// Thread-safe and allocation-free.
///
/// @param lifetime the lifetime to name. A value outside the enumeration is named
///        `"UnknownStackBufferLifetime"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackBufferLifetimeName(StackBufferLifetime lifetime) noexcept;

/// When a buffer lives.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param buffer the buffer. A value outside the enumeration is read as a pass buffer, the
///        shorter of the two lives.
/// @return `StackBufferLifetime::Persistent` for the first six buffers and
///         `StackBufferLifetime::Pass` for the rest.
[[nodiscard]] constexpr StackBufferLifetime stackBufferLifetimeOf(StackBuffer buffer) noexcept
{
    return static_cast<std::uint8_t>(buffer) <= static_cast<std::uint8_t>(StackBuffer::LocalWeightGrids)
               ? StackBufferLifetime::Persistent
               : StackBufferLifetime::Pass;
}

/// One of the maps a stack can write beside its image.
///
/// The three rejection maps and the coverage map count samples, so they are thirty-two-bit
/// integers; the variance, gain and weight-sum maps carry values, so they are Float32. Which of
/// them a stack writes is what `StackOutputRequest` says, and `stackOutputMapRequested` reads it.
enum class StackOutputMap : std::uint8_t
{
    /// How many samples were rejected below the kept range, under
    /// `RejectionMapForm::LowAndHigh`.
    RejectionLow,

    /// How many samples were rejected above the kept range, under
    /// `RejectionMapForm::LowAndHigh`.
    RejectionHigh,

    /// How many samples were rejected on either side, under `RejectionMapForm::Merged`.
    RejectionMerged,

    /// How many samples contributed to each output sample.
    Coverage,

    /// The variance of the contributing samples.
    Variance,

    /// The signal-to-noise gain of the weighted combination over one frame.
    SignalToNoiseGain,

    /// The sum of the weights that contributed.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    WeightSum
};

/// How many maps `StackOutputMap` names.
inline constexpr std::size_t kStackOutputMapCount = 7u;

/// Whether a value names a map this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param map the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackOutputMap(StackOutputMap map) noexcept
{
    return static_cast<std::uint8_t>(map) <= static_cast<std::uint8_t>(StackOutputMap::WeightSum);
}

/// The map's own name.
///
/// Thread-safe and allocation-free.
///
/// @param map the map to name. A value outside the enumeration is named `"UnknownStackOutputMap"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackOutputMapName(StackOutputMap map) noexcept;

/// What one sample of a map is.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param map the map. A value outside the enumeration is read as a value map.
/// @return `SampleType::UInt32` for the three rejection maps and the coverage map,
///         `SampleType::Float32` for the variance, gain and weight-sum maps. Both are
///         `kStackMapSampleBytes` wide, which is what the plan counts.
[[nodiscard]] constexpr SampleType stackOutputMapSampleType(StackOutputMap map) noexcept
{
    return static_cast<std::uint8_t>(map) <= static_cast<std::uint8_t>(StackOutputMap::Coverage) ? SampleType::UInt32
                                                                                                 : SampleType::Float32;
}

/// Whether a stack writes a map.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param output what the stack was asked to write.
/// @param map the map.
/// @return true when the request names it: the low and high rejection maps under
///         `RejectionMapForm::LowAndHigh`, the merged one under `RejectionMapForm::Merged`, and
///         each of the other four under its own flag. A value outside the enumeration is never
///         requested.
[[nodiscard]] constexpr bool stackOutputMapRequested(const StackOutputRequest& output, StackOutputMap map) noexcept
{
    switch (map)
    {
    case StackOutputMap::RejectionLow:
    case StackOutputMap::RejectionHigh:
        return output.rejectionMaps == RejectionMapForm::LowAndHigh;
    case StackOutputMap::RejectionMerged:
        return output.rejectionMaps == RejectionMapForm::Merged;
    case StackOutputMap::Coverage:
        return output.coverageMap;
    case StackOutputMap::Variance:
        return output.varianceMap;
    case StackOutputMap::SignalToNoiseGain:
        return output.signalToNoiseGainMap;
    case StackOutputMap::WeightSum:
        return output.weightSumMap;
    }
    return false;
}

/// How many maps a stack writes.
///
/// The figure the plan's `StackMemoryPlan::outputMapBytes` multiplies the output elements by.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param output what the stack was asked to write.
/// @return the count, between zero and `kStackOutputMapCount`.
[[nodiscard]] constexpr std::uint32_t stackOutputMapCount(const StackOutputRequest& output) noexcept
{
    std::uint32_t count = 0u;
    for (std::uint8_t index = 0u; index < kStackOutputMapCount; ++index)
    {
        count += stackOutputMapRequested(output, static_cast<StackOutputMap>(index)) ? 1u : 0u;
    }
    return count;
}

/// One of the accumulators a streaming strategy keeps.
///
/// Each is one allocation of its own, so a term of doubles never begins at an address a term of
/// counts left unaligned. Which terms a stack keeps is what `stackAccumulatorKept` says, and it is
/// the same rule the plan's `StackMemoryPlan::accumulatorBytes` was summed under. A term covers
/// every output element, or under `stackAccumulatorsCoverOneTile` every element of one tile's rows;
/// "per output element" below reads as per element of what the term covers.
enum class StackAccumulator : std::uint8_t
{
    /// The running sum, or the running extremum, one double per output element.
    Sums,

    /// How many samples have contributed, one thirty-two-bit count per output element.
    Counts,

    /// The running sum of the weights, one double per output element, kept when the stack
    /// weighs its frames or applies a local surface.
    WeightSums,

    /// The running sum of the squared weights, one double per output element, kept for the
    /// signal-to-noise gain map.
    WeightSquareSums,

    /// The running mean and second moment, two doubles per output element, kept for the
    /// variance map.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    VarianceMoments
};

/// How many accumulators `StackAccumulator` names.
inline constexpr std::size_t kStackAccumulatorCount = 5u;

/// Whether a value names an accumulator this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param accumulator the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackAccumulator(StackAccumulator accumulator) noexcept
{
    return static_cast<std::uint8_t>(accumulator) <= static_cast<std::uint8_t>(StackAccumulator::VarianceMoments);
}

/// The accumulator's own name.
///
/// Thread-safe and allocation-free.
///
/// @param accumulator the accumulator to name. A value outside the enumeration is named
///        `"UnknownStackAccumulator"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackAccumulatorName(StackAccumulator accumulator) noexcept;

/// What one output element of an accumulator costs.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param accumulator the accumulator. A value outside the enumeration costs nothing.
/// @return `kStackAccumulatorBytes` for the sums and the two weight terms, `kStackCountBytes` for
///         the counts, and twice `kStackAccumulatorBytes` for the variance moments.
[[nodiscard]] constexpr std::uint64_t stackAccumulatorBytesPerElement(StackAccumulator accumulator) noexcept
{
    switch (accumulator)
    {
    case StackAccumulator::Sums:
    case StackAccumulator::WeightSums:
    case StackAccumulator::WeightSquareSums:
        return kStackAccumulatorBytes;
    case StackAccumulator::Counts:
        return kStackCountBytes;
    case StackAccumulator::VarianceMoments:
        return 2u * kStackAccumulatorBytes;
    }
    return 0u;
}

/// One buffer of a working set, as planned and as held.
struct StackBufferAccount
{
    /// Which buffer.
    StackBuffer buffer = StackBuffer::OutputImage;

    /// When it lives.
    StackBufferLifetime lifetime = StackBufferLifetime::Persistent;

    /// What the plan states for it: the persistent figure for a persistent buffer, and for a pass
    /// buffer the figure of the pass that is open, or of the combine pass when none is.
    std::uint64_t plannedBytes = 0u;

    /// What is taken from the engine context for it right now. Zero before it is taken, after it
    /// is returned, and after it is handed over.
    std::uint64_t heldBytes = 0u;

    /// How many allocations it is made of right now: one for most buffers, one per map and one
    /// per kept term for the maps and the accumulators.
    std::uint32_t allocations = 0u;

    /// Whether two accounts say the same thing.
    ///
    /// @param left one account.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackBufferAccount& left,
                                                   const StackBufferAccount& right) noexcept = default;
};

/// What a working set has taken, against what its plan said.
///
/// A plain value read out under the set's own bookkeeping, so the figures are consistent with each
/// other. The bytes the caller stated it holds elsewhere are reported and never taken here, which
/// is why `heldBytes` is compared with the plan's figures less `preparedStateBytes`.
struct StackMemoryAccount
{
    /// Every buffer, in `StackBuffer` order.
    std::array<StackBufferAccount, kStackBufferCount> buffers{};

    /// The plan's persistent figure, the prepared bytes included.
    std::uint64_t plannedPersistentBytes = 0u;

    /// The plan's peak, the prepared bytes included.
    std::uint64_t plannedPeakBytes = 0u;

    /// What the caller stated it already holds, counted by the plan and never taken by the set.
    std::uint64_t preparedStateBytes = 0u;

    /// What the engine context had outstanding when the set was created, before it took anything.
    std::uint64_t baselineLiveBytes = 0u;

    /// What the engine context had left when the set was created: its budget less the baseline,
    /// or the unbounded default when nothing bounds it.
    std::uint64_t availableBytes = 0u;

    /// What the set holds from the engine context right now, over every buffer.
    std::uint64_t heldBytes = 0u;

    /// The most the set ever held at once. Never falls. When returned in StackExecutionReport,
    /// also includes concurrently held scratch and fitted grids from the composed operations;
    /// the executor combines their reported peaks with the set's current account.
    std::uint64_t peakHeldBytes = 0u;

    /// What the set took and then handed to the caller, which is still outstanding in the engine
    /// context and no longer counted in `heldBytes`.
    std::uint64_t handedOverBytes = 0u;

    /// How many requests this set refused or had refused, at its own budget check or by the
    /// resource. Every one of them left the set as it was.
    std::uint64_t refusals = 0u;

    /// Whether a pass is open.
    bool passOpen = false;

    /// Which pass is open, or `StackPassKind::Combine` when none is, so that two accounts of a
    /// set between passes compare equal.
    StackPassKind currentPass = StackPassKind::Combine;

    /// How many passes have begun and ended.
    std::uint32_t passesCompleted = 0u;

    /// Whether two accounts say the same thing.
    ///
    /// Not `noexcept`, because an array's comparison is not.
    ///
    /// @param left one account.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackMemoryAccount& left,
                                                   const StackMemoryAccount& right) = default;
};

/// The memory a stack holds while it runs.
///
/// Built from an engine context and a plan, on which it keeps a lease until destruction, so the
/// context refuses shutdown while any of its buffers are outstanding. Move-only, because two
/// owners of one set of buffers would return them twice. A default-constructed one holds nothing,
/// which is also what a move and `release` leave behind; `valid` is how to ask, and every accessor
/// answers for that state with an empty span rather than requiring the question to have been
/// asked.
///
/// Not thread-safe as an object: one set belongs to one execution, and the threads of that
/// execution write through the spans it handed out before the parallel region rather than
/// asking it for them inside one. The buffers themselves are the executor's to share.
class StackWorkingSet
{
public:
    /// A set that holds nothing.
    AFTERNIGHTENGINE_API StackWorkingSet() noexcept;

    /// Returns every buffer still held to the engine context, and the lease with them.
    ///
    /// However the execution ended - returned, failed, cancelled or unwound - this runs, because
    /// it is a destructor. What was handed over is the caller's and is not touched.
    AFTERNIGHTENGINE_API ~StackWorkingSet();

    /// Takes over another set's buffers, leaving it holding nothing.
    ///
    /// Allocation-free. Spans already handed out stay valid: the ownership moves and the bytes
    /// do not.
    ///
    /// @param other the set to take over. Left exactly as a default-constructed one.
    AFTERNIGHTENGINE_API StackWorkingSet(StackWorkingSet&& other) noexcept;

    /// Returns this set's buffers and takes over another's.
    ///
    /// @param other the set to take over. Left exactly as a default-constructed one.
    /// @return this set.
    AFTERNIGHTENGINE_API StackWorkingSet& operator=(StackWorkingSet&& other) noexcept;

    /// Copying is deleted: two owners of one set of buffers would return them twice.
    StackWorkingSet(const StackWorkingSet&) = delete;

    /// Copying is deleted. See the copy constructor.
    StackWorkingSet& operator=(const StackWorkingSet&) = delete;

    /// Takes the persistent buffers of a plan, or reports why it cannot before taking any.
    ///
    /// The plan is first checked against the model and the execution: every figure it states is
    /// recomputed and a plan that does not describe this stack on this execution is refused by
    /// name. Then its peak, less what the caller stated it already holds, is checked against what
    /// the engine context has left now, and a peak that does not fit is refused with the numbers
    /// before anything is taken. Only then are the persistent buffers taken, in `StackBuffer`
    /// order, and zeroed on the execution's workers; a refusal from the resource on any of them
    /// returns what was taken and reports the buffer that was refused, with the resource's refusal
    /// as the cause.
    ///
    /// What was taken and what was planned are reported to the execution's diagnostic sink at
    /// `DiagnosticSeverity::Info` in one sentence; an execution given no sink is served in
    /// silence.
    ///
    /// Thread-safe with respect to `engine` and `execution`, which are only read. Allocates the
    /// buffers, one bookkeeping block, and only a failure it has to describe.
    ///
    /// @param engine the context whose memory resource and budget own every buffer. It cannot
    ///        shut down while the set lives. A context that holds nothing is refused with
    ///        `ErrorCode::NotInitialized`.
    /// @param execution the operation the stack runs as. Must hold something, must run in
    ///        `engine`, and must have been admitted the workers the plan was made for.
    /// @param model the checked stack the plan was made from.
    /// @param plan the plan, as `planStack` returned it.
    /// @return the set; `ErrorCode::NotInitialized` when `engine` or `execution` holds nothing;
    ///         `ErrorCode::InvalidArgument` when `execution` runs in another engine context;
    ///         `ErrorCode::IncompatibleArguments` when a figure or a pass of the plan does not
    ///         describe this model's stack on this execution, naming it in the message;
    ///         `ErrorCode::LimitExceeded` when a figure is not representable;
    ///         `ErrorCode::MemoryBudgetExceeded` when the plan's peak does not fit what the engine
    ///         context has left, carrying the requested, available and limit bytes, or when the
    ///         resource refuses a buffer; `ErrorCode::AllocationFailed` when the machine does; and
    ///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the execution is stopped
    ///         before the buffers are cleared - refused before anything is taken when it is already
    ///         stopped, and with everything taken returned when it stops while they are cleared.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<StackWorkingSet> create(const EngineContext& engine,
                                                                             const ExecutionContext& execution,
                                                                             const StackModel& model,
                                                                             const StackPlan& plan);

    /// Whether this set holds anything.
    ///
    /// Allocation-free.
    ///
    /// @return false for a default-constructed set, one that has been moved from, and one that has
    ///         been released.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// Which engine context's budget the buffers belong to.
    ///
    /// @return the owning context's identity, or `0` when this set holds nothing. No live context
    ///         has an identity of zero.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t contextId() const noexcept;

    /// The plan the set was built against.
    ///
    /// @return a copy of the plan, or an empty optional when this set holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<StackPlan> plan() const noexcept;

    /// Takes the buffers of the next planned pass, or reports why it cannot before taking any.
    ///
    /// The pass must be the next one in the plan's order, and no pass may be open. The execution's
    /// cancellation checkpoint runs before any memory is asked for, so a cancelled operation takes
    /// nothing. The pass's buffers are then checked as a whole against what the engine context has
    /// left, refused with the numbers when they do not fit, and taken one by one otherwise; a
    /// refusal from the resource returns what this call took and reports the buffer. The buffers
    /// are uninitialised: a pass writes what it reads. A finalization takes nothing and still has
    /// to be begun and ended, so the account says where the run is.
    ///
    /// What was taken is reported to the execution's diagnostic sink in one sentence at
    /// `DiagnosticSeverity::Info`.
    ///
    /// Not thread-safe. Allocates the buffers and only a failure it has to describe.
    ///
    /// @param execution the operation the stack runs as. Must run in the set's engine context.
    /// @param kind the pass to begin.
    /// @return success; `ErrorCode::NotInitialized` when this set holds nothing;
    ///         `ErrorCode::InvalidArgument` when `execution` runs in another engine context;
    ///         `ErrorCode::InvalidState` when a pass is open, when every planned pass has run, or
    ///         when `kind` is not the plan's next pass; `ErrorCode::Cancelled` or
    ///         `ErrorCode::DeadlineExceeded` from the checkpoint; `ErrorCode::LimitExceeded` when
    ///         the pass's figures do not add up; `ErrorCode::MemoryBudgetExceeded` when they do
    ///         not fit or the resource refuses one; and `ErrorCode::AllocationFailed` when the
    ///         machine does.
    [[nodiscard]] AFTERNIGHTENGINE_API Status beginPass(const ExecutionContext& execution, StackPassKind kind);

    /// Returns the open pass's buffers to the engine context.
    ///
    /// Every span handed out for a pass buffer is dead afterwards. A set with no pass open is left
    /// as it was, so a cleanup path that runs after a refused `beginPass` needs no branch of its
    /// own.
    ///
    /// Not thread-safe. Allocation-free apart from the release itself.
    AFTERNIGHTENGINE_API void endPass() noexcept;

    /// Which pass is open.
    ///
    /// @return the pass, or an empty optional when none is open or this set holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<StackPassKind> currentPass() const noexcept;

    /// How many passes have begun and ended.
    ///
    /// @return the count, or `0` when this set holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t passesCompleted() const noexcept;

    /// What this set has taken, against what its plan said.
    ///
    /// Allocation-free.
    ///
    /// @return the account, all zero for a set that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API StackMemoryAccount account() const noexcept;

    /// The combined image's samples, at the output representation.
    ///
    /// Packed planar: one plane per channel, rows of exactly the width, no padding, which is the
    /// layout the plan counted and a codec writes. `outputImageLayout` says so exactly. When the plan's
    /// `StackMemoryPlan::outputCoversOneTile` holds, they are the rows of one tile, which the caller
    /// takes as each tile is finished, and so are the mask and the maps below.
    ///
    /// @return the bytes, or an empty span when the image is not held.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<std::byte> outputImage() noexcept;

    /// Where the combined image's samples sit.
    ///
    /// @return the layout, or an empty optional when the image is not held.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageLayout> outputImageLayout() const noexcept;

    /// The output's validity mask, one bit-plane per channel, selecting nothing until something
    /// fills it in.
    ///
    /// @return a view of the bits, or an empty optional when the mask is not held.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ValidityMaskView<std::uint8_t>> outputMask() noexcept;

    /// One of the maps that count samples, one entry per output element in planar order.
    ///
    /// @param map the map. Must be one whose `stackOutputMapSampleType` is `SampleType::UInt32`.
    /// @return the counts, or an empty span when the map is a value map, was not requested, or is
    ///         not held.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<std::uint32_t> outputCountMap(StackOutputMap map) noexcept;

    /// One of the maps that carry values, one entry per output element in planar order.
    ///
    /// @param map the map. Must be one whose `stackOutputMapSampleType` is `SampleType::Float32`.
    /// @return the values, or an empty span when the map is a count map, was not requested, or is
    ///         not held.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<float> outputValueMap(StackOutputMap map) noexcept;

    /// The running sums or extrema, one per output element - or per element of one tile's rows
    /// when `stackAccumulatorsCoverOneTile` holds for the plan's model and access.
    ///
    /// @return the accumulator, or an empty span when the stack does not keep it or this set holds
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<double> accumulatorSums() noexcept;

    /// The sample counts, one per element the terms cover.
    ///
    /// @return the accumulator, or an empty span when the stack does not keep it or this set holds
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<std::uint32_t> accumulatorCounts() noexcept;

    /// The running weight sums, one per element the terms cover.
    ///
    /// @return the accumulator, or an empty span when the stack does not keep it or this set holds
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<double> accumulatorWeightSums() noexcept;

    /// The running squared weight sums, one per element the terms cover.
    ///
    /// @return the accumulator, or an empty span when the stack does not keep it or this set holds
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<double> accumulatorWeightSquareSums() noexcept;

    /// The running mean and second moment, two per element the terms cover, the mean first.
    ///
    /// @return the accumulator, or an empty span when the stack does not keep it or this set holds
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<double> accumulatorVarianceMoments() noexcept;

    /// The per-frame weights, one per frame and channel, frame-major.
    ///
    /// @return the table, or an empty span when this set holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<double> frameWeights() noexcept;

    /// The per-frame normalization coefficients, an offset and a scale per frame and channel,
    /// frame-major with the offset first.
    ///
    /// @return the table, or an empty span when the stack's normalization scope uses no global
    ///         method or this set holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<double> frameNormalization() noexcept;

    /// The executor's per-frame bookkeeping, laid out as `stackFrameBookkeepingLayoutOf` states
    /// for this stack on the plan's workers, after the two tables in the same block.
    ///
    /// Zeroed with the rest of the persistent state, and beginning on a
    /// `kStackFrameCounterBytes` boundary.
    ///
    /// @return the bytes, or an empty span when this set holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<std::byte> frameBookkeeping() noexcept;

    /// The local-weight grids, `kLocalWeightNodeBytes` per node and frame, frame-major and then
    /// row-major over the nodes: the level before the quality while the grids are measured, and the
    /// finished weight before the quality once they are finalized.
    ///
    /// @return the grids, or an empty span when the stack applies no local weighting or this set
    ///         holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<float> localWeightGrids() noexcept;

    /// One worker's scratch, `StackMemoryPlan::scratchBytesPerWorker` bytes beginning on a
    /// `kStackScratchAlignmentBytes` boundary.
    ///
    /// @param worker which worker, below `StackMemoryPlan::workers`.
    /// @return the bytes, or an empty span outside the combine pass, for a worker the plan did not
    ///         size, or when the plan sizes no scratch - a streaming strategy with no local
    ///         normalization.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<std::byte> workerScratch(std::uint32_t worker) noexcept;

    /// The tile: the frames' samples for one band of rows, at the tile width, arranged as the
    /// executor decides.
    ///
    /// @return the bytes, or an empty span outside the combine pass or when the plan's tile is
    ///         empty - a streaming stack under whole-frame access over frames stored at the tile
    ///         width, with no local weighting.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<std::byte> tile() noexcept;

    /// The read buffer of the open pass: whole frames or bands, times the concurrent reads.
    ///
    /// @return the bytes, or an empty span outside a pass that reads.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<std::byte> readBuffer() noexcept;

    /// The workspace of the open measurement pass.
    ///
    /// @return the bytes, or an empty span outside a measurement pass.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<std::byte> measurementWorkspace() noexcept;

    /// Hands the combined image's storage to the caller, with no copy.
    ///
    /// The storage keeps its own lease on the engine context and stays outstanding there; the set
    /// stops counting it as held and counts it as handed over. `outputImage` is empty afterwards.
    ///
    /// @return the storage; `ErrorCode::NotInitialized` when this set holds nothing;
    ///         `ErrorCode::InvalidState` when the image was already handed over.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<ImageStorageBytes> releaseOutputImage();

    /// Hands the output's validity mask to the caller, with no copy.
    ///
    /// @return the mask; `ErrorCode::NotInitialized` when this set holds nothing;
    ///         `ErrorCode::InvalidState` when the mask was already handed over.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<ValidityMask> releaseOutputMask();

    /// Hands one map's storage to the caller, with no copy.
    ///
    /// The storage is one packed plane per channel of `kStackMapSampleBytes` elements, which
    /// `ImageStorage::fromBytes` adopts as `stackOutputMapSampleType(map)`.
    ///
    /// @param map the map.
    /// @return the storage; `ErrorCode::NotInitialized` when this set holds nothing;
    ///         `ErrorCode::InvalidArgument` when `map` was not requested;
    ///         `ErrorCode::InvalidState` when it was already handed over.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<ImageStorageBytes> releaseOutputMap(StackOutputMap map);

    /// Returns every buffer still held now rather than at the end of the scope.
    ///
    /// The explicit half of what the destructor does. Idempotent, and leaves the set exactly as a
    /// default-constructed one; what was handed over is untouched.
    AFTERNIGHTENGINE_API void release() noexcept;

private:
    /// The buffers, the accounting and the hold on the engine context, defined only inside the
    /// engine so that the published surface says nothing about how a set is laid out.
    class State;

    /// Takes ownership of a state the factory has already built.
    ///
    /// @param state the set's buffers and accounting. Never null on this path.
    explicit StackWorkingSet(std::unique_ptr<State> state) noexcept;

    /// What this set holds, or null when it holds nothing.
    std::unique_ptr<State> m_state;
};

} // namespace an
