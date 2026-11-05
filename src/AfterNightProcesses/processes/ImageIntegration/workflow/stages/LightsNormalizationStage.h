// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file LightsNormalizationStage.h
 * @brief Workflow stage that measures a group's normalization coefficients before stacking.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

/// Measures the coefficients each integration group's stack brings its lights to the reference by.
///
/// The lights are measured where the stack reads them: the registered lights on the registration's plane when every
/// one is current, the registration inputs warped onto that plane in memory when they are not, a drizzled group's
/// lights warped onto its canvas's analysis plane, and a group with no registration as its lights are read. A global
/// group is one match per light against the combined reference frames - or, with overlap normalization, the matches
/// that best reconcile every overlapping pair; a local group is one background surface per light, its scale against the
/// reference, and the global match measured over the locally normalized light. The coefficients are published as the
/// group's coefficient document and its sidecar; a group whose sidecar still describes the inputs and settings is not
/// measured again.
class AFTERNIGHTPROCESSES_API LightsNormalizationStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;
};
