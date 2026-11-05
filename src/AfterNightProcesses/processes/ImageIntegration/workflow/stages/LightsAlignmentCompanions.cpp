// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The alignment stage's answers to the planner: whether a group's registered lights are current.

#include "stdafx.hpp"

#include "processes/ImageIntegration/workflow/WorkflowStageCompanions.h"
#include "processes/ImageIntegration/workflow/stages/LightsAlignmentStage.h"

namespace ImageIntegration
{

bool lightsAlignmentOutputsReusable(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group)
{
    return registeredLightsCurrent(manifest, group, nullptr);
}

bool lightsAlignmentOutputsReusable(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group,
                                    const ExecutionPreparationSnapshot& snapshot)
{
    return registeredLightsCurrent(manifest, group, &snapshot);
}

} // namespace ImageIntegration
