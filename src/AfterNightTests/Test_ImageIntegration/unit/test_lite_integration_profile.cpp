// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>
#include <optional>
#include <vector>

#include "processes/ImageIntegration/lite/LiteIntegrationProfile.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

namespace
{
FrameInfo makeLightFrame(const QString& filename, const QString& filter = QStringLiteral("L"),
                         const QString& object = QStringLiteral("M42"))
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    const FrameMetadata metadata(FrameType::Light, QSize(320, 240), 60.0, std::nullopt, 100, 10, filter, object,
                                 QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2026-01-01"), QString());
}

FrameInfo makeTypedFrame(const FrameType type, const QString& filename, const QString& filter = QStringLiteral("L"),
                         const QString& object = QStringLiteral("M42"), const double exposure = 60.0,
                         const QString& customTag = {})
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    const FrameMetadata metadata(type, QSize(320, 240), exposure, std::nullopt, 100, 10, filter, object,
                                 QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2026-01-01"), customTag);
}

std::vector<FrameInfo> makeLightFrames(const int count, const QString& prefix)
{
    std::vector<FrameInfo> frames;
    frames.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        frames.push_back(makeLightFrame(QStringLiteral("%1_%2.fit").arg(prefix).arg(index + 1, 2, 10, QChar('0'))));
    }
    return frames;
}

std::vector<FrameInfo> makeTypedFrames(const FrameType type, const int count, const QString& prefix,
                                       const QString& filter = QStringLiteral("L"),
                                       const QString& object = QStringLiteral("M42"), const double exposure = 60.0)
{
    std::vector<FrameInfo> frames;
    frames.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        frames.push_back(makeTypedFrame(type, QStringLiteral("%1_%2.fit").arg(prefix).arg(index + 1, 2, 10, QChar('0')),
                                        filter, object, exposure));
    }
    return frames;
}
} // namespace

TEST(ImageIntegrationLiteProfile, SourceGroupingKeepsSessionAndCustomTagsOutOfTheLiteWorkflow)
{
    const GroupingOptions lightOptions = ImageIntegrationLite::sourceGroupingOptions(FrameType::Light);
    EXPECT_TRUE(lightOptions.byResolution);
    EXPECT_TRUE(lightOptions.byFilter);
    EXPECT_TRUE(lightOptions.byObject);
    EXPECT_TRUE(lightOptions.byExposure);
    EXPECT_FALSE(lightOptions.bySession);
    EXPECT_FALSE(lightOptions.byCustomTag);

    const GroupingOptions flatOptions = ImageIntegrationLite::sourceGroupingOptions(FrameType::Flat);
    EXPECT_TRUE(flatOptions.byFilter);
    EXPECT_FALSE(flatOptions.byObject);
    EXPECT_FALSE(flatOptions.bySession);

    const GroupingOptions darkOptions = ImageIntegrationLite::sourceGroupingOptions(FrameType::Dark);
    EXPECT_TRUE(darkOptions.byExposure);
    EXPECT_TRUE(darkOptions.byCustomTag);
}

TEST(ImageIntegrationLiteProfile, IntegrationGroupingExposesOnlyFilterAsUserFacingSplit)
{
    const GroupingOptions options = ImageIntegrationLite::lightIntegrationGroupingOptions();

    EXPECT_TRUE(options.byResolution);
    EXPECT_TRUE(options.byFilter);
    EXPECT_TRUE(options.byObject);
    EXPECT_FALSE(options.bySession);
    EXPECT_FALSE(options.byExposure);
    EXPECT_FALSE(options.byTemperature);
    EXPECT_FALSE(options.byCustomTag);
}

TEST(ImageIntegrationLiteProfile, RecommendedDefaultsKeepAdvancedOutputArtifactsOff)
{
    const StackingSettings settings = ImageIntegrationLite::stackingSettings();

    EXPECT_EQ(settings.stackMethod, QStringLiteral("Average"));
    EXPECT_EQ(settings.rejectionMethod, QStringLiteral("Auto"));
    EXPECT_EQ(settings.normalizationMode, NormalizationMode::Global);
    EXPECT_EQ(settings.globalNormalizationMethod, GlobalNormalizationMethod::AdditiveScaling);
    EXPECT_EQ(settings.scaleEstimator, ScaleEstimatorMethod::IKSS);
    EXPECT_FALSE(settings.localNormalization.enabled);
    EXPECT_FALSE(settings.drizzle.enabled);
    EXPECT_FALSE(settings.createQualityMaps);
    EXPECT_FALSE(settings.createRejectionMaps);
    EXPECT_FALSE(settings.solveFinalMasterIfNeeded);
}

TEST(ImageIntegrationLiteProfile, AutoRejectionUsesFullProcessLightTiers)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, makeLightFrames(25, QStringLiteral("lite_auto_rej_l")));

    ImageIntegrationLite::applyDefaults(model);

    const auto& stackingByGroup = model.stackingSettings(FrameType::Light);
    ASSERT_FALSE(stackingByGroup.empty());
    EXPECT_EQ(stackingByGroup.begin()->second.rejectionMethod, QStringLiteral("Linear Fit"));
    EXPECT_EQ(stackingByGroup.begin()->second.requestedRejectionMethod, QStringLiteral("Auto"));
    EXPECT_DOUBLE_EQ(stackingByGroup.begin()->second.sigmaLow, 5.0);
    EXPECT_DOUBLE_EQ(stackingByGroup.begin()->second.sigmaHigh, 4.0);
}

TEST(ImageIntegrationLiteProfile, AutoRejectionUsesCalibrationFrameCounts)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Flat, makeTypedFrames(FrameType::Flat, 10, QStringLiteral("lite_auto_rej_flat"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0));

    ImageIntegrationLite::applyDefaults(model);

    const auto& stackingByGroup = model.stackingSettings(FrameType::Flat);
    ASSERT_FALSE(stackingByGroup.empty());
    EXPECT_EQ(stackingByGroup.begin()->second.rejectionMethod, QStringLiteral("Sigma Clipping"));
    EXPECT_EQ(stackingByGroup.begin()->second.requestedRejectionMethod, QStringLiteral("Auto"));
}

TEST(ImageIntegrationLiteProfile, RecommendedDefaultsAvoidAutomaticPlateSolving)
{
    const RegistrationSettings registration = ImageIntegrationLite::registrationSettings();
    const StackingSettings stacking = ImageIntegrationLite::stackingSettings();

    EXPECT_FALSE(registration.autoSolveUnsolvedFrames);
    EXPECT_FALSE(stacking.solveFinalMasterIfNeeded);
}

TEST(ImageIntegrationLiteProfile, ProcessingOptionsCanEnableFinalMasterPlateSolving)
{
    auto options = ImageIntegrationLite::defaultProcessingOptions();
    options.solveFinalMasterIfNeeded = true;

    const StackingSettings stacking = ImageIntegrationLite::stackingSettings(options);

    EXPECT_TRUE(stacking.solveFinalMasterIfNeeded);
}

TEST(ImageIntegrationLiteProfile, ProcessingOptionsCanEnableLocalNormalizationExplicitly)
{
    auto options = ImageIntegrationLite::defaultProcessingOptions();
    options.normalizationPreset = ImageIntegrationLite::NormalizationPreset::Local;
    options.stackMethod = QStringLiteral("Median");
    options.rejectionMethod = QStringLiteral("None");
    options.weighting = QStringLiteral("Noise");

    const StackingSettings settings = ImageIntegrationLite::stackingSettings(options);

    EXPECT_EQ(settings.stackMethod, QStringLiteral("Median"));
    EXPECT_EQ(settings.rejectionMethod, QStringLiteral("None"));
    EXPECT_EQ(settings.weighting, QStringLiteral("Noise"));
    EXPECT_EQ(settings.normalizationMode, NormalizationMode::Local);
    EXPECT_TRUE(settings.localNormalization.enabled);
}

TEST(ImageIntegrationLiteProfile, ProcessingOptionsCanTuneRejectionParameters)
{
    auto options = ImageIntegrationLite::defaultProcessingOptions();
    options.rejectionMethod = QStringLiteral("Linear Fit");

    StackingSettings settings = ImageIntegrationLite::stackingSettings(options);

    EXPECT_EQ(settings.rejectionMethod, QStringLiteral("Linear Fit"));
    EXPECT_DOUBLE_EQ(settings.sigmaLow, 5.0);
    EXPECT_DOUBLE_EQ(settings.sigmaHigh, 4.0);

    options.rejectionLow = 4.5;
    options.rejectionHigh = 3.5;
    settings = ImageIntegrationLite::stackingSettings(options);

    EXPECT_DOUBLE_EQ(settings.sigmaLow, 4.5);
    EXPECT_DOUBLE_EQ(settings.sigmaHigh, 3.5);
}

TEST(ImageIntegrationLiteProfile, ProcessingOptionsCanEnableCommonDrizzleSubset)
{
    auto options = ImageIntegrationLite::defaultProcessingOptions();
    options.drizzleEnabled = true;
    options.drizzleScale = 3.0;
    options.drizzlePixelFraction = 0.8;

    const StackingSettings settings = ImageIntegrationLite::stackingSettings(options);

    EXPECT_EQ(settings.normalizationMode, NormalizationMode::Global);
    EXPECT_EQ(settings.globalNormalizationMethod, GlobalNormalizationMethod::AdditiveScaling);
    EXPECT_FALSE(settings.localNormalization.enabled);
    EXPECT_TRUE(settings.drizzle.enabled);
    EXPECT_DOUBLE_EQ(settings.drizzle.scale, 3.0);
    EXPECT_DOUBLE_EQ(settings.drizzle.pixelFraction, 0.8);
    EXPECT_EQ(settings.drizzle.kernel, QStringLiteral("Square"));
}

TEST(ImageIntegrationLiteProfile, ProcessingOptionsCanApplyResourceLimits)
{
    auto options = ImageIntegrationLite::defaultProcessingOptions();
    options.limitMemory = true;
    options.maxMemoryMb = 4096;
    options.maxParallelCores = 1;

    const StackingSettings stacking = ImageIntegrationLite::stackingSettings(options);
    const StackingSettings calibrationStacking =
        ImageIntegrationLite::calibrationStackingSettings(FrameType::Flat, options);
    WorkflowManifest manifest;
    ImageIntegrationLite::applyExecutionOptions(manifest, options);

    EXPECT_TRUE(stacking.limitMemory);
    EXPECT_EQ(stacking.maxMemoryMb, 4096);
    EXPECT_TRUE(calibrationStacking.limitMemory);
    EXPECT_EQ(calibrationStacking.maxMemoryMb, 4096);
    EXPECT_EQ(manifest.maxParallelCores, 1);
}

TEST(ImageIntegrationLiteProfile, RegistrationSettingsFollowCommonOptions)
{
    auto options = ImageIntegrationLite::defaultProcessingOptions();
    options.transformation = QStringLiteral("polynomial3");
    options.autoReference = false;

    const RegistrationSettings settings = ImageIntegrationLite::registrationSettings(options);

    EXPECT_EQ(settings.registrationMethod, QStringLiteral("star_matching"));
    EXPECT_EQ(settings.transformation, QStringLiteral("Homography"));
    EXPECT_EQ(settings.transformMode, QStringLiteral("polynomial"));
    EXPECT_EQ(settings.transformOrder, 3);
    EXPECT_TRUE(settings.twoPassRefinementEnabled);
    EXPECT_FALSE(settings.autoReferenceDuringRegistration);
    EXPECT_TRUE(settings.enrichWithGaiaCatalog);
}

TEST(ImageIntegrationLiteProfile, ProcessingOptionsCanEnableAutoCropIntersectionAlignment)
{
    auto options = ImageIntegrationLite::defaultProcessingOptions();
    options.autoCrop = true;

    const RegistrationSettings settings = ImageIntegrationLite::registrationSettings(options);

    EXPECT_EQ(settings.alignmentMode, QStringLiteral("intersection"));
}

TEST(ImageIntegrationLiteProfile, ManifestMetadataLabelsLiteRunsAndRoundTrips)
{
    WorkflowManifest manifest;

    ImageIntegrationLite::applyManifestMetadata(manifest);

    EXPECT_EQ(manifest.processLabel, QStringLiteral("Image Integration Lite"));
    EXPECT_TRUE(manifest.processingProfileId.isEmpty());
    EXPECT_TRUE(manifest.processingProfileLabel.isEmpty());

    const QJsonObject json = manifest.toJson();
    EXPECT_EQ(json.value(QStringLiteral("processLabel")).toString(), QStringLiteral("Image Integration Lite"));
    EXPECT_FALSE(json.contains(QStringLiteral("processingProfile")));

    const WorkflowManifest roundTrip = WorkflowManifest::fromJson(json);
    EXPECT_EQ(roundTrip.processLabel, manifest.processLabel);
    EXPECT_EQ(roundTrip.processingProfileId, manifest.processingProfileId);
    EXPECT_EQ(roundTrip.processingProfileLabel, manifest.processingProfileLabel);
}

TEST(ImageIntegrationLiteProfile, ApplyingDefaultsSetsModelToLitePolicy)
{
    IntegrationDataModel model;

    ImageIntegrationLite::applyDefaults(model);

    EXPECT_EQ(model.lightCalibrationCacheMode(), LightCalibrationCacheMode::InMemory);
    EXPECT_EQ(model.groupingOptions(FrameType::Light), ImageIntegrationLite::sourceGroupingOptions(FrameType::Light));
    EXPECT_EQ(model.lightIntegrationGroupingOptions(), ImageIntegrationLite::lightIntegrationGroupingOptions());
    EXPECT_TRUE(model.calibrationMatchingPolicy().allowFlatSessionFallback);
    EXPECT_TRUE(model.calibrationMatchingPolicy().allowDarkExposureFallback);
    EXPECT_FALSE(model.calibrationMatchingPolicy().subtractBiasFromDark);
}

TEST(ImageIntegrationLiteProfile, SingleCalibrationFrameGroupsAreDetectedAsSuppliedMasters)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Bias, {makeTypedFrame(FrameType::Bias, QStringLiteral("lite_single_bias.fit"))});
    model.addFrames(FrameType::Dark, {makeTypedFrame(FrameType::Dark, QStringLiteral("lite_single_dark_60.fit"),
                                                     QString(), QString(), 60.0)});
    model.addFrames(FrameType::Flat, {makeTypedFrame(FrameType::Flat, QStringLiteral("lite_single_flat_l.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 2.0)});

    ImageIntegrationLite::applyDefaults(model);
    ImageIntegrationLite::autoDetectSingleFrameCalibrationMasters(model);

    ASSERT_EQ(model.biasFrames().size(), 1u);
    ASSERT_EQ(model.darkFrames().size(), 1u);
    ASSERT_EQ(model.flatFrames().size(), 1u);
    EXPECT_TRUE(model.isMasterFrame(model.biasFrames().frames().front().id));
    EXPECT_TRUE(model.isMasterFrame(model.darkFrames().frames().front().id));
    EXPECT_TRUE(model.isMasterFrame(model.flatFrames().frames().front().id));
}

TEST(ImageIntegrationLiteProfile, MixedExposureMasterDarksCalibrateMatchingLights)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    const FrameInfo light60 = makeTypedFrame(FrameType::Light, QStringLiteral("lite_mastermatch_light_60.fit"),
                                             QStringLiteral("L"), QStringLiteral("M42"), 60.0);
    const FrameInfo light90 = makeTypedFrame(FrameType::Light, QStringLiteral("lite_mastermatch_light_90.fit"),
                                             QStringLiteral("L"), QStringLiteral("M42"), 90.0);
    const FrameInfo dark60 =
        makeTypedFrame(FrameType::Dark, QStringLiteral("lite_mastermatch_dark_60.fit"), QString(), QString(), 60.0);
    const FrameInfo dark90 =
        makeTypedFrame(FrameType::Dark, QStringLiteral("lite_mastermatch_dark_90.fit"), QString(), QString(), 90.0);

    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    model.addFrames(FrameType::Light, {light60, light90});
    model.addFrames(FrameType::Dark, {dark60, dark90});

    ImageIntegrationLite::applyDefaults(model);
    ImageIntegrationLite::autoDetectSingleFrameCalibrationMasters(model);

    EXPECT_TRUE(model.isMasterFrame(dark60.id));
    EXPECT_TRUE(model.isMasterFrame(dark90.id));
    ASSERT_EQ(model.darkGroups().size(), 2u);
    ASSERT_EQ(model.lightGroups().size(), 2u);
    ASSERT_EQ(model.integrationGroups().size(), 1u);

    const WorkflowManifest manifest = ManifestBuilder(model).setReprocessExisting(true).build();

    ASSERT_TRUE(manifest.darkStage.has_value());
    ASSERT_EQ(manifest.darkStage->groups.size(), 2u);
    EXPECT_TRUE(std::all_of(manifest.darkStage->groups.begin(), manifest.darkStage->groups.end(),
                            [](const WorkflowManifest::CalibrationStage::Group& group) {
                                return group.skipIfExists && group.existingMaster.has_value();
                            }));

    ASSERT_EQ(manifest.lightsCalibration.size(), 2u);
    for (const auto& params : manifest.lightsCalibration)
    {
        ASSERT_TRUE(params.darkPath.has_value());
        if (params.lightPath == light60.id)
        {
            EXPECT_EQ(params.darkPath->canonicalPath(), dark60.id.canonicalPath());
        }
        else if (params.lightPath == light90.id)
        {
            EXPECT_EQ(params.darkPath->canonicalPath(), dark90.id.canonicalPath());
        }
        else
        {
            FAIL() << "Unexpected light in calibration manifest.";
        }
    }
}

TEST(ImageIntegrationLiteProfile, MixedExposureDarkStacksProduceSeparateMasters)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    model.addFrames(FrameType::Light, {makeTypedFrame(FrameType::Light, QStringLiteral("lite_darkstack_light_60.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 60.0),
                                       makeTypedFrame(FrameType::Light, QStringLiteral("lite_darkstack_light_90.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 90.0)});
    model.addFrames(
        FrameType::Dark,
        {makeTypedFrame(FrameType::Dark, QStringLiteral("lite_darkstack_dark_60a.fit"), QString(), QString(), 60.0),
         makeTypedFrame(FrameType::Dark, QStringLiteral("lite_darkstack_dark_61a.fit"), QString(), QString(), 61.0),
         makeTypedFrame(FrameType::Dark, QStringLiteral("lite_darkstack_dark_90a.fit"), QString(), QString(), 90.0),
         makeTypedFrame(FrameType::Dark, QStringLiteral("lite_darkstack_dark_91a.fit"), QString(), QString(), 91.0)});

    ImageIntegrationLite::applyDefaults(model);
    ImageIntegrationLite::autoDetectSingleFrameCalibrationMasters(model);

    EXPECT_EQ(model.masterCount(FrameType::Dark), 0);
    ASSERT_EQ(model.darkGroups().size(), 2u);

    const WorkflowManifest manifest = ManifestBuilder(model).setReprocessExisting(true).build();

    ASSERT_TRUE(manifest.darkStage.has_value());
    ASSERT_EQ(manifest.darkStage->groups.size(), 2u);
    for (const auto& group : manifest.darkStage->groups)
    {
        EXPECT_FALSE(group.skipIfExists);
        EXPECT_FALSE(group.existingMaster.has_value());
        EXPECT_EQ(group.inputFiles.size(), 2u);
    }
}

TEST(ImageIntegrationLiteProfile, ApplyingDefaultsSetsCalibrationNormalizationByFrameType)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Bias, {makeTypedFrame(FrameType::Bias, QStringLiteral("lite_bias.fit"))});
    model.addFrames(FrameType::Dark, {makeTypedFrame(FrameType::Dark, QStringLiteral("lite_dark.fit"))});
    model.addFrames(FrameType::Flat, {makeTypedFrame(FrameType::Flat, QStringLiteral("lite_flat.fit"))});
    model.addFrames(FrameType::Light, {makeTypedFrame(FrameType::Light, QStringLiteral("lite_light.fit"))});

    ImageIntegrationLite::applyDefaults(model);

    ASSERT_FALSE(model.stackingSettings(FrameType::Bias).empty());
    ASSERT_FALSE(model.stackingSettings(FrameType::Dark).empty());
    ASSERT_FALSE(model.stackingSettings(FrameType::Flat).empty());

    EXPECT_EQ(model.stackingSettings(FrameType::Bias).begin()->second.normalizationMode, NormalizationMode::None);
    EXPECT_EQ(model.stackingSettings(FrameType::Dark).begin()->second.normalizationMode, NormalizationMode::None);
    EXPECT_EQ(model.stackingSettings(FrameType::Flat).begin()->second.normalizationMode, NormalizationMode::Global);
    EXPECT_EQ(model.stackingSettings(FrameType::Flat).begin()->second.globalNormalizationMethod,
              GlobalNormalizationMethod::Multiplicative);
    EXPECT_FALSE(model.stackingSettings(FrameType::Bias).begin()->second.solveFinalMasterIfNeeded);
    EXPECT_FALSE(model.stackingSettings(FrameType::Dark).begin()->second.solveFinalMasterIfNeeded);
    EXPECT_FALSE(model.stackingSettings(FrameType::Flat).begin()->second.solveFinalMasterIfNeeded);
}

TEST(ImageIntegrationLiteProfile, ApplyingDefaultsEnablesMasterFlatNormalizationForLightCalibration)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    model.addFrames(FrameType::Light, {makeTypedFrame(FrameType::Light, QStringLiteral("lite_flatlevel_light_a.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42")),
                                       makeTypedFrame(FrameType::Light, QStringLiteral("lite_flatlevel_light_b.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"))});
    model.addFrames(FrameType::Flat, {makeTypedFrame(FrameType::Flat, QStringLiteral("lite_flatlevel_flat_a.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0),
                                      makeTypedFrame(FrameType::Flat, QStringLiteral("lite_flatlevel_flat_b.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0)});

    ImageIntegrationLite::applyDefaults(model);

    ASSERT_FALSE(model.lightCalibrationSettings().empty());
    for (const auto& [key, settings] : model.lightCalibrationSettings())
    {
        Q_UNUSED(key);
        EXPECT_TRUE(settings.flatAutoLevel);
    }

    const WorkflowManifest manifest =
        ManifestBuilder(model).setReprocessExisting(true).setIncludeRejectionMaps(false).build();

    ASSERT_FALSE(manifest.lightsCalibration.empty());
    for (const auto& params : manifest.lightsCalibration)
    {
        EXPECT_TRUE(params.flatPath.has_value());
        EXPECT_TRUE(params.settings.flatAutoLevel);
    }
}

TEST(ImageIntegrationLiteProfile, FlatsMatchLiteLightsByFilterWithoutObjectOrDate)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    model.addFrames(FrameType::Light, {makeTypedFrame(FrameType::Light, QStringLiteral("lite_match_light_a.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42")),
                                       makeTypedFrame(FrameType::Light, QStringLiteral("lite_match_light_b.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"))});
    model.addFrames(FrameType::Flat, {makeTypedFrame(FrameType::Flat, QStringLiteral("lite_match_flat_a.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel")),
                                      makeTypedFrame(FrameType::Flat, QStringLiteral("lite_match_flat_b.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"))});

    ImageIntegrationLite::applyDefaults(model);

    WorkflowManifest manifest = ManifestBuilder(model).setReprocessExisting(true).build();
    ImageIntegrationLite::applyManifestMetadata(manifest);

    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    ASSERT_EQ(manifest.lightsCalibration.size(), 2u);
    EXPECT_TRUE(manifest.lightsCalibration.front().flatPath.has_value());
}

TEST(ImageIntegrationLiteProfile, DarkFlatFramesCalibrateFlatsOnly)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    model.addFrames(FrameType::Light, {makeTypedFrame(FrameType::Light, QStringLiteral("lite_darkflat_light_a.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 120.0),
                                       makeTypedFrame(FrameType::Light, QStringLiteral("lite_darkflat_light_b.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 120.0)});
    model.addFrames(FrameType::Flat, {makeTypedFrame(FrameType::Flat, QStringLiteral("lite_darkflat_flat_a.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0),
                                      makeTypedFrame(FrameType::Flat, QStringLiteral("lite_darkflat_flat_b.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0)});
    model.addFrames(FrameType::Dark,
                    {makeTypedFrame(FrameType::Dark, QStringLiteral("lite_darkflat_dark_a.fit"), QString(), QString(),
                                    3.0, ImageIntegrationLite::darkFlatCustomTag()),
                     makeTypedFrame(FrameType::Dark, QStringLiteral("lite_darkflat_dark_b.fit"), QString(), QString(),
                                    3.0, ImageIntegrationLite::darkFlatCustomTag())});

    ImageIntegrationLite::applyDefaults(model);

    WorkflowManifest manifest = ManifestBuilder(model).setReprocessExisting(true).build();
    ImageIntegrationLite::applyDarkFlatCalibration(manifest, model, true);

    ASSERT_TRUE(manifest.darkStage.has_value());
    ASSERT_EQ(manifest.darkStage->groups.size(), 1u);
    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    EXPECT_TRUE(manifest.flatStage->groups.front().darkMaster.has_value());

    ASSERT_EQ(manifest.lightsCalibration.size(), 2u);
    for (const auto& params : manifest.lightsCalibration)
    {
        EXPECT_FALSE(params.darkPath.has_value());
        EXPECT_TRUE(params.flatPath.has_value());
    }
}

TEST(ImageIntegrationLiteProfile, NormalDarkFramesDoNotCalibrateFlatsInLite)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    model.addFrames(FrameType::Light, {makeTypedFrame(FrameType::Light, QStringLiteral("lite_normaldark_light_a.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 120.0),
                                       makeTypedFrame(FrameType::Light, QStringLiteral("lite_normaldark_light_b.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 120.0)});
    model.addFrames(FrameType::Flat, {makeTypedFrame(FrameType::Flat, QStringLiteral("lite_normaldark_flat_a.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0),
                                      makeTypedFrame(FrameType::Flat, QStringLiteral("lite_normaldark_flat_b.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0)});
    model.addFrames(
        FrameType::Dark,
        {makeTypedFrame(FrameType::Dark, QStringLiteral("lite_normaldark_dark_a.fit"), QString(), QString(), 120.0),
         makeTypedFrame(FrameType::Dark, QStringLiteral("lite_normaldark_dark_b.fit"), QString(), QString(), 120.0)});

    ImageIntegrationLite::applyDefaults(model);

    WorkflowManifest manifest =
        ManifestBuilder(model).setReprocessExisting(true).setIncludeRejectionMaps(false).build();
    ImageIntegrationLite::applyDarkFlatCalibration(manifest, model, true);

    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    EXPECT_FALSE(manifest.flatStage->groups.front().darkMaster.has_value());

    const bool lightsCalibrationHasDark = std::any_of(
        manifest.lightsCalibration.begin(), manifest.lightsCalibration.end(),
        [](const WorkflowManifest::LightsCalibrationParams& params) { return params.darkPath.has_value(); });
    EXPECT_TRUE(lightsCalibrationHasDark);
}

namespace
{
/// A Lite project of two lights, two flats, two dark flats and two bias frames, each kind at its exposure; with
/// `flatDarksFiledAsDarks` the flats' darks are filed as darks rather than dark flats, beside darks for the lights.
void addDarkFlatProject(IntegrationDataModel& model, const QString& prefix, const bool flatDarksFiledAsDarks)
{
    model.addFrames(FrameType::Light, {makeTypedFrame(FrameType::Light, prefix + QStringLiteral("_light_a.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 120.0),
                                       makeTypedFrame(FrameType::Light, prefix + QStringLiteral("_light_b.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 120.0)});
    model.addFrames(FrameType::Flat, {makeTypedFrame(FrameType::Flat, prefix + QStringLiteral("_flat_a.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0),
                                      makeTypedFrame(FrameType::Flat, prefix + QStringLiteral("_flat_b.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0)});
    const QString flatDarkTag = flatDarksFiledAsDarks ? QString() : ImageIntegrationLite::darkFlatCustomTag();
    model.addFrames(
        FrameType::Dark,
        {makeTypedFrame(FrameType::Dark, prefix + QStringLiteral("_flat_dark_a.fit"), QString(), QString(), 3.0,
                        flatDarkTag),
         makeTypedFrame(FrameType::Dark, prefix + QStringLiteral("_flat_dark_b.fit"), QString(), QString(), 3.0,
                        flatDarkTag),
         makeTypedFrame(FrameType::Dark, prefix + QStringLiteral("_light_dark_a.fit"), QString(), QString(), 120.0),
         makeTypedFrame(FrameType::Dark, prefix + QStringLiteral("_light_dark_b.fit"), QString(), QString(), 120.0)});
    model.addFrames(
        FrameType::Bias,
        {makeTypedFrame(FrameType::Bias, prefix + QStringLiteral("_bias_a.fit"), QString(), QString(), 0.0),
         makeTypedFrame(FrameType::Bias, prefix + QStringLiteral("_bias_b.fit"), QString(), QString(), 0.0)});
}

[[nodiscard]] WorkflowManifest buildLiteManifest(const IntegrationDataModel& model, const bool reprocessExisting)
{
    WorkflowManifest manifest = ManifestBuilder(model).setReprocessExisting(reprocessExisting).build();
    ImageIntegrationLite::applyDarkFlatCalibration(manifest, model, reprocessExisting);
    return manifest;
}
} // namespace

// A master dark flat holds the bias, as every master dark does, so a flat calibrated with one takes the bias no more: a
// flat given both lost the bias pedestal twice and over-corrected every light it flat-fielded.
TEST(ImageIntegrationLiteProfile, AFlatTakesItsDarkFlatAloneAndNotTheBias)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());
    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    addDarkFlatProject(model, QStringLiteral("lite_alone"), false);
    ImageIntegrationLite::applyDefaults(model);

    const WorkflowManifest manifest = buildLiteManifest(model, true);
    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    const auto& flat = manifest.flatStage->groups.front();
    EXPECT_TRUE(flat.darkMaster.has_value()) << "the flat takes its dark flat";
    EXPECT_FALSE(flat.biasMaster.has_value()) << "the dark flat holds the bias";
}

// Lite calibrates a flat with a dark flat, never with a light's dark: where the builder matched the flats to darks at
// their exposure filed as darks, Lite lets that dark go, and the flat takes the bias in its place - stacked for it, as
// no light needs it - rather than neither.
TEST(ImageIntegrationLiteProfile, AFlatWhoseLightDarkLiteLetsGoTakesTheBias)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());
    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    addDarkFlatProject(model, QStringLiteral("lite_filed"), true);
    ImageIntegrationLite::applyDefaults(model);

    const WorkflowManifest manifest = buildLiteManifest(model, true);
    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    const auto& flat = manifest.flatStage->groups.front();
    EXPECT_FALSE(flat.darkMaster.has_value()) << "Lite does not calibrate a flat with a light's dark";
    ASSERT_TRUE(flat.biasMaster.has_value()) << "a flat no dark is applied to takes the bias";
    ASSERT_TRUE(manifest.biasStage.has_value());
    const bool biasStacked =
        std::any_of(manifest.biasStage->groups.begin(), manifest.biasStage->groups.end(), [&](const auto& group) {
            return FrameId(QDir(outputDir.path()).filePath(group.outputFilename)) == *flat.biasMaster;
        });
    EXPECT_TRUE(biasStacked) << "the bias the flat takes is stacked";
    const qsizetype bias = manifest.stageSequence.indexOf(QStringLiteral("Bias"));
    ASSERT_GE(bias, 0);
    EXPECT_LT(bias, manifest.stageSequence.indexOf(QStringLiteral("Flat")));
    for (const auto& light : manifest.lightsCalibration)
    {
        EXPECT_TRUE(light.darkPath.has_value()) << "a light takes its dark";
        EXPECT_FALSE(light.biasPath.has_value()) << "standard mode: the light's dark holds the bias";
    }
}

// A flat whose generated master exists names the masters it was made with, as on the run that made it: a master's
// identity holds them, so naming others would make the next run re-stack the flat with the bias in place of its dark
// flat.
TEST(ImageIntegrationLiteProfile, AFlatWhoseMasterExistsNamesTheMastersItWasMadeWith)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());
    IntegrationDataModel model;
    model.setOutputDirectory(outputDir.path());
    addDarkFlatProject(model, QStringLiteral("lite_kept"), false);
    ImageIntegrationLite::applyDefaults(model);

    const WorkflowManifest first = buildLiteManifest(model, true);
    ASSERT_TRUE(first.flatStage.has_value());
    ASSERT_EQ(first.flatStage->groups.size(), 1u);
    ASSERT_TRUE(first.flatStage->groups.front().darkMaster.has_value());
    QFile made(QDir(outputDir.path()).filePath(first.flatStage->groups.front().outputFilename));
    ASSERT_TRUE(made.open(QIODevice::WriteOnly));
    made.close();

    const WorkflowManifest next = buildLiteManifest(model, false);
    ASSERT_TRUE(next.flatStage.has_value());
    ASSERT_EQ(next.flatStage->groups.size(), 1u);
    const auto& flat = next.flatStage->groups.front();
    EXPECT_TRUE(flat.skipIfExists);
    ASSERT_TRUE(flat.darkMaster.has_value()) << "the flat keeps its dark flat";
    EXPECT_EQ(*flat.darkMaster, *first.flatStage->groups.front().darkMaster);
    EXPECT_FALSE(flat.biasMaster.has_value());
}

TEST(ImageIntegrationLiteProfile, DatasetSummaryCountsFramesAndFilters)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeLightFrame(QStringLiteral("light_l_1.fit"), QStringLiteral("L")),
                                       makeLightFrame(QStringLiteral("light_l_2.fit"), QStringLiteral("L")),
                                       makeLightFrame(QStringLiteral("light_ha.fit"), QStringLiteral("Ha"))});

    const ImageIntegrationLite::DatasetSummary summary = ImageIntegrationLite::summarizeDataset(model);

    EXPECT_EQ(summary.lightFrames, 3);
    EXPECT_EQ(summary.filters, QStringList({QStringLiteral("Ha"), QStringLiteral("L")}));
    EXPECT_EQ(summary.objects, QStringList({QStringLiteral("M42")}));
    EXPECT_DOUBLE_EQ(summary.totalLightExposureSeconds, 180.0);
}

TEST(ImageIntegrationLiteProfile, ReadinessBlocksMultipleObjects)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeLightFrame(QStringLiteral("m42.fit"), QStringLiteral("L"), QStringLiteral("M42")),
                     makeLightFrame(QStringLiteral("m31.fit"), QStringLiteral("L"), QStringLiteral("M31"))});

    const WorkflowValidationResult result = ImageIntegrationLite::validateReadiness(model);

    EXPECT_TRUE(result.hasErrors());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(result.issues.back().fixTarget, FixTarget::LightFrames);
}

TEST(ImageIntegrationLiteProfile, ReadinessHandlesNormalDarkFrames)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light, {makeTypedFrame(FrameType::Light, QStringLiteral("lite_ready_light.fit"),
                                                      QStringLiteral("L"), QStringLiteral("M42"), 120.0)});
    model.addFrames(FrameType::Dark, {makeTypedFrame(FrameType::Dark, QStringLiteral("lite_ready_dark.fit"), QString(),
                                                     QString(), 120.0)});

    ImageIntegrationLite::applyDefaults(model);

    const WorkflowValidationResult result = ImageIntegrationLite::validateReadiness(model);

    EXPECT_FALSE(result.hasErrors());
}
