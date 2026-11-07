// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ManifestDiff.h"

#include "ManifestHashing.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"

#include <QJsonArray>

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

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

/// An initializer that converts to the type of whatever field it initializes, so that one stands for one field.
struct AnyField
{
    template <typename Field> operator Field() const;
};

/// Whether `Aggregate` can be brace-initialized from as many initializers as `Indices` names.
template <typename Aggregate, std::size_t... Indices>
constexpr bool initializedBy(std::index_sequence<Indices...> /*fields*/)
{
    return requires { Aggregate{(static_cast<void>(Indices), AnyField{})...}; };
}

/// The number of fields of an aggregate: the most initializers it can be brace-initialized with.
template <typename Aggregate, std::size_t Count = 0u> constexpr std::size_t fieldCount()
{
    if constexpr (initializedBy<Aggregate>(std::make_index_sequence<Count + 1u>{}))
    {
        return fieldCount<Aggregate, Count + 1u>();
    }
    else
    {
        return Count;
    }
}

template <auto Field> bool sameField(const StackingSettings& lhs, const StackingSettings& rhs)
{
    return lhs.*Field == rhs.*Field;
}

template <auto Record, auto Field> bool sameRecordField(const StackingSettings& lhs, const StackingSettings& rhs)
{
    return lhs.*Record.*Field == rhs.*Record.*Field;
}

bool sameDrizzleKernelSetting(const StackingSettings& lhs, const StackingSettings& rhs)
{
    return sameDrizzleKernel(lhs.drizzle.kernel, rhs.drizzle.kernel);
}

constexpr char kStackingChange[] = "Stacking settings changed";
constexpr char kNormalizationChange[] = "Normalization settings changed";
constexpr char kDrizzleChange[] = "Drizzle settings changed";
constexpr char kRegisteredOutputChange[] = "Registered-output policy changed";
constexpr char kCometChange[] = "Comet stacking mode changed";

// The stack's product is stated by every setting it is made under, so every setting another stage reads is the
// stack's too. The normalization measures where the stack reads the lights: on the registration's plane, or on a
// drizzled group's analysis plane, which turning drizzle on or off moves and its scale, drop size, kernel and filter
// array do not. Whether the alignment writes the registered images is the registered-output policy's, and a comet stack
// places its lights from the comet's products at every stage.
constexpr LightStages kStack{LightStage::Stacking};
constexpr LightStages kNormalized{LightStage::Normalization, LightStage::Stacking};
constexpr LightStages kDrizzled{LightStage::Drizzle, LightStage::Stacking};
constexpr LightStages kDrizzlePlane{LightStage::Normalization, LightStage::Drizzle, LightStage::Stacking};
constexpr LightStages kAligned{LightStage::Alignment, LightStage::Stacking};
constexpr LightStages kEveryStage{LightStage::Alignment, LightStage::Normalization, LightStage::Drizzle,
                                  LightStage::Stacking};

template <auto Field>
constexpr StackingSettingReaders field(const char* name, const LightStages stages, const char* change)
{
    return StackingSettingReaders{name, stages, change, &sameField<Field>};
}

template <auto Record, auto Field>
constexpr StackingSettingReaders recordField(const char* name, const LightStages stages, const char* change)
{
    return StackingSettingReaders{name, stages, change, &sameRecordField<Record, Field>};
}

constexpr std::array kStackingSettingReaders = {
    field<&StackingSettings::stackMethod>("stackMethod", kStack, kStackingChange),
    field<&StackingSettings::rejectionMethod>("rejectionMethod", kStack, kStackingChange),
    // The automatic recommendation the rejection method was resolved from, kept as provenance: no stage reads it.
    field<&StackingSettings::requestedRejectionMethod>("requestedRejectionMethod", LightStages{}, ""),
    field<&StackingSettings::sigmaLow>("sigmaLow", kStack, kStackingChange),
    field<&StackingSettings::sigmaHigh>("sigmaHigh", kStack, kStackingChange),
    field<&StackingSettings::weighting>("weighting", kStack, kStackingChange),
    field<&StackingSettings::createQualityMaps>("createQualityMaps", kStack, kStackingChange),
    field<&StackingSettings::createRejectionMaps>("createRejectionMaps", kStack, kStackingChange),
    field<&StackingSettings::mergeRejectionMaps>("mergeRejectionMaps", kStack, kStackingChange),
    field<&StackingSettings::spatialRejectionClustering>("spatialRejectionClustering", kStack, kStackingChange),
    field<&StackingSettings::restackExcludingFlaggedFrames>("restackExcludingFlaggedFrames", kStack, kStackingChange),
    field<&StackingSettings::starCountScaling>("starCountScaling", kStack, kStackingChange),
    field<&StackingSettings::exponentialAlpha>("exponentialAlpha", kStack, kStackingChange),
    field<&StackingSettings::minWeightFloor>("minWeightFloor", kStack, kStackingChange),
    field<&StackingSettings::perChannelFwhmWeighting>("perChannelFwhmWeighting", kStack, kStackingChange),
    recordField<&StackingSettings::localWeighting, &LocalWeightingSettings::enabled>("localWeighting.enabled", kStack,
                                                                                     kStackingChange),
    recordField<&StackingSettings::localWeighting, &LocalWeightingSettings::windowSize>("localWeighting.windowSize",
                                                                                        kStack, kStackingChange),
    recordField<&StackingSettings::localWeighting, &LocalWeightingSettings::gridSpacing>("localWeighting.gridSpacing",
                                                                                         kStack, kStackingChange),
    recordField<&StackingSettings::localWeighting, &LocalWeightingSettings::snrWeightExponent>(
        "localWeighting.snrWeightExponent", kStack, kStackingChange),
    field<&StackingSettings::limitMemory>("limitMemory", kStack, kStackingChange),
    field<&StackingSettings::maxMemoryMb>("maxMemoryMb", kStack, kStackingChange),
    field<&StackingSettings::normalizationMode>("normalizationMode", kNormalized, kNormalizationChange),
    field<&StackingSettings::globalNormalizationMethod>("globalNormalizationMethod", kNormalized, kNormalizationChange),
    field<&StackingSettings::scaleEstimator>("scaleEstimator", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::overlapNormalization, &OverlapNormalizationSettings::enabled>(
        "overlapNormalization.enabled", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::overlapNormalization, &OverlapNormalizationSettings::minOverlapPixels>(
        "overlapNormalization.minOverlapPixels", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::overlapNormalization, &OverlapNormalizationSettings::minOverlapFraction>(
        "overlapNormalization.minOverlapFraction", kNormalized, kNormalizationChange),
    field<&StackingSettings::normalizationReferenceMode>("normalizationReferenceMode", kNormalized,
                                                         kNormalizationChange),
    field<&StackingSettings::manualNormalizationReference>("manualNormalizationReference", kNormalized,
                                                           kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::enabled>(
        "localNormalization.enabled", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::gridSize>(
        "localNormalization.gridSize", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::gridInterpolation>(
        "localNormalization.gridInterpolation", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::localScaleEnabled>(
        "localNormalization.localScaleEnabled", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::referenceStrategy>(
        "localNormalization.referenceStrategy", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::maxIntegratedFrames>(
        "localNormalization.maxIntegratedFrames", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::evaluationCriteria>(
        "localNormalization.evaluationCriteria", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::backgroundEstimator>(
        "localNormalization.backgroundEstimator", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::backgroundPercentile>(
        "localNormalization.backgroundPercentile", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::sigmaClipK>(
        "localNormalization.sigmaClipK", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::sigmaClipIterations>(
        "localNormalization.sigmaClipIterations", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::backgroundSmoothingRadius>(
        "localNormalization.backgroundSmoothingRadius", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::scaleEvaluation>(
        "localNormalization.scaleEvaluation", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::scaleMode>(
        "localNormalization.scaleMode", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::psfType>(
        "localNormalization.psfType", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::moffatBeta>(
        "localNormalization.moffatBeta", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::maxStars>(
        "localNormalization.maxStars", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::minDetectionSnr>(
        "localNormalization.minDetectionSnr", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::allowClusteredSources>(
        "localNormalization.allowClusteredSources", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::lowClip>(
        "localNormalization.lowClip", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::highClip>(
        "localNormalization.highClip", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::hotPixelMedianRadius>(
        "localNormalization.hotPixelMedianRadius", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::noiseReductionRadius>(
        "localNormalization.noiseReductionRadius", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::localNormalization, &LocalNormalizationSettings::localScaleSmoothingRadius>(
        "localNormalization.localScaleSmoothingRadius", kNormalized, kNormalizationChange),
    recordField<&StackingSettings::drizzle, &DrizzleSettings::enabled>("drizzle.enabled", kDrizzlePlane,
                                                                       kDrizzleChange),
    recordField<&StackingSettings::drizzle, &DrizzleSettings::scale>("drizzle.scale", kDrizzled, kDrizzleChange),
    recordField<&StackingSettings::drizzle, &DrizzleSettings::pixelFraction>("drizzle.pixelFraction", kDrizzled,
                                                                             kDrizzleChange),
    StackingSettingReaders{"drizzle.kernel", kDrizzled, kDrizzleChange, &sameDrizzleKernelSetting},
    recordField<&StackingSettings::drizzle, &DrizzleSettings::cfaAware>("drizzle.cfaAware", kDrizzled, kDrizzleChange),
    recordField<&StackingSettings::drizzle, &DrizzleSettings::cfaPattern>("drizzle.cfaPattern", kDrizzled,
                                                                          kDrizzleChange),
    field<&StackingSettings::cometStackingMode>("cometStackingMode", kEveryStage, kCometChange),
    field<&StackingSettings::solveFinalMasterIfNeeded>("solveFinalMasterIfNeeded", kStack, kStackingChange),
    field<&StackingSettings::registeredOutputPolicy>("registeredOutputPolicy", kAligned, kRegisteredOutputChange),
    recordField<&StackingSettings::hdr, &HdrExposureMergeSettings::enabled>("hdr.enabled", kStack, kStackingChange),
};

// A field added to StackingSettings, or to a settings record it holds, stops the build here until it has its entry
// above, naming the stages that read it, and the counts below are the new ones.
static_assert(fieldCount<StackingSettings>() == 30u && fieldCount<LocalWeightingSettings>() == 4u &&
                  fieldCount<OverlapNormalizationSettings>() == 3u && fieldCount<LocalNormalizationSettings>() == 24u &&
                  fieldCount<DrizzleSettings>() == 6u && fieldCount<HdrExposureMergeSettings>() == 1u,
              "A stacking setting was added or removed: give it its entry in kStackingSettingReaders, with the light "
              "stages that read it, and update the field counts here.");
static_assert(
    kStackingSettingReaders.size() == 30u - 5u + 4u + 3u + 24u + 6u + 1u,
    "kStackingSettingReaders holds one entry for each field of StackingSettings and of its settings records.");

/// Whether two stacking settings agree on everything a stage reads; for a calibration master, whose stage reads them
/// all.
bool stackingEqual(const StackingSettings& lhs, const StackingSettings& rhs)
{
    return std::all_of(
        kStackingSettingReaders.begin(), kStackingSettingReaders.end(),
        [&](const StackingSettingReaders& setting) { return setting.stages.empty() || setting.same(lhs, rhs); });
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

const QStringList& LightStageReasons::of(const LightStage stage) const noexcept
{
    switch (stage)
    {
    case LightStage::Alignment:
        return alignment;
    case LightStage::Normalization:
        return normalization;
    case LightStage::Drizzle:
        return drizzle;
    case LightStage::Stacking:
        break;
    }
    return stacking;
}

void LightStageReasons::add(const LightStages stages, const QString& reason)
{
    for (const auto& [stage, reasons] :
         {std::pair{LightStage::Alignment, &alignment}, std::pair{LightStage::Normalization, &normalization},
          std::pair{LightStage::Drizzle, &drizzle}, std::pair{LightStage::Stacking, &stacking}})
    {
        if (stages.contains(stage) && !reasons->contains(reason))
        {
            reasons->append(reason);
        }
    }
}

bool LightStageReasons::any() const noexcept
{
    return !stages().empty();
}

LightStages LightStageReasons::stages() const noexcept
{
    LightStages result;
    for (const LightStage stage :
         {LightStage::Alignment, LightStage::Normalization, LightStage::Drizzle, LightStage::Stacking})
    {
        if (!of(stage).isEmpty())
        {
            result = result.with(stage);
        }
    }
    return result;
}

std::span<const StackingSettingReaders> stackingSettingReaders() noexcept
{
    return kStackingSettingReaders;
}

LightStageReasons changedStackingSettings(const StackingSettings& previous, const StackingSettings& current)
{
    LightStageReasons reasons;
    for (const StackingSettingReaders& setting : kStackingSettingReaders)
    {
        if (!setting.stages.empty() && !setting.same(previous, current))
        {
            reasons.add(setting.stages, QString::fromLatin1(setting.change));
        }
    }
    return reasons;
}

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
            change.calibrationAssignmentsChanged = prev_group.registrationGroupKey != curr_group.registrationGroupKey;
            change.settingsChanged = changedStackingSettings(prev_group.stacking, curr_group.stacking);
            // The stack weighs the lights against the normalization reference frames too.
            if (!framesEqual(prev_group.normalizationReferenceFrames, curr_group.normalizationReferenceFrames))
            {
                change.settingsChanged.add({LightStage::Normalization, LightStage::Stacking},
                                           QStringLiteral("Normalization reference frames changed"));
            }
            if (previous.combineRgbImages != current.combineRgbImages)
            {
                change.settingsChanged.add({LightStage::Stacking}, QStringLiteral("Combine RGB images changed"));
            }

            if (change.lightFilesChanged || change.calibrationAssignmentsChanged || change.settingsChanged.any())
            {
                change.change = ChangeType::Modified;
            }
        }

        diff.integrationChanges.push_back(std::move(change));
    }

    return diff;
}
