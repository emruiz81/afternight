// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file BiasStackingStage.h
 * @brief Workflow stage for stacking bias frames into master bias.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

/**
 * @class BiasStackingStage
 * @brief Stacks bias frames into master bias files.
 *
 * Processes each bias group defined in the manifest, producing
 * a master bias file per group using the configured stacking settings.
 */
class AFTERNIGHTPROCESSES_API BiasStackingStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;
};
