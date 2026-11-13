// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stacking stage's answers to the planner, the alignment stage and the normalization stage: how a group's lights
/// reach the stack, and whether coefficients measured for that route are reusable.

#include "stdafx.hpp"

#include "processes/ImageIntegration/workflow/WorkflowStageCompanions.h"
#include "processes/ImageIntegration/workflow/stages/TransformOnReadRouting.h"

namespace ImageIntegration
{

TransformOnReadRouting planLightsTransformOnRead(const WorkflowManifest& manifest,
                                                 const WorkflowManifest::IntegrationGroup& group,
                                                 const ExecutionPreparationSnapshot*)
{
    return decideLightsTransformOnRead(manifest, group);
}

TransformOnReadRouting confirmLightsTransformOnRead(const WorkflowManifest& manifest,
                                                    const WorkflowManifest::IntegrationGroup& group,
                                                    const std::vector<RegisteredLightRecord>& lights,
                                                    const QString& referenceInputPath,
                                                    const ExecutionPreparationSnapshot*)
{
    return confirmLightsTransformOnReadFor(manifest, group, lights, referenceInputPath);
}

bool transformOnReadGlobalCoefficientsReusable(const WorkflowManifest&, const WorkflowManifest::IntegrationGroup&,
                                               const ExecutionPreparationSnapshot&)
{
    // The normalization stage measures a group on the registration's plane whichever way the stack reads its lights,
    // and decides its own reuse from its document's sidecar; the route keeps no coefficients of its own to reuse.
    return false;
}

} // namespace ImageIntegration
