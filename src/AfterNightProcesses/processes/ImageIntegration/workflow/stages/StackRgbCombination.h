// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file StackRgbCombination.h
 * @brief Combining a run's R/G/B masters into one RGB master after the per-group stacks.
 *
 * combineEligibleRgbMasters() is the single entry point: it decides which completed groups form an eligible R/G/B set,
 * reads the three mono masters through the run's registry, matches the other two to the best one by the linear match
 * the channel composition also uses (`processes/core/LinearChannelMatch.h`, whose limits are its only copy), composes
 * them, and publishes the colour master once, whole and stamped with the identity of the three masters it was made
 * from. A set that cannot be combined is reported without failing the stage.
 */
#pragma once

#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/CancellationToken.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/LightsStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/StageResult.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

class ArtifactTracker;

namespace ImageIntegration
{
class WorkflowStageServices;
} // namespace ImageIntegration

namespace ImageIntegration::detail
{

/// The fingerprint a colour master satisfies: the three masters it was made from, each as its file is now, and which
/// of them the others were matched to.
[[nodiscard]] QString combinedRgbArtifactHash(const QStringList& channelPaths, int referenceChannel);

/// Combines every eligible red, green and blue set of masters into a colour master.
///
/// @return true when the run was stopped.
[[nodiscard]] bool combineEligibleRgbMasters(const WorkflowManifest& manifest, const WorkflowStageServices& services,
                                             CancellationToken& token, ProgressReporter& reporter,
                                             std::vector<IntegrationGroupResult>& completed_group_results,
                                             const QSet<QString>& processed_group_keys,
                                             IntegrationErrorReport& error_report, QStringList& stage_warnings,
                                             bool& any_group_succeeded, const StackGroupMonotonicClock& monotonic_clock,
                                             WorkflowExecutionCounterAccumulator* counters, ArtifactTracker& tracker,
                                             const StackPreviewGenerator& preview_generator);

} // namespace ImageIntegration::detail
