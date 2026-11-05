// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file DarkStackingStage.h
 * @brief Workflow stage for stacking dark frames into master dark.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

/**
 * @class DarkStackingStage
 * @brief Stacks dark frames into master dark files.
 *
 * Processes each dark group defined in the manifest, producing
 * a master dark file per group. May apply bias subtraction if
 * a matching master bias is available.
 */
class AFTERNIGHTPROCESSES_API DarkStackingStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;
};
