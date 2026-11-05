// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The dark stage: each dark group stacked into its master, or an existing or current master used.

#include "stdafx.hpp"
#include "DarkStackingStage.h"

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
    if (!manifest.darkStage.has_value())
    {
        return groups;
    }

    const auto& stage = *manifest.darkStage;
    if (manifest.executionMode != ExecutionMode::Incremental)
    {
        groups.reserve(stage.groups.size());
        for (const auto& group : stage.groups)
        {
            groups.push_back(&group);
        }
        return groups;
    }
    if (manifest.incrementalDarkGroups.empty())
    {
        return groups;
    }

    groups.reserve(stage.groups.size());
    for (const auto& group : stage.groups)
    {
        const auto it =
            std::find(manifest.incrementalDarkGroups.begin(), manifest.incrementalDarkGroups.end(), group.groupKey);
        if (it != manifest.incrementalDarkGroups.end())
        {
            groups.push_back(&group);
        }
    }

    return groups;
}
} // namespace

QString DarkStackingStage::name() const
{
    return QStringLiteral("DarkStacking");
}

WorkflowStage DarkStackingStage::stageId() const
{
    return WorkflowStage::Dark;
}

bool DarkStackingStage::shouldSkip(const WorkflowManifest& manifest) const
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

QString DarkStackingStage::skipReason(const WorkflowManifest& manifest) const
{
    const auto groups = groupsToProcess(manifest);
    if (groups.empty())
    {
        if (manifest.executionMode == ExecutionMode::Incremental && !manifest.incrementalDarkGroups.empty())
        {
            return QStringLiteral("Incremental dark selection did not match current groups.");
        }
        return QStringLiteral("No dark groups to process.");
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

StageResult DarkStackingStage::execute(const WorkflowManifest& manifest, CancellationToken& token,
                                       ProgressReporter& reporter)
{
    stage_utils::CalibrationStageRun run;
    run.stage = stageId();
    run.stageName = name();
    run.frameType = QStringLiteral("dark");
    run.groups = groupsToProcess(manifest);
    // A master dark is stacked from its frames as they are, holding their bias, as a supplied master dark does.
    run.appliesBias = false;
    run.appliesDark = false;
    run.normalizes = false;
    return stage_utils::runCalibrationStage(run, manifest, stageServices(), executionCounterAccumulator(),
                                            artifactPublicationLedger().get(), token, reporter);
}
