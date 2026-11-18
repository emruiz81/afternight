// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Where a run of the workflow stands among its stages, as a front end says it: the step it is at, the stage's name,
/// how far the stage has come and how far the whole run - "Step 5/9: Registration", 58 %.
///
/// A run goes through the stages its manifest names (`WorkflowManifest::stageSequence`) and reports, of the stage it
/// is at, how far that stage has come over its groups. How far the whole has come is the stages passed and the share
/// of the one on its way, each stage weighing what the run is expected to spend in it (`workflowStageWeights`, from
/// what the manifest asks of each stage), or the same as the others where nothing says. The time a run has left is the
/// time it has taken scaled by what is left, told only once the run can tell.

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/ProgressReporter.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

struct WorkflowManifest;

namespace ImageIntegration
{

/// Where a run stands among the stages it goes through.
struct WorkflowRunStanding
{
    /// The stage the run is at, counted from 1 among the stages it goes through; 0 while it is at none of them - it
    /// prepares, or reports a stage its sequence does not name.
    int step = 0;

    /// How many stages the run goes through.
    int steps = 0;

    /// The name of the stage the run is at, as a person reads it: "Registration"; empty while it reports none.
    QString stage;

    /// How far the stage has come, from 0 to 1.
    double stageFraction = 0.0;

    /// How far the whole run has come, from 0 to 1.
    double fraction = 0.0;

    [[nodiscard]] bool operator==(const WorkflowRunStanding&) const = default;
};

/// A stage's name as a run says it: "Registration", "Lights calibration"; empty for no stage.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString workflowStageTitle(WorkflowStage stage);

/// The name a stage has in a manifest's stage sequence: "LightsRegistration"; empty for no stage.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString workflowStageToken(WorkflowStage stage);

/// Where a stage is in a sequence of stage names, from 0: the entry that is its name, or else the first that holds
/// it; -1 when none does.
[[nodiscard]] AFTERNIGHTPROCESSES_API int workflowStageIndex(const QStringList& sequence, WorkflowStage stage);

/// What each stage of a manifest's sequence weighs in its run, in the sequence's order: the seconds the run is
/// expected to spend in it, from what the manifest asks of it, and never nothing - a stage with nothing counted still
/// takes its turn.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<double> workflowStageWeights(const WorkflowManifest& manifest);

/// Where a run stands, from the stages it goes through and what it reports.
///
/// @param sequence the names of the stages the run goes through, in order.
/// @param weights what each stage weighs, in the sequence's order; the stages weigh alike when there is not one for
///        each stage, or none is above nothing.
/// @param progress what the run reports: the stage it is at and how far that stage has come over its groups.
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowRunStanding workflowRunStanding(const QStringList& sequence,
                                                                              const std::vector<double>& weights,
                                                                              const WorkflowProgress& progress);

/// A standing in words: "Step 5/9: Registration"; empty while the run is at none of its stages.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString workflowRunStandingText(const WorkflowRunStanding& standing);

/// The time a run has left, in milliseconds, from the time it has taken and how far it has come: nothing until it has
/// run for ten seconds and come a twentieth of its way, and nothing once it is done.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<qint64> workflowRemainingMs(qint64 elapsedMs, double fraction);

} // namespace ImageIntegration
