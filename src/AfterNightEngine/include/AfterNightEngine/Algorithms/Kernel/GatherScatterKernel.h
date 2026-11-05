// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Indexed reads and writes of one Float32 plane: gathers and scatters.
///
/// A gather reads the source at one coordinate per output and writes a linear run in coordinate
/// order; a scatter writes each selected source sample to the destination pixel its coordinate
/// rounds to. Coordinates follow the pixel-centre convention of `KernelCoordinate` and must be finite
/// and no further than 2^53 - 2 from zero, so every tap index and border transform is exact in signed
/// 64-bit arithmetic. Every coordinate is checked before the first write.
///
/// Each family has a scalar oracle that walks in one fixed order on the calling thread -
/// `referenceGatherKernel`, `referenceGatherInterpolatedKernel` and `referenceScatterKernel` - and a
/// parallel entry point that writes the oracle's bits at every thread count: `gatherSamples` and
/// `scatterPlane`. Where two selected inputs of a scatter name one destination pixel, a stated
/// `ScatterConflictPolicy` decides the answer rather than which thread wrote last.
///
/// Every entry point reads one-sample-per-pixel planes under a `KernelValidityView`, and shares these
/// refusals: `ErrorCode::NotInitialized` for an execution that holds nothing;
/// `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one;
/// `ErrorCode::IncompatibleArguments` for a plane with more than one sample per pixel, or a packed
/// mask that does not cover the source and its selected bit-plane; and `ErrorCode::InvalidArgument`
/// for a packed mask supplied under a rule that reads none, or missing under
/// `ValidityRule::ExplicitMask`.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <cstdint>
#include <span>

namespace an
{

/// What a scatter does when two selected inputs name the same destination pixel.
enum class ScatterConflictPolicy : std::uint8_t
{
    /// Refuse before writing any destination.
    Reject,

    /// Write only the first selected input in row-major source order.
    First,

    /// Write every selected input, so the last in row-major source order remains.
    Last,

    /// Add every selected input in row-major source order to the destination's existing value, the
    /// first one included.
    Add
};

/// Whether a value names a scatter conflict rule.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to test.
/// @return true when it is one of the four enumerators.
[[nodiscard]] constexpr bool isScatterConflictPolicy(ScatterConflictPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(ScatterConflictPolicy::Add);
}

/// What a scatter does with a selected coordinate outside the destination.
enum class ScatterBoundsPolicy : std::uint8_t
{
    /// Refuse before writing any destination.
    Reject,

    /// Discard the selected input and count it in the result.
    Discard
};

/// Whether a value names a scatter bounds rule.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to test.
/// @return true when it is one of the two enumerators.
[[nodiscard]] constexpr bool isScatterBoundsPolicy(ScatterBoundsPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(ScatterBoundsPolicy::Discard);
}

/// Conflict and footprint choices for one scatter invocation.
struct ScatterKernelParameters
{
    /// Deterministic rule for repeated destination coordinates.
    ScatterConflictPolicy conflicts = ScatterConflictPolicy::Reject;

    /// Rule for coordinates outside the destination footprint.
    ScatterBoundsPolicy bounds = ScatterBoundsPolicy::Reject;
};

/// Result of a completed scatter.
struct ScatterKernelResult
{
    /// Source selection and destination write counts.
    KernelRunResult run{};

    /// Selected in-bounds inputs whose rounded destination had already been named earlier.
    std::uint64_t conflicts = 0u;

    /// Selected inputs discarded for lying outside the destination.
    std::uint64_t discarded = 0u;

    /// Two scatter results agree when every count agrees.
    friend constexpr bool operator==(const ScatterKernelResult& left,
                                     const ScatterKernelResult& right) noexcept = default;
};

/// Gathers nearest samples into a caller-owned linear output run.
///
/// The destination must hold at least one element per coordinate and must not overlap the source.
/// A constant-border output is valid and written; an invalid in-footprint source leaves its output
/// untouched. Cancellation is checked during coordinate validation and every 256 outputs.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample input plane.
/// @param coordinates the finite, representable output locations to read, in output order.
/// @param destination where gathered values are written.
/// @param parameters clamp or constant-border behavior.
/// @param validity the source validity rule and optional mask, borrowed for the call.
/// @return the counts; `ErrorCode::InvalidArgument` for a border other than `Clamp` or `Constant`
///         (checked first), a non-finite constant border, a coordinate that is not finite or not
///         representable, or a destination that overlaps the source;
///         `ErrorCode::IncompatibleArguments` when the destination is shorter than the coordinate
///         run; or a shared refusal named in the file comment.
[[nodiscard]] AFTERNIGHTENGINE_API Result<KernelRunResult>
referenceGatherKernel(const ExecutionContext& execution, PlaneView<const float> source,
                      std::span<const KernelCoordinate> coordinates, std::span<float> destination,
                      const NearestKernelParameters& parameters, const KernelValidityView& validity);

/// Gathers samples under any `KernelInterpolation` with the authoritative scalar numeric contract.
///
/// A point has no footprint, so `Area` and `LinearExact` read as `Linear` and `NearestExact` as
/// `Nearest`; `Cubic` and `Lanczos4` read their own windows. Every nonzero-weight tap follows the
/// border rule and the invalid-neighbour policy, and the weighted taps accumulate in Float64 and
/// narrow once. Coordinates are validated before the first write. Outputs are independent and
/// written in coordinate order; an output the policy cannot produce leaves its slot untouched.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample Float32 source plane.
/// @param coordinates the finite, representable locations to read, in output order.
/// @param destination at least one Float32 slot per coordinate and disjoint from the source.
/// @param parameters interpolation, border, constant, and invalid-neighbor policy.
/// @param validity the source validity rule and optional mask, borrowed for the call.
/// @return the counts; `ErrorCode::IncompatibleArguments` when the destination is shorter than the
///         coordinate run; `ErrorCode::InvalidArgument` for an unknown interpolation, border or
///         invalid-neighbour policy, `KernelBorderMode::NotApplicable`, a non-finite constant or
///         substitute, a coordinate that is not finite or not representable, or a destination that
///         overlaps the source; or a shared refusal named in the file comment.
[[nodiscard]] AFTERNIGHTENGINE_API Result<KernelRunResult>
referenceGatherInterpolatedKernel(const ExecutionContext& execution, PlaneView<const float> source,
                                  std::span<const KernelCoordinate> coordinates, std::span<float> destination,
                                  const SamplingKernelParameters& parameters, const KernelValidityView& validity);

/// Gathers independent samples on the operation's admitted threads.
///
/// Numeric results and untouched invalid slots are bitwise equal to
/// `referenceGatherInterpolatedKernel`. All coordinates and overlap are checked before work starts;
/// a stop observed after that point may leave some scheduler ranges written and the rest untouched.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample Float32 source plane.
/// @param coordinates the finite, representable locations to read, in output order.
/// @param destination at least one Float32 slot per coordinate and disjoint from the source.
/// @param parameters interpolation, border, constant, and invalid-neighbor policy.
/// @param validity the source validity rule and optional mask, borrowed through the call.
/// @return the counts, or the refusals `referenceGatherInterpolatedKernel` documents.
[[nodiscard]] AFTERNIGHTENGINE_API Result<KernelRunResult>
gatherSamples(const ExecutionContext& execution, PlaneView<const float> source,
              std::span<const KernelCoordinate> coordinates, std::span<float> destination,
              const SamplingKernelParameters& parameters, const KernelValidityView& validity);

/// Scatters source samples to rounded pixel-center destinations in fixed source-row order.
///
/// There is exactly one coordinate per source sample. Source and destination must be disjoint. A
/// rejecting bounds or conflict policy performs a complete validation pass before the first write,
/// so its failure leaves the destination untouched. `First`, `Last`, and `Add` are deterministic
/// because the scalar source order is fixed. Cancellation is checked every 256 inputs, including
/// during conflict searches; a stop after writing begins may leave a prefix committed.
///
/// The oracle finds a conflict by comparing each selected input with every earlier one, so its cost
/// grows with the square of the sample count; `scatterPlane` groups by destination in linear time.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample source plane.
/// @param coordinates one destination coordinate per logical source sample.
/// @param destination the single-sample output plane.
/// @param parameters conflict and out-of-bounds behavior.
/// @param validity the source validity rule and optional mask, borrowed for the call.
/// @return the counts, conflicts, and discarded inputs; `ErrorCode::IncompatibleArguments` when the
///         coordinate count is not the source's sample count, or when two selected inputs name one
///         destination pixel under `ScatterConflictPolicy::Reject`; `ErrorCode::OutOfRange` for a
///         selected coordinate outside the destination under `ScatterBoundsPolicy::Reject`;
///         `ErrorCode::InvalidArgument` for an unknown policy, a coordinate that is not finite or
///         not representable, or any overlap of source and destination; or a shared refusal named
///         in the file comment.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ScatterKernelResult>
referenceScatterKernel(const ExecutionContext& execution, PlaneView<const float> source,
                       std::span<const KernelCoordinate> coordinates, PlaneView<float> destination,
                       const ScatterKernelParameters& parameters, const KernelValidityView& validity);

/// Counts, bounded-scratch facts, and scheduling facts from a parallel scatter.
struct ScatterExecutionResult
{
    /// Logical scatter result, including conflicts and discarded selected inputs.
    ScatterKernelResult run{};

    /// Distinct in-bounds destination pixels named by selected inputs.
    std::uint64_t destinationGroups = 0u;

    /// Temporary bytes requested from the engine context for deterministic grouping.
    std::uint64_t scratchBytes = 0u;

    /// Workers actually planned over the distinct destination groups.
    std::uint32_t plannedWorkers = 0u;
};

/// Deterministically scatters a Float32 plane after bounded-scratch destination grouping.
///
/// Selected inputs are stably radix-grouped by row-major destination index, retaining source order
/// within a group. Groups are disjoint and may run in parallel; `First`, `Last`, and `Add` therefore
/// produce bitwise identical results at every thread count. Bounds and `Reject` conflicts are
/// resolved before the first destination write. Temporary memory is charged to the engine context
/// and released on every exit.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample Float32 source plane.
/// @param coordinates exactly one finite, representable destination coordinate per source sample.
/// @param destination the disjoint single-sample Float32 output plane.
/// @param parameters deterministic conflict and out-of-bounds policies.
/// @param validity the source validity rule and optional mask, borrowed through the call.
/// @return the counts, grouping, scratch and worker facts; the refusals `referenceScatterKernel`
///         documents, where every argument, bounds and conflict refusal is raised before the first
///         destination write and a stop can arrive later; `ErrorCode::LimitExceeded`
///         when the grouping buffers would exceed addressable memory; `ErrorCode::NotInitialized` for
///         an execution with no memory resource; or `ErrorCode::MemoryBudgetExceeded` or
///         `ErrorCode::AllocationFailed` when that resource refuses the grouping buffers.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ScatterExecutionResult>
scatterPlane(const ExecutionContext& execution, PlaneView<const float> source,
             std::span<const KernelCoordinate> coordinates, PlaneView<float> destination,
             const ScatterKernelParameters& parameters, const KernelValidityView& validity);

} // namespace an
