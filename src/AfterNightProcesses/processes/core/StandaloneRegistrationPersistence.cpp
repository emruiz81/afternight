// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StandaloneRegistrationPersistence.h"

#include "processes/core/RegistrationMetricsStateLoader.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationWorkState.h"

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace
{
void assignError(QString* error, const QString& value)
{
    if (error != nullptr)
    {
        *error = value;
    }
}

QString metricsDatabasePathFor(const QString& outputRoot)
{
    return QDir(outputRoot).filePath(QStringLiteral("registration_metrics.db"));
}

QString canonicalFramePath(const QString& path)
{
    try
    {
        return FrameId(path).canonicalPath();
    }
    catch (...)
    {
        return {};
    }
}

std::vector<FrameId> collectFrameIds(const std::vector<FrameInfo>& images)
{
    std::vector<FrameId> frameIds;
    frameIds.reserve(images.size());
    for (const FrameInfo& frame : images)
    {
        if (frame.id.isValid())
        {
            frameIds.push_back(frame.id);
        }
    }
    return frameIds;
}

QString fallbackStandaloneGroupKey(const QString& lightFilesHash)
{
    return QStringLiteral("standalone_registration|%1").arg(lightFilesHash.left(16));
}

QString registeredImageInputPath(const an::ui::RegisteredFrame& image, const QString& frameId)
{
    return image.sourcePath.isEmpty() ? frameId : image.sourcePath;
}

qint64 fileMtimeSecs(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
    {
        return 0;
    }

    return info.lastModified().toSecsSinceEpoch();
}

std::unordered_map<QString, const an::ui::RegisteredFrame*>
registeredImagesByCanonicalPath(const an::ui::RegisteredFrameSet& registeredData)
{
    std::unordered_map<QString, const an::ui::RegisteredFrame*> out;
    out.reserve(registeredData.frames.size() * 2);
    for (const auto& [path, image] : registeredData.frames)
    {
        const QString key = canonicalFramePath(path);
        if (!key.isEmpty())
        {
            out.emplace(key, &image);
        }

        const QString sourceKey = canonicalFramePath(image.sourcePath);
        if (!sourceKey.isEmpty())
        {
            out.emplace(sourceKey, &image);
        }
    }

    return out;
}

std::unordered_map<QString, an::ui::RegisteredFrame>
buildRegisteredImageFrameMap(const std::vector<FrameId>& currentFrames,
                             const an::ui::RegisteredFrameSet& registeredData)
{
    std::unordered_map<QString, an::ui::RegisteredFrame> out;
    const auto registeredByKey = registeredImagesByCanonicalPath(registeredData);
    out.reserve(currentFrames.size());
    for (const FrameId& frame : currentFrames)
    {
        const QString frameId = frame.canonicalPath();
        if (frameId.isEmpty())
        {
            continue;
        }

        const auto it = registeredByKey.find(frameId);
        if (it != registeredByKey.end() && it->second != nullptr)
        {
            out.emplace(frameId, *(it->second));
        }
    }

    return out;
}

an::ui::RegistrationMetricRecord buildMetricRecord(const an::ui::StandaloneRegistrationPersistenceContext& context,
                                                   const FrameId& frameId, const an::ui::RegisteredFrame* image)
{
    an::ui::RegistrationMetricRecord metric;
    metric.frameId = frameId;
    metric.groupKey = context.groupKey;
    metric.settingsVersion = context.settingsVersion;
    metric.outputPath.clear();

    if (image == nullptr)
    {
        metric.metrics.succeeded = false;
        return metric;
    }

    metric.metrics = image->metrics;
    metric.metrics.stars = image->stars.empty() ? image->metrics.stars : static_cast<int>(image->stars.size());
    metric.metrics.succeeded = !image->failed;
    metric.noise = image->noise;
    if (image->transform.has_value())
    {
        metric.transformJson = stage_utils::serializeTransformJson(*image->transform, image->distortion);
    }

    return metric;
}

an::ui::RegistrationStarCacheEntry buildStarCacheEntry(const an::ui::StandaloneRegistrationPersistenceContext& context,
                                                       const FrameId& frameId, const an::ui::RegisteredFrame* image)
{
    an::ui::RegistrationStarCacheEntry entry;
    entry.frameId = frameId;
    entry.groupKey = context.groupKey;
    entry.detectionHash = context.detectionHash;
    entry.inputPath = image ? registeredImageInputPath(*image, frameId.canonicalPath()) : frameId.canonicalPath();
    entry.inputMtime = fileMtimeSecs(entry.inputPath);
    const QFileInfo inputInfo(entry.inputPath);
    entry.inputSizeBytes = inputInfo.exists() && inputInfo.isFile() ? inputInfo.size() : -1;
    entry.inputModifiedAtMs =
        inputInfo.exists() && inputInfo.isFile() ? inputInfo.lastModified().toMSecsSinceEpoch() : -1;
    entry.inputIdentityToken = inputInfo.exists() && inputInfo.isFile()
                                   ? ArtifactTracker::fileIdentityForPath(inputInfo.absoluteFilePath())
                                   : QString();
    if (image != nullptr)
    {
        entry.starsCount = image->stars.empty() ? image->metrics.stars : static_cast<int>(image->stars.size());
        entry.fwhm = image->metrics.fwhm;
        entry.snr = image->metrics.snr;
        entry.roundness = std::clamp(1.0 - image->metrics.eccentricity, 0.0, 1.0);
        entry.noise = image->noise;
        entry.starsBlob = an::ui::serializeRegistrationStarCacheBlob(image->stars);
    }
    else
    {
        entry.starsBlob = an::ui::serializeRegistrationStarCacheBlob({});
    }
    return entry;
}

bool canReusePersistedStarData(const RegistrationWorkAnalysis& analysis)
{
    if (!analysis.needsWork)
    {
        return true;
    }

    if (analysis.reasons.empty())
    {
        return false;
    }

    for (const auto& reason : analysis.reasons)
    {
        switch (reason.code)
        {
        case RegistrationWorkReasonCode::RegistrationSettingsChanged:
        case RegistrationWorkReasonCode::ReferenceChanged:
        case RegistrationWorkReasonCode::FrameListChanged:
        case RegistrationWorkReasonCode::MissingMetrics:
        case RegistrationWorkReasonCode::FailedMetrics:
            break;

        case RegistrationWorkReasonCode::NoPreviousState:
        case RegistrationWorkReasonCode::DetectionSettingsChanged:
        case RegistrationWorkReasonCode::CalibrationChanged:
        case RegistrationWorkReasonCode::SettingsVersionChanged:
        case RegistrationWorkReasonCode::StarCacheUnavailable:
        case RegistrationWorkReasonCode::MissingCachedStars:
        case RegistrationWorkReasonCode::ChangedInputs:
            return false;
        }
    }

    return true;
}
} // namespace

namespace an::ui
{
std::optional<StandaloneRegistrationPersistenceContext>
buildStandaloneRegistrationPersistenceContext(const QString& outputRoot, const std::vector<FrameInfo>& images,
                                              const RegistrationSettings& settings, QString* error)
{
    assignError(error, QString());

    const QString trimmedRoot = outputRoot.trimmed();
    if (trimmedRoot.isEmpty())
    {
        assignError(error, QStringLiteral("Registration output directory is empty."));
        return std::nullopt;
    }

    const auto frameIds = collectFrameIds(images);
    if (frameIds.empty())
    {
        assignError(error, QStringLiteral("No registration frames available."));
        return std::nullopt;
    }

    StandaloneRegistrationPersistenceContext context;
    context.outputRoot = trimmedRoot;
    context.databasePath = metricsDatabasePathFor(trimmedRoot);
    context.settingsVersion = 0;
    context.detectionHash = ImageIntegration::RegistrationHashUtils::registrationDetectionHash(settings);
    context.registrationHash = ImageIntegration::RegistrationHashUtils::registrationTransformHash(settings);
    context.referenceFrameId = canonicalFramePath(settings.referenceImage.trimmed());
    if (context.referenceFrameId.isEmpty())
    {
        LOG_WARNING("StandaloneRegistrationPersistence: canonicalFramePath failed for '{}', "
                    "using raw path as reference frame ID.",
                    settings.referenceImage.trimmed().toStdString());
        context.referenceFrameId = settings.referenceImage.trimmed();
    }
    context.lightFilesHash = ImageIntegration::RegistrationHashUtils::hashFrameIdList(frameIds);
    context.calibrationHash.clear();
    context.outputWcsJson.clear();

    std::unordered_set<QString> uniqueGroupKeys;
    uniqueGroupKeys.reserve(images.size());
    for (const FrameInfo& frame : images)
    {
        if (!frame.id.isValid())
        {
            continue;
        }

        try
        {
            const auto groupKey = RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(frame));
            uniqueGroupKeys.insert(groupKey.toString());
        }
        catch (const std::exception& ex)
        {
            LOG_WARNING("StandaloneRegistrationPersistence: failed to derive registration group key: {}", ex.what());
        }
        catch (...)
        {
            LOG_WARNING("StandaloneRegistrationPersistence: failed to derive registration group key.");
        }
    }

    if (uniqueGroupKeys.size() == 1)
    {
        context.groupKey = *uniqueGroupKeys.begin();
        context.interoperableGroup = true;
    }
    else
    {
        context.groupKey = fallbackStandaloneGroupKey(context.lightFilesHash);
        context.interoperableGroup = false;
    }

    return context;
}

bool standaloneRegistrationStateMatchesContext(const StandaloneRegistrationPersistenceContext& context,
                                               const RegistrationGroupState& state)
{
    return state.groupKey == context.groupKey && state.settingsVersion == context.settingsVersion &&
           state.detectionHash == context.detectionHash && state.registrationHash == context.registrationHash &&
           state.referenceFrameId == context.referenceFrameId && state.lightFilesHash == context.lightFilesHash &&
           state.calibrationHash == context.calibrationHash;
}

bool persistStandaloneRegistrationState(const StandaloneRegistrationPersistenceContext& context,
                                        const std::vector<FrameInfo>& images, const RegisteredFrameSet& registeredData,
                                        QString* error)
{
    assignError(error, QString());

    if (registeredData.frames.empty())
    {
        assignError(error, QStringLiteral("Registration state is empty."));
        return false;
    }

    RegistrationMetricsWriter writer(context.databasePath, QStringLiteral("StandaloneRegistrationWriter"));
    if (!writer.initialize(error))
    {
        return false;
    }

    if (!writer.deleteAllForGroup(context.groupKey, error))
    {
        return false;
    }

    RegistrationGroupState groupState;
    groupState.groupKey = context.groupKey;
    groupState.settingsVersion = context.settingsVersion;
    groupState.detectionHash = context.detectionHash;
    groupState.registrationHash = context.registrationHash;
    groupState.referenceFrameId = context.referenceFrameId;
    groupState.lightFilesHash = context.lightFilesHash;
    groupState.calibrationHash = context.calibrationHash;
    groupState.outputWcsJson = context.outputWcsJson;

    const auto registeredByKey = registeredImagesByCanonicalPath(registeredData);
    for (const FrameInfo& frame : images)
    {
        if (!frame.id.isValid())
        {
            continue;
        }

        const FrameId& frameId = frame.id;
        const auto it = registeredByKey.find(frameId.canonicalPath());
        const RegisteredFrame* image = (it != registeredByKey.end()) ? it->second : nullptr;

        const auto starCache = buildStarCacheEntry(context, frameId, image);
        if (!writer.upsertStarCache(starCache, error))
        {
            return false;
        }

        const auto metric = buildMetricRecord(context, frameId, image);
        if (!writer.upsertMetric(metric, error))
        {
            return false;
        }
    }

    // The group's state is what makes its records read as a registration, so it is written once every frame's are:
    // a write refused partway leaves frames no registration claims.
    return writer.upsertGroupState(groupState, error);
}

std::optional<StandaloneRegistrationPersistenceState>
loadStandaloneRegistrationState(const StandaloneRegistrationPersistenceContext& context,
                                const std::vector<FrameInfo>& currentImages, QString* error)
{
    assignError(error, QString());

    if (!QFileInfo::exists(context.databasePath))
    {
        return std::nullopt;
    }

    RegistrationMetricsStateLoader loader(context.databasePath, QStringLiteral("StandaloneRegistrationLoader"));
    if (!loader.open(error))
    {
        return std::nullopt;
    }

    std::vector<FrameId> currentFrames = collectFrameIds(currentImages);
    auto loaded = loader.loadState(context.groupKey, context.settingsVersion, currentFrames, error);
    if (!loaded.has_value())
    {
        return std::nullopt;
    }

    StandaloneRegistrationPersistenceState state;
    state.context = context;
    state.groupState = loaded->groupState;
    state.frameMetrics = loaded->frameMetrics;
    state.registeredData = loaded->registeredFrames;
    if (state.groupState.has_value())
    {
        state.context.detectionHash = state.groupState->detectionHash;
        state.context.registrationHash = state.groupState->registrationHash;
        state.context.referenceFrameId = state.groupState->referenceFrameId;
        state.context.lightFilesHash = state.groupState->lightFilesHash;
        state.context.calibrationHash = state.groupState->calibrationHash;
        state.context.outputWcsJson = state.groupState->outputWcsJson;
        state.context.settingsVersion = state.groupState->settingsVersion;
        state.context.groupKey = state.groupState->groupKey;
    }

    state.metricsByFrameId.reserve(state.frameMetrics.size());
    for (const auto& metric : state.frameMetrics)
    {
        state.metricsByFrameId.emplace(metric.frameId.canonicalPath(), metric);
    }

    state.registeredImagesByFrameId = buildRegisteredImageFrameMap(currentFrames, state.registeredData);
    if (state.registeredImagesByFrameId.empty() && !state.frameMetrics.empty())
    {
        std::vector<FrameId> recordedFrames;
        recordedFrames.reserve(state.frameMetrics.size());
        for (const auto& metric : state.frameMetrics)
        {
            recordedFrames.push_back(metric.frameId);
        }
        state.registeredImagesByFrameId = buildRegisteredImageFrameMap(recordedFrames, state.registeredData);
    }

    return state;
}

StandaloneRegistrationReuseCheck
checkReusableStandaloneRegistrationState(const StandaloneRegistrationPersistenceContext& context,
                                         const std::vector<FrameInfo>& currentImages,
                                         const std::unordered_set<QString>& ignoredFrameIds, QString* error)
{
    assignError(error, QString());

    StandaloneRegistrationReuseCheck result;

    if (!QFileInfo::exists(context.databasePath))
    {
        result.reason = QStringLiteral("No persisted registration database found.");
        return result;
    }

    core::SqliteConnection connection(context.databasePath, QStringLiteral("StandaloneRegistrationReuse"));
    if (!connection.open(error))
    {
        result.reason = (error != nullptr) ? *error : QStringLiteral("Failed to open metrics database.");
        return result;
    }

    RegistrationWorkCurrentState currentState;
    currentState.groupKey = context.groupKey;
    currentState.settingsVersion = context.settingsVersion;
    currentState.detectionHash = context.detectionHash;
    currentState.registrationHash = context.registrationHash;
    currentState.referenceFrameId = context.referenceFrameId;
    currentState.lightFilesHash = context.lightFilesHash;
    currentState.lightFiles = collectFrameIds(currentImages);
    currentState.ignoredFrameIds = ignoredFrameIds;

    const RegistrationWorkAnalysis analysis = analyzeRegistrationWork(connection.db(), currentState);
    result.reusable = !analysis.needsWork;
    result.starDataReusable = canReusePersistedStarData(analysis);
    if (analysis.reasons.empty())
    {
        result.reason = QStringLiteral("Registration metrics state is reusable.");
    }
    else
    {
        result.reason = describeRegistrationWorkReason(analysis.reasons.front());
    }
    return result;
}

bool clearStandaloneRegistrationState(const StandaloneRegistrationPersistenceContext& context, QString* error)
{
    assignError(error, QString());

    if (!QFileInfo::exists(context.databasePath))
    {
        return true; // nothing to clear — do not create the file
    }

    RegistrationMetricsWriter writer(context.databasePath, QStringLiteral("StandaloneRegistrationClear"));
    if (!writer.initialize(error))
    {
        return false;
    }

    return writer.deleteAllForGroup(context.groupKey, error);
}
} // namespace an::ui
