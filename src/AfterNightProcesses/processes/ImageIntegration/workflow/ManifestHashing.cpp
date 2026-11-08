// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ManifestHashing.h"

#include "ArtifactSchema.h"
#include "ArtifactTracker.h"
#include "ExecutionPreparationSnapshot.h"
#include "WorkflowRawLoadPolicy.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>

#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <set>

namespace
{

QByteArray hashData(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

QString canonicalOrRaw(const FrameId& id)
{
    const QString canon = id.canonicalPath();
    return canon.isEmpty() ? id.toString() : canon;
}

QJsonArray sortedFileList(const std::vector<FrameId>& files)
{
    std::vector<QString> paths;
    paths.reserve(files.size());
    for (const auto& f : files)
    {
        paths.push_back(canonicalOrRaw(f));
    }
    std::sort(paths.begin(), paths.end());
    QJsonArray arr;
    for (const auto& p : paths)
        arr.append(p);
    return arr;
}

void insertFileStamp(QJsonObject& obj, const QString& path)
{
    obj.insert(QStringLiteral("path"), path);

    if (path.isEmpty())
    {
        obj.insert(QStringLiteral("mtimeMs"), 0);
        obj.insert(QStringLiteral("sizeBytes"), 0);
        obj.insert(QStringLiteral("identityToken"), QString());
        return;
    }

    const QFileInfo info(path);
    obj.insert(QStringLiteral("mtimeMs"), info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0);
    obj.insert(QStringLiteral("sizeBytes"), info.exists() ? static_cast<qint64>(info.size()) : 0);
    obj.insert(QStringLiteral("identityToken"), info.isFile() ? ArtifactTracker::fileIdentityForPath(path) : QString());
}

QJsonObject fileStamp(const QString& path, const ExecutionPreparationSnapshot* snapshot = nullptr)
{
    if (snapshot != nullptr)
        return snapshot->fileStampJson(path);
    QJsonObject obj;
    insertFileStamp(obj, path);
    return obj;
}

QJsonObject fileStamp(const FrameId& frame, const ExecutionPreparationSnapshot* snapshot = nullptr)
{
    return fileStamp(canonicalOrRaw(frame), snapshot);
}

QJsonObject fileStamp(const std::optional<FrameId>& frame, const ExecutionPreparationSnapshot* snapshot = nullptr)
{
    return frame.has_value() ? fileStamp(frame.value(), snapshot) : fileStamp(QString(), snapshot);
}

QJsonArray sortedFileStampList(const std::vector<FrameId>& files, const ExecutionPreparationSnapshot* snapshot)
{
    std::vector<QJsonObject> entries;
    entries.reserve(files.size());
    for (const auto& file : files)
    {
        entries.push_back(fileStamp(file, snapshot));
    }

    std::sort(entries.begin(), entries.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value(QStringLiteral("path")).toString() < b.value(QStringLiteral("path")).toString();
    });

    QJsonArray arr;
    for (const auto& entry : entries)
    {
        arr.append(entry);
    }
    return arr;
}

} // anonymous namespace

void addLocalNormalizationSettings(QJsonObject& obj, const LocalNormalizationSettings& s)
{
    obj.insert(QStringLiteral("enabled"), s.enabled);
    obj.insert(QStringLiteral("gridSize"), s.gridSize);
    obj.insert(QStringLiteral("gridInterpolation"), s.gridInterpolation);
    obj.insert(QStringLiteral("localScaleEnabled"), s.localScaleEnabled);
    obj.insert(QStringLiteral("referenceStrategy"), s.referenceStrategy);
    obj.insert(QStringLiteral("maxIntegratedFrames"), s.maxIntegratedFrames);
    obj.insert(QStringLiteral("evaluationCriteria"), s.evaluationCriteria);
    obj.insert(QStringLiteral("backgroundEstimator"), s.backgroundEstimator);
    obj.insert(QStringLiteral("backgroundPercentile"), s.backgroundPercentile);
    obj.insert(QStringLiteral("sigmaClipK"), s.sigmaClipK);
    obj.insert(QStringLiteral("sigmaClipIterations"), s.sigmaClipIterations);
    obj.insert(QStringLiteral("backgroundSmoothingRadius"), s.backgroundSmoothingRadius);
    obj.insert(QStringLiteral("scaleEvaluation"), s.scaleEvaluation);
    obj.insert(QStringLiteral("scaleMode"), s.scaleMode);
    obj.insert(QStringLiteral("psfType"), s.psfType);
    obj.insert(QStringLiteral("moffatBeta"), s.moffatBeta);
    obj.insert(QStringLiteral("maxStars"), s.maxStars);
    obj.insert(QStringLiteral("minDetectionSnr"), s.minDetectionSnr);
    obj.insert(QStringLiteral("allowClusteredSources"), s.allowClusteredSources);
    obj.insert(QStringLiteral("lowClip"), s.lowClip);
    obj.insert(QStringLiteral("highClip"), s.highClip);
    obj.insert(QStringLiteral("hotPixelMedianRadius"), s.hotPixelMedianRadius);
    obj.insert(QStringLiteral("noiseReductionRadius"), s.noiseReductionRadius);
    obj.insert(QStringLiteral("localScaleSmoothingRadius"), s.localScaleSmoothingRadius);
}

void addOverlapNormalizationSettings(QJsonObject& obj, const OverlapNormalizationSettings& s)
{
    obj.insert(QStringLiteral("enabled"), s.enabled);
    obj.insert(QStringLiteral("minOverlapPixels"), s.minOverlapPixels);
    obj.insert(QStringLiteral("minOverlapFraction"), s.minOverlapFraction);
    obj.insert(QStringLiteral("graphPolicy"),
               QString::fromLatin1(an::overlapGraphPolicyName(ImageIntegration::kWorkflowOverlapGraphPolicy)));
}

namespace
{

void addLocalWeightingSettings(QJsonObject& obj, const LocalWeightingSettings& s)
{
    obj.insert(QStringLiteral("enabled"), s.enabled);
    obj.insert(QStringLiteral("windowSize"), s.windowSize);
    obj.insert(QStringLiteral("gridSpacing"), s.gridSpacing);
    obj.insert(QStringLiteral("snrWeightExponent"), s.snrWeightExponent);
}

/// A drizzle kernel by its key, so the dialog's "Lanczos3" and a session read's "lanczos3" are one kernel.
QString drizzleKernelFingerprint(const QString& kernel)
{
    return ImageIntegration::RegistrationHashUtils::fingerprintChoice(ImageIntegration::PersistedChoice::DrizzleKernel,
                                                                      kernel);
}

void addDrizzleSettings(QJsonObject& obj, const DrizzleSettings& s)
{
    obj.insert(QStringLiteral("enabled"), s.enabled);
    obj.insert(QStringLiteral("scale"), s.scale);
    obj.insert(QStringLiteral("pixelFraction"), s.pixelFraction);
    obj.insert(QStringLiteral("kernel"), drizzleKernelFingerprint(s.kernel));
    obj.insert(QStringLiteral("cfaAware"), s.cfaAware);
    obj.insert(QStringLiteral("cfaPattern"), s.cfaPattern);
}

void addHdrExposureMergeSettings(QJsonObject& obj, const HdrExposureMergeSettings& s)
{
    obj.insert(QStringLiteral("enabled"), s.enabled);
}

// StackingSettings expansion checklist:
// Keep this hash payload in sync with processing-affecting model/manifest settings, manifest diffing,
// and the regression tests that pin the defaults and persistence contract.
void addStackingSettings(QJsonObject& obj, const StackingSettings& s)
{
    obj.insert(QStringLiteral("stackMethod"), s.stackMethod);
    obj.insert(QStringLiteral("rejectionMethod"), s.rejectionMethod);
    obj.insert(QStringLiteral("sigmaLow"), s.sigmaLow);
    obj.insert(QStringLiteral("sigmaHigh"), s.sigmaHigh);
    obj.insert(QStringLiteral("weighting"), s.weighting);
    obj.insert(QStringLiteral("createRejectionMaps"), s.createRejectionMaps);
    obj.insert(QStringLiteral("mergeRejectionMaps"), s.mergeRejectionMaps);
    obj.insert(QStringLiteral("createQualityMaps"), s.createQualityMaps);
    obj.insert(QStringLiteral("spatialRejectionClustering"), s.spatialRejectionClustering);
    obj.insert(QStringLiteral("restackExcludingFlaggedFrames"), s.restackExcludingFlaggedFrames);
    obj.insert(QStringLiteral("starCountScaling"), s.starCountScaling);
    obj.insert(QStringLiteral("exponentialAlpha"), s.exponentialAlpha);
    obj.insert(QStringLiteral("minWeightFloor"), s.minWeightFloor);
    // The settings no longer hold a per-channel width weighting; its key stays in the payload, off as every stack ran
    // it, so the settings hash to the identity every master - a calibration master as well as a light master - was
    // stacked under and none is stacked again for it.
    obj.insert(QStringLiteral("perChannelFwhmWeighting"), false);
    QJsonObject local_weighting;
    addLocalWeightingSettings(local_weighting, s.localWeighting);
    obj.insert(QStringLiteral("localWeighting"), local_weighting);

    obj.insert(QStringLiteral("limitMemory"), s.limitMemory);
    obj.insert(QStringLiteral("maxMemoryMb"), s.maxMemoryMb);

    obj.insert(QStringLiteral("normalizationMode"), static_cast<int>(s.normalizationMode));
    obj.insert(QStringLiteral("globalNormalizationMethod"), static_cast<int>(s.globalNormalizationMethod));
    obj.insert(QStringLiteral("scaleEstimator"), static_cast<int>(s.scaleEstimator));
    QJsonObject overlap;
    addOverlapNormalizationSettings(overlap, s.overlapNormalization);
    obj.insert(QStringLiteral("overlapNormalization"), overlap);
    obj.insert(QStringLiteral("normalizationReferenceMode"), static_cast<int>(s.normalizationReferenceMode));
    obj.insert(QStringLiteral("manualNormalizationReference"), s.manualNormalizationReference);

    QJsonObject local;
    addLocalNormalizationSettings(local, s.localNormalization);
    obj.insert(QStringLiteral("localNormalization"), local);

    QJsonObject drizzle;
    addDrizzleSettings(drizzle, s.drizzle);
    obj.insert(QStringLiteral("drizzle"), drizzle);

    obj.insert(QStringLiteral("cometStackingMode"), s.cometStackingMode);
    obj.insert(QStringLiteral("solveFinalMasterIfNeeded"), s.solveFinalMasterIfNeeded);
    obj.insert(QStringLiteral("registeredOutputPolicy"), static_cast<int>(s.registeredOutputPolicy));

    QJsonObject hdr;
    addHdrExposureMergeSettings(hdr, s.hdr);
    obj.insert(QStringLiteral("hdr"), hdr);
}

void addRegistrationSettings(QJsonObject& obj, const RegistrationSettings& s)
{
    using ImageIntegration::RegistrationHashUtils::chromaticAberrationToJson;
    using ImageIntegration::RegistrationHashUtils::cometTrackingToJson;
    using ImageIntegration::RegistrationHashUtils::manualControlPointsToJson;
    using ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod;
    using ImageIntegration::RegistrationSettingsUtils::usesCometTracking;
    using ImageIntegration::RegistrationSettingsUtils::usesManualControlPoints;
    using ImageIntegration::RegistrationSettingsUtils::usesStarDetection;
    using ImageIntegration::RegistrationSettingsUtils::usesTransformContract;

    const QString method = normalizeRegistrationMethod(s.registrationMethod);

    obj.insert(QStringLiteral("version"), s.version);
    obj.insert(QStringLiteral("autoReferenceDuringRegistration"), s.autoReferenceDuringRegistration);
    obj.insert(QStringLiteral("autoSolveUnsolvedFrames"), s.autoSolveUnsolvedFrames);
    obj.insert(QStringLiteral("enrichWithGaiaCatalog"), s.enrichWithGaiaCatalog);
    obj.insert(QStringLiteral("referenceImage"), s.referenceImage);
    obj.insert(QStringLiteral("registrationMethod"), method);
    obj.insert(QStringLiteral("alignmentMode"), s.alignmentMode);
    obj.insert(QStringLiteral("interpolation"), s.interpolation);
    obj.insert(QStringLiteral("cfaPattern"), s.cfaPattern);
    obj.insert(QStringLiteral("debayerMethod"), s.debayerMethod);

    if (usesStarDetection(method))
    {
        obj.insert(QStringLiteral("minStarPairs"), s.minStarPairs);
        obj.insert(QStringLiteral("maxStarPairs"), s.maxStarPairs);
        obj.insert(QStringLiteral("radius"), s.radius);
        obj.insert(QStringLiteral("threshold"), s.threshold);
        obj.insert(QStringLiteral("amplitudeRangeEnabled"), s.amplitudeRangeEnabled);
        obj.insert(QStringLiteral("thresholdMinimum"), s.thresholdMinimum);
        obj.insert(QStringLiteral("thresholdMaximum"), s.thresholdMaximum);
        obj.insert(QStringLiteral("roundnessThreshold"), s.roundnessThreshold);
        obj.insert(QStringLiteral("roundnessRangeEnabled"), s.roundnessRangeEnabled);
        obj.insert(QStringLiteral("roundnessThresholdMaximum"), s.roundnessThresholdMaximum);
        obj.insert(QStringLiteral("convergence"), s.convergence);
        obj.insert(QStringLiteral("relaxPsfChecks"), s.relaxPsfChecks);
        obj.insert(QStringLiteral("profileType"), s.profileType);
        obj.insert(QStringLiteral("minimumBeta"), s.minimumBeta);
    }

    if (usesCometTracking(method) && s.cometTracking.has_value())
    {
        obj.insert(QStringLiteral("cometTracking"), cometTrackingToJson(*s.cometTracking));
    }

    obj.insert(QStringLiteral("chromaticAberrationEnabled"), s.chromaticAberration.enabled);
    if (s.chromaticAberration.enabled)
    {
        obj.insert(QStringLiteral("chromaticAberration"), chromaticAberrationToJson(s.chromaticAberration));
    }

    if (usesTransformContract(method))
    {
        obj.insert(QStringLiteral("transformation"), s.transformation);
        obj.insert(QStringLiteral("transformMode"), s.transformMode);
        obj.insert(QStringLiteral("transformOrder"), s.transformOrder);
        if (usesManualControlPoints(method))
        {
            obj.insert(QStringLiteral("manualControlPoints"), manualControlPointsToJson(s.manualControlPointsByFrame));
        }
    }
}

void addCalibrationSettings(QJsonObject& obj, const WorkflowManifest::CalibrationSettings& s)
{
    obj.insert(QStringLiteral("cosmeticCorrection"), s.cosmeticCorrection);
    obj.insert(QStringLiteral("sigmaHot"), s.sigmaHot);
    obj.insert(QStringLiteral("sigmaCold"), s.sigmaCold);
    obj.insert(QStringLiteral("darkOptimization"), s.darkOptimization);
    obj.insert(QStringLiteral("darkMultiplicationFactor"), s.darkMultiplicationFactor);
    obj.insert(QStringLiteral("flatAutoLevel"), s.flatAutoLevel);
    obj.insert(QStringLiteral("pedestal"), s.pedestal);
    obj.insert(QStringLiteral("subtractBiasFromDark"), s.subtractBiasFromDark);
}

void addWorkflowRawLoadPolicy(QJsonObject& obj)
{
    obj.insert(QStringLiteral("workflowRawLoadPolicy"), ImageIntegration::workflowRawLoadPolicyHashObject());
}

QString jsonHash(const QJsonObject& obj)
{
    QJsonDocument doc(obj);
    return hashData(doc.toJson(QJsonDocument::Compact));
}

QString jsonHash(const QJsonArray& arr)
{
    QJsonDocument doc(arr);
    return hashData(doc.toJson(QJsonDocument::Compact));
}

QJsonValue canonicalizeJson(const QJsonValue& value)
{
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end());

        QJsonObject ordered;
        for (const auto& key : keys)
        {
            ordered.insert(key, canonicalizeJson(object.value(key)));
        }
        return ordered;
    }

    if (value.isArray())
    {
        QJsonArray ordered;
        for (const auto& item : value.toArray())
        {
            ordered.append(canonicalizeJson(item));
        }
        return ordered;
    }

    return value;
}

QString canonicalJsonHash(const QJsonObject& object)
{
    return jsonHash(canonicalizeJson(object).toObject());
}

} // namespace

QString computeFrameFileStampFingerprint(const std::vector<FrameId>& frames,
                                         const ExecutionPreparationSnapshot* snapshot)
{
    QJsonArray inputs;
    for (const auto& frame : frames)
    {
        inputs.append(fileStamp(frame, snapshot));
    }
    return jsonHash(inputs);
}

std::vector<ExecutionPreparationSnapshot::FileStamp>
captureFrameFileStamps(const std::vector<FrameId>& frames, const ExecutionPreparationSnapshot* snapshot)
{
    std::vector<ExecutionPreparationSnapshot::FileStamp> stamps;
    stamps.reserve(frames.size());
    for (const auto& frame : frames)
    {
        if (snapshot != nullptr)
        {
            if (const auto* captured = snapshot->fileStamp(frame.canonicalPath()))
            {
                stamps.push_back(*captured);
                continue;
            }
        }
        ExecutionPreparationSnapshot::FileStamp stamp;
        stamp.canonicalPath = frame.canonicalPath();
        if (const auto revision = ArtifactTracker::captureFileRevision(stamp.canonicalPath))
        {
            stamp.canonicalPath = revision->path;
            stamp.exists = true;
            stamp.isFile = true;
            stamp.sizeBytes = revision->sizeBytes;
            stamp.modifiedAtMs = revision->modifiedAtMs;
            stamp.identityToken = revision->identityToken;
        }
        stamps.push_back(std::move(stamp));
    }
    return stamps;
}

QString computeFrameFileStampFingerprint(const std::vector<ExecutionPreparationSnapshot::FileStamp>& stamps)
{
    QJsonArray inputs;
    for (const auto& stamp : stamps)
    {
        QJsonObject object;
        object.insert(QStringLiteral("path"), stamp.canonicalPath);
        object.insert(QStringLiteral("mtimeMs"), stamp.exists ? stamp.modifiedAtMs : 0);
        object.insert(QStringLiteral("sizeBytes"), stamp.exists ? stamp.sizeBytes : 0);
        object.insert(QStringLiteral("identityToken"), stamp.isFile ? stamp.identityToken : QString());
        inputs.append(object);
    }
    return jsonHash(inputs);
}

bool frameFileStampsMatchCurrent(const std::vector<ExecutionPreparationSnapshot::FileStamp>& expected)
{
    for (const auto& stamp : expected)
    {
        const auto current = ArtifactTracker::captureFileRevision(stamp.canonicalPath);
        if (!stamp.exists)
        {
            if (current.has_value())
            {
                return false;
            }
            continue;
        }
        if (!current.has_value() || !stamp.isFile || current->sizeBytes != stamp.sizeBytes ||
            current->modifiedAtMs != stamp.modifiedAtMs || current->identityToken != stamp.identityToken)
        {
            return false;
        }
    }
    return true;
}

QString computeNormalizationSettingsFingerprint(const WorkflowManifest::IntegrationGroup& group,
                                                const std::vector<FrameId>& referenceFrames,
                                                const QString& cometProductsFingerprint)
{
    const bool use_local =
        group.stacking.normalizationMode == NormalizationMode::Local && group.stacking.localNormalization.enabled;
    const bool use_global = group.stacking.normalizationMode == NormalizationMode::Global;

    QJsonObject settings;
    ImageIntegration::schema::insertArtifactFamilyIdentity(
        settings, ImageIntegration::schema::ArtifactFamily::NormalizationCoefficients);

    QJsonArray references;
    for (const auto& frame : referenceFrames)
    {
        references.append(frame.toJson());
    }
    settings.insert(QStringLiteral("referenceFrames"), references);
    settings.insert(QStringLiteral("normalizationMode"), static_cast<int>(group.stacking.normalizationMode));
    // Local normalization matches each light globally in Additive With Scaling whatever method the group stores, so a
    // Local group's coefficients hold no stored method.
    if (!use_local)
    {
        settings.insert(QStringLiteral("globalNormalizationMethod"),
                        static_cast<int>(group.stacking.globalNormalizationMethod));
    }
    settings.insert(QStringLiteral("scaleEstimator"), static_cast<int>(group.stacking.scaleEstimator));
    settings.insert(QStringLiteral("normalizationReferenceMode"),
                    static_cast<int>(group.stacking.normalizationReferenceMode));
    settings.insert(QStringLiteral("manualNormalizationReference"), group.stacking.manualNormalizationReference);
    if (!cometProductsFingerprint.isEmpty())
    {
        settings.insert(QStringLiteral("cometProductsFingerprint"), cometProductsFingerprint);
    }
    if (use_global)
    {
        QJsonObject overlap;
        addOverlapNormalizationSettings(overlap, group.stacking.overlapNormalization);
        settings.insert(QStringLiteral("overlapNormalization"), overlap);
    }
    if (use_local)
    {
        QJsonObject local;
        addLocalNormalizationSettings(local, group.stacking.localNormalization);
        settings.insert(QStringLiteral("localNormalization"), local);
        // The request the settings resolve to, the constants no dialog offers included: a change to any of them
        // changes the grids.
        settings.insert(QStringLiteral("localNormalizationRequest"),
                        ImageIntegration::localNormalizationRequestJson(
                            ImageIntegration::localNormalizationRequestFor(group.stacking.localNormalization)));
        // What counts as a valid sample of a local grid is not a user setting, but it does decide the coefficients,
        // so the grids' own family identity is part of the fingerprint: a stage that changes that rule moves the
        // family's revision and nothing built under the old rule is reused.
        QJsonObject gridFamily;
        ImageIntegration::schema::insertArtifactFamilyIdentity(
            gridFamily, ImageIntegration::schema::ArtifactFamily::LocalNormalizationGrid);
        settings.insert(QStringLiteral("localNormalizationGrid"), gridFamily);
    }
    return canonicalJsonHash(settings);
}

QString computeStackingSettingsHash(const StackingSettings& settings)
{
    QJsonObject obj;
    addStackingSettings(obj, settings);
    return jsonHash(obj);
}

QString computeRegistrationSettingsHash(const RegistrationSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 2);
    ImageIntegration::schema::insertArtifactFamilyIdentity(obj, ImageIntegration::schema::ArtifactFamily::Registration);
    addRegistrationSettings(obj, settings);
    addWorkflowRawLoadPolicy(obj);
    return jsonHash(obj);
}

QString computeCalibrationSettingsHash(const WorkflowManifest::CalibrationSettings& settings)
{
    QJsonObject obj;
    addCalibrationSettings(obj, settings);
    return jsonHash(obj);
}

QString computeLightsCalibrationHash(const std::vector<WorkflowManifest::LightsCalibrationParams>& params,
                                     const ExecutionPreparationSnapshot* snapshot)
{
    std::vector<QJsonObject> entries;
    entries.reserve(params.size());

    for (const auto& p : params)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("schemaVersion"), 3);
        ImageIntegration::schema::insertArtifactFamilyIdentity(
            obj, ImageIntegration::schema::ArtifactFamily::CalibratedLight);
        obj.insert(QStringLiteral("light"), fileStamp(p.lightPath, snapshot));
        obj.insert(QStringLiteral("bias"), fileStamp(p.biasPath, snapshot));
        obj.insert(QStringLiteral("dark"), fileStamp(p.darkPath, snapshot));
        obj.insert(QStringLiteral("flat"), fileStamp(p.flatPath, snapshot));
        addWorkflowRawLoadPolicy(obj);

        QJsonObject settings;
        addCalibrationSettings(settings, p.settings);
        obj.insert(QStringLiteral("settings"), settings);
        entries.push_back(std::move(obj));
    }

    std::sort(entries.begin(), entries.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value(QStringLiteral("light")).toObject().value(QStringLiteral("path")).toString() <
               b.value(QStringLiteral("light")).toObject().value(QStringLiteral("path")).toString();
    });

    QJsonArray arr;
    for (const auto& e : entries)
        arr.append(e);
    return jsonHash(arr);
}

QString computeCalibratedLightArtifactHash(const WorkflowManifest::LightsCalibrationParams& params,
                                           const ExecutionPreparationSnapshot* snapshot)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 2);
    ImageIntegration::schema::insertArtifactFamilyIdentity(obj,
                                                           ImageIntegration::schema::ArtifactFamily::CalibratedLight);

    obj.insert(QStringLiteral("light"), fileStamp(params.lightPath, snapshot));
    obj.insert(QStringLiteral("bias"), fileStamp(params.biasPath, snapshot));
    obj.insert(QStringLiteral("dark"), fileStamp(params.darkPath, snapshot));
    obj.insert(QStringLiteral("flat"), fileStamp(params.flatPath, snapshot));
    addWorkflowRawLoadPolicy(obj);

    QJsonObject settings;
    addCalibrationSettings(settings, params.settings);
    obj.insert(QStringLiteral("settings"), settings);

    return jsonHash(obj);
}

QString
computeCalibratedLightArtifactHash(const WorkflowManifest::LightsCalibrationParams& params,
                                   const std::vector<ExecutionPreparationSnapshot::FileStamp>& orderedInputStamps)
{
    if (orderedInputStamps.size() != 4)
    {
        return QString();
    }
    const auto stamp_json = [](const ExecutionPreparationSnapshot::FileStamp& stamp) {
        QJsonObject object;
        object.insert(QStringLiteral("path"), stamp.canonicalPath);
        object.insert(QStringLiteral("mtimeMs"), stamp.exists ? stamp.modifiedAtMs : 0);
        object.insert(QStringLiteral("sizeBytes"), stamp.exists ? stamp.sizeBytes : 0);
        object.insert(QStringLiteral("identityToken"), stamp.isFile ? stamp.identityToken : QString());
        return object;
    };

    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 2);
    ImageIntegration::schema::insertArtifactFamilyIdentity(obj,
                                                           ImageIntegration::schema::ArtifactFamily::CalibratedLight);
    obj.insert(QStringLiteral("light"), stamp_json(orderedInputStamps[0]));
    obj.insert(QStringLiteral("bias"), stamp_json(orderedInputStamps[1]));
    obj.insert(QStringLiteral("dark"), stamp_json(orderedInputStamps[2]));
    obj.insert(QStringLiteral("flat"), stamp_json(orderedInputStamps[3]));
    addWorkflowRawLoadPolicy(obj);
    QJsonObject settings;
    addCalibrationSettings(settings, params.settings);
    obj.insert(QStringLiteral("settings"), settings);
    return jsonHash(obj);
}

QString calibratedLightOutputPathFor(const QString& outputDirectory, const FrameId& lightFrame)
{
    const QString filename = ImageIntegration::makeCalibratedOutputFilename(lightFrame.canonicalPath());
    return QDir(outputDirectory).filePath(QStringLiteral("calibrated_lights/%1").arg(filename));
}

QString chromaticAberrationLightOutputPathFor(const QString& outputDirectory, const FrameId& lightFrame)
{
    const QString filename =
        ImageIntegration::makeFitsIntermediateOutputFilename(lightFrame.canonicalPath(), QStringLiteral("_ca"));
    return QDir(outputDirectory).filePath(QStringLiteral("ca_corrected_lights/%1").arg(filename));
}

QString registrationInputPathForFrame(const QString& outputDirectory, bool calibrateLightsInMemory,
                                      const WorkflowManifest::RegistrationGroup* registrationGroup,
                                      const FrameId& lightFrame, const ExecutionPreparationSnapshot* snapshot)
{
    if (registrationGroup != nullptr && registrationGroup->settings.chromaticAberration.enabled)
    {
        const QString chromatic_aberration = chromaticAberrationLightOutputPathFor(outputDirectory, lightFrame);
        const auto* stamp = snapshot != nullptr ? snapshot->fileStamp(chromatic_aberration) : nullptr;
        if (snapshot != nullptr ? stamp != nullptr && stamp->exists : QFileInfo::exists(chromatic_aberration))
        {
            return chromatic_aberration;
        }
    }

    if (calibrateLightsInMemory)
    {
        return lightFrame.canonicalPath();
    }

    const QString calibrated = calibratedLightOutputPathFor(outputDirectory, lightFrame);
    const auto* stamp = snapshot != nullptr ? snapshot->fileStamp(calibrated) : nullptr;
    const bool calibrated_exists =
        snapshot != nullptr ? stamp != nullptr && stamp->exists : QFileInfo::exists(calibrated);
    return calibrated_exists ? calibrated : lightFrame.canonicalPath();
}

bool registrationGroupMaterializationUsesFits(const WorkflowManifest& manifest,
                                              const WorkflowManifest::RegistrationGroup& registrationGroup,
                                              const ExecutionPreparationSnapshot* snapshot)
{
    return std::any_of(
        registrationGroup.lightFiles.cbegin(), registrationGroup.lightFiles.cend(), [&](const FrameId& frame) {
            const QString path = registrationInputPathForFrame(
                manifest.outputDirectory, manifest.calibrateLightsInMemory, &registrationGroup, frame, snapshot);
            return !ImageIntegration::isFitsExtension(QFileInfo(path).suffix());
        });
}

QString
registrationGroupMaterializationContractFingerprint(const WorkflowManifest& manifest,
                                                    const WorkflowManifest::RegistrationGroup& registrationGroup,
                                                    const ExecutionPreparationSnapshot* snapshot)
{
    QJsonObject contract;
    contract.insert(QStringLiteral("schemaVersion"), 1);
    ImageIntegration::schema::insertArtifactFamilyIdentity(contract,
                                                           ImageIntegration::schema::ArtifactFamily::CalibratedLight);
    contract.insert(QStringLiteral("registrationGroupKey"), registrationGroup.groupKey.toString());
    contract.insert(QStringLiteral("format"),
                    registrationGroupMaterializationUsesFits(manifest, registrationGroup, snapshot)
                        ? QStringLiteral("fits-float32")
                        : QStringLiteral("original"));
    QJsonArray inputs;
    for (const auto& frame : registrationGroup.lightFiles)
    {
        inputs.append(registrationInputPathForFrame(manifest.outputDirectory, manifest.calibrateLightsInMemory,
                                                    &registrationGroup, frame, snapshot));
    }
    contract.insert(QStringLiteral("orderedRegistrationInputs"), inputs);
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(contract).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
}

QString computeLightsCalibrationHashForAssignments(
    const std::vector<const WorkflowManifest::LightsCalibrationParams*>& assignments,
    const ExecutionPreparationSnapshot* snapshot)
{
    if (assignments.empty())
    {
        return {};
    }

    std::vector<QJsonObject> entries;
    entries.reserve(assignments.size());
    QHash<QString, QJsonObject> file_stamp_cache;
    const auto cached_file_stamp = [&file_stamp_cache, snapshot](const QString& path) {
        auto it = file_stamp_cache.constFind(path);
        if (it != file_stamp_cache.constEnd())
        {
            return it.value();
        }

        QJsonObject stamp = fileStamp(path, snapshot);
        file_stamp_cache.insert(path, stamp);
        return stamp;
    };
    const auto cached_frame_stamp = [&](const FrameId& frame) { return cached_file_stamp(canonicalOrRaw(frame)); };
    const auto cached_optional_frame_stamp = [&](const std::optional<FrameId>& frame) {
        return frame.has_value() ? cached_frame_stamp(frame.value()) : cached_file_stamp(QString());
    };

    for (const auto* assignment : assignments)
    {
        if (assignment == nullptr)
        {
            continue;
        }

        QJsonObject obj;
        obj.insert(QStringLiteral("schemaVersion"), 3);
        ImageIntegration::schema::insertArtifactFamilyIdentity(
            obj, ImageIntegration::schema::ArtifactFamily::CalibratedLight);
        obj.insert(QStringLiteral("light"), cached_frame_stamp(assignment->lightPath));
        obj.insert(QStringLiteral("bias"), cached_optional_frame_stamp(assignment->biasPath));
        obj.insert(QStringLiteral("dark"), cached_optional_frame_stamp(assignment->darkPath));
        obj.insert(QStringLiteral("flat"), cached_optional_frame_stamp(assignment->flatPath));
        addWorkflowRawLoadPolicy(obj);

        QJsonObject settings;
        addCalibrationSettings(settings, assignment->settings);
        obj.insert(QStringLiteral("settings"), settings);
        entries.push_back(std::move(obj));
    }

    if (entries.empty())
    {
        return {};
    }

    std::sort(entries.begin(), entries.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value(QStringLiteral("light")).toObject().value(QStringLiteral("path")).toString() <
               b.value(QStringLiteral("light")).toObject().value(QStringLiteral("path")).toString();
    });

    QJsonArray arr;
    for (const auto& entry : entries)
    {
        arr.append(entry);
    }
    return jsonHash(arr);
}

QString computeLightsCalibrationHashForSubset(const std::vector<WorkflowManifest::LightsCalibrationParams>& allParams,
                                              const std::vector<FrameId>& lightFiles,
                                              const ExecutionPreparationSnapshot* snapshot)
{
    if (snapshot != nullptr)
    {
        const auto* facts = snapshot->calibrationSubsetFacts(lightFiles);
        if (facts != nullptr)
            return facts->subsetHash;
    }

    // Use FrameId identity keys for membership while retaining each assignment's
    // canonical spelling in the serialized fingerprint.
    std::set<QString> lightPathSet;
    for (const auto& frame : lightFiles)
    {
        lightPathSet.insert(frame.identityKey());
    }

    std::vector<const WorkflowManifest::LightsCalibrationParams*> matching_assignments;
    matching_assignments.reserve(lightFiles.size());

    for (const auto& p : allParams)
    {
        if (lightPathSet.find(p.lightPath.identityKey()) == lightPathSet.end())
        {
            continue;
        }

        matching_assignments.push_back(&p);
    }

    return computeLightsCalibrationHashForAssignments(matching_assignments, snapshot);
}

QString computeCalibrationGroupHash(const WorkflowManifest& manifest,
                                    const WorkflowManifest::CalibrationStage::Group& group,
                                    const ExecutionPreparationSnapshot* snapshot)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 3);
    ImageIntegration::schema::insertArtifactFamilyIdentity(obj,
                                                           ImageIntegration::schema::ArtifactFamily::CalibrationMaster);
    obj.insert(QStringLiteral("groupKey"), group.groupKey.toString());
    obj.insert(QStringLiteral("outputFilename"), group.outputFilename);
    obj.insert(QStringLiteral("inputFiles"), sortedFileStampList(group.inputFiles, snapshot));
    obj.insert(QStringLiteral("existingMaster"), fileStamp(group.existingMaster, snapshot));
    obj.insert(QStringLiteral("biasMaster"), fileStamp(group.biasMaster, snapshot));
    obj.insert(QStringLiteral("darkMaster"), fileStamp(group.darkMaster, snapshot));
    addWorkflowRawLoadPolicy(obj);

    if (!manifest.outputDirectory.trimmed().isEmpty())
    {
        obj.insert(QStringLiteral("outputDirectory"), QDir(manifest.outputDirectory).absolutePath());
    }
    else
    {
        obj.insert(QStringLiteral("outputDirectory"), QString());
    }

    QJsonObject stacking;
    addStackingSettings(stacking, group.stacking);
    obj.insert(QStringLiteral("stacking"), stacking);

    return jsonHash(obj);
}

QString computeIntegrationGroupHash(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group,
                                    const ExecutionPreparationSnapshot* snapshot)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 3);
    ImageIntegration::schema::insertArtifactFamilyIdentity(obj, ImageIntegration::schema::ArtifactFamily::Stack);
    obj.insert(QStringLiteral("groupKey"), group.groupKey.toString());
    obj.insert(QStringLiteral("outputFilename"), group.outputFilename);
    obj.insert(QStringLiteral("registrationGroupKey"), group.registrationGroupKey.toString());
    obj.insert(QStringLiteral("lightFiles"), sortedFileList(group.lightFiles));
    obj.insert(QStringLiteral("combineRgbImages"), manifest.combineRgbImages);

    // Include registration settings of the dependent registration group (if present)
    for (const auto& reg : manifest.registrationGroups)
    {
        if (reg.groupKey == group.registrationGroupKey)
        {
            QJsonObject regObj;
            addRegistrationSettings(regObj, reg.settings);
            obj.insert(QStringLiteral("registrationSettings"), regObj);
            obj.insert(QStringLiteral("registrationReferenceFrame"),
                       reg.referenceFrame.has_value() ? canonicalOrRaw(reg.referenceFrame.value()) : QString());
            break;
        }
    }

    QJsonObject stacking;
    addStackingSettings(stacking, group.stacking);
    obj.insert(QStringLiteral("stacking"), stacking);

    // Normalization inputs
    obj.insert(QStringLiteral("normalizationReferenceFrames"), sortedFileList(group.normalizationReferenceFrames));
    obj.insert(QStringLiteral("normalizationCoefficientsArtifactPath"),
               group.normalizationCoefficientsArtifactPath.has_value()
                   ? group.normalizationCoefficientsArtifactPath.value()
                   : QString());

    // Drizzle settings
    if (group.drizzle.has_value())
    {
        QJsonObject drizzle;
        drizzle.insert(QStringLiteral("enabled"), group.drizzle->enabled);
        if (group.drizzle->enabled)
        {
            // The level a drizzled master's samples are stated at, and the solution it carries: a master stacked under
            // another is stacked again.
            drizzle.insert(QStringLiteral("levelRevision"), ImageIntegration::schema::kDrizzledMasterLevelRevision);
            drizzle.insert(QStringLiteral("solutionRevision"),
                           ImageIntegration::schema::kDrizzledMasterSolutionRevision);
        }
        drizzle.insert(QStringLiteral("scale"), group.drizzle->scale);
        drizzle.insert(QStringLiteral("pixelFraction"), group.drizzle->pixelFraction);
        drizzle.insert(QStringLiteral("kernel"), drizzleKernelFingerprint(group.drizzle->kernel));
        drizzle.insert(QStringLiteral("cfaAware"), group.drizzle->cfaAware);
        drizzle.insert(QStringLiteral("cfaPattern"), group.drizzle->cfaPattern);
        drizzle.insert(QStringLiteral("mapPersistencePolicy"),
                       group.drizzle->mapPersistencePolicy ==
                               WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint
                           ? QStringLiteral("DenseCheckpoint")
                           : QStringLiteral("CompactDescriptorPreferred"));
        obj.insert(QStringLiteral("drizzle"), drizzle);
    }
    else
    {
        obj.insert(QStringLiteral("drizzle"), QJsonValue());
    }

    obj.insert(QStringLiteral("drizzlePixmapArtifactDir"),
               group.drizzlePixmapArtifactDir.has_value() ? group.drizzlePixmapArtifactDir.value() : QString());
    obj.insert(QStringLiteral("drizzleAccumulatorArtifact"),
               group.drizzleAccumulatorArtifact.has_value() ? group.drizzleAccumulatorArtifact.value() : QString());
    addWorkflowRawLoadPolicy(obj);

    if (!manifest.outputDirectory.trimmed().isEmpty())
    {
        obj.insert(QStringLiteral("outputDirectory"), QDir(manifest.outputDirectory).absolutePath());
    }
    else
    {
        obj.insert(QStringLiteral("outputDirectory"), QString());
    }

    return jsonHash(obj);
}

QString computeStackedLightArtifactHash(const WorkflowManifest& manifest,
                                        const WorkflowManifest::IntegrationGroup& group,
                                        const QString& inputsFingerprint, const QString& transformsFingerprint,
                                        const QString& sourceDistortionFingerprint,
                                        const QString& cometProductsFingerprint,
                                        const ExecutionPreparationSnapshot* snapshot)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("integrationGroupHash"), computeIntegrationGroupHash(manifest, group, snapshot));
    obj.insert(QStringLiteral("lightsCalibrationHash"),
               computeLightsCalibrationHashForSubset(manifest.lightsCalibration, group.lightFiles, snapshot));
    obj.insert(QStringLiteral("calibrateLightsInMemory"), manifest.calibrateLightsInMemory);
    ImageIntegration::schema::insertArtifactFamilyIdentity(obj, ImageIntegration::schema::ArtifactFamily::Stack);
    obj.insert(QStringLiteral("normalizationArtifactSchemaVersion"),
               ImageIntegration::schema::kNormalizationArtifactSchemaVersion);
    obj.insert(QStringLiteral("inputsFingerprint"), inputsFingerprint);
    obj.insert(QStringLiteral("transformsFingerprint"), transformsFingerprint);
    obj.insert(QStringLiteral("sourceDistortionFingerprint"), sourceDistortionFingerprint);
    obj.insert(QStringLiteral("cometProductsFingerprint"), cometProductsFingerprint);
    return jsonHash(obj);
}
