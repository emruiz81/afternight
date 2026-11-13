// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowManifest.h"

#include <QJsonArray>

#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"

#include <QCryptographicHash>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace ImageIntegration
{
QString astrometryApiKeyHash(QString apiKey)
{
    apiKey = apiKey.trimmed();
    if (apiKey.isEmpty())
    {
        return QString();
    }

    const QByteArray domainSeparated =
        QByteArrayLiteral("afternight:image-integration:astrometry-api-key:v1:") + apiKey.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(domainSeparated, QCryptographicHash::Sha256).toHex());
}
} // namespace ImageIntegration

namespace
{
QString resolutionToString(int width, int height)
{
    return QStringLiteral("%1x%2").arg(width).arg(height);
}

bool parseResolutionString(const QString& resolution, QSize& out)
{
    const QString trimmed = resolution.trimmed();
    const int x_pos = trimmed.indexOf(QLatin1Char('x'));
    if (x_pos <= 0)
    {
        return false;
    }

    bool ok_w = false;
    bool ok_h = false;
    const int w = trimmed.left(x_pos).toInt(&ok_w);
    const int h = trimmed.mid(x_pos + 1).toInt(&ok_h);
    if (!ok_w || !ok_h)
    {
        return false;
    }
    out = QSize(w, h);
    return true;
}

bool validateGroupDimensions(const std::vector<FrameId>& files, const QString& declared_resolution,
                             const QString& group_label, const ImageIntegration::FrameHeaderReader& header_reader,
                             WorkflowValidationResult& out)
{
    if (files.size() < 2 || !header_reader)
    {
        return true;
    }

    int expected_w = 0;
    int expected_h = 0;

    for (size_t i = 0; i < files.size(); ++i)
    {
        const auto& frame = files[i];
        if (!frame.isValid())
        {
            out.issues.push_back({ValidationIssue::Severity::Error,
                                  QStringLiteral("Invalid input frame ID in %1.").arg(group_label),
                                  FixTarget::LightFrames});
            return false;
        }

        const auto header = header_reader(frame.canonicalPath());
        if (!header.has_value())
        {
            out.issues.push_back(
                {ValidationIssue::Severity::Error,
                 QStringLiteral("Failed to read image metadata in %1: %2").arg(group_label, frame.canonicalPath()),
                 FixTarget::LightFrames});
            return false;
        }

        const int w = static_cast<int>(header->width);
        const int h = static_cast<int>(header->height);
        if (w <= 0 || h <= 0)
        {
            out.issues.push_back(
                {ValidationIssue::Severity::Error,
                 QStringLiteral("Invalid image dimensions in %1: %2").arg(group_label, frame.canonicalPath()),
                 FixTarget::LightFrames});
            return false;
        }

        if (i == 0)
        {
            expected_w = w;
            expected_h = h;

            if (!declared_resolution.trimmed().isEmpty())
            {
                QSize declared;
                if (parseResolutionString(declared_resolution, declared) &&
                    (declared.width() != expected_w || declared.height() != expected_h))
                {
                    out.issues.push_back(
                        {ValidationIssue::Severity::Error,
                         QStringLiteral("Group resolution (%1) does not match input files (%2) in %3")
                             .arg(declared_resolution, resolutionToString(expected_w, expected_h), group_label),
                         FixTarget::LightFrames});
                    return false;
                }
            }
            continue;
        }

        if (w != expected_w || h != expected_h)
        {
            out.issues.push_back({ValidationIssue::Severity::Error,
                                  QStringLiteral("Mismatched frame dimensions in %1: expected %2, got %3 (%4)")
                                      .arg(group_label, resolutionToString(expected_w, expected_h),
                                           resolutionToString(w, h), frame.canonicalPath()),
                                  FixTarget::LightFrames});
            return false;
        }
    }

    return true;
}

std::set<QString> canonicalFrameSet(const std::vector<FrameId>& frames)
{
    std::set<QString> result;
    for (const auto& frame : frames)
    {
        result.insert(frame.canonicalPath());
    }
    return result;
}

bool containsFrame(const std::set<QString>& frames, const FrameId& frame)
{
    return frame.isValid() && frames.find(frame.canonicalPath()) != frames.end();
}

int enabledCometObservationCount(const RegistrationSettings::CometTrackingSettings& settings)
{
    int count = 0;
    for (const auto& observation : settings.observations)
    {
        if (observation.enabled && !observation.frameId.trimmed().isEmpty() &&
            std::isfinite(observation.sourcePosition.x()) && std::isfinite(observation.sourcePosition.y()))
        {
            ++count;
        }
    }
    return count;
}

/// The word a manifest stores for the exclusion that left a light out.
QString leftOutLightKindToString(const WorkflowManifest::LeftOutLight::Kind kind)
{
    return kind == WorkflowManifest::LeftOutLight::Kind::StackExcluded ? QStringLiteral("stack_excluded")
                                                                       : QStringLiteral("excluded");
}

/// The exclusion a stored word names; any word but the stack's own reads as the lights' list's.
WorkflowManifest::LeftOutLight::Kind leftOutLightKindFromString(const QString& kind)
{
    return kind == QStringLiteral("stack_excluded") ? WorkflowManifest::LeftOutLight::Kind::StackExcluded
                                                    : WorkflowManifest::LeftOutLight::Kind::Excluded;
}

QJsonArray toJsonArray(const std::vector<FrameId>& ids)
{
    QJsonArray array;
    for (const auto& id : ids)
    {
        array.append(id.toJson());
    }
    return array;
}

std::vector<FrameId> fromJsonArray(const QJsonArray& array)
{
    std::vector<FrameId> ids;
    ids.reserve(array.size());
    for (const auto& value : array)
    {
        ids.push_back(FrameId::fromJson(value));
    }
    return ids;
}

QJsonArray toJsonArray(const std::vector<double>& values)
{
    QJsonArray array;
    for (const double value : values)
    {
        array.append(value);
    }
    return array;
}

std::vector<double> fromDoubleArray(const QJsonArray& array)
{
    std::vector<double> values;
    values.reserve(array.size());
    for (const auto& value : array)
    {
        values.push_back(value.toDouble());
    }
    return values;
}

QJsonArray toJsonArray(const std::vector<QString>& values)
{
    QJsonArray array;
    for (const QString& value : values)
    {
        array.append(value);
    }
    return array;
}

std::vector<QString> fromStringArray(const QJsonArray& array)
{
    std::vector<QString> values;
    values.reserve(array.size());
    for (const auto& value : array)
    {
        values.push_back(value.toString());
    }
    return values;
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
// 1. Keep this JSON in lockstep with IntegrationDataModel serialization.
// 2. Mirror processing-affecting fields in manifest hashing and diffing.
// 3. Extend defaults/persistence regression tests for new fields.
QJsonObject stackingToJson(const StackingSettings& settings)
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

StackingSettings stackingFromJson(const QJsonObject& obj)
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
    // A per-channel width weighting an earlier version stored ("perChannelFwhmWeighting") is not read: every channel is
    // weighed by its light's one star width, as every stack weighed it.
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
    if (obj.contains(QStringLiteral("localNormalization")))
    {
        settings.localNormalization =
            localNormalizationFromJson(obj.value(QStringLiteral("localNormalization")).toObject());
    }
    if (obj.contains(QStringLiteral("drizzle")))
    {
        settings.drizzle = drizzleSettingsFromJson(obj.value(QStringLiteral("drizzle")).toObject());
    }
    settings.cometStackingMode = obj.value(QStringLiteral("cometStackingMode")).toString(settings.cometStackingMode);
    settings.solveFinalMasterIfNeeded =
        obj.value(QStringLiteral("solveFinalMasterIfNeeded")).toBool(settings.solveFinalMasterIfNeeded);
    if (obj.value(QStringLiteral("registeredOutputPolicy")).toInt(static_cast<int>(RegisteredOutputPolicy::Retained)) ==
        static_cast<int>(RegisteredOutputPolicy::Discardable))
    {
        settings.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    }
    if (obj.contains(QStringLiteral("hdr")))
    {
        settings.hdr = hdrExposureMergeSettingsFromJson(obj.value(QStringLiteral("hdr")).toObject());
    }
    return settings;
}

QJsonObject registrationToJson(const RegistrationSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("version"), settings.version);
    obj.insert(QStringLiteral("minStarPairs"), settings.minStarPairs);
    obj.insert(QStringLiteral("maxStarPairs"), settings.maxStarPairs);
    obj.insert(QStringLiteral("autoReferenceDuringRegistration"), settings.autoReferenceDuringRegistration);
    obj.insert(QStringLiteral("autoSolveUnsolvedFrames"), settings.autoSolveUnsolvedFrames);
    obj.insert(QStringLiteral("enrichWithGaiaCatalog"), settings.enrichWithGaiaCatalog);
    obj.insert(QStringLiteral("referenceImage"), settings.referenceImage);
    obj.insert(QStringLiteral("registrationMethod"), settings.registrationMethod);
    obj.insert(QStringLiteral("transformation"), settings.transformation);
    obj.insert(QStringLiteral("transformMode"), settings.transformMode);
    obj.insert(QStringLiteral("transformOrder"), settings.transformOrder);
    obj.insert(QStringLiteral("twoPassRefinementEnabled"), settings.twoPassRefinementEnabled);
    obj.insert(QStringLiteral("alignmentMode"), settings.alignmentMode);
    obj.insert(QStringLiteral("interpolation"), settings.interpolation);
    obj.insert(QStringLiteral("manualControlPoints"),
               ImageIntegration::RegistrationHashUtils::manualControlPointsToJson(settings.manualControlPointsByFrame));
    if (settings.cometTracking.has_value())
    {
        obj.insert(QStringLiteral("cometTracking"),
                   ImageIntegration::RegistrationHashUtils::cometTrackingToJson(*settings.cometTracking));
    }
    obj.insert(QStringLiteral("chromaticAberration"),
               ImageIntegration::RegistrationHashUtils::chromaticAberrationToJson(settings.chromaticAberration));

    obj.insert(QStringLiteral("radius"), settings.radius);
    obj.insert(QStringLiteral("threshold"), settings.threshold);
    obj.insert(QStringLiteral("amplitudeRangeEnabled"), settings.amplitudeRangeEnabled);
    obj.insert(QStringLiteral("thresholdMinimum"), settings.thresholdMinimum);
    obj.insert(QStringLiteral("thresholdMaximum"), settings.thresholdMaximum);
    obj.insert(QStringLiteral("roundnessThreshold"), settings.roundnessThreshold);
    obj.insert(QStringLiteral("roundnessRangeEnabled"), settings.roundnessRangeEnabled);
    obj.insert(QStringLiteral("roundnessThresholdMaximum"), settings.roundnessThresholdMaximum);
    obj.insert(QStringLiteral("convergence"), settings.convergence);
    obj.insert(QStringLiteral("relaxPsfChecks"), settings.relaxPsfChecks);
    obj.insert(QStringLiteral("profileType"), settings.profileType);
    obj.insert(QStringLiteral("minimumBeta"), settings.minimumBeta);

    obj.insert(QStringLiteral("cfaPattern"), settings.cfaPattern);
    obj.insert(QStringLiteral("debayerMethod"), settings.debayerMethod);
    return obj;
}

RegistrationSettings registrationFromJson(const QJsonObject& obj)
{
    RegistrationSettings settings;

    settings.version = obj.value(QStringLiteral("version")).toInt(settings.version);

    settings.minStarPairs = obj.value(QStringLiteral("minStarPairs")).toInt(settings.minStarPairs);
    settings.maxStarPairs = obj.value(QStringLiteral("maxStarPairs")).toInt(settings.maxStarPairs);
    settings.autoReferenceDuringRegistration =
        obj.value(QStringLiteral("autoReferenceDuringRegistration")).toBool(settings.autoReferenceDuringRegistration);
    settings.autoSolveUnsolvedFrames =
        obj.value(QStringLiteral("autoSolveUnsolvedFrames")).toBool(settings.autoSolveUnsolvedFrames);
    settings.enrichWithGaiaCatalog =
        obj.value(QStringLiteral("enrichWithGaiaCatalog")).toBool(settings.enrichWithGaiaCatalog);
    settings.referenceImage = obj.value(QStringLiteral("referenceImage")).toString(settings.referenceImage);
    settings.registrationMethod = obj.value(QStringLiteral("registrationMethod")).toString(settings.registrationMethod);
    settings.transformation = obj.value(QStringLiteral("transformation")).toString(settings.transformation);
    settings.transformMode = obj.value(QStringLiteral("transformMode")).toString(settings.transformMode);
    settings.transformOrder = obj.value(QStringLiteral("transformOrder")).toInt(settings.transformOrder);
    settings.twoPassRefinementEnabled =
        obj.value(QStringLiteral("twoPassRefinementEnabled")).toBool(settings.twoPassRefinementEnabled);
    settings.alignmentMode = obj.value(QStringLiteral("alignmentMode")).toString(settings.alignmentMode);
    settings.interpolation = obj.value(QStringLiteral("interpolation")).toString(settings.interpolation);
    settings.manualControlPointsByFrame = ImageIntegration::RegistrationHashUtils::manualControlPointsFromJson(
        obj.value(QStringLiteral("manualControlPoints")));
    settings.cometTracking =
        ImageIntegration::RegistrationHashUtils::cometTrackingFromJson(obj.value(QStringLiteral("cometTracking")));
    {
        const QJsonObject ca_obj = obj.value(QStringLiteral("chromaticAberration")).toObject();
        settings.chromaticAberration.enabled =
            ca_obj.value(QStringLiteral("enabled")).toBool(settings.chromaticAberration.enabled);
        settings.chromaticAberration.edgeFraction =
            ca_obj.value(QStringLiteral("edgeFraction")).toDouble(settings.chromaticAberration.edgeFraction);
        settings.chromaticAberration.transformType =
            ca_obj.value(QStringLiteral("transformType")).toString(settings.chromaticAberration.transformType);
        settings.chromaticAberration.minMatchedStars =
            ca_obj.value(QStringLiteral("minMatchedStars")).toInt(settings.chromaticAberration.minMatchedStars);
        settings.chromaticAberration.interpolation =
            ca_obj.value(QStringLiteral("interpolation")).toString(settings.chromaticAberration.interpolation);
    }

    settings.radius = obj.value(QStringLiteral("radius")).toInt(settings.radius);
    settings.threshold = obj.value(QStringLiteral("threshold")).toDouble(settings.threshold);
    settings.amplitudeRangeEnabled =
        obj.value(QStringLiteral("amplitudeRangeEnabled")).toBool(settings.amplitudeRangeEnabled);
    settings.thresholdMinimum = obj.value(QStringLiteral("thresholdMinimum")).toDouble(settings.thresholdMinimum);
    settings.thresholdMaximum = obj.value(QStringLiteral("thresholdMaximum")).toDouble(settings.thresholdMaximum);
    settings.roundnessThreshold = obj.value(QStringLiteral("roundnessThreshold")).toDouble(settings.roundnessThreshold);
    settings.roundnessRangeEnabled =
        obj.value(QStringLiteral("roundnessRangeEnabled")).toBool(settings.roundnessRangeEnabled);
    settings.roundnessThresholdMaximum =
        obj.value(QStringLiteral("roundnessThresholdMaximum")).toDouble(settings.roundnessThresholdMaximum);
    settings.convergence = obj.value(QStringLiteral("convergence")).toInt(settings.convergence);
    settings.relaxPsfChecks = obj.value(QStringLiteral("relaxPsfChecks")).toBool(settings.relaxPsfChecks);
    settings.profileType = obj.value(QStringLiteral("profileType")).toString(settings.profileType);
    settings.minimumBeta = obj.value(QStringLiteral("minimumBeta")).toDouble(settings.minimumBeta);

    settings.cfaPattern = obj.value(QStringLiteral("cfaPattern")).toString(settings.cfaPattern);
    settings.debayerMethod = obj.value(QStringLiteral("debayerMethod")).toString(settings.debayerMethod);
    ImageIntegration::RegistrationHashUtils::reconcileTransformContract(settings);
    return settings;
}

QJsonObject plateSolveToJson(const WorkflowManifest::PlateSolveSettings& settings, const QString& apiKeyHash)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("defaultSolver"), settings.defaultSolver);
    obj.insert(QStringLiteral("astapPath"), settings.astapPath);
    obj.insert(QStringLiteral("astrometryLocalPath"), settings.astrometryLocalPath);
    const QString persisted_hash = apiKeyHash.trimmed().isEmpty()
                                       ? ImageIntegration::astrometryApiKeyHash(settings.astrometryApiKey)
                                       : apiKeyHash.trimmed();
    if (!persisted_hash.isEmpty())
    {
        obj.insert(QStringLiteral("astrometryApiKeyRedacted"), true);
        obj.insert(QStringLiteral("astrometryApiKeyHash"), persisted_hash);
    }
    obj.insert(QStringLiteral("indexPath"), settings.indexPath);
    obj.insert(QStringLiteral("gaiaCatalogDirectory"), settings.gaiaCatalogDirectory);
    obj.insert(QStringLiteral("gaiaCatalogPack"), settings.gaiaCatalogPack);
    obj.insert(QStringLiteral("searchRadius"), settings.searchRadius);
    obj.insert(QStringLiteral("downsample"), settings.downsample);
    obj.insert(QStringLiteral("timeoutSeconds"), settings.timeoutSeconds);
    obj.insert(QStringLiteral("useHints"), settings.useHints);
    if (settings.focalLengthMm.has_value())
        obj.insert(QStringLiteral("focalLengthMm"), *settings.focalLengthMm);
    if (settings.pixelSizeUm.has_value())
        obj.insert(QStringLiteral("pixelSizeUm"), *settings.pixelSizeUm);
    return obj;
}

WorkflowManifest::PlateSolveSettings plateSolveFromJson(const QJsonObject& obj, QString& apiKeyHash)
{
    WorkflowManifest::PlateSolveSettings settings;
    settings.defaultSolver = obj.value(QStringLiteral("defaultSolver")).toString(settings.defaultSolver);
    settings.astapPath = obj.value(QStringLiteral("astapPath")).toString(settings.astapPath);
    settings.astrometryLocalPath =
        obj.value(QStringLiteral("astrometryLocalPath")).toString(settings.astrometryLocalPath);
    // The key is stated only by its hash; a key stated in plain is never read.
    apiKeyHash = obj.value(QStringLiteral("astrometryApiKeyHash")).toString().trimmed();
    settings.indexPath = obj.value(QStringLiteral("indexPath")).toString(settings.indexPath);
    settings.gaiaCatalogDirectory =
        obj.value(QStringLiteral("gaiaCatalogDirectory")).toString(settings.gaiaCatalogDirectory);
    settings.gaiaCatalogPack = obj.value(QStringLiteral("gaiaCatalogPack")).toString(settings.gaiaCatalogPack);
    settings.searchRadius = obj.value(QStringLiteral("searchRadius")).toDouble(settings.searchRadius);
    settings.downsample = obj.value(QStringLiteral("downsample")).toInt(settings.downsample);
    settings.timeoutSeconds = obj.value(QStringLiteral("timeoutSeconds")).toInt(settings.timeoutSeconds);
    settings.useHints = obj.value(QStringLiteral("useHints")).toBool(settings.useHints);
    if (obj.contains(QStringLiteral("focalLengthMm")))
        settings.focalLengthMm = obj.value(QStringLiteral("focalLengthMm")).toDouble();
    if (obj.contains(QStringLiteral("pixelSizeUm")))
        settings.pixelSizeUm = obj.value(QStringLiteral("pixelSizeUm")).toDouble();
    return settings;
}

QJsonObject drizzleToJson(const WorkflowManifest::IntegrationGroup::DrizzleSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), settings.enabled);
    obj.insert(QStringLiteral("scale"), settings.scale);
    obj.insert(QStringLiteral("pixelFraction"), settings.pixelFraction);
    obj.insert(QStringLiteral("kernel"), settings.kernel);
    obj.insert(QStringLiteral("cfaAware"), settings.cfaAware);
    obj.insert(QStringLiteral("cfaPattern"), settings.cfaPattern);
    obj.insert(QStringLiteral("mapPersistencePolicy"),
               settings.mapPersistencePolicy ==
                       WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint
                   ? QStringLiteral("DenseCheckpoint")
                   : QStringLiteral("CompactDescriptorPreferred"));
    return obj;
}

WorkflowManifest::IntegrationGroup::DrizzleSettings drizzleFromJson(const QJsonObject& obj)
{
    WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
    settings.enabled = obj.value(QStringLiteral("enabled")).toBool(settings.enabled);
    settings.scale = obj.value(QStringLiteral("scale")).toDouble(settings.scale);
    settings.pixelFraction = obj.value(QStringLiteral("pixelFraction")).toDouble(settings.pixelFraction);
    settings.kernel = obj.value(QStringLiteral("kernel")).toString(settings.kernel);
    settings.cfaAware = obj.value(QStringLiteral("cfaAware")).toBool(settings.cfaAware);
    settings.cfaPattern = obj.value(QStringLiteral("cfaPattern")).toString(settings.cfaPattern);
    if (obj.value(QStringLiteral("mapPersistencePolicy")).toString() == QStringLiteral("DenseCheckpoint"))
    {
        settings.mapPersistencePolicy =
            WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint;
    }
    return settings;
}

bool isValidDrizzleKernel(const QString& kernel)
{
    const QString normalized = kernel.trimmed().toLower();
    return normalized == QStringLiteral("square") || normalized == QStringLiteral("point") ||
           normalized == QStringLiteral("turbo") || normalized == QStringLiteral("gaussian") ||
           normalized == QStringLiteral("lanczos2") || normalized == QStringLiteral("lanczos3") ||
           normalized == QStringLiteral("lanczos-2") || normalized == QStringLiteral("lanczos-3");
}

/// Raises an error for each stacking text no table knows, in the words the stacking stage refuses it with once the
/// run has started: the method and the rejection, and for a light group the weighting and the star-count scaling.
void validateStackingChoices(const StackingSettings& settings, const QString& group, const bool light,
                             const FixTarget target, WorkflowValidationResult& result)
{
    using ImageIntegration::PersistedChoice;
    struct Choice
    {
        const char* setting;
        PersistedChoice choice;
        const QString& text;
    };
    const Choice choices[] = {{"stacking method", PersistedChoice::StackMethod, settings.stackMethod},
                              {"rejection method", PersistedChoice::RejectionMethod, settings.rejectionMethod},
                              {"weighting", PersistedChoice::Weighting, settings.weighting},
                              {"star count scaling", PersistedChoice::StarCountScaling, settings.starCountScaling}};
    for (const Choice& choice : choices)
    {
        // A master is combined by its method and rejection alone.
        const bool read =
            light || choice.choice == PersistedChoice::StackMethod || choice.choice == PersistedChoice::RejectionMethod;
        if (read && !ImageIntegration::persistedChoiceFromText(choice.choice, choice.text).known)
        {
            result.issues.push_back({ValidationIssue::Severity::Error,
                                     QStringLiteral("Unknown %1 '%2' in group '%3'.")
                                         .arg(QString::fromLatin1(choice.setting), choice.text.trimmed(), group),
                                     target});
        }
    }
}

bool isValidCfaPattern(const QString& pattern)
{
    const QString normalized = pattern.trimmed().toLower();
    return normalized == QStringLiteral("rggb") || normalized == QStringLiteral("bggr") ||
           normalized == QStringLiteral("gbrg") || normalized == QStringLiteral("grbg") ||
           normalized == QStringLiteral("auto");
}

QJsonObject calibrationToJson(const WorkflowManifest::CalibrationSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("cosmeticCorrection"), settings.cosmeticCorrection);
    obj.insert(QStringLiteral("sigmaHot"), settings.sigmaHot);
    obj.insert(QStringLiteral("sigmaCold"), settings.sigmaCold);
    obj.insert(QStringLiteral("darkOptimization"), settings.darkOptimization);
    obj.insert(QStringLiteral("darkMultiplicationFactor"), settings.darkMultiplicationFactor);
    obj.insert(QStringLiteral("flatAutoLevel"), settings.flatAutoLevel);
    obj.insert(QStringLiteral("pedestal"), settings.pedestal);
    obj.insert(QStringLiteral("subtractBiasFromDark"), settings.subtractBiasFromDark);
    return obj;
}

WorkflowManifest::CalibrationSettings calibrationFromJson(const QJsonObject& obj)
{
    WorkflowManifest::CalibrationSettings settings;
    settings.cosmeticCorrection = obj.value(QStringLiteral("cosmeticCorrection")).toBool();
    settings.sigmaHot = obj.value(QStringLiteral("sigmaHot")).toDouble(settings.sigmaHot);
    settings.sigmaCold = obj.value(QStringLiteral("sigmaCold")).toDouble(settings.sigmaCold);
    settings.darkOptimization = obj.value(QStringLiteral("darkOptimization")).toBool();
    settings.darkMultiplicationFactor =
        obj.value(QStringLiteral("darkMultiplicationFactor")).toDouble(settings.darkMultiplicationFactor);
    settings.flatAutoLevel = obj.value(QStringLiteral("flatAutoLevel")).toBool(settings.flatAutoLevel);
    settings.pedestal = obj.value(QStringLiteral("pedestal")).toDouble(settings.pedestal);
    settings.subtractBiasFromDark = obj.value(QStringLiteral("subtractBiasFromDark")).toBool(false);
    return settings;
}
} // namespace

QJsonObject WorkflowManifest::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("outputDirectory"), outputDirectory);
    if (!executionId.isEmpty())
    {
        root.insert(QStringLiteral("executionId"), executionId);
    }
    root.insert(QStringLiteral("reprocessExisting"), reprocessExisting);
    root.insert(QStringLiteral("calibrateLightsInMemory"), calibrateLightsInMemory);
    root.insert(QStringLiteral("executionMode"), static_cast<int>(executionMode));
    root.insert(QStringLiteral("executionScope"), executionScope);
    root.insert(QStringLiteral("executionScopeKey"), executionScopeKey);
    root.insert(QStringLiteral("maxParallelCores"), maxParallelCores);
    root.insert(QStringLiteral("combineRgbImages"), combineRgbImages);
    if (!processLabel.trimmed().isEmpty())
    {
        root.insert(QStringLiteral("processLabel"), processLabel.trimmed());
    }
    if (!processingProfileId.trimmed().isEmpty() || !processingProfileLabel.trimmed().isEmpty())
    {
        QJsonObject profile;
        if (!processingProfileId.trimmed().isEmpty())
        {
            profile.insert(QStringLiteral("id"), processingProfileId.trimmed());
        }
        if (!processingProfileLabel.trimmed().isEmpty())
        {
            profile.insert(QStringLiteral("label"), processingProfileLabel.trimmed());
        }
        root.insert(QStringLiteral("processingProfile"), profile);
    }
    root.insert(QStringLiteral("plateSolve"), plateSolveToJson(plateSolve, astrometryApiKeyHash));

    if (!dirtyGroups.empty())
    {
        QJsonArray dirty;
        for (const auto& key : dirtyGroups)
        {
            dirty.append(key.toString());
        }
        root.insert(QStringLiteral("dirtyGroups"), dirty);
    }
    if (!incrementalBiasGroups.empty())
    {
        QJsonArray groups;
        for (const auto& key : incrementalBiasGroups)
        {
            groups.append(key.toString());
        }
        root.insert(QStringLiteral("incrementalBiasGroups"), groups);
    }
    if (!incrementalDarkGroups.empty())
    {
        QJsonArray groups;
        for (const auto& key : incrementalDarkGroups)
        {
            groups.append(key.toString());
        }
        root.insert(QStringLiteral("incrementalDarkGroups"), groups);
    }
    if (!incrementalFlatGroups.empty())
    {
        QJsonArray groups;
        for (const auto& key : incrementalFlatGroups)
        {
            groups.append(key.toString());
        }
        root.insert(QStringLiteral("incrementalFlatGroups"), groups);
    }
    if (!incrementalRegistrationGroups.empty())
    {
        QJsonArray groups;
        for (const auto& key : incrementalRegistrationGroups)
        {
            groups.append(key.toString());
        }
        root.insert(QStringLiteral("incrementalRegistrationGroups"), groups);
    }
    if (!incrementalIntegrationGroups.empty())
    {
        QJsonArray groups;
        for (const auto& key : incrementalIntegrationGroups)
        {
            groups.append(key.toString());
        }
        root.insert(QStringLiteral("incrementalIntegrationGroups"), groups);
    }

    // Add summary section
    QJsonObject summary;
    summary.insert(QStringLiteral("biasGroups"),
                   biasStage.has_value() ? static_cast<int>(biasStage->groups.size()) : 0);
    summary.insert(QStringLiteral("darkGroups"),
                   darkStage.has_value() ? static_cast<int>(darkStage->groups.size()) : 0);
    summary.insert(QStringLiteral("flatGroups"),
                   flatStage.has_value() ? static_cast<int>(flatStage->groups.size()) : 0);
    summary.insert(QStringLiteral("registrationGroups"), static_cast<int>(registrationGroups.size()));
    summary.insert(QStringLiteral("integrationGroups"), static_cast<int>(integrationGroups.size()));
    summary.insert(QStringLiteral("totalLightFrames"), static_cast<int>(lightsCalibration.size()));
    root.insert(QStringLiteral("summary"), summary);

    root.insert(QStringLiteral("stageSequence"), QJsonArray::fromStringList(stageSequence));

    auto writeCalibrationStage = [](const CalibrationStage& stage) {
        QJsonObject obj;
        obj.insert(QStringLiteral("frameType"), stage.frameType);
        QJsonArray groups;
        for (const auto& group : stage.groups)
        {
            QJsonObject groupObj;
            groupObj.insert(QStringLiteral("groupKey"), group.groupKey.toString());
            groupObj.insert(QStringLiteral("outputFilename"), group.outputFilename);
            groupObj.insert(QStringLiteral("inputFiles"), toJsonArray(group.inputFiles));
            groupObj.insert(QStringLiteral("stacking"), stackingToJson(group.stacking));
            groupObj.insert(QStringLiteral("skipIfExists"), group.skipIfExists);
            if (group.existingMaster.has_value())
            {
                groupObj.insert(QStringLiteral("existingMaster"), group.existingMaster->toJson());
            }
            if (group.biasMaster.has_value())
            {
                groupObj.insert(QStringLiteral("biasMaster"), group.biasMaster->toJson());
            }
            if (group.darkMaster.has_value())
            {
                groupObj.insert(QStringLiteral("darkMaster"), group.darkMaster->toJson());
            }
            if (group.statedNormalization != NormalizationMode::None)
            {
                groupObj.insert(QStringLiteral("statedNormalization"), static_cast<int>(group.statedNormalization));
            }
            groups.append(groupObj);
        }
        obj.insert(QStringLiteral("groups"), groups);
        return obj;
    };

    if (biasStage.has_value())
    {
        root.insert(QStringLiteral("biasStage"), writeCalibrationStage(*biasStage));
    }
    if (darkStage.has_value())
    {
        root.insert(QStringLiteral("darkStage"), writeCalibrationStage(*darkStage));
    }
    if (flatStage.has_value())
    {
        root.insert(QStringLiteral("flatStage"), writeCalibrationStage(*flatStage));
    }

    QJsonArray lights_calibration;
    for (const auto& params : lightsCalibration)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("lightPath"), params.lightPath.toJson());
        if (params.biasPath.has_value())
        {
            obj.insert(QStringLiteral("biasPath"), params.biasPath->toJson());
        }
        if (params.darkPath.has_value())
        {
            obj.insert(QStringLiteral("darkPath"), params.darkPath->toJson());
        }
        if (params.flatPath.has_value())
        {
            obj.insert(QStringLiteral("flatPath"), params.flatPath->toJson());
        }
        obj.insert(QStringLiteral("settings"), calibrationToJson(params.settings));
        lights_calibration.append(obj);
    }
    if (!lights_calibration.isEmpty())
    {
        root.insert(QStringLiteral("lightsCalibration"), lights_calibration);
    }

    QJsonArray pre_calibrated_groups;
    for (const auto& group : preCalibratedLightGroups)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("groupKey"), group.groupKey.toString());
        obj.insert(QStringLiteral("lightFiles"), toJsonArray(group.lightFiles));
        pre_calibrated_groups.append(obj);
    }
    if (!pre_calibrated_groups.isEmpty())
    {
        root.insert(QStringLiteral("preCalibratedLightGroups"), pre_calibrated_groups);
    }

    QJsonArray left_out_lights;
    for (const auto& left_out : leftOutLights)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("light"), left_out.light.toJson());
        obj.insert(QStringLiteral("kind"), leftOutLightKindToString(left_out.kind));
        obj.insert(QStringLiteral("groupKey"), left_out.groupKey.toString());
        left_out_lights.append(obj);
    }
    if (!left_out_lights.isEmpty())
    {
        root.insert(QStringLiteral("leftOutLights"), left_out_lights);
    }

    QJsonArray regGroups;
    for (const auto& reg : registrationGroups)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("groupKey"), reg.groupKey.toString());
        obj.insert(QStringLiteral("resolution"), reg.resolution);
        obj.insert(QStringLiteral("object"), reg.object);
        obj.insert(QStringLiteral("lightFiles"), toJsonArray(reg.lightFiles));
        obj.insert(QStringLiteral("settings"), registrationToJson(reg.settings));
        if (reg.referenceFrame.has_value())
        {
            obj.insert(QStringLiteral("referenceFrame"), reg.referenceFrame->toJson());
        }
        if (!reg.lightExposureSeconds.empty())
        {
            obj.insert(QStringLiteral("lightExposureSeconds"), toJsonArray(reg.lightExposureSeconds));
        }
        if (!reg.lightCaptureTimestampsUtc.empty())
        {
            obj.insert(QStringLiteral("lightCaptureTimestampsUtc"), toJsonArray(reg.lightCaptureTimestampsUtc));
        }
        regGroups.append(obj);
    }
    root.insert(QStringLiteral("registrationGroups"), regGroups);

    QJsonArray intGroups;
    for (const auto& group : integrationGroups)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("groupKey"), group.groupKey.toString());
        obj.insert(QStringLiteral("outputFilename"), group.outputFilename);
        obj.insert(QStringLiteral("resolution"), group.resolution);
        obj.insert(QStringLiteral("object"), group.object);
        obj.insert(QStringLiteral("filter"), group.filter);
        obj.insert(QStringLiteral("exposure"), group.exposure);
        obj.insert(QStringLiteral("lightFiles"), toJsonArray(group.lightFiles));
        if (!group.lightExposureSeconds.empty())
        {
            obj.insert(QStringLiteral("lightExposureSeconds"), toJsonArray(group.lightExposureSeconds));
        }
        obj.insert(QStringLiteral("stacking"), stackingToJson(group.stacking));
        obj.insert(QStringLiteral("normalizationReferenceFrames"), toJsonArray(group.normalizationReferenceFrames));
        if (group.normalizationCoefficientsArtifactPath.has_value())
        {
            obj.insert(QStringLiteral("normalizationCoefficientsArtifactPath"),
                       group.normalizationCoefficientsArtifactPath.value());
        }
        if (group.drizzle.has_value())
        {
            obj.insert(QStringLiteral("drizzle"), drizzleToJson(group.drizzle.value()));
        }
        if (group.drizzlePixmapArtifactDir.has_value())
        {
            obj.insert(QStringLiteral("drizzlePixmapArtifactDir"), group.drizzlePixmapArtifactDir.value());
        }
        if (group.drizzleAccumulatorArtifact.has_value())
        {
            obj.insert(QStringLiteral("drizzleAccumulatorArtifact"), group.drizzleAccumulatorArtifact.value());
        }
        obj.insert(QStringLiteral("registrationGroupKey"), group.registrationGroupKey.toString());
        obj.insert(QStringLiteral("skipIfExists"), group.skipIfExists);
        intGroups.append(obj);
    }
    root.insert(QStringLiteral("integrationGroups"), intGroups);

    return root;
}

WorkflowManifest WorkflowManifest::redactedForPersistence() const
{
    WorkflowManifest copy = *this;
    if (copy.astrometryApiKeyHash.trimmed().isEmpty())
    {
        copy.astrometryApiKeyHash = ImageIntegration::astrometryApiKeyHash(copy.plateSolve.astrometryApiKey);
    }
    copy.plateSolve.astrometryApiKey.clear();
    return copy;
}

WorkflowManifest WorkflowManifest::fromJson(const QJsonObject& json)
{
    WorkflowManifest manifest;
    // The format the manifest states, as it states it: zero when it states none. What reads a stored manifest reuses
    // it only at the format the writer writes (`isCurrentFormat`).
    manifest.version = json.value(QStringLiteral("version")).toInt(0);
    manifest.outputDirectory = json.value(QStringLiteral("outputDirectory")).toString();
    manifest.executionId = json.value(QStringLiteral("executionId")).toString();
    manifest.reprocessExisting = json.value(QStringLiteral("reprocessExisting")).toBool();
    manifest.calibrateLightsInMemory = json.value(QStringLiteral("calibrateLightsInMemory")).toBool(true);
    manifest.executionMode =
        persistedEnumerator(json.value(QStringLiteral("executionMode")).toInt(static_cast<int>(ExecutionMode::Full)),
                            kPersistedExecutionModes, ExecutionMode::Full);
    manifest.executionScope = json.value(QStringLiteral("executionScope")).toString(QStringLiteral("full"));
    manifest.executionScopeKey = json.value(QStringLiteral("executionScopeKey")).toString();
    manifest.maxParallelCores = json.value(QStringLiteral("maxParallelCores")).toInt(manifest.maxParallelCores);
    if (manifest.maxParallelCores < 0)
    {
        manifest.maxParallelCores = 0;
    }
    manifest.combineRgbImages = json.value(QStringLiteral("combineRgbImages")).toBool(false);
    manifest.processLabel = json.value(QStringLiteral("processLabel")).toString();
    const QJsonObject processingProfile = json.value(QStringLiteral("processingProfile")).toObject();
    manifest.processingProfileId = processingProfile.value(QStringLiteral("id")).toString();
    manifest.processingProfileLabel = processingProfile.value(QStringLiteral("label")).toString();
    if (json.contains(QStringLiteral("plateSolve")))
    {
        manifest.plateSolve =
            plateSolveFromJson(json.value(QStringLiteral("plateSolve")).toObject(), manifest.astrometryApiKeyHash);
    }
    if (manifest.executionScope.trimmed().isEmpty())
    {
        manifest.executionScope = QStringLiteral("full");
    }

    if (json.contains(QStringLiteral("dirtyGroups")))
    {
        const QJsonArray dirty = json.value(QStringLiteral("dirtyGroups")).toArray();
        for (const auto& value : dirty)
        {
            manifest.dirtyGroups.emplace_back(value.toString());
        }
    }
    if (json.contains(QStringLiteral("incrementalBiasGroups")))
    {
        const QJsonArray groups = json.value(QStringLiteral("incrementalBiasGroups")).toArray();
        for (const auto& value : groups)
        {
            manifest.incrementalBiasGroups.emplace_back(value.toString());
        }
    }
    if (json.contains(QStringLiteral("incrementalDarkGroups")))
    {
        const QJsonArray groups = json.value(QStringLiteral("incrementalDarkGroups")).toArray();
        for (const auto& value : groups)
        {
            manifest.incrementalDarkGroups.emplace_back(value.toString());
        }
    }
    if (json.contains(QStringLiteral("incrementalFlatGroups")))
    {
        const QJsonArray groups = json.value(QStringLiteral("incrementalFlatGroups")).toArray();
        for (const auto& value : groups)
        {
            manifest.incrementalFlatGroups.emplace_back(value.toString());
        }
    }
    if (json.contains(QStringLiteral("incrementalRegistrationGroups")))
    {
        const QJsonArray groups = json.value(QStringLiteral("incrementalRegistrationGroups")).toArray();
        for (const auto& value : groups)
        {
            manifest.incrementalRegistrationGroups.emplace_back(value.toString());
        }
    }
    if (json.contains(QStringLiteral("incrementalIntegrationGroups")))
    {
        const QJsonArray groups = json.value(QStringLiteral("incrementalIntegrationGroups")).toArray();
        for (const auto& value : groups)
        {
            manifest.incrementalIntegrationGroups.emplace_back(GroupKey{value.toString()});
        }
    }

    manifest.stageSequence = json.value(QStringLiteral("stageSequence")).toVariant().toStringList();

    auto readCalibrationStage = [](const QJsonObject& obj) {
        CalibrationStage stage;
        stage.frameType = obj.value(QStringLiteral("frameType")).toString();
        const QJsonArray groups = obj.value(QStringLiteral("groups")).toArray();
        for (const auto& value : groups)
        {
            const QJsonObject groupObj = value.toObject();
            CalibrationStage::Group group;
            group.groupKey = GroupKey{groupObj.value(QStringLiteral("groupKey")).toString()};
            group.outputFilename = groupObj.value(QStringLiteral("outputFilename")).toString();
            group.inputFiles = fromJsonArray(groupObj.value(QStringLiteral("inputFiles")).toArray());
            group.stacking = stackingFromJson(groupObj.value(QStringLiteral("stacking")).toObject());
            group.skipIfExists = groupObj.value(QStringLiteral("skipIfExists")).toBool();
            if (groupObj.contains(QStringLiteral("existingMaster")))
            {
                group.existingMaster = FrameId::fromJson(groupObj.value(QStringLiteral("existingMaster")));
            }
            if (groupObj.contains(QStringLiteral("biasMaster")))
            {
                group.biasMaster = FrameId::fromJson(groupObj.value(QStringLiteral("biasMaster")));
            }
            if (groupObj.contains(QStringLiteral("darkMaster")))
            {
                group.darkMaster = FrameId::fromJson(groupObj.value(QStringLiteral("darkMaster")));
            }
            switch (
                groupObj.value(QStringLiteral("statedNormalization")).toInt(static_cast<int>(NormalizationMode::None)))
            {
            case static_cast<int>(NormalizationMode::Global):
                group.statedNormalization = NormalizationMode::Global;
                break;
            case static_cast<int>(NormalizationMode::Local):
                group.statedNormalization = NormalizationMode::Local;
                break;
            default:
                group.statedNormalization = NormalizationMode::None;
                break;
            }
            stage.groups.push_back(std::move(group));
        }
        return stage;
    };

    if (json.contains(QStringLiteral("biasStage")))
    {
        manifest.biasStage = readCalibrationStage(json.value(QStringLiteral("biasStage")).toObject());
    }
    if (json.contains(QStringLiteral("darkStage")))
    {
        manifest.darkStage = readCalibrationStage(json.value(QStringLiteral("darkStage")).toObject());
    }
    if (json.contains(QStringLiteral("flatStage")))
    {
        manifest.flatStage = readCalibrationStage(json.value(QStringLiteral("flatStage")).toObject());
    }

    const QJsonArray lights_calibration = json.value(QStringLiteral("lightsCalibration")).toArray();
    for (const auto& value : lights_calibration)
    {
        const QJsonObject obj = value.toObject();
        LightsCalibrationParams params;
        params.lightPath = FrameId::fromJson(obj.value(QStringLiteral("lightPath")));
        if (obj.contains(QStringLiteral("biasPath")))
        {
            params.biasPath = FrameId::fromJson(obj.value(QStringLiteral("biasPath")));
        }
        if (obj.contains(QStringLiteral("darkPath")))
        {
            params.darkPath = FrameId::fromJson(obj.value(QStringLiteral("darkPath")));
        }
        if (obj.contains(QStringLiteral("flatPath")))
        {
            params.flatPath = FrameId::fromJson(obj.value(QStringLiteral("flatPath")));
        }
        params.settings = calibrationFromJson(obj.value(QStringLiteral("settings")).toObject());
        manifest.lightsCalibration.push_back(std::move(params));
    }

    for (const auto& value : json.value(QStringLiteral("preCalibratedLightGroups")).toArray())
    {
        const QJsonObject obj = value.toObject();
        PreCalibratedLightGroup group;
        group.groupKey = GroupKey{obj.value(QStringLiteral("groupKey")).toString()};
        group.lightFiles = fromJsonArray(obj.value(QStringLiteral("lightFiles")).toArray());
        manifest.preCalibratedLightGroups.push_back(std::move(group));
    }

    for (const auto& value : json.value(QStringLiteral("leftOutLights")).toArray())
    {
        const QJsonObject obj = value.toObject();
        LeftOutLight left_out;
        left_out.light = FrameId::fromJson(obj.value(QStringLiteral("light")));
        left_out.kind = leftOutLightKindFromString(obj.value(QStringLiteral("kind")).toString());
        left_out.groupKey = IntegrationGroupKey{GroupKey{obj.value(QStringLiteral("groupKey")).toString()}};
        manifest.leftOutLights.push_back(std::move(left_out));
    }

    const QJsonArray regGroups = json.value(QStringLiteral("registrationGroups")).toArray();
    for (const auto& value : regGroups)
    {
        const QJsonObject obj = value.toObject();
        RegistrationGroup group;
        group.groupKey = GroupKey{obj.value(QStringLiteral("groupKey")).toString()};
        group.resolution = obj.value(QStringLiteral("resolution")).toString();
        group.object = obj.value(QStringLiteral("object")).toString();
        group.lightFiles = fromJsonArray(obj.value(QStringLiteral("lightFiles")).toArray());
        group.settings = registrationFromJson(obj.value(QStringLiteral("settings")).toObject());
        if (obj.contains(QStringLiteral("referenceFrame")))
        {
            group.referenceFrame = FrameId::fromJson(obj.value(QStringLiteral("referenceFrame")));
        }
        if (obj.contains(QStringLiteral("lightExposureSeconds")))
        {
            group.lightExposureSeconds = fromDoubleArray(obj.value(QStringLiteral("lightExposureSeconds")).toArray());
        }
        if (obj.contains(QStringLiteral("lightCaptureTimestampsUtc")))
        {
            group.lightCaptureTimestampsUtc =
                fromStringArray(obj.value(QStringLiteral("lightCaptureTimestampsUtc")).toArray());
        }
        manifest.registrationGroups.push_back(std::move(group));
    }

    const QJsonArray intGroups = json.value(QStringLiteral("integrationGroups")).toArray();
    for (const auto& value : intGroups)
    {
        const QJsonObject obj = value.toObject();
        IntegrationGroup group;
        group.groupKey = IntegrationGroupKey{GroupKey{obj.value(QStringLiteral("groupKey")).toString()}};
        group.outputFilename = obj.value(QStringLiteral("outputFilename")).toString();
        group.resolution = obj.value(QStringLiteral("resolution")).toString();
        group.object = obj.value(QStringLiteral("object")).toString();
        group.filter = obj.value(QStringLiteral("filter")).toString();
        group.exposure = obj.value(QStringLiteral("exposure")).toString();
        group.lightFiles = fromJsonArray(obj.value(QStringLiteral("lightFiles")).toArray());
        if (obj.contains(QStringLiteral("lightExposureSeconds")))
        {
            group.lightExposureSeconds = fromDoubleArray(obj.value(QStringLiteral("lightExposureSeconds")).toArray());
        }
        group.stacking = stackingFromJson(obj.value(QStringLiteral("stacking")).toObject());
        group.normalizationReferenceFrames =
            fromJsonArray(obj.value(QStringLiteral("normalizationReferenceFrames")).toArray());
        if (obj.contains(QStringLiteral("normalizationCoefficientsArtifactPath")))
        {
            group.normalizationCoefficientsArtifactPath =
                obj.value(QStringLiteral("normalizationCoefficientsArtifactPath")).toString();
        }
        if (obj.contains(QStringLiteral("drizzle")))
        {
            group.drizzle = drizzleFromJson(obj.value(QStringLiteral("drizzle")).toObject());
        }
        if (obj.contains(QStringLiteral("drizzlePixmapArtifactDir")))
        {
            group.drizzlePixmapArtifactDir = obj.value(QStringLiteral("drizzlePixmapArtifactDir")).toString();
        }
        if (obj.contains(QStringLiteral("drizzleAccumulatorArtifact")))
        {
            group.drizzleAccumulatorArtifact = obj.value(QStringLiteral("drizzleAccumulatorArtifact")).toString();
        }
        group.registrationGroupKey = GroupKey{obj.value(QStringLiteral("registrationGroupKey")).toString()};
        group.skipIfExists = obj.value(QStringLiteral("skipIfExists")).toBool();
        manifest.integrationGroups.push_back(std::move(group));
    }

    return manifest;
}

WorkflowValidationResult WorkflowManifest::validate(const ImageIntegration::FrameHeaderReader& headerReader) const
{
    WorkflowValidationResult result;
    if (version <= 0)
    {
        result.issues.push_back({ValidationIssue::Severity::Error, QStringLiteral("Manifest version is invalid.")});
    }

    if (outputDirectory.trimmed().isEmpty())
    {
        result.issues.push_back({ValidationIssue::Severity::Error, QStringLiteral("Output directory is required.")});
    }
    else
    {
        const QFileInfo out_info(outputDirectory);
        if (out_info.exists() && !out_info.isDir())
        {
            result.issues.push_back({ValidationIssue::Severity::Error,
                                     QStringLiteral("Output directory path is not a directory."),
                                     FixTarget::OutputDirectory});
        }
        else if (out_info.exists() && !out_info.isWritable())
        {
            result.issues.push_back({ValidationIssue::Severity::Error,
                                     QStringLiteral("Output directory is not writable."), FixTarget::OutputDirectory});
        }
        else if (!out_info.exists())
        {
            const QDir parent_dir = QFileInfo(outputDirectory).dir();
            const QFileInfo parent_info(parent_dir.absolutePath());

            // If the directory doesn't exist yet, ensure the parent is writable so we can create it.
            if (!parent_info.exists() || !parent_info.isDir() || !parent_info.isWritable())
            {
                result.issues.push_back(
                    {ValidationIssue::Severity::Error,
                     QStringLiteral("Output directory cannot be created (parent directory is not writable)."),
                     FixTarget::OutputDirectory});
            }
        }
    }

    // Allow a registration-only manifest: registrationGroups present, integrationGroups empty.
    if (registrationGroups.empty())
    {
        result.issues.push_back(
            {ValidationIssue::Severity::Error, QStringLiteral("Registration groups are required.")});
    }

    const bool is_registration_only = integrationGroups.empty() && !registrationGroups.empty();
    if (integrationGroups.empty() && !is_registration_only)
    {
        result.issues.push_back({ValidationIssue::Severity::Error, QStringLiteral("Integration groups are required.")});
    }

    std::set<QString> registration_keys;
    for (const auto& registration : registrationGroups)
    {
        registration_keys.insert(registration.groupKey.toString());
    }

    const auto find_registration_group = [&](const GroupKey& key) -> const RegistrationGroup* {
        const auto it = std::find_if(
            registrationGroups.begin(), registrationGroups.end(),
            [&](const RegistrationGroup& registration) { return registration.groupKey.toString() == key.toString(); });
        return it == registrationGroups.end() ? nullptr : &*it;
    };

    if (!integrationGroups.empty())
    {
        for (const auto& group : integrationGroups)
        {
            if (group.stacking.registeredOutputPolicy != RegisteredOutputPolicy::Retained &&
                group.stacking.registeredOutputPolicy != RegisteredOutputPolicy::Discardable)
            {
                result.issues.push_back(
                    {ValidationIssue::Severity::Error, QStringLiteral("Registered-output policy is invalid.")});
            }

            if (group.outputFilename.trimmed().isEmpty())
            {
                result.issues.push_back({ValidationIssue::Severity::Error,
                                         QStringLiteral("Integration group output filename is required.")});
            }

            const QString registration_key = group.registrationGroupKey.toString();
            if (registration_key.trimmed().isEmpty() ||
                registration_keys.find(registration_key) == registration_keys.end())
            {
                result.issues.push_back({ValidationIssue::Severity::Error,
                                         QStringLiteral("Integration group registration reference is missing.")});
            }

            const RegistrationGroup* registration_group = find_registration_group(group.registrationGroupKey);
            const bool registration_uses_comet =
                registration_group != nullptr && ImageIntegration::RegistrationSettingsUtils::usesCometTracking(
                                                     registration_group->settings.registrationMethod);
            const bool uses_comet_stacking = ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode);

            if (uses_comet_stacking && !registration_uses_comet)
            {
                result.issues.push_back({ValidationIssue::Severity::Error,
                                         QStringLiteral("Comet stacking requires a comet tracking registration group."),
                                         FixTarget::IntegrationTab});
            }

            if (registration_uses_comet && !uses_comet_stacking)
            {
                result.issues.push_back({ValidationIssue::Severity::Error,
                                         QStringLiteral("Comet tracking registration requires Comet stacking."),
                                         FixTarget::IntegrationTab});
            }

            if (!group.normalizationReferenceFrames.empty())
            {
                std::set<QString> inputs;
                for (const auto& id : group.lightFiles)
                {
                    inputs.insert(id.canonicalPath());
                }

                std::set<QString> refs;
                for (const auto& id : group.normalizationReferenceFrames)
                {
                    refs.insert(id.canonicalPath());
                    if (inputs.find(id.canonicalPath()) == inputs.end())
                    {
                        result.issues.push_back(
                            {ValidationIssue::Severity::Error,
                             QStringLiteral(
                                 "Normalization reference frame is not part of the integration group inputs.")});
                        break;
                    }
                }

                if (refs.size() != group.normalizationReferenceFrames.size())
                {
                    result.issues.push_back({ValidationIssue::Severity::Error,
                                             QStringLiteral("Normalization reference frames contain duplicates.")});
                }

                if (!group.normalizationCoefficientsArtifactPath.has_value() ||
                    group.normalizationCoefficientsArtifactPath->trimmed().isEmpty())
                {
                    result.issues.push_back(
                        {ValidationIssue::Severity::Warning,
                         QStringLiteral("Normalization is enabled but coefficients artifact path is missing.")});
                }
            }

            validateStackingChoices(group.stacking, group.groupKey.toCompactString(), true, FixTarget::IntegrationTab,
                                    result);

            if (group.drizzle.has_value() && group.drizzle->enabled)
            {
                using PersistencePolicy = WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy;
                if (group.drizzle->scale < 1.0 || group.drizzle->scale > 4.0)
                {
                    result.issues.push_back({ValidationIssue::Severity::Error,
                                             QStringLiteral("Drizzle scale must be between 1.0 and 4.0.")});
                }
                if (group.drizzle->pixelFraction <= 0.0 || group.drizzle->pixelFraction > 1.0)
                {
                    result.issues.push_back({ValidationIssue::Severity::Error,
                                             QStringLiteral("Drizzle pixel fraction must be within (0.0, 1.0].")});
                }
                if (!isValidDrizzleKernel(group.drizzle->kernel))
                {
                    result.issues.push_back(
                        {ValidationIssue::Severity::Error, QStringLiteral("Drizzle kernel is invalid.")});
                }
                if (group.drizzle->cfaAware && !isValidCfaPattern(group.drizzle->cfaPattern))
                {
                    result.issues.push_back(
                        {ValidationIssue::Severity::Error, QStringLiteral("Drizzle CFA pattern is invalid.")});
                }
                if (group.drizzle->mapPersistencePolicy != PersistencePolicy::CompactDescriptorPreferred &&
                    group.drizzle->mapPersistencePolicy != PersistencePolicy::DenseCheckpoint)
                {
                    result.issues.push_back({ValidationIssue::Severity::Error,
                                             QStringLiteral("Drizzle map persistence policy is invalid.")});
                }
            }

            (void)validateGroupDimensions(
                group.lightFiles, group.resolution,
                QStringLiteral("integration group '%1'").arg(group.groupKey.toCompactString()), headerReader, result);
        }
    }

    for (const std::optional<CalibrationStage>* stage : {&biasStage, &darkStage, &flatStage})
    {
        if (stage->has_value())
        {
            for (const CalibrationStage::Group& calibration : (*stage)->groups)
            {
                validateStackingChoices(calibration.stacking, calibration.groupKey.toCompactString(), false,
                                        FixTarget::CalibrationTab, result);
            }
        }
    }

    for (const auto& registration : registrationGroups)
    {
        if (ImageIntegration::RegistrationSettingsUtils::usesCometTracking(registration.settings.registrationMethod))
        {
            const std::set<QString> registration_inputs = canonicalFrameSet(registration.lightFiles);

            if (!registration.settings.cometTracking.has_value())
            {
                result.issues.push_back(
                    {ValidationIssue::Severity::Error,
                     QStringLiteral("Comet tracking settings are required for comet tracking registration."),
                     FixTarget::RegistrationTab});
            }
            else
            {
                const auto& comet_settings = *registration.settings.cometTracking;
                if (enabledCometObservationCount(comet_settings) < 2)
                {
                    result.issues.push_back(
                        {ValidationIssue::Severity::Error,
                         QStringLiteral("Comet tracking requires at least two enabled observations."),
                         FixTarget::RegistrationTab});
                }

                for (const auto& observation : comet_settings.observations)
                {
                    if (!observation.enabled)
                    {
                        continue;
                    }
                    if (registration_inputs.find(FrameId(observation.frameId).canonicalPath()) ==
                        registration_inputs.end())
                    {
                        result.issues.push_back(
                            {ValidationIssue::Severity::Error,
                             QStringLiteral("Comet tracking observations must reference frames in the registration "
                                            "group."),
                             FixTarget::RegistrationTab});
                        break;
                    }
                }
            }

            if (!registration.referenceFrame.has_value() || !registration.referenceFrame->isValid())
            {
                result.issues.push_back({ValidationIssue::Severity::Error,
                                         QStringLiteral("Comet tracking requires an explicit reference frame."),
                                         FixTarget::RegistrationTab});
            }
            else if (!containsFrame(registration_inputs, *registration.referenceFrame))
            {
                result.issues.push_back(
                    {ValidationIssue::Severity::Error,
                     QStringLiteral("Comet tracking reference frame must be part of the registration group inputs."),
                     FixTarget::RegistrationTab});
            }
        }

        (void)validateGroupDimensions(
            registration.lightFiles, registration.resolution,
            QStringLiteral("registration group '%1'").arg(registration.groupKey.toCompactString()), headerReader,
            result);
    }

    std::set<QString> calibrated_light_identities;
    for (const auto& params : lightsCalibration)
    {
        if (!params.lightPath.isValid())
        {
            result.issues.push_back(
                {ValidationIssue::Severity::Error, QStringLiteral("Light calibration entry has invalid light path.")});
        }
        else if (!calibrated_light_identities.insert(params.lightPath.identityKey()).second)
        {
            result.issues.push_back({ValidationIssue::Severity::Error,
                                     QStringLiteral("Duplicate light calibration entry for frame '%1'.")
                                         .arg(params.lightPath.canonicalPath()),
                                     FixTarget::CalibrationTab});
        }
        if (params.biasPath.has_value() && !params.biasPath->isValid())
        {
            result.issues.push_back(
                {ValidationIssue::Severity::Error, QStringLiteral("Light calibration entry has invalid bias path.")});
        }
        if (params.darkPath.has_value() && !params.darkPath->isValid())
        {
            result.issues.push_back(
                {ValidationIssue::Severity::Error, QStringLiteral("Light calibration entry has invalid dark path.")});
        }
        if (params.flatPath.has_value() && !params.flatPath->isValid())
        {
            result.issues.push_back(
                {ValidationIssue::Severity::Error, QStringLiteral("Light calibration entry has invalid flat path.")});
        }
    }

    return result;
}

QString WorkflowManifest::summary() const
{
    return QStringLiteral("Manifest v%1: %2 integration group(s)")
        .arg(version)
        .arg(static_cast<int>(integrationGroups.size()));
}
