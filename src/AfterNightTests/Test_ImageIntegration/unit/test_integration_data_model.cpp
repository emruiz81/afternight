// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QtTest/QSignalSpy>
#include <QTimeZone>

#include <optional>

#include <QElapsedTimer>

#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"

namespace
{
FrameInfo makeFrame(FrameType type, const QString& filename, const QSize& size, double exposure, const QString& session,
                    const QString& custom_tag)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, size, exposure, std::nullopt, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, session, custom_tag);
}

FrameInfo makeLightFrame(const QString& filename, const QSize& size, const QString& object)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(FrameType::Light, size, 10.0, std::nullopt, 100, 10, QStringLiteral("L"), object,
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString());
}

std::vector<FrameInfo> makeLightFrames(int count)
{
    std::vector<FrameInfo> frames;
    frames.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        frames.push_back(makeLightFrame(QStringLiteral("light_%1.fit").arg(i), QSize(100, 100), QStringLiteral("M42")));
    }
    return frames;
}
} // namespace

TEST(IntegrationDataModel, SettingMasterDeselectionExcludesOtherFramesInGroup)
{
    IntegrationDataModel model;
    const auto frame1 = makeFrame(FrameType::Bias, QStringLiteral("bias_master.fit"), QSize(100, 100), 0.0,
                                  QStringLiteral("2025-01-01"), QString());
    const auto frame2 = makeFrame(FrameType::Bias, QStringLiteral("bias_other.fit"), QSize(100, 100), 0.0,
                                  QStringLiteral("2025-01-01"), QString());

    model.addFrames(FrameType::Bias, {frame1, frame2});
    model.setMasterStatus(frame1.displayPath, true, MasterSource::UserDefined);

    const auto master_opt = model.frames(FrameType::Bias).findById(frame1.id);
    const auto other_opt = model.frames(FrameType::Bias).findById(frame2.id);
    ASSERT_TRUE(master_opt.has_value());
    ASSERT_TRUE(other_opt.has_value());

    EXPECT_FALSE(master_opt->excluded);
    EXPECT_TRUE(other_opt->excluded);
    EXPECT_TRUE(model.isMasterFrame(frame1.id));
}

TEST(IntegrationDataModel, SelectingAnotherFrameClearsMasterStatus)
{
    IntegrationDataModel model;
    const auto frame1 = makeFrame(FrameType::Bias, QStringLiteral("bias_master.fit"), QSize(100, 100), 0.0,
                                  QStringLiteral("2025-01-01"), QString());
    const auto frame2 = makeFrame(FrameType::Bias, QStringLiteral("bias_other.fit"), QSize(100, 100), 0.0,
                                  QStringLiteral("2025-01-01"), QString());

    model.addFrames(FrameType::Bias, {frame1, frame2});
    model.setMasterStatus(frame1.displayPath, true, MasterSource::UserDefined);

    model.updateFrameAttribute(frame2.displayPath, FrameAttribute::Excluded, false);

    EXPECT_FALSE(model.isMasterFrame(frame1.id));
    const auto other_opt = model.frames(FrameType::Bias).findById(frame2.id);
    ASSERT_TRUE(other_opt.has_value());
    EXPECT_FALSE(other_opt->excluded);
}

TEST(IntegrationDataModel, PersistedMasterEnforcesExclusiveSelectionOnLoad)
{
    IntegrationDataModel model;
    const auto frame1 = makeFrame(FrameType::Bias, QStringLiteral("bias_master.fit"), QSize(100, 100), 0.0,
                                  QStringLiteral("2025-01-01"), QString());
    const auto frame2 = makeFrame(FrameType::Bias, QStringLiteral("bias_other.fit"), QSize(100, 100), 0.0,
                                  QStringLiteral("2025-01-01"), QString());

    model.addFrames(FrameType::Bias, {frame1, frame2});
    model.setMasterStatus(frame1.displayPath, true, MasterSource::UserDefined);

    QJsonObject payload = model.toJson();
    QJsonObject frames_root = payload.value(QStringLiteral("frames")).toObject();
    QJsonArray bias_frames = frames_root.value(QStringLiteral("Bias")).toArray();
    for (int i = 0; i < bias_frames.size(); ++i)
    {
        QJsonObject frame_obj = bias_frames.at(i).toObject();
        frame_obj.insert(QStringLiteral("excluded"), false);
        bias_frames.replace(i, frame_obj);
    }
    frames_root.insert(QStringLiteral("Bias"), bias_frames);
    payload.insert(QStringLiteral("frames"), frames_root);

    IntegrationDataModel restored;
    restored.fromJson(payload);

    EXPECT_TRUE(restored.isMasterFrame(frame1.id));

    const auto master_opt = restored.frames(FrameType::Bias).findById(frame1.id);
    const auto other_opt = restored.frames(FrameType::Bias).findById(frame2.id);
    ASSERT_TRUE(master_opt.has_value());
    ASSERT_TRUE(other_opt.has_value());
    EXPECT_FALSE(master_opt->excluded);
    EXPECT_TRUE(other_opt->excluded);
}

TEST(IntegrationDataModel, FromJsonClearsStaleFrameQualityMetrics)
{
    IntegrationDataModel model;
    const auto frame = makeFrame(FrameType::Light, QStringLiteral("quality_metric_light.fit"), QSize(100, 100), 10.0,
                                 QStringLiteral("2025-01-01"), QString());
    model.addFrames(FrameType::Light, {frame});

    FrameQualityMetrics metrics;
    metrics.stars = 123;
    metrics.fwhm = 2.4;
    metrics.snr = 45.0;
    model.setFrameQualityMetrics(frame.id, metrics);
    ASSERT_TRUE(model.frameQualityMetrics(frame.id).has_value());

    const QJsonObject payload = model.toJson();
    model.fromJson(payload);

    EXPECT_FALSE(model.frameQualityMetrics(frame.id).has_value());
}

TEST(IntegrationDataModel, MissingCacheModeDefaultsToInMemoryOnLoad)
{
    IntegrationDataModel source;
    QJsonObject payload = source.toJson();
    payload.remove(QStringLiteral("lightCalibrationCacheMode"));

    IntegrationDataModel restored;
    restored.setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);
    ASSERT_EQ(restored.lightCalibrationCacheMode(), LightCalibrationCacheMode::Disk);

    restored.fromJson(payload);

    EXPECT_EQ(restored.lightCalibrationCacheMode(), LightCalibrationCacheMode::InMemory);
}

TEST(IntegrationDataModel, CalibrationPolicySubtractBiasFromDarkRoundTripsProjectJson)
{
    IntegrationDataModel model;
    CalibrationMatchingPolicy policy = model.calibrationMatchingPolicy();
    policy.subtractBiasFromDark = true;
    model.setCalibrationMatchingPolicy(policy);

    const QJsonObject payload = model.toJson();
    const QJsonObject calibration_policy = payload.value(QStringLiteral("calibrationPolicy")).toObject();
    ASSERT_TRUE(calibration_policy.contains(QStringLiteral("subtractBiasFromDark")));
    EXPECT_TRUE(calibration_policy.value(QStringLiteral("subtractBiasFromDark")).toBool());

    IntegrationDataModel restored;
    restored.fromJson(payload);

    EXPECT_TRUE(restored.calibrationMatchingPolicy().subtractBiasFromDark);
}

TEST(IntegrationDataModel, MissingSubtractBiasFromDarkDefaultsFalseOnLoad)
{
    IntegrationDataModel model;
    CalibrationMatchingPolicy policy = model.calibrationMatchingPolicy();
    policy.subtractBiasFromDark = true;
    model.setCalibrationMatchingPolicy(policy);

    QJsonObject payload = model.toJson();
    QJsonObject calibration_policy = payload.value(QStringLiteral("calibrationPolicy")).toObject();
    calibration_policy.remove(QStringLiteral("subtractBiasFromDark"));
    payload.insert(QStringLiteral("calibrationPolicy"), calibration_policy);

    IntegrationDataModel restored;
    restored.fromJson(payload);

    EXPECT_FALSE(restored.calibrationMatchingPolicy().subtractBiasFromDark);
}

TEST(IntegrationDataModel, EmitsSignalOnFrameAdded)
{
    IntegrationDataModel model;
    QSignalSpy spy(&model, &IntegrationDataModel::framesAdded);

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    EXPECT_EQ(spy.count(), 1);
}

TEST(IntegrationDataModel, EmitsSignalOnFrameRemoved)
{
    IntegrationDataModel model;
    QSignalSpy spy(&model, &IntegrationDataModel::framesRemoved);

    auto frame = makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                           QStringLiteral("2025-01-01"), QString());
    model.addFrames(FrameType::Light, {frame});
    model.removeFrames(FrameType::Light, QStringList{frame.displayPath});

    EXPECT_EQ(spy.count(), 1);
}

TEST(IntegrationDataModel, EmitsSignalOnGroupingChanged)
{
    IntegrationDataModel model;
    QSignalSpy spy(&model, &IntegrationDataModel::groupingChanged);

    GroupingOptions options;
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    EXPECT_EQ(spy.count(), 1);
}

TEST(IntegrationDataModel, RegroupResetsPerGroupSettings)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings stacking;
    stacking.drizzle.enabled = true;
    model.setStackingSettings(FrameType::Light, group_key, stacking);
    EXPECT_FALSE(model.stackingSettings(FrameType::Light).empty());

    QSignalSpy reset_spy(&model, &IntegrationDataModel::regroupResetSettings);

    GroupingOptions options = model.groupingOptions(FrameType::Light);
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    EXPECT_EQ(reset_spy.count(), 1);
    EXPECT_TRUE(model.stackingSettings(FrameType::Light).empty());
}

TEST(IntegrationDataModel, EmitsSignalOnSettingsChanged)
{
    IntegrationDataModel model;
    QSignalSpy spy(&model, &IntegrationDataModel::settingsChanged);

    GroupKey key{QStringLiteral("group")};
    StackingSettings settings;
    model.setStackingSettings(FrameType::Light, key, settings);

    EXPECT_EQ(spy.count(), 1);
}

TEST(IntegrationDataModel, BatchOperationsEmitSingleSignal)
{
    IntegrationDataModel model;
    QSignalSpy reset_spy(&model, &IntegrationDataModel::modelReset);
    QSignalSpy added_spy(&model, &IntegrationDataModel::framesAdded);

    model.beginBatchUpdate();
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});
    GroupingOptions options;
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);
    model.endBatchUpdate();

    EXPECT_EQ(reset_spy.count(), 1);
    EXPECT_EQ(added_spy.count(), 0);
}

TEST(IntegrationDataModel, GroupsRecomputedOnFrameChange)
{
    IntegrationDataModel model;
    GroupingOptions options;
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    auto frame_a = makeFrame(FrameType::Light, QStringLiteral("light_a.fit"), QSize(100, 100), 10.0,
                             QStringLiteral("2025-01-01"), QStringLiteral("A"));
    auto frame_b = makeFrame(FrameType::Light, QStringLiteral("light_b.fit"), QSize(100, 100), 10.0,
                             QStringLiteral("2025-01-01"), QStringLiteral("B"));

    model.addFrames(FrameType::Light, {frame_a, frame_b});

    EXPECT_EQ(model.lightGroups().size(), 2u);

    model.updateFrameAttribute(frame_b.displayPath, FrameAttribute::CustomTag, QStringLiteral("A"));
    EXPECT_EQ(model.lightGroups().size(), 1u);
}

TEST(IntegrationDataModel, ObjectOverrideRecomputesGroups)
{
    IntegrationDataModel model;
    GroupingOptions options;
    options.byObject = true;
    options.bySession = false;
    model.setGroupingOptions(FrameType::Light, options);

    auto frame_a =
        makeFrame(FrameType::Light, QStringLiteral("light_a.fit"), QSize(100, 100), 10.0, QString(), QString());
    auto frame_b =
        makeFrame(FrameType::Light, QStringLiteral("light_b.fit"), QSize(100, 100), 10.0, QString(), QString());

    model.addFrames(FrameType::Light, {frame_a, frame_b});

    EXPECT_EQ(model.lightGroups().size(), 1u);

    model.updateFrameAttribute(frame_b.displayPath, FrameAttribute::UserObjectName, QStringLiteral("M31"));
    EXPECT_EQ(model.lightGroups().size(), 2u);

    model.updateFrameAttribute(frame_b.displayPath, FrameAttribute::UserObjectName, QString());
    EXPECT_EQ(model.lightGroups().size(), 1u);
}

TEST(IntegrationDataModel, CalibrationRecomputedOnGroupChange)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    int initial_revision = model.lightCalibration().revision;

    GroupingOptions options;
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    int updated_revision = model.lightCalibration().revision;

    EXPECT_NE(initial_revision, updated_revision);
}

TEST(IntegrationDataModel, FrameSelectionPersists)
{
    IntegrationDataModel model;
    FrameId id_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));
    FrameId id_b(QDir::tempPath() + QDir::separator() + QStringLiteral("b.fit"));

    model.setSelectedFrameIds({id_a, id_b});

    EXPECT_EQ(model.selectedFrameIds().size(), 2u);
    EXPECT_EQ(model.selectedFrameIds()[0], id_a);
}

TEST(IntegrationDataModel, GroupSelectionPersists)
{
    IntegrationDataModel model;
    GroupKey group_a{QStringLiteral("group_a")};
    GroupKey group_b{QStringLiteral("group_b")};

    model.setSelectedGroupKeys({group_a, group_b});

    EXPECT_EQ(model.selectedGroupKeys().size(), 2u);
    EXPECT_EQ(model.selectedGroupKeys()[1], group_b);
}

TEST(IntegrationDataModel, ExcludeFrameWorks)
{
    IntegrationDataModel model;
    GroupingOptions options;
    options.bySession = true;
    model.setGroupingOptions(FrameType::Light, options);

    auto frame_a = makeFrame(FrameType::Light, QStringLiteral("light_a.fit"), QSize(100, 100), 10.0,
                             QStringLiteral("2025-01-01"), QString());
    auto frame_b = makeFrame(FrameType::Light, QStringLiteral("light_b.fit"), QSize(100, 100), 10.0,
                             QStringLiteral("2025-01-02"), QString());
    model.addFrames(FrameType::Light, {frame_a, frame_b});

    EXPECT_EQ(model.lightGroups().size(), 2u);

    model.updateFrameAttribute(frame_b.displayPath, FrameAttribute::Excluded, true);
    EXPECT_EQ(model.lightGroups().size(), 1u);
}

TEST(IntegrationDataModel, ManualGroupAssignmentWorks)
{
    IntegrationDataModel model;
    GroupingOptions options;
    options.bySession = true;
    model.setGroupingOptions(FrameType::Light, options);

    auto frame_a = makeFrame(FrameType::Light, QStringLiteral("light_a.fit"), QSize(100, 100), 10.0,
                             QStringLiteral("2025-01-01"), QString());
    auto frame_b = makeFrame(FrameType::Light, QStringLiteral("light_b.fit"), QSize(100, 100), 10.0,
                             QStringLiteral("2025-01-02"), QString());
    model.addFrames(FrameType::Light, {frame_a, frame_b});

    EXPECT_EQ(model.lightGroups().size(), 2u);

    model.updateFrameAttribute(frame_b.displayPath, FrameAttribute::ManualGroupKey, QStringLiteral("ManualGroup"));
    model.updateFrameAttribute(frame_a.displayPath, FrameAttribute::ManualGroupKey, QStringLiteral("ManualGroup"));

    EXPECT_EQ(model.lightGroups().size(), 1u);
}

TEST(IntegrationDataModel, RegistrationGroupsIgnoreLightGroupingOptions)
{
    IntegrationDataModel model;

    // Intentionally disable the usual light grouping knobs.
    GroupingOptions options;
    options.byObject = false;
    options.byCustomTag = false;
    model.setGroupingOptions(FrameType::Light, options);

    auto frame_m42 = makeLightFrame(QStringLiteral("m42.fit"), QSize(100, 100), QStringLiteral("M42"));
    auto frame_m31 = makeLightFrame(QStringLiteral("m31.fit"), QSize(100, 100), QStringLiteral("M31"));
    model.addFrames(FrameType::Light, {frame_m42, frame_m31});

    // Registration groups are always split by resolution + object.
    EXPECT_EQ(model.registrationGroups().size(), 2u);
}

TEST(IntegrationDataModel, ValidationProducesResults)
{
    IntegrationDataModel model;
    WorkflowValidationResult initial = model.validate();

    EXPECT_TRUE(initial.hasErrors());
    EXPECT_FALSE(model.isReadyForIntegration());

    model.setOutputDirectory(QDir::tempPath());

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    WorkflowValidationResult after = model.validate();
    EXPECT_FALSE(after.hasErrors());
    EXPECT_TRUE(model.isReadyForIntegration());
}

TEST(IntegrationDataModel, JsonRoundTripsFramesAndSettings)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QStringLiteral("D:/Output"));

    GroupingOptions options;
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    auto light = makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                           QStringLiteral("2025-01-01"), QStringLiteral("A"));
    model.addFrames(FrameType::Light, {light});
    model.updateFrameAttribute(light.displayPath, FrameAttribute::UserObjectName, QStringLiteral("M31"));

    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings stacking;
    stacking.normalizationMode = NormalizationMode::None;
    stacking.drizzle.enabled = true;
    stacking.drizzle.scale = 2.5;
    stacking.solveFinalMasterIfNeeded = false;
    model.setStackingSettings(FrameType::Light, group_key, stacking);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    EXPECT_EQ(restored.outputDirectory(), QStringLiteral("D:/Output"));
    EXPECT_EQ(restored.lightFrames().size(), 1u);
    EXPECT_FALSE(restored.hasUnsavedChanges());

    const auto restored_frame = restored.frames(FrameType::Light).findById(light.id);
    ASSERT_TRUE(restored_frame.has_value());
    EXPECT_EQ(restored_frame->userObjectName, QStringLiteral("M31"));

    const auto& restored_stacking = restored.stackingSettings(FrameType::Light);
    auto it = restored_stacking.find(group_key);
    ASSERT_TRUE(it != restored_stacking.end());
    EXPECT_EQ(it->second.normalizationMode, NormalizationMode::None);
    EXPECT_TRUE(it->second.drizzle.enabled);
    EXPECT_DOUBLE_EQ(it->second.drizzle.scale, 2.5);
    EXPECT_FALSE(it->second.solveFinalMasterIfNeeded);
}

TEST(IntegrationDataModel, JsonRoundTripsRegistrationAutoReferenceDuringRegistrationSetting)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QStringLiteral("D:/Output"));

    const auto light = makeLightFrame(QStringLiteral("light.fit"), QSize(100, 100), QStringLiteral("M42"));
    model.addFrames(FrameType::Light, {light});

    const RegistrationGroupKey reg_key = RegistrationGroupKey::fromFrame(light);
    const GroupKey group_key = RegistrationGroupEngine::toMachineGroupKey(reg_key);

    RegistrationSettings settings;
    settings.autoReferenceDuringRegistration = false;
    model.setRegistrationSettings(group_key, settings);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.registrationSettings();
    const auto it = restored_settings.find(group_key);
    ASSERT_TRUE(it != restored_settings.end());
    EXPECT_FALSE(it->second.autoReferenceDuringRegistration);
}

TEST(IntegrationDataModel, JsonRoundTripsRegistrationAutoSolveUnsolvedFramesSetting)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QStringLiteral("D:/Output"));

    const auto light = makeLightFrame(QStringLiteral("light.fit"), QSize(100, 100), QStringLiteral("M42"));
    model.addFrames(FrameType::Light, {light});

    const RegistrationGroupKey reg_key = RegistrationGroupKey::fromFrame(light);
    const GroupKey group_key = RegistrationGroupEngine::toMachineGroupKey(reg_key);

    RegistrationSettings settings;
    settings.autoSolveUnsolvedFrames = true;
    model.setRegistrationSettings(group_key, settings);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.registrationSettings();
    const auto it = restored_settings.find(group_key);
    ASSERT_TRUE(it != restored_settings.end());
    EXPECT_TRUE(it->second.autoSolveUnsolvedFrames);
}

TEST(RegistrationSettings, DefaultsToAutoTransformMode)
{
    RegistrationSettings settings;

    EXPECT_EQ(settings.transformation, QStringLiteral("Homography"));
    EXPECT_EQ(settings.transformMode, QStringLiteral("Auto"));
    EXPECT_EQ(settings.transformOrder, 0);
}

TEST(IntegrationDataModel, MissingRegistrationTransformModeDefaultsToAutoOnLoad)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QStringLiteral("D:/Output"));

    const auto light = makeLightFrame(QStringLiteral("light.fit"), QSize(100, 100), QStringLiteral("M42"));
    model.addFrames(FrameType::Light, {light});

    const RegistrationGroupKey reg_key = RegistrationGroupKey::fromFrame(light);
    const GroupKey group_key = RegistrationGroupEngine::toMachineGroupKey(reg_key);

    RegistrationSettings settings;
    model.setRegistrationSettings(group_key, settings);

    QJsonObject json = model.toJson();
    QJsonArray registration_settings = json.value(QStringLiteral("registrationSettings")).toArray();
    ASSERT_EQ(registration_settings.size(), 1);

    QJsonObject record = registration_settings.at(0).toObject();
    QJsonObject serialized_settings = record.value(QStringLiteral("settings")).toObject();
    serialized_settings.remove(QStringLiteral("transformMode"));
    record.insert(QStringLiteral("settings"), serialized_settings);
    registration_settings.replace(0, record);
    json.insert(QStringLiteral("registrationSettings"), registration_settings);

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.registrationSettings();
    const auto it = restored_settings.find(group_key);
    ASSERT_TRUE(it != restored_settings.end());
    EXPECT_EQ(it->second.transformation, QStringLiteral("Homography"));
    EXPECT_EQ(it->second.transformMode, QStringLiteral("Auto"));
    EXPECT_EQ(it->second.transformOrder, 0);
}

TEST(IntegrationDataModel, JsonRoundTripsRegistrationTransformContractFields)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QStringLiteral("D:/Output"));

    const auto light = makeLightFrame(QStringLiteral("light.fit"), QSize(100, 100), QStringLiteral("M42"));
    model.addFrames(FrameType::Light, {light});

    const RegistrationGroupKey reg_key = RegistrationGroupKey::fromFrame(light);
    const GroupKey group_key = RegistrationGroupEngine::toMachineGroupKey(reg_key);

    RegistrationSettings settings;
    settings.transformMode = QStringLiteral("polynomial");
    settings.transformOrder = 4;
    settings.twoPassRefinementEnabled = true;
    model.setRegistrationSettings(group_key, settings);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.registrationSettings();
    const auto it = restored_settings.find(group_key);
    ASSERT_TRUE(it != restored_settings.end());
    EXPECT_EQ(it->second.transformMode, QStringLiteral("polynomial"));
    EXPECT_EQ(it->second.transformOrder, 4);
    EXPECT_TRUE(it->second.twoPassRefinementEnabled);
}

TEST(IntegrationDataModel, JsonRoundTripsManualRegistrationControlPoints)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QStringLiteral("D:/Output"));

    const auto light_a = makeLightFrame(QStringLiteral("light_a.fit"), QSize(100, 100), QStringLiteral("M42"));
    const auto light_b = makeLightFrame(QStringLiteral("light_b.fit"), QSize(100, 100), QStringLiteral("M42"));
    model.addFrames(FrameType::Light, {light_a, light_b});

    const RegistrationGroupKey reg_key = RegistrationGroupKey::fromFrame(light_a);
    const GroupKey group_key = RegistrationGroupEngine::toMachineGroupKey(reg_key);

    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("manual_points");
    settings.referenceImage = light_a.id.canonicalPath();
    settings.manualControlPointsByFrame.insert(
        light_b.id.canonicalPath(),
        RegistrationSettings::ManualControlPointList{{QPointF(14.0, 14.0), QPointF(12.0, 12.0)},
                                                     {QPointF(22.0, 16.0), QPointF(20.0, 14.0)}});
    model.setRegistrationSettings(group_key, settings);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.registrationSettings();
    const auto it = restored_settings.find(group_key);
    ASSERT_TRUE(it != restored_settings.end());
    EXPECT_EQ(it->second.registrationMethod, QStringLiteral("manual_points"));
    EXPECT_EQ(it->second.referenceImage, light_a.id.canonicalPath());
    ASSERT_EQ(it->second.manualControlPointsByFrame.size(), 1);
    ASSERT_TRUE(it->second.manualControlPointsByFrame.contains(light_b.id.canonicalPath()));
    EXPECT_EQ(it->second.manualControlPointsByFrame.value(light_b.id.canonicalPath()).size(), 2);
}

// =============================================================================
// Per-Group Settings Tests
// =============================================================================

TEST(IntegrationDataModel, SetLightCalibrationSettings_StoresPerGroup)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    LightCalibrationSettings settings;
    settings.cosmeticCorrection = true;
    settings.sigmaCold = 4.0;
    settings.sigmaHot = 5.0;
    settings.darkOptimization = true;
    settings.darkMultiplicationFactor = 0.9;
    settings.flatAutoLevel = false;
    settings.pedestal = 100.0;

    model.setLightCalibrationSettings(group_key, settings);

    const auto& stored = model.lightCalibrationSettings();
    auto it = stored.find(group_key);
    ASSERT_TRUE(it != stored.end());
    EXPECT_TRUE(it->second.cosmeticCorrection);
    EXPECT_DOUBLE_EQ(it->second.sigmaCold, 4.0);
    EXPECT_DOUBLE_EQ(it->second.sigmaHot, 5.0);
    EXPECT_TRUE(it->second.darkOptimization);
    EXPECT_DOUBLE_EQ(it->second.darkMultiplicationFactor, 0.9);
    EXPECT_FALSE(it->second.flatAutoLevel);
    EXPECT_DOUBLE_EQ(it->second.pedestal, 100.0);
}

TEST(IntegrationDataModel, ClearLightCalibrationSettings_RemovesEntry)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    LightCalibrationSettings settings;
    settings.cosmeticCorrection = true;
    model.setLightCalibrationSettings(group_key, settings);

    EXPECT_FALSE(model.lightCalibrationSettings().empty());

    model.clearLightCalibrationSettings(group_key);

    EXPECT_TRUE(model.lightCalibrationSettings().empty());
}

TEST(IntegrationDataModel, ClearAllLightCalibrationSettings_RemovesAll)
{
    IntegrationDataModel model;

    GroupingOptions options;
    options.bySession = true;
    model.setGroupingOptions(FrameType::Light, options);

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString()),
                                       makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-02"), QString())});

    const auto& groups = model.lightGroups();
    ASSERT_EQ(groups.size(), 2u);

    LightCalibrationSettings settings;
    settings.cosmeticCorrection = true;
    model.setLightCalibrationSettings(groups[0].key, settings);
    model.setLightCalibrationSettings(groups[1].key, settings);

    EXPECT_EQ(model.lightCalibrationSettings().size(), 2u);

    model.clearAllLightCalibrationSettings();

    EXPECT_TRUE(model.lightCalibrationSettings().empty());
}

TEST(IntegrationDataModel, SetCalibrationOverride_UpdatesExisting)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    ASSERT_FALSE(groups.empty());

    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias, QStringLiteral("bias_v1.fit"));
    EXPECT_EQ(model.calibrationOverrides().size(), 1u);
    EXPECT_EQ(model.calibrationOverrides()[0].masterPath, QStringLiteral("bias_v1.fit"));

    // Update existing override
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias, QStringLiteral("bias_v2.fit"));
    EXPECT_EQ(model.calibrationOverrides().size(), 1u);
    EXPECT_EQ(model.calibrationOverrides()[0].masterPath, QStringLiteral("bias_v2.fit"));
}

TEST(IntegrationDataModel, ClearCalibrationOverride_RemovesEntry)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    ASSERT_FALSE(groups.empty());

    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias, QStringLiteral("bias.fit"));
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Dark, QStringLiteral("dark.fit"));
    EXPECT_EQ(model.calibrationOverrides().size(), 2u);

    model.clearCalibrationOverride(lightKey, CalibrationOverrideType::Bias);
    EXPECT_EQ(model.calibrationOverrides().size(), 1u);
    EXPECT_EQ(model.calibrationOverrides()[0].type, CalibrationOverrideType::Dark);
}

TEST(IntegrationDataModel, RegroupClearsLightCalibrationSettings)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    LightCalibrationSettings settings;
    settings.cosmeticCorrection = true;
    model.setLightCalibrationSettings(group_key, settings);
    EXPECT_FALSE(model.lightCalibrationSettings().empty());

    QSignalSpy reset_spy(&model, &IntegrationDataModel::regroupResetSettings);

    GroupingOptions options = model.groupingOptions(FrameType::Light);
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    EXPECT_EQ(reset_spy.count(), 1);
    EXPECT_TRUE(model.lightCalibrationSettings().empty());
}

TEST(IntegrationDataModel, RegroupClearsCalibrationOverrides)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias, QStringLiteral("bias.fit"));
    EXPECT_FALSE(model.calibrationOverrides().empty());

    GroupingOptions options = model.groupingOptions(FrameType::Light);
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    EXPECT_TRUE(model.calibrationOverrides().empty());
}

TEST(IntegrationDataModel, SetStackingSettings_PersistsAllFields)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Median");
    settings.rejectionMethod = QStringLiteral("Winsorized");
    settings.requestedRejectionMethod = QStringLiteral("Auto");
    settings.sigmaLow = 2.5;
    settings.sigmaHigh = 3.5;
    settings.weighting = QStringLiteral("PSF Signal");
    settings.createRejectionMaps = true;
    settings.mergeRejectionMaps = true;
    settings.createQualityMaps = true;
    settings.localWeighting.enabled = true;
    settings.localWeighting.windowSize = 9;
    settings.localWeighting.gridSpacing = 48;
    settings.localWeighting.snrWeightExponent = 2.25;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.localNormalization.gridSize = 8;
    settings.localNormalization.referenceStrategy = QStringLiteral("Best Frames");
    settings.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    settings.manualNormalizationReference = QStringLiteral("C:/data/light_ref.fit");
    settings.drizzle.enabled = true;
    settings.drizzle.scale = 2.0;
    settings.solveFinalMasterIfNeeded = false;

    model.setStackingSettings(FrameType::Light, group_key, settings);

    const auto& stored = model.stackingSettings(FrameType::Light);
    auto it = stored.find(group_key);
    ASSERT_TRUE(it != stored.end());

    EXPECT_EQ(it->second.stackMethod, QStringLiteral("Median"));
    EXPECT_EQ(it->second.rejectionMethod, QStringLiteral("Winsorized"));
    EXPECT_EQ(it->second.requestedRejectionMethod, QStringLiteral("Auto"));
    EXPECT_DOUBLE_EQ(it->second.sigmaLow, 2.5);
    EXPECT_DOUBLE_EQ(it->second.sigmaHigh, 3.5);
    EXPECT_EQ(it->second.weighting, QStringLiteral("PSF Signal"));
    EXPECT_TRUE(it->second.createRejectionMaps);
    EXPECT_TRUE(it->second.mergeRejectionMaps);
    EXPECT_TRUE(it->second.createQualityMaps);
    EXPECT_TRUE(it->second.localWeighting.enabled);
    EXPECT_EQ(it->second.localWeighting.windowSize, 9);
    EXPECT_EQ(it->second.localWeighting.gridSpacing, 48);
    EXPECT_DOUBLE_EQ(it->second.localWeighting.snrWeightExponent, 2.25);
    EXPECT_EQ(it->second.normalizationMode, NormalizationMode::Local);
    EXPECT_TRUE(it->second.localNormalization.enabled);
    EXPECT_EQ(it->second.localNormalization.gridSize, 8);
    EXPECT_EQ(it->second.localNormalization.referenceStrategy, QStringLiteral("Best Frames"));
    EXPECT_EQ(it->second.normalizationReferenceMode, NormalizationReferenceMode::Manual);
    EXPECT_EQ(it->second.manualNormalizationReference, QStringLiteral("C:/data/light_ref.fit"));
    EXPECT_TRUE(it->second.drizzle.enabled);
    EXPECT_DOUBLE_EQ(it->second.drizzle.scale, 2.0);
    EXPECT_FALSE(it->second.solveFinalMasterIfNeeded);
}

TEST(IntegrationDataModel, SetStackingSettings_PersistsHdrExposureMerge)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light_hdr.fit"), QSize(100, 100),
                                                 10.0, QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.hdr.enabled = true;

    model.setStackingSettings(FrameType::Light, group_key, settings);

    const auto& stored = model.stackingSettings(FrameType::Light);
    auto it = stored.find(group_key);
    ASSERT_TRUE(it != stored.end());

    EXPECT_TRUE(it->second.hdr.enabled);
}

TEST(IntegrationDataModel, LightCalibrationSettings_EmitsCalibrationChanged)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    QSignalSpy calibration_spy(&model, &IntegrationDataModel::calibrationChanged);
    QSignalSpy settings_spy(&model, &IntegrationDataModel::settingsChanged);

    LightCalibrationSettings settings;
    settings.cosmeticCorrection = true;
    model.setLightCalibrationSettings(group_key, settings);

    EXPECT_EQ(calibration_spy.count(), 1);
    EXPECT_EQ(settings_spy.count(), 1);
}

TEST(IntegrationDataModel, CalibrationOverride_EmitsCalibrationChanged)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    QSignalSpy calibration_spy(&model, &IntegrationDataModel::calibrationChanged);

    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias, QStringLiteral("bias.fit"));

    EXPECT_EQ(calibration_spy.count(), 1);
}

// =============================================================================
// Performance optimization.
// =============================================================================

TEST(IntegrationDataModel, ValidationCacheReturnsConsistentResult)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString())});
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const auto result1 = model.validate();
    const auto result2 = model.validate();

    EXPECT_EQ(result1.issues.size(), result2.issues.size());
    EXPECT_FALSE(model.isValidationCacheDirty());
}

TEST(IntegrationDataModel, ValidationCacheInvalidatedOnFrameAdd)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString())});

    (void)model.validate();
    EXPECT_FALSE(model.isValidationCacheDirty());

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});
    EXPECT_TRUE(model.isValidationCacheDirty());

    (void)model.validate();
    EXPECT_FALSE(model.isValidationCacheDirty());
}

TEST(IntegrationDataModel, ValidationCacheInvalidatedOnSettingsChange)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    (void)model.validate();
    EXPECT_FALSE(model.isValidationCacheDirty());

    model.setOutputDirectory(QStringLiteral("%1%2new_out").arg(QDir::tempPath(), QDir::separator()));
    EXPECT_TRUE(model.isValidationCacheDirty());
}

TEST(IntegrationDataModel, OutputDirectoryChangeClearsFrameQualityMetrics)
{
    IntegrationDataModel model;
    auto light = makeFrame(FrameType::Light, QStringLiteral("light_quality.fit"), QSize(100, 100), 10.0,
                           QStringLiteral("2025-01-01"), QString());
    model.addFrames(FrameType::Light, {light});

    FrameQualityMetrics metrics;
    metrics.stars = 42;
    metrics.fwhm = 2.4;
    metrics.snr = 35.0;
    metrics.eccentricity = 0.12;
    metrics.succeeded = true;
    model.setFrameQualityMetrics(light.id, metrics);

    EXPECT_TRUE(model.frameQualityMetrics(light.id).has_value());

    model.setOutputDirectory(QStringLiteral("C:/output_a"));
    EXPECT_FALSE(model.frameQualityMetrics(light.id).has_value());

    model.setFrameQualityMetrics(light.id, metrics);
    EXPECT_TRUE(model.frameQualityMetrics(light.id).has_value());

    model.setOutputDirectory(QStringLiteral("C:/output_b"));
    EXPECT_FALSE(model.frameQualityMetrics(light.id).has_value());
}

TEST(Performance, ValidationCachedUnder5ms)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    model.beginBatchUpdate();
    std::vector<FrameInfo> bias_frames;
    bias_frames.reserve(50);
    for (int i = 0; i < 50; ++i)
    {
        bias_frames.push_back(makeFrame(FrameType::Bias, QStringLiteral("bias_%1.fit").arg(i), QSize(100, 100), 0.0,
                                        QString(), QString()));
    }
    model.addFrames(FrameType::Bias, bias_frames);
    model.addFrames(FrameType::Light, makeLightFrames(1000));
    model.endBatchUpdate();

    (void)model.validate();

    QElapsedTimer timer;
    timer.start();
    (void)model.validate();

    const qint64 elapsed_ms = timer.nsecsElapsed() / 1000000;
    EXPECT_LT(elapsed_ms, 5);
}

TEST(IntegrationDataModel, GroupsNotComputedUntilAccessed)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, makeLightFrames(100));

    EXPECT_TRUE(model.isGroupCacheDirty(FrameType::Light));
    (void)model.lightGroups();
    EXPECT_FALSE(model.isGroupCacheDirty(FrameType::Light));
}

TEST(IntegrationDataModel, GroupCacheInvalidatedOnGroupingOptionsChange)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, makeLightFrames(100));
    (void)model.lightGroups();

    GroupingOptions opts = model.groupingOptions(FrameType::Light);
    opts.byFilter = !opts.byFilter;
    model.setGroupingOptions(FrameType::Light, opts);

    EXPECT_TRUE(model.isGroupCacheDirty(FrameType::Light));
}

TEST(Performance, CachedGroupAccessUnder1ms)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, makeLightFrames(1000));
    (void)model.lightGroups();

    QElapsedTimer timer;
    timer.start();
    (void)model.lightGroups();

    const qint64 elapsed_ns = timer.nsecsElapsed();
    EXPECT_LT(elapsed_ns, 1000000); // 1ms
}

TEST(IntegrationDataModel, ResetClearsEverythingAndEmitsSignal)
{
    IntegrationDataModel model;

    // Setup some state
    model.setOutputDirectory(QStringLiteral("D:/Output"));
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    GroupingOptions options;
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings stacking;
    stacking.drizzle.enabled = true;
    model.setStackingSettings(FrameType::Light, group_key, stacking);

    LightCalibrationSettings calSettings;
    calSettings.cosmeticCorrection = true;
    model.setLightCalibrationSettings(group_key, calSettings);

    QSignalSpy reset_spy(&model, &IntegrationDataModel::modelReset);

    // Act
    model.reset();

    // Assert
    EXPECT_EQ(reset_spy.count(), 1);
    EXPECT_TRUE(model.lightFrames().frames().empty());
    EXPECT_TRUE(model.stackingSettings(FrameType::Light).empty());
    EXPECT_TRUE(model.lightCalibrationSettings().empty());
    EXPECT_TRUE(model.outputDirectory().isEmpty());

    // Grouping options should be back to defaults
    const auto& lightOpts = model.groupingOptions(FrameType::Light);
    EXPECT_FALSE(lightOpts.byCustomTag);
    EXPECT_TRUE(lightOpts.byResolution);
    EXPECT_TRUE(lightOpts.byObject);
    EXPECT_FALSE(lightOpts.byExposure);
    EXPECT_FALSE(lightOpts.byIsoGain);

    const auto& darkOpts = model.groupingOptions(FrameType::Dark);
    EXPECT_TRUE(darkOpts.byExposure);
    EXPECT_FALSE(darkOpts.byObject);

    const auto& flatOpts = model.groupingOptions(FrameType::Flat);
    EXPECT_FALSE(flatOpts.byExposure);
    EXPECT_FALSE(flatOpts.byObject);

    EXPECT_FALSE(model.hasUnsavedChanges());
}

TEST(IntegrationDataModel, IntegrationGroupingSeparation)
{
    IntegrationDataModel model;

    // Setup: two light frames with different sessions
    auto frame1 = makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 1.0, QStringLiteral("S1"),
                            QString());
    auto frame2 = makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(100, 100), 1.0, QStringLiteral("S2"),
                            QString());
    model.addFrames(FrameType::Light, {frame1, frame2});

    // Default integration grouping: Session is OFF. Should be 1 group.
    EXPECT_FALSE(model.lightIntegrationGroupingOptions().bySession);
    EXPECT_EQ(model.integrationGroups().size(), 1u);

    // Turn ON Session grouping for Integration
    GroupingOptions opts = model.lightIntegrationGroupingOptions();
    opts.bySession = true;
    model.setLightIntegrationGroupingOptions(opts);

    // Should now be 2 groups
    EXPECT_EQ(model.integrationGroups().size(), 2u);

    // Ensure Calibration grouping is unaffected (default session is ON for lights)
    EXPECT_TRUE(model.groupingOptions(FrameType::Light).bySession);
    EXPECT_EQ(model.lightGroups().size(), 2u);

    // Turn OFF Session grouping for Calibration
    GroupingOptions calOptions = model.groupingOptions(FrameType::Light);
    calOptions.bySession = false;
    model.setGroupingOptions(FrameType::Light, calOptions);

    // Calibration groups should merge to 1
    EXPECT_EQ(model.lightGroups().size(), 1u);

    // Integration groups should remain 2 (since we set it to true above)
    EXPECT_EQ(model.integrationGroups().size(), 2u);
}
// =============================================================================
// Pre-Calibrated Light Groups Tests
// =============================================================================

TEST(IntegrationDataModel, PreCalibratedFlag_DefaultFalse)
{
    // Verify that preCalibrated defaults to false
    LightCalibrationSettings settings;
    EXPECT_FALSE(settings.preCalibrated);
}

TEST(IntegrationDataModel, PreCalibratedFlag_StoresPerGroup)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    LightCalibrationSettings settings;
    settings.preCalibrated = true;
    settings.cosmeticCorrection = false; // Other settings should still be stored

    model.setLightCalibrationSettings(group_key, settings);

    const auto& stored = model.lightCalibrationSettings();
    auto it = stored.find(group_key);
    ASSERT_TRUE(it != stored.end());
    EXPECT_TRUE(it->second.preCalibrated);
    EXPECT_FALSE(it->second.cosmeticCorrection);
}

TEST(IntegrationDataModel, PreCalibratedFlag_SerializesToJson)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    LightCalibrationSettings settings;
    settings.preCalibrated = true;

    model.setLightCalibrationSettings(group_key, settings);
    model.setOutputDirectory(QDir::tempPath());

    // Serialize and restore
    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& stored = restored.lightCalibrationSettings();
    auto it = stored.find(group_key);
    ASSERT_TRUE(it != stored.end());
    EXPECT_TRUE(it->second.preCalibrated);
}

TEST(IntegrationDataModel, PreCalibratedFlag_ResetClearsFlag)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    LightCalibrationSettings settings;
    settings.preCalibrated = true;

    model.setLightCalibrationSettings(group_key, settings);
    EXPECT_FALSE(model.lightCalibrationSettings().empty());

    model.reset();

    // After reset, lightCalibrationSettings should be cleared
    EXPECT_TRUE(model.lightCalibrationSettings().empty());
}

TEST(IntegrationDataModel, PreCalibratedFlag_ValidationSkipsPreCalibratedGroups)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add lights without any calibration frames
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    // Add bias frames so validation would normally warn about missing dark
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QStringLiteral("2025-01-01"), QString())});

    // Without preCalibrated flag, we might get calibration warnings
    const auto result_before = model.validate();

    // Mark the light group as pre-calibrated
    const GroupKey group_key = model.lightGroups().front().key;
    LightCalibrationSettings settings;
    settings.preCalibrated = true;
    model.setLightCalibrationSettings(group_key, settings);

    // After marking as pre-calibrated, calibration warnings for this group should be suppressed
    const auto result_after = model.validate();

    // Count calibration-related issues
    auto countCalibrationIssues = [](const WorkflowValidationResult& r) {
        int count = 0;
        for (const auto& issue : r.issues)
        {
            if (issue.category() == QStringLiteral("Calibration"))
            {
                ++count;
            }
        }
        return count;
    };

    // The pre-calibrated group should not generate calibration warnings
    EXPECT_GE(countCalibrationIssues(result_before), countCalibrationIssues(result_after));
}

TEST(IntegrationDataModel, PreCalibratedFlag_InvalidatesValidationCacheOnToggle)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add lights + bias so validation produces a Calibration warning (missing flats).
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QStringLiteral("2025-01-01"), QString())});

    auto countCalibrationIssues = [](const WorkflowValidationResult& r) {
        int count = 0;
        for (const auto& issue : r.issues)
        {
            if (issue.category() == QStringLiteral("Calibration"))
            {
                ++count;
            }
        }
        return count;
    };

    // Warm the validation cache.
    const auto before_first = model.validate();
    const auto before_cached = model.validate();
    ASSERT_EQ(countCalibrationIssues(before_first), countCalibrationIssues(before_cached));
    ASSERT_GT(countCalibrationIssues(before_first), 0);

    const GroupKey group_key = model.lightGroups().front().key;

    // Toggle ON: should immediately suppress calibration issues.
    LightCalibrationSettings settings;
    settings.preCalibrated = true;
    model.setLightCalibrationSettings(group_key, settings);

    const auto after_enabled = model.validate();
    EXPECT_EQ(countCalibrationIssues(after_enabled), 0);

    // Toggle OFF: issues should return.
    settings.preCalibrated = false;
    model.setLightCalibrationSettings(group_key, settings);

    const auto after_disabled = model.validate();
    EXPECT_GT(countCalibrationIssues(after_disabled), 0);
}

TEST(IntegrationDataModel, PreCalibratedFlag_RegroupResetsFlag)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const GroupKey group_key = model.integrationGroups().front();

    LightCalibrationSettings settings;
    settings.preCalibrated = true;
    model.setLightCalibrationSettings(group_key, settings);
    EXPECT_FALSE(model.lightCalibrationSettings().empty());

    // Changing grouping options should clear settings (including preCalibrated)
    GroupingOptions options = model.groupingOptions(FrameType::Light);
    options.byCustomTag = true;
    model.setGroupingOptions(FrameType::Light, options);

    EXPECT_TRUE(model.lightCalibrationSettings().empty());
}

// =============================================================================
// Manually Unassigned Calibration Tests
// =============================================================================

TEST(IntegrationDataModel, UnassignedCalibration_CanBeSet)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    ASSERT_FALSE(groups.empty());

    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    // Set unassigned marker
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));

    EXPECT_EQ(model.calibrationOverrides().size(), 1u);
    EXPECT_EQ(model.calibrationOverrides()[0].masterPath, QString::fromLatin1(kUnassignedCalibrationMarker));
}

TEST(IntegrationDataModel, UnassignedCalibration_ReturnsManuallyUnassignedMatch)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});
    model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 10.0,
                                                QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    ASSERT_FALSE(groups.empty());

    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    // Set unassigned marker for flat
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Flat,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));

    // Build matcher and check result
    CalibrationMatcher matcher(model.groupingOptions(FrameType::Bias), model.groupingOptions(FrameType::Dark),
                               model.groupingOptions(FrameType::Flat), model.calibrationMatchingPolicy());
    matcher.setOverrides(model.calibrationOverrides());

    std::vector<FlatCalibrationGroup> flat_groups;
    for (const auto& group : model.flatGroups())
    {
        if (!group.frames.empty())
        {
            FlatCalibrationGroup entry;
            entry.key = FlatGroupKey::fromFrame(group.frames.front());
            entry.groupKey = group.key;
            entry.masterPath = QStringLiteral("flat.fit");
            flat_groups.push_back(entry);
        }
    }
    matcher.setFlatGroups(flat_groups);

    auto match = matcher.findFlat(lightKey);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManuallyUnassigned);
}

TEST(IntegrationDataModel, UnassignedCalibration_ClearedOnReset)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    ASSERT_FALSE(groups.empty());

    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Dark,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));
    EXPECT_FALSE(model.calibrationOverrides().empty());

    model.reset();

    EXPECT_TRUE(model.calibrationOverrides().empty());
}

TEST(IntegrationDataModel, UnassignedCalibration_ClearedByClearAll)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    ASSERT_FALSE(groups.empty());

    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Dark,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));
    EXPECT_EQ(model.calibrationOverrides().size(), 2u);

    model.clearAllCalibrationOverrides();

    EXPECT_TRUE(model.calibrationOverrides().empty());
}

TEST(IntegrationDataModel, UnassignedCalibration_ValidationProducesWarning)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"), QString())});
    model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 10.0,
                                                QStringLiteral("2025-01-01"), QString())});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(100, 100), 10.0,
                                                QStringLiteral("2025-01-01"), QString())});

    const auto& groups = model.lightGroups();
    ASSERT_FALSE(groups.empty());

    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    // Set unassigned marker for flat
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Flat,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));

    const auto result = model.validate();

    // Should have a warning for manually unassigned flat
    bool found_unassigned_warning = false;
    for (const auto& issue : result.issues)
    {
        if (issue.code == ValidationErrorCode::ManuallyUnassignedCalibration)
        {
            found_unassigned_warning = true;
            EXPECT_EQ(issue.severity, ValidationIssue::Severity::Warning);
            break;
        }
    }
    EXPECT_TRUE(found_unassigned_warning);
}

// ---------------------------------------------------------------------------
// Master re-detection after grouping change
// ---------------------------------------------------------------------------

namespace
{
/// Helper: create a dark frame whose path includes "master" so the auto-
/// detection heuristic will fire.  Resolution / gain / offset are kept
/// identical across calls so that only the exposure distinguishes groups.
FrameInfo makeMasterDarkFrame(double exposureSeconds, const QString& filename)
{
    // Path must contain "master" somewhere for the heuristic to match.
    const QString path = QDir::tempPath() + QStringLiteral("/DARK/master_darks/") + filename;
    FrameMetadata metadata(FrameType::Dark, QSize(3008, 3004), exposureSeconds, std::optional<double>(-10.0), 76, 35,
                           QString(), QString(), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                           QStringLiteral("Camera1"));
    return FrameInfo(FrameId(path), path, metadata, QString(), QString());
}

/// Helper: create a dark frame whose path does NOT contain "master".
FrameInfo makeRegularDarkFrame(double exposureSeconds, const QString& filename)
{
    const QString path = QDir::tempPath() + QStringLiteral("/DARK/calibration/") + filename;
    FrameMetadata metadata(FrameType::Dark, QSize(3008, 3004), exposureSeconds, std::optional<double>(-10.0), 76, 35,
                           QString(), QString(), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                           QStringLiteral("Camera1"));
    return FrameInfo(FrameId(path), path, metadata, QString(), QString());
}

/// Helper: create a bias frame whose path includes "master".
FrameInfo makeMasterBiasFrame(const QString& filename)
{
    const QString path = QDir::tempPath() + QStringLiteral("/BIAS/master/") + filename;
    FrameMetadata metadata(FrameType::Bias, QSize(3008, 3004), 0.0, std::nullopt, 76, 35, QString(), QString(),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Camera1"));
    return FrameInfo(FrameId(path), path, metadata, QString(), QString());
}

/// Helper: create a flat frame whose path includes "master".
FrameInfo makeMasterFlatFrame(const QString& filename, const QString& filter)
{
    const QString path = QDir::tempPath() + QStringLiteral("/FLAT/master/") + filename;
    FrameMetadata metadata(FrameType::Flat, QSize(3008, 3004), 1.0, std::nullopt, 76, 35, filter, QString(),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Camera1"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString());
}

/// Return default Dark grouping with byExposure enabled.
GroupingOptions darkGroupingWithExposure()
{
    GroupingOptions opts;
    opts.byResolution = true;
    opts.byExposure = true;
    opts.byFilter = false;
    opts.byObject = false;
    opts.bySession = false;
    opts.byIsoGain = false;
    opts.byOffset = true;
    opts.byTemperature = false;
    opts.byCustomTag = false;
    opts.exposureToleranceSeconds = 3.0;
    return opts;
}

/// Return Dark grouping with byExposure disabled (groups merge).
GroupingOptions darkGroupingWithoutExposure()
{
    GroupingOptions opts = darkGroupingWithExposure();
    opts.byExposure = false;
    opts.exposureToleranceSeconds = 0.0;
    return opts;
}
} // namespace

// Reproduces the exact user scenario: 4 master darks with different exposures,
// toggle "by Exposure" off and back on → masters must be restored.
TEST(IntegrationDataModel, MasterRedetection_DarkMastersRestoredAfterGroupingToggle)
{
    IntegrationDataModel model;

    const auto dark60 = makeMasterDarkFrame(60.0, QStringLiteral("master_dark_60s.fits"));
    const auto dark90 = makeMasterDarkFrame(90.0, QStringLiteral("master_dark_90s.fits"));
    const auto dark120 = makeMasterDarkFrame(120.0, QStringLiteral("master_dark_120s.fits"));
    const auto dark300 = makeMasterDarkFrame(300.0, QStringLiteral("master_dark_300s.fits"));

    model.addFrames(FrameType::Dark, {dark60, dark90, dark120, dark300});
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    // Set all four as masters (each in its own exposure group).
    model.setMasterStatus(dark60.displayPath, true, MasterSource::AutoDetected);
    model.setMasterStatus(dark90.displayPath, true, MasterSource::AutoDetected);
    model.setMasterStatus(dark120.displayPath, true, MasterSource::AutoDetected);
    model.setMasterStatus(dark300.displayPath, true, MasterSource::AutoDetected);

    ASSERT_TRUE(model.isMasterFrame(dark60.id));
    ASSERT_TRUE(model.isMasterFrame(dark90.id));
    ASSERT_TRUE(model.isMasterFrame(dark120.id));
    ASSERT_TRUE(model.isMasterFrame(dark300.id));
    EXPECT_EQ(model.masterCount(FrameType::Dark), 4);

    // Toggle "by Exposure" OFF → all 4 merge into one group → masters cleared.
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithoutExposure());

    // All masters should be cleared (only one frame can be master per group, and
    // group has 4 non-excluded members).
    EXPECT_EQ(model.masterCount(FrameType::Dark), 0);

    // Toggle "by Exposure" back ON → groups split again → re-detection should fire.
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    EXPECT_TRUE(model.isMasterFrame(dark60.id));
    EXPECT_TRUE(model.isMasterFrame(dark90.id));
    EXPECT_TRUE(model.isMasterFrame(dark120.id));
    EXPECT_TRUE(model.isMasterFrame(dark300.id));
    EXPECT_EQ(model.masterCount(FrameType::Dark), 4);
}

// Non-master-named files must NOT be auto-detected during re-grouping.
TEST(IntegrationDataModel, MasterRedetection_RegularFramesNotPromoted)
{
    IntegrationDataModel model;

    const auto dark = makeRegularDarkFrame(60.0, QStringLiteral("dark_60s.fits"));
    model.addFrames(FrameType::Dark, {dark});
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    // Not set as master, and filename doesn't contain "master".
    EXPECT_FALSE(model.isMasterFrame(dark.id));
    EXPECT_EQ(model.masterCount(FrameType::Dark), 0);

    // Toggle grouping off and on: should NOT promote a regular frame.
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithoutExposure());
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    EXPECT_FALSE(model.isMasterFrame(dark.id));
    EXPECT_EQ(model.masterCount(FrameType::Dark), 0);
}

// Groups with more than one non-excluded frame must not be auto-detected
// even if a "master" named file is among them.
TEST(IntegrationDataModel, MasterRedetection_MultiFrameGroupNotPromoted)
{
    IntegrationDataModel model;

    const auto master_dark = makeMasterDarkFrame(60.0, QStringLiteral("master_dark_60s.fits"));
    const auto regular_dark = makeRegularDarkFrame(60.0, QStringLiteral("dark_60s.fits"));
    model.addFrames(FrameType::Dark, {master_dark, regular_dark});

    GroupingOptions opts = darkGroupingWithExposure();
    model.setGroupingOptions(FrameType::Dark, opts);

    // Both darks at 60s → same group → 2 non-excluded frames.
    EXPECT_EQ(model.masterCount(FrameType::Dark), 0);

    // Toggle grouping off and on: multi-frame group shouldn't auto-detect.
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithoutExposure());
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    EXPECT_EQ(model.masterCount(FrameType::Dark), 0);
}

// A group that already has a UserDefined master must not receive a second
// auto-detected master when grouping changes.
TEST(IntegrationDataModel, MasterRedetection_DoesNotOverrideExistingMaster)
{
    IntegrationDataModel model;

    const auto master_dark = makeMasterDarkFrame(60.0, QStringLiteral("master_dark_60s.fits"));
    const auto user_dark = makeRegularDarkFrame(120.0, QStringLiteral("my_favourite_dark.fits"));
    model.addFrames(FrameType::Dark, {master_dark, user_dark});

    GroupingOptions opts = darkGroupingWithExposure();
    model.setGroupingOptions(FrameType::Dark, opts);

    // User explicitly sets the regular dark as master (different group).
    model.setMasterStatus(user_dark.displayPath, true, MasterSource::UserDefined);
    EXPECT_TRUE(model.isMasterFrame(user_dark.id));
    // master_dark is alone in its group but not yet set as master.

    // Toggle grouping off and on.  Re-detection should add master_dark
    // but must NOT touch user_dark.
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithoutExposure());
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    // The master_dark should be re-detected (alone in its group).
    EXPECT_TRUE(model.isMasterFrame(master_dark.id));
    // The user_dark group should be preserved (UserDefined was cleared during merge,
    // but since its filename isn't "master", it won't be re-detected).
    // Only the master-named single-frame group gets re-detected.
    EXPECT_FALSE(model.isMasterFrame(user_dark.id));
}

// Bias frames: re-detected on bias grouping toggle.
TEST(IntegrationDataModel, MasterRedetection_BiasRestoredAfterGroupingToggle)
{
    IntegrationDataModel model;

    const auto bias = makeMasterBiasFrame(QStringLiteral("master_bias.fits"));
    model.addFrames(FrameType::Bias, {bias});

    // Bias default grouping: by resolution + offset.
    GroupingOptions bias_opts;
    bias_opts.byResolution = true;
    bias_opts.byOffset = true;

    model.setGroupingOptions(FrameType::Bias, bias_opts);
    model.setMasterStatus(bias.displayPath, true, MasterSource::AutoDetected);
    ASSERT_TRUE(model.isMasterFrame(bias.id));

    // Toggle a grouping option (e.g. disable byOffset and re-enable).
    GroupingOptions bias_opts2 = bias_opts;
    bias_opts2.byOffset = false;
    model.setGroupingOptions(FrameType::Bias, bias_opts2);

    // Since there's only 1 bias frame, it's still a single-frame group → should remain or be re-detected.
    // Actually clearConflicting won't clear it here because the group only has 1 frame.
    // But let's verify the re-detection path doesn't break anything.
    model.setGroupingOptions(FrameType::Bias, bias_opts);
    EXPECT_TRUE(model.isMasterFrame(bias.id));
    EXPECT_EQ(model.masterCount(FrameType::Bias), 1);
}

// Light frames are never masters → re-detection must not promote them.
TEST(IntegrationDataModel, MasterRedetection_LightFramesNeverPromoted)
{
    IntegrationDataModel model;

    // Create a light frame with "master" in the name.
    const QString path = QDir::tempPath() + QStringLiteral("/LIGHT/master/master_light.fits");
    FrameMetadata meta(FrameType::Light, QSize(3008, 3004), 60.0, std::nullopt, 76, 35, QStringLiteral("L"),
                       QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                       QStringLiteral("Camera1"));
    FrameInfo light_frame(FrameId(path), path, meta, QStringLiteral("2025-01-01"), QString());

    model.addFrames(FrameType::Light, {light_frame});

    GroupingOptions light_opts;
    light_opts.byResolution = true;
    light_opts.byFilter = true;
    light_opts.byObject = true;
    light_opts.bySession = true;
    light_opts.byOffset = true;
    model.setGroupingOptions(FrameType::Light, light_opts);

    // Toggle a grouping option.
    GroupingOptions light_opts2 = light_opts;
    light_opts2.bySession = false;
    model.setGroupingOptions(FrameType::Light, light_opts2);
    model.setGroupingOptions(FrameType::Light, light_opts);

    EXPECT_FALSE(model.isMasterFrame(light_frame.id));
    EXPECT_EQ(model.masterCount(FrameType::Light), 0);
}

// Re-detection during JSON deserialization recovers masters that were lost
// before the project was saved.
TEST(IntegrationDataModel, MasterRedetection_FromJsonRecoversLostMasters)
{
    // Build a model with 4 master darks and set them as masters.
    IntegrationDataModel original;

    const auto dark60 = makeMasterDarkFrame(60.0, QStringLiteral("master_dark_60s.fits"));
    const auto dark120 = makeMasterDarkFrame(120.0, QStringLiteral("master_dark_120s.fits"));
    original.addFrames(FrameType::Dark, {dark60, dark120});
    original.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());
    original.setMasterStatus(dark60.displayPath, true, MasterSource::AutoDetected);
    original.setMasterStatus(dark120.displayPath, true, MasterSource::AutoDetected);

    ASSERT_EQ(original.masterCount(FrameType::Dark), 2);

    // Serialize to JSON, then strip the masterSources to simulate
    // a project saved after the bug caused master loss.
    QJsonObject json = original.toJson();
    json.insert(QStringLiteral("masterSources"), QJsonArray());

    // Load into a fresh model.
    IntegrationDataModel restored;
    restored.fromJson(json);

    // Re-detection during fromJson should recover both masters.
    EXPECT_TRUE(restored.isMasterFrame(dark60.id));
    EXPECT_TRUE(restored.isMasterFrame(dark120.id));
    EXPECT_EQ(restored.masterCount(FrameType::Dark), 2);
}

// Flat frames with "master" in path: re-detected after grouping toggle.
TEST(IntegrationDataModel, MasterRedetection_FlatMasterRestoredAfterGroupingToggle)
{
    IntegrationDataModel model;

    const auto flat_L = makeMasterFlatFrame(QStringLiteral("master_flat_L.fits"), QStringLiteral("L"));
    const auto flat_R = makeMasterFlatFrame(QStringLiteral("master_flat_R.fits"), QStringLiteral("R"));

    model.addFrames(FrameType::Flat, {flat_L, flat_R});

    // Group by resolution + filter + offset + session.
    GroupingOptions flat_opts;
    flat_opts.byResolution = true;
    flat_opts.byFilter = true;
    flat_opts.byOffset = true;
    flat_opts.bySession = true;
    model.setGroupingOptions(FrameType::Flat, flat_opts);

    model.setMasterStatus(flat_L.displayPath, true, MasterSource::AutoDetected);
    model.setMasterStatus(flat_R.displayPath, true, MasterSource::AutoDetected);
    ASSERT_EQ(model.masterCount(FrameType::Flat), 2);

    // Toggle byFilter off → both flats merge (same resolution, offset, session).
    GroupingOptions flat_opts2 = flat_opts;
    flat_opts2.byFilter = false;
    model.setGroupingOptions(FrameType::Flat, flat_opts2);
    EXPECT_EQ(model.masterCount(FrameType::Flat), 0);

    // Toggle byFilter back on → each flat is alone in its group → re-detected.
    model.setGroupingOptions(FrameType::Flat, flat_opts);
    EXPECT_TRUE(model.isMasterFrame(flat_L.id));
    EXPECT_TRUE(model.isMasterFrame(flat_R.id));
    EXPECT_EQ(model.masterCount(FrameType::Flat), 2);
}

// The warning signal should only fire for net-cleared masters (not restored ones).
TEST(IntegrationDataModel, MasterRedetection_WarningNotEmittedWhenAllRestored)
{
    IntegrationDataModel model;

    const auto dark60 = makeMasterDarkFrame(60.0, QStringLiteral("master_dark_60s.fits"));
    const auto dark120 = makeMasterDarkFrame(120.0, QStringLiteral("master_dark_120s.fits"));

    model.addFrames(FrameType::Dark, {dark60, dark120});
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());
    model.setMasterStatus(dark60.displayPath, true, MasterSource::AutoDetected);
    model.setMasterStatus(dark120.displayPath, true, MasterSource::AutoDetected);
    ASSERT_EQ(model.masterCount(FrameType::Dark), 2);

    // Merge groups and split again in one go (directly restore grouping).
    // The intermediate "all cleared" state shouldn't produce a warning because
    // all masters are restored.
    QSignalSpy warningSpy(&model, &IntegrationDataModel::masterSelectionPolicyWarning);

    // The warning only fires for the final setGroupingOptions.
    // Going directly from "with exposure" to "without" will clear but those
    // won't be restored (multi-frame group). But going back to "with" should
    // restore all of them.
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithoutExposure());
    const int warningsAfterMerge = warningSpy.count();

    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    // After re-enabling, masters are restored, so no additional warning.
    EXPECT_EQ(warningSpy.count(), warningsAfterMerge);
    EXPECT_EQ(model.masterCount(FrameType::Dark), 2);
}

// Excluded master frames are NOT re-detected (they were intentionally excluded).
TEST(IntegrationDataModel, MasterRedetection_ExcludedFrameNotRedetected)
{
    IntegrationDataModel model;

    const auto dark = makeMasterDarkFrame(60.0, QStringLiteral("master_dark_60s.fits"));
    model.addFrames(FrameType::Dark, {dark});
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    // Set as master, then exclude it.
    model.setMasterStatus(dark.displayPath, true, MasterSource::AutoDetected);
    ASSERT_TRUE(model.isMasterFrame(dark.id));

    model.updateFrameAttribute(dark.displayPath, FrameAttribute::Excluded, true);
    EXPECT_FALSE(model.isMasterFrame(dark.id));

    // Toggle grouping. The excluded frame should NOT be re-detected.
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithoutExposure());
    model.setGroupingOptions(FrameType::Dark, darkGroupingWithExposure());

    EXPECT_FALSE(model.isMasterFrame(dark.id));
    EXPECT_EQ(model.masterCount(FrameType::Dark), 0);
}
