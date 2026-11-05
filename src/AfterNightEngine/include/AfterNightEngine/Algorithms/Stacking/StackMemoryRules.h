// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which accumulator terms a stack keeps, and over how many rows.
///
/// The planner counts the terms, the working set takes them and the executor fills them, and all
/// three have to agree on the same answers; this header states them once, as constant expressions
/// over a checked model. `stackAccumulatorKept` says which terms a streaming stack keeps,
/// `stackAccumulatorsCoverOneTile` whether those terms hold the whole output or only the tile
/// being combined, and `stackAccumulatorBytesPerRow` what one output row of them costs. They live
/// apart from `Algorithms/Stacking/StackAccumulators.h` because they need the full model and the
/// working set's accumulator table, which the arithmetic does not. `stackOutputCanCoverOneTile`
/// says the same of the output, its mask and its maps, for a caller that takes them tile by tile.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>

#include <cstddef>
#include <cstdint>

namespace an
{

/// Whether a stack keeps an accumulator.
///
/// Nothing is kept under `StackAccumulationStrategy::TileSampleHistory`, whose combination writes
/// the output directly. Under whole-output accumulators the sums and the counts are always kept,
/// the weight sums when the stack weighs its frames or applies a local surface, the squared
/// weight sums when the gain map is asked for, and the variance moments when the variance map is.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param accumulator the accumulator. A value outside the enumeration is never kept.
/// @return true when the stack keeps it.
[[nodiscard]] constexpr bool stackAccumulatorKept(const StackModel& model, StackAccumulator accumulator) noexcept
{
    if (stackAccumulationStrategyOf(model) != StackAccumulationStrategy::WholeOutputAccumulators)
    {
        return false;
    }
    switch (accumulator)
    {
    case StackAccumulator::Sums:
    case StackAccumulator::Counts:
        return true;
    case StackAccumulator::WeightSums:
        return model.usesFrameWeights() || model.usesLocalWeights();
    case StackAccumulator::WeightSquareSums:
        return model.request().output.signalToNoiseGainMap;
    case StackAccumulator::VarianceMoments:
        return model.request().output.varianceMap;
    }
    return false;
}

/// Whether a stack's accumulators hold one tile's rows rather than the whole output.
///
/// Under region access every frame's band of a tile is folded into the terms before the next tile
/// is read, so an element's terms are complete once its tile is combined. When nothing reads the
/// terms across tiles afterwards, the executor finalizes each tile's rows as soon as they are
/// combined and clears the same terms for the next tile. That holds for a mean, a minimum or a
/// maximum with no frame weights, no local weights and no normalization. A sum keeps whole-output
/// terms because its sum domain may divide every element by the output's peak; a weighted stack
/// because its integration summary is read from the weight sums of the whole output; and a
/// normalizing stack because what it measures before the combine is planned beside persistent
/// state that has to be sized before the tile is. Whole-frame access folds one frame into every
/// tile in turn, so no tile is complete before the last frame, and it keeps whole-output terms too.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param access how the source serves its frames.
/// @return true when the terms hold one tile's rows.
[[nodiscard]] constexpr bool stackAccumulatorsCoverOneTile(const StackModel& model, StackSourceAccess access) noexcept
{
    const StackRequest& request = model.request();
    const bool elementwise = request.combination == StackCombination::Average ||
                             request.combination == StackCombination::Minimum ||
                             request.combination == StackCombination::Maximum;
    return isStackSourceRegionAccess(access) &&
           stackAccumulationStrategyOf(model) == StackAccumulationStrategy::WholeOutputAccumulators && elementwise &&
           !model.usesFrameWeights() && !model.usesLocalWeights() &&
           request.normalization.scope == StackNormalizationScope::None;
}

/// Whether a stack's output can be held for one tile's rows and handed to its caller tile by tile.
///
/// The output of a tile can leave the working set as soon as nothing will write it again. That is
/// so as soon as the tile is combined under sample history, whose combination writes each tile's
/// output directly, and when the terms hold one tile, which are finalized into the tile's output
/// before the next tile clears them. Whole-output terms are finalized only after the last frame,
/// so their output is written whole at the end and is held whole.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param access how the source serves its frames.
/// @return true when the planner may size the output, its mask and its maps for one tile.
[[nodiscard]] constexpr bool stackOutputCanCoverOneTile(const StackModel& model, StackSourceAccess access) noexcept
{
    return stackAccumulationStrategyOf(model) == StackAccumulationStrategy::TileSampleHistory ||
           stackAccumulatorsCoverOneTile(model, access);
}

/// What one output row of every kept accumulator costs, over every channel.
///
/// The per-element figures of `stackAccumulatorBytesPerElement` for each accumulator
/// `stackAccumulatorKept` keeps, times the width and the channels: the whole-output terms are this
/// times the height, and terms that hold one tile are this times the tile's rows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @return the bytes, zero under sample history.
[[nodiscard]] constexpr std::uint64_t stackAccumulatorBytesPerRow(const StackModel& model) noexcept
{
    std::uint64_t perElement = 0u;
    for (std::size_t index = 0u; index < kStackAccumulatorCount; ++index)
    {
        const auto accumulator = static_cast<StackAccumulator>(index);
        if (stackAccumulatorKept(model, accumulator))
        {
            perElement += stackAccumulatorBytesPerElement(accumulator);
        }
    }
    const ImageExtent& extent = model.request().input.descriptor.extent();
    return static_cast<std::uint64_t>(extent.width()) * extent.channelCount() * perElement;
}

} // namespace an
