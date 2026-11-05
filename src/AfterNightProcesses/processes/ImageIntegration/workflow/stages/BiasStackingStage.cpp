// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The bias stage: each bias group stacked into its master, or an existing or current master used.

#include "stdafx.hpp"
#include "BiasStackingStage.h"

#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/CalibrationStackInputs.h"

#include <QDir>

#include <algorithm>
#include <optional>

namespace
{
std::vector<const WorkflowManifest::CalibrationStage::Group*> groupsToProcess(const WorkflowManifest& manifest)
{
    std::vector<const WorkflowManifest::CalibrationStage::Group*> groups;
    if (!manifest.biasStage.has_value())
    {
        return groups;
    }

    const auto& stage = *manifest.biasStage;
    if (manifest.executionMode != ExecutionMode::Incremental)
    {
        groups.reserve(stage.groups.size());
        for (const auto& group : stage.groups)
        {
            groups.push_back(&group);
        }
        return groups;
    }
    if (manifest.incrementalBiasGroups.empty())
    {
        return groups;
    }

    groups.reserve(stage.groups.size());
    for (const auto& group : stage.groups)
    {
        const auto it =
            std::find(manifest.incrementalBiasGroups.begin(), manifest.incrementalBiasGroups.end(), group.groupKey);
        if (it != manifest.incrementalBiasGroups.end())
        {
            groups.push_back(&group);
        }
    }
    return groups;
}
} // namespace

QString BiasStackingStage::name() const
{
    return QStringLiteral("BiasStacking");
}

WorkflowStage BiasStackingStage::stageId() const
{
    return WorkflowStage::Bias;
}

bool BiasStackingStage::shouldSkip(const WorkflowManifest& manifest) const
{
    const auto groups = groupsToProcess(manifest);
    if (groups.empty())
    {
        return true;
    }

    if (manifest.reprocessExisting)
    {
        return false;
    }

    const auto* snapshot = executionPreparationSnapshot();
    std::optional<ArtifactTracker> tracker;
    if (snapshot == nullptr)
        tracker.emplace(manifest.outputDirectory, executionCounterAccumulator(),
                        stageServices() != nullptr ? sessionArtifactStampReader(stageServices()->session())
                                                   : ArtifactStampReader{});
    for (const auto* group_ptr : groups)
    {
        const auto& group = *group_ptr;
        if (group.existingMaster.has_value())
        {
            // User provided an existing master; stacking is not required for this group.
            continue;
        }

        const QString output_path = QDir(manifest.outputDirectory).filePath(group.outputFilename);
        const QString hash = computeCalibrationGroupHash(manifest, group, snapshot);
        const bool valid = snapshot != nullptr ? snapshot->isArtifactValid(output_path, hash,
                                                                           PreparationArtifactValidation::StrictContent)
                                               : tracker->isArtifactValid(output_path, hash);
        if (!valid)
        {
            return false;
        }
    }

    return true;
}

QString BiasStackingStage::skipReason(const WorkflowManifest& manifest) const
{
    const auto groups = groupsToProcess(manifest);
    if (groups.empty())
    {
        if (manifest.executionMode == ExecutionMode::Incremental && !manifest.incrementalBiasGroups.empty())
        {
            return QStringLiteral("Incremental bias selection did not match current groups.");
        }
        return QStringLiteral("No bias groups to process.");
    }

    if (manifest.reprocessExisting)
    {
        return QStringLiteral("Reprocess existing enabled.");
    }

    const auto* snapshot = executionPreparationSnapshot();
    std::optional<ArtifactTracker> tracker;
    if (snapshot == nullptr)
        tracker.emplace(manifest.outputDirectory, executionCounterAccumulator(),
                        stageServices() != nullptr ? sessionArtifactStampReader(stageServices()->session())
                                                   : ArtifactStampReader{});
    bool all_existing = true;
    bool all_valid = true;
    for (const auto* group_ptr : groups)
    {
        const auto& group = *group_ptr;
        if (!group.existingMaster.has_value())
        {
            all_existing = false;
            const QString output_path = QDir(manifest.outputDirectory).filePath(group.outputFilename);
            const QString hash = computeCalibrationGroupHash(manifest, group, snapshot);
            const bool valid =
                snapshot != nullptr
                    ? snapshot->isArtifactValid(output_path, hash, PreparationArtifactValidation::StrictContent)
                    : tracker->isArtifactValid(output_path, hash);
            if (!valid)
            {
                all_valid = false;
            }
        }
    }

    if (all_existing)
    {
        return QStringLiteral("All groups use existing masters.");
    }
    if (all_valid)
    {
        return QStringLiteral("All outputs up-to-date or existing masters.");
    }

    return QStringLiteral("No work required.");
}

StageResult BiasStackingStage::execute(const WorkflowManifest& manifest, CancellationToken& token,
                                       ProgressReporter& reporter)
{
    stage_utils::CalibrationStageRun run;
    run.stage = stageId();
    run.stageName = name();
    run.frameType = QStringLiteral("bias");
    run.groups = groupsToProcess(manifest);
    run.appliesBias = false;
    run.appliesDark = false;
    run.normalizes = false;
    return stage_utils::runCalibrationStage(run, manifest, stageServices(), executionCounterAccumulator(),
                                            artifactPublicationLedger().get(), token, reporter);
}
