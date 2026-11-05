// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - choosing a drizzled stack's route from the stack model's own accumulation
// strategy. Only the caller's policy selects the dump-history route; the automatic route is always
// the per-frame source, and the reason records whether the stack's combination needs tile sample
// history, a per-frame extremum or variance reduction, or neither.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleStackWorkflow.h>

#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

namespace an
{

Result<DrizzleStackWorkflowPlan> planDrizzleStackWorkflow(const StackModel& model, DrizzleStackExecutionPolicy policy)
{
    if (policy != DrizzleStackExecutionPolicy::Automatic && policy != DrizzleStackExecutionPolicy::DumpHistoryReference)
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               {.message = "the drizzle stack execution policy is outside its enumeration",
                                .operation = "an::planDrizzleStackWorkflow",
                                .domain = ErrorDomain::Algorithms});
    }

    const StackAccumulationStrategy strategy = stackAccumulationStrategyOf(model);
    if (policy == DrizzleStackExecutionPolicy::DumpHistoryReference)
    {
        return DrizzleStackWorkflowPlan{.path = DrizzleStackExecutionPath::DumpHistoryReference,
                                        .historyReason = DrizzleStackHistoryReason::ReferencePolicy,
                                        .accumulationStrategy = strategy,
                                        .dumpBacked = true};
    }

    DrizzleStackHistoryReason reason = DrizzleStackHistoryReason::None;
    if (strategy == StackAccumulationStrategy::TileSampleHistory)
    {
        reason = DrizzleStackHistoryReason::SampleHistory;
    }
    else
    {
        const StackRequest& request = model.request();
        if (request.combination == StackCombination::Minimum || request.combination == StackCombination::Maximum ||
            request.output.varianceMap)
        {
            reason = DrizzleStackHistoryReason::DeferredPerFrameReduction;
        }
    }
    return DrizzleStackWorkflowPlan{.path = DrizzleStackExecutionPath::DirectPerFrameSource,
                                    .historyReason = reason,
                                    .accumulationStrategy = strategy,
                                    .dumpBacked = false};
}

} // namespace an
