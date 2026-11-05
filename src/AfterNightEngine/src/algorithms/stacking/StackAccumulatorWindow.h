// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - accumulation into, and finalization out of, stack terms that hold one
// tile's rows.
//
// When `stackAccumulatorsCoverOneTile` allows it, the planner counts the accumulator terms for one
// tile rather than for the whole output, and the executor finalizes each tile's rows as soon as
// every frame's band of it is folded in, then clears the same terms for the next tile. These two
// operations are the whole-output ones re-addressed to such a window: the arithmetic, the checks
// and the bits are the same, and only where an element's terms sit changes. They are private
// because the window is a planning decision a caller of the published accumulators never makes.
//
// The same window addresses the output when its caller takes it tile by tile: the image, the mask and
// the maps then hold one tile's rows, and an element of output row `y` sits `y - firstRow` rows into
// its channel's plane of them, exactly as a term does.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <span>

namespace an
{
class ExecutionContext;
class StackModel;
} // namespace an

/// The stacking operations' private helpers and the executor's state, shared by the stacking units
/// and by nothing else.
namespace an::stacking
{

/// The output rows a set of terms, or a set of finalization targets, holds.
///
/// The terms or the targets are packed planar over the window: channel by channel, and inside a
/// channel the window's rows in order at the output's width, so the element of output row `y` sits
/// `y - firstRow` rows into its channel's plane.
struct StackAccumulatorWindow
{
    /// The output row the terms' first row is.
    std::uint32_t firstRow = 0u;

    /// How many rows the terms hold.
    std::uint32_t rows = 0u;
};

/// Whether a stack's frames can be accumulated at their stored representation.
///
/// True for 8 and 16-bit integer codes of the raw-sensor domain, whose widening to the Float32 tile
/// carries each code across as the number it is: accumulating the code in Float64 is accumulating
/// its Float32 widening, bit for bit, so the executor need not write the widened band at all.
///
/// @param model the checked stack.
/// @return true when the stored representation may stand in for the tile's.
[[nodiscard]] bool stackAccumulatesStoredSamples(const StackModel& model) noexcept;

/// `accumulateStackFrames` into terms that hold only a window of the output.
///
/// The executor folds a region tile's bands into terms that hold that tile when
/// `stackAccumulatorsCoverOneTile` says the stack allows it, and a whole-frame band into terms that
/// hold the output. Every band must lie inside the window; each element's terms are updated exactly
/// as the whole-output operation updates them.
///
/// @param execution the operation.
/// @param model the checked stack.
/// @param set the terms, each kept one covering every element of the window.
/// @param window the output rows the terms hold.
/// @param bands one band per frame, all over the same rows.
/// @param frameWeights the frame weight table, or empty.
/// @param admittedPerBand receives each band's admitted count.
/// @param storedSamples whether the bands are at the frames' stored representation rather than the
///        tile's, which `stackAccumulatesStoredSamples` must allow.
/// @return what the whole-output operation reports; `ErrorCode::OutOfRange` for a window outside
///         the output; `ErrorCode::IncompatibleArguments` when a term does not cover the window, the
///         bands lie outside it or a band is at a representation this call was not told of.
[[nodiscard]] Result<StackAccumulationOutcome>
accumulateStackWindow(const ExecutionContext& execution, const StackModel& model, const StackAccumulatorSet& set,
                      const StackAccumulatorWindow& window, std::span<const StackFrameBand> bands,
                      std::span<const double> frameWeights, std::span<std::uint64_t> admittedPerBand,
                      bool storedSamples = false);

/// `finalizeStackAccumulators` for a window's rows, from terms that hold only that window.
///
/// The window's output rows, their mask rows and their map entries are written exactly as the
/// whole-output finalization writes them, with a divisor of one; nothing outside the window is
/// touched. What it reports counts the window's elements.
///
/// @param execution the operation.
/// @param model the checked stack.
/// @param set the terms, each kept one covering every element of the window.
/// @param window the output rows to write.
/// @param targets the whole output's image, mask and maps, or the window's alone.
/// @param targetsCoverWindow whether the targets hold the window's rows alone.
/// @return what was written; `ErrorCode::OutOfRange` for a window outside the output; the
///         whole-output operation's refusals otherwise.
[[nodiscard]] Result<StackFinalizeOutcome> finalizeStackWindow(const ExecutionContext& execution,
                                                               const StackModel& model, const StackAccumulatorSet& set,
                                                               const StackAccumulatorWindow& window,
                                                               const StackFinalizeTargets& targets,
                                                               bool targetsCoverWindow = false);

/// `checkStackFinalizeTargets` for targets that hold a window of the output's rows alone.
///
/// @param model the checked stack.
/// @param targets the window's image, mask and maps.
/// @param window the output rows they hold.
/// @param operation the public caller the refusal names.
/// @return the window's element count; `ErrorCode::OutOfRange` for a window outside the output; the
///         whole-output check's refusals otherwise, over the window's rows.
[[nodiscard]] Result<std::uint64_t> checkStackWindowTargets(const StackModel& model,
                                                            const StackFinalizeTargets& targets,
                                                            const StackAccumulatorWindow& window,
                                                            const char* operation);

} // namespace an::stacking
