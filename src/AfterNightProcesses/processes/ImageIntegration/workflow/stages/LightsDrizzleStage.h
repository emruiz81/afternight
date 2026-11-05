// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file LightsDrizzleStage.h
 * @brief Workflow stage that writes a drizzled group's map descriptors.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

/// Writes the documents a drizzled group's stack maps its lights by.
///
/// For each drizzled group: where every light its registration placed lands on the drizzle canvas the group's canvas
/// mode plans, as one descriptor document per light - its geometry and the engine's byte-exact identity of it - and,
/// when the group keeps dense checkpoints, the map's coordinates beside it; and the group sidecar listing them. A
/// group whose sidecar and descriptors still describe the current registration, inputs and settings writes nothing.
class AFTERNIGHTPROCESSES_API LightsDrizzleStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;
};
