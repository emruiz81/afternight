// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file FlatStackingStage.h
 * @brief Workflow stage for stacking flat frames into master flat.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

/**
 * @class FlatStackingStage
 * @brief Stacks flat frames into master flat files.
 *
 * Processes each flat group defined in the manifest, producing
 * a master flat file per group. May apply bias/dark subtraction
 * if matching masters are available.
 */
class AFTERNIGHTPROCESSES_API FlatStackingStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;
};
