// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <cstddef>
#include <optional>
#include <string>

class ImageHistoryLineage;
class ImageHistoryTracker;

namespace an::ui::history
{

struct StepRemovalPlan
{
    size_t next_current_step = 0;
    bool should_restore_after_removal = false;
};

[[nodiscard]] constexpr bool isValidStepIndex(const size_t total_steps, const size_t step_index)
{
    return step_index < total_steps;
}

[[nodiscard]] constexpr bool canJumpToStep(const size_t total_steps, const size_t current_step,
                                           const size_t target_step)
{
    return isValidStepIndex(total_steps, current_step) && isValidStepIndex(total_steps, target_step) &&
           current_step != target_step;
}

[[nodiscard]] constexpr bool canRemoveStep(const size_t total_steps, const size_t step_index)
{
    return total_steps > 1 && step_index > 0 && step_index < total_steps;
}

[[nodiscard]] constexpr std::optional<size_t> previousStep(const size_t current_step)
{
    if (current_step == 0)
    {
        return std::nullopt;
    }

    return current_step - 1;
}

[[nodiscard]] constexpr std::optional<size_t> nextStep(const size_t total_steps, const size_t current_step)
{
    if (!isValidStepIndex(total_steps, current_step) || current_step + 1 >= total_steps)
    {
        return std::nullopt;
    }

    return current_step + 1;
}

[[nodiscard]] constexpr StepRemovalPlan planStepRemoval(const size_t total_steps, const size_t current_step,
                                                        const size_t removed_step)
{
    StepRemovalPlan plan{current_step, false};

    if (!isValidStepIndex(total_steps, current_step) || !canRemoveStep(total_steps, removed_step))
    {
        return plan;
    }

    if (removed_step == current_step)
    {
        plan.next_current_step = current_step > 0 ? current_step - 1 : 0;
        plan.should_restore_after_removal = true;
        return plan;
    }

    if (removed_step < current_step)
    {
        plan.next_current_step = current_step - 1;
    }

    return plan;
}

/// The history as text: the inherited lineage, then every step, the current one marked.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string formatHistoryLog(const ImageHistoryTracker& tracker);

/// The lineage a view made from another view's image continues: the source's own lineage and every step it recorded.
[[nodiscard]] AFTERNIGHTCONTROLS_API ImageHistoryLineage inheritedLineage(const ImageHistoryTracker& source);

} // namespace an::ui::history
