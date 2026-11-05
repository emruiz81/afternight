// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The integration workflow's alignment stage: every registered light of a registration group warped onto the plane
/// the group is aligned onto, written whole through the output transaction stamped with what it satisfies, and its
/// path recorded beside the frame's registration. A comet-aligned run's frames are warped onto the comet's plane the
/// same way. A group whose lights the stack warps as it reads them writes nothing here once their registrations are
/// confirmed.

#include "processes/ImageIntegration/workflow/WorkflowStageCompanions.h"
#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

class ExecutionPreparationSnapshot;

/// Warps each registered light onto its group's plane.
class AFTERNIGHTPROCESSES_API LightsAlignmentStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;
};

namespace ImageIntegration
{

/// Whether every registered light an integration group needs is current: written where the alignment stage writes it,
/// the file its registration record names, unchanged since, and its frame registered. Read from the preparation
/// snapshot when one is given and from the files as they are now otherwise.
///
/// @param manifest the run's manifest.
/// @param group the integration group.
/// @param snapshot the preparation snapshot, or nothing.
/// @param counters the run's counters, or nothing.
/// @return true when nothing of the group needs aligning again.
[[nodiscard]] bool registeredLightsCurrent(const WorkflowManifest& manifest,
                                           const WorkflowManifest::IntegrationGroup& group,
                                           const ExecutionPreparationSnapshot* snapshot,
                                           WorkflowExecutionCounterAccumulator* counters = nullptr);

} // namespace ImageIntegration
