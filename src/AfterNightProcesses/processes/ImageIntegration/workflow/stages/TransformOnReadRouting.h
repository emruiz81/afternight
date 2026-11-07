// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// How a registered group's lights reach the stack: as the registered images the alignment stage writes, or as their
/// registration inputs warped onto the registration's plane as the stack reads them, with no registered image written.
///
/// The choice does not depend on what a streaming stack could not do while warping - a local or overlap normalization,
/// a weighting, a rejection restack, an interpolation other than bilinear, any canvas but the reference's, any method
/// but an average. The engine's stack reads a frame warped in memory exactly as it reads one from disk, and the
/// normalization stage measures either on the registration's plane, so none of those is a reason any more. What is left
/// are the reasons that belong to the workflow: a drizzle and a comet stack place their lights themselves, a reprocess
/// keeps the registered images it may resume from, and a group that keeps its registered images asks for them to be
/// written. An incremental run reads a group's lights as a full run does, so a run that only stacks again reads them as
/// the run before it did and finds that run's normalization coefficients current.
///
/// The planner, the alignment stage and the normalization stage ask through the stage companions, which are defined in
/// `TransformOnReadCompanions.cpp` beside this unit.

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowStageCompanions.h"

#include <QString>

#include <vector>

namespace ImageIntegration
{

/// The route a group's settings choose, before its registration is known.
///
/// @param manifest the run's manifest.
/// @param group the integration group.
/// @return the route and its reason.
[[nodiscard]] AFTERNIGHTPROCESSES_API TransformOnReadRouting
decideLightsTransformOnRead(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group);

/// The route once the group's registration is known: warped as read only when the settings choose it and every light
/// has a transform record and a file to be read from, the reference among them.
///
/// @param manifest the run's manifest.
/// @param group the integration group.
/// @param lights each light and its registration.
/// @param referenceInputPath the file the group's reference is read from.
/// @return the route and its reason.
[[nodiscard]] TransformOnReadRouting confirmLightsTransformOnReadFor(const WorkflowManifest& manifest,
                                                                     const WorkflowManifest::IntegrationGroup& group,
                                                                     const std::vector<RegisteredLightRecord>& lights,
                                                                     const QString& referenceInputPath);

} // namespace ImageIntegration
