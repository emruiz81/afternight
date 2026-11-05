// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StandaloneStackingUtils.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/CometStackingProductResolver.h"
#include "processes/core/RegistrationMetricsPersistenceTypes.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <AfterNightEngine/IO/CodecFormat.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace
{
struct StandaloneStackingFrameDescriptor
{
    an::ui::StandaloneStackingFrame frame;
    QString path;
    QString canonicalPath;
    QString filter;
    QString session;
    double exposureSeconds = 0.0;
};

struct LatestMetricRecord
{
    an::ui::RegistrationMetricRecord record;
    int order = -1;
};

QString normalizeMetadataField(QString value, const QString& fallback = QString())
{
    value = value.trimmed();
    if (value.isEmpty() || value == QStringLiteral("-") ||
        value.compare(QStringLiteral("N/A"), Qt::CaseInsensitive) == 0)
    {
        return fallback;
    }

    return value;
}

QString formatExposureToken(const double exposureSeconds)
{
    if (!(exposureSeconds > 0.0) || !std::isfinite(exposureSeconds))
    {
        return QStringLiteral("unknown");
    }

    if (std::abs(exposureSeconds - std::round(exposureSeconds)) < 1e-6)
    {
        return QStringLiteral("%1s").arg(static_cast<int>(std::round(exposureSeconds)));
    }

    return QStringLiteral("%1s").arg(exposureSeconds, 0, 'f', exposureSeconds >= 10.0 ? 1 : 2);
}

QString formatExposureDisplay(const double exposureSeconds)
{
    if (!(exposureSeconds > 0.0) || !std::isfinite(exposureSeconds))
    {
        return QStringLiteral("-");
    }

    if (std::abs(exposureSeconds - std::round(exposureSeconds)) < 1e-6)
    {
        return QStringLiteral("%1 s").arg(static_cast<int>(std::round(exposureSeconds)));
    }

    return QStringLiteral("%1 s").arg(exposureSeconds, 0, 'f', exposureSeconds >= 10.0 ? 1 : 2);
}

QString trimRepeatedUnderscores(QString value)
{
    while (value.contains(QStringLiteral("__")))
    {
        value.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    while (value.startsWith(QLatin1Char('_')))
    {
        value.remove(0, 1);
    }
    while (value.endsWith(QLatin1Char('_')))
    {
        value.chop(1);
    }
    return value;
}

QString safeTemplateValue(QString value)
{
    value = ImageIntegration::sanitizeFilename(value.trimmed());
    value = trimRepeatedUnderscores(value);
    return value;
}

bool isWritableImageOutputPath(const QString& path)
{
    switch (an::codecFormatFromPath(path.toUtf8().toStdString()))
    {
    case an::CodecFormat::Fits:
    case an::CodecFormat::Tiff:
    case an::CodecFormat::Png:
    case an::CodecFormat::Jpeg:
    case an::CodecFormat::Xisf:
        return true;
    case an::CodecFormat::Unknown:
    case an::CodecFormat::Raw:
    case an::CodecFormat::Dump:
        return false;
    }

    return false;
}

std::vector<StandaloneStackingFrameDescriptor>
makeFrameDescriptors(const std::vector<an::ui::StandaloneStackingFrame>& images)
{
    std::vector<StandaloneStackingFrameDescriptor> descriptors;
    descriptors.reserve(images.size());

    for (const auto& frame : images)
    {
        const QString& path = frame.entry.path;
        if (path.isEmpty())
        {
            continue;
        }

        descriptors.push_back(StandaloneStackingFrameDescriptor{
            frame, path, an::ui::registrationCanonicalFrameKey(path),
            normalizeMetadataField(frame.filter, QStringLiteral("Broadband")),
            an::ui::standaloneStackingSessionLabel(frame.capturedAtUtc, path), (std::max)(0.0, frame.exposureSeconds)});
    }

    std::sort(descriptors.begin(), descriptors.end(),
              [](const StandaloneStackingFrameDescriptor& lhs, const StandaloneStackingFrameDescriptor& rhs) {
                  if (lhs.filter != rhs.filter)
                  {
                      return lhs.filter < rhs.filter;
                  }
                  if (lhs.session != rhs.session)
                  {
                      return lhs.session < rhs.session;
                  }
                  if (lhs.exposureSeconds != rhs.exposureSeconds)
                  {
                      return lhs.exposureSeconds < rhs.exposureSeconds;
                  }
                  return lhs.path < rhs.path;
              });
    return descriptors;
}

bool descriptorsMatchGroup(const StandaloneStackingFrameDescriptor& frame, const an::ui::StandaloneStackingGroup& group,
                           const an::ui::StandaloneStackingGroupingOptions& options)
{
    if (options.byFilter && frame.filter != group.filter)
    {
        return false;
    }
    if (options.bySession && frame.session != group.session)
    {
        return false;
    }
    if (!options.byExposure)
    {
        return true;
    }

    if (!(frame.exposureSeconds > 0.0) || !(group.representativeExposureSeconds > 0.0))
    {
        return std::abs(frame.exposureSeconds - group.representativeExposureSeconds) < 1e-6;
    }

    const double tolerance = (std::max)(frame.exposureSeconds, group.representativeExposureSeconds) *
                             (std::max)(0.0, options.exposureToleranceFraction);
    return std::abs(frame.exposureSeconds - group.representativeExposureSeconds) <= tolerance;
}

QString groupLabelFor(const StandaloneStackingFrameDescriptor& frame,
                      const an::ui::StandaloneStackingGroupingOptions& options)
{
    QStringList parts;
    if (options.byFilter)
    {
        parts << frame.filter;
    }
    if (options.bySession)
    {
        parts << frame.session;
    }
    if (options.byExposure)
    {
        parts << formatExposureDisplay(frame.exposureSeconds);
    }

    return parts.isEmpty() ? QStringLiteral("All Frames") : parts.join(QStringLiteral("  •  "));
}

QString groupIdFor(const StandaloneStackingFrameDescriptor& frame,
                   const an::ui::StandaloneStackingGroupingOptions& options)
{
    QStringList parts;
    if (options.byFilter)
    {
        parts << QStringLiteral("filter=%1").arg(frame.filter);
    }
    if (options.bySession)
    {
        parts << QStringLiteral("session=%1").arg(frame.session);
    }
    if (options.byExposure)
    {
        parts << QStringLiteral("exposure=%1").arg(QString::number(frame.exposureSeconds, 'f', 6));
    }

    return parts.isEmpty() ? QStringLiteral("all") : parts.join(QStringLiteral("|"));
}

QString groupTokenFor(const an::ui::StandaloneStackingGroup& group)
{
    QString label = group.label;
    label.replace(QStringLiteral("  •  "), QStringLiteral("_"));
    return safeTemplateValue(label);
}

QString ensureUniqueOutputFileName(QString fileName, const an::ui::StandaloneStackingGroup& group,
                                   QSet<QString>& usedOutputNames)
{
    QString key = fileName.toCaseFolded();
    if (!usedOutputNames.contains(key))
    {
        usedOutputNames.insert(key);
        return fileName;
    }

    const QFileInfo info(fileName);
    const QString baseName = info.completeBaseName();
    const QString extension =
        info.suffix().trimmed().isEmpty() ? QStringLiteral("fit") : info.suffix().trimmed().toLower();
    const QString groupSuffix = [&group]() {
        const QString token = safeTemplateValue(groupTokenFor(group));
        return token.isEmpty() ? QStringLiteral("group") : token;
    }();

    constexpr int kMaxDedupAttempts = 1000;
    int duplicateIndex = 0;
    do
    {
        const QString suffix =
            duplicateIndex == 0 ? groupSuffix : QStringLiteral("%1_%2").arg(groupSuffix).arg(duplicateIndex + 1);
        fileName = QStringLiteral("%1_%2.%3").arg(baseName, suffix, extension);
        key = fileName.toCaseFolded();
        ++duplicateIndex;
    } while (usedOutputNames.contains(key) && duplicateIndex < kMaxDedupAttempts);

    usedOutputNames.insert(key);
    return fileName;
}

bool loadLatestMetricsByFrameId(const QString& databasePath,
                                std::unordered_map<QString, an::ui::RegistrationMetricRecord>& out, QString* error)
{
    out.clear();

    an::ui::RegistrationMetricsStateLoader loader(databasePath, QStringLiteral("StandaloneStackingLoader"));
    if (!loader.open(error))
    {
        return false;
    }

    std::vector<an::ui::RegistrationMetricsGroupVersion> states;
    if (!loader.loadAvailableStates(states, error))
    {
        return false;
    }

    int stateOrder = 0;
    std::unordered_map<QString, LatestMetricRecord> latestByFrame;
    for (const auto& state : states)
    {
        std::vector<an::ui::RegistrationMetricRecord> records;
        if (!loader.loadMetricRecords(state.groupKey, state.settingsVersion, records, error))
        {
            return false;
        }

        for (const auto& record : records)
        {
            const QString frameId = record.frameId.identityKey();
            if (frameId.isEmpty())
            {
                continue;
            }

            const auto it = latestByFrame.find(frameId);
            const bool replace = it == latestByFrame.end() || record.updatedAt > it->second.record.updatedAt ||
                                 (record.updatedAt == it->second.record.updatedAt && stateOrder >= it->second.order);
            if (replace)
            {
                latestByFrame[frameId] = LatestMetricRecord{record, stateOrder};
            }
        }

        ++stateOrder;
    }

    out.reserve(latestByFrame.size());
    for (auto& [frameId, latest] : latestByFrame)
    {
        out.emplace(frameId, std::move(latest.record));
    }

    return true;
}

QString normalizedStandaloneTransformRole(QString role)
{
    role = role.trimmed();
    if (role.isEmpty() || role.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0 ||
        role.compare(QStringLiteral("ordinary"), Qt::CaseInsensitive) == 0 ||
        role.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0)
    {
        return QStringLiteral("ordinary");
    }

    return role;
}

bool standaloneStackingModeRequestsComet(QString mode)
{
    return ImageIntegration::usesCometStackingMode(mode);
}

an::ui::StandaloneStackingTransformRoleResult failStandaloneTransformRole(QString role, QString error)
{
    an::ui::StandaloneStackingTransformRoleResult result;
    result.effectiveRole = std::move(role);
    result.error = std::move(error);
    return result;
}

/// What a person is told when only some of a set's frames carry the figures a weighting reads: how many lack them,
/// that the weighting cannot weigh the set, and the first frame without them.
QString partlyMeasuredWarning(const std::size_t missing, const std::size_t frames, const QString& figures,
                              const QString& weighting, const QString& firstPath)
{
    return QStringLiteral("%1 of %2 frames %3 missing usable %4 figures, so %5 weighting cannot weigh the set; "
                          "the first is '%6'.")
        .arg(missing)
        .arg(frames)
        .arg(missing == 1u ? QStringLiteral("is") : QStringLiteral("are"))
        .arg(figures)
        .arg(weighting)
        .arg(QFileInfo(firstPath).fileName());
}
} // namespace

namespace an::ui
{
QString standaloneStackingSessionLabel(const QDateTime& capturedAtUtc, const QString& filePath)
{
    if (capturedAtUtc.isValid())
    {
        return capturedAtUtc.toUTC().date().toString(QStringLiteral("yyyy-MM-dd"));
    }

    const QFileInfo info(filePath);
    if (info.exists())
    {
        return info.lastModified().toUTC().date().toString(QStringLiteral("yyyy-MM-dd"));
    }

    const QString parentName = info.dir().dirName().trimmed();
    return parentName.isEmpty() ? QStringLiteral("Session") : parentName;
}

std::vector<StandaloneStackingGroup> buildStandaloneStackingGroups(const std::vector<StandaloneStackingFrame>& images,
                                                                   const StandaloneStackingGroupingOptions& options,
                                                                   const QString& outputTemplate)
{
    std::vector<StandaloneStackingGroup> groups;
    const auto descriptors = makeFrameDescriptors(images);
    groups.reserve(descriptors.size());

    for (const auto& frame : descriptors)
    {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const StandaloneStackingGroup& group) {
            return descriptorsMatchGroup(frame, group, options);
        });

        if (it == groups.end())
        {
            StandaloneStackingGroup group;
            group.id = groupIdFor(frame, options);
            group.label = groupLabelFor(frame, options);
            group.filter = frame.filter;
            group.session = frame.session;
            group.representativeExposureSeconds = frame.exposureSeconds;
            group.frameCount = 0;
            group.totalExposureSeconds = 0.0;
            groups.push_back(std::move(group));
            it = std::prev(groups.end());
        }

        it->images.push_back(frame.frame);
        ++it->frameCount;
        it->totalExposureSeconds += frame.exposureSeconds;
        if (it->filter != frame.filter)
        {
            it->filter = QStringLiteral("Mixed");
        }
        if (it->session != frame.session)
        {
            it->session = QStringLiteral("Mixed");
        }
        if (frame.exposureSeconds > 0.0)
        {
            const double totalKnownExposure =
                (it->representativeExposureSeconds * static_cast<double>(it->frameCount - 1)) + frame.exposureSeconds;
            it->representativeExposureSeconds = totalKnownExposure / static_cast<double>(it->frameCount);
        }
    }

    QSet<QString> usedOutputNames;
    for (auto& group : groups)
    {
        group.outputFileName = ensureUniqueOutputFileName(resolveStandaloneStackingOutputName(outputTemplate, group),
                                                          group, usedOutputNames);
    }

    return groups;
}

QString resolveStandaloneStackingOutputName(const QString& outputTemplate, const StandaloneStackingGroup& group)
{
    QString fileName = outputTemplate.trimmed();
    if (fileName.isEmpty())
    {
        fileName = QStringLiteral("stacked_$filter.fit");
    }

    QString filterValue = group.filter.trimmed();
    if (filterValue.compare(QStringLiteral("Broadband"), Qt::CaseInsensitive) == 0)
    {
        filterValue.clear();
    }

    fileName.replace(QStringLiteral("$filter"), safeTemplateValue(filterValue));
    fileName.replace(QStringLiteral("$session"), safeTemplateValue(group.session));
    fileName.replace(QStringLiteral("$exposure"),
                     safeTemplateValue(formatExposureToken(group.representativeExposureSeconds)));
    fileName.replace(QStringLiteral("$group"), groupTokenFor(group));

    QFileInfo info(fileName);
    QString baseName = trimRepeatedUnderscores(ImageIntegration::sanitizeFilename(info.completeBaseName()));
    if (baseName.isEmpty())
    {
        baseName = QStringLiteral("stacked");
    }

    QString extension = info.suffix().trimmed().toLower();
    if (!isWritableImageOutputPath(fileName))
    {
        extension = QStringLiteral("fit");
    }

    return QStringLiteral("%1.%2").arg(baseName, extension);
}

bool standaloneStackingWeightingRequiresMetrics(StandaloneStackingWeightingMode mode) noexcept
{
    switch (mode)
    {
    case StandaloneStackingWeightingMode::Fwhm:
    case StandaloneStackingWeightingMode::Snr:
    case StandaloneStackingWeightingMode::Stars:
        return true;
    case StandaloneStackingWeightingMode::Equal:
    case StandaloneStackingWeightingMode::Noise:
    case StandaloneStackingWeightingMode::Exposure:
        return false;
    }

    return false;
}

StandaloneStackingWeightingMode standaloneStackingWeightingModeFromText(const QString& text) noexcept
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("fwhm"))
    {
        return StandaloneStackingWeightingMode::Fwhm;
    }
    if (normalized == QStringLiteral("weighted fwhm") || normalized == QStringLiteral("weighted_fwhm"))
    {
        return StandaloneStackingWeightingMode::Fwhm;
    }
    if (normalized == QStringLiteral("noise"))
    {
        return StandaloneStackingWeightingMode::Noise;
    }
    if (normalized == QStringLiteral("snr"))
    {
        return StandaloneStackingWeightingMode::Snr;
    }
    if (normalized == QStringLiteral("stars") || normalized == QStringLiteral("star count"))
    {
        return StandaloneStackingWeightingMode::Stars;
    }
    if (normalized == QStringLiteral("number of stars"))
    {
        return StandaloneStackingWeightingMode::Stars;
    }
    if (normalized == QStringLiteral("exposure") || normalized == QStringLiteral("exposure time"))
    {
        return StandaloneStackingWeightingMode::Exposure;
    }

    return StandaloneStackingWeightingMode::Equal;
}

QString standaloneStackingWeightingLabel(const StandaloneStackingWeightingMode mode)
{
    switch (mode)
    {
    case StandaloneStackingWeightingMode::Equal:
        return QStringLiteral("None (Equal)");
    case StandaloneStackingWeightingMode::Fwhm:
        return QStringLiteral("FWHM");
    case StandaloneStackingWeightingMode::Noise:
        return QStringLiteral("Noise");
    case StandaloneStackingWeightingMode::Snr:
        return QStringLiteral("SNR");
    case StandaloneStackingWeightingMode::Stars:
        return QStringLiteral("Stars");
    case StandaloneStackingWeightingMode::Exposure:
        return QStringLiteral("Exposure");
    }

    return QStringLiteral("None (Equal)");
}

StandaloneStackingTransformRoleResult
validateStandaloneStackingTransformRoleSelection(const StandaloneStackingTransformRoleRequest& request)
{
    const QString role = normalizedStandaloneTransformRole(request.requestedRole);
    const bool cometMode = standaloneStackingModeRequestsComet(request.stackingMode);

    if (!cometMode && role == QStringLiteral("ordinary"))
    {
        StandaloneStackingTransformRoleResult result;
        result.allowed = true;
        result.effectiveRole = role;
        return result;
    }

    if (cometMode || role.compare(QStringLiteral("comet_aligned"), Qt::CaseInsensitive) == 0)
    {
        const QString effective_role = role == QStringLiteral("ordinary") ? QStringLiteral("comet_aligned") : role;
        if (request.registrationMetricsDbPath.trimmed().isEmpty() ||
            !QFileInfo::exists(request.registrationMetricsDbPath))
        {
            return failStandaloneTransformRole(
                effective_role,
                QStringLiteral("Standalone comet stacking requires registration_metrics.db with an active "
                               "role-qualified %1 registration run.")
                    .arg(effective_role));
        }

        if (request.frameIds.empty())
        {
            return failStandaloneTransformRole(
                effective_role, QStringLiteral("Standalone comet stacking requires an explicit frame list."));
        }

        RegistrationMetricsStateLoader loader(request.registrationMetricsDbPath,
                                              QStringLiteral("StandaloneStackingCometResolver"));
        QString error;
        if (!loader.open(&error))
        {
            return failStandaloneTransformRole(
                effective_role,
                QStringLiteral("Failed to open registration metrics for standalone comet stacking: %1").arg(error));
        }

        std::vector<RegistrationMetricsGroupVersion> states;
        if (!loader.loadAvailableStates(states, &error) || states.empty())
        {
            return failStandaloneTransformRole(
                effective_role,
                error.trimmed().isEmpty()
                    ? QStringLiteral(
                          "Standalone comet stacking found no registration states in registration_metrics.db.")
                    : error);
        }

        std::sort(states.begin(), states.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.groupKey == rhs.groupKey)
            {
                return lhs.settingsVersion > rhs.settingsVersion;
            }
            return lhs.groupKey > rhs.groupKey;
        });

        QStringList attempted_contexts;
        for (const auto& state_version : states)
        {
            const auto state = loader.loadGroupState(state_version.groupKey, state_version.settingsVersion, &error);
            if (!state.has_value())
            {
                continue;
            }

            an::ui::CometStackingProductResolveRequest resolve_request;
            resolve_request.databasePath = request.registrationMetricsDbPath;
            resolve_request.groupKey = state->groupKey;
            resolve_request.registrationHash = state->registrationHash;
            resolve_request.lightFilesHash = state->lightFilesHash;
            resolve_request.calibrationHash = state->calibrationHash;
            resolve_request.resolvedReferenceFrameId = state->referenceFrameId;
            resolve_request.stackingMode = request.stackingMode;
            resolve_request.role = effective_role;
            resolve_request.drizzle = request.drizzle;
            resolve_request.frameIds = request.frameIds;

            const auto products = an::ui::resolveCometStackingProducts(resolve_request);
            if (products.available && products.kind == an::ui::CometStackingProductKind::RegisteredOutputs &&
                !products.registeredOutputs.empty())
            {
                StandaloneStackingTransformRoleResult result;
                result.allowed = true;
                result.effectiveRole = effective_role;
                result.resolvedInputPaths.reserve(products.registeredOutputs.size());
                for (const auto& output : products.registeredOutputs)
                {
                    result.resolvedInputPaths.push_back(output.outputPath);
                }
                return result;
            }

            attempted_contexts.push_back(products.errorMessage.trimmed().isEmpty()
                                             ? QStringLiteral("%1@%2 did not provide %3")
                                                   .arg(state->groupKey)
                                                   .arg(state->settingsVersion)
                                                   .arg(effective_role)
                                             : products.errorMessage);
        }

        return failStandaloneTransformRole(
            effective_role,
            QStringLiteral("Standalone comet stacking requires successful %1 registered outputs for every consumed "
                           "frame in an active exact comet run. %2")
                .arg(effective_role, attempted_contexts.join(QStringLiteral(" "))));
    }

    return failStandaloneTransformRole(
        role, QStringLiteral("Standalone transform-role stacking cannot consume unsupported role '%1' from "
                             "registration_metrics.db.")
                  .arg(role));
}

an::StackWeighting standaloneStackingWeightingSource(const StandaloneStackingWeightingMode mode) noexcept
{
    switch (mode)
    {
    case StandaloneStackingWeightingMode::Equal:
        return an::StackWeighting::Uniform;
    case StandaloneStackingWeightingMode::Fwhm:
        return an::StackWeighting::Fwhm;
    case StandaloneStackingWeightingMode::Noise:
        return an::StackWeighting::Noise;
    case StandaloneStackingWeightingMode::Snr:
        return an::StackWeighting::SignalToNoise;
    case StandaloneStackingWeightingMode::Stars:
        return an::StackWeighting::StarCount;
    case StandaloneStackingWeightingMode::Exposure:
        return an::StackWeighting::Exposure;
    }
    return an::StackWeighting::Uniform;
}

StandaloneStackingWeightingResult planStandaloneStackingWeights(const QString& registrationMetricsDbPath,
                                                                const std::vector<StandaloneStackingFrame>& images,
                                                                const StandaloneStackingWeightingMode mode)
{
    StandaloneStackingWeightingResult result;
    result.method = standaloneStackingWeightingSource(mode);
    if (mode == StandaloneStackingWeightingMode::Equal || mode == StandaloneStackingWeightingMode::Noise)
    {
        return result;
    }

    const auto fallBack = [&result](QString warning) {
        result.method = an::StackWeighting::Uniform;
        result.measurements.clear();
        result.usedFallback = true;
        result.warning = std::move(warning);
        return result;
    };

    result.measurements.resize(images.size());
    std::size_t measured = 0u;
    std::optional<std::size_t> firstMissing;
    if (mode == StandaloneStackingWeightingMode::Exposure)
    {
        for (std::size_t index = 0; index < images.size(); ++index)
        {
            const double exposure = images[index].exposureSeconds;
            if (exposure > 0.0 && std::isfinite(exposure))
            {
                result.measurements[index].exposureSeconds = exposure;
                ++measured;
            }
            else if (!firstMissing.has_value())
            {
                firstMissing = index;
            }
        }
        if (measured == 0u)
        {
            return fallBack(
                QStringLiteral("Exposure weighting could not derive valid exposure values; using equal weights."));
        }
    }
    else
    {
        const QFileInfo dbInfo(registrationMetricsDbPath);
        if (!dbInfo.exists())
        {
            return fallBack(QStringLiteral("Registration metrics were not found in this output directory, so %1 "
                                           "weighting is falling back to equal weights.")
                                .arg(standaloneStackingWeightingLabel(mode)));
        }

        std::unordered_map<QString, RegistrationMetricRecord> metricsByFrameId;
        QString error;
        if (!loadLatestMetricsByFrameId(registrationMetricsDbPath, metricsByFrameId, &error))
        {
            return fallBack(QStringLiteral("Registration metrics could not be loaded for %1 weighting, so the stack "
                                           "is using equal weights.")
                                .arg(standaloneStackingWeightingLabel(mode)));
        }

        for (std::size_t index = 0; index < images.size(); ++index)
        {
            const QString canonical = registrationCanonicalFrameKey(images[index].entry.path);
            const QString identity = canonical.isEmpty() ? QString() : FrameId(canonical).identityKey();
            const auto it = metricsByFrameId.find(identity);
            bool usable = false;
            if (it != metricsByFrameId.end() && it->second.metrics.succeeded)
            {
                const FrameQualityMetrics& metric = it->second.metrics;
                an::StackFrameMeasurement& measurement = result.measurements[index];
                switch (mode)
                {
                case StandaloneStackingWeightingMode::Fwhm:
                    if (metric.fwhm > 0.0 && std::isfinite(metric.fwhm))
                    {
                        measurement.fullWidth = metric.fwhm;
                        if (metric.stars > 0)
                        {
                            measurement.starCount = static_cast<std::uint32_t>(metric.stars);
                        }
                        usable = true;
                    }
                    break;
                case StandaloneStackingWeightingMode::Snr:
                    if (metric.snr > 0.0 && std::isfinite(metric.snr))
                    {
                        measurement.signalToNoise = metric.snr;
                        usable = true;
                    }
                    break;
                case StandaloneStackingWeightingMode::Stars:
                    if (metric.stars > 0)
                    {
                        measurement.starCount = static_cast<std::uint32_t>(metric.stars);
                        measurement.medianRoundness = std::clamp(1.0 - metric.eccentricity, 0.0, 1.0);
                        usable = true;
                    }
                    break;
                case StandaloneStackingWeightingMode::Equal:
                case StandaloneStackingWeightingMode::Noise:
                case StandaloneStackingWeightingMode::Exposure:
                    break;
                }
            }
            if (usable)
            {
                ++measured;
            }
            else
            {
                result.measurements[index] = an::StackFrameMeasurement{};
                if (!firstMissing.has_value())
                {
                    firstMissing = index;
                }
            }
        }
        if (measured == 0u)
        {
            return fallBack(QStringLiteral("Registration metrics are missing or incomplete for %1 weighting, so the "
                                           "stack is using equal weights.")
                                .arg(standaloneStackingWeightingLabel(mode)));
        }
    }

    if (firstMissing.has_value())
    {
        result.partlyMeasured = true;
        result.warning =
            partlyMeasuredWarning(images.size() - measured, images.size(),
                                  mode == StandaloneStackingWeightingMode::Exposure ? QStringLiteral("exposure")
                                                                                    : QStringLiteral("registration"),
                                  standaloneStackingWeightingLabel(mode), images[*firstMissing].entry.path);
    }
    return result;
}

StandaloneStackingWeightingResult standaloneNoiseWeights(const std::vector<StandaloneStackingFrame>& images,
                                                         const std::vector<std::optional<double>>& backgroundNoise)
{
    StandaloneStackingWeightingResult result;
    result.method = an::StackWeighting::Noise;
    result.measurements.resize(images.size());
    std::size_t measured = 0u;
    std::optional<std::size_t> firstMissing;
    for (std::size_t index = 0; index < images.size(); ++index)
    {
        const std::optional<double> noise = index < backgroundNoise.size() ? backgroundNoise[index] : std::nullopt;
        if (noise.has_value() && std::isfinite(*noise) && *noise > 0.0)
        {
            // The frame-level figure alone: no dispersion beside it, so the engine weighs the frame by one over its
            // noise squared, and no channel's own figure, so every channel takes that weight.
            result.measurements[index].backgroundNoise = *noise;
            ++measured;
        }
        else if (!firstMissing.has_value())
        {
            firstMissing = index;
        }
    }
    if (measured == 0u)
    {
        result.method = an::StackWeighting::Uniform;
        result.measurements.clear();
        result.usedFallback = true;
        result.warning =
            QStringLiteral("Noise weighting could not estimate usable background-noise values; using equal weights.");
        return result;
    }
    if (firstMissing.has_value())
    {
        result.partlyMeasured = true;
        result.warning = partlyMeasuredWarning(images.size() - measured, images.size(), QStringLiteral("noise"),
                                               standaloneStackingWeightingLabel(StandaloneStackingWeightingMode::Noise),
                                               images[*firstMissing].entry.path);
    }
    return result;
}
} // namespace an::ui
