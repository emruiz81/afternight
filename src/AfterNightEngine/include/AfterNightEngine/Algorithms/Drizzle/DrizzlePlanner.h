// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The drizzle planner: what one sequence will cost in memory and reads, decided before a buffer is
/// allocated or a pixel evaluated.
///
/// `planDrizzle` turns a checked `DrizzleModel`, the execution's budget and a
/// `DrizzlePlanningRequest` into a `DrizzlePlan`: the source band height, the coordinate-map rows
/// and halo, the destination band height and how many bands the output is split into, every
/// persistent and transient buffer by name in `DrizzleMemoryPlan`, and the reads that split costs in
/// `DrizzleIoPlan`. The source band is chosen first, as tall as the request allows while one
/// destination row still fits, and then the tallest destination band that fits beside it. A
/// destination split into several bands repeats the whole source traversal, and the plan says so
/// before execution rather than after.
///
/// A plan is an inspectable value, not a reservation. `executeDrizzle` recomputes it from the
/// reader's actual access and its own limits, rechecks the live budget and allocates from the
/// context, so editing a plan cannot widen what an execution may take.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

namespace an
{

class ExecutionContext;

/// The source reader's smallest available unit; applies to samples, masks and auxiliary weights.
enum class DrizzleSourceAccess : std::uint8_t
{
    WholeFrame, ///< Decode once per destination band and retain the frame while source blocks are processed.
    Region ///< Read only a full-width source band; no halo is needed for sample values.
};

/// Coordinate storage, always for one source frame at a time.
enum class DrizzleMapStorage : std::uint8_t
{
    BoundedRows, ///< Two Float32 coordinate planes for a source band plus its interpolation halo.
    Dense ///< Two Float32 coordinate planes for the entire source; explicit, never a fallback.
};

/// Why the destination accumulator is this many rows high.
enum class DrizzleTileReason : std::uint8_t
{
    WholeOutput, ///< The complete output accumulator fits.
    CallerRowCap, ///< The caller bounded the accumulator height.
    BudgetBound ///< The largest full-width band fitting the available memory.
};

/// Controls the cost of processing one sequence; does not override its numeric contract.
struct DrizzlePlanningRequest
{
    DrizzleSourceAccess access = DrizzleSourceAccess::WholeFrame; ///< Reader access contract.
    DrizzleMapStorage mapStorage = DrizzleMapStorage::BoundedRows; ///< Coordinate storage choice.
    std::uint32_t maximumSourceRows = 32u; ///< Source checkpoint band height, in [1,256]; narrowed by memory.
    std::uint32_t maximumDestinationRows = 0u; ///< Accumulator row cap, or zero for no caller cap.
    std::uint32_t concurrentFrameReads = 1u; ///< Number of simultaneous reader buffers, in [1,frameCount].
    std::uint64_t preparedStateBytes = 0u; ///< Caller-owned resident inputs, coefficients and other retained state.
    std::uint64_t preparedContextBytes = 0u; ///< Portion of preparedStateBytes already counted by this context.
    std::uint64_t sourceWorkspaceBytes = 0u; ///< Additional peak workspace per concurrent reader, excluding payloads.
    std::uint64_t workingSetLimitBytes = 0u; ///< Optional total operation ceiling; zero adds no caller limit.
};

/// Checked byte accounting; all payloads are packed planar, with no hidden row padding.
///
/// Persistent buffers are outputImageBytes, outputMaskBytes, outputMapBytes, frameStateBytes,
/// kernelTableBytes, finalizationBytes and preparedStateBytes. Accumulators and the source/map/scratch workspace live
/// during accumulation. Outputs are zero-initialized; accumulators and coverage state are zeroed
/// per destination band. Each worker scratch slice is independently aligned to 64 bytes.
struct DrizzleMemoryPlan
{
    std::uint64_t outputImageBytes = 0u; ///< Complete result at the requested sample type.
    std::uint64_t outputMaskBytes = 0u; ///< Packed validity, one bit per destination pixel per plane.
    std::uint64_t outputMapBytes = 0u; ///< Optional Float32 weight and UInt32 coverage images.
    std::uint64_t frameStateBytes = 0u; ///< Owned frame descriptors/weights and one active geometry/map control state.
    std::uint64_t kernelTableBytes = 0u; ///< Complete DrizzleKernelTable, including order and padding; zero otherwise.
    std::uint64_t finalizationBytes = 0u; ///< Float64 numerator image for a Float32 peak sum; Float64 reuses output.
    std::uint64_t preparedStateBytes = 0u; ///< Retained caller state, including already context-counted bytes.
    std::uint64_t persistentBytes = 0u; ///< Sum of the preceding persistent terms.
    std::uint64_t accumulatorBytes = 0u; ///< Float64 weighted sum and signed weight sum per destination-band element.
    std::uint64_t coverageStateBytes = 0u; ///< Optional UInt32 count and last contributing frame per band element.
    std::uint64_t sourceBufferBytes = 0u; ///< Stored samples, explicit mask and auxiliary weights for concurrent reads.
    std::uint64_t sourceWorkspaceBytes = 0u; ///< Declared extra reader workspace times concurrent reads.
    std::uint64_t coordinateMapBytes = 0u; ///< One map's two Float32 planes and per-row bounds, halo included.
    std::uint64_t scratchBytesPerWorker = 0u; ///< Aligned row values, weights, corner geometry and counters per worker.
    std::uint64_t scratchBytes = 0u; ///< Worker scratch at the admitted width, never the context maximum.
    std::uint64_t peakBytes = 0u; ///< Total simultaneous resident bytes, including prepared state.
    std::uint64_t availableBytes = 0u; ///< Available total for this plan after unrelated context residency.
    std::uint64_t budgetBytes = 0u; ///< Smaller of the context ceiling and any caller ceiling.
    bool budgetBounded = false; ///< False only when both ceilings are unbounded.
};

/// The complete read cost, including repeated passes forced by destination tiling.
struct DrizzleIoPlan
{
    std::uint64_t frameBytes = 0u; ///< One source's samples, packed mask and optional weight/flat payloads.
    std::uint64_t distinctInputBytes =
        0u; ///< Every source read once; a shared flat is conservatively counted per frame.
    std::uint64_t logicalInputBytes = 0u; ///< Distinct bytes times the destination tile count.
    std::uint64_t readRequests = 0u; ///< Frame reads, or source-band reads for a region reader, over every pass.
    std::uint32_t inputPasses = 0u; ///< One ordered traversal of all frames per destination band.
};

/// A reviewable execution plan, valid for the checked model and the operation that produced it.
///
/// Destination pixels have a single owner. Tasks partition disjoint destination rows, while every
/// pixel accumulates frames, source rows and columns in that fixed order. There are no floating
/// atomics, lock-order reductions or per-thread partial sums, so changing worker count cannot change
/// the sum. Source blocks and maps are shared read-only between those tasks. A capped destination
/// band repeats the source pass, and that I/O multiplication is reported before execution.
/// A runner must validate/replan edited values and recheck memory before allocation: this is a
/// snapshot, not a reservation. No decoder, kernel or artifact writer is executed by the planner.
struct DrizzlePlan
{
    DrizzlePlanningRequest request{}; ///< Exact planning inputs, retained for executor validation.
    DrizzleMemoryPlan memory{}; ///< Named payload and peak costs.
    DrizzleIoPlan io{}; ///< Source read costs.
    std::uint32_t sourceRows = 0u; ///< Source band height and cancellation checkpoint interval in rows.
    std::uint32_t sourceBlockCount = 0u; ///< Ceil(source height/sourceRows).
    std::uint32_t mapRows = 0u; ///< Maximum rows held by the coordinate map, halo included.
    std::uint32_t mapHaloRows = 0u; ///< One for square interpolation, zero for other kernels.
    std::uint32_t destinationRows = 0u; ///< Full-width accumulator height.
    std::uint32_t destinationTileCount = 0u; ///< Ceil(output height/destinationRows).
    DrizzleTileReason tileReason = DrizzleTileReason::WholeOutput; ///< Why the accumulator height was chosen.
    std::uint32_t workers = 0u; ///< Admitted scheduler concurrency; never creates a parallelism limit.
    BackendSelection backend{}; ///< Context dispatch eligibility; no claim that a drizzle kernel ran.
    ComputePrecision accumulation = ComputePrecision::Float64; ///< Signed weighted sums use double precision.
    Float64Justification accumulationReason = Float64Justification::StackingAccumulation; ///< Numeric justification.
    bool collectMaximumWeight = false; ///< One optional completed-output reduction, not one per frame.
};

/// Plans geometry-dependent storage, deterministic parallel work and reads without allocating buffers.
///
/// Chooses the largest source band up to the request that leaves room for one destination row,
/// then the largest destination band that fits. Persistent output storage is never silently spilled;
/// less than one row returns a typed budget refusal. All count arithmetic is checked before use.
/// Checks cancellation at entry and before and after delivering its one diagnostic, which is a
/// warning when the destination is split and the source is therefore read more than once, and
/// informational otherwise; callbacks use the execution's serialized sink. A model built against
/// looser geometry limits is checked again against this execution's.
/// @param execution the initialized operation supplying budgets, backend, concurrency and cancellation.
/// @param model the checked drizzle description.
/// @param request source access, coordinate storage and optional narrower limits.
/// @return the plan. Otherwise `ErrorCode::NotInitialized` for an uninitialized execution;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`; `ErrorCode::InvalidArgument`
///         for an unknown access or map storage, a source band cap outside [1,256], or concurrent
///         reads outside [1, frameCount]; any refusal `DrizzleModel::create` reports when the model
///         is rechecked against this execution's limits; `ErrorCode::UnsupportedBackend` when the
///         execution is constrained to a backend it cannot select; `ErrorCode::IncompatibleArguments`
///         when `preparedContextBytes` exceeds the prepared state or the context's live bytes;
///         `ErrorCode::LimitExceeded` when the byte accounting overflows; or
///         `ErrorCode::MemoryBudgetExceeded`, with requested, available and limit bytes, when not
///         even one destination row fits. Success changes no memory-resource counter.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzlePlan>
planDrizzle(const ExecutionContext& execution, const DrizzleModel& model, const DrizzlePlanningRequest& request);

} // namespace an
