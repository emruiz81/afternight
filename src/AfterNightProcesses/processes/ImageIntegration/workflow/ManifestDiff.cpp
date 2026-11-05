// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ManifestDiff.h"

#include "ManifestHashing.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"

#include <QJsonArray>

#include <algorithm>
#include <map>
#include <set>

namespace
{
using ChangeType = ManifestDiff::CalibrationGroupChange::ChangeType;

QString changeTypeToString(ChangeType type)
{
    switch (type)
    {
    case ChangeType::Added:
        return QStringLiteral("added");
    case ChangeType::Removed:
        return QStringLiteral("removed");
    case ChangeType::Modified:
        return QStringLiteral("modified");
    case ChangeType::Unchanged:
        return QStringLiteral("unchanged");
    }

    return QStringLiteral("unknown");
}

/// Whether two kernel texts name one kernel: the dialog spells it "Lanczos3", a session read once gave "lanczos3".
bool sameDrizzleKernel(const QString& lhs, const QString& rhs)
{
    using ImageIntegration::PersistedChoice;
    using ImageIntegration::RegistrationHashUtils::fingerprintChoice;
    return fingerprintChoice(PersistedChoice::DrizzleKernel, lhs) ==
           fingerprintChoice(PersistedChoice::DrizzleKernel, rhs);
}

// StackingSettings expansion checklist:
// Every new processing-affecting setting must be compared here and mirrored in model/manifest serialization,
// manifest hashing, and defaults/persistence regression tests.
bool stackingEqual(const StackingSettings& lhs, const StackingSettings& rhs)
{
    return lhs.stackMethod == rhs.stackMethod && lhs.rejectionMethod == rhs.rejectionMethod &&
           lhs.sigmaLow == rhs.sigmaLow && lhs.sigmaHigh == rhs.sigmaHigh && lhs.weighting == rhs.weighting &&
           lhs.createRejectionMaps == rhs.createRejectionMaps && lhs.mergeRejectionMaps == rhs.mergeRejectionMaps &&
           lhs.createQualityMaps == rhs.createQualityMaps &&
           lhs.spatialRejectionClustering == rhs.spatialRejectionClustering &&
           lhs.restackExcludingFlaggedFrames == rhs.restackExcludingFlaggedFrames &&
           lhs.limitMemory == rhs.limitMemory && lhs.maxMemoryMb == rhs.maxMemoryMb &&
           lhs.starCountScaling == rhs.starCountScaling && lhs.exponentialAlpha == rhs.exponentialAlpha &&
           lhs.minWeightFloor == rhs.minWeightFloor && lhs.perChannelFwhmWeighting == rhs.perChannelFwhmWeighting &&
           lhs.localWeighting.enabled == rhs.localWeighting.enabled &&
           lhs.localWeighting.windowSize == rhs.localWeighting.windowSize &&
           lhs.localWeighting.gridSpacing == rhs.localWeighting.gridSpacing &&
           lhs.localWeighting.snrWeightExponent == rhs.localWeighting.snrWeightExponent &&
           lhs.normalizationMode == rhs.normalizationMode &&
           lhs.globalNormalizationMethod == rhs.globalNormalizationMethod && lhs.scaleEstimator == rhs.scaleEstimator &&
           lhs.overlapNormalization.enabled == rhs.overlapNormalization.enabled &&
           lhs.overlapNormalization.minOverlapPixels == rhs.overlapNormalization.minOverlapPixels &&
           lhs.overlapNormalization.minOverlapFraction == rhs.overlapNormalization.minOverlapFraction &&
           lhs.normalizationReferenceMode == rhs.normalizationReferenceMode &&
           lhs.manualNormalizationReference == rhs.manualNormalizationReference &&
           lhs.localNormalization.enabled == rhs.localNormalization.enabled &&
           lhs.localNormalization.gridSize == rhs.localNormalization.gridSize &&
           lhs.localNormalization.gridInterpolation == rhs.localNormalization.gridInterpolation &&
           lhs.localNormalization.localScaleEnabled == rhs.localNormalization.localScaleEnabled &&
           lhs.localNormalization.referenceStrategy == rhs.localNormalization.referenceStrategy &&
           lhs.localNormalization.maxIntegratedFrames == rhs.localNormalization.maxIntegratedFrames &&
           lhs.localNormalization.evaluationCriteria == rhs.localNormalization.evaluationCriteria &&
           lhs.localNormalization.backgroundEstimator == rhs.localNormalization.backgroundEstimator &&
           lhs.localNormalization.backgroundPercentile == rhs.localNormalization.backgroundPercentile &&
           lhs.localNormalization.sigmaClipK == rhs.localNormalization.sigmaClipK &&
           lhs.localNormalization.sigmaClipIterations == rhs.localNormalization.sigmaClipIterations &&
           lhs.localNormalization.backgroundSmoothingRadius == rhs.localNormalization.backgroundSmoothingRadius &&
           lhs.localNormalization.scaleEvaluation == rhs.localNormalization.scaleEvaluation &&
           lhs.localNormalization.scaleMode == rhs.localNormalization.scaleMode &&
           lhs.localNormalization.psfType == rhs.localNormalization.psfType &&
           lhs.localNormalization.moffatBeta == rhs.localNormalization.moffatBeta &&
           lhs.localNormalization.maxStars == rhs.localNormalization.maxStars &&
           lhs.localNormalization.minDetectionSnr == rhs.localNormalization.minDetectionSnr &&
           lhs.localNormalization.allowClusteredSources == rhs.localNormalization.allowClusteredSources &&
           lhs.localNormalization.lowClip == rhs.localNormalization.lowClip &&
           lhs.localNormalization.highClip == rhs.localNormalization.highClip &&
           lhs.localNormalization.hotPixelMedianRadius == rhs.localNormalization.hotPixelMedianRadius &&
           lhs.localNormalization.noiseReductionRadius == rhs.localNormalization.noiseReductionRadius &&
           lhs.localNormalization.localScaleSmoothingRadius == rhs.localNormalization.localScaleSmoothingRadius &&
           lhs.drizzle.enabled == rhs.drizzle.enabled && lhs.drizzle.scale == rhs.drizzle.scale &&
           lhs.drizzle.pixelFraction == rhs.drizzle.pixelFraction &&
           sameDrizzleKernel(lhs.drizzle.kernel, rhs.drizzle.kernel) && lhs.drizzle.cfaAware == rhs.drizzle.cfaAware &&
           lhs.drizzle.cfaPattern == rhs.drizzle.cfaPattern && lhs.cometStackingMode == rhs.cometStackingMode &&
           lhs.solveFinalMasterIfNeeded == rhs.solveFinalMasterIfNeeded &&
           lhs.registeredOutputPolicy == rhs.registeredOutputPolicy && lhs.hdr.enabled == rhs.hdr.enabled;
}

bool registrationEqual(const RegistrationSettings& lhs, const RegistrationSettings& rhs)
{
    using ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod;
    using ImageIntegration::RegistrationSettingsUtils::usesCometTracking;
    using ImageIntegration::RegistrationSettingsUtils::usesManualControlPoints;
    using ImageIntegration::RegistrationSettingsUtils::usesStarDetection;
    using ImageIntegration::RegistrationSettingsUtils::usesTransformContract;

    const QString lhs_method = normalizeRegistrationMethod(lhs.registrationMethod);
    const QString rhs_method = normalizeRegistrationMethod(rhs.registrationMethod);

    if (lhs.version != rhs.version || lhs_method != rhs_method ||
        lhs.autoReferenceDuringRegistration != rhs.autoReferenceDuringRegistration ||
        lhs.autoSolveUnsolvedFrames != rhs.autoSolveUnsolvedFrames ||
        lhs.enrichWithGaiaCatalog != rhs.enrichWithGaiaCatalog || lhs.referenceImage != rhs.referenceImage ||
        lhs.alignmentMode != rhs.alignmentMode || lhs.interpolation != rhs.interpolation ||
        lhs.cfaPattern != rhs.cfaPattern || lhs.debayerMethod != rhs.debayerMethod ||
        lhs.chromaticAberration.enabled != rhs.chromaticAberration.enabled)
    {
        return false;
    }

    if (lhs.chromaticAberration.enabled)
    {
        if (lhs.chromaticAberration.edgeFraction != rhs.chromaticAberration.edgeFraction ||
            lhs.chromaticAberration.transformType != rhs.chromaticAberration.transformType ||
            lhs.chromaticAberration.minMatchedStars != rhs.chromaticAberration.minMatchedStars ||
            lhs.chromaticAberration.interpolation != rhs.chromaticAberration.interpolation)
        {
            return false;
        }
    }

    if (usesCometTracking(lhs_method) && lhs.cometTracking != rhs.cometTracking)
    {
        return false;
    }

    if (usesStarDetection(lhs_method))
    {
        if (lhs.minStarPairs != rhs.minStarPairs || lhs.maxStarPairs != rhs.maxStarPairs || lhs.radius != rhs.radius ||
            lhs.threshold != rhs.threshold || lhs.amplitudeRangeEnabled != rhs.amplitudeRangeEnabled ||
            lhs.thresholdMinimum != rhs.thresholdMinimum || lhs.thresholdMaximum != rhs.thresholdMaximum ||
            lhs.roundnessThreshold != rhs.roundnessThreshold ||
            lhs.roundnessRangeEnabled != rhs.roundnessRangeEnabled ||
            lhs.roundnessThresholdMaximum != rhs.roundnessThresholdMaximum || lhs.convergence != rhs.convergence ||
            lhs.relaxPsfChecks != rhs.relaxPsfChecks || lhs.profileType != rhs.profileType ||
            lhs.minimumBeta != rhs.minimumBeta)
        {
            return false;
        }
    }

    if (usesTransformContract(lhs_method))
    {
        // Two-pass refinement follows the mode and is read by no run.
        if (lhs.transformation != rhs.transformation || lhs.transformMode != rhs.transformMode ||
            lhs.transformOrder != rhs.transformOrder)
        {
            return false;
        }

        if (usesManualControlPoints(lhs_method) && lhs.manualControlPointsByFrame != rhs.manualControlPointsByFrame)
        {
            return false;
        }
    }

    return true;
}

QString canonicalOrRaw(const FrameId& id)
{
    const QString canon = id.canonicalPath();
    return canon.isEmpty() ? id.toString() : canon;
}

std::vector<QString> sortedFramePaths(const std::vector<FrameId>& frames)
{
    std::vector<QString> paths;
    paths.reserve(frames.size());
    for (const auto& frame : frames)
    {
        paths.push_back(canonicalOrRaw(frame));
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

bool framesEqual(const std::vector<FrameId>& lhs, const std::vector<FrameId>& rhs)
{
    return sortedFramePaths(lhs) == sortedFramePaths(rhs);
}

bool optionalFrameEqual(const std::optional<FrameId>& lhs, const std::optional<FrameId>& rhs)
{
    if (lhs.has_value() != rhs.has_value())
    {
        return false;
    }
    if (!lhs.has_value())
    {
        return true;
    }
    return canonicalOrRaw(lhs.value()) == canonicalOrRaw(rhs.value());
}

QString manifestAstrometryApiKeyHash(const WorkflowManifest& manifest)
{
    if (!manifest.astrometryApiKeyHash.trimmed().isEmpty())
    {
        return manifest.astrometryApiKeyHash.trimmed();
    }
    return ImageIntegration::astrometryApiKeyHash(manifest.plateSolve.astrometryApiKey);
}
} // namespace

bool ManifestDiff::hasChanges() const
{
    return totalAdditions() > 0 || totalRemovals() > 0 || totalModifications() > 0 || !lightsCalibrationChanges.empty();
}

int ManifestDiff::totalAdditions() const
{
    auto countChanges = [](auto&& list) {
        return static_cast<int>(std::count_if(list.begin(), list.end(),
                                              [](const auto& entry) { return entry.change == ChangeType::Added; }));
    };

    return countChanges(calibrationChanges) + countChanges(registrationChanges) + countChanges(integrationChanges);
}

int ManifestDiff::totalRemovals() const
{
    auto countChanges = [](auto&& list) {
        return static_cast<int>(std::count_if(list.begin(), list.end(),
                                              [](const auto& entry) { return entry.change == ChangeType::Removed; }));
    };

    return countChanges(calibrationChanges) + countChanges(registrationChanges) + countChanges(integrationChanges);
}

int ManifestDiff::totalModifications() const
{
    auto countChanges = [](auto&& list) {
        return static_cast<int>(std::count_if(list.begin(), list.end(),
                                              [](const auto& entry) { return entry.change == ChangeType::Modified; }));
    };

    return countChanges(calibrationChanges) + countChanges(registrationChanges) + countChanges(integrationChanges) +
           static_cast<int>(lightsCalibrationChanges.size());
}

QString ManifestDiff::toSummaryText() const
{
    return QStringLiteral(
               "Manifest diff: %1 additions, %2 removals, %3 modifications (including %4 light calibration changes)")
        .arg(totalAdditions())
        .arg(totalRemovals())
        .arg(totalModifications())
        .arg(static_cast<int>(lightsCalibrationChanges.size()));
}

QJsonObject ManifestDiff::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("additions"), totalAdditions());
    obj.insert(QStringLiteral("removals"), totalRemovals());
    obj.insert(QStringLiteral("modifications"), totalModifications());

    QJsonArray integration;
    for (const auto& change : integrationChanges)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("groupKey"), change.groupKey.toString());
        entry.insert(QStringLiteral("outputFilename"), change.outputFilename);
        entry.insert(QStringLiteral("change"), changeTypeToString(change.change));
        entry.insert(QStringLiteral("lightFilesChanged"), change.lightFilesChanged);
        entry.insert(QStringLiteral("previousFrameCount"), change.previousFrameCount);
        entry.insert(QStringLiteral("currentFrameCount"), change.currentFrameCount);
        integration.append(entry);
    }
    obj.insert(QStringLiteral("integrationChanges"), integration);

    QJsonArray lights;
    for (const auto& change : lightsCalibrationChanges)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("lightPath"), change.lightPath.toJson());
        entry.insert(QStringLiteral("changed"), change.changed);
        lights.append(entry);
    }
    obj.insert(QStringLiteral("lightsCalibrationChanges"), lights);

    return obj;
}

bool calibrationSettingsEqual(const WorkflowManifest::CalibrationSettings& lhs,
                              const WorkflowManifest::CalibrationSettings& rhs)
{
    return lhs.cosmeticCorrection == rhs.cosmeticCorrection && lhs.sigmaHot == rhs.sigmaHot &&
           lhs.sigmaCold == rhs.sigmaCold && lhs.darkOptimization == rhs.darkOptimization &&
           lhs.darkMultiplicationFactor == rhs.darkMultiplicationFactor && lhs.flatAutoLevel == rhs.flatAutoLevel &&
           lhs.pedestal == rhs.pedestal && lhs.subtractBiasFromDark == rhs.subtractBiasFromDark;
}

bool lightsCalibrationParamsEqual(const WorkflowManifest::LightsCalibrationParams& lhs,
                                  const WorkflowManifest::LightsCalibrationParams& rhs)
{
    return lhs.lightPath == rhs.lightPath && lhs.biasPath == rhs.biasPath && lhs.darkPath == rhs.darkPath &&
           lhs.flatPath == rhs.flatPath && calibrationSettingsEqual(lhs.settings, rhs.settings);
}

bool plateSolveSettingsEqual(const WorkflowManifest::PlateSolveSettings& lhs, const QString& lhsApiKeyHash,
                             const WorkflowManifest::PlateSolveSettings& rhs, const QString& rhsApiKeyHash)
{
    return lhs.defaultSolver == rhs.defaultSolver && lhs.astapPath == rhs.astapPath &&
           lhs.astrometryLocalPath == rhs.astrometryLocalPath && lhsApiKeyHash == rhsApiKeyHash &&
           lhs.indexPath == rhs.indexPath && lhs.gaiaCatalogDirectory == rhs.gaiaCatalogDirectory &&
           lhs.gaiaCatalogPack == rhs.gaiaCatalogPack && lhs.focalLengthMm == rhs.focalLengthMm &&
           lhs.pixelSizeUm == rhs.pixelSizeUm && qFuzzyCompare(lhs.searchRadius + 1.0, rhs.searchRadius + 1.0) &&
           lhs.downsample == rhs.downsample && lhs.timeoutSeconds == rhs.timeoutSeconds && lhs.useHints == rhs.useHints;
}

ManifestDiff compareManifests(const WorkflowManifest& previous, const WorkflowManifest& current)
{
    ManifestDiff diff;
    const bool plate_solve_changed =
        !plateSolveSettingsEqual(previous.plateSolve, manifestAstrometryApiKeyHash(previous), current.plateSolve,
                                 manifestAstrometryApiKeyHash(current));

    // Compare individual lights calibration params
    std::map<QString, WorkflowManifest::LightsCalibrationParams> prev_lights;
    for (const auto& p : previous.lightsCalibration)
        prev_lights[p.lightPath.canonicalPath()] = p;

    std::map<QString, WorkflowManifest::LightsCalibrationParams> curr_lights;
    for (const auto& p : current.lightsCalibration)
        curr_lights[p.lightPath.canonicalPath()] = p;

    std::set<QString> all_light_paths;
    for (const auto& p : previous.lightsCalibration)
        all_light_paths.insert(p.lightPath.canonicalPath());
    for (const auto& p : current.lightsCalibration)
        all_light_paths.insert(p.lightPath.canonicalPath());

    for (const auto& path : all_light_paths)
    {
        auto it_prev = prev_lights.find(path);
        auto it_curr = curr_lights.find(path);

        ManifestDiff::LightsCalibrationChange change;
        change.lightPath = FrameId(path);

        if (it_prev == prev_lights.end() || it_curr == curr_lights.end())
        {
            change.changed = true;
        }
        else
        {
            change.changed = !lightsCalibrationParamsEqual(it_prev->second, it_curr->second);
        }

        if (change.changed)
        {
            diff.lightsCalibrationChanges.push_back(std::move(change));
        }
    }

    auto compareCalibrationStage = [&](const QString& frame_type,
                                       const std::optional<WorkflowManifest::CalibrationStage>& prev_stage,
                                       const std::optional<WorkflowManifest::CalibrationStage>& curr_stage) {
        std::map<QString, WorkflowManifest::CalibrationStage::Group> prev_groups;
        std::map<QString, WorkflowManifest::CalibrationStage::Group> curr_groups;

        if (prev_stage.has_value())
        {
            for (const auto& group : prev_stage->groups)
            {
                prev_groups[group.groupKey.toString()] = group;
            }
        }
        if (curr_stage.has_value())
        {
            for (const auto& group : curr_stage->groups)
            {
                curr_groups[group.groupKey.toString()] = group;
            }
        }

        std::set<QString> keys;
        for (const auto& entry : prev_groups)
        {
            keys.insert(entry.first);
        }
        for (const auto& entry : curr_groups)
        {
            keys.insert(entry.first);
        }

        for (const auto& key : keys)
        {
            ManifestDiff::CalibrationGroupChange change;
            change.frameType = frame_type;
            change.groupKey = GroupKey{key};

            auto prev_it = prev_groups.find(key);
            auto curr_it = curr_groups.find(key);

            if (prev_it == prev_groups.end())
            {
                change.change = ChangeType::Added;
                change.currentInputCount = static_cast<int>(curr_it->second.inputFiles.size());
            }
            else if (curr_it == curr_groups.end())
            {
                change.change = ChangeType::Removed;
                change.previousInputCount = static_cast<int>(prev_it->second.inputFiles.size());
            }
            else
            {
                const auto& prev_group = prev_it->second;
                const auto& curr_group = curr_it->second;

                change.previousInputCount = static_cast<int>(prev_group.inputFiles.size());
                change.currentInputCount = static_cast<int>(curr_group.inputFiles.size());

                change.inputFilesChanged = !framesEqual(prev_group.inputFiles, curr_group.inputFiles);
                change.stackingSettingsChanged = !stackingEqual(prev_group.stacking, curr_group.stacking);
                change.outputFilenameChanged = prev_group.outputFilename != curr_group.outputFilename;
                change.calibrationDependenciesChanged =
                    !optionalFrameEqual(prev_group.existingMaster, curr_group.existingMaster) ||
                    !optionalFrameEqual(prev_group.biasMaster, curr_group.biasMaster) ||
                    !optionalFrameEqual(prev_group.darkMaster, curr_group.darkMaster);
                change.calibrationFingerprintChanged = computeCalibrationGroupHash(previous, prev_group) !=
                                                       computeCalibrationGroupHash(current, curr_group);

                if (change.inputFilesChanged || change.stackingSettingsChanged || change.outputFilenameChanged ||
                    change.calibrationDependenciesChanged || change.calibrationFingerprintChanged)
                {
                    change.change = ChangeType::Modified;
                }
            }

            diff.calibrationChanges.push_back(std::move(change));
        }
    };

    compareCalibrationStage(QStringLiteral("BIAS"), previous.biasStage, current.biasStage);
    compareCalibrationStage(QStringLiteral("DARK"), previous.darkStage, current.darkStage);
    compareCalibrationStage(QStringLiteral("FLAT"), previous.flatStage, current.flatStage);

    std::map<QString, WorkflowManifest::RegistrationGroup> prev_reg;
    std::map<QString, WorkflowManifest::RegistrationGroup> curr_reg;
    for (const auto& group : previous.registrationGroups)
    {
        prev_reg[group.groupKey.toString()] = group;
    }
    for (const auto& group : current.registrationGroups)
    {
        curr_reg[group.groupKey.toString()] = group;
    }

    std::set<QString> reg_keys;
    for (const auto& entry : prev_reg)
    {
        reg_keys.insert(entry.first);
    }
    for (const auto& entry : curr_reg)
    {
        reg_keys.insert(entry.first);
    }

    for (const auto& key : reg_keys)
    {
        ManifestDiff::RegistrationGroupChange change;
        change.groupKey = GroupKey{key};

        auto prev_it = prev_reg.find(key);
        auto curr_it = curr_reg.find(key);

        if (prev_it == prev_reg.end())
        {
            change.change = ChangeType::Added;
            change.currentFrameCount = static_cast<int>(curr_it->second.lightFiles.size());
        }
        else if (curr_it == curr_reg.end())
        {
            change.change = ChangeType::Removed;
            change.previousFrameCount = static_cast<int>(prev_it->second.lightFiles.size());
        }
        else
        {
            const auto& prev_group = prev_it->second;
            const auto& curr_group = curr_it->second;

            change.previousFrameCount = static_cast<int>(prev_group.lightFiles.size());
            change.currentFrameCount = static_cast<int>(curr_group.lightFiles.size());

            change.lightFilesChanged = !framesEqual(prev_group.lightFiles, curr_group.lightFiles);
            change.settingsChanged =
                !registrationEqual(prev_group.settings, curr_group.settings) || plate_solve_changed;
            change.referenceFrameChanged = prev_group.referenceFrame != curr_group.referenceFrame;

            if (change.lightFilesChanged || change.settingsChanged || change.referenceFrameChanged)
            {
                change.change = ChangeType::Modified;
            }
        }

        diff.registrationChanges.push_back(std::move(change));
    }

    std::map<QString, WorkflowManifest::IntegrationGroup> prev_int;
    std::map<QString, WorkflowManifest::IntegrationGroup> curr_int;
    for (const auto& group : previous.integrationGroups)
    {
        prev_int[group.groupKey.toString()] = group;
    }
    for (const auto& group : current.integrationGroups)
    {
        curr_int[group.groupKey.toString()] = group;
    }

    std::set<QString> int_keys;
    for (const auto& entry : prev_int)
    {
        int_keys.insert(entry.first);
    }
    for (const auto& entry : curr_int)
    {
        int_keys.insert(entry.first);
    }

    for (const auto& key : int_keys)
    {
        ManifestDiff::IntegrationGroupChange change;
        change.groupKey = IntegrationGroupKey{GroupKey{key}};

        auto prev_it = prev_int.find(key);
        auto curr_it = curr_int.find(key);

        if (prev_it == prev_int.end())
        {
            change.change = ChangeType::Added;
            change.outputFilename = curr_it->second.outputFilename;
            change.currentFrameCount = static_cast<int>(curr_it->second.lightFiles.size());
        }
        else if (curr_it == curr_int.end())
        {
            change.change = ChangeType::Removed;
            change.outputFilename = prev_it->second.outputFilename;
            change.previousFrameCount = static_cast<int>(prev_it->second.lightFiles.size());
        }
        else
        {
            const auto& prev_group = prev_it->second;
            const auto& curr_group = curr_it->second;

            change.outputFilename = curr_group.outputFilename;
            change.previousFrameCount = static_cast<int>(prev_group.lightFiles.size());
            change.currentFrameCount = static_cast<int>(curr_group.lightFiles.size());

            change.lightFilesChanged = !framesEqual(prev_group.lightFiles, curr_group.lightFiles);
            change.stackingSettingsChanged = !stackingEqual(prev_group.stacking, curr_group.stacking) ||
                                             previous.combineRgbImages != current.combineRgbImages;
            change.calibrationAssignmentsChanged = prev_group.registrationGroupKey != curr_group.registrationGroupKey;
            change.normalizationSettingsChanged =
                !framesEqual(prev_group.normalizationReferenceFrames, curr_group.normalizationReferenceFrames);

            if (change.lightFilesChanged || change.stackingSettingsChanged || change.calibrationAssignmentsChanged ||
                change.normalizationSettingsChanged)
            {
                change.change = ChangeType::Modified;
            }
        }

        diff.integrationChanges.push_back(std::move(change));
    }

    return diff;
}
