// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - private execution door for explicitly isolated preview programs.
//
// The published executor refuses a program holding a lower-neighbour sampled curve, because that
// step is a coarse preview approximation and must never reach a production chain. The real-time
// preview still has to run one, so the executor's body is reached through this private entry
// with the refusal made a parameter, and only `executeRealtimePreviewGamma`, whose plan cannot
// be inspected or composed, passes true.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>

/// Implementation entry points shared between engine translation units and never published.
namespace an::detail
{

/// Runs a pointwise program exactly as `executePointwiseProgram` does, with the refusal of a
/// lower-neighbour curve under the caller's control.
///
/// Every check, failure and counter is the published executor's; the only difference is that a
/// program holding a `PointwiseCurveSampling::Lower` step is executed rather than refused with
/// `ErrorCode::IncompatibleArguments` when `permitRealtimePreviewApproximation` is true.
///
/// @param execution the operation policy and engine-context lease.
/// @param program the immutable validated chain.
/// @param sources exactly `program.sourceCount()` borrowed planar images.
/// @param destination the equally shaped writable planar image.
/// @param validity the primary input's validity description and optional shared packed mask.
/// @param permitRealtimePreviewApproximation true only for the opaque real-time preview plan.
/// @return what `executePointwiseProgram` returns for the same arguments, apart from that refusal.
[[nodiscard]] Result<PointwiseExecutionResult>
executePointwiseProgramImpl(const ExecutionContext& execution, const PointwiseProgram& program,
                            std::span<const PointwiseImageView> sources, PointwiseMutableImageView destination,
                            const KernelValidityView& validity, bool permitRealtimePreviewApproximation);

} // namespace an::detail
