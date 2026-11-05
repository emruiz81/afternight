// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the checks the sample-history combinations share, and the failure
// helpers every stacking unit builds its refusals with.
//
// `combineStackSampleHistory` and `combineRejectedStackSampleHistory` take every frame's band of
// one tile at once, and both hold the bands and the per-worker scratch to one rule before a sample
// is read: `checkHistory` checks each band as `checkStackFrameBand` does and then against the
// first, and `checkScratch` checks the slices against the plan's per-worker scratch.
// `frameRecordOf` flattens a checked band into the record the column row reads. `failure` and
// `refuse` spell a refusal in the algorithms domain, so every stacking operation's failures carry
// the same shape: an error code, the operation's qualified name, a sentence for a person and the
// typed context a caller branches on. `windowHistory` re-addresses a checked history to targets
// that hold one window of the output's rows, which the two combinations' window forms below write
// when the executor's caller takes the output tile by tile.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejection.h>
#include "algorithms/stacking/StackAccumulatorWindow.h"
#include "kernels/reference/AlgorithmKernels.h"

#include <string>
#include <vector>

namespace an::stacking
{
/// What a checked history says about where its bands are, once every band has been checked.
struct HistoryGeometry
{
    /// The output's width, which every band shares, in pixels.
    std::uint32_t width = 0u;

    /// How many rows every band holds.
    std::uint32_t rows = 0u;

    /// The output's channel count, which every band shares.
    std::uint32_t channels = 0u;

    /// The output row every band's first row lands on.
    std::uint32_t firstOutputRow = 0u;

    /// How many elements one channel plane of the targets holds: of the whole output, or of the
    /// window `windowHistory` re-addressed the history to. `firstOutputRow` is then counted from the
    /// window's first row.
    std::uint64_t planeElements = 0u;

    /// How many rows the targets hold: the output's height, or the window's rows.
    std::uint32_t targetRows = 0u;

    /// Whether every band's layout holds one sample per pixel in each plane.
    bool planar = true;
};

/// A failure of one stacking operation in the algorithms domain.
///
/// @param code why it failed.
/// @param operation the operation's qualified name, a string literal.
/// @param message what a person reads.
/// @param context the typed facts a caller branches on, if any.
/// @return the failure.
[[nodiscard]] Status failure(ErrorCode code, const char* operation, std::string message,
                             std::vector<StatusContextField> context = {});

/// A failure naming the parameter at fault as its `ContextKey::Parameter` field.
///
/// @param code why it failed.
/// @param operation the operation's qualified name, a string literal.
/// @param parameter the parameter, spelled as a caller reads it.
/// @param message what is wrong with it.
/// @return the failure.
[[nodiscard]] Status refuse(ErrorCode code, const char* operation, const char* parameter, std::string message);

/// Whether an address is a multiple of a boundary, which must not be zero.
[[nodiscard]] bool pointerIsAligned(const void* pointer, std::uint64_t alignment) noexcept;

/// Checks one band per frame of a history against the model and against the first band.
///
/// Every band must pass `checkStackFrameBand`, sit at its own frame index, and land on the first
/// band's output rows with the first band's row count. Refusals are
/// `ErrorCode::IncompatibleArguments` for the wrong number of bands or a band of another shape,
/// `ErrorCode::InvalidArgument` for a band out of its place, and whatever `checkStackFrameBand`
/// refuses. Every refusal of its own names the supplied public caller as its operation.
[[nodiscard]] Result<HistoryGeometry> checkHistory(const StackModel& model, std::span<const StackFrameBand> frames,
                                                   const char* operation);

/// Re-addresses a checked history to targets that hold one window of the output's rows alone.
///
/// The bands' first row and the plane size become the window's: `firstOutputRow` counted from the
/// window's first row, `planeElements` and `targetRows` the window's. Refusals are
/// `ErrorCode::OutOfRange` naming `window` when the bands' rows do not lie inside it.
[[nodiscard]] Result<HistoryGeometry> windowHistory(const HistoryGeometry& geometry,
                                                    const StackAccumulatorWindow& window, const char* operation);

/// `combineStackSampleHistory` into targets that hold one window of the output's rows alone, which
/// the bands' rows must lie inside; the targets are held to the window's rows as
/// `checkStackWindowTargets` holds them.
[[nodiscard]] Result<StackSampleHistoryOutcome>
combineStackSampleHistoryWindow(const ExecutionContext& execution, const StackModel& model,
                                std::span<const StackFrameBand> frames,
                                std::span<const std::span<std::byte>> workerScratch,
                                const StackFinalizeTargets& targets, const StackAccumulatorWindow& window);

/// `combineRejectedStackSampleHistory` with per-frame counters, into targets and maps that hold one
/// window of the output's rows alone.
[[nodiscard]] Result<StackRejectionOutcome> combineRejectedStackSampleHistoryWindow(
    const ExecutionContext& execution, const StackModel& model, std::span<const StackFrameBand> frames,
    std::span<const std::span<std::byte>> workerScratch, const StackFinalizeTargets& targets,
    std::span<const double> frameWeights, const StackRejectionMaps& maps,
    StackRejectionCombinationPolicy combinationPolicy, const StackRejectionFrameCounters& counters,
    const StackAccumulatorWindow& window);

/// Checks that there is one scratch slice per admitted worker, each at least the model's per-worker
/// scratch and on the scratch alignment boundary; `ErrorCode::IncompatibleArguments` for too few or
/// too small, `ErrorCode::InvalidArgument` for a misaligned one, under the same operation name as
/// `checkHistory`.
[[nodiscard]] Status checkScratch(const ExecutionContext& execution, const StackModel& model,
                                  std::span<const std::span<std::byte>> workerScratch, const char* operation);

/// The record the column row reads for one checked band: its sample address, strides, element
/// width, validity rule and, under the explicit-mask rule, its packed mask. Borrows the band's bytes.
[[nodiscard]] StackColumnFrame frameRecordOf(const StackFrameBand& band) noexcept;
} // namespace an::stacking
