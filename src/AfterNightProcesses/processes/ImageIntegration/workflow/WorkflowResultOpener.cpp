// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Opening a run's stacked outputs and the maps beside them in views of the run's.

#include "stdafx.hpp"
#include "WorkflowResultOpener.h"

#include "processes/ProcessCommonUtils.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringList>

#include <cstddef>
#include <memory>
#include <utility>

namespace ImageIntegration
{
namespace
{
/// The maps the artifact records say were written with a master, rejection maps first.
void appendMapsOfOutput(const ArtifactTracker& tracker, const QDir& outputDir, const QFileInfo& outInfo,
                        const StackingSettings& stackingSettings, QStringList& set)
{
    if (!outInfo.exists())
    {
        return;
    }
    const auto appendWhenCurrent = [&](const QString& filename) {
        const QFileInfo map(outputDir.filePath(filename));
        if (isStackingAuxiliaryOutputCurrent(tracker, outInfo.absoluteFilePath(), map.absoluteFilePath()))
        {
            set.append(map.absoluteFilePath());
        }
    };
    if (stackingSettings.createRejectionMaps)
    {
        for (const auto& spec : rejectionMapsForOutput(outInfo, stackingSettings.mergeRejectionMaps))
        {
            appendWhenCurrent(spec.filename);
        }
    }
    if (stackingSettings.createQualityMaps)
    {
        for (const auto& spec : qualityMapsForOutput(outInfo, true))
        {
            appendWhenCurrent(spec.filename);
        }
    }
}

/// Opens the sets one after another: each set's views are docked beside its first, and the next set opens once the
/// open before it has ended.
void openInTurn(const std::shared_ptr<const std::vector<QStringList>>& sets, const std::size_t next)
{
    if (next >= sets->size())
    {
        return;
    }
    process_utils::openRunResults((*sets)[next], [sets, next]() { openInTurn(sets, next + 1u); });
}

void openSets(std::vector<QStringList> sets)
{
    if (!sets.empty())
    {
        openInTurn(std::make_shared<const std::vector<QStringList>>(std::move(sets)), 0u);
    }
}

/// Each file that exists, as a set of its own.
void appendEachExisting(const QStringList& files, std::vector<QStringList>& sets)
{
    for (const QString& file : files)
    {
        if (QFileInfo::exists(file))
        {
            sets.push_back(QStringList{file});
        }
    }
}
} // namespace

std::vector<QStringList> integratedOutputSets(const QDir& outputDir,
                                              const std::vector<WorkflowManifest::IntegrationGroup>& integrationGroups,
                                              const bool openIntegratedFiles,
                                              const QSet<QString>& skippedIntegrationGroupKeys,
                                              const QStringList& extraOutputFiles)
{
    // A map is opened only when the artifact records say it was written with the master beside it.
    const ArtifactTracker tracker(outputDir.absolutePath());
    std::vector<QStringList> sets;
    for (const auto& group : integrationGroups)
    {
        if (skippedIntegrationGroupKeys.contains(group.groupKey.toString()))
        {
            continue;
        }
        const QFileInfo master(outputDir.filePath(group.outputFilename));
        QStringList set;
        if (openIntegratedFiles && master.exists())
        {
            set.append(master.absoluteFilePath());
        }
        appendMapsOfOutput(tracker, outputDir, master, group.stacking, set);
        if (!set.isEmpty())
        {
            sets.push_back(std::move(set));
        }
    }
    if (openIntegratedFiles)
    {
        appendEachExisting(extraOutputFiles, sets);
    }
    return sets;
}

void openIntegratedOutputFiles(const QStringList& outputFiles, const bool openIntegratedFiles)
{
    if (!openIntegratedFiles)
    {
        return;
    }
    std::vector<QStringList> sets;
    appendEachExisting(outputFiles, sets);
    openSets(std::move(sets));
}

void openIntegratedOutputsForManifest(const QDir& outputDir,
                                      const std::vector<WorkflowManifest::IntegrationGroup>& integrationGroups,
                                      const bool openIntegratedFiles, const QSet<QString>& skippedIntegrationGroupKeys,
                                      const QStringList& extraOutputFiles)
{
    openSets(integratedOutputSets(outputDir, integrationGroups, openIntegratedFiles, skippedIntegrationGroupKeys,
                                  extraOutputFiles));
}

} // namespace ImageIntegration
