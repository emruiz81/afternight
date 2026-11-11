// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <span>
#include <utility>

/**
 * @file RunFacts.cpp
 * @brief The run as a whole: how it ended, its totals, its stages and timings, its nights, its equipment and its
 * provenance.
 */

namespace ImageIntegration::Reporting
{
/// Extract the observing date (YYYY-MM-DD) from a LIGHT file path.
/// Looks for a date-like directory component or filename prefix.
[[nodiscard]] QDate extractObservingDate(const QString& filePath)
{
    // Try to match YYYY-MM-DD in the path
    static const QRegularExpression dateRx(QStringLiteral("(\\d{4}-\\d{2}-\\d{2})"));
    auto match = dateRx.match(filePath);
    if (match.hasMatch())
    {
        const QDate d = QDate::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd"));
        if (d.isValid())
            return d;
    }
    return {};
}

/// Extract sensor temperature from a filename like ..._-10.00_60.00s_0003.fits
[[nodiscard]] std::optional<double> extractSensorTemperature(const QString& filePath)
{
    static const QRegularExpression tempRx(QStringLiteral("_(-?\\d+\\.\\d+)_\\d+\\.\\d+s_"));
    auto match = tempRx.match(filePath);
    if (match.hasMatch())
    {
        bool ok = false;
        const double temp = match.captured(1).toDouble(&ok);
        if (ok)
            return temp;
    }
    return std::nullopt;
}

/// Extract sub-exposure duration (seconds) from a filename like ..._-10.00_60.00s_0003.fits
[[nodiscard]] std::optional<double> extractExposureFromFilename(const QString& filePath)
{
    static const QRegularExpression expRx(QStringLiteral("_(\\d+(?:\\.\\d+)?)s(?=_|\\.|$)"),
                                          QRegularExpression::CaseInsensitiveOption);
    auto match = expRx.match(filePath);
    if (match.hasMatch())
    {
        bool ok = false;
        const double sec = match.captured(1).toDouble(&ok);
        if (ok && sec > 0)
            return sec;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> parseGroupExposure(const WorkflowManifest::IntegrationGroup& group)
{
    bool ok = false;
    const double fromField = group.exposure.toDouble(&ok);
    if (ok && fromField > 0.0)
    {
        return fromField;
    }
    return std::nullopt;
}

void addExposureBucket(QList<ReportExposureBucket>& buckets, double exposureSeconds, int frameCount)
{
    constexpr double kExposureMergeToleranceSeconds = 1e-3;
    for (auto& bucket : buckets)
    {
        if (std::abs(bucket.exposureSeconds - exposureSeconds) <= kExposureMergeToleranceSeconds)
        {
            bucket.frameCount += frameCount;
            return;
        }
    }

    buckets.append(ReportExposureBucket{exposureSeconds, frameCount});
}

[[nodiscard]] ExposureBreakdown buildExposureBreakdown(const WorkflowManifest::IntegrationGroup& group)
{
    ExposureBreakdown breakdown;
    breakdown.totalFrames = static_cast<int>(group.lightFiles.size());

    const std::optional<double> groupExposure = parseGroupExposure(group);
    const bool hasEmbeddedExposures = group.lightExposureSeconds.size() == group.lightFiles.size();
    for (size_t i = 0; i < group.lightFiles.size(); ++i)
    {
        const auto& lightFile = group.lightFiles[i];

        std::optional<double> resolvedExposure;
        if (hasEmbeddedExposures)
        {
            const double embeddedExposure = group.lightExposureSeconds[i];
            if (embeddedExposure > 0.0)
            {
                resolvedExposure = embeddedExposure;
            }
        }

        if (!resolvedExposure.has_value())
        {
            resolvedExposure = extractExposureFromFilename(lightFile.canonicalPath());
        }
        if (!resolvedExposure.has_value() && groupExposure.has_value())
        {
            resolvedExposure = groupExposure;
        }

        if (resolvedExposure.has_value() && *resolvedExposure > 0.0)
        {
            addExposureBucket(breakdown.buckets, *resolvedExposure, 1);
            ++breakdown.knownFrames;
            breakdown.totalMs += static_cast<int>(std::llround(*resolvedExposure * 1000.0));
        }
        else
        {
            ++breakdown.unknownFrames;
        }
    }

    std::sort(breakdown.buckets.begin(), breakdown.buckets.end(),
              [](const ReportExposureBucket& lhs, const ReportExposureBucket& rhs) {
                  return lhs.exposureSeconds < rhs.exposureSeconds;
              });

    return breakdown;
}

[[nodiscard]] GroupOutcomeCounts computeGroupOutcomeCounts(const WorkflowManifest& manifest,
                                                           const WorkflowExecutor::Result& result)
{
    GroupOutcomeCounts counts;
    counts.totalGroups = static_cast<int>(manifest.integrationGroups.size());

    for (const auto& groupResult : result.integrationGroupResults)
    {
        if (groupResult.success)
        {
            ++counts.regeneratedGroups;
        }
        else
        {
            ++counts.failedGroups;
        }
    }

    if (counts.totalGroups <= 0)
    {
        counts.totalGroups = counts.regeneratedGroups + counts.failedGroups;
    }

    const int reused = counts.totalGroups - counts.regeneratedGroups - counts.failedGroups;
    counts.reusedGroups = reused > 0 ? reused : 0;
    return counts;
}

[[nodiscard]] QString stageDisplayName(QString stageName)
{
    for (int i = stageName.length() - 1; i > 0; --i)
    {
        if (stageName[i].isUpper() && (stageName[i - 1].isLower() || stageName[i - 1].isDigit()))
        {
            stageName.insert(i, QLatin1Char(' '));
        }
    }
    return stageName.trimmed();
}

[[nodiscard]] InputFileSets collectInputFileSets(const WorkflowManifest& manifest)
{
    InputFileSets sets;

    if (manifest.biasStage.has_value())
    {
        for (const auto& group : manifest.biasStage->groups)
        {
            for (const auto& file : group.inputFiles)
            {
                sets.biasFiles.insert(file.canonicalPath());
            }
        }
    }
    if (manifest.darkStage.has_value())
    {
        for (const auto& group : manifest.darkStage->groups)
        {
            for (const auto& file : group.inputFiles)
            {
                sets.darkFiles.insert(file.canonicalPath());
            }
        }
    }
    if (manifest.flatStage.has_value())
    {
        for (const auto& group : manifest.flatStage->groups)
        {
            for (const auto& file : group.inputFiles)
            {
                sets.flatFiles.insert(file.canonicalPath());
            }
        }
    }
    for (const auto& group : manifest.integrationGroups)
    {
        for (const auto& file : group.lightFiles)
        {
            sets.lightFiles.insert(file.canonicalPath());
        }
    }

    return sets;
}

[[nodiscard]] QString sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

[[nodiscard]] QString resolveTargetName(const WorkflowManifest::IntegrationGroup& group)
{
    const QString directObject = group.object.trimmed();
    if (!directObject.isEmpty())
    {
        return directObject;
    }

    GroupingOptions options;
    options.byObject = true;
    options.bySession = true;

    QString parsedObject;
    GroupKey::parseMetadata(group.groupKey.key, FrameType::Light, options, &parsedObject, nullptr);
    return parsedObject.trimmed();
}

[[nodiscard]] QString canonicalOrRawPath(const FrameId& id)
{
    const QString canonical = id.canonicalPath();
    return canonical.isEmpty() ? id.toString() : canonical;
}

[[nodiscard]] QStringList collectTargets(const WorkflowManifest& manifest)
{
    QSet<QString> targets;
    GroupingOptions options;
    options.byObject = true;
    options.bySession = true;

    for (const auto& group : manifest.integrationGroups)
    {
        const QString directObject = group.object.trimmed();
        if (!directObject.isEmpty())
        {
            targets.insert(directObject);
            continue;
        }

        QString parsedObject;
        GroupKey::parseMetadata(group.groupKey.key, FrameType::Light, options, &parsedObject, nullptr);
        if (!parsedObject.trimmed().isEmpty())
        {
            targets.insert(parsedObject.trimmed());
        }
    }

    QStringList list = targets.values();
    std::sort(list.begin(), list.end(),
              [](const QString& lhs, const QString& rhs) { return lhs.compare(rhs, Qt::CaseInsensitive) < 0; });
    return list;
}

/// A header text as the report shows it: trimmed, and nothing for the dash a header writes for a figure it does not
/// have.
[[nodiscard]] QString normalizeMetadataText(const QString& rawText)
{
    const QString text = rawText.trimmed();
    if (text.isEmpty() || text == QStringLiteral("-"))
    {
        return {};
    }

    return text;
}

[[nodiscard]] QString normalizeMetadataText(const std::string& rawText)
{
    return normalizeMetadataText(QString::fromStdString(rawText));
}

/// A header card's value without the quotes a FITS string is written in.
[[nodiscard]] QString unquotedCardText(const std::string& value)
{
    QString text = QString::fromStdString(value).trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\'')))
    {
        text = text.mid(1, text.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    return text.trimmed();
}

[[nodiscard]] QString readFirstFitsValue(const an::ImageMetadata& metadata, const QStringList& keys)
{
    const std::span<const an::CodecCard> cards = metadata.codec().cards();
    for (const auto& key : keys)
    {
        for (const an::CodecCard& card : cards)
        {
            if (card.origin() != an::CodecNamespace::Fits ||
                QString::fromStdString(card.keyword()).compare(key, Qt::CaseInsensitive) != 0)
            {
                continue;
            }
            const QString value = normalizeMetadataText(unquotedCardText(card.value()));
            if (!value.isEmpty())
            {
                return value;
            }
        }
    }

    return {};
}

/// The target a header names: the acquisition record's object, or the one its cards state.
[[nodiscard]] QString metadataObjectName(const an::ImageMetadata& metadata)
{
    const QString stated = normalizeMetadataText(metadata.acquisition().objectName());
    if (!stated.isEmpty())
    {
        return stated;
    }
    return normalizeMetadataText(an::projectAcquisition(metadata.codec()).acquisition.objectName);
}

/// The camera a header names: the acquisition record's instrument, or the one its cards state.
[[nodiscard]] QString metadataCameraModel(const an::ImageMetadata& metadata)
{
    const QString stated = normalizeMetadataText(metadata.acquisition().instrumentName());
    if (!stated.isEmpty())
    {
        return stated;
    }
    return normalizeMetadataText(an::projectAcquisition(metadata.codec()).acquisition.instrumentName);
}

/// Whether a header states an exposure, in its acquisition record or its cards.
[[nodiscard]] bool metadataStatesExposure(const an::ImageMetadata& metadata)
{
    return metadata.acquisition().exposureSeconds().has_value() ||
           an::projectAcquisition(metadata.codec()).acquisition.exposureSeconds.has_value();
}

/// The telescope a header names: its `TELESCOP` card, or the acquisition record's telescope.
[[nodiscard]] QString metadataTelescope(const an::ImageMetadata& metadata)
{
    const QString carded = readFirstFitsValue(metadata, {QStringLiteral("TELESCOP"), QStringLiteral("TELESCOPE")});
    if (!carded.isEmpty())
    {
        return carded;
    }
    const QString stated = normalizeMetadataText(metadata.acquisition().telescopeName());
    if (!stated.isEmpty())
    {
        return stated;
    }
    return normalizeMetadataText(an::projectAcquisition(metadata.codec()).acquisition.telescopeName);
}

/// The ISO speed a camera reported, or else its gain setting, as the image summary reads them.
[[nodiscard]] QString formatIsoGainLabel(const an::ImageMetadata& metadata)
{
    const an::AcquisitionMetadata& acquisition = metadata.acquisition();
    const an::AcquisitionProjection projected = an::projectAcquisition(metadata.codec());

    std::optional<std::int64_t> iso = projected.isoSpeed;
    if (!iso.has_value() && acquisition.rawCamera().has_value() && acquisition.rawCamera()->isoSpeed > 0.0)
    {
        iso = std::llround(acquisition.rawCamera()->isoSpeed);
    }
    if (iso.has_value())
    {
        return QStringLiteral("ISO %1").arg(static_cast<qlonglong>(*iso));
    }
    if (projected.gainSetting.has_value())
    {
        return QStringLiteral("Gain %1").arg(static_cast<qlonglong>(*projected.gainSetting));
    }
    return {};
}

[[nodiscard]] QString formatOffsetLabel(const an::ImageMetadata& metadata)
{
    const an::AcquisitionProjection projected = an::projectAcquisition(metadata.codec());
    if (!projected.cameraOffset.has_value())
    {
        return {};
    }

    return QStringLiteral("Offset %1").arg(static_cast<qlonglong>(*projected.cameraOffset));
}

[[nodiscard]] QString formatLocationFromFits(const an::ImageMetadata& metadata)
{
    const QString lat = readFirstFitsValue(metadata, {QStringLiteral("SITELAT"), QStringLiteral("OBS-LAT"),
                                                      QStringLiteral("LAT-OBS"), QStringLiteral("OBSGEO-B")});
    const QString lon = readFirstFitsValue(metadata, {QStringLiteral("SITELONG"), QStringLiteral("OBS-LONG"),
                                                      QStringLiteral("LONG-OBS"), QStringLiteral("OBSGEO-L")});

    if (!lat.isEmpty() && !lon.isEmpty())
    {
        return QStringLiteral("%1, %2").arg(lat, lon);
    }

    return {};
}

[[nodiscard]] QString resolveAppVersion(const QJsonObject& context)
{
    const QString contextVersion = context.value(QStringLiteral("appVersion")).toString().trimmed();
    if (!contextVersion.isEmpty())
    {
        return contextVersion;
    }

    const QString engineVersion = QString::fromUtf8(an::engineVersionString()).trimmed();
    if (!engineVersion.isEmpty())
    {
        return engineVersion;
    }

    return QStringLiteral("Unknown");
}

[[nodiscard]] QString resolveContextValue(const QJsonObject& context, const QString& key, const QString& fallback)
{
    const QString value = context.value(key).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

[[nodiscard]] QString reportProcessLabel(const WorkflowManifest& manifest)
{
    const QString label = manifest.processLabel.trimmed();
    return label.isEmpty() ? QStringLiteral("Image Integration") : label;
}

[[nodiscard]] QString reportTitle(const WorkflowManifest& manifest)
{
    return manifest.processLabel.trimmed().isEmpty()
               ? QStringLiteral("AfterNight Integration Report")
               : QStringLiteral("AfterNight %1 Report").arg(manifest.processLabel.trimmed());
}

[[nodiscard]] QString reportProcessingProfileText(const WorkflowManifest& manifest)
{
    const QString label = manifest.processingProfileLabel.trimmed();
    const QString id = manifest.processingProfileId.trimmed();
    if (label.isEmpty())
    {
        return id;
    }
    if (id.isEmpty())
    {
        return label;
    }
    return QStringLiteral("%1 (`%2`)").arg(label, id);
}

QString computeReproducibilityFingerprint(const WorkflowManifest& manifest, const QJsonObject& context,
                                          QString* manifestSettingsFingerprint, QString* inputFingerprint)
{
    auto appendSortedArray = [](QJsonObject& object, const QString& key, QStringList values) {
        std::sort(values.begin(), values.end(),
                  [](const QString& lhs, const QString& rhs) { return lhs.compare(rhs, Qt::CaseInsensitive) < 0; });
        QJsonArray array;
        for (const auto& value : values)
        {
            array.append(value);
        }
        object.insert(key, array);
    };

    QStringList integrationGroupHashes;
    integrationGroupHashes.reserve(static_cast<int>(manifest.integrationGroups.size()));
    for (const auto& group : manifest.integrationGroups)
    {
        integrationGroupHashes << QStringLiteral("%1:%2").arg(group.groupKey.toString(),
                                                              computeIntegrationGroupHash(manifest, group));
    }

    QStringList calibrationGroupHashes;
    const auto collectCalibrationHashes = [&](const std::optional<WorkflowManifest::CalibrationStage>& stage,
                                              const QString& stageName) {
        if (!stage.has_value())
        {
            return;
        }
        for (const auto& group : stage->groups)
        {
            calibrationGroupHashes << QStringLiteral("%1:%2:%3")
                                          .arg(stageName, group.groupKey.toString(),
                                               computeCalibrationGroupHash(manifest, group));
        }
    };
    collectCalibrationHashes(manifest.biasStage, QStringLiteral("bias"));
    collectCalibrationHashes(manifest.darkStage, QStringLiteral("dark"));
    collectCalibrationHashes(manifest.flatStage, QStringLiteral("flat"));

    QStringList registrationHashes;
    registrationHashes.reserve(static_cast<int>(manifest.registrationGroups.size()));
    for (const auto& registrationGroup : manifest.registrationGroups)
    {
        const QString referencePath = registrationGroup.referenceFrame.has_value()
                                          ? canonicalOrRawPath(registrationGroup.referenceFrame.value())
                                          : QString();
        registrationHashes << QStringLiteral("%1:%2:%3")
                                  .arg(registrationGroup.groupKey.toString(),
                                       computeRegistrationSettingsHash(registrationGroup.settings), referencePath);
    }

    QJsonObject manifestSettingsObject;
    manifestSettingsObject.insert(QStringLiteral("manifestVersion"), manifest.version);
    manifestSettingsObject.insert(QStringLiteral("executionMode"), static_cast<int>(manifest.executionMode));
    manifestSettingsObject.insert(QStringLiteral("executionScope"), manifest.executionScope);
    manifestSettingsObject.insert(QStringLiteral("executionScopeKey"), manifest.executionScopeKey);
    if (!manifest.processLabel.trimmed().isEmpty())
    {
        manifestSettingsObject.insert(QStringLiteral("processLabel"), manifest.processLabel.trimmed());
    }
    if (!manifest.processingProfileId.trimmed().isEmpty())
    {
        manifestSettingsObject.insert(QStringLiteral("processingProfileId"), manifest.processingProfileId.trimmed());
    }
    if (!manifest.processingProfileLabel.trimmed().isEmpty())
    {
        manifestSettingsObject.insert(QStringLiteral("processingProfileLabel"),
                                      manifest.processingProfileLabel.trimmed());
    }
    manifestSettingsObject.insert(QStringLiteral("reprocessExisting"), manifest.reprocessExisting);
    manifestSettingsObject.insert(QStringLiteral("calibrateLightsInMemory"), manifest.calibrateLightsInMemory);
    manifestSettingsObject.insert(QStringLiteral("maxParallelCores"), manifest.maxParallelCores);
    manifestSettingsObject.insert(QStringLiteral("lightsCalibrationHash"),
                                  computeLightsCalibrationHash(manifest.lightsCalibration));
    appendSortedArray(manifestSettingsObject, QStringLiteral("integrationGroupHashes"), integrationGroupHashes);
    appendSortedArray(manifestSettingsObject, QStringLiteral("registrationHashes"), registrationHashes);
    appendSortedArray(manifestSettingsObject, QStringLiteral("calibrationGroupHashes"), calibrationGroupHashes);

    const QString manifestHash = sha256Hex(QJsonDocument(manifestSettingsObject).toJson(QJsonDocument::Compact));

    const InputFileSets inputSets = collectInputFileSets(manifest);
    QJsonObject inputObject;
    appendSortedArray(inputObject, QStringLiteral("lightFiles"), inputSets.lightFiles.values());
    appendSortedArray(inputObject, QStringLiteral("biasFiles"), inputSets.biasFiles.values());
    appendSortedArray(inputObject, QStringLiteral("darkFiles"), inputSets.darkFiles.values());
    appendSortedArray(inputObject, QStringLiteral("flatFiles"), inputSets.flatFiles.values());

    const QString inputHash = sha256Hex(QJsonDocument(inputObject).toJson(QJsonDocument::Compact));

    if (manifestSettingsFingerprint != nullptr)
    {
        *manifestSettingsFingerprint = manifestHash;
    }
    if (inputFingerprint != nullptr)
    {
        *inputFingerprint = inputHash;
    }

    QJsonObject composite;
    composite.insert(QStringLiteral("afterNightVersion"), resolveAppVersion(context));
    composite.insert(QStringLiteral("manifestSettingsFingerprint"), manifestHash);
    composite.insert(QStringLiteral("inputFingerprint"), inputHash);

    return sha256Hex(QJsonDocument(composite).toJson(QJsonDocument::Compact));
}

QString reportVerdict(const ReportOutcome outcome, const ReportGroupCounts& groups)
{
    const QString made = QString::number(groups.made);
    const QString total = QString::number(groups.total);
    switch (outcome)
    {
    case ReportOutcome::Cancelled:
        return reportText("The run was cancelled after %1 of its %2 masters were made.").arg(made, total);
    case ReportOutcome::Failed:
        return groups.total == 1 ? reportText("The run failed before its master was made.")
                                 : reportText("The run failed before any of its %1 masters was made.").arg(total);
    case ReportOutcome::Partial:
        return reportText("The run made %1 of its %2 masters; %3 failed and %4 were not reached.")
            .arg(made, total, QString::number(groups.failed), QString::number(groups.notReached));
    case ReportOutcome::CompletedWithWarnings:
    case ReportOutcome::Completed:
        break;
    }
    // A run of one master is said of that master: "all 1 of its masters" is not how a person says it.
    if (groups.total == 1)
    {
        return groups.reused > 0 ? reportText("The run's master was still valid, so it was not made again.")
                                 : reportText("The run made its master.");
    }
    if (groups.made == 0 && groups.reused > 0)
    {
        return reportText("Every one of the run's %1 masters was still valid, so none was made again.").arg(total);
    }
    if (groups.reused > 0)
    {
        return reportText("The run made %1 of its %2 masters; the other %3 were still valid.")
            .arg(made, total, QString::number(groups.reused));
    }
    return reportText("The run made all %1 of its masters.").arg(total);
}

ReportExposure reportExposure(const ExposureBreakdown& breakdown)
{
    ReportExposure exposure;
    exposure.frames = breakdown.totalFrames;
    exposure.framesWithExposure = breakdown.knownFrames;
    exposure.integrationMs = breakdown.totalMs;
    exposure.bins.reserve(static_cast<std::size_t>(breakdown.buckets.size()));
    for (const ReportExposureBucket& bucket : breakdown.buckets)
    {
        exposure.bins.push_back(ReportExposureBin{.seconds = bucket.exposureSeconds, .frames = bucket.frameCount});
    }
    return exposure;
}

namespace
{
/// When the archive says it was written, in the zone it states; nothing when it states no time that can be read.
[[nodiscard]] std::optional<QDateTime> archivedAt(const QJsonObject& context)
{
    const QString text = context.value(QStringLiteral("archivedAtLocal")).toString().trimmed();
    if (text.isEmpty())
    {
        return std::nullopt;
    }
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid())
    {
        parsed = QDateTime::fromString(text, Qt::ISODate);
    }
    if (!parsed.isValid())
    {
        parsed = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!parsed.isValid())
    {
        return std::nullopt;
    }
    // The archive writes its local time without an offset and the offset beside it.
    const QJsonValue offset = context.value(QStringLiteral("utcOffsetSeconds"));
    if (parsed.timeSpec() == Qt::LocalTime && offset.isDouble())
    {
        return QDateTime(parsed.date(), parsed.time(), QTimeZone::fromSecondsAheadOfUtc(offset.toInt()));
    }
    return parsed;
}

[[nodiscard]] ReportStageState stageState(const QString& status)
{
    const QString normalized = status.trimmed().toLower();
    if (normalized == QStringLiteral("failed"))
    {
        return ReportStageState::Failed;
    }
    if (normalized == QStringLiteral("partial"))
    {
        return ReportStageState::Partial;
    }
    if (normalized == QStringLiteral("cancelled"))
    {
        return ReportStageState::Cancelled;
    }
    if (normalized == QStringLiteral("skipped"))
    {
        return ReportStageState::Skipped;
    }
    return ReportStageState::Ran;
}

/// The groups a stage works on, by its name: the calibration groups of its kind, the registration groups, or the
/// integration groups.
[[nodiscard]] int stageGroupCount(const WorkflowManifest& manifest, const QString& stageName)
{
    const auto count = [](const std::optional<WorkflowManifest::CalibrationStage>& stage) {
        return stage.has_value() ? static_cast<int>(stage->groups.size()) : 0;
    };
    const QString lower = stageName.toLower();
    if (lower.contains(QStringLiteral("bias")))
    {
        return count(manifest.biasStage);
    }
    if (lower.contains(QStringLiteral("dark")))
    {
        return count(manifest.darkStage);
    }
    if (lower.contains(QStringLiteral("flat")))
    {
        return count(manifest.flatStage);
    }
    if (lower.contains(QStringLiteral("registration")) || lower.contains(QStringLiteral("alignment")))
    {
        return static_cast<int>(manifest.registrationGroups.size());
    }
    return static_cast<int>(manifest.integrationGroups.size());
}

/// The name a stage's result goes by, for the name the manifest's sequence lists it under.
[[nodiscard]] QString stageResultName(const QString& sequenceName)
{
    if (sequenceName == QStringLiteral("Bias") || sequenceName == QStringLiteral("Dark") ||
        sequenceName == QStringLiteral("Flat"))
    {
        return sequenceName + QStringLiteral("Stacking");
    }
    return sequenceName;
}

/// The run's stages in the order the manifest lists them: the ones the run timed, as it timed them, and the ones it
/// has no result for. Such a stage was not reached in a run that stopped; in a run that completed, what it makes was
/// still valid when the run was incremental and the stage has groups, and the run had no work for it otherwise. A
/// stage the run timed and the manifest does not list follows them.
void fillStages(const ReportBuild& build, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();
    const WorkflowExecutor::Result& result = build.result();
    const auto timed = [&](const WorkflowExecutor::Result::StageTiming& stage) {
        ReportStage stated;
        stated.key = stage.stageName;
        stated.name = stageDisplayName(stage.stageName);
        stated.status = stage.status.trimmed().isEmpty() ? QStringLiteral("unknown") : stage.status.trimmed().toLower();
        stated.state = stageState(stage.status);
        stated.durationMs = stage.durationMs;
        stated.groups = stageGroupCount(manifest, stage.stageName);
        if (stated.groups <= 0 && stage.groupsProcessed > 0)
        {
            stated.groups = stage.groupsProcessed;
        }
        return stated;
    };

    const bool stopped = result.cancelled || !result.success;
    const bool incremental = manifest.executionMode == ExecutionMode::Incremental;
    std::vector<bool> stated(result.stageResults.size(), false);
    model.stages.reserve(result.stageResults.size() + static_cast<std::size_t>(manifest.stageSequence.size()));
    for (const QString& sequenceName : manifest.stageSequence)
    {
        const QString resultName = stageResultName(sequenceName.trimmed());
        bool found = false;
        for (std::size_t i = 0; i < result.stageResults.size(); ++i)
        {
            if (!stated[i] && result.stageResults[i].stageName == resultName)
            {
                model.stages.push_back(timed(result.stageResults[i]));
                stated[i] = true;
                found = true;
                break;
            }
        }
        if (found)
        {
            continue;
        }
        ReportStage untimed;
        untimed.key = resultName;
        untimed.name = stageDisplayName(resultName);
        untimed.groups = stageGroupCount(manifest, resultName);
        if (stopped)
        {
            untimed.state = ReportStageState::NotReached;
            untimed.status = QStringLiteral("not reached");
            untimed.note = reportText("The run stopped before this stage.");
        }
        else if (incremental && untimed.groups > 0)
        {
            untimed.state = ReportStageState::Reused;
            untimed.status = QStringLiteral("reused");
            untimed.note = reportText("Everything this stage makes was still valid.");
        }
        else
        {
            untimed.state = ReportStageState::Skipped;
            untimed.status = QStringLiteral("skipped");
            untimed.note = reportText("The run had no work for this stage.");
        }
        model.stages.push_back(std::move(untimed));
    }
    for (std::size_t i = 0; i < result.stageResults.size(); ++i)
    {
        if (!stated[i])
        {
            model.stages.push_back(timed(result.stageResults[i]));
        }
    }
}

/// What the engine counted of its memory over the run, and what the run's image cache counted, when collected.
void fillResources(const ReportBuild& build, ReportRun& run)
{
    const WorkflowExecutor::Result& result = build.result();
    if (result.engineMemory.has_value())
    {
        run.engineMemory = ReportEngineMemory{.peakBytes = result.engineMemory->peakBytes,
                                              .peakBytesBeforeRun = result.engineMemory->peakBytesBeforeRun,
                                              .liveBytesAtEnd = result.engineMemory->liveBytesAtEnd,
                                              .allocations = result.engineMemory->allocations,
                                              .bytesAllocated = result.engineMemory->bytesAllocated,
                                              .deniedAllocations = result.engineMemory->deniedAllocations};
    }
    if (!result.workflowCacheDiagnostics.has_value())
    {
        return;
    }
    const WorkflowImageCacheDiagnostics& cache = *result.workflowCacheDiagnostics;
    const auto add = [&run](const char* key, const char* name, const std::uint64_t value) {
        run.cacheDiagnostics.push_back(
            ReportCounter{.key = QString::fromLatin1(key), .name = reportText(name), .value = value});
    };
    add("physicalDecodedLoads", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Images decoded"),
        cache.physicalDecodedLoads);
    add("physicalCalibrations", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Images calibrated"),
        cache.physicalCalibrations);
    add("hits", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Cache hits"), cache.hits);
    add("misses", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Cache misses"), cache.misses);
    add("evictions", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Cache evictions"), cache.evictions);
    add("capacityBypasses", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Capacity bypasses"), cache.capacityBypasses);
    add("oversizedBypasses", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Oversized bypasses"),
        cache.oversizedBypasses);
    add("overflowBypasses", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Overflow bypasses"), cache.overflowBypasses);
    add("generationInvalidations", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Generation invalidations"),
        cache.generationInvalidations);
    add("publications", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Cache publications"), cache.publications);
    add("failedBuilds", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Failed builds"), cache.failedBuilds);
    add("cancelledBuilds", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Cancelled builds"), cache.cancelledBuilds);
    add("sameKeyWaits", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Waits for the same image"), cache.sameKeyWaits);
    add("cleanupCalls", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Cleanup calls"), cache.cleanupCalls);
    add("currentBytes", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Bytes held at the end"), cache.currentBytes);
    add("peakBytes", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Peak bytes held"), cache.peakBytes);
    add("lookupEntries", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Lookup entries"), cache.lookupEntries);
    add("allowanceRefusals", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Allowance refusals"),
        cache.allowanceRefusals);
    add("allowanceEvictions", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Allowance evictions"),
        cache.allowanceEvictions);
}

void fillCounters(const ReportBuild& build, ReportRun& run)
{
    std::optional<WorkflowExecutionCounters> counters = build.result().executionCounters;
    if (!counters.has_value())
    {
        counters = build.inputs().counters;
    }
    if (!counters.has_value())
    {
        return;
    }
    const auto add = [&run](const char* key, const char* name, const std::uint64_t value) {
        run.counters.push_back(
            ReportCounter{.key = QString::fromLatin1(key), .name = reportText(name), .value = value});
    };
    add("bytesHashed", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Bytes hashed"), counters->bytesHashed());
    add("bytesCopied", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Bytes copied"), counters->bytesCopied());
    add("sqliteStatementExecutions", QT_TRANSLATE_NOOP("ImageIntegrationReport", "SQLite statement executions"),
        counters->sqliteStatementExecutions());
    add("sqliteTransactionsStarted", QT_TRANSLATE_NOOP("ImageIntegrationReport", "SQLite transactions started"),
        counters->sqliteTransactionsStarted());
    add("sqliteTransactionsCommitted", QT_TRANSLATE_NOOP("ImageIntegrationReport", "SQLite transactions committed"),
        counters->sqliteTransactionsCommitted());
    add("sqliteTransactionsRolledBack", QT_TRANSLATE_NOOP("ImageIntegrationReport", "SQLite transactions rolled back"),
        counters->sqliteTransactionsRolledBack());
    add("inputDecodeOperations", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Input decode operations"),
        counters->inputDecodeOperations());
    add("scientificOutputEncodeCompletions",
        QT_TRANSLATE_NOOP("ImageIntegrationReport", "Scientific output encode completions"),
        counters->scientificOutputEncodeCompletions());
    add("inputPassesStarted", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Input passes started"),
        counters->inputPassesStarted());
    add("tilePassesStarted", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Tile passes started"),
        counters->tilePassesStarted());
    add("stackLoaderRequests", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Stack loader requests"),
        counters->stackLoaderRequests());
    add("roiSubsetReads", QT_TRANSLATE_NOOP("ImageIntegrationReport", "ROI subset reads"), counters->roiSubsetReads());
    add("fullFrameFallbackReads", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Full-frame fallback reads"),
        counters->fullFrameFallbackReads());
    add("logicalDecodedPixelBytes", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Logical decoded pixel bytes"),
        counters->logicalDecodedPixelBytes());
    add("calibrationInvocations", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibration invocations"),
        counters->calibrationInvocations());
    add("calibratedSpillFilesCreated", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibrated spill files created"),
        counters->calibratedSpillFilesCreated());
    add("calibratedSpillBytesWritten",
        QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibrated spill payload bytes written"),
        counters->calibratedSpillBytesWritten());
    add("calibratedSpillBytesRead", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibrated spill payload bytes read"),
        counters->calibratedSpillBytesRead());
    add("calibratedSpillRoiReads", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibrated spill ROI reads"),
        counters->calibratedSpillRoiReads());
    add("calibratedSpillCleanupSuccesses",
        QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibrated spill cleanup successes"),
        counters->calibratedSpillCleanupSuccesses());
    add("calibratedSpillCleanupFailures",
        QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibrated spill cleanup failures"),
        counters->calibratedSpillCleanupFailures());
    add("dumpBytesRead", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Dump payload bytes read"),
        counters->dumpBytesRead());
    add("dumpBytesWritten", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Dump payload bytes written"),
        counters->dumpBytesWritten());
    add("pixmapBytesRead", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Dense pixmap payload bytes read"),
        counters->pixmapBytesRead());
    add("pixmapBytesWritten", QT_TRANSLATE_NOOP("ImageIntegrationReport", "Dense pixmap payload bytes written"),
        counters->pixmapBytesWritten());
}

void fillSessionNotes(const ReportBuild& build, ReportRun& run)
{
    const QString outputDir = build.manifest().outputDirectory.trimmed();
    IReportFiles* files = build.sources().files;
    if (outputDir.isEmpty() || files == nullptr)
    {
        return;
    }
    const QString notesPath =
        QDir(outputDir).filePath(QStringLiteral(".afternight/image_integration/session_notes.txt"));
    const std::optional<QString> notes = files->readText(notesPath);
    if (!notes.has_value() || notes->trimmed().isEmpty())
    {
        return;
    }
    run.sessionNotes = notes->trimmed();
    run.sessionNotesChangedAt = files->lastModified(notesPath);
}

/// The filter a master's lights were taken through, as the run's rollups name it.
[[nodiscard]] QString filterName(const ReportMaster& master)
{
    return master.filter.isEmpty() ? reportText("Unfiltered") : master.filter;
}

/// The nights the lights were taken on, and each filter's lights and exposure on each. A light's night is the one
/// its capture time states; a light with no capture time is on no night.
void fillNights(const ReportModel& model, ReportRun& run)
{
    struct Night
    {
        int frames = 0;
        qint64 integrationMs = 0;
        QMap<QString, ReportNightFilter> filters;
    };
    QMap<QDate, Night> nights;
    for (const ReportMaster& master : model.masters)
    {
        const QString filter = filterName(master);
        for (const ReportFrame& frame : master.frames)
        {
            if (!frame.night.has_value() || !frame.night->isValid())
            {
                continue;
            }
            Night& night = nights[*frame.night];
            ReportNightFilter& ofFilter = night.filters[filter];
            ofFilter.filter = filter;
            ++night.frames;
            ++ofFilter.frames;
            if (frame.exposureSeconds.has_value())
            {
                const qint64 ms = std::llround(*frame.exposureSeconds * 1000.0);
                night.integrationMs += ms;
                ofFilter.integrationMs += ms;
            }
        }
    }
    run.nights.reserve(static_cast<std::size_t>(nights.size()));
    for (auto it = nights.cbegin(); it != nights.cend(); ++it)
    {
        ReportNight night;
        night.date = it.key();
        night.frames = it->frames;
        night.integrationMs = it->integrationMs;
        for (const ReportNightFilter& ofFilter : it->filters)
        {
            night.filters.push_back(ofFilter);
        }
        run.nights.push_back(std::move(night));
    }
}

/// The lights of each group as they were taken: their exposures, their share of the run's integration time, and the
/// temperature, the gain and the offset their headers state.
void fillAcquisition(const ReportModel& model, ReportRun& run)
{
    qint64 grandTotalMs = 0;
    for (const ReportMaster& master : model.masters)
    {
        grandTotalMs += master.frames.empty() ? 0 : master.exposure.integrationMs;
    }
    for (const ReportMaster& master : model.masters)
    {
        if (master.frames.empty())
        {
            // A combined colour image is taken from no light of its own.
            continue;
        }
        ReportAcquisitionRow row;
        row.groupKey = master.key;
        row.group = master.name;
        row.target = master.target;
        row.filter = filterName(master);
        row.exposure = master.exposure;
        row.share = grandTotalMs > 0
                        ? static_cast<double>(master.exposure.integrationMs) / static_cast<double>(grandTotalMs)
                        : 0.0;
        for (const ReportFrame& frame : master.frames)
        {
            if (frame.temperature.has_value())
            {
                row.lowestTemperature =
                    (std::min)(row.lowestTemperature.value_or(*frame.temperature), *frame.temperature);
                row.highestTemperature =
                    (std::max)(row.highestTemperature.value_or(*frame.temperature), *frame.temperature);
            }
            if (!frame.gain.isEmpty() && !row.gain.contains(frame.gain))
            {
                row.gain.append(frame.gain);
            }
            if (!frame.offset.isEmpty() && !row.offset.contains(frame.offset))
            {
                row.offset.append(frame.offset);
            }
        }
        if (row.lowestTemperature.has_value())
        {
            run.lowestTemperature =
                (std::min)(run.lowestTemperature.value_or(*row.lowestTemperature), *row.lowestTemperature);
            run.highestTemperature =
                (std::max)(run.highestTemperature.value_or(*row.highestTemperature), *row.highestTemperature);
        }
        run.acquisition.push_back(std::move(row));
    }
}

/// The lights by what became of them, the ones the masters combine, and the exposure those add up to.
void fillFrameStates(const ReportModel& model, ReportRun& run)
{
    QMap<int, int> byState;
    int stated = 0;
    qint64 integratedMs = 0;
    const auto count = [&](const ReportFrame& frame) {
        const bool leftOutByUser =
            frame.state == ReportFrameState::ExcludedByUser || frame.state == ReportFrameState::StackExcludedByUser;
        if (leftOutByUser && frame.exposureSeconds.has_value())
        {
            // The groups' own lights are in the captured time already; a light the user left out is in no group.
            run.capturedMs += std::llround(*frame.exposureSeconds * 1000.0);
        }
        if (!frame.state.has_value())
        {
            return;
        }
        ++stated;
        ++byState[static_cast<int>(*frame.state)];
        if (*frame.state == ReportFrameState::Used && frame.exposureSeconds.has_value())
        {
            integratedMs += std::llround(*frame.exposureSeconds * 1000.0);
        }
    };
    for (const ReportMaster& master : model.masters)
    {
        for (const ReportFrame& frame : master.frames)
        {
            count(frame);
        }
    }
    for (const ReportGroupNotStacked& group : model.groupsNotStacked)
    {
        for (const ReportFrame& frame : group.frames)
        {
            count(frame);
        }
    }
    if (stated == 0)
    {
        return;
    }
    for (auto it = byState.cbegin(); it != byState.cend(); ++it)
    {
        run.frameStates.push_back(
            ReportFrameStateCount{.state = static_cast<ReportFrameState>(it.key()), .frames = it.value()});
    }
    run.framesUsed = byState.value(static_cast<int>(ReportFrameState::Used), 0);
    run.integratedMs = integratedMs;
}

/// The equipment the headers of the masters the run made state, one row per distinct set.
void fillEquipment(ReportBuild& build, const ReportModel& model, ReportRun& run)
{
    QHash<QString, std::size_t> rowByKey;
    for (const ReportMaster& master : model.masters)
    {
        if (master.state != ReportMasterState::Made || master.file.isEmpty())
        {
            continue;
        }
        const QString path = build.resolvePath(master.file);
        if (!build.exists(path))
        {
            continue;
        }
        const std::optional<ReportHeader> header = build.header(path);
        if (!header.has_value())
        {
            continue;
        }
        const an::ImageMetadata& metadata = header->metadata;

        ReportEquipmentRow row;
        if (header->width > 0 && header->height > 0)
        {
            row.outputResolution = QStringLiteral("%1 × %2")
                                       .arg(static_cast<qulonglong>(header->width))
                                       .arg(static_cast<qulonglong>(header->height));
        }
        if (const auto capture = parseResolutionDimensions(master.captureResolution); capture.has_value())
        {
            row.captureResolution = QStringLiteral("%1 × %2").arg(capture->first).arg(capture->second);
        }
        else if (!master.captureResolution.trimmed().isEmpty())
        {
            row.captureResolution = friendlyResolution(master.captureResolution.trimmed());
        }
        row.target = metadataObjectName(metadata);
        if (row.target.isEmpty())
        {
            row.target = master.target;
        }
        row.camera = metadataCameraModel(metadata);
        row.gain = formatIsoGainLabel(metadata);
        row.offset = formatOffsetLabel(metadata);
        row.telescope = metadataTelescope(metadata);
        row.site = formatLocationFromFits(metadata);
        if (row.camera.isEmpty() && row.gain.isEmpty() && row.offset.isEmpty() && row.telescope.isEmpty() &&
            row.site.isEmpty())
        {
            continue;
        }

        QString name = master.name.trimmed();
        if (name.isEmpty())
        {
            name = master.fileName.trimmed();
        }
        const QString key = QStringList{row.target, row.camera, row.captureResolution, row.outputResolution,
                                        row.gain,   row.offset, row.telescope,         row.site}
                                .join(QChar(0x1F));
        const auto found = rowByKey.constFind(key);
        if (found == rowByKey.constEnd())
        {
            row.masters = {name};
            rowByKey.insert(key, run.equipment.size());
            run.equipment.push_back(std::move(row));
        }
        else if (!run.equipment[*found].masters.contains(name))
        {
            run.equipment[*found].masters.append(name);
        }
    }
    for (ReportEquipmentRow& row : run.equipment)
    {
        row.masters.sort(Qt::CaseInsensitive);
    }
}

/// How many of the run's lights state each field the report follows, read from each light's header alone.
void fillHeaderCompleteness(ReportBuild& build, ReportRun& run)
{
    QStringList lights = collectInputFileSets(build.manifest()).lightFiles.values();
    if (lights.isEmpty() || build.sources().headers == nullptr)
    {
        return;
    }
    lights.sort(Qt::CaseInsensitive);

    int exposure = 0;
    int gain = 0;
    int offset = 0;
    int object = 0;
    int telescope = 0;
    int site = 0;
    int temperature = 0;
    int focalLength = 0;
    int read = 0;
    for (const QString& light : lights)
    {
        const std::optional<ReportHeader> header = build.header(light);
        if (!header.has_value())
        {
            continue;
        }
        ++read;
        const an::ImageMetadata& metadata = header->metadata;
        exposure += metadataStatesExposure(metadata) ? 1 : 0;
        gain += formatIsoGainLabel(metadata).isEmpty() ? 0 : 1;
        offset += formatOffsetLabel(metadata).isEmpty() ? 0 : 1;
        object += metadataObjectName(metadata).isEmpty() ? 0 : 1;
        telescope += metadataTelescope(metadata).isEmpty() ? 0 : 1;
        site += formatLocationFromFits(metadata).isEmpty() ? 0 : 1;
        temperature +=
            an::projectAcquisition(metadata.codec()).acquisition.sensorTemperatureCelsius.has_value() ? 1 : 0;
        focalLength += readFirstFitsValue(metadata, {QStringLiteral("FOCALLEN")}).isEmpty() ? 0 : 1;
    }
    if (read == 0)
    {
        // No header could be read: the fields are not stated, which is not the same as stated by no light.
        build.notStated(reportText("What the lights' headers state"),
                        build.sources().session == nullptr
                            ? reportText("The report was built without an engine session, so no header was read.")
                            : reportText("None of the lights' files could be read."));
        return;
    }

    ReportHeaderCompleteness completeness;
    completeness.lights = static_cast<int>(lights.size());
    completeness.fields = {
        ReportHeaderField{.name = reportText("Exposure"), .present = exposure},
        ReportHeaderField{.name = reportText("ISO or gain"), .present = gain},
        ReportHeaderField{.name = reportText("Offset"), .present = offset},
        ReportHeaderField{.name = reportText("Target"), .present = object},
        ReportHeaderField{.name = reportText("Telescope"), .present = telescope},
        ReportHeaderField{.name = reportText("Site coordinates"), .present = site},
        ReportHeaderField{.name = reportText("Sensor temperature"), .present = temperature},
        ReportHeaderField{.name = reportText("Focal length"), .present = focalLength},
    };
    run.headers = std::move(completeness);
}

/// The run's targets, each with its lights, its filters, its masters and the mean of what its masters measured.
void fillTargets(const ReportBuild& build, ReportModel& model)
{
    struct Aggregate
    {
        int frames = 0;
        qint64 integrationMs = 0;
        QSet<QString> filters;
        QSet<QString> masters;
        QList<double> snr;
        QList<double> overlap;
    };
    QHash<QString, const ReportMaster*> masterByKey;
    for (const ReportMaster& master : model.masters)
    {
        masterByKey.insert(master.key, &master);
    }

    QMap<QString, Aggregate> byTarget;
    for (const auto& group : build.manifest().integrationGroups)
    {
        QString target = resolveTargetName(group);
        if (target.isEmpty())
        {
            target = reportText("Unspecified Target");
        }
        Aggregate& aggregate = byTarget[target];
        aggregate.frames += static_cast<int>(group.lightFiles.size());
        aggregate.integrationMs += buildExposureBreakdown(group).totalMs;
        if (!group.filter.trimmed().isEmpty())
        {
            aggregate.filters.insert(group.filter.trimmed());
        }
        if (!group.outputFilename.trimmed().isEmpty())
        {
            aggregate.masters.insert(group.outputFilename.trimmed());
        }
        const ReportMaster* master = masterByKey.value(group.groupKey.toString(), nullptr);
        if (master == nullptr || master->state != ReportMasterState::Made)
        {
            continue;
        }
        if (!master->fileName.trimmed().isEmpty())
        {
            aggregate.masters.insert(master->fileName.trimmed());
        }
        if (master->snapshot.snr.has_value())
        {
            aggregate.snr.append(*master->snapshot.snr);
        }
        if (master->overlapRetained.has_value())
        {
            aggregate.overlap.append(*master->overlapRetained);
        }
    }

    const auto sorted = [](const QSet<QString>& values) {
        QStringList list = values.values();
        list.sort(Qt::CaseInsensitive);
        return list;
    };
    model.targets.reserve(static_cast<std::size_t>(byTarget.size()));
    for (auto it = byTarget.cbegin(); it != byTarget.cend(); ++it)
    {
        ReportTarget target;
        target.name = it.key();
        target.frames = it->frames;
        target.integrationMs = it->integrationMs;
        target.filters = sorted(it->filters);
        target.masters = sorted(it->masters);
        target.snr = averageFiniteValues(it->snr);
        target.overlapRetained = averageFiniteValues(it->overlap);
        model.targets.push_back(std::move(target));
    }
}

/// How the run ended. A run that stopped is partial when it made a master, since those are kept; a run that
/// completed is partial when a group failed inside it.
[[nodiscard]] ReportOutcome outcomeOf(const WorkflowExecutor::Result& result, const ReportGroupCounts& groups)
{
    if (result.cancelled)
    {
        return ReportOutcome::Cancelled;
    }
    if (!result.success)
    {
        return groups.made > 0 ? ReportOutcome::Partial : ReportOutcome::Failed;
    }
    if (result.partialSuccess || groups.failed > 0)
    {
        return ReportOutcome::Partial;
    }
    if (result.warningCount > 0 || !result.warnings.isEmpty())
    {
        return ReportOutcome::CompletedWithWarnings;
    }
    return ReportOutcome::Completed;
}

[[nodiscard]] ReportGroupCounts countGroups(const ReportModel& model)
{
    ReportGroupCounts counts;
    counts.total = static_cast<int>(model.masters.size());
    for (const ReportMaster& master : model.masters)
    {
        switch (master.state)
        {
        case ReportMasterState::Made:
            ++counts.made;
            break;
        case ReportMasterState::Reused:
            ++counts.reused;
            break;
        case ReportMasterState::Failed:
            ++counts.failed;
            break;
        case ReportMasterState::NotReached:
        case ReportMasterState::Cancelled:
            ++counts.notReached;
            break;
        }
    }
    return counts;
}
} // namespace

void fillRun(ReportBuild& build, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();
    const WorkflowExecutor::Result& result = build.result();
    const QJsonObject& context = build.inputs().context;
    ReportRun& run = model.run;

    run.title = reportTitle(manifest);
    run.process = reportProcessLabel(manifest);
    run.profile = manifest.processingProfileLabel.trimmed();
    run.profileId = manifest.processingProfileId.trimmed();
    run.targets = collectTargets(manifest);

    run.mode = manifest.executionMode == ExecutionMode::Incremental        ? reportText("Incremental")
               : manifest.executionMode == ExecutionMode::RegistrationOnly ? reportText("Registration Only")
                                                                           : reportText("Full");
    run.scope =
        manifest.executionScope.trimmed().isEmpty() ? QStringLiteral("full") : manifest.executionScope.trimmed();
    run.scopeKey = manifest.executionScopeKey.trimmed();
    run.scopeName = friendlyGroupKey(run.scopeKey);
    run.incremental = manifest.executionMode == ExecutionMode::Incremental;
    for (const auto& changed : manifest.incrementalIntegrationGroups)
    {
        run.changedGroups.append(friendlyGroupKey(changed.toString()));
    }

    run.groups = countGroups(model);
    for (const auto& group : manifest.integrationGroups)
    {
        const ExposureBreakdown breakdown = buildExposureBreakdown(group);
        run.frames += breakdown.totalFrames;
        run.capturedMs += breakdown.totalMs;
    }
    // The lights the user left out were taken too; their exposure joins the captured time with their states.
    run.frames += static_cast<int>(manifest.leftOutLights.size());
    run.warnings = result.warningCount > 0 ? result.warningCount : static_cast<int>(result.warnings.size());
    run.errors = static_cast<int>(result.errors.size());

    run.processingMs = result.totalDurationMs;
    if (result.preparationTiming.hasMeasurements())
    {
        run.preparation = ReportPreparationTiming{.manifestBuildMs = result.preparationTiming.manifestBuildMs,
                                                  .planAndValidationMs = result.preparationTiming.planAndValidationMs};
    }
    if (result.executorTiming.captured)
    {
        run.executor = ReportExecutorTiming{.stageExecutionMs = result.executorTiming.stageExecutionMs,
                                            .stateCommitMs = result.executorTiming.stateCommitMs,
                                            .archiveMs = result.executorTiming.archiveMs,
                                            .reportMs = result.executorTiming.reportMs};
    }
    fillCounters(build, run);
    fillResources(build, run);
    fillSessionNotes(build, run);
    fillNights(model, run);
    fillAcquisition(model, run);
    fillFrameStates(model, run);
    fillEquipment(build, model, run);
    fillHeaderCompleteness(build, run);
    if (!manifest.integrationGroups.empty())
    {
        run.captureResolution = manifest.integrationGroups.front().resolution.trimmed();
    }

    IReportHost* host = build.sources().host;
    run.applicationVersion = resolveAppVersion(context);
    run.executionId = resolveContextValue(context, QStringLiteral("executionId"), manifest.executionId);
    run.archivedAt = archivedAt(context);
    if (!run.archivedAt.has_value() && host != nullptr)
    {
        run.archivedAt = host->now();
    }
    run.timeZoneId =
        resolveContextValue(context, QStringLiteral("timeZoneId"), host != nullptr ? host->timeZoneId() : QString());
    run.utcOffsetSeconds = context.value(QStringLiteral("utcOffsetSeconds")).isDouble()
                               ? context.value(QStringLiteral("utcOffsetSeconds")).toInt()
                           : run.archivedAt.has_value() ? run.archivedAt->offsetFromUtc()
                                                        : 0;
    run.platform = host != nullptr ? host->platform() : QString();
    run.archiveFormat = context.value(QStringLiteral("schemaVersion")).toInt(0);
    run.manifestFormat = manifest.version;
    run.fingerprint =
        computeReproducibilityFingerprint(manifest, context, &run.settingsFingerprint, &run.inputsFingerprint);
    run.outputFolder = manifest.outputDirectory;
    run.log = result.capturedLog;

    run.outcome = outcomeOf(result, run.groups);
    run.verdict = reportVerdict(run.outcome, run.groups);

    fillStages(build, model);
    fillTargets(build, model);
}

void fillDisk(ReportBuild& build, ReportModel& model)
{
    const auto add = [&model](const QString& kind, const qint64 bytes, const int files) {
        if (files > 0)
        {
            model.run.disk.push_back(ReportDiskUse{.kind = kind, .bytes = bytes, .files = files});
        }
    };

    qint64 masterBytes = 0;
    int masters = 0;
    qint64 mapBytes = 0;
    int maps = 0;
    for (const ReportMaster& master : model.masters)
    {
        if (master.state != ReportMasterState::Made)
        {
            continue;
        }
        if (master.bytes.has_value())
        {
            masterBytes += *master.bytes;
            ++masters;
        }
        for (const ReportMap& map : master.maps)
        {
            if (map.bytes.has_value())
            {
                mapBytes += *map.bytes;
                ++maps;
            }
        }
    }
    add(reportText("Masters"), masterBytes, masters);
    add(reportText("Maps"), mapBytes, maps);

    qint64 calibrationBytes = 0;
    int calibrationMasters = 0;
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        if (master.state == ReportCalibrationState::Made && master.bytes.has_value())
        {
            calibrationBytes += *master.bytes;
            ++calibrationMasters;
        }
    }
    add(reportText("Calibration masters"), calibrationBytes, calibrationMasters);

    // The registered lights the folder holds: the size the registration recorded of each whose file is there.
    qint64 registeredBytes = 0;
    int registered = 0;
    QSet<QString> counted;
    for (const auto& group : build.manifest().registrationGroups)
    {
        const QHash<QString, RegistrationMetricRow>& rows = build.metrics(group.groupKey.toString());
        for (auto row = rows.cbegin(); row != rows.cend(); ++row)
        {
            if (row->outputPath.trimmed().isEmpty() || row->outputSizeBytes <= 0 || counted.contains(row->outputPath) ||
                !build.exists(row->outputPath))
            {
                continue;
            }
            counted.insert(row->outputPath);
            registeredBytes += row->outputSizeBytes;
            ++registered;
        }
    }
    add(reportText("Registered lights"), registeredBytes, registered);
}
} // namespace ImageIntegration::Reporting
