// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LiteIntegrationProfile.h"

#include "processes/ImageIntegration/model/IntegrationGroupEngine.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/core/CalibrationMasterDefaults.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace
{
constexpr int kMinFramesForRejection = 8;
constexpr int kMinFramesForAdvancedRejection = 20;
constexpr int kLightMinFramesForWinsorized = 15;
constexpr int kLightMinFramesForLinearFit = 25;
constexpr int kLightMinFramesForGesdt = 40;
constexpr double kPercentileLow = 0.2;
constexpr double kPercentileHigh = 0.1;
constexpr double kGesdtMaxOutlierFraction = 0.3;
constexpr double kGesdtSignificanceLevel = 0.05;

[[nodiscard]] QString normalizedDisplayValue(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QStringLiteral("-") : value;
}

void appendSortedUnique(QStringList& destination, const QSet<QString>& values)
{
    destination = values.values();
    destination.sort(Qt::CaseInsensitive);
}

[[nodiscard]] int includedFrameCount(const std::vector<FrameInfo>& frames)
{
    return static_cast<int>(
        std::count_if(frames.begin(), frames.end(), [](const FrameInfo& frame) { return !frame.excluded; }));
}

[[nodiscard]] bool isAutoChoice(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized.isEmpty() || normalized == QStringLiteral("auto") || normalized == QStringLiteral("automatic") ||
           normalized == QStringLiteral("auto (recommended)");
}

[[nodiscard]] double dynamicSigmaForFrameCount(const int frameCount, const double base = 3.5)
{
    if (frameCount < 10)
    {
        return 3.0;
    }

    const double sigma = base - 0.5 * std::log10(static_cast<double>(frameCount));
    return std::clamp(std::round(sigma * 10.0) / 10.0, 2.0, 3.5);
}

void resolveAutoRejection(StackingSettings& settings, const FrameType type, const int frameCount)
{
    if (!isAutoChoice(settings.rejectionMethod))
    {
        settings.requestedRejectionMethod.clear();
        return;
    }

    const QString requested = settings.rejectionMethod.trimmed();
    settings.requestedRejectionMethod = requested.isEmpty() ? QStringLiteral("Auto") : requested;

    if (settings.stackMethod.trimmed().compare(QStringLiteral("Average"), Qt::CaseInsensitive) != 0)
    {
        settings.rejectionMethod = QStringLiteral("None");
        return;
    }

    switch (type)
    {
    case FrameType::Bias:
    case FrameType::Dark:
        if (frameCount < kMinFramesForRejection)
        {
            settings.rejectionMethod = QStringLiteral("None");
            return;
        }
        settings.rejectionMethod = (frameCount < kMinFramesForAdvancedRejection)
                                       ? QStringLiteral("Sigma Clipping")
                                       : QStringLiteral("Winsorized Sigma Clipping");
        settings.sigmaLow = dynamicSigmaForFrameCount(frameCount);
        settings.sigmaHigh = settings.sigmaLow;
        return;
    case FrameType::Flat:
        if (frameCount < kMinFramesForRejection)
        {
            settings.rejectionMethod = QStringLiteral("None");
            return;
        }
        settings.rejectionMethod = QStringLiteral("Sigma Clipping");
        settings.sigmaLow = dynamicSigmaForFrameCount(frameCount, 3.8);
        settings.sigmaHigh = settings.sigmaLow;
        return;
    case FrameType::Light:
        if (frameCount < kMinFramesForRejection)
        {
            settings.rejectionMethod = QStringLiteral("Percentile");
            settings.sigmaLow = kPercentileLow;
            settings.sigmaHigh = kPercentileHigh;
            return;
        }
        if (frameCount < kLightMinFramesForWinsorized)
        {
            settings.rejectionMethod = QStringLiteral("Sigma Clipping");
            settings.sigmaLow = dynamicSigmaForFrameCount(frameCount);
            settings.sigmaHigh = settings.sigmaLow;
            return;
        }
        if (frameCount < kLightMinFramesForLinearFit)
        {
            settings.rejectionMethod = QStringLiteral("Winsorized Sigma Clipping");
            settings.sigmaLow = dynamicSigmaForFrameCount(frameCount);
            settings.sigmaHigh = settings.sigmaLow;
            return;
        }
        if (frameCount < kLightMinFramesForGesdt)
        {
            settings.rejectionMethod = QStringLiteral("Linear Fit");
            settings.sigmaLow = 5.0;
            settings.sigmaHigh = 4.0;
            return;
        }
        settings.rejectionMethod = QStringLiteral("GESDT");
        settings.sigmaLow = kGesdtMaxOutlierFraction;
        settings.sigmaHigh = kGesdtSignificanceLevel;
        return;
    }
}

[[nodiscard]] StackingSettings stackingSettingsForGroup(const ImageIntegrationLite::ProcessingOptions& options,
                                                        const FrameType type, const int frameCount)
{
    StackingSettings settings = ImageIntegrationLite::stackingSettings(options);
    resolveAutoRejection(settings, type, frameCount);
    return settings;
}

[[nodiscard]] StackingSettings
calibrationStackingSettingsForGroup(const ImageIntegrationLite::ProcessingOptions& options, const FrameType type,
                                    const int frameCount)
{
    StackingSettings settings = ImageIntegrationLite::calibrationStackingSettings(type, options);
    resolveAutoRejection(settings, type, frameCount);
    return settings;
}

void applyCurrentGroupDefaults(IntegrationDataModel& model, const ImageIntegrationLite::ProcessingOptions& options)
{
    const IntegrationGroupEngine::GroupMap integrationGroups =
        IntegrationGroupEngine().computeGroups(model.lightFrames().frames(), model.lightIntegrationGroupingOptions());
    for (const auto& [integrationKey, frames] : integrationGroups)
    {
        model.setStackingSettings(FrameType::Light, integrationKey.key,
                                  stackingSettingsForGroup(options, FrameType::Light, static_cast<int>(frames.size())));
    }

    for (const auto& group : model.biasGroups())
    {
        model.setStackingSettings(
            FrameType::Bias, group.key,
            calibrationStackingSettingsForGroup(options, FrameType::Bias, includedFrameCount(group.frames)));
    }
    for (const auto& group : model.darkGroups())
    {
        model.setStackingSettings(
            FrameType::Dark, group.key,
            calibrationStackingSettingsForGroup(options, FrameType::Dark, includedFrameCount(group.frames)));
    }
    for (const auto& group : model.flatGroups())
    {
        model.setStackingSettings(
            FrameType::Flat, group.key,
            calibrationStackingSettingsForGroup(options, FrameType::Flat, includedFrameCount(group.frames)));
    }

    const LightCalibrationSettings lightCalibration = ImageIntegrationLite::lightCalibrationSettings(options);
    for (const auto& group : model.lightGroups())
    {
        model.setLightCalibrationSettings(group.key, lightCalibration);
    }

    const RegistrationSettings registration = ImageIntegrationLite::registrationSettings(options);
    for (const auto& [key, frames] : model.registrationGroups())
    {
        Q_UNUSED(frames);
        model.setRegistrationSettings(RegistrationGroupEngine::toMachineGroupKey(key), registration);
    }
}

[[nodiscard]] QString outputPathForMaster(const QString& outputDirectory, const QString& outputFilename)
{
    if (outputDirectory.trimmed().isEmpty())
    {
        return outputFilename;
    }

    return QDir(outputDirectory).filePath(outputFilename);
}

[[nodiscard]] bool outputExists(const QString& outputDirectory, const QString& outputFilename)
{
    if (outputDirectory.trimmed().isEmpty() || outputFilename.trimmed().isEmpty())
    {
        return false;
    }

    return QFileInfo::exists(outputPathForMaster(outputDirectory, outputFilename));
}

/// The stage group that stacks the master of `group`, a calibration group of `frameType`: its supplied master when it
/// holds one, its stored stacking settings or Lite's for its frame count, and its output reused when it exists and the
/// run does not reprocess.
[[nodiscard]] WorkflowManifest::CalibrationStage::Group
calibrationStageGroup(const QString& outputDirectory, const IntegrationDataModel& model, const FrameType frameType,
                      const CalibrationOverrideType overrideType, const GroupedFrameSet& group,
                      const bool reprocessExisting)
{
    WorkflowManifest::CalibrationStage::Group manifestGroup;
    manifestGroup.groupKey = group.key;
    manifestGroup.outputFilename =
        calibrationMasterOutputFilename(overrideType, group.key, model.groupingOptions(frameType), frameType);
    manifestGroup.inputFiles.reserve(group.frames.size());
    for (const FrameInfo& frame : group.frames)
    {
        manifestGroup.inputFiles.push_back(frame.id);
    }

    if (const auto settingsIt = model.stackingSettings(frameType).find(group.key);
        settingsIt != model.stackingSettings(frameType).end())
    {
        manifestGroup.stacking = settingsIt->second;
    }
    else
    {
        manifestGroup.stacking = calibrationStackingSettingsForGroup(ImageIntegrationLite::defaultProcessingOptions(),
                                                                     frameType, includedFrameCount(group.frames));
    }

    for (const auto& frame : group.frames)
    {
        if (frame.excluded)
        {
            continue;
        }

        if (model.isMasterFrame(frame.id))
        {
            manifestGroup.skipIfExists = true;
            manifestGroup.existingMaster = frame.id;
            break;
        }
    }

    if (!manifestGroup.skipIfExists && !reprocessExisting &&
        outputExists(outputDirectory, manifestGroup.outputFilename))
    {
        manifestGroup.skipIfExists = true;
    }
    return manifestGroup;
}

/// The file a calibration stage group's master is read from: the supplied master, else the one the stage writes.
[[nodiscard]] QString masterPathOf(const QString& outputDirectory,
                                   const WorkflowManifest::CalibrationStage::Group& group)
{
    return group.existingMaster.has_value() ? group.existingMaster->canonicalPath()
                                            : outputPathForMaster(outputDirectory, group.outputFilename);
}

void ensureStageBefore(QStringList& sequence, const QString& stage, const QString& beforeStage)
{
    if (sequence.contains(stage))
    {
        return;
    }

    const int beforeIndex = sequence.indexOf(beforeStage);
    if (beforeIndex >= 0)
    {
        sequence.insert(beforeIndex, stage);
        return;
    }

    sequence.append(stage);
}
} // namespace

namespace ImageIntegrationLite
{

ProcessingOptions defaultProcessingOptions()
{
    ProcessingOptions options;
    options.maxParallelCores = ImageIntegration::core::detectedParallelCoreCount();
    return options;
}

RejectionParameterDefaults rejectionParameterDefaults(const QString& rejectionMethod)
{
    const QString normalized = rejectionMethod.trimmed().toLower();
    if (normalized == QStringLiteral("linear fit") || normalized == QStringLiteral("linear_fit"))
    {
        return RejectionParameterDefaults{5.0, 4.0};
    }
    if (normalized == QStringLiteral("percentile"))
    {
        return RejectionParameterDefaults{kPercentileLow, kPercentileHigh};
    }
    if (normalized == QStringLiteral("gesdt"))
    {
        return RejectionParameterDefaults{kGesdtMaxOutlierFraction, kGesdtSignificanceLevel};
    }
    return {};
}

GroupingOptions sourceGroupingOptions(const FrameType type)
{
    GroupingOptions options;
    options.byResolution = true;
    options.byIsoGain = false;
    options.byOffset = true;
    options.bySession = false;
    options.byCustomTag = false;

    switch (type)
    {
    case FrameType::Bias:
        options.byFilter = false;
        options.byObject = false;
        options.byExposure = false;
        options.byTemperature = false;
        break;
    case FrameType::Dark:
        options.byFilter = false;
        options.byObject = false;
        options.byExposure = true;
        options.byTemperature = false;
        options.byCustomTag = true;
        break;
    case FrameType::Flat:
        options.byFilter = true;
        options.byObject = false;
        options.byExposure = false;
        options.byTemperature = false;
        break;
    case FrameType::Light:
        options.byFilter = true;
        options.byObject = true;
        options.byExposure = true;
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

GroupingOptions lightIntegrationGroupingOptions()
{
    GroupingOptions options = sourceGroupingOptions(FrameType::Light);
    options.byFilter = true;
    options.byObject = true;
    options.byResolution = true;
    options.bySession = false;
    options.byExposure = false;
    options.byTemperature = false;
    options.byCustomTag = false;
    options.exposureToleranceSeconds = 0.0;
    options.temperatureToleranceC = 0.0;
    return options;
}

CalibrationMatchingPolicy calibrationMatchingPolicy(const CalibrationPreset preset)
{
    CalibrationMatchingPolicy policy;
    policy.allowFlatSessionFallback = true;
    policy.maxFlatSessionDaysApart = 30;
    policy.preferNewerFlat = false;
    policy.allowDarkExposureFallback = true;
    policy.darkExposureTolerancePercent = 20;
    policy.warnOnFallback = false;
    policy.subtractBiasFromDark = false;

    switch (preset)
    {
    case CalibrationPreset::Standard:
        break;
    case CalibrationPreset::StrictMatching:
        policy.allowFlatSessionFallback = false;
        policy.allowDarkExposureFallback = false;
        break;
    case CalibrationPreset::Adaptive:
        policy.allowFlatSessionFallback = true;
        policy.allowDarkExposureFallback = true;
        break;
    }

    return policy;
}

LightCalibrationSettings lightCalibrationSettings(const ProcessingOptions& options)
{
    LightCalibrationSettings settings;
    settings.cosmeticCorrection = true;
    settings.darkOptimization = options.darkOptimization;
    settings.flatAutoLevel = options.flatAutoLevel;

    switch (options.calibrationPreset)
    {
    case CalibrationPreset::Standard:
    case CalibrationPreset::StrictMatching:
        break;
    case CalibrationPreset::Adaptive:
        settings.darkOptimization = true;
        break;
    }

    return settings;
}

RegistrationSettings registrationSettings()
{
    return registrationSettings(defaultProcessingOptions());
}

RegistrationSettings registrationSettings(const ProcessingOptions& options)
{
    RegistrationSettings settings;
    settings.autoReferenceDuringRegistration = options.autoReference;
    settings.autoSolveUnsolvedFrames = false;
    settings.enrichWithGaiaCatalog = true;
    settings.registrationMethod = QStringLiteral("star_matching");
    // The integration dialog's own mapping, so one choice is one stored contract in either front end.
    using namespace ImageIntegration::RegistrationSettingsUtils;
    settings.transformation = baseTransformForSelectionKey(options.transformation);
    settings.transformMode = transformModeForSelectionKey(options.transformation);
    settings.transformOrder = transformOrderForSelectionKey(options.transformation);
    settings.twoPassRefinementEnabled = twoPassForSelectionKey(options.transformation);
    settings.alignmentMode = options.autoCrop ? QStringLiteral("intersection") : QStringLiteral("standard");
    settings.interpolation = QStringLiteral("Auto");
    settings.cometTracking = std::nullopt;
    settings.chromaticAberration.enabled = false;
    return settings;
}

StackingSettings stackingSettings()
{
    return stackingSettings(defaultProcessingOptions());
}

StackingSettings stackingSettings(const ProcessingOptions& options)
{
    StackingSettings settings;
    settings.stackMethod = options.stackMethod;
    settings.rejectionMethod = options.rejectionMethod;
    if (settings.stackMethod.trimmed().compare(QStringLiteral("Average"), Qt::CaseInsensitive) != 0)
    {
        settings.rejectionMethod = QStringLiteral("None");
    }
    const RejectionParameterDefaults rejectionDefaults = rejectionParameterDefaults(settings.rejectionMethod);
    settings.sigmaLow = options.rejectionLow > 0.0 ? options.rejectionLow : rejectionDefaults.low;
    settings.sigmaHigh = options.rejectionHigh > 0.0 ? options.rejectionHigh : rejectionDefaults.high;
    settings.weighting = options.weighting;
    settings.createRejectionMaps = false;
    settings.mergeRejectionMaps = false;
    settings.limitMemory = options.limitMemory;
    settings.maxMemoryMb = (std::max)(512, options.maxMemoryMb);
    settings.drizzle.enabled = options.drizzleEnabled;
    settings.drizzle.scale = options.drizzleScale;
    settings.drizzle.pixelFraction = options.drizzlePixelFraction;
    settings.drizzle.kernel = QStringLiteral("Square");
    settings.solveFinalMasterIfNeeded = options.solveFinalMasterIfNeeded;

    auto applyNormalizationPreset = [&settings](const NormalizationPreset preset) {
        settings.localNormalization.enabled = false;
        switch (preset)
        {
        case NormalizationPreset::None:
            settings.normalizationMode = NormalizationMode::None;
            settings.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
            break;
        case NormalizationPreset::GlobalAdditive:
            settings.normalizationMode = NormalizationMode::Global;
            settings.globalNormalizationMethod = GlobalNormalizationMethod::Additive;
            break;
        case NormalizationPreset::GlobalAdditiveScaling:
            settings.normalizationMode = NormalizationMode::Global;
            settings.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
            break;
        case NormalizationPreset::GlobalMultiplicative:
            settings.normalizationMode = NormalizationMode::Global;
            settings.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
            break;
        case NormalizationPreset::GlobalMultiplicativeScaling:
            settings.normalizationMode = NormalizationMode::Global;
            settings.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
            break;
        case NormalizationPreset::Local:
            settings.normalizationMode = NormalizationMode::Local;
            settings.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
            settings.localNormalization.enabled = true;
            break;
        }
    };

    settings.scaleEstimator = ScaleEstimatorMethod::IKSS;
    settings.createQualityMaps = false;
    applyNormalizationPreset(options.normalizationPreset);
    return settings;
}

StackingSettings calibrationStackingSettings(const FrameType type, const ProcessingOptions& options)
{
    StackingSettings settings = stackingSettings(options);
    settings.weighting = QStringLiteral("None");
    settings.createQualityMaps = false;
    settings.createRejectionMaps = false;
    settings.mergeRejectionMaps = false;
    settings.drizzle.enabled = false;
    settings.solveFinalMasterIfNeeded = false;
    settings.localNormalization.enabled = false;

    switch (type)
    {
    case FrameType::Flat:
        settings.normalizationMode = NormalizationMode::Global;
        settings.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
        break;
    case FrameType::Bias:
    case FrameType::Dark:
    case FrameType::Light:
        settings.normalizationMode = NormalizationMode::None;
        break;
    }

    return settings;
}

QString darkFlatCustomTag()
{
    return QStringLiteral("Dark Flat");
}

bool isDarkFlatFrame(const FrameInfo& frame)
{
    return frame.type() == FrameType::Dark && frame.customTag.compare(darkFlatCustomTag(), Qt::CaseInsensitive) == 0;
}

void applyDefaults(IntegrationDataModel& model)
{
    applyDefaults(model, defaultProcessingOptions());
}

void applyDefaults(IntegrationDataModel& model, const ProcessingOptions& options)
{
    model.beginBatchUpdate();
    model.setGroupingOptions(FrameType::Bias, sourceGroupingOptions(FrameType::Bias));
    model.setGroupingOptions(FrameType::Dark, sourceGroupingOptions(FrameType::Dark));
    model.setGroupingOptions(FrameType::Flat, sourceGroupingOptions(FrameType::Flat));
    model.setGroupingOptions(FrameType::Light, sourceGroupingOptions(FrameType::Light));
    model.setLightIntegrationGroupingOptions(lightIntegrationGroupingOptions());
    model.setCalibrationMatchingPolicy(calibrationMatchingPolicy(options.calibrationPreset));
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::InMemory);
    applyCurrentGroupDefaults(model, options);
    model.endBatchUpdate();
}

void applyProcessingOptions(IntegrationDataModel& model, const ProcessingOptions& options)
{
    model.beginBatchUpdate();
    model.setCalibrationMatchingPolicy(calibrationMatchingPolicy(options.calibrationPreset));
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::InMemory);
    applyCurrentGroupDefaults(model, options);
    model.endBatchUpdate();
}

void autoDetectSingleFrameCalibrationMasters(IntegrationDataModel& model)
{
    const auto detectMasters = [&model](const GroupedFrameView& groups) {
        for (const auto& group : groups)
        {
            const FrameInfo* onlyFrame = nullptr;
            int includedCount = 0;
            for (const FrameInfo& frame : group.frames)
            {
                if (frame.excluded)
                {
                    continue;
                }

                ++includedCount;
                onlyFrame = &frame;
                if (includedCount > 1)
                {
                    break;
                }
            }

            if (includedCount != 1 || !onlyFrame || model.isMasterFrame(onlyFrame->id))
            {
                continue;
            }

            model.setMasterStatus(onlyFrame->id.canonicalPath(), true, MasterSource::AutoDetected);
        }
    };

    model.beginBatchUpdate();
    detectMasters(model.biasGroups());
    detectMasters(model.darkGroups());
    detectMasters(model.flatGroups());
    model.endBatchUpdate();
}

void applyManifestMetadata(WorkflowManifest& manifest)
{
    manifest.processLabel = QStringLiteral("Image Integration Lite");
    manifest.processingProfileId.clear();
    manifest.processingProfileLabel.clear();
}

void applyExecutionOptions(WorkflowManifest& manifest, const ProcessingOptions& options)
{
    manifest.maxParallelCores = ImageIntegration::core::clampParallelCoreLimit(options.maxParallelCores);
}

void applyDarkFlatCalibration(WorkflowManifest& manifest, const IntegrationDataModel& model,
                              const bool reprocessExisting)
{
    struct DarkFlatGroup
    {
        GroupKey groupKey;
        DarkGroupKey key;
        QString masterPath;
    };

    std::vector<DarkFlatGroup> darkFlatGroups;
    for (const auto& group : model.darkGroups())
    {
        const auto firstDarkFlat = std::find_if(group.frames.begin(), group.frames.end(), isDarkFlatFrame);
        if (firstDarkFlat == group.frames.end())
        {
            continue;
        }

        const WorkflowManifest::CalibrationStage::Group manifestGroup = calibrationStageGroup(
            manifest.outputDirectory, model, FrameType::Dark, CalibrationOverrideType::Dark, group, reprocessExisting);

        if (!manifest.darkStage.has_value())
        {
            WorkflowManifest::CalibrationStage stage;
            stage.frameType = QStringLiteral("DARK");
            manifest.darkStage = std::move(stage);
        }

        const auto existing = std::find_if(manifest.darkStage->groups.begin(), manifest.darkStage->groups.end(),
                                           [&](const WorkflowManifest::CalibrationStage::Group& existingGroup) {
                                               return existingGroup.groupKey == manifestGroup.groupKey;
                                           });
        if (existing == manifest.darkStage->groups.end())
        {
            manifest.darkStage->groups.push_back(manifestGroup);
        }
        else
        {
            *existing = manifestGroup;
        }

        darkFlatGroups.push_back({group.key, DarkGroupKey::fromFrame(*firstDarkFlat),
                                  masterPathOf(manifest.outputDirectory, manifestGroup)});
    }

    std::set<QString> darkFlatMasterPaths;
    std::vector<DarkCalibrationGroup> darkFlatCalibrationGroups;
    darkFlatCalibrationGroups.reserve(darkFlatGroups.size());
    for (const DarkFlatGroup& group : darkFlatGroups)
    {
        darkFlatMasterPaths.insert(group.masterPath);
        darkFlatCalibrationGroups.push_back({group.key, group.groupKey, group.masterPath});
    }

    for (auto& params : manifest.lightsCalibration)
    {
        if (params.darkPath.has_value() &&
            darkFlatMasterPaths.find(params.darkPath->canonicalPath()) != darkFlatMasterPaths.end())
        {
            params.darkPath.reset();
        }
    }

    if (manifest.flatStage.has_value())
    {
        // Lite calibrates a flat with a dark flat, never with a light's dark, so a dark the builder matched to a flat
        // is let go. A master dark flat holds the bias, as every master dark does, so a flat takes the bias only when
        // no dark flat is applied to it, by the builder's rule; where the builder left the bias out for the dark Lite
        // lets go, the flat takes it here and the bias group is stacked for it.
        GroupingOptions darkFlatMatching = model.groupingOptions(FrameType::Dark);
        darkFlatMatching.byCustomTag = false;
        CalibrationMatcher darkFlatMatcher(model.groupingOptions(FrameType::Bias), darkFlatMatching,
                                           model.groupingOptions(FrameType::Flat), model.calibrationMatchingPolicy());
        darkFlatMatcher.setDarkGroups(std::move(darkFlatCalibrationGroups));

        std::vector<BiasCalibrationGroup> biasCandidates;
        for (const auto& group : model.biasGroups())
        {
            if (group.frames.empty())
            {
                continue;
            }
            const WorkflowManifest::CalibrationStage::Group stageGroup =
                calibrationStageGroup(manifest.outputDirectory, model, FrameType::Bias, CalibrationOverrideType::Bias,
                                      group, reprocessExisting);
            biasCandidates.push_back({BiasGroupKey::fromFrame(group.frames.front()), group.key,
                                      masterPathOf(manifest.outputDirectory, stageGroup)});
        }
        CalibrationMatcher biasMatcher(model.groupingOptions(FrameType::Bias), model.groupingOptions(FrameType::Dark),
                                       model.groupingOptions(FrameType::Flat), model.calibrationMatchingPolicy());
        biasMatcher.setBiasGroups(std::move(biasCandidates));
        biasMatcher.setOverrides(model.calibrationOverrides());

        for (auto& flatGroup : manifest.flatStage->groups)
        {
            // A supplied master flat is used as it is and takes no master, as the builder leaves it. A flat whose
            // generated master exists is matched as any other, so the masters it names - which its master's identity
            // holds - are the same whether or not the file is there.
            if (flatGroup.existingMaster.has_value())
            {
                continue;
            }

            const auto sourceGroup =
                std::find_if(model.flatGroups().begin(), model.flatGroups().end(),
                             [&](const GroupedFrameSet& group) { return group.key == flatGroup.groupKey; });
            if (sourceGroup == model.flatGroups().end() || sourceGroup->frames.empty())
            {
                continue;
            }

            const LightGroupKey flatKeyAsLight = LightGroupKey::fromFrame(sourceGroup->frames.front());
            const CalibrationMatch darkMatch =
                darkFlatGroups.empty() ? CalibrationMatch{} : darkFlatMatcher.findDark(flatKeyAsLight);
            flatGroup.darkMaster.reset();
            if (appliesMaster(darkMatch) && !darkMatch.masterPath.isEmpty())
            {
                flatGroup.darkMaster = FrameId(darkMatch.masterPath);
            }

            if (!flatTakesBias(darkMatch))
            {
                flatGroup.biasMaster.reset();
                continue;
            }
            if (flatGroup.biasMaster.has_value())
            {
                continue;
            }

            const CalibrationMatch biasMatch = biasMatcher.findBias(flatKeyAsLight);
            if (!appliesMaster(biasMatch) || biasMatch.masterPath.isEmpty())
            {
                continue;
            }
            const auto biasGroup =
                std::find_if(model.biasGroups().begin(), model.biasGroups().end(), [&](const GroupedFrameSet& group) {
                    return group.key.toString() == biasMatch.matchedGroupKey;
                });
            if (biasGroup != model.biasGroups().end())
            {
                if (!manifest.biasStage.has_value())
                {
                    WorkflowManifest::CalibrationStage stage;
                    stage.frameType = QStringLiteral("BIAS");
                    manifest.biasStage = std::move(stage);
                }
                const bool stacked =
                    std::any_of(manifest.biasStage->groups.begin(), manifest.biasStage->groups.end(),
                                [&](const auto& stageGroup) { return stageGroup.groupKey == biasGroup->key; });
                if (!stacked)
                {
                    manifest.biasStage->groups.push_back(
                        calibrationStageGroup(manifest.outputDirectory, model, FrameType::Bias,
                                              CalibrationOverrideType::Bias, *biasGroup, reprocessExisting));
                }
                ensureStageBefore(manifest.stageSequence, QStringLiteral("Bias"),
                                  manifest.stageSequence.contains(QStringLiteral("Dark")) ? QStringLiteral("Dark")
                                                                                          : QStringLiteral("Flat"));
            }
            flatGroup.biasMaster = FrameId(biasMatch.masterPath);
        }
    }

    if (darkFlatGroups.empty())
    {
        return;
    }

    ensureStageBefore(manifest.stageSequence, QStringLiteral("Dark"), QStringLiteral("Flat"));
}

DatasetSummary summarizeDataset(const IntegrationDataModel& model)
{
    DatasetSummary summary;
    summary.biasFrames = static_cast<int>(model.biasFrames().size());
    summary.darkFrames = static_cast<int>(model.darkFrames().size());
    summary.flatFrames = static_cast<int>(model.flatFrames().size());
    summary.lightFrames = static_cast<int>(model.lightFrames().size());

    QSet<QString> filters;
    QSet<QString> objects;
    for (const FrameInfo& frame : model.lightFrames().frames())
    {
        if (frame.excluded)
        {
            continue;
        }

        filters.insert(normalizedDisplayValue(frame.metadata.filter()));
        objects.insert(normalizedDisplayValue(frame.effectiveObjectName()));
        summary.totalLightExposureSeconds += frame.metadata.exposureSeconds();
    }

    appendSortedUnique(summary.filters, filters);
    appendSortedUnique(summary.objects, objects);
    return summary;
}

WorkflowValidationResult validateReadiness(const IntegrationDataModel& model)
{
    WorkflowValidationResult result = model.validate();

    QStringList realObjects;
    for (const QString& object : summarizeDataset(model).objects)
    {
        if (object != QStringLiteral("-"))
        {
            realObjects.append(object);
        }
    }

    if (realObjects.size() > 1)
    {
        result.issues.push_back({ValidationIssue::Severity::Error, ValidationErrorCode::Unknown,
                                 QStringLiteral("Image Integration Lite expects one target object. Use the full Image "
                                                "Integration process for multi-object projects."),
                                 QString(), FixTarget::LightFrames});
    }

    const std::vector<FrameInfo> darkFrames = model.darkFrames().frames();
    const bool hasDarkFlat = std::any_of(darkFrames.begin(), darkFrames.end(),
                                         [](const FrameInfo& frame) { return isDarkFlatFrame(frame); });
    if (!model.flatFrames().empty() && model.biasFrames().empty() && !hasDarkFlat)
    {
        result.issues.push_back(
            {ValidationIssue::Severity::Warning, ValidationErrorCode::MissingCalibrationMatch,
             QStringLiteral("Flat frames have no bias or Dark Flat calibration. Master flats will be uncalibrated and "
                            "may over-correct lights."),
             QString(), FixTarget::FlatFrames});
    }

    return result;
}

QString firstReadinessErrors(const WorkflowValidationResult& validation)
{
    QStringList lines;
    for (const ValidationIssue& issue : validation.issues)
    {
        if (issue.severity != ValidationIssue::Severity::Error)
        {
            continue;
        }

        lines << issue.message;
        if (lines.size() >= 3)
        {
            break;
        }
    }

    return lines.join(QStringLiteral(" "));
}

WorkflowManifest buildManifest(const IntegrationDataModel& model, const ProcessingOptions& options,
                               const bool reprocessExisting)
{
    WorkflowManifest manifest =
        ManifestBuilder(model).setReprocessExisting(reprocessExisting).setIncludeRejectionMaps(false).build();
    applyManifestMetadata(manifest);
    applyExecutionOptions(manifest, options);
    applyDarkFlatCalibration(manifest, model, reprocessExisting);
    return manifest;
}

WorkflowManifest prepareManifest(const QJsonObject& modelSnapshot, const ProcessingOptions& options,
                                 const bool reprocessExisting)
{
    IntegrationDataModel preparedModel;
    preparedModel.fromJson(modelSnapshot);
    applyDefaults(preparedModel, options);
    autoDetectSingleFrameCalibrationMasters(preparedModel);

    const WorkflowValidationResult validation = validateReadiness(preparedModel);
    if (validation.hasErrors())
    {
        throw std::runtime_error(firstReadinessErrors(validation).toUtf8().constData());
    }

    return buildManifest(preparedModel, options, reprocessExisting);
}

} // namespace ImageIntegrationLite
