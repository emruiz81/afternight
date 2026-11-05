// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What a run must recompute and what it may reuse: the plan the executor follows and the decisions the plan dialog
/// shows, each stage's and group's action with its reason.

#include "WorkflowManifest.h"
#include "ManifestDiff.h"
#include "WorkflowStageCompanions.h"

#include <memory>
#include <optional>
#include <vector>

class IntegrationDataModel;
class ExecutionPreparationSnapshot;

struct ExecutionPlanDecision
{
    enum class Action
    {
        Recompute,
        Reuse,
        Skip
    };

    QString stage;
    QString groupKey;
    Action action = Action::Skip;
    QString actionLabel;
    QString reason;
    QString dependencies;
    int estimatedSeconds = 0;
    QString estimatedTime;
    QString outputs;
    std::optional<ImageIntegration::TransformOnReadRouting> transformOnReadDecision;
};

/**
 * @struct ExecutionPlan
 * @brief Minimum set of actions required to fulfill a workflow request.
 */
struct AFTERNIGHTPROCESSES_API ExecutionPlan
{
    ExecutionMode mode = ExecutionMode::Full;

    // Specific groups that need reprocessing (for Incremental mode)
    std::vector<GroupKey> biasGroupsToProcess;
    std::vector<GroupKey> darkGroupsToProcess;
    std::vector<GroupKey> flatGroupsToProcess;
    std::vector<GroupKey> registrationGroupsToProcess;
    std::vector<IntegrationGroupKey> integrationGroupsToProcess;

    // Returns true only for a no-op Incremental plan. For other modes, the executor filters
    // stages based on ExecutionMode, and the plan does not enumerate groups.
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] QString toSummaryText() const;
    void ensureIntegrationGroupsToProcess(const std::vector<IntegrationGroupKey>& groups);
};

/**
 * @class ExecutionPlanner
 * @brief Analyzes manifest changes to determine the optimal execution path.
 */
class AFTERNIGHTPROCESSES_API ExecutionPlanner
{
public:
    ExecutionPlanner() = default;
    explicit ExecutionPlanner(const IntegrationDataModel& model);

    // Build plan by comparing with previous manifest. The snapshot this overload builds reads no frame header, so it
    // reuses no drizzle artifact; a caller that has a session builds the snapshot with its header reader.
    [[nodiscard]] ExecutionPlan buildPlan(const WorkflowManifest& currentManifest,
                                          const std::optional<WorkflowManifest>& previousManifest) const;
    [[nodiscard]] ExecutionPlan buildPlan(const WorkflowManifest& currentManifest,
                                          const ExecutionPreparationSnapshot& snapshot) const;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<ExecutionPlanDecision>
buildExecutionPlanDecisions(const WorkflowManifest& manifest, const ExecutionPlan& plan,
                            const std::optional<WorkflowManifest>& previousManifest = std::nullopt);
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<ExecutionPlanDecision>
buildExecutionPlanDecisions(const WorkflowManifest& manifest, const ExecutionPlan& plan,
                            const ExecutionPreparationSnapshot& snapshot);
