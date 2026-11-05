// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "IntegrationDataModel.h"
#include "IntegrationGroupEngine.h"
#include "CalibrationMatcher.h"

#include "processes/ImageIntegration/core/RegistrationHashUtils.h"

#include <verdigris/wobjectimpl.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTimeZone>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <unordered_map>

W_OBJECT_IMPL(IntegrationDataModel)

namespace
{
constexpr int kModelJsonVersion = 4;

QStringList canonicalUniquePaths(const QStringList& paths)
{
    QStringList canonical_paths;
    canonical_paths.reserve(paths.size());

    QSet<QString> seen;
    for (const auto& raw_path : paths)
    {
        if (raw_path.trimmed().isEmpty())
        {
            continue;
        }

        const QString canonical = FrameId(raw_path).canonicalPath();
        if (canonical.isEmpty() || seen.contains(canonical))
        {
            continue;
        }

        seen.insert(canonical);
        canonical_paths.push_back(canonical);
    }

    return canonical_paths;
}

[[nodiscard]] GroupingOptions normalizeGroupingOptionsForFrameType(FrameType type, GroupingOptions options) noexcept
{
    // Resolution grouping is mandatory in V2.
    options.byResolution = true;

    // Keep model options consistent with what the UI actually exposes per frame type.
    // This prevents "hidden" options (e.g., Exposure for Flats) from affecting Hierarchical display mode.
    switch (type)
    {
    case FrameType::Bias:
        options.byFilter = false;
        options.byObject = false;
        options.bySession = false;
        options.byExposure = false;
        options.byTemperature = false;
        break;
    case FrameType::Dark:
        options.byFilter = false;
        options.byObject = false;
        options.bySession = false;
        break;
    case FrameType::Flat:
        options.byExposure = false;
        options.byTemperature = false;
        break;
    case FrameType::Light:
        options.byTemperature = false;
        break;
    }

    if (!options.byExposure)
    {
        options.exposureToleranceSeconds = 0.0;
    }
    if (!options.byTemperature)
    {
        options.temperatureToleranceC = 0.0;
    }

    return options;
}

[[nodiscard]] GroupKey groupKeyForFrame(FrameType type, const FrameInfo& frame, const GroupingOptions& options)
{
    if (frame.manualGroupKey.has_value())
    {
        return frame.manualGroupKey.value();
    }

    GroupingEngine engine;
    return engine.computeGroupKey(frame, type, options);
}

[[nodiscard]] bool shouldApplyMasterSelectionPolicy(MasterSource source) noexcept
{
    return source == MasterSource::UserDefined || source == MasterSource::AutoDetected;
}

struct ClearedMasterInfo
{
    FrameId id;
    MasterSource source = MasterSource::Unknown;
    GroupKey groupKey;
};

[[nodiscard]] std::optional<ClearedMasterInfo>
clearMasterForGroupIfPresent(QHash<FrameId, MasterSource>& masterSources, const FrameCollection& collection,
                             FrameType type, const GroupingOptions& options, const GroupKey& targetGroupKey,
                             const FrameId& keepSelectedId)
{
    std::optional<FrameId> to_remove;
    MasterSource to_remove_source = MasterSource::Unknown;

    for (auto it = masterSources.constBegin(); it != masterSources.constEnd(); ++it)
    {
        const FrameId master_id = it.key();
        const MasterSource source = it.value();
        if (master_id == keepSelectedId || !shouldApplyMasterSelectionPolicy(source))
        {
            continue;
        }

        const auto master_opt = collection.findById(master_id);
        if (!master_opt.has_value())
        {
            continue;
        }

        if (groupKeyForFrame(type, *master_opt, options) == targetGroupKey)
        {
            to_remove = master_id;
            to_remove_source = source;
            break;
        }
    }

    if (!to_remove.has_value())
    {
        return std::nullopt;
    }

    masterSources.remove(*to_remove);
    return ClearedMasterInfo{*to_remove, to_remove_source, targetGroupKey};
}

[[nodiscard]] std::vector<ClearedMasterInfo> clearConflictingMastersForType(QHash<FrameId, MasterSource>& masterSources,
                                                                            const FrameCollection& collection,
                                                                            FrameType type,
                                                                            const GroupingOptions& options)
{
    std::vector<ClearedMasterInfo> cleared;
    std::vector<FrameId> to_clear;

    for (auto it = masterSources.constBegin(); it != masterSources.constEnd(); ++it)
    {
        const FrameId master_id = it.key();
        const MasterSource source = it.value();
        if (!shouldApplyMasterSelectionPolicy(source))
        {
            continue;
        }

        const auto master_opt = collection.findById(master_id);
        if (!master_opt.has_value())
        {
            continue;
        }

        const GroupKey group_key = groupKeyForFrame(type, *master_opt, options);

        // If the master itself is excluded, it can't remain a master.
        bool conflict = master_opt->excluded;

        if (!conflict)
        {
            // If any other frame in the same group is included, the group is no longer "master-only".
            const auto group_frames = collection.filterByGroup(group_key, type, options);
            for (const auto& frame : group_frames)
            {
                if (!frame.excluded && frame.id != master_id)
                {
                    conflict = true;
                    break;
                }
            }
        }

        if (conflict)
        {
            to_clear.push_back(master_id);
            cleared.push_back(ClearedMasterInfo{master_id, source, group_key});
        }
    }

    for (const auto& id : to_clear)
    {
        masterSources.remove(id);
    }

    return cleared;
}

/// Lightweight master-path heuristic (mirrors FileImport::isProbablyMasterFilePath
/// but lives in the model layer so we can re-run detection without a UI dependency).
[[nodiscard]] bool isProbablyMasterPath(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    const auto containsMaster = [](const QString& value) { return value.toLower().contains(QStringLiteral("master")); };

    const QFileInfo info(filePath);
    if (containsMaster(info.fileName()))
    {
        return true;
    }

    QDir dir = info.dir();
    for (int i = 0; i < 3; ++i)
    {
        if (containsMaster(dir.dirName()))
        {
            return true;
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return false;
}

/// After clearing conflicting masters due to a grouping change, try to re-detect
/// masters for single-frame groups whose file path matches the master heuristic.
/// This restores masters that were lost when groups were merged and then re-split.
[[nodiscard]] std::vector<FrameId> redetectAutoMastersForType(QHash<FrameId, MasterSource>& masterSources,
                                                              const FrameCollection& collection, FrameType type,
                                                              const GroupingOptions& options)
{
    // Only calibration frame types can be masters.
    if (type == FrameType::Light)
    {
        return {};
    }

    // Build a set of group keys that already have a master.
    QSet<GroupKey> groupsWithMaster;
    for (auto it = masterSources.constBegin(); it != masterSources.constEnd(); ++it)
    {
        const auto frame_opt = collection.findById(it.key());
        if (!frame_opt)
        {
            continue;
        }
        groupsWithMaster.insert(groupKeyForFrame(type, *frame_opt, options));
    }

    // Group all non-excluded frames by their new group key.
    QHash<GroupKey, std::vector<FrameId>> nonExcludedByGroup;
    for (const auto& frame : collection.frames())
    {
        if (frame.excluded || frame.type() != type)
        {
            continue;
        }
        nonExcludedByGroup[groupKeyForFrame(type, frame, options)].push_back(frame.id);
    }

    std::vector<FrameId> redetected;

    for (auto it = nonExcludedByGroup.constBegin(); it != nonExcludedByGroup.constEnd(); ++it)
    {
        // Skip groups that already have a master.
        if (groupsWithMaster.contains(it.key()))
        {
            continue;
        }

        const auto& ids = it.value();

        // Only auto-detect when the group contains exactly one non-excluded frame
        // whose path matches the master-filename heuristic.
        if (ids.size() != 1)
        {
            continue;
        }

        const auto candidate = collection.findById(ids.front());
        if (!candidate)
        {
            continue;
        }

        if (isProbablyMasterPath(candidate->displayPath) || isProbablyMasterPath(candidate->id.canonicalPath()))
        {
            masterSources.insert(candidate->id, MasterSource::AutoDetected);
            redetected.push_back(candidate->id);
        }
    }

    return redetected;
}

[[nodiscard]] int enforceExclusiveSelectionForMaster(FrameCollection& collection, FrameType type,
                                                     const GroupingOptions& options, const GroupKey& groupKey,
                                                     const FrameId& masterId)
{
    const auto group_frames = collection.filterByGroup(groupKey, type, options);
    int changed = 0;

    for (const auto& frame : group_frames)
    {
        const bool should_exclude = (frame.id != masterId);
        if (frame.excluded == should_exclude)
        {
            continue;
        }

        if (collection.updateFrame(frame.id,
                                   [&](FrameInfo& mutable_frame) { mutable_frame.excluded = should_exclude; }))
        {
            ++changed;
        }
    }

    return changed;
}

QJsonObject groupingOptionsToJson(const GroupingOptions& options)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("byResolution"), options.byResolution);
    obj.insert(QStringLiteral("byFilter"), options.byFilter);
    obj.insert(QStringLiteral("byObject"), options.byObject);
    obj.insert(QStringLiteral("bySession"), options.bySession);
    obj.insert(QStringLiteral("byIsoGain"), options.byIsoGain);
    obj.insert(QStringLiteral("byOffset"), options.byOffset);
    obj.insert(QStringLiteral("byExposure"), options.byExposure);
    obj.insert(QStringLiteral("exposureToleranceSeconds"), options.exposureToleranceSeconds);
    obj.insert(QStringLiteral("byTemperature"), options.byTemperature);
    obj.insert(QStringLiteral("temperatureToleranceC"), options.temperatureToleranceC);
    obj.insert(QStringLiteral("byCustomTag"), options.byCustomTag);
    return obj;
}

GroupingOptions groupingOptionsFromJson(const QJsonObject& obj)
{
    GroupingOptions options;
    options.byResolution = true;
    options.byFilter = obj.value(QStringLiteral("byFilter")).toBool(options.byFilter);
    options.byObject = obj.value(QStringLiteral("byObject")).toBool(options.byObject);
    options.bySession = obj.value(QStringLiteral("bySession")).toBool(options.bySession);
    options.byIsoGain = obj.value(QStringLiteral("byIsoGain")).toBool(options.byIsoGain);
    options.byOffset = obj.value(QStringLiteral("byOffset")).toBool(options.byOffset);
    options.byExposure = obj.value(QStringLiteral("byExposure")).toBool(options.byExposure);
    options.exposureToleranceSeconds =
        obj.value(QStringLiteral("exposureToleranceSeconds")).toDouble(options.exposureToleranceSeconds);
    options.byTemperature = obj.value(QStringLiteral("byTemperature")).toBool(options.byTemperature);
    options.temperatureToleranceC =
        obj.value(QStringLiteral("temperatureToleranceC")).toDouble(options.temperatureToleranceC);
    options.byCustomTag = obj.value(QStringLiteral("byCustomTag")).toBool(options.byCustomTag);
    return options;
}

QJsonObject localNormalizationToJson(const LocalNormalizationSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), settings.enabled);
    obj.insert(QStringLiteral("gridSize"), settings.gridSize);
    obj.insert(QStringLiteral("gridInterpolation"), settings.gridInterpolation);
    obj.insert(QStringLiteral("localScaleEnabled"), settings.localScaleEnabled);
    obj.insert(QStringLiteral("referenceStrategy"), settings.referenceStrategy);
    obj.insert(QStringLiteral("maxIntegratedFrames"), settings.maxIntegratedFrames);
    obj.insert(QStringLiteral("evaluationCriteria"), settings.evaluationCriteria);
    obj.insert(QStringLiteral("backgroundEstimator"), settings.backgroundEstimator);
    obj.insert(QStringLiteral("backgroundPercentile"), settings.backgroundPercentile);
    obj.insert(QStringLiteral("sigmaClipK"), settings.sigmaClipK);
    obj.insert(QStringLiteral("sigmaClipIterations"), settings.sigmaClipIterations);
    obj.insert(QStringLiteral("backgroundSmoothingRadius"), settings.backgroundSmoothingRadius);
    obj.insert(QStringLiteral("scaleEvaluation"), settings.scaleEvaluation);
    obj.insert(QStringLiteral("scaleMode"), settings.scaleMode);
    obj.insert(QStringLiteral("psfType"), settings.psfType);
    obj.insert(QStringLiteral("moffatBeta"), settings.moffatBeta);
    obj.insert(QStringLiteral("maxStars"), settings.maxStars);
    obj.insert(QStringLiteral("minDetectionSnr"), settings.minDetectionSnr);
    obj.insert(QStringLiteral("allowClusteredSources"), settings.allowClusteredSources);
    obj.insert(QStringLiteral("lowClip"), settings.lowClip);
    obj.insert(QStringLiteral("highClip"), settings.highClip);
    obj.insert(QStringLiteral("hotPixelMedianRadius"), settings.hotPixelMedianRadius);
    obj.insert(QStringLiteral("noiseReductionRadius"), settings.noiseReductionRadius);
    obj.insert(QStringLiteral("localScaleSmoothingRadius"), settings.localScaleSmoothingRadius);
    return obj;
}

LocalNormalizationSettings localNormalizationFromJson(const QJsonObject& obj)
{
    LocalNormalizationSettings settings;
    settings.enabled = obj.value(QStringLiteral("enabled")).toBool(settings.enabled);
    settings.gridSize = obj.value(QStringLiteral("gridSize")).toInt(settings.gridSize);
    settings.gridInterpolation = obj.value(QStringLiteral("gridInterpolation")).toString(settings.gridInterpolation);
    settings.localScaleEnabled = obj.value(QStringLiteral("localScaleEnabled")).toBool(settings.localScaleEnabled);
    settings.referenceStrategy = obj.value(QStringLiteral("referenceStrategy")).toString(settings.referenceStrategy);
    settings.maxIntegratedFrames = obj.value(QStringLiteral("maxIntegratedFrames")).toInt(settings.maxIntegratedFrames);
    settings.evaluationCriteria = obj.value(QStringLiteral("evaluationCriteria")).toString(settings.evaluationCriteria);
    settings.backgroundEstimator =
        obj.value(QStringLiteral("backgroundEstimator")).toString(settings.backgroundEstimator);
    settings.backgroundPercentile =
        obj.value(QStringLiteral("backgroundPercentile")).toDouble(settings.backgroundPercentile);
    settings.sigmaClipK = obj.value(QStringLiteral("sigmaClipK")).toDouble(settings.sigmaClipK);
    settings.sigmaClipIterations = obj.value(QStringLiteral("sigmaClipIterations")).toInt(settings.sigmaClipIterations);
    settings.backgroundSmoothingRadius =
        obj.value(QStringLiteral("backgroundSmoothingRadius")).toInt(settings.backgroundSmoothingRadius);
    settings.scaleEvaluation = obj.value(QStringLiteral("scaleEvaluation")).toString(settings.scaleEvaluation);
    settings.scaleMode = obj.value(QStringLiteral("scaleMode")).toString(settings.scaleMode);
    settings.psfType = obj.value(QStringLiteral("psfType")).toString(settings.psfType);
    settings.moffatBeta = obj.value(QStringLiteral("moffatBeta")).toDouble(settings.moffatBeta);
    settings.maxStars = obj.value(QStringLiteral("maxStars")).toInt(settings.maxStars);
    settings.minDetectionSnr = obj.value(QStringLiteral("minDetectionSnr")).toDouble(settings.minDetectionSnr);
    settings.allowClusteredSources =
        obj.value(QStringLiteral("allowClusteredSources")).toBool(settings.allowClusteredSources);
    settings.lowClip = obj.value(QStringLiteral("lowClip")).toDouble(settings.lowClip);
    settings.highClip = obj.value(QStringLiteral("highClip")).toDouble(settings.highClip);
    settings.hotPixelMedianRadius =
        obj.value(QStringLiteral("hotPixelMedianRadius")).toInt(settings.hotPixelMedianRadius);
    settings.noiseReductionRadius =
        obj.value(QStringLiteral("noiseReductionRadius")).toInt(settings.noiseReductionRadius);
    settings.localScaleSmoothingRadius =
        obj.value(QStringLiteral("localScaleSmoothingRadius")).toInt(settings.localScaleSmoothingRadius);
    return settings;
}

QJsonObject overlapNormalizationToJson(const OverlapNormalizationSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), settings.enabled);
    obj.insert(QStringLiteral("minOverlapPixels"), settings.minOverlapPixels);
    obj.insert(QStringLiteral("minOverlapFraction"), settings.minOverlapFraction);
    return obj;
}

OverlapNormalizationSettings overlapNormalizationFromJson(const QJsonObject& obj)
{
    OverlapNormalizationSettings settings;
    settings.enabled = obj.value(QStringLiteral("enabled")).toBool(settings.enabled);
    settings.minOverlapPixels =
        (std::max)(0, obj.value(QStringLiteral("minOverlapPixels")).toInt(settings.minOverlapPixels));
    settings.minOverlapFraction = obj.value(QStringLiteral("minOverlapFraction")).toDouble(settings.minOverlapFraction);
    return settings;
}

QJsonObject localWeightingToJson(const LocalWeightingSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), settings.enabled);
    obj.insert(QStringLiteral("windowSize"), settings.windowSize);
    obj.insert(QStringLiteral("gridSpacing"), settings.gridSpacing);
    obj.insert(QStringLiteral("snrWeightExponent"), settings.snrWeightExponent);
    return obj;
}

LocalWeightingSettings localWeightingFromJson(const QJsonObject& obj)
{
    LocalWeightingSettings settings;
    settings.enabled = obj.value(QStringLiteral("enabled")).toBool(settings.enabled);
    settings.windowSize = obj.value(QStringLiteral("windowSize")).toInt(settings.windowSize);
    settings.gridSpacing = obj.value(QStringLiteral("gridSpacing")).toInt(settings.gridSpacing);
    settings.snrWeightExponent = obj.value(QStringLiteral("snrWeightExponent")).toDouble(settings.snrWeightExponent);
    return settings;
}

QJsonObject drizzleSettingsToJson(const DrizzleSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), settings.enabled);
    obj.insert(QStringLiteral("scale"), settings.scale);
    obj.insert(QStringLiteral("pixelFraction"), settings.pixelFraction);
    obj.insert(QStringLiteral("kernel"), settings.kernel);
    obj.insert(QStringLiteral("cfaAware"), settings.cfaAware);
    obj.insert(QStringLiteral("cfaPattern"), settings.cfaPattern);
    return obj;
}

DrizzleSettings drizzleSettingsFromJson(const QJsonObject& obj)
{
    DrizzleSettings settings;
    settings.enabled = obj.value(QStringLiteral("enabled")).toBool(settings.enabled);
    settings.scale = obj.value(QStringLiteral("scale")).toDouble(settings.scale);
    settings.pixelFraction = obj.value(QStringLiteral("pixelFraction")).toDouble(settings.pixelFraction);
    settings.kernel = obj.value(QStringLiteral("kernel")).toString(settings.kernel);
    settings.cfaAware = obj.value(QStringLiteral("cfaAware")).toBool(settings.cfaAware);
    settings.cfaPattern = obj.value(QStringLiteral("cfaPattern")).toString(settings.cfaPattern);
    return settings;
}

QJsonObject hdrExposureMergeSettingsToJson(const HdrExposureMergeSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), settings.enabled);
    return obj;
}

HdrExposureMergeSettings hdrExposureMergeSettingsFromJson(const QJsonObject& obj)
{
    HdrExposureMergeSettings settings;
    settings.enabled = obj.value(QStringLiteral("enabled")).toBool(settings.enabled);
    return settings;
}

// StackingSettings expansion checklist:
// 1. Update model JSON here.
// 2. Mirror the field in WorkflowManifest serialization.
// 3. Mirror processing-affecting fields in manifest hashing and diffing.
// 4. Extend defaults/persistence regression tests.
QJsonObject stackingSettingsToJson(const StackingSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("stackMethod"), settings.stackMethod);
    obj.insert(QStringLiteral("rejectionMethod"), settings.rejectionMethod);
    obj.insert(QStringLiteral("requestedRejectionMethod"), settings.requestedRejectionMethod);
    obj.insert(QStringLiteral("sigmaLow"), settings.sigmaLow);
    obj.insert(QStringLiteral("sigmaHigh"), settings.sigmaHigh);
    obj.insert(QStringLiteral("weighting"), settings.weighting);
    obj.insert(QStringLiteral("createRejectionMaps"), settings.createRejectionMaps);
    obj.insert(QStringLiteral("mergeRejectionMaps"), settings.mergeRejectionMaps);
    obj.insert(QStringLiteral("createQualityMaps"), settings.createQualityMaps);
    obj.insert(QStringLiteral("spatialRejectionClustering"), settings.spatialRejectionClustering);
    obj.insert(QStringLiteral("restackExcludingFlaggedFrames"), settings.restackExcludingFlaggedFrames);
    obj.insert(QStringLiteral("limitMemory"), settings.limitMemory);
    obj.insert(QStringLiteral("maxMemoryMb"), settings.maxMemoryMb);
    obj.insert(QStringLiteral("starCountScaling"), settings.starCountScaling);
    obj.insert(QStringLiteral("exponentialAlpha"), settings.exponentialAlpha);
    obj.insert(QStringLiteral("minWeightFloor"), settings.minWeightFloor);
    obj.insert(QStringLiteral("perChannelFwhmWeighting"), settings.perChannelFwhmWeighting);
    obj.insert(QStringLiteral("localWeighting"), localWeightingToJson(settings.localWeighting));
    obj.insert(QStringLiteral("normalizationMode"), static_cast<int>(settings.normalizationMode));
    obj.insert(QStringLiteral("globalNormalizationMethod"), static_cast<int>(settings.globalNormalizationMethod));
    obj.insert(QStringLiteral("scaleEstimator"), static_cast<int>(settings.scaleEstimator));
    obj.insert(QStringLiteral("overlapNormalization"), overlapNormalizationToJson(settings.overlapNormalization));
    obj.insert(QStringLiteral("normalizationReferenceMode"), static_cast<int>(settings.normalizationReferenceMode));
    obj.insert(QStringLiteral("manualNormalizationReference"), settings.manualNormalizationReference);
    obj.insert(QStringLiteral("localNormalization"), localNormalizationToJson(settings.localNormalization));
    obj.insert(QStringLiteral("drizzle"), drizzleSettingsToJson(settings.drizzle));
    obj.insert(QStringLiteral("cometStackingMode"), settings.cometStackingMode);
    obj.insert(QStringLiteral("solveFinalMasterIfNeeded"), settings.solveFinalMasterIfNeeded);
    obj.insert(QStringLiteral("registeredOutputPolicy"), static_cast<int>(settings.registeredOutputPolicy));
    obj.insert(QStringLiteral("hdr"), hdrExposureMergeSettingsToJson(settings.hdr));
    return obj;
}

StackingSettings stackingSettingsFromJson(const QJsonObject& obj)
{
    StackingSettings settings;
    settings.stackMethod = obj.value(QStringLiteral("stackMethod")).toString(settings.stackMethod);
    settings.rejectionMethod = obj.value(QStringLiteral("rejectionMethod")).toString(settings.rejectionMethod);
    settings.requestedRejectionMethod =
        obj.value(QStringLiteral("requestedRejectionMethod")).toString(settings.requestedRejectionMethod);
    settings.sigmaLow = obj.value(QStringLiteral("sigmaLow")).toDouble(settings.sigmaLow);
    settings.sigmaHigh = obj.value(QStringLiteral("sigmaHigh")).toDouble(settings.sigmaHigh);
    settings.weighting = obj.value(QStringLiteral("weighting")).toString(settings.weighting);
    settings.createRejectionMaps =
        obj.value(QStringLiteral("createRejectionMaps")).toBool(settings.createRejectionMaps);
    settings.mergeRejectionMaps = obj.value(QStringLiteral("mergeRejectionMaps")).toBool(settings.mergeRejectionMaps);
    settings.createQualityMaps = obj.value(QStringLiteral("createQualityMaps")).toBool(settings.createQualityMaps);
    settings.spatialRejectionClustering =
        obj.value(QStringLiteral("spatialRejectionClustering")).toBool(settings.spatialRejectionClustering);
    settings.restackExcludingFlaggedFrames =
        obj.value(QStringLiteral("restackExcludingFlaggedFrames")).toBool(settings.restackExcludingFlaggedFrames);
    settings.limitMemory = obj.value(QStringLiteral("limitMemory")).toBool(settings.limitMemory);
    settings.maxMemoryMb = obj.value(QStringLiteral("maxMemoryMb")).toInt(settings.maxMemoryMb);
    settings.starCountScaling = obj.value(QStringLiteral("starCountScaling")).toString(settings.starCountScaling);
    settings.exponentialAlpha = obj.value(QStringLiteral("exponentialAlpha")).toDouble(settings.exponentialAlpha);
    settings.minWeightFloor = obj.value(QStringLiteral("minWeightFloor")).toDouble(settings.minWeightFloor);
    settings.perChannelFwhmWeighting =
        obj.value(QStringLiteral("perChannelFwhmWeighting")).toBool(settings.perChannelFwhmWeighting);
    settings.localWeighting = localWeightingFromJson(obj.value(QStringLiteral("localWeighting")).toObject());
    settings.normalizationMode = persistedEnumerator(
        obj.value(QStringLiteral("normalizationMode")).toInt(static_cast<int>(settings.normalizationMode)),
        kPersistedNormalizationModes, settings.normalizationMode);
    settings.globalNormalizationMethod =
        persistedEnumerator(obj.value(QStringLiteral("globalNormalizationMethod"))
                                .toInt(static_cast<int>(settings.globalNormalizationMethod)),
                            kPersistedGlobalNormalizationMethods, settings.globalNormalizationMethod);
    settings.scaleEstimator = persistedEnumerator(
        obj.value(QStringLiteral("scaleEstimator")).toInt(static_cast<int>(settings.scaleEstimator)),
        kPersistedScaleEstimatorMethods, settings.scaleEstimator);
    settings.overlapNormalization =
        overlapNormalizationFromJson(obj.value(QStringLiteral("overlapNormalization")).toObject());
    settings.normalizationReferenceMode =
        persistedEnumerator(obj.value(QStringLiteral("normalizationReferenceMode"))
                                .toInt(static_cast<int>(settings.normalizationReferenceMode)),
                            kPersistedNormalizationReferenceModes, settings.normalizationReferenceMode);
    settings.manualNormalizationReference =
        obj.value(QStringLiteral("manualNormalizationReference")).toString(settings.manualNormalizationReference);
    settings.localNormalization =
        localNormalizationFromJson(obj.value(QStringLiteral("localNormalization")).toObject());
    settings.drizzle = drizzleSettingsFromJson(obj.value(QStringLiteral("drizzle")).toObject());
    settings.cometStackingMode = obj.value(QStringLiteral("cometStackingMode")).toString(settings.cometStackingMode);
    settings.solveFinalMasterIfNeeded =
        obj.value(QStringLiteral("solveFinalMasterIfNeeded")).toBool(settings.solveFinalMasterIfNeeded);
    if (obj.value(QStringLiteral("registeredOutputPolicy")).toInt(static_cast<int>(RegisteredOutputPolicy::Retained)) ==
        static_cast<int>(RegisteredOutputPolicy::Discardable))
    {
        settings.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    }
    settings.hdr = hdrExposureMergeSettingsFromJson(obj.value(QStringLiteral("hdr")).toObject());
    return settings;
}

QJsonObject lightCalibrationSettingsToJson(const LightCalibrationSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("cosmeticCorrection"), settings.cosmeticCorrection);
    obj.insert(QStringLiteral("sigmaCold"), settings.sigmaCold);
    obj.insert(QStringLiteral("sigmaHot"), settings.sigmaHot);
    obj.insert(QStringLiteral("darkOptimization"), settings.darkOptimization);
    obj.insert(QStringLiteral("darkMultiplicationFactor"), settings.darkMultiplicationFactor);
    obj.insert(QStringLiteral("flatAutoLevel"), settings.flatAutoLevel);
    obj.insert(QStringLiteral("pedestal"), settings.pedestal);
    obj.insert(QStringLiteral("preCalibrated"), settings.preCalibrated);
    return obj;
}

LightCalibrationSettings lightCalibrationSettingsFromJson(const QJsonObject& obj)
{
    LightCalibrationSettings settings;
    settings.cosmeticCorrection = obj.value(QStringLiteral("cosmeticCorrection")).toBool(settings.cosmeticCorrection);
    settings.sigmaCold = obj.value(QStringLiteral("sigmaCold")).toDouble(settings.sigmaCold);
    settings.sigmaHot = obj.value(QStringLiteral("sigmaHot")).toDouble(settings.sigmaHot);
    settings.darkOptimization = obj.value(QStringLiteral("darkOptimization")).toBool(settings.darkOptimization);
    settings.darkMultiplicationFactor =
        obj.value(QStringLiteral("darkMultiplicationFactor")).toDouble(settings.darkMultiplicationFactor);
    settings.flatAutoLevel = obj.value(QStringLiteral("flatAutoLevel")).toBool(settings.flatAutoLevel);
    settings.pedestal = obj.value(QStringLiteral("pedestal")).toDouble(settings.pedestal);
    settings.preCalibrated = obj.value(QStringLiteral("preCalibrated")).toBool(settings.preCalibrated);
    return settings;
}

QJsonObject lightGroupKeyToJson(const LightGroupKey& key)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("resolution"), key.resolution);
    if (key.isoGain.has_value())
    {
        obj.insert(QStringLiteral("isoGain"), key.isoGain.value());
    }
    if (key.offset.has_value())
    {
        obj.insert(QStringLiteral("offset"), key.offset.value());
    }
    obj.insert(QStringLiteral("exposureSeconds"), key.exposureSeconds);
    obj.insert(QStringLiteral("filter"), key.filter);
    obj.insert(QStringLiteral("objectName"), key.objectName);
    obj.insert(QStringLiteral("session"), key.session);
    obj.insert(QStringLiteral("customTag"), key.customTag);
    return obj;
}

std::optional<LightGroupKey> lightGroupKeyFromJson(const QJsonObject& obj)
{
    LightGroupKey key;
    key.resolution = obj.value(QStringLiteral("resolution")).toString();
    if (key.resolution.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    if (obj.contains(QStringLiteral("isoGain")))
    {
        key.isoGain = obj.value(QStringLiteral("isoGain")).toInt();
    }
    if (obj.contains(QStringLiteral("offset")))
    {
        key.offset = obj.value(QStringLiteral("offset")).toInt();
    }
    key.exposureSeconds = obj.value(QStringLiteral("exposureSeconds")).toDouble();
    key.filter = obj.value(QStringLiteral("filter")).toString();
    key.objectName = obj.value(QStringLiteral("objectName")).toString();
    key.session = obj.value(QStringLiteral("session")).toString();
    key.customTag = obj.value(QStringLiteral("customTag")).toString();
    return key;
}

bool lightKeysEqual(const LightGroupKey& lhs, const LightGroupKey& rhs)
{
    return lhs.resolution == rhs.resolution && lhs.isoGain == rhs.isoGain && lhs.offset == rhs.offset &&
           lhs.exposureSeconds == rhs.exposureSeconds && lhs.filter == rhs.filter && lhs.objectName == rhs.objectName &&
           lhs.session == rhs.session && lhs.customTag == rhs.customTag;
}

// Use the canonical implementation from RegistrationHashUtils.h
using ImageIntegration::RegistrationHashUtils::registrationSettingsFromJson;
using ImageIntegration::RegistrationHashUtils::registrationSettingsToJson;

QJsonObject frameMetadataToJson(const FrameMetadata& meta)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), toString(meta.type()));
    obj.insert(QStringLiteral("width"), meta.dimensions().width());
    obj.insert(QStringLiteral("height"), meta.dimensions().height());
    obj.insert(QStringLiteral("exposureSeconds"), meta.exposureSeconds());
    if (meta.temperatureC().has_value())
    {
        obj.insert(QStringLiteral("temperatureC"), meta.temperatureC().value());
    }
    if (meta.isoGain().has_value())
    {
        obj.insert(QStringLiteral("isoGain"), meta.isoGain().value());
    }
    if (meta.offset().has_value())
    {
        obj.insert(QStringLiteral("offset"), meta.offset().value());
    }
    obj.insert(QStringLiteral("filter"), meta.filter());
    obj.insert(QStringLiteral("objectName"), meta.objectName());
    obj.insert(QStringLiteral("capturedAtUtc"), meta.capturedAtUtc().toString(Qt::ISODateWithMs));
    obj.insert(QStringLiteral("readoutMode"), meta.readoutMode());
    return obj;
}

FrameMetadata frameMetadataFromJson(const QJsonObject& obj, FrameType fallback_type)
{
    FrameType type = fallback_type;
    if (auto parsed = frameTypeFromString(obj.value(QStringLiteral("type")).toString()))
    {
        type = *parsed;
    }

    const int width = obj.value(QStringLiteral("width")).toInt(0);
    const int height = obj.value(QStringLiteral("height")).toInt(0);
    const double exposure = obj.value(QStringLiteral("exposureSeconds")).toDouble(0.0);

    std::optional<double> temperature;
    if (obj.contains(QStringLiteral("temperatureC")))
    {
        temperature = obj.value(QStringLiteral("temperatureC")).toDouble();
    }

    std::optional<int> iso_gain;
    if (obj.contains(QStringLiteral("isoGain")))
    {
        iso_gain = obj.value(QStringLiteral("isoGain")).toInt();
    }

    std::optional<int> offset;
    if (obj.contains(QStringLiteral("offset")))
    {
        offset = obj.value(QStringLiteral("offset")).toInt();
    }

    const QString filter = obj.value(QStringLiteral("filter")).toString();
    const QString object = obj.value(QStringLiteral("objectName")).toString();

    QDateTime captured_at =
        QDateTime::fromString(obj.value(QStringLiteral("capturedAtUtc")).toString(), Qt::ISODateWithMs);
    if (!captured_at.isValid())
    {
        captured_at = QDateTime::fromString(obj.value(QStringLiteral("capturedAtUtc")).toString(), Qt::ISODate);
    }
    captured_at = captured_at.toUTC();

    const QString readout = obj.value(QStringLiteral("readoutMode")).toString();

    return FrameMetadata(type, QSize(width, height), exposure, temperature, iso_gain, offset, filter, object,
                         captured_at, readout);
}

QJsonObject frameInfoToJson(const FrameInfo& frame)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("path"), frame.displayPath);
    obj.insert(QStringLiteral("metadata"), frameMetadataToJson(frame.metadata));
    obj.insert(QStringLiteral("userSession"), frame.userSession);
    obj.insert(QStringLiteral("userObjectName"), frame.userObjectName);
    obj.insert(QStringLiteral("customTag"), frame.customTag);
    obj.insert(QStringLiteral("excluded"), frame.excluded);
    if (frame.manualGroupKey.has_value())
    {
        obj.insert(QStringLiteral("manualGroupKey"), frame.manualGroupKey->toString());
    }
    return obj;
}

std::optional<FrameInfo> frameInfoFromJson(const QJsonObject& obj, FrameType fallback_type)
{
    const QString path = obj.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty())
    {
        return std::nullopt;
    }

    FrameMetadata metadata = frameMetadataFromJson(obj.value(QStringLiteral("metadata")).toObject(), fallback_type);
    const QString session = obj.value(QStringLiteral("userSession")).toString();
    const QString user_object = obj.value(QStringLiteral("userObjectName")).toString();
    const QString custom_tag = obj.value(QStringLiteral("customTag")).toString();
    const bool excluded = obj.value(QStringLiteral("excluded")).toBool(false);

    std::optional<GroupKey> manual_group;
    const QString manual_key = obj.value(QStringLiteral("manualGroupKey")).toString();
    if (!manual_key.trimmed().isEmpty())
    {
        manual_group = GroupKey{manual_key};
    }

    try
    {
        return FrameInfo(FrameId(path), path, metadata, session, custom_tag, excluded, manual_group, user_object);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

[[nodiscard]] MasterSource masterSourceFromInt(int value)
{
    switch (value)
    {
    case static_cast<int>(MasterSource::Unknown):
        return MasterSource::Unknown;
    case static_cast<int>(MasterSource::UserDefined):
        return MasterSource::UserDefined;
    case static_cast<int>(MasterSource::AutoGenerated):
        return MasterSource::AutoGenerated;
    case static_cast<int>(MasterSource::AutoDetected):
        return MasterSource::AutoDetected;
    default:
        return MasterSource::Unknown;
    }
}

} // namespace

class IntegrationDataModel::Impl
{
public:
    explicit Impl(IntegrationDataModel* parent)
    {
        for (int i = 0; i < kFrameTypeCount; ++i)
        {
            const FrameType type = frameTypeForIndex(i);
            GroupingOptions base_options;
            if (type == FrameType::Dark)
            {
                base_options.byExposure = true;
            }
            else if (type == FrameType::Flat)
            {
                base_options.byObject = false;
            }
            groupingOptions[static_cast<size_t>(i)] = normalizeGroupingOptionsForFrameType(type, base_options);
        }

        // Default integration grouping options (strict subset for stacking)
        lightIntegrationGroupingOptions.byResolution = true;
        lightIntegrationGroupingOptions.byFilter = true;
        lightIntegrationGroupingOptions.byObject = true;
        lightIntegrationGroupingOptions.bySession = false;
        lightIntegrationGroupingOptions.byIsoGain = false;
        lightIntegrationGroupingOptions.byOffset = true;
        lightIntegrationGroupingOptions.byExposure = false;
        lightIntegrationGroupingOptions.exposureToleranceSeconds = 3.0;
        lightIntegrationGroupingOptions.byTemperature = false;
        lightIntegrationGroupingOptions.byCustomTag = false;

        collections = {std::make_unique<FrameCollection>(parent), std::make_unique<FrameCollection>(parent),
                       std::make_unique<FrameCollection>(parent), std::make_unique<FrameCollection>(parent)};
        groupsDirty.fill(true);
    }

    void invalidateValidation() const
    {
        validationDirty = true;
    }

    std::array<std::unique_ptr<FrameCollection>, 4> collections;
    std::array<GroupingOptions, 4> groupingOptions;
    GroupingOptions lightIntegrationGroupingOptions;
    std::array<GroupedFrameView, 4> groupedViews;
    std::array<bool, 4> groupsDirty{};

    mutable bool validationDirty = true;
    mutable std::optional<WorkflowValidationResult> cachedValidation;

    CalibrationAssignments lightCalibration;
    CalibrationAssignments flatCalibration;
    bool calibrationDirty = true;

    RegistrationGroupMap registrationGroups;
    bool registrationDirty = true;
    IntegrationGroupList integrationGroups;
    bool integrationDirty = true;

    std::array<StackingSettingsMap, 4> stackingSettings;
    RegistrationSettingsMap registrationSettings;
    LightCalibrationSettingsMap lightCalibrationSettings;
    std::map<GroupKey, QSet<QString>> stackExcludedFrames;

    std::map<FrameId, FrameQualityMetrics> qualityMetrics;

    std::vector<CalibrationOverride> calibrationOverrides;

    CalibrationMatchingPolicy calibrationPolicy;

    std::vector<FrameId> selectedFrameIds;
    std::vector<GroupKey> selectedGroupKeys;
    std::optional<GroupKey> selectedRegistrationGroupKey;

    QHash<FrameId, MasterSource> masterSources;

    QString outputDirectory;
    bool hasUnsavedChanges = false;

    LightCalibrationCacheMode lightCalibrationCacheMode = LightCalibrationCacheMode::InMemory;

    int batchDepth = 0;
    bool pendingReset = false;
    std::array<bool, 4> pendingRegroupResetSettings{};

    void ensureRegistrationGroups()
    {
        if (!registrationDirty)
        {
            return;
        }

        RegistrationGroupEngine engine;
        registrationGroups = engine.computeGroups(collections[indexFor(FrameType::Light)]->frames());
        registrationDirty = false;
    }

    void ensureGroups(FrameType type)
    {
        const int idx = indexFor(type);
        if (!groupsDirty[static_cast<size_t>(idx)])
        {
            return;
        }

        GroupingEngine engine;
        groupedViews[static_cast<size_t>(idx)] = engine.groupFrames(collections[static_cast<size_t>(idx)]->frames(),
                                                                    type, groupingOptions[static_cast<size_t>(idx)]);
        groupsDirty[static_cast<size_t>(idx)] = false;
    }

    [[nodiscard]] bool sanitizeRegistrationReferenceFrames()
    {
        std::unordered_map<QString, GroupKey> frame_to_machine_key;

        const auto light_frames = collections[indexFor(FrameType::Light)]->frames();
        frame_to_machine_key.reserve(light_frames.size());
        for (const auto& frame : light_frames)
        {
            if (frame.excluded)
            {
                continue;
            }

            const RegistrationGroupKey reg_key = RegistrationGroupKey::fromFrame(frame);
            frame_to_machine_key.emplace(frame.id.canonicalPath(), RegistrationGroupEngine::toMachineGroupKey(reg_key));
        }

        bool changed = false;
        for (auto& entry : registrationSettings)
        {
            const GroupKey& machine_key = entry.first;
            RegistrationSettings& settings = entry.second;

            const QString ref = settings.referenceImage.trimmed();
            if (ref.isEmpty())
            {
                continue;
            }

            const QString canonical_ref = FrameId(ref).canonicalPath();
            const auto it = frame_to_machine_key.find(canonical_ref);
            if (it == frame_to_machine_key.end() || it->second != machine_key)
            {
                settings.referenceImage.clear();
                ++settings.version;
                changed = true;
            }
            else if (settings.referenceImage != canonical_ref)
            {
                settings.referenceImage = canonical_ref;
                changed = true;
            }
        }

        if (changed)
        {
            invalidateValidation();
            hasUnsavedChanges = true;
        }

        return changed;
    }
};

IntegrationDataModel::IntegrationDataModel(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>(this)) {}

IntegrationDataModel::~IntegrationDataModel() = default;

const FrameCollection& IntegrationDataModel::biasFrames() const
{
    return *m_impl->collections[indexFor(FrameType::Bias)];
}

const FrameCollection& IntegrationDataModel::darkFrames() const
{
    return *m_impl->collections[indexFor(FrameType::Dark)];
}

const FrameCollection& IntegrationDataModel::flatFrames() const
{
    return *m_impl->collections[indexFor(FrameType::Flat)];
}

const FrameCollection& IntegrationDataModel::lightFrames() const
{
    return *m_impl->collections[indexFor(FrameType::Light)];
}

const FrameCollection& IntegrationDataModel::frames(FrameType type) const
{
    return *m_impl->collections[indexFor(type)];
}

const GroupedFrameView& IntegrationDataModel::biasGroups() const
{
    m_impl->ensureGroups(FrameType::Bias);
    return m_impl->groupedViews[indexFor(FrameType::Bias)];
}

const GroupedFrameView& IntegrationDataModel::darkGroups() const
{
    m_impl->ensureGroups(FrameType::Dark);
    return m_impl->groupedViews[indexFor(FrameType::Dark)];
}

const GroupedFrameView& IntegrationDataModel::flatGroups() const
{
    m_impl->ensureGroups(FrameType::Flat);
    return m_impl->groupedViews[indexFor(FrameType::Flat)];
}

const GroupedFrameView& IntegrationDataModel::lightGroups() const
{
    m_impl->ensureGroups(FrameType::Light);
    return m_impl->groupedViews[indexFor(FrameType::Light)];
}

int IntegrationDataModel::masterCount(FrameType type) const
{
    const auto& frameList = m_impl->collections[indexFor(type)]->frames();
    int count = 0;
    for (const auto& frame : frameList)
    {
        if (m_impl->masterSources.contains(frame.id))
        {
            ++count;
        }
    }
    return count;
}

bool IntegrationDataModel::isMasterFrame(const FrameId& id) const
{
    return m_impl->masterSources.contains(id);
}

std::optional<MasterSource> IntegrationDataModel::masterSource(const FrameId& id) const
{
    auto it = m_impl->masterSources.find(id);
    if (it == m_impl->masterSources.end())
    {
        return std::nullopt;
    }
    return it.value();
}

const CalibrationAssignments& IntegrationDataModel::lightCalibration() const
{
    if (m_impl->calibrationDirty)
    {
        m_impl->lightCalibration.lightGroups.clear();
        m_impl->flatCalibration.flatGroups.clear();

        for (const auto& group : lightGroups())
        {
            m_impl->lightCalibration.lightGroups.push_back(group.key);
        }
        for (const auto& group : flatGroups())
        {
            m_impl->flatCalibration.flatGroups.push_back(group.key);
        }

        ++m_impl->lightCalibration.revision;
        ++m_impl->flatCalibration.revision;
        m_impl->calibrationDirty = false;
    }

    return m_impl->lightCalibration;
}

const CalibrationAssignments& IntegrationDataModel::flatCalibration() const
{
    if (m_impl->calibrationDirty)
    {
        (void)lightCalibration();
    }

    return m_impl->flatCalibration;
}

const RegistrationGroupMap& IntegrationDataModel::registrationGroups() const
{
    m_impl->ensureRegistrationGroups();
    return m_impl->registrationGroups;
}

std::optional<FrameId> IntegrationDataModel::registrationReferenceFrame(const RegistrationGroupKey& key) const
{
    const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(key);
    auto it = m_impl->registrationSettings.find(machine_key);
    if (it == m_impl->registrationSettings.end())
    {
        return std::nullopt;
    }

    const QString path = it->second.referenceImage.trimmed();
    if (path.isEmpty())
    {
        return std::nullopt;
    }

    const FrameId ref_id(path);
    const auto frame_opt = m_impl->collections[indexFor(FrameType::Light)]->findById(ref_id);
    if (!frame_opt.has_value() || frame_opt->excluded)
    {
        return std::nullopt;
    }

    if (RegistrationGroupKey::fromFrame(*frame_opt) != key)
    {
        return std::nullopt;
    }

    return ref_id;
}

void IntegrationDataModel::setRegistrationReferenceFrame(const RegistrationGroupKey& key,
                                                         const std::optional<FrameId>& reference)
{
    if (reference.has_value())
    {
        const auto frame_opt = m_impl->collections[indexFor(FrameType::Light)]->findById(*reference);
        if (!frame_opt.has_value() || frame_opt->excluded || RegistrationGroupKey::fromFrame(*frame_opt) != key)
        {
            const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(key);
            LOG_WARNING("IntegrationDataModel: ignoring registration reference set request (group='{}', "
                        "requested='{}', reason='{}')",
                        machine_key.toString().toStdString(), reference->canonicalPath().toStdString(),
                        (!frame_opt.has_value() ? "frame-not-found"
                                                : (frame_opt->excluded ? "frame-excluded" : "frame-not-in-group")));
            return;
        }
    }

    const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(key);
    auto& settings = m_impl->registrationSettings[machine_key];
    const QString new_ref = reference.has_value() ? reference->canonicalPath() : QString();
    if (settings.referenceImage == new_ref)
    {
        return;
    }

    LOG_INFO("IntegrationDataModel: registration reference updated (group='{}', reference='{}')",
             machine_key.toString().toStdString(),
             (new_ref.trimmed().isEmpty() ? std::string("(none)") : new_ref.toStdString()));

    settings.referenceImage = new_ref;
    ++settings.version;
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit registrationChanged();
    emit settingsChanged();
}

void IntegrationDataModel::setFrameQualityMetrics(const FrameId& id, const FrameQualityMetrics& metrics)
{
    m_impl->qualityMetrics[id] = metrics;
}

std::optional<FrameQualityMetrics> IntegrationDataModel::frameQualityMetrics(const FrameId& id) const
{
    auto it = m_impl->qualityMetrics.find(id);
    if (it == m_impl->qualityMetrics.end())
    {
        return std::nullopt;
    }
    return it->second;
}

void IntegrationDataModel::clearFrameQualityMetrics(const FrameId& id)
{
    m_impl->qualityMetrics.erase(id);
}

void IntegrationDataModel::clearFrameQualityMetrics(const std::vector<FrameId>& ids)
{
    for (const auto& id : ids)
    {
        m_impl->qualityMetrics.erase(id);
    }
}

void IntegrationDataModel::clearAllFrameQualityMetrics()
{
    m_impl->qualityMetrics.clear();
}

const IntegrationGroupList& IntegrationDataModel::integrationGroups() const
{
    if (m_impl->integrationDirty)
    {
        m_impl->integrationGroups.clear();

        // Use IntegrationGroupEngine for proper grouping by Resolution+Filter+Object
        // with optional Session/ISO/Exposure/CustomTag based on integration grouping options
        IntegrationGroupEngine engine;
        const auto& options = m_impl->lightIntegrationGroupingOptions;
        auto groups = engine.computeGroups(m_impl->collections[indexFor(FrameType::Light)]->frames(), options);

        for (const auto& [key, frames] : groups)
        {
            m_impl->integrationGroups.push_back(key.key);
        }

        m_impl->integrationDirty = false;
    }

    return m_impl->integrationGroups;
}

QSet<QString> IntegrationDataModel::stackExcludedFrameIds(const GroupKey& integrationGroupKey) const
{
    const auto it = m_impl->stackExcludedFrames.find(integrationGroupKey);
    if (it == m_impl->stackExcludedFrames.end())
    {
        return {};
    }
    return it->second;
}

bool IntegrationDataModel::isFrameStackExcluded(const GroupKey& integrationGroupKey, const FrameId& id) const
{
    const auto it = m_impl->stackExcludedFrames.find(integrationGroupKey);
    if (it == m_impl->stackExcludedFrames.end())
    {
        return false;
    }
    return it->second.contains(id.canonicalPath());
}

const GroupingOptions& IntegrationDataModel::groupingOptions(FrameType type) const
{
    return m_impl->groupingOptions[indexFor(type)];
}

const GroupingOptions& IntegrationDataModel::lightIntegrationGroupingOptions() const
{
    return m_impl->lightIntegrationGroupingOptions;
}

void IntegrationDataModel::setLightIntegrationGroupingOptions(const GroupingOptions& options)
{
    // Enforce mandatory fields for integration grouping
    GroupingOptions normalized = options;
    normalized.byResolution = true;
    normalized.byFilter = true;
    normalized.byObject = true;

    if (m_impl->lightIntegrationGroupingOptions == normalized)
    {
        return;
    }

    m_impl->lightIntegrationGroupingOptions = normalized;
    m_impl->integrationDirty = true;
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit integrationGroupsChanged();
    emit settingsChanged();
}

const StackingSettingsMap& IntegrationDataModel::stackingSettings(FrameType type) const
{
    return m_impl->stackingSettings[indexFor(type)];
}

const RegistrationSettingsMap& IntegrationDataModel::registrationSettings() const
{
    return m_impl->registrationSettings;
}

const LightCalibrationSettingsMap& IntegrationDataModel::lightCalibrationSettings() const
{
    return m_impl->lightCalibrationSettings;
}

const std::vector<CalibrationOverride>& IntegrationDataModel::calibrationOverrides() const
{
    return m_impl->calibrationOverrides;
}

const CalibrationMatchingPolicy& IntegrationDataModel::calibrationMatchingPolicy() const
{
    return m_impl->calibrationPolicy;
}

void IntegrationDataModel::setCalibrationMatchingPolicy(const CalibrationMatchingPolicy& policy)
{
    if (m_impl->calibrationPolicy.allowFlatSessionFallback == policy.allowFlatSessionFallback &&
        m_impl->calibrationPolicy.maxFlatSessionDaysApart == policy.maxFlatSessionDaysApart &&
        m_impl->calibrationPolicy.preferNewerFlat == policy.preferNewerFlat &&
        m_impl->calibrationPolicy.allowDarkExposureFallback == policy.allowDarkExposureFallback &&
        m_impl->calibrationPolicy.darkExposureTolerancePercent == policy.darkExposureTolerancePercent &&
        m_impl->calibrationPolicy.warnOnFallback == policy.warnOnFallback &&
        m_impl->calibrationPolicy.subtractBiasFromDark == policy.subtractBiasFromDark)
    {
        return;
    }

    m_impl->calibrationPolicy = policy;
    m_impl->calibrationDirty = true;
    m_impl->invalidateValidation();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit calibrationChanged(FrameType::Light);
    emit settingsChanged();
}

void IntegrationDataModel::clearAllCalibrationOverrides()
{
    if (m_impl->calibrationOverrides.empty())
    {
        return;
    }

    m_impl->calibrationOverrides.clear();
    m_impl->calibrationDirty = true;
    m_impl->invalidateValidation();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit calibrationChanged(FrameType::Light);
    emit settingsChanged();
}

void IntegrationDataModel::setSelectedFrameIds(const std::vector<FrameId>& ids)
{
    if (m_impl->selectedFrameIds != ids)
    {
        m_impl->selectedFrameIds = ids;
        emit selectionChanged();
    }
}

void IntegrationDataModel::setSelectedGroupKeys(const std::vector<GroupKey>& keys)
{
    if (m_impl->selectedGroupKeys != keys)
    {
        m_impl->selectedGroupKeys = keys;
        emit selectionChanged();
    }
}

void IntegrationDataModel::setSelectedRegistrationGroupKey(const std::optional<GroupKey>& key)
{
    if (m_impl->selectedRegistrationGroupKey != key)
    {
        m_impl->selectedRegistrationGroupKey = key;
        emit selectionChanged();
    }
}

const std::vector<FrameId>& IntegrationDataModel::selectedFrameIds() const
{
    return m_impl->selectedFrameIds;
}

const std::vector<GroupKey>& IntegrationDataModel::selectedGroupKeys() const
{
    return m_impl->selectedGroupKeys;
}

std::optional<GroupKey> IntegrationDataModel::selectedRegistrationGroupKey() const
{
    return m_impl->selectedRegistrationGroupKey;
}

void IntegrationDataModel::addFrames(FrameType type, const std::vector<FrameInfo>& frames)
{
    if (frames.empty())
    {
        return;
    }

    auto& collection = m_impl->collections[indexFor(type)];

    // Performance: add in a single pass (FrameCollection coalesces the signal).
    collection->addFrames(frames);

    QStringList added_paths;
    added_paths.reserve(static_cast<int>(frames.size()));
    for (const auto& frame : frames)
    {
        added_paths << frame.displayPath;
    }

    m_impl->invalidateValidation();

    m_impl->groupsDirty[indexFor(type)] = true;
    m_impl->calibrationDirty = true;
    if (type == FrameType::Light)
    {
        m_impl->registrationDirty = true;
        (void)m_impl->sanitizeRegistrationReferenceFrames();
    }
    m_impl->integrationDirty = true;
    m_impl->hasUnsavedChanges = true;

    {
        const int idx = indexFor(type);
        const auto cleared =
            clearConflictingMastersForType(m_impl->masterSources, *collection, type, m_impl->groupingOptions[idx]);
        if (!cleared.empty() && m_impl->batchDepth == 0)
        {
            emit masterSelectionPolicyWarning(
                tr("Master Selection"),
                tr("%1 master frame(s) were unset because new frames were added to their groups.")
                    .arg(static_cast<int>(cleared.size())));
        }
    }

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit framesAdded(type, added_paths);
    emit groupingChanged(type);
    emit calibrationChanged(type);
    if (type == FrameType::Light)
    {
        emit registrationChanged();
    }
    emit integrationGroupsChanged();
    emit settingsChanged();
}

void IntegrationDataModel::addExternalMaster(FrameType type, const FrameInfo& master)
{
    const QString path = master.displayPath;
    if (path.trimmed().isEmpty())
    {
        return;
    }

    // A master picked for a tab is that tab's kind whatever its cards say, and it keeps only the figures a calibration
    // match can use: a sensor temperature a camera can report, a positive ISO or gain, an offset that is not negative.
    const FrameMetadata& read = master.metadata;
    std::optional<double> temperature = read.temperatureC();
    if (temperature.has_value() && !(*temperature > -273.0 && *temperature < 100.0))
    {
        temperature.reset();
    }
    std::optional<int> isoGain = read.isoGain();
    if (isoGain.has_value() && *isoGain <= 0)
    {
        isoGain.reset();
    }
    std::optional<int> offset = read.offset();
    if (offset.has_value() && *offset < 0)
    {
        offset.reset();
    }
    const double exposure = (std::max)(read.exposureSeconds(), 0.0);
    const QDateTime capturedAt =
        read.capturedAtUtc().isValid() ? read.capturedAtUtc() : QDateTime::currentDateTimeUtc();

    const FrameMetadata metadata(type, read.dimensions(), exposure, temperature, isoGain, offset, read.filter(),
                                 read.objectName(), capturedAt, read.readoutMode());
    const FrameInfo frame(FrameId(path), path, metadata, capturedAt.date().toString(Qt::ISODate), QString(), false,
                          std::nullopt);

    addFrames(type, {frame});
    setMasterStatus(path, true, MasterSource::UserDefined);
}

void IntegrationDataModel::removeFrames(FrameType type, const QStringList& paths)
{
    auto& collection = m_impl->collections[indexFor(type)];

    QStringList removed_paths;
    for (const auto& path : paths)
    {
        if (path.trimmed().isEmpty())
        {
            continue;
        }

        FrameId id(path);
        if (collection->removeFrame(id))
        {
            removed_paths << path;
        }
    }

    if (removed_paths.isEmpty())
    {
        return;
    }

    for (const auto& removed_path : removed_paths)
    {
        m_impl->masterSources.remove(FrameId(removed_path));
    }
    if (type == FrameType::Light && !m_impl->stackExcludedFrames.empty())
    {
        const QStringList canonical_removed = canonicalUniquePaths(removed_paths);
        for (auto it = m_impl->stackExcludedFrames.begin(); it != m_impl->stackExcludedFrames.end();)
        {
            for (const auto& canonical : canonical_removed)
            {
                it->second.remove(canonical);
            }

            if (it->second.isEmpty())
            {
                it = m_impl->stackExcludedFrames.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    m_impl->invalidateValidation();
    m_impl->groupsDirty[indexFor(type)] = true;
    m_impl->calibrationDirty = true;
    if (type == FrameType::Light)
    {
        m_impl->registrationDirty = true;
        (void)m_impl->sanitizeRegistrationReferenceFrames();
    }
    m_impl->integrationDirty = true;
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit framesRemoved(type, removed_paths);
    emit groupingChanged(type);
    emit calibrationChanged(type);
    if (type == FrameType::Light)
    {
        emit registrationChanged();
    }
    emit integrationGroupsChanged();
    emit settingsChanged();
}

void IntegrationDataModel::updateFrameAttribute(const QString& path, FrameAttribute attr, const QVariant& value)
{
    if (path.trimmed().isEmpty())
    {
        return;
    }

    FrameId id(path);
    bool updated = false;
    FrameType updated_type = FrameType::Light;

    for (int i = 0; i < static_cast<int>(m_impl->collections.size()); ++i)
    {
        auto& collection = m_impl->collections[static_cast<size_t>(i)];
        if (collection->updateFrame(id, [&](FrameInfo& frame) {
                switch (attr)
                {
                case FrameAttribute::UserSession:
                    frame.userSession = value.toString();
                    break;
                case FrameAttribute::UserObjectName:
                    frame.userObjectName = value.toString();
                    break;
                case FrameAttribute::CustomTag:
                    frame.customTag = value.toString();
                    break;
                case FrameAttribute::Excluded:
                    frame.excluded = value.toBool();
                    break;
                case FrameAttribute::ManualGroupKey:
                    if (value.toString().isEmpty())
                    {
                        frame.manualGroupKey.reset();
                    }
                    else
                    {
                        frame.manualGroupKey = GroupKey{value.toString()};
                    }
                    break;
                }
            }))
        {
            updated = true;
            updated_type = frameTypeForIndex(i);
            break;
        }
    }

    if (!updated)
    {
        return;
    }

    enum class MasterSelectionClearReason
    {
        MasterDeselected,
        OtherFrameSelected
    };

    std::optional<ClearedMasterInfo> cleared_by_selection;
    std::optional<MasterSelectionClearReason> cleared_by_selection_reason;
    std::vector<ClearedMasterInfo> cleared_by_conflict;

    const bool grouping_may_have_changed =
        (attr == FrameAttribute::UserSession) || (attr == FrameAttribute::UserObjectName) ||
        (attr == FrameAttribute::CustomTag) || (attr == FrameAttribute::ManualGroupKey);

    if (attr == FrameAttribute::Excluded || grouping_may_have_changed)
    {
        const int idx = indexFor(updated_type);
        const auto& collection = *m_impl->collections[idx];
        const auto frame_opt = collection.findById(id);
        if (frame_opt.has_value())
        {
            const auto& options = m_impl->groupingOptions[idx];

            if (attr == FrameAttribute::Excluded)
            {
                // If the master itself becomes excluded, it can no longer be a master.
                const auto it = m_impl->masterSources.find(id);
                if (value.toBool() && it != m_impl->masterSources.end() && shouldApplyMasterSelectionPolicy(it.value()))
                {
                    const MasterSource src = it.value();
                    m_impl->masterSources.remove(id);
                    cleared_by_selection =
                        ClearedMasterInfo{id, src, groupKeyForFrame(updated_type, *frame_opt, options)};
                    cleared_by_selection_reason = MasterSelectionClearReason::MasterDeselected;
                }

                // If another frame in a master group is re-selected, unset the master (user intent: stack the group).
                if (!value.toBool())
                {
                    const GroupKey group_key = groupKeyForFrame(updated_type, *frame_opt, options);
                    cleared_by_selection = clearMasterForGroupIfPresent(m_impl->masterSources, collection, updated_type,
                                                                        options, group_key, id);
                    if (cleared_by_selection.has_value())
                    {
                        cleared_by_selection_reason = MasterSelectionClearReason::OtherFrameSelected;
                    }
                }
            }

            if (grouping_may_have_changed)
            {
                cleared_by_conflict =
                    clearConflictingMastersForType(m_impl->masterSources, collection, updated_type, options);
            }
        }
    }

    m_impl->invalidateValidation();

    m_impl->groupsDirty[indexFor(updated_type)] = true;
    m_impl->calibrationDirty = true;
    if (updated_type == FrameType::Light)
    {
        m_impl->registrationDirty = true;
        (void)m_impl->sanitizeRegistrationReferenceFrames();
    }
    m_impl->integrationDirty = true;
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    if (cleared_by_selection.has_value())
    {
        const QString master_name = QFileInfo(cleared_by_selection->id.canonicalPath()).fileName();
        const QString group_label = cleared_by_selection->groupKey.toCompactString();
        if (cleared_by_selection_reason == MasterSelectionClearReason::MasterDeselected)
        {
            emit masterSelectionPolicyWarning(
                tr("Master Selection"),
                tr("Master \"%1\" was unset because it was deselected (group \"%2\").").arg(master_name, group_label));
        }
        else
        {
            emit masterSelectionPolicyWarning(
                tr("Master Selection"),
                tr("Master \"%1\" was unset because another frame in group \"%2\" was selected.")
                    .arg(master_name, group_label));
        }
    }
    else if (!cleared_by_conflict.empty())
    {
        emit masterSelectionPolicyWarning(tr("Master Selection"),
                                          tr("%1 master frame(s) were unset because their groups are not master-only.")
                                              .arg(static_cast<int>(cleared_by_conflict.size())));
    }

    emit frameUpdated(path);
    emit groupingChanged(updated_type);
    emit calibrationChanged(updated_type);
    if (updated_type == FrameType::Light)
    {
        emit registrationChanged();
    }
    emit integrationGroupsChanged();
    emit settingsChanged();
}

void IntegrationDataModel::setFramesExcluded(FrameType type, const QStringList& paths, bool excluded)
{
    if (paths.isEmpty())
    {
        return;
    }

    const int idx = indexFor(type);
    auto& collection = *m_impl->collections[idx];
    const auto& options = m_impl->groupingOptions[idx];

    QStringList canonical_paths;
    canonical_paths.reserve(paths.size());

    std::vector<FrameId> ids;
    ids.reserve(static_cast<size_t>(paths.size()));

    QSet<QString> seen;
    for (const auto& rawPath : paths)
    {
        if (rawPath.trimmed().isEmpty())
        {
            continue;
        }

        const FrameId id(rawPath);
        const QString canonical = id.canonicalPath();
        if (canonical.isEmpty() || seen.contains(canonical))
        {
            continue;
        }

        seen.insert(canonical);
        canonical_paths << canonical;
        ids.push_back(id);
    }

    if (ids.empty())
    {
        return;
    }

    QStringList changed_paths;
    changed_paths.reserve(canonical_paths.size());

    for (size_t i = 0; i < ids.size(); ++i)
    {
        const FrameId& id = ids[i];
        bool did_change = false;
        const bool found = collection.updateFrame(id, [&](FrameInfo& frame) {
            if (frame.excluded == excluded)
            {
                return;
            }
            frame.excluded = excluded;
            did_change = true;
        });

        if (found && did_change)
        {
            changed_paths << canonical_paths[static_cast<int>(i)];
        }
    }

    if (changed_paths.isEmpty())
    {
        return;
    }

    std::vector<ClearedMasterInfo> cleared_by_exclusion;
    std::vector<ClearedMasterInfo> cleared_by_reselect;

    if (excluded)
    {
        for (const auto& canonical : changed_paths)
        {
            const FrameId id(canonical);
            const auto it = m_impl->masterSources.find(id);
            if (it == m_impl->masterSources.end() || !shouldApplyMasterSelectionPolicy(it.value()))
            {
                continue;
            }

            const auto frame_opt = collection.findById(id);
            if (!frame_opt.has_value())
            {
                continue;
            }

            const MasterSource src = it.value();
            m_impl->masterSources.remove(id);
            cleared_by_exclusion.push_back(ClearedMasterInfo{id, src, groupKeyForFrame(type, *frame_opt, options)});
        }
    }
    else
    {
        QHash<GroupKey, FrameId> keep_selected_by_group;
        keep_selected_by_group.reserve(changed_paths.size());

        for (const auto& canonical : changed_paths)
        {
            const FrameId id(canonical);
            const auto frame_opt = collection.findById(id);
            if (!frame_opt.has_value())
            {
                continue;
            }

            const GroupKey group_key = groupKeyForFrame(type, *frame_opt, options);
            if (!keep_selected_by_group.contains(group_key))
            {
                keep_selected_by_group.insert(group_key, id);
            }
        }

        for (auto it = keep_selected_by_group.constBegin(); it != keep_selected_by_group.constEnd(); ++it)
        {
            const GroupKey& group_key = it.key();
            const FrameId& keep_id = it.value();
            const auto cleared =
                clearMasterForGroupIfPresent(m_impl->masterSources, collection, type, options, group_key, keep_id);
            if (cleared.has_value())
            {
                cleared_by_reselect.push_back(*cleared);
            }
        }
    }

    m_impl->invalidateValidation();

    m_impl->groupsDirty[static_cast<size_t>(idx)] = true;
    m_impl->calibrationDirty = true;
    if (type == FrameType::Light)
    {
        m_impl->registrationDirty = true;
        (void)m_impl->sanitizeRegistrationReferenceFrames();
    }
    m_impl->integrationDirty = true;
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    if (cleared_by_exclusion.size() == 1)
    {
        const auto& info = cleared_by_exclusion.front();
        const QString master_name = QFileInfo(info.id.canonicalPath()).fileName();
        const QString group_label = info.groupKey.toCompactString();
        emit masterSelectionPolicyWarning(
            tr("Master Selection"),
            tr("Master \"%1\" was unset because it was deselected (group \"%2\").").arg(master_name, group_label));
    }
    else if (!cleared_by_exclusion.empty())
    {
        emit masterSelectionPolicyWarning(tr("Master Selection"),
                                          tr("%1 master frame(s) were unset because they were excluded.")
                                              .arg(static_cast<int>(cleared_by_exclusion.size())));
    }

    if (cleared_by_reselect.size() == 1)
    {
        const auto& info = cleared_by_reselect.front();
        const QString master_name = QFileInfo(info.id.canonicalPath()).fileName();
        const QString group_label = info.groupKey.toCompactString();
        emit masterSelectionPolicyWarning(
            tr("Master Selection"), tr("Master \"%1\" was unset because another frame in group \"%2\" was selected.")
                                        .arg(master_name, group_label));
    }
    else if (!cleared_by_reselect.empty())
    {
        emit masterSelectionPolicyWarning(
            tr("Master Selection"),
            tr("%1 master frame(s) were unset because other frames in their groups were included.")
                .arg(static_cast<int>(cleared_by_reselect.size())));
    }

    emit groupingChanged(type);
    emit calibrationChanged(type);
    if (type == FrameType::Light)
    {
        emit registrationChanged();
    }
    emit integrationGroupsChanged();
    emit settingsChanged();
}

void IntegrationDataModel::setFramesStackExcluded(const GroupKey& integrationGroupKey, const QStringList& paths,
                                                  bool excluded)
{
    if (integrationGroupKey.toString().trimmed().isEmpty() || paths.isEmpty())
    {
        return;
    }

    const QStringList canonical_paths = canonicalUniquePaths(paths);
    if (canonical_paths.isEmpty())
    {
        return;
    }

    auto& excluded_paths = m_impl->stackExcludedFrames[integrationGroupKey];
    bool changed = false;
    for (const auto& canonical : canonical_paths)
    {
        if (excluded)
        {
            if (!excluded_paths.contains(canonical))
            {
                excluded_paths.insert(canonical);
                changed = true;
            }
        }
        else if (excluded_paths.remove(canonical))
        {
            changed = true;
        }
    }

    if (!changed)
    {
        if (excluded_paths.isEmpty())
        {
            m_impl->stackExcludedFrames.erase(integrationGroupKey);
        }
        return;
    }

    if (excluded_paths.isEmpty())
    {
        m_impl->stackExcludedFrames.erase(integrationGroupKey);
    }

    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit stackExclusionsChanged(integrationGroupKey);
    emit settingsChanged();
}

void IntegrationDataModel::clearStackExcludedFrames(const GroupKey& integrationGroupKey)
{
    if (integrationGroupKey.toString().trimmed().isEmpty())
    {
        return;
    }

    const auto erased = m_impl->stackExcludedFrames.erase(integrationGroupKey);
    if (erased == 0)
    {
        return;
    }

    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit stackExclusionsChanged(integrationGroupKey);
    emit settingsChanged();
}

void IntegrationDataModel::clearAllSessionAndObjectOverrides()
{
    bool has_any_overrides = false;
    for (int i = 0; i < static_cast<int>(m_impl->collections.size()); ++i)
    {
        const auto& collection = m_impl->collections[static_cast<size_t>(i)];
        for (const auto& frame : collection->frames())
        {
            if (!frame.userSession.trimmed().isEmpty() || !frame.userObjectName.trimmed().isEmpty())
            {
                has_any_overrides = true;
                break;
            }
        }
        if (has_any_overrides)
        {
            break;
        }
    }

    if (!has_any_overrides)
    {
        return;
    }

    beginBatchUpdate();
    for (int i = 0; i < static_cast<int>(m_impl->collections.size()); ++i)
    {
        const auto& collection = m_impl->collections[static_cast<size_t>(i)];
        for (const auto& frame : collection->frames())
        {
            const QString canonicalPath = frame.id.canonicalPath();
            if (!frame.userSession.trimmed().isEmpty())
            {
                updateFrameAttribute(canonicalPath, FrameAttribute::UserSession, QString());
            }
            if (!frame.userObjectName.trimmed().isEmpty())
            {
                updateFrameAttribute(canonicalPath, FrameAttribute::UserObjectName, QString());
            }
        }
    }
    endBatchUpdate();
}

void IntegrationDataModel::setMasterStatus(const QString& path, bool isMaster, MasterSource source)
{
    if (path.trimmed().isEmpty())
    {
        return;
    }

    FrameId id(path);

    // Determine which frame type this ID belongs to.
    std::optional<FrameType> frame_type;
    for (int i = 0; i < 4; ++i)
    {
        const auto type = static_cast<FrameType>(i);
        if (m_impl->collections[indexFor(type)]->findById(id).has_value())
        {
            frame_type = type;
            break;
        }
    }

    if (!frame_type)
    {
        // If the frame isn't in the model, do nothing.
        return;
    }

    const int idx = indexFor(*frame_type);
    auto& collection = *m_impl->collections[idx];
    const auto frame_opt = collection.findById(id);
    if (!frame_opt)
    {
        return;
    }

    const GroupKey target_group_key = groupKeyForFrame(*frame_type, *frame_opt, m_impl->groupingOptions[idx]);

    bool changed = false;

    if (!isMaster)
    {
        changed = m_impl->masterSources.remove(id) != 0;
    }
    else
    {
        // Auto-detected masters should not override any existing master in the same group.
        if (source == MasterSource::AutoDetected)
        {
            for (auto it = m_impl->masterSources.begin(); it != m_impl->masterSources.end(); ++it)
            {
                if (it.key() == id)
                {
                    continue;
                }

                const auto other_opt = collection.findById(it.key());
                if (!other_opt)
                {
                    continue;
                }

                if (groupKeyForFrame(*frame_type, *other_opt, m_impl->groupingOptions[idx]) == target_group_key)
                {
                    return;
                }
            }
        }

        // Enforce: at most one master per stacking group.
        {
            std::vector<FrameId> to_clear;
            for (auto it = m_impl->masterSources.begin(); it != m_impl->masterSources.end(); ++it)
            {
                if (it.key() == id)
                {
                    continue;
                }

                const auto other_opt = collection.findById(it.key());
                if (!other_opt)
                {
                    continue;
                }

                if (groupKeyForFrame(*frame_type, *other_opt, m_impl->groupingOptions[idx]) == target_group_key)
                {
                    to_clear.push_back(it.key());
                }
            }

            for (const auto& other_id : to_clear)
            {
                changed = (m_impl->masterSources.remove(other_id) != 0) || changed;
            }
        }

        auto it = m_impl->masterSources.find(id);
        if (it == m_impl->masterSources.end() || it.value() != source)
        {
            m_impl->masterSources.insert(id, source);
            changed = true;
        }
    }

    int deselected_other_frames = 0;
    if (isMaster && changed && shouldApplyMasterSelectionPolicy(source))
    {
        // When a frame is explicitly designated as a master, it must be the only selected frame
        // in its stacking group (the rest are deselected).
        const auto group_frames = collection.filterByGroup(target_group_key, *frame_type, m_impl->groupingOptions[idx]);
        for (const auto& frame : group_frames)
        {
            if (!frame.excluded && frame.id != id)
            {
                ++deselected_other_frames;
            }
        }

        (void)enforceExclusiveSelectionForMaster(collection, *frame_type, m_impl->groupingOptions[idx],
                                                 target_group_key, id);
    }

    if (!changed)
    {
        return;
    }

    m_impl->invalidateValidation();
    m_impl->groupsDirty[idx] = true;
    m_impl->calibrationDirty = true;
    m_impl->integrationDirty = true;
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    if (deselected_other_frames > 0 && source == MasterSource::UserDefined)
    {
        const QString master_name = QFileInfo(id.canonicalPath()).fileName();
        emit masterSelectionPolicyWarning(tr("Master Selection"),
                                          tr("Setting \"%1\" as master deselected other frames in group \"%2\".")
                                              .arg(master_name, target_group_key.toString()));
    }

    emit frameUpdated(path);
    emit groupingChanged(*frame_type);
    emit calibrationChanged(*frame_type);
    emit integrationGroupsChanged();
    emit settingsChanged();
}

void IntegrationDataModel::setGroupingOptions(FrameType type, const GroupingOptions& options)
{
    const GroupingOptions normalized = normalizeGroupingOptionsForFrameType(type, options);

    auto& existing = m_impl->groupingOptions[indexFor(type)];
    if (existing == normalized)
    {
        return;
    }
    m_impl->invalidateValidation();

    const int idx = indexFor(type);
    const bool had_stacking_settings = !m_impl->stackingSettings[idx].empty();
    // Registration grouping/settings are independent of light grouping options.
    const bool had_registration_settings = false;
    const bool had_light_calibration_settings = (type == FrameType::Light) && !m_impl->lightCalibrationSettings.empty();
    const bool had_calibration_overrides = (type == FrameType::Light) && !m_impl->calibrationOverrides.empty();
    if (had_stacking_settings)
    {
        m_impl->stackingSettings[idx].clear();
    }
    (void)had_registration_settings;
    if (had_light_calibration_settings)
    {
        m_impl->lightCalibrationSettings.clear();
    }
    if (had_calibration_overrides)
    {
        m_impl->calibrationOverrides.clear();
    }

    existing = normalized;
    m_impl->groupsDirty[idx] = true;
    m_impl->calibrationDirty = true;
    m_impl->integrationDirty = true;
    m_impl->hasUnsavedChanges = true;

    {
        const auto cleared =
            clearConflictingMastersForType(m_impl->masterSources, *m_impl->collections[idx], type, existing);

        // After clearing, try to re-detect masters for single-frame groups whose
        // file path matches the master-filename heuristic. This restores masters
        // that were lost when groups merged (e.g. "by Exposure" toggled off) and
        // then split again (toggled back on).
        const auto restored =
            redetectAutoMastersForType(m_impl->masterSources, *m_impl->collections[idx], type, existing);

        const int net_cleared = static_cast<int>(cleared.size()) - static_cast<int>(restored.size());
        if (net_cleared > 0 && m_impl->batchDepth == 0)
        {
            emit masterSelectionPolicyWarning(
                tr("Master Selection"), tr("%1 master frame(s) were unset because grouping changed.").arg(net_cleared));
        }
    }

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        if (had_stacking_settings || had_registration_settings || had_light_calibration_settings ||
            had_calibration_overrides)
        {
            m_impl->pendingRegroupResetSettings[static_cast<size_t>(idx)] = true;
        }
        return;
    }

    if (had_stacking_settings || had_registration_settings || had_light_calibration_settings ||
        had_calibration_overrides)
    {
        emit regroupResetSettings(type);
    }
    emit groupingChanged(type);
    emit calibrationChanged(type);
    emit settingsChanged();
}

void IntegrationDataModel::setStackingSettings(FrameType type, const GroupKey& key, const StackingSettings& settings)
{
    m_impl->stackingSettings[indexFor(type)][key] = settings;
    m_impl->invalidateValidation();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit settingsChanged();
}

void IntegrationDataModel::setCalibrationOverride(const LightGroupKey& key, CalibrationOverrideType type,
                                                  const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        clearCalibrationOverride(key, type);
        return;
    }

    CalibrationOverride override;
    override.lightKey = key;
    override.type = type;
    override.masterPath = path;
    override.groupKey = GroupKey{path};

    auto existing = std::find_if(
        m_impl->calibrationOverrides.begin(), m_impl->calibrationOverrides.end(),
        [&](const CalibrationOverride& entry) { return entry.type == type && lightKeysEqual(entry.lightKey, key); });
    if (existing != m_impl->calibrationOverrides.end())
    {
        *existing = override;
    }
    else
    {
        m_impl->calibrationOverrides.push_back(override);
    }

    m_impl->calibrationDirty = true;
    m_impl->invalidateValidation();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit calibrationChanged(FrameType::Light);
}

void IntegrationDataModel::clearCalibrationOverride(const LightGroupKey& key, CalibrationOverrideType type)
{
    const auto matches = [&](const CalibrationOverride& entry) {
        return entry.type == type && lightKeysEqual(entry.lightKey, key);
    };

    auto& overrides = m_impl->calibrationOverrides;
    auto it = std::remove_if(overrides.begin(), overrides.end(), matches);
    if (it == overrides.end())
    {
        return;
    }

    overrides.erase(it, overrides.end());
    m_impl->calibrationDirty = true;
    m_impl->invalidateValidation();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit calibrationChanged(FrameType::Light);
}

void IntegrationDataModel::setRegistrationSettings(const GroupKey& key, const RegistrationSettings& settings)
{
    m_impl->invalidateValidation();
    RegistrationSettings updated = settings;
    const auto& light_collection = *m_impl->collections[indexFor(FrameType::Light)];

    const QString ref_path = updated.referenceImage.trimmed();
    if (!ref_path.isEmpty())
    {
        const FrameId ref_id(ref_path);
        const auto frame_opt = light_collection.findById(ref_id);
        if (!frame_opt.has_value() || frame_opt->excluded)
        {
            updated.referenceImage.clear();
        }
        else
        {
            const GroupKey expected =
                RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(*frame_opt));
            if (expected != key)
            {
                updated.referenceImage.clear();
            }
            else
            {
                updated.referenceImage = ref_id.canonicalPath();
            }
        }
    }

    RegistrationSettings::ManualControlPointMap sanitized_manual_points;
    for (auto it = updated.manualControlPointsByFrame.cbegin(); it != updated.manualControlPointsByFrame.cend(); ++it)
    {
        const QString raw_path = it.key().trimmed();
        if (raw_path.isEmpty())
        {
            continue;
        }

        QString canonical_path;
        std::optional<FrameInfo> frame_opt;
        try
        {
            const FrameId frame_id(raw_path);
            canonical_path = frame_id.canonicalPath();
            frame_opt = light_collection.findById(frame_id);
        }
        catch (...)
        {
            continue;
        }

        if (!frame_opt.has_value() || frame_opt->excluded || canonical_path == updated.referenceImage)
        {
            continue;
        }

        const GroupKey expected =
            RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(*frame_opt));
        if (expected != key)
        {
            continue;
        }

        RegistrationSettings::ManualControlPointList sanitized_points;
        sanitized_points.reserve(it.value().size());
        for (const auto& point : it.value())
        {
            if (!std::isfinite(point.sourcePoint.x()) || !std::isfinite(point.sourcePoint.y()) ||
                !std::isfinite(point.referencePoint.x()) || !std::isfinite(point.referencePoint.y()))
            {
                continue;
            }
            sanitized_points.push_back(point);
        }

        if (!sanitized_points.isEmpty())
        {
            sanitized_manual_points.insert(canonical_path, sanitized_points);
        }
    }
    updated.manualControlPointsByFrame = std::move(sanitized_manual_points);

    if (updated.cometTracking.has_value())
    {
        auto comet = *updated.cometTracking;
        QSet<QString> enabled_observation_frames;
        QVector<RegistrationSettings::CometTrackingObservation> sanitized_observations;
        sanitized_observations.reserve(comet.observations.size());

        for (auto observation : comet.observations)
        {
            observation.frameId = observation.frameId.trimmed();
            if (observation.frameId.isEmpty() || !std::isfinite(observation.sourcePosition.x()) ||
                !std::isfinite(observation.sourcePosition.y()))
            {
                continue;
            }

            try
            {
                const FrameId frame_id(observation.frameId);
                const auto frame_opt = light_collection.findById(frame_id);
                if (!frame_opt.has_value() || frame_opt->excluded)
                {
                    continue;
                }

                const GroupKey expected =
                    RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(*frame_opt));
                if (expected != key)
                {
                    continue;
                }

                observation.frameId = frame_id.canonicalPath();
            }
            catch (...)
            {
                continue;
            }

            if (observation.enabled)
            {
                if (enabled_observation_frames.contains(observation.frameId))
                {
                    continue;
                }
                enabled_observation_frames.insert(observation.frameId);
            }
            sanitized_observations.push_back(observation);
        }

        comet.observations = std::move(sanitized_observations);
        updated.cometTracking = comet;
    }

    if (updated.cometTracking.has_value())
    {
        auto& comet = *updated.cometTracking;
        if (comet.schemaVersion <= 0)
        {
            comet.schemaVersion = 1;
        }
    }

    auto& ca = updated.chromaticAberration;
    ca.edgeFraction = std::clamp(ca.edgeFraction, 0.30, 0.80);
    ca.minMatchedStars = std::clamp(ca.minMatchedStars, 3, 50);
    if (ca.transformType.trimmed().compare(QStringLiteral("Affine"), Qt::CaseInsensitive) == 0)
    {
        ca.transformType = QStringLiteral("Affine");
    }
    else
    {
        ca.transformType = QStringLiteral("Similarity");
    }
    if (ca.interpolation.trimmed().isEmpty())
    {
        ca.interpolation = QStringLiteral("Lanczos4");
    }

    // Always bump version on explicit writes to keep metrics DB keying deterministic.
    ++updated.version;
    m_impl->registrationSettings[key] = std::move(updated);
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit registrationChanged();
    emit settingsChanged();
}

void IntegrationDataModel::setLightCalibrationSettings(const GroupKey& key, const LightCalibrationSettings& settings)
{
    m_impl->lightCalibrationSettings[key] = settings;
    m_impl->calibrationDirty = true;
    m_impl->invalidateValidation();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit calibrationChanged(FrameType::Light);
    emit settingsChanged();
}

void IntegrationDataModel::clearLightCalibrationSettings(const GroupKey& key)
{
    if (m_impl->lightCalibrationSettings.erase(key) == 0)
    {
        return;
    }

    m_impl->calibrationDirty = true;
    m_impl->invalidateValidation();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit calibrationChanged(FrameType::Light);
    emit settingsChanged();
}

void IntegrationDataModel::clearAllLightCalibrationSettings()
{
    if (m_impl->lightCalibrationSettings.empty())
    {
        return;
    }

    m_impl->lightCalibrationSettings.clear();
    m_impl->calibrationDirty = true;
    m_impl->invalidateValidation();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit calibrationChanged(FrameType::Light);
    emit settingsChanged();
}

void IntegrationDataModel::setOutputDirectory(const QString& path)
{
    if (m_impl->outputDirectory == path)
    {
        return;
    }

    m_impl->invalidateValidation();

    m_impl->outputDirectory = path;
    m_impl->qualityMetrics.clear();
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit outputDirectoryChanged(path);
}

const QString& IntegrationDataModel::outputDirectory() const
{
    return m_impl->outputDirectory;
}

void IntegrationDataModel::setLightCalibrationCacheMode(LightCalibrationCacheMode mode)
{
    if (m_impl->lightCalibrationCacheMode == mode)
    {
        return;
    }

    m_impl->lightCalibrationCacheMode = mode;
    m_impl->hasUnsavedChanges = true;

    if (m_impl->batchDepth > 0)
    {
        m_impl->pendingReset = true;
        return;
    }

    emit settingsChanged();
}

LightCalibrationCacheMode IntegrationDataModel::lightCalibrationCacheMode() const
{
    return m_impl->lightCalibrationCacheMode;
}

void IntegrationDataModel::reset()
{
    beginBatchUpdate();

    for (auto& collection : m_impl->collections)
    {
        collection->clear();
    }
    m_impl->stackingSettings = {};
    m_impl->registrationSettings.clear();
    m_impl->calibrationOverrides.clear();
    m_impl->lightCalibrationSettings.clear();
    m_impl->stackExcludedFrames.clear();
    m_impl->masterSources.clear();
    m_impl->qualityMetrics.clear();
    m_impl->selectedFrameIds.clear();
    m_impl->selectedGroupKeys.clear();
    m_impl->selectedRegistrationGroupKey.reset();
    m_impl->qualityMetrics.clear();
    m_impl->outputDirectory.clear();
    m_impl->lightCalibrationCacheMode = LightCalibrationCacheMode::InMemory;

    // Reset default grouping options
    for (size_t i = 0; i < 4; ++i)
    {
        const FrameType type = frameTypeForIndex(static_cast<int>(i));
        GroupingOptions base_options;
        if (type == FrameType::Dark)
        {
            base_options.byExposure = true;
        }
        else if (type == FrameType::Flat)
        {
            base_options.byObject = false;
        }
        m_impl->groupingOptions[i] = normalizeGroupingOptionsForFrameType(type, base_options);
    }

    // Reset integration grouping options
    m_impl->lightIntegrationGroupingOptions.byResolution = true;
    m_impl->lightIntegrationGroupingOptions.byFilter = true;
    m_impl->lightIntegrationGroupingOptions.byObject = true;
    m_impl->lightIntegrationGroupingOptions.bySession = false;
    m_impl->lightIntegrationGroupingOptions.byIsoGain = false;
    m_impl->lightIntegrationGroupingOptions.byOffset = true;
    m_impl->lightIntegrationGroupingOptions.byExposure = false;
    m_impl->lightIntegrationGroupingOptions.exposureToleranceSeconds = 3.0;
    m_impl->lightIntegrationGroupingOptions.byTemperature = false;
    m_impl->lightIntegrationGroupingOptions.byCustomTag = false;

    // Reset calibration policy
    m_impl->calibrationPolicy = CalibrationMatchingPolicy{};

    // Reset dirty flags
    m_impl->groupsDirty.fill(true);
    m_impl->calibrationDirty = true;
    m_impl->registrationDirty = true;
    m_impl->integrationDirty = true;
    m_impl->hasUnsavedChanges = false;
    m_impl->pendingReset = true;

    // Trigger full reset of all regroup listeners
    m_impl->pendingRegroupResetSettings.fill(true);

    endBatchUpdate();
}

void IntegrationDataModel::beginBatchUpdate()
{
    ++m_impl->batchDepth;
    for (auto& collection : m_impl->collections)
    {
        collection->beginBatchUpdate();
    }
}

void IntegrationDataModel::endBatchUpdate()
{
    if (m_impl->batchDepth == 0)
    {
        return;
    }

    --m_impl->batchDepth;
    for (auto& collection : m_impl->collections)
    {
        collection->endBatchUpdate();
    }

    if (m_impl->batchDepth == 0 && m_impl->pendingReset)
    {
        m_impl->pendingReset = false;
        emit modelReset();

        for (int i = 0; i < 4; ++i)
        {
            if (!m_impl->pendingRegroupResetSettings[static_cast<size_t>(i)])
            {
                continue;
            }

            m_impl->pendingRegroupResetSettings[static_cast<size_t>(i)] = false;
            emit regroupResetSettings(frameTypeForIndex(i));
        }
    }
}

QJsonObject IntegrationDataModel::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("version"), kModelJsonVersion);
    obj.insert(QStringLiteral("outputDirectory"), m_impl->outputDirectory);
    obj.insert(QStringLiteral("lightCalibrationCacheMode"), static_cast<int>(m_impl->lightCalibrationCacheMode));

    QJsonObject calibration_policy;
    calibration_policy.insert(QStringLiteral("allowFlatSessionFallback"),
                              m_impl->calibrationPolicy.allowFlatSessionFallback);
    calibration_policy.insert(QStringLiteral("maxFlatSessionDaysApart"),
                              m_impl->calibrationPolicy.maxFlatSessionDaysApart);
    calibration_policy.insert(QStringLiteral("preferNewerFlat"), m_impl->calibrationPolicy.preferNewerFlat);
    calibration_policy.insert(QStringLiteral("allowDarkExposureFallback"),
                              m_impl->calibrationPolicy.allowDarkExposureFallback);
    calibration_policy.insert(QStringLiteral("darkExposureTolerancePercent"),
                              m_impl->calibrationPolicy.darkExposureTolerancePercent);
    calibration_policy.insert(QStringLiteral("warnOnFallback"), m_impl->calibrationPolicy.warnOnFallback);
    calibration_policy.insert(QStringLiteral("subtractBiasFromDark"), m_impl->calibrationPolicy.subtractBiasFromDark);
    obj.insert(QStringLiteral("calibrationPolicy"), calibration_policy);

    QJsonObject grouping;
    for (int i = 0; i < 4; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        grouping.insert(toString(type), groupingOptionsToJson(m_impl->groupingOptions[static_cast<size_t>(i)]));
    }
    obj.insert(QStringLiteral("groupingOptions"), grouping);
    obj.insert(QStringLiteral("lightIntegrationGroupingOptions"),
               groupingOptionsToJson(m_impl->lightIntegrationGroupingOptions));

    QJsonObject frames_root;
    for (int i = 0; i < 4; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        const auto& collection = m_impl->collections[static_cast<size_t>(i)];

        QJsonArray frames;
        for (const auto& frame : collection->frames())
        {
            frames.append(frameInfoToJson(frame));
        }
        frames_root.insert(toString(type), frames);
    }
    obj.insert(QStringLiteral("frames"), frames_root);

    QJsonObject stacking_root;
    for (int i = 0; i < 4; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        const auto& map = m_impl->stackingSettings[static_cast<size_t>(i)];
        QJsonArray entries;
        for (const auto& entry : map)
        {
            QJsonObject record;
            record.insert(QStringLiteral("groupKey"), entry.first.toJson());
            record.insert(QStringLiteral("settings"), stackingSettingsToJson(entry.second));
            entries.append(record);
        }
        stacking_root.insert(toString(type), entries);
    }
    obj.insert(QStringLiteral("stackingSettings"), stacking_root);

    QJsonArray stack_exclusion_entries;
    for (const auto& entry : m_impl->stackExcludedFrames)
    {
        if (entry.second.isEmpty())
        {
            continue;
        }

        QStringList paths;
        paths.reserve(entry.second.size());
        for (const auto& path : entry.second)
        {
            paths.push_back(path);
        }
        paths.sort();

        QJsonArray frame_entries;
        for (const auto& path : paths)
        {
            frame_entries.append(path);
        }

        QJsonObject record;
        record.insert(QStringLiteral("groupKey"), entry.first.toJson());
        record.insert(QStringLiteral("frames"), frame_entries);
        stack_exclusion_entries.append(record);
    }
    obj.insert(QStringLiteral("stackExcludedFrames"), stack_exclusion_entries);

    QJsonArray registration_entries;
    for (const auto& entry : m_impl->registrationSettings)
    {
        QJsonObject record;
        record.insert(QStringLiteral("groupKey"), entry.first.toJson());
        record.insert(QStringLiteral("settings"), registrationSettingsToJson(entry.second));
        registration_entries.append(record);
    }
    obj.insert(QStringLiteral("registrationSettings"), registration_entries);

    QJsonArray light_settings;
    for (const auto& entry : m_impl->lightCalibrationSettings)
    {
        QJsonObject record;
        record.insert(QStringLiteral("groupKey"), entry.first.toJson());
        record.insert(QStringLiteral("settings"), lightCalibrationSettingsToJson(entry.second));
        light_settings.append(record);
    }
    obj.insert(QStringLiteral("lightCalibrationSettings"), light_settings);

    QJsonArray override_entries;
    for (const auto& entry : m_impl->calibrationOverrides)
    {
        QJsonObject record;
        record.insert(QStringLiteral("lightKey"), lightGroupKeyToJson(entry.lightKey));
        record.insert(QStringLiteral("type"), static_cast<int>(entry.type));
        record.insert(QStringLiteral("masterPath"), entry.masterPath);
        record.insert(QStringLiteral("groupKey"), entry.groupKey.toJson());
        override_entries.append(record);
    }
    obj.insert(QStringLiteral("calibrationOverrides"), override_entries);

    QJsonArray master_entries;
    for (auto it = m_impl->masterSources.constBegin(); it != m_impl->masterSources.constEnd(); ++it)
    {
        QJsonObject record;
        record.insert(QStringLiteral("frameId"), it.key().toJson());
        record.insert(QStringLiteral("source"), static_cast<int>(it.value()));
        master_entries.append(record);
    }
    obj.insert(QStringLiteral("masterSources"), master_entries);

    return obj;
}

void IntegrationDataModel::fromJson(const QJsonObject& json)
{
    // The version a newer model states is said; GroupKey::fromJson reads a key as a string or as an object.
    const int schema_version = json.value(QStringLiteral("version")).toInt(0);
    if (schema_version > kModelJsonVersion)
    {
        LOG_WARNING("IntegrationDataModel: JSON schema version {} is newer than supported version {}. "
                    "Some settings may not load correctly.",
                    schema_version, kModelJsonVersion);
    }

    beginBatchUpdate();

    for (auto& collection : m_impl->collections)
    {
        collection->clear();
    }
    m_impl->stackingSettings = {};
    m_impl->registrationSettings.clear();
    m_impl->calibrationOverrides.clear();
    m_impl->lightCalibrationSettings.clear();
    m_impl->stackExcludedFrames.clear();
    m_impl->masterSources.clear();
    m_impl->qualityMetrics.clear();
    m_impl->selectedFrameIds.clear();
    m_impl->selectedGroupKeys.clear();
    m_impl->selectedRegistrationGroupKey.reset();

    const QString new_output_directory = json.value(QStringLiteral("outputDirectory")).toString();
    const bool output_changed = (m_impl->outputDirectory != new_output_directory);
    m_impl->outputDirectory = new_output_directory;

    const int rawCacheMode = json.value(QStringLiteral("lightCalibrationCacheMode"))
                                 .toInt(static_cast<int>(LightCalibrationCacheMode::InMemory));
    m_impl->lightCalibrationCacheMode = (rawCacheMode >= static_cast<int>(LightCalibrationCacheMode::Disk) &&
                                         rawCacheMode <= static_cast<int>(LightCalibrationCacheMode::InMemory))
                                            ? static_cast<LightCalibrationCacheMode>(rawCacheMode)
                                            : LightCalibrationCacheMode::InMemory;

    if (json.contains(QStringLiteral("lightIntegrationGroupingOptions")))
    {
        m_impl->lightIntegrationGroupingOptions =
            groupingOptionsFromJson(json.value(QStringLiteral("lightIntegrationGroupingOptions")).toObject());
    }
    // Else keep defaults set in Impl constructor

    const QJsonObject calibration_policy = json.value(QStringLiteral("calibrationPolicy")).toObject();
    if (!calibration_policy.isEmpty())
    {
        CalibrationMatchingPolicy policy;
        policy.allowFlatSessionFallback = calibration_policy.value(QStringLiteral("allowFlatSessionFallback"))
                                              .toBool(policy.allowFlatSessionFallback);
        policy.maxFlatSessionDaysApart =
            calibration_policy.value(QStringLiteral("maxFlatSessionDaysApart")).toInt(policy.maxFlatSessionDaysApart);
        policy.preferNewerFlat =
            calibration_policy.value(QStringLiteral("preferNewerFlat")).toBool(policy.preferNewerFlat);
        policy.allowDarkExposureFallback = calibration_policy.value(QStringLiteral("allowDarkExposureFallback"))
                                               .toBool(policy.allowDarkExposureFallback);
        policy.darkExposureTolerancePercent = calibration_policy.value(QStringLiteral("darkExposureTolerancePercent"))
                                                  .toInt(policy.darkExposureTolerancePercent);
        policy.warnOnFallback =
            calibration_policy.value(QStringLiteral("warnOnFallback")).toBool(policy.warnOnFallback);
        policy.subtractBiasFromDark =
            calibration_policy.value(QStringLiteral("subtractBiasFromDark")).toBool(policy.subtractBiasFromDark);
        m_impl->calibrationPolicy = policy;
    }

    const QJsonObject grouping = json.value(QStringLiteral("groupingOptions")).toObject();
    for (int i = 0; i < 4; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        const QJsonObject entry = grouping.value(toString(type)).toObject();
        if (!entry.isEmpty())
        {
            m_impl->groupingOptions[static_cast<size_t>(i)] =
                normalizeGroupingOptionsForFrameType(type, groupingOptionsFromJson(entry));
        }
    }

    const QJsonObject frames_root = json.value(QStringLiteral("frames")).toObject();
    for (int i = 0; i < 4; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        const QJsonArray frames = frames_root.value(toString(type)).toArray();
        std::vector<FrameInfo> parsed;
        parsed.reserve(frames.size());
        for (const auto& value : frames)
        {
            if (!value.isObject())
            {
                continue;
            }
            if (auto frame = frameInfoFromJson(value.toObject(), type))
            {
                parsed.push_back(std::move(*frame));
            }
        }
        m_impl->collections[static_cast<size_t>(i)]->addFrames(parsed);
    }

    const QJsonObject stacking_root = json.value(QStringLiteral("stackingSettings")).toObject();
    for (int i = 0; i < 4; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        const QJsonArray entries = stacking_root.value(toString(type)).toArray();
        auto& map = m_impl->stackingSettings[static_cast<size_t>(i)];
        for (const auto& value : entries)
        {
            const QJsonObject record = value.toObject();
            const auto group_key = GroupKey::fromJson(record.value(QStringLiteral("groupKey")));
            if (!group_key.has_value())
            {
                continue;
            }
            map[*group_key] = stackingSettingsFromJson(record.value(QStringLiteral("settings")).toObject());
        }
    }

    const QJsonArray stack_exclusion_entries = json.value(QStringLiteral("stackExcludedFrames")).toArray();
    for (const auto& value : stack_exclusion_entries)
    {
        const QJsonObject record = value.toObject();
        const auto group_key = GroupKey::fromJson(record.value(QStringLiteral("groupKey")));
        if (!group_key.has_value())
        {
            continue;
        }

        QStringList paths;
        const QJsonArray frames = record.value(QStringLiteral("frames")).toArray();
        paths.reserve(frames.size());
        for (const auto& frame_value : frames)
        {
            const QString path = frame_value.toString().trimmed();
            if (!path.isEmpty())
            {
                paths.push_back(path);
            }
        }

        const QStringList canonical_paths = canonicalUniquePaths(paths);
        if (canonical_paths.isEmpty())
        {
            continue;
        }

        auto& excluded_paths = m_impl->stackExcludedFrames[*group_key];
        for (const auto& canonical : canonical_paths)
        {
            excluded_paths.insert(canonical);
        }
    }

    const QJsonArray registration_settings = json.value(QStringLiteral("registrationSettings")).toArray();
    for (const auto& value : registration_settings)
    {
        const QJsonObject record = value.toObject();
        const auto group_key = GroupKey::fromJson(record.value(QStringLiteral("groupKey")));
        if (!group_key.has_value())
        {
            continue;
        }
        m_impl->registrationSettings[*group_key] =
            registrationSettingsFromJson(record.value(QStringLiteral("settings")).toObject());
    }

    const QJsonArray light_settings = json.value(QStringLiteral("lightCalibrationSettings")).toArray();
    for (const auto& value : light_settings)
    {
        const QJsonObject record = value.toObject();
        const auto group_key = GroupKey::fromJson(record.value(QStringLiteral("groupKey")));
        if (!group_key.has_value())
        {
            continue;
        }
        m_impl->lightCalibrationSettings[*group_key] =
            lightCalibrationSettingsFromJson(record.value(QStringLiteral("settings")).toObject());
    }

    const QJsonArray override_entries = json.value(QStringLiteral("calibrationOverrides")).toArray();
    for (const auto& value : override_entries)
    {
        const QJsonObject record = value.toObject();
        const auto light_key = lightGroupKeyFromJson(record.value(QStringLiteral("lightKey")).toObject());
        if (!light_key.has_value())
        {
            continue;
        }

        CalibrationOverride entry;
        entry.lightKey = *light_key;
        entry.type = static_cast<CalibrationOverrideType>(record.value(QStringLiteral("type")).toInt(0));
        entry.masterPath = record.value(QStringLiteral("masterPath")).toString();
        if (const auto key = GroupKey::fromJson(record.value(QStringLiteral("groupKey"))); key.has_value())
        {
            entry.groupKey = *key;
        }
        m_impl->calibrationOverrides.push_back(std::move(entry));
    }

    const QJsonArray masters = json.value(QStringLiteral("masterSources")).toArray();
    for (const auto& value : masters)
    {
        const QJsonObject record = value.toObject();
        try
        {
            const FrameId id = FrameId::fromJson(record.value(QStringLiteral("frameId")));
            const int raw_source = record.value(QStringLiteral("source")).toInt(0);
            const auto source = masterSourceFromInt(raw_source);
            m_impl->masterSources.insert(id, source);
        }
        catch (...)
        {
        }
    }

    for (int i = 0; i < 4; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        auto& collection = *m_impl->collections[static_cast<size_t>(i)];
        const auto& options = m_impl->groupingOptions[static_cast<size_t>(i)];
        std::vector<FrameId> masters_to_enforce;

        for (auto it = m_impl->masterSources.constBegin(); it != m_impl->masterSources.constEnd(); ++it)
        {
            if (!shouldApplyMasterSelectionPolicy(it.value()))
            {
                continue;
            }
            if (collection.findById(it.key()).has_value())
            {
                masters_to_enforce.push_back(it.key());
            }
        }

        for (const auto& id : masters_to_enforce)
        {
            const auto master_opt = collection.findById(id);
            if (!master_opt.has_value())
            {
                continue;
            }
            if (master_opt->excluded)
            {
                m_impl->masterSources.remove(id);
                continue;
            }

            const GroupKey group_key = groupKeyForFrame(type, *master_opt, options);
            (void)enforceExclusiveSelectionForMaster(collection, type, options, group_key, id);
        }

        // Recover masters that were lost before saving (e.g. due to the grouping
        // toggle bug fixed in redetectAutoMastersForType).  Only applies to
        // calibration types with single-frame groups matching the master heuristic.
        (void)redetectAutoMastersForType(m_impl->masterSources, collection, type, options);
    }

    m_impl->groupsDirty.fill(true);
    m_impl->calibrationDirty = true;
    m_impl->registrationDirty = true;
    m_impl->integrationDirty = true;
    m_impl->pendingReset = true;
    m_impl->invalidateValidation();

    endBatchUpdate();

    m_impl->hasUnsavedChanges = false;

    if (output_changed)
    {
        emit outputDirectoryChanged(m_impl->outputDirectory);
    }
}

void IntegrationDataModel::markSaved()
{
    m_impl->hasUnsavedChanges = false;
}

bool IntegrationDataModel::hasUnsavedChanges() const
{
    return m_impl->hasUnsavedChanges;
}

bool IntegrationDataModel::isGroupCacheDirty(FrameType type) const
{
    return m_impl->groupsDirty[static_cast<size_t>(indexFor(type))];
}

bool IntegrationDataModel::isValidationCacheDirty() const
{
    return m_impl->validationDirty;
}

bool IntegrationDataModel::isReadyForIntegration() const
{
    return !validate().hasErrors();
}

WorkflowValidationResult IntegrationDataModel::validate() const
{
    if (!m_impl->validationDirty && m_impl->cachedValidation.has_value())
    {
        return *m_impl->cachedValidation;
    }

    WorkflowValidationResult result;

    // IMPORTANT: This validation is designed for the UI and must not do heavy I/O.
    // Expensive checks (e.g. reading image headers to validate dimensions/channels)
    // should happen during execution-time manifest validation.

    // Output directory checks (lightweight filesystem queries only).
    if (m_impl->outputDirectory.trimmed().isEmpty())
    {
        result.issues.push_back(ValidationIssue::missingOutputDirectory());
    }
    else
    {
        const QFileInfo out_info(m_impl->outputDirectory);
        if (out_info.exists() && !out_info.isDir())
        {
            result.issues.push_back({ValidationIssue::Severity::Error, ValidationErrorCode::OutputPathNotDirectory,
                                     QStringLiteral("Output directory path is not a directory."), QString(),
                                     FixTarget::OutputDirectory});
        }
        else if (out_info.exists() && !out_info.isWritable())
        {
            result.issues.push_back({ValidationIssue::Severity::Error, ValidationErrorCode::OutputDirectoryNotWritable,
                                     QStringLiteral("Output directory is not writable."), QString(),
                                     FixTarget::OutputDirectory});
        }
        else if (!out_info.exists())
        {
            const QDir parent_dir = QFileInfo(m_impl->outputDirectory).dir();
            const QFileInfo parent_info(parent_dir.absolutePath());
            if (!parent_info.exists() || !parent_info.isDir() || !parent_info.isWritable())
            {
                result.issues.push_back(
                    {ValidationIssue::Severity::Error, ValidationErrorCode::OutputDirectoryCannotCreate,
                     QStringLiteral("Output directory cannot be created (parent directory is not writable)."),
                     QString(), FixTarget::OutputDirectory});
            }
        }
    }

    if (lightFrames().empty())
    {
        result.issues.push_back({ValidationIssue::Severity::Error, ValidationErrorCode::NoLightFrames,
                                 QStringLiteral("No light frames available."), QString(), FixTarget::LightFrames});
        // Don't return early - continue to report all issues so user sees full validation state.
    }

    // Skip calibration checks if no light frames (nothing to calibrate).
    if (lightFrames().empty())
    {
        m_impl->cachedValidation = result;
        m_impl->validationDirty = false;
        return result;
    }

    // Calibration warnings: use CalibrationMatcher to detect match quality.
    // This is best-effort and should not block execution.
    const bool has_bias = !biasFrames().empty();
    const bool has_dark = !darkFrames().empty();
    const bool has_flat = !flatFrames().empty();

    if (!has_bias && !has_dark && !has_flat)
    {
        m_impl->cachedValidation = result;
        m_impl->validationDirty = false;
        return result; // No calibration frames added, no warnings needed.
    }

    // Build calibration matcher with same logic as LightCalibrationPanel.
    CalibrationMatcher matcher(groupingOptions(FrameType::Bias), groupingOptions(FrameType::Dark),
                               groupingOptions(FrameType::Flat), m_impl->calibrationPolicy);
    matcher.setOverrides(m_impl->calibrationOverrides);

    // Collect calibration groups.
    {
        std::vector<BiasCalibrationGroup> bias_groups;
        for (const auto& group : biasGroups())
        {
            if (group.frames.empty())
                continue;
            BiasCalibrationGroup entry;
            entry.key = BiasGroupKey::fromFrame(group.frames.front());
            entry.groupKey = group.key;
            bias_groups.push_back(entry);
        }
        matcher.setBiasGroups(std::move(bias_groups));
    }
    {
        std::vector<DarkCalibrationGroup> dark_groups;
        for (const auto& group : darkGroups())
        {
            if (group.frames.empty())
                continue;
            DarkCalibrationGroup entry;
            entry.key = DarkGroupKey::fromFrame(group.frames.front());
            entry.groupKey = group.key;
            dark_groups.push_back(entry);
        }
        matcher.setDarkGroups(std::move(dark_groups));
    }
    {
        std::vector<FlatCalibrationGroup> flat_groups;
        for (const auto& group : flatGroups())
        {
            if (group.frames.empty())
                continue;
            FlatCalibrationGroup entry;
            entry.key = FlatGroupKey::fromFrame(group.frames.front());
            entry.groupKey = group.key;
            flat_groups.push_back(entry);
        }
        matcher.setFlatGroups(std::move(flat_groups));
    }

    // Check each light group for calibration match quality.
    // Validation is UI-oriented and should not block execution:
    // - LIGHTS: Flat + (Bias OR Dark)
    // - If only Flat exists: warn about missing Dark/Bias
    // - If only Bias/Dark exists: warn about missing Flat
    // - Fallback matches (ClosestDate/ClosestExposure) are warnings
    // - Pre-calibrated groups are skipped entirely
    constexpr int kMaxPerTypeWarnings = 5;
    int missing_dark_or_bias_reported = 0;
    int missing_flat_reported = 0;
    int fallback_reported = 0;

    for (const auto& group : lightGroups())
    {
        if (group.frames.empty())
            continue;

        // Skip validation for pre-calibrated groups
        auto calIt = m_impl->lightCalibrationSettings.find(group.key);
        if (calIt != m_impl->lightCalibrationSettings.end() && calIt->second.preCalibrated)
        {
            continue;
        }

        const LightGroupKey light_key = LightGroupKey::fromFrame(group.frames.front());
        // Store the parseable GroupKey format for lookup in CalibrationTab (uses Qt::UserRole).
        // The display string is formatted by the UI layer (SummaryTab) for presentation.
        const QString group_key_str = group.key.toString();

        CalibrationMatch bias_match;
        CalibrationMatch dark_match;
        CalibrationMatch flat_match;

        // Helper to check if a match is usable (not None and not ManuallyUnassigned)
        auto isUsableMatch = [](const CalibrationMatch& m) {
            return m.type != CalibrationMatch::MatchType::None &&
                   m.type != CalibrationMatch::MatchType::ManuallyUnassigned;
        };

        const bool bias_ok = has_bias ? isUsableMatch(bias_match = matcher.findBias(light_key)) : false;
        const bool dark_ok = has_dark ? isUsableMatch(dark_match = matcher.findDark(light_key)) : false;
        const bool flat_ok = has_flat ? isUsableMatch(flat_match = matcher.findFlat(light_key)) : false;

        const bool bias_or_dark_ok = bias_ok || dark_ok;

        // Requirements warnings (group-level, not per-file):
        // - If flats exist or match is expected, but neither bias nor dark is available/matching.
        // - If bias/dark exist, but no flat is available/matching.
        if (!bias_or_dark_ok)
        {
            // If user has *any* bias/dark frames, this means no match; if they have none, it means missing inputs.
            if (missing_dark_or_bias_reported++ < kMaxPerTypeWarnings)
            {
                result.issues.push_back(
                    ValidationIssue::missingCalibration(group_key_str, CalibrationFrameType::DarkOrBias));
            }
        }
        else
        {
            // CCD mode logic.
            if (m_impl->calibrationPolicy.subtractBiasFromDark)
            {
                // CCD Mode: Expect both Dark AND Bias
                if (dark_ok && !bias_ok)
                {
                    if (missing_dark_or_bias_reported++ < kMaxPerTypeWarnings)
                    {
                        result.issues.push_back({ValidationIssue::Severity::Warning,
                                                 ValidationErrorCode::MissingCalibrationMatch,
                                                 QStringLiteral("CCD mode enabled: Missing Bias for Dark subtraction"),
                                                 group_key_str, FixTarget::CalibrationTab});
                    }
                }
                if (bias_ok && !dark_ok)
                {
                    if (missing_dark_or_bias_reported++ < kMaxPerTypeWarnings)
                    {
                        result.issues.push_back({ValidationIssue::Severity::Warning,
                                                 ValidationErrorCode::MissingCalibrationMatch,
                                                 QStringLiteral("CCD mode enabled: Missing Dark for Dark subtraction"),
                                                 group_key_str, FixTarget::CalibrationTab});
                    }
                }
            }
            else
            {
                // Standard Mode: Bias is only used when no Dark match exists.
                // Do not warn when both are present; Dark takes precedence and Bias is ignored.
            }
        }

        if (!flat_ok && (has_flat || has_bias || has_dark))
        {
            // Only warn about missing flats when the user is doing (or attempting) calibration.
            if (missing_flat_reported++ < kMaxPerTypeWarnings)
            {
                result.issues.push_back(ValidationIssue::missingCalibration(group_key_str, CalibrationFrameType::Flat));
            }
        }

        // Fallback warnings (yellow cells):
        if (m_impl->calibrationPolicy.warnOnFallback && dark_ok &&
            dark_match.type == CalibrationMatch::MatchType::ClosestExposure)
        {
            if (fallback_reported++ < kMaxPerTypeWarnings)
            {
                result.issues.push_back(
                    ValidationIssue::fallbackCalibration(group_key_str, CalibrationFrameType::Dark));
            }
        }
        if (m_impl->calibrationPolicy.warnOnFallback && flat_ok &&
            flat_match.type == CalibrationMatch::MatchType::ClosestDate)
        {
            if (fallback_reported++ < kMaxPerTypeWarnings)
            {
                result.issues.push_back(
                    ValidationIssue::fallbackCalibration(group_key_str, CalibrationFrameType::Flat));
            }
        }

        // Manually unassigned calibration warnings:
        if (bias_match.type == CalibrationMatch::MatchType::ManuallyUnassigned)
        {
            result.issues.push_back(
                ValidationIssue::manuallyUnassignedCalibration(group_key_str, CalibrationFrameType::Bias));
        }
        if (dark_match.type == CalibrationMatch::MatchType::ManuallyUnassigned)
        {
            result.issues.push_back(
                ValidationIssue::manuallyUnassignedCalibration(group_key_str, CalibrationFrameType::Dark));
        }
        if (flat_match.type == CalibrationMatch::MatchType::ManuallyUnassigned)
        {
            result.issues.push_back(
                ValidationIssue::manuallyUnassignedCalibration(group_key_str, CalibrationFrameType::Flat));
        }
    }

    m_impl->cachedValidation = result;
    m_impl->validationDirty = false;
    return result;
}
