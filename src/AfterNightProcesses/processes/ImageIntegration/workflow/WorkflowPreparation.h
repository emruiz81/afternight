// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file WorkflowPreparation.h
 * @brief What happens between an accepted execution request and the executor starting, without a window.
 *
 * The full process builds a manifest from a snapshot of its model, normalizes it for execution, reads the reusable
 * artifacts, plans a full or incremental run, validates it and derives the plan's decisions; the Lite process and the
 * other direct front ends build their manifest, normalize it, read the artifacts and validate it with no plan. Both
 * ran inside lambdas and anonymous helpers of their front ends, so nothing but the front end could perform them. They
 * live here so that the front ends and a measurement driver run the same preparation: the front ends on their worker
 * with their progress dialogs, a driver on its own thread with none.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/ExecutionPlanner.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class ExecutionPreparationSnapshot;
class IntegrationDataModel;

namespace ImageIntegration
{

/// A prepared execution: the manifest the executor runs, what it may reuse, and how long preparing it took.
struct PreparedWorkflowExecution
{
    WorkflowManifest manifest;
    std::shared_ptr<const ExecutionPreparationSnapshot> preparationSnapshot;
    /// The plan, when the preparation planned one; a direct preparation leaves it default.
    ExecutionPlan plan;
    WorkflowValidationResult validation;
    /// The plan's decisions, when the preparation planned one.
    std::vector<ExecutionPlanDecision> decisions;
    WorkflowPreparationTiming preparationTiming;
    QString errorMessage;
    bool success = false;
};

/// Whether the preparation should stop; asked between its steps.
using WorkflowPreparationCancelled = std::function<bool()>;

/// A step's share of the preparation, out of 100, and what it is doing, for a progress display.
using WorkflowPreparationProgress = std::function<void(int percent, const QString& text)>;

/// The execution options the full process reads from its stacking tab.
struct FullWorkflowOptions
{
    bool reprocessExisting = false;
    int maxParallelCores = 0;
    bool combineRgbImages = false;
};

/// The full process's manifest of every frame type and group of a model.
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowManifest buildFullWorkflowManifest(const IntegrationDataModel& model,
                                                                                 const FullWorkflowOptions& options);

/// Adjusts a manifest for the host it will run on: in-memory calibration off when memory is low, the calibration
/// stage placed or removed to match, and alignment ahead of a comet stack that consumes it.
///
/// @param availablePhysicalBytes the host's available memory; asked of the platform when absent.
AFTERNIGHTPROCESSES_API void
normalizeManifestForExecution(WorkflowManifest& manifest,
                              std::optional<std::uint64_t> availablePhysicalBytes = std::nullopt);

/// Empties every incremental selection, for a full run.
AFTERNIGHTPROCESSES_API void clearIncrementalSelections(WorkflowManifest& manifest);

/// Selects exactly the groups an incremental plan processes.
AFTERNIGHTPROCESSES_API void applyIncrementalPlanToManifest(WorkflowManifest& manifest, const ExecutionPlan& plan);

/// The full process's preparation: build, normalize, read the reusable artifacts, plan a full or incremental run -
/// the forced group always processed by an incremental one - validate, and derive the plan's decisions.
///
/// A failure or a cancellation is returned, not thrown: `success` is false and `errorMessage` says why, and the timing
/// covers what ran. The header reader reads a drizzle source's geometry and the frames' sizes the validation checks;
/// without one neither is read.
[[nodiscard]] AFTERNIGHTPROCESSES_API PreparedWorkflowExecution preparePlannedWorkflowExecution(
    const std::function<WorkflowManifest()>& buildManifest,
    const std::optional<IntegrationGroupKey>& forcedIntegrationGroup, const WorkflowPreparationCancelled& cancelled,
    const WorkflowPreparationProgress& progress = {}, const FrameHeaderReader& headerReader = {});

/// A direct front end's preparation: build, normalize, read the reusable artifacts and validate, with no plan.
///
/// A failure or a cancellation is returned as `preparePlannedWorkflowExecution` returns one.
[[nodiscard]] AFTERNIGHTPROCESSES_API PreparedWorkflowExecution prepareDirectWorkflowExecution(
    const std::function<WorkflowManifest()>& buildManifest, const WorkflowPreparationCancelled& cancelled,
    const FrameHeaderReader& headerReader = {});

/// What a front end does with a prepared execution.
enum class PreparedWorkflowDisposition
{
    Failed, ///< The preparation failed or was cancelled.
    NothingToProcess, ///< An incremental plan found nothing changed since the last successful run.
    Invalid, ///< The prepared manifest does not validate.
    NoGroups, ///< No stage has a group to process.
    Ready ///< The executor may start.
};

/// The full process's reading of a planned preparation, in the order it checks.
[[nodiscard]] AFTERNIGHTPROCESSES_API PreparedWorkflowDisposition
plannedDisposition(const PreparedWorkflowExecution& prepared);

/// The direct front ends' reading of a direct preparation, in the order they check.
[[nodiscard]] AFTERNIGHTPROCESSES_API PreparedWorkflowDisposition
directDisposition(const PreparedWorkflowExecution& prepared);

} // namespace ImageIntegration
