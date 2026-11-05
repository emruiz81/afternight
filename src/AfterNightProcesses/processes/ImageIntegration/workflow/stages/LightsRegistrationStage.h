// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The integration workflow's registration stage: every light of a registration group read as the registration reads
/// it, its chromatic aberration corrected when asked, its stars detected, and each frame registered onto the group's
/// reference - by its stars, directly or through a light already registered near it when the reference holds too few
/// of them, by phase correlation, by the control points a person placed, or along a comet's track - with a frame the
/// chosen method could not register recovered from its world-coordinate solution where one exists.
/// What it records is the registration database's: each frame's figures and transform, the star detections a later run
/// reuses, the Gaia sources a frame's stars matched, the comet products, and the group's state with the solution of the
/// plane the group is aligned onto.
///
/// The stage runs only inside a workflow run, whose services lend it the session: every frame is read through the
/// run's codec registry in an operation the session admits at the run's width.

#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

#include <memory>

namespace an::ui
{
class PlateSolveService;
}

/// Detects the stars of every light of a registration group and registers each frame onto the group's reference.
class AFTERNIGHTPROCESSES_API LightsRegistrationStage : public IWorkflowStage
{
public:
    /// A stage that solves an unsolved frame with the plate solve it is lent, or with one of its own when it is lent
    /// none and a solve is asked for.
    ///
    /// @param plateSolveService the plate solve, shared; may be empty.
    explicit LightsRegistrationStage(std::shared_ptr<an::ui::PlateSolveService> plateSolveService = {});

    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;

private:
    std::shared_ptr<an::ui::PlateSolveService> m_plateSolveService;
};
