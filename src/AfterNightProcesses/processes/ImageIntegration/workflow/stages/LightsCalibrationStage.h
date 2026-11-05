// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file LightsCalibrationStage.h
 * @brief Workflow stage for calibrating light frames.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

/**
 * @class LightsCalibrationStage
 * @brief Applies calibration (bias, dark, flat) to light frames.
 *
 * For each integration group, applies the matched master calibration
 * frames to produce calibrated light frames ready for registration.
 */
class AFTERNIGHTPROCESSES_API LightsCalibrationStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;
};
