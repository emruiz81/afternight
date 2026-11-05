// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Opening a run's stacked outputs and the maps beside them in views.

#include "stdafx.hpp"
#include "WorkflowResultOpener.h"

#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringList>

namespace ImageIntegration
{
namespace
{
void openRejectionMapsForOutput(const ArtifactTracker& tracker, const QDir& outputDir, const QFileInfo& outInfo,
                                const StackingSettings& stackingSettings)
{
    if (!outInfo.exists() || !stackingSettings.createRejectionMaps)
    {
        return;
    }

    auto tryOpenRejectionMap = [&](const RejectionMapSpec& spec) {
        const QString mapPath = outputDir.filePath(spec.filename);
        if (!isStackingAuxiliaryOutputCurrent(tracker, outInfo.absoluteFilePath(), mapPath))
        {
            return;
        }

        // Read on the thread pool and shown on the GUI thread by the product's file workflow, as the stack is.
        try
        {
            process_utils::openRecentSingleFile(mapPath);
        }
        catch (...)
        {
            LOG_WARNING("Failed to open rejection map '{}'", mapPath.toStdString());
        }
    };

    const auto specs = rejectionMapsForOutput(outInfo, stackingSettings.mergeRejectionMaps);
    for (const auto& spec : specs)
    {
        tryOpenRejectionMap(spec);
    }
}

void openQualityMapsForOutput(const ArtifactTracker& tracker, const QDir& outputDir, const QFileInfo& outInfo,
                              const StackingSettings& stackingSettings)
{
    if (!outInfo.exists() || !stackingSettings.createQualityMaps)
    {
        return;
    }

    auto tryOpenQualityMap = [&](const StackingAuxiliaryOutputSpec& spec) {
        const QString mapPath = outputDir.filePath(spec.filename);
        if (!isStackingAuxiliaryOutputCurrent(tracker, outInfo.absoluteFilePath(), mapPath))
        {
            return;
        }

        // Read on the thread pool and shown on the GUI thread by the product's file workflow, as the stack is.
        try
        {
            process_utils::openRecentSingleFile(mapPath);
        }
        catch (...)
        {
            LOG_WARNING("Failed to open quality map '{}'", mapPath.toStdString());
        }
    };

    const auto specs = qualityMapsForOutput(outInfo, true);
    for (const auto& spec : specs)
    {
        tryOpenQualityMap(spec);
    }
}
} // namespace

void openIntegratedOutputFiles(const QStringList& outputFiles, const bool openIntegratedFiles)
{
    if (!openIntegratedFiles)
    {
        return;
    }

    for (const QString& outputPath : outputFiles)
    {
        if (!QFileInfo::exists(outputPath))
        {
            continue;
        }

        try
        {
            process_utils::openRecentSingleFile(outputPath);
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR("Failed to open integration result '{}': {}", outputPath.toStdString(), ex.what());
        }
        catch (...)
        {
            LOG_ERROR("Failed to open integration result '{}'", outputPath.toStdString());
        }
    }
}

void openIntegratedOutputsForManifest(const QDir& outputDir,
                                      const std::vector<WorkflowManifest::IntegrationGroup>& integrationGroups,
                                      bool openIntegratedFiles, const QSet<QString>& skippedIntegrationGroupKeys,
                                      const QStringList& extraOutputFiles)
{
    // A map is opened only when the artifact records say it was written with the master beside it.
    const ArtifactTracker tracker(outputDir.absolutePath());
    for (const auto& group : integrationGroups)
    {
        if (skippedIntegrationGroupKeys.contains(group.groupKey.toString()))
        {
            continue;
        }

        const QString fullPath = outputDir.filePath(group.outputFilename);
        ImageViewControl* groupNeighbour = nullptr;

        if (openIntegratedFiles && QFileInfo::exists(fullPath))
        {
            try
            {
                process_utils::openRecentSingleFile(fullPath);
                if (auto* activeView = process_utils::currentActiveViewControl())
                {
                    groupNeighbour = activeView;
                }
            }
            catch (const std::exception& ex)
            {
                LOG_ERROR("Failed to open integration result '{}': {}", fullPath.toStdString(), ex.what());
            }
            catch (...)
            {
                LOG_ERROR("Failed to open integration result '{}'", fullPath.toStdString());
            }
        }

        openRejectionMapsForOutput(tracker, outputDir, QFileInfo(fullPath), group.stacking);
        openQualityMapsForOutput(tracker, outputDir, QFileInfo(fullPath), group.stacking);
        if (groupNeighbour)
        {
            if (auto* viewRegistry = process_utils::currentViewRegistry())
            {
                viewRegistry->setActiveImageViewControl(groupNeighbour);
            }
        }
    }

    openIntegratedOutputFiles(extraOutputFiles, openIntegratedFiles);
}

} // namespace ImageIntegration
