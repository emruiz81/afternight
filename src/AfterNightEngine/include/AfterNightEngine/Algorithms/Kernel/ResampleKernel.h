// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Resampling one Float32 plane at one stated coordinate per destination sample.
///
/// The caller supplies the whole mapping: coordinates are row-major over the destination, follow
/// the pixel-centre convention of `KernelCoordinate`, and must be finite and no further than
/// 2^53 - 2 from zero. `referenceResampleNearestKernel` and `referenceResampleKernel` are the scalar
/// oracles and walk the destination row by row on the calling thread; `resamplePlane` divides it
/// into tiles over the operation's threads and writes the oracle's bits whatever the thread count,
/// because every output is independent and is read through the same sampling function.
///
/// Every entry point refuses with `ErrorCode::NotInitialized` an execution that holds nothing; with
/// `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` a stopped one; with
/// `ErrorCode::IncompatibleArguments` a plane with more than one sample per pixel, a coordinate count
/// that is not the destination's sample count, or a packed mask that does not cover the source and
/// its selected bit-plane; and with `ErrorCode::InvalidArgument` a coordinate that is not finite or
/// not representable, a destination that overlaps the source in any way, or a packed mask supplied
/// under a rule that reads none or missing under `ValidityRule::ExplicitMask`.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <span>

namespace an
{

/// Samples a source plane into an output plane by nearest pixel-center coordinates.
///
/// Coordinates are row-major over the destination logical samples and must be finite and exactly
/// representable by the reference kernel's signed coordinate domain. Source and
/// destination are disjoint. A constant-border output is valid and written; an invalid in-footprint
/// source leaves the destination untouched. Cancellation is checked during coordinate validation
/// and once per output row; a mid-run stop may leave a row-major prefix written.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the single-sample output plane.
/// @param coordinates exactly one coordinate per destination sample.
/// @param parameters clamp or constant-border behavior.
/// @param validity the source validity rule and optional mask, borrowed for the call.
/// @return the counts; `ErrorCode::InvalidArgument` for a border other than `Clamp` or `Constant`,
///         checked before anything else, or for a non-finite constant border; or a refusal the file
///         comment names.
[[nodiscard]] AFTERNIGHTENGINE_API Result<KernelRunResult>
referenceResampleNearestKernel(const ExecutionContext& execution, PlaneView<const float> source,
                               PlaneView<float> destination, std::span<const KernelCoordinate> coordinates,
                               const NearestKernelParameters& parameters, const KernelValidityView& validity);

/// Authoritative scalar resampling oracle, under any `KernelInterpolation`.
///
/// Coordinates use the pixel-center convention and are row-major over the destination. Every
/// coordinate is checked before the first write. A coordinate has no footprint, so `Area` and
/// `LinearExact` read as `Linear` and `NearestExact` as `Nearest`, while `Cubic` and `Lanczos4` read
/// their own windows; the weighted taps accumulate in `double` and narrow once. The selected border
/// and invalid-neighbor policies apply independently to every nonzero-weight tap, and an output the
/// policy cannot produce leaves its destination sample untouched. Source and destination must be
/// disjoint.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample Float32 source plane.
/// @param destination the single-sample Float32 output plane.
/// @param coordinates exactly one finite, representable coordinate per destination sample.
/// @param parameters interpolation, border, constant, and invalid-neighbor policy.
/// @param validity the source validity rule and optional mask, borrowed for the call.
/// @return the counts; `ErrorCode::InvalidArgument` for an unknown interpolation, border or
///         invalid-neighbour policy, `KernelBorderMode::NotApplicable`, or a non-finite constant or
///         substitute; or a refusal the file comment names.
[[nodiscard]] AFTERNIGHTENGINE_API Result<KernelRunResult>
referenceResampleKernel(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
                        std::span<const KernelCoordinate> coordinates, const SamplingKernelParameters& parameters,
                        const KernelValidityView& validity);

/// Counts and scheduling facts from parallel resampling.
struct SamplingExecutionResult
{
    /// Logical selection and destination-write counts.
    KernelRunResult run{};

    /// Row-major tiles planned for the destination.
    std::uint64_t tileCount = 0u;

    /// Workers the operation's admission and tile count actually planned.
    std::uint32_t plannedWorkers = 0u;
};

/// Resamples a Float32 plane through the central parallel tile executor.
///
/// Numeric results are bitwise equal to `referenceResampleKernel`: each output is independent,
/// bilinear accumulation order is fixed, and thread count changes only which worker owns a tile.
/// Validation is atomic before the first write. Cancellation after execution starts may leave
/// complete tiles committed and is observed before every tile.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample Float32 source plane.
/// @param destination the disjoint single-sample Float32 output plane.
/// @param coordinates exactly one finite, representable coordinate per destination sample.
/// @param parameters interpolation, border, constant, and invalid-neighbor policy.
/// @param validity the source validity rule and optional mask, borrowed through the call.
/// @return the counts and the tile and worker plan it ran; the refusals `referenceResampleKernel`
///         documents; or `ErrorCode::LimitExceeded` when the destination's tile grid or its work
///         estimate cannot be represented.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SamplingExecutionResult>
resamplePlane(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
              std::span<const KernelCoordinate> coordinates, const SamplingKernelParameters& parameters,
              const KernelValidityView& validity);

} // namespace an
