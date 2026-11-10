// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

/**
 * @file CalibrationFacts.cpp
 * @brief The calibration masters of a run, and the groups of lights the user marked as calibrated already.
 */

namespace ImageIntegration::Reporting
{
namespace
{
/// The words a calibration kind is named by: in a result's `type`, in a slug and for a reader.
struct KindNames
{
    ReportCalibrationKind kind;
    const char* type;
};

[[nodiscard]] QString fileNameOf(const std::optional<FrameId>& master)
{
    return master.has_value() ? QFileInfo(master->canonicalPath()).fileName() : QString();
}

void fillStage(ReportBuild& build, const std::optional<WorkflowManifest::CalibrationStage>& stage,
               const KindNames& names, ReportModel& model)
{
    if (!stage.has_value())
    {
        return;
    }
    const WorkflowManifest& manifest = build.manifest();
    const QString type = QString::fromLatin1(names.type);
    for (const auto& group : stage->groups)
    {
        const QString key = group.groupKey.toString();
        const WorkflowExecutor::Result::CalibrationGroupResult* groupResult = nullptr;
        for (const auto& candidate : build.result().calibrationGroupResults)
        {
            if (candidate.groupKey == key && candidate.type.compare(type, Qt::CaseInsensitive) == 0)
            {
                groupResult = &candidate;
                break;
            }
        }

        ReportCalibrationMaster master;
        master.kind = names.kind;
        master.key = key;
        // A calibration group's key may be spelled as a group of lights is: the kind keeps their anchors apart.
        master.slug = build.slugs().slugFor(QStringLiteral("%1|%2").arg(type.toLower(), key));
        master.name = friendlyGroupKey(key);
        master.fileName = group.outputFilename;
        if (groupResult != nullptr && !groupResult->masterPath.trimmed().isEmpty())
        {
            master.file = groupResult->masterPath;
        }
        else if (group.existingMaster.has_value())
        {
            master.file = group.existingMaster->canonicalPath();
        }
        else if (!group.outputFilename.trimmed().isEmpty() && !manifest.outputDirectory.trimmed().isEmpty())
        {
            master.file = QDir(manifest.outputDirectory).filePath(group.outputFilename);
        }
        if (master.fileName.trimmed().isEmpty())
        {
            master.fileName = QFileInfo(master.file).fileName();
        }

        // A master the user supplied is its group's existing master; one the run found still valid leaves no result.
        master.state = group.existingMaster.has_value() ? ReportCalibrationState::Supplied
                       : groupResult == nullptr         ? ReportCalibrationState::Reused
                       : groupResult->success           ? ReportCalibrationState::Made
                                                        : ReportCalibrationState::Failed;
        master.frames = static_cast<int>(group.inputFiles.size());
        master.inputFiles.reserve(master.frames);
        for (const FrameId& input : group.inputFiles)
        {
            master.inputFiles.append(input.canonicalPath());
        }
        master.biasMaster = fileNameOf(group.biasMaster);
        master.darkMaster = fileNameOf(group.darkMaster);
        master.stackingSummary = stage_utils::describeStackingSettings(group.stacking);
        model.calibrationMasters.push_back(std::move(master));
    }
}
} // namespace

void fillCalibration(ReportBuild& build, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();
    fillStage(build, manifest.biasStage, KindNames{ReportCalibrationKind::Bias, "Bias"}, model);
    fillStage(build, manifest.darkStage, KindNames{ReportCalibrationKind::Dark, "Dark"}, model);
    fillStage(build, manifest.flatStage, KindNames{ReportCalibrationKind::Flat, "Flat"}, model);

    // A pre-calibrated group has no calibration entry, as a group no master matched has none; naming it tells the
    // two apart.
    for (const auto& group : manifest.preCalibratedLightGroups)
    {
        ReportLightGroup lights;
        lights.key = group.groupKey.toString();
        lights.name = friendlyGroupKey(lights.key);
        lights.lights = static_cast<int>(group.lightFiles.size());
        lights.preCalibrated = true;
        model.lightGroups.push_back(std::move(lights));
    }
}
} // namespace ImageIntegration::Reporting
