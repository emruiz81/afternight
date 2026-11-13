// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The decisions about a group's products that belong to the stage making them, which the planner asks and does not
/// make itself.
///
/// The planner decides what a run recomputes and what it reuses; whether a group's lights reach the stack as images the
/// alignment stage writes or warped as they are read, whether a group's cached coefficients may be read that way, and
/// whether a group's registered lights are current are each the stacking, normalization or alignment stage's own
/// knowledge. The planner reaches them through these functions, which are defined beside each stage, so it compiles and
/// is tested without any stage, and a stage that moves changes its own answer without the planner changing. Each answer
/// is a workflow record: no engine type crosses here.

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QString>

#include <vector>

class ExecutionPreparationSnapshot;

namespace ImageIntegration
{

/// How a group's lights reach the stack.
enum class RegisteredInputRoute
{
    /// As registered images the alignment stage writes.
    MaterializedRegisteredImages,
    /// Warped as they are read, with no registered images written.
    TransformOnRead
};

/// The stacking stage's reading of how a group's lights reach the stack, and why.
struct TransformOnReadRouting
{
    RegisteredInputRoute route = RegisteredInputRoute::MaterializedRegisteredImages;

    /// A stable token naming the reason, for a report and a log.
    QString reason;

    /// The reason in more words, when there are more to say.
    QString detail;

    [[nodiscard]] bool usesTransformOnRead() const noexcept
    {
        return route == RegisteredInputRoute::TransformOnRead;
    }
};

/// The planning reading of how a group's lights reach the stack: decided from the settings and, where the route depends
/// on cached coefficients, from them as the snapshot pins them, the registration data left for the stage to validate
/// when it runs. Defined beside the stacking stage.
///
/// @param manifest the run's manifest.
/// @param group the integration group.
/// @param snapshot the preparation snapshot, or nothing to read the files as they are now.
/// @return the route and its reason.
[[nodiscard]] TransformOnReadRouting planLightsTransformOnRead(const WorkflowManifest& manifest,
                                                               const WorkflowManifest::IntegrationGroup& group,
                                                               const ExecutionPreparationSnapshot* snapshot);

/// One light of a group as the alignment stage found its registration: the frame, the file it is read from and the
/// transform record the registration stage wrote for it.
struct RegisteredLightRecord
{
    FrameId frame;

    /// The file the light is warped from: its chromatic aberration output, its calibrated light or the light itself.
    QString inputPath;

    /// The schema 5 transform record; empty when the light was not registered.
    QString transformJson;
};

/// The stacking stage's reading of how a group's lights reach the stack once their registration is known: whether the
/// lights the planning reading would warp as they are read are each registered, and their records describe the files
/// the stack would read. Defined beside the stacking stage.
///
/// @param manifest the run's manifest.
/// @param group the integration group.
/// @param lights each light of the group and its registration.
/// @param referenceInputPath the file the group's reference is read from.
/// @param snapshot the preparation snapshot, or nothing to read the files as they are now.
/// @return the route and its reason.
[[nodiscard]] TransformOnReadRouting confirmLightsTransformOnRead(const WorkflowManifest& manifest,
                                                                  const WorkflowManifest::IntegrationGroup& group,
                                                                  const std::vector<RegisteredLightRecord>& lights,
                                                                  const QString& referenceInputPath,
                                                                  const ExecutionPreparationSnapshot* snapshot);

/// Whether the standard-global coefficients a group would read as its lights are warped are cached and valid for this
/// run, which makes its normalization reusable. Defined beside the normalization stage.
///
/// @param manifest the run's manifest.
/// @param group the integration group.
/// @param snapshot the preparation snapshot.
/// @return true when the cached coefficients may be reused.
[[nodiscard]] bool transformOnReadGlobalCoefficientsReusable(const WorkflowManifest& manifest,
                                                             const WorkflowManifest::IntegrationGroup& group,
                                                             const ExecutionPreparationSnapshot& snapshot);

/// Whether a group's registered lights are current, read from the files as they are now. Defined beside the alignment
/// stage.
///
/// @param manifest the run's manifest.
/// @param group the integration group.
/// @return true when every registered light the group needs is current.
[[nodiscard]] bool lightsAlignmentOutputsReusable(const WorkflowManifest& manifest,
                                                  const WorkflowManifest::IntegrationGroup& group);

/// Whether a group's registered lights are current, read from the preparation snapshot. Defined beside the alignment
/// stage.
///
/// @param manifest the run's manifest.
/// @param group the integration group.
/// @param snapshot the preparation snapshot.
/// @return true when every registered light the group needs is current.
[[nodiscard]] bool lightsAlignmentOutputsReusable(const WorkflowManifest& manifest,
                                                  const WorkflowManifest::IntegrationGroup& group,
                                                  const ExecutionPreparationSnapshot& snapshot);

} // namespace ImageIntegration
