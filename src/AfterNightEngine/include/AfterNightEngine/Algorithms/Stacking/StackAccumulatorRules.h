// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Model-dependent decisions kept out of the kernel interface.
///
/// `Algorithms/Stacking/StackAccumulators.h` names `StackModel` without including it, so a kernel
/// unit that only runs the arithmetic does not inherit the model's include closure. What the
/// accumulator needs from a model - which `StackAccumulatorUpdate` its combination implies - is a
/// constant expression over the full model type, and it lives here, where both are included.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>

namespace an
{

/// How a model's samples change its running terms.
///
/// The minimum and the maximum combinations keep an extremum; every other combination keeps a
/// weighted sum. A model whose strategy is sample history has no whole-output accumulators, and
/// the operations below refuse it before this table is consulted.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @return the update.
[[nodiscard]] constexpr StackAccumulatorUpdate stackAccumulatorUpdateOf(const StackModel& model) noexcept
{
    switch (model.request().combination)
    {
    case StackCombination::Minimum:
        return StackAccumulatorUpdate::Minimum;
    case StackCombination::Maximum:
        return StackAccumulatorUpdate::Maximum;
    case StackCombination::Average:
    case StackCombination::Sum:
    case StackCombination::Median:
    case StackCombination::AutoAdaptiveWeightedAverage:
        break;
    }
    return StackAccumulatorUpdate::WeightedSum;
}

} // namespace an
