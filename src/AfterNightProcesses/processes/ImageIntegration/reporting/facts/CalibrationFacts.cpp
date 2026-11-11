// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"

#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include <AfterNightEngine/IO/CodecMetadataProjection.h>

#include <QDir>
#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>
#include <tuple>
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

/// What tells a later run a calibration master's file is the same one - its size and when it was last changed - and
/// the exposure its header states.
void fillFile(ReportBuild& build, ReportCalibrationMaster& master)
{
    const QString file = build.resolvePath(master.file);
    IReportFiles* files = build.sources().files;
    if (file.isEmpty() || files == nullptr || !files->exists(file))
    {
        return;
    }
    master.bytes = files->size(file);
    if (const std::optional<QDateTime> modified = files->lastModified(file); modified.has_value())
    {
        master.modifiedAt = modified->toUTC();
    }
    if (const std::optional<ReportHeader> header = build.header(file); header.has_value())
    {
        const std::optional<double> exposure =
            an::projectAcquisition(header->metadata.codec()).acquisition.exposureSeconds;
        if (exposure.has_value() && *exposure > 0.0)
        {
            master.exposureSeconds = exposure;
        }
    }
}

/// A calibration master's level and noise per channel: the ones the latest earlier run of the folder stated for the
/// same file, when its report is kept; else what one read of the file measures.
void fillLevels(ReportBuild& build, ReportCalibrationMaster& master)
{
    if (master.state == ReportCalibrationState::Failed)
    {
        return;
    }
    if (master.bytes.has_value() && master.modifiedAt.has_value())
    {
        for (const StoredRun& run : build.earlierRuns())
        {
            const auto same = std::find_if(run.model.calibrationMasters.begin(), run.model.calibrationMasters.end(),
                                           [&master](const ReportCalibrationMaster& candidate) {
                                               return candidate.kind == master.kind && candidate.file == master.file &&
                                                      candidate.bytes == master.bytes &&
                                                      candidate.modifiedAt.has_value() &&
                                                      candidate.modifiedAt->toMSecsSinceEpoch() ==
                                                          master.modifiedAt->toMSecsSinceEpoch() &&
                                                      !candidate.level.empty();
                                           });
            if (same == run.model.calibrationMasters.end())
            {
                continue;
            }
            master.level = same->level;
            master.noise = same->noise;
            master.measuredFraction = same->measuredFraction;
            master.measuredByRun = same->measuredByRun.isEmpty() ? run.runId : same->measuredByRun;
            return;
        }
    }
    const QString what = reportText("the master %1 %2")
                             .arg(master.kind == ReportCalibrationKind::Bias   ? reportText("bias")
                                  : master.kind == ReportCalibrationKind::Dark ? reportText("dark")
                                                                               : reportText("flat"),
                                  master.name.isEmpty() ? master.fileName : master.name);
    std::optional<ReportImageLevels> levels = build.levels(build.resolvePath(master.file), what);
    if (!levels.has_value())
    {
        return;
    }
    master.level = std::move(levels->level);
    master.noise = std::move(levels->noise);
    master.measuredFraction = levels->measuredFraction;
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
    const std::vector<ImageIntegration::ArchivedCalibrationPreview> previews =
        ImageIntegration::archivedCalibrationPreviews(manifest);
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

        // A master the user supplied is its group's existing master. One the run found still valid leaves no result
        // when its whole stage was skipped, and a result that says so when the stage ran for another group.
        master.state = group.existingMaster.has_value()                ? ReportCalibrationState::Supplied
                       : groupResult == nullptr || groupResult->reused ? ReportCalibrationState::Reused
                       : groupResult->success                          ? ReportCalibrationState::Made
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
        fillFile(build, master);
        fillLevels(build, master);
        // The picture the archive keeps of the master, named as the archive names it.
        for (const ImageIntegration::ArchivedCalibrationPreview& preview : previews)
        {
            if (preview.groupKey == key && preview.type.compare(type, Qt::CaseInsensitive) == 0)
            {
                const QString picture = QStringLiteral("report_previews/calibration/%1").arg(preview.fileName);
                if (build.exists(picture))
                {
                    master.preview = picture;
                }
                break;
            }
        }
        model.calibrationMasters.push_back(std::move(master));
    }
}

/// A path as two files are told to be the same by: resolved against the run's folder, absolute and clean.
[[nodiscard]] QString sameFileKey(const ReportBuild& build, const QString& path)
{
    const QString resolved = build.resolvePath(path);
    return resolved.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(resolved).absoluteFilePath());
}

/// The lights of one master that share a calibration.
struct SharedCalibration
{
    QString bias;
    QString dark;
    QString flat;
    WorkflowManifest::CalibrationSettings settings;
    bool preCalibrated = false;
    bool calibrated = false;
    int lights = 0;

    [[nodiscard]] bool sameAs(const SharedCalibration& other) const
    {
        const auto fields = [](const SharedCalibration& shared) {
            return std::tie(shared.bias, shared.dark, shared.flat, shared.preCalibrated, shared.calibrated,
                            shared.settings.cosmeticCorrection, shared.settings.sigmaHot, shared.settings.sigmaCold,
                            shared.settings.darkOptimization, shared.settings.darkMultiplicationFactor,
                            shared.settings.flatAutoLevel, shared.settings.pedestal,
                            shared.settings.subtractBiasFromDark);
        };
        return fields(*this) == fields(other);
    }
};
} // namespace

void fillCalibration(ReportBuild& build, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();
    fillStage(build, manifest.biasStage, KindNames{ReportCalibrationKind::Bias, "Bias"}, model);
    fillStage(build, manifest.darkStage, KindNames{ReportCalibrationKind::Dark, "Dark"}, model);
    fillStage(build, manifest.flatStage, KindNames{ReportCalibrationKind::Flat, "Flat"}, model);
}

void fillLineage(ReportBuild& build, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();

    // The anchor of each calibration master the run states, by its file.
    QHash<QString, std::size_t> calibrationByFile;
    for (std::size_t i = 0; i < model.calibrationMasters.size(); ++i)
    {
        const QString key = sameFileKey(build, model.calibrationMasters[i].file);
        if (!key.isEmpty())
        {
            calibrationByFile.insert(key, i);
        }
    }
    const auto calibrationOf = [&](const QString& path) -> ReportCalibrationMaster* {
        const auto found = calibrationByFile.constFind(sameFileKey(build, path));
        return found == calibrationByFile.cend() ? nullptr : &model.calibrationMasters[*found];
    };
    const auto slugOf = [&](const QString& path) {
        const ReportCalibrationMaster* master = path.isEmpty() ? nullptr : calibrationOf(path);
        return master != nullptr ? master->slug : QString();
    };

    // The masters a flat's frames are calibrated with.
    if (manifest.flatStage.has_value())
    {
        for (const auto& group : manifest.flatStage->groups)
        {
            const QString key = group.groupKey.toString();
            for (ReportCalibrationMaster& flat : model.calibrationMasters)
            {
                if (flat.kind != ReportCalibrationKind::Flat || flat.key != key)
                {
                    continue;
                }
                flat.biasMasterSlug =
                    group.biasMaster.has_value() ? slugOf(group.biasMaster->canonicalPath()) : QString();
                flat.darkMasterSlug =
                    group.darkMaster.has_value() ? slugOf(group.darkMaster->canonicalPath()) : QString();
                break;
            }
        }
    }

    QHash<QString, const WorkflowManifest::LightsCalibrationParams*> calibrationByLight;
    calibrationByLight.reserve(static_cast<qsizetype>(manifest.lightsCalibration.size()));
    for (const auto& calibration : manifest.lightsCalibration)
    {
        calibrationByLight.insert(calibration.lightPath.canonicalPath(), &calibration);
    }
    QSet<QString> preCalibratedLights;
    for (const auto& group : manifest.preCalibratedLightGroups)
    {
        for (const FrameId& light : group.lightFiles)
        {
            preCalibratedLights.insert(light.canonicalPath());
        }
    }

    for (const auto& group : manifest.integrationGroups)
    {
        const QString masterKey = group.groupKey.toString();
        const auto master =
            std::find_if(model.masters.begin(), model.masters.end(),
                         [&masterKey](const ReportMaster& candidate) { return candidate.key == masterKey; });
        if (master == model.masters.end())
        {
            continue;
        }

        // The lights of the group by the calibration they take, in the order the first light of each is met.
        std::vector<SharedCalibration> shared;
        for (const FrameId& light : group.lightFiles)
        {
            SharedCalibration ofLight;
            const QString path = light.canonicalPath();
            if (const auto entry = calibrationByLight.constFind(path); entry != calibrationByLight.constEnd())
            {
                const WorkflowManifest::LightsCalibrationParams& calibration = **entry;
                ofLight.calibrated = true;
                ofLight.bias = calibration.biasPath.has_value() ? calibration.biasPath->canonicalPath() : QString();
                ofLight.dark = calibration.darkPath.has_value() ? calibration.darkPath->canonicalPath() : QString();
                ofLight.flat = calibration.flatPath.has_value() ? calibration.flatPath->canonicalPath() : QString();
                ofLight.settings = calibration.settings;
            }
            else
            {
                ofLight.preCalibrated = preCalibratedLights.contains(path);
            }
            const auto same =
                std::find_if(shared.begin(), shared.end(),
                             [&ofLight](const SharedCalibration& candidate) { return candidate.sameAs(ofLight); });
            if (same == shared.end())
            {
                ofLight.lights = 1;
                shared.push_back(std::move(ofLight));
            }
            else
            {
                ++same->lights;
            }
        }

        for (std::size_t i = 0; i < shared.size(); ++i)
        {
            const SharedCalibration& calibration = shared[i];
            ReportLightGroup lights;
            lights.key = shared.size() == 1 ? masterKey : QStringLiteral("%1#%2").arg(masterKey).arg(i + 1);
            lights.name = shared.size() == 1 ? master->name : reportText("%1, part %2").arg(master->name).arg(i + 1);
            lights.masterKey = masterKey;
            lights.lights = calibration.lights;
            lights.preCalibrated = calibration.preCalibrated;
            lights.biasMaster = QFileInfo(calibration.bias).fileName();
            lights.darkMaster = QFileInfo(calibration.dark).fileName();
            lights.flatMaster = QFileInfo(calibration.flat).fileName();
            lights.biasMasterSlug = slugOf(calibration.bias);
            lights.darkMasterSlug = slugOf(calibration.dark);
            lights.flatMasterSlug = slugOf(calibration.flat);
            if (calibration.calibrated)
            {
                lights.settings.push_back(lightsCalibrationBlock(calibration.settings, !calibration.dark.isEmpty(),
                                                                 !calibration.flat.isEmpty()));
            }
            for (const QString& file : {calibration.bias, calibration.dark, calibration.flat})
            {
                ReportCalibrationMaster* used = file.isEmpty() ? nullptr : calibrationOf(file);
                if (used != nullptr && !used->usedBy.contains(masterKey))
                {
                    used->usedBy.append(masterKey);
                }
            }
            master->lightGroups.append(lights.key);
            model.lightGroups.push_back(std::move(lights));
        }
    }
}
} // namespace ImageIntegration::Reporting
