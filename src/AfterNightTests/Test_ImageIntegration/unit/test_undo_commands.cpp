// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QTimeZone>
#include <QUndoStack>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"

namespace
{
FrameInfo makeFrame(FrameType type, const QString& filename, const QSize& size = QSize(100, 100),
                    double exposure = 10.0)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, size, exposure, std::nullopt, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString());
}
} // namespace

// --- RemoveFramesCommand Tests ---

TEST(RemoveFramesCommand, UndoRestoresFrames)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto frame = makeFrame(FrameType::Light, QStringLiteral("light1.fit"));
    model.addFrames(FrameType::Light, {frame});
    EXPECT_EQ(model.lightFrames().size(), 1);

    // Remove via command
    stack.push(new RemoveFramesCommand(&model, FrameType::Light, {frame}));
    EXPECT_EQ(model.lightFrames().size(), 0);

    // Undo should restore
    stack.undo();
    EXPECT_EQ(model.lightFrames().size(), 1);
}

TEST(RemoveFramesCommand, RedoRemovesFramesAgain)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto frame = makeFrame(FrameType::Light, QStringLiteral("light1.fit"));
    model.addFrames(FrameType::Light, {frame});

    stack.push(new RemoveFramesCommand(&model, FrameType::Light, {frame}));
    EXPECT_EQ(model.lightFrames().size(), 0);

    stack.undo();
    EXPECT_EQ(model.lightFrames().size(), 1);

    stack.redo();
    EXPECT_EQ(model.lightFrames().size(), 0);
}

TEST(RemoveFramesCommand, RemovesMultipleFrames)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto frame1 = makeFrame(FrameType::Dark, QStringLiteral("dark1.fit"));
    auto frame2 = makeFrame(FrameType::Dark, QStringLiteral("dark2.fit"));
    auto frame3 = makeFrame(FrameType::Dark, QStringLiteral("dark3.fit"));
    model.addFrames(FrameType::Dark, {frame1, frame2, frame3});
    EXPECT_EQ(model.darkFrames().size(), 3);

    stack.push(new RemoveFramesCommand(&model, FrameType::Dark, {frame1, frame2}));
    EXPECT_EQ(model.darkFrames().size(), 1);

    stack.undo();
    EXPECT_EQ(model.darkFrames().size(), 3);
}

// --- ClearAllCommand Tests ---

TEST(ClearAllCommand, ClearsAllFrameTypes)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto bias = makeFrame(FrameType::Bias, QStringLiteral("bias1.fit"));
    auto dark = makeFrame(FrameType::Dark, QStringLiteral("dark1.fit"));
    auto flat = makeFrame(FrameType::Flat, QStringLiteral("flat1.fit"));
    auto light = makeFrame(FrameType::Light, QStringLiteral("light1.fit"));

    model.addFrames(FrameType::Bias, {bias});
    model.addFrames(FrameType::Dark, {dark});
    model.addFrames(FrameType::Flat, {flat});
    model.addFrames(FrameType::Light, {light});

    EXPECT_EQ(model.biasFrames().size(), 1);
    EXPECT_EQ(model.darkFrames().size(), 1);
    EXPECT_EQ(model.flatFrames().size(), 1);
    EXPECT_EQ(model.lightFrames().size(), 1);

    std::vector<FrameInfo> allFrames = {bias, dark, flat, light};
    stack.push(new ClearAllCommand(&model, std::move(allFrames)));

    EXPECT_EQ(model.biasFrames().size(), 0);
    EXPECT_EQ(model.darkFrames().size(), 0);
    EXPECT_EQ(model.flatFrames().size(), 0);
    EXPECT_EQ(model.lightFrames().size(), 0);
}

TEST(ClearAllCommand, UndoRestoresAllFrameTypes)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto bias = makeFrame(FrameType::Bias, QStringLiteral("bias1.fit"));
    auto dark = makeFrame(FrameType::Dark, QStringLiteral("dark1.fit"));
    auto flat = makeFrame(FrameType::Flat, QStringLiteral("flat1.fit"));
    auto light = makeFrame(FrameType::Light, QStringLiteral("light1.fit"));

    model.addFrames(FrameType::Bias, {bias});
    model.addFrames(FrameType::Dark, {dark});
    model.addFrames(FrameType::Flat, {flat});
    model.addFrames(FrameType::Light, {light});

    std::vector<FrameInfo> allFrames = {bias, dark, flat, light};
    stack.push(new ClearAllCommand(&model, std::move(allFrames)));

    // All cleared
    EXPECT_EQ(model.biasFrames().size(), 0);
    EXPECT_EQ(model.darkFrames().size(), 0);
    EXPECT_EQ(model.flatFrames().size(), 0);
    EXPECT_EQ(model.lightFrames().size(), 0);

    // Undo should restore all
    stack.undo();

    EXPECT_EQ(model.biasFrames().size(), 1);
    EXPECT_EQ(model.darkFrames().size(), 1);
    EXPECT_EQ(model.flatFrames().size(), 1);
    EXPECT_EQ(model.lightFrames().size(), 1);
}

// --- SetGroupingOptionsCommand Tests ---

TEST(SetGroupingOptionsCommand, ChangesGroupingOptions)
{
    IntegrationDataModel model;
    QUndoStack stack;

    GroupingOptions oldOpts = model.groupingOptions(FrameType::Light);
    EXPECT_TRUE(oldOpts.byFilter);
    EXPECT_FALSE(oldOpts.byIsoGain);

    GroupingOptions newOpts = oldOpts;
    newOpts.byFilter = false;
    newOpts.byIsoGain = true;

    stack.push(new SetGroupingOptionsCommand(&model, FrameType::Light, oldOpts, newOpts));

    const auto& current = model.groupingOptions(FrameType::Light);
    EXPECT_FALSE(current.byFilter);
    EXPECT_TRUE(current.byIsoGain);
}

TEST(SetGroupingOptionsCommand, UndoRestoresOldOptions)
{
    IntegrationDataModel model;
    QUndoStack stack;

    GroupingOptions oldOpts = model.groupingOptions(FrameType::Light);
    GroupingOptions newOpts = oldOpts;
    newOpts.byExposure = true;

    stack.push(new SetGroupingOptionsCommand(&model, FrameType::Light, oldOpts, newOpts));
    EXPECT_TRUE(model.groupingOptions(FrameType::Light).byExposure);

    stack.undo();
    EXPECT_FALSE(model.groupingOptions(FrameType::Light).byExposure);
}

TEST(SetGroupingOptionsCommand, RedoReappliesNewOptions)
{
    IntegrationDataModel model;
    QUndoStack stack;

    GroupingOptions oldOpts = model.groupingOptions(FrameType::Dark);
    GroupingOptions newOpts = oldOpts;
    newOpts.byCustomTag = true;

    stack.push(new SetGroupingOptionsCommand(&model, FrameType::Dark, oldOpts, newOpts));
    stack.undo();
    EXPECT_FALSE(model.groupingOptions(FrameType::Dark).byCustomTag);

    stack.redo();
    EXPECT_TRUE(model.groupingOptions(FrameType::Dark).byCustomTag);
}

// --- SetCalibrationOverrideCommand Tests ---

TEST(SetCalibrationOverrideCommand, SetsOverride)
{
    IntegrationDataModel model;
    QUndoStack stack;

    LightGroupKey key;
    key.resolution = QStringLiteral("1920x1080");
    key.filter = QStringLiteral("L");
    key.objectName = QStringLiteral("M42");

    QString newPath = QStringLiteral("/masters/bias_master.fit");

    stack.push(new SetCalibrationOverrideCommand(&model, key, CalibrationOverrideType::Bias, QString(), newPath));

    const auto& overrides = model.calibrationOverrides();
    EXPECT_EQ(overrides.size(), 1);
    EXPECT_EQ(overrides[0].masterPath, newPath);
    EXPECT_EQ(overrides[0].type, CalibrationOverrideType::Bias);
}

TEST(SetCalibrationOverrideCommand, UndoClearsOverride)
{
    IntegrationDataModel model;
    QUndoStack stack;

    LightGroupKey key;
    key.resolution = QStringLiteral("1920x1080");
    key.filter = QStringLiteral("L");
    key.objectName = QStringLiteral("M42");

    QString newPath = QStringLiteral("/masters/dark_master.fit");

    stack.push(new SetCalibrationOverrideCommand(&model, key, CalibrationOverrideType::Dark, QString(), newPath));

    EXPECT_EQ(model.calibrationOverrides().size(), 1);

    stack.undo();
    EXPECT_EQ(model.calibrationOverrides().size(), 0);
}

TEST(SetCalibrationOverrideCommand, UndoRestoresPreviousOverride)
{
    IntegrationDataModel model;
    QUndoStack stack;

    LightGroupKey key;
    key.resolution = QStringLiteral("1920x1080");
    key.filter = QStringLiteral("L");
    key.objectName = QStringLiteral("M42");

    QString oldPath = QStringLiteral("/masters/flat_old.fit");
    QString newPath = QStringLiteral("/masters/flat_new.fit");

    // Set initial override directly
    model.setCalibrationOverride(key, CalibrationOverrideType::Flat, oldPath);
    EXPECT_EQ(model.calibrationOverrides().size(), 1);
    EXPECT_EQ(model.calibrationOverrides()[0].masterPath, oldPath);

    // Change via command
    stack.push(new SetCalibrationOverrideCommand(&model, key, CalibrationOverrideType::Flat, oldPath, newPath));

    EXPECT_EQ(model.calibrationOverrides()[0].masterPath, newPath);

    // Undo should restore old path
    stack.undo();
    EXPECT_EQ(model.calibrationOverrides()[0].masterPath, oldPath);
}

// --- Undo Stack Integration Tests ---

TEST(UndoStack, LimitPreventsUnboundedGrowth)
{
    IntegrationDataModel model;
    QUndoStack stack;
    stack.setUndoLimit(5);

    // Push 10 commands
    for (int i = 0; i < 10; ++i)
    {
        auto frame = makeFrame(FrameType::Light, QString("light%1.fit").arg(i));
        model.addFrames(FrameType::Light, {frame});
        stack.push(new RemoveFramesCommand(&model, FrameType::Light, {frame}));
    }

    // Should only be able to undo 5 times
    EXPECT_EQ(stack.count(), 5);

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_TRUE(stack.canUndo());
        stack.undo();
    }

    EXPECT_FALSE(stack.canUndo());
}

TEST(UndoStack, NewCommandClearsRedoStack)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto frame1 = makeFrame(FrameType::Light, QStringLiteral("light1.fit"));
    auto frame2 = makeFrame(FrameType::Light, QStringLiteral("light2.fit"));
    model.addFrames(FrameType::Light, {frame1, frame2});

    stack.push(new RemoveFramesCommand(&model, FrameType::Light, {frame1}));
    stack.undo();

    EXPECT_TRUE(stack.canRedo());

    // Push new command should clear redo
    stack.push(new RemoveFramesCommand(&model, FrameType::Light, {frame2}));

    EXPECT_FALSE(stack.canRedo());
}

TEST(AddFramesCommand, RedoAddsAndUndoRemovesFrames)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto frame = makeFrame(FrameType::Light, QStringLiteral("add_light.fit"));
    stack.push(new AddFramesCommand(&model, FrameType::Light, {frame}));
    EXPECT_EQ(model.lightFrames().size(), 1);

    stack.undo();
    EXPECT_EQ(model.lightFrames().size(), 0);

    stack.redo();
    EXPECT_EQ(model.lightFrames().size(), 1);
}

TEST(SetFrameAttributesCommand, RestoresExcludedState)
{
    IntegrationDataModel model;
    QUndoStack stack;
    auto frame = makeFrame(FrameType::Light, QStringLiteral("exclude_light.fit"));
    model.addFrames(FrameType::Light, {frame});

    std::vector<FrameAttributeChange> changes{
        {frame.id.canonicalPath(), false, true},
    };
    stack.push(new SetFrameAttributesCommand(&model, FrameAttribute::Excluded, changes));
    EXPECT_TRUE(model.lightFrames().frames().front().excluded);

    stack.undo();
    EXPECT_FALSE(model.lightFrames().frames().front().excluded);
}

TEST(SetMasterStatusCommand, UndoRedoMasterFlag)
{
    IntegrationDataModel model;
    QUndoStack stack;
    auto frame = makeFrame(FrameType::Dark, QStringLiteral("master_dark.fit"));
    model.addFrames(FrameType::Dark, {frame});

    stack.push(new SetMasterStatusCommand(&model, frame.id.canonicalPath(), false, MasterSource::Unknown, true,
                                          MasterSource::UserDefined));
    EXPECT_TRUE(model.isMasterFrame(frame.id));

    stack.undo();
    EXPECT_FALSE(model.isMasterFrame(frame.id));
}

TEST(SetRegistrationReferenceCommand, UndoRedoReference)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto frame = makeFrame(FrameType::Light, QStringLiteral("reference_light.fit"));
    model.addFrames(FrameType::Light, {frame});
    const RegistrationGroupKey key = RegistrationGroupKey::fromFrame(frame);

    stack.push(new SetRegistrationReferenceCommand(&model, key, std::nullopt, frame.id));
    EXPECT_TRUE(model.registrationReferenceFrame(key).has_value());

    stack.undo();
    EXPECT_FALSE(model.registrationReferenceFrame(key).has_value());
}

TEST(ClearAllCalibrationOverridesCommand, UndoRestoresOverrides)
{
    IntegrationDataModel model;
    QUndoStack stack;

    LightGroupKey key;
    key.resolution = QStringLiteral("100x100");
    key.filter = QStringLiteral("L");
    key.objectName = QStringLiteral("M42");

    model.setCalibrationOverride(key, CalibrationOverrideType::Bias, QStringLiteral("bias.fit"));
    model.setCalibrationOverride(key, CalibrationOverrideType::Dark, QStringLiteral("dark.fit"));
    ASSERT_EQ(model.calibrationOverrides().size(), 2);

    stack.push(new ClearAllCalibrationOverridesCommand(&model, model.calibrationOverrides()));
    EXPECT_TRUE(model.calibrationOverrides().empty());

    stack.undo();
    EXPECT_EQ(model.calibrationOverrides().size(), 2);
}

TEST(SetOutputDirectoryCommand, UndoRedoOutputDir)
{
    IntegrationDataModel model;
    QUndoStack stack;

    stack.push(new SetOutputDirectoryCommand(&model, QStringLiteral("C:/old"), QStringLiteral("C:/new")));
    EXPECT_EQ(model.outputDirectory(), QStringLiteral("C:/new"));
    stack.undo();
    EXPECT_EQ(model.outputDirectory(), QStringLiteral("C:/old"));
}

TEST(SetLightIntegrationGroupingOptionsCommand, UndoRedoLightGrouping)
{
    IntegrationDataModel model;
    QUndoStack stack;

    GroupingOptions oldOpts = model.lightIntegrationGroupingOptions();
    GroupingOptions newOpts = oldOpts;
    newOpts.bySession = !oldOpts.bySession;

    stack.push(new SetLightIntegrationGroupingOptionsCommand(&model, oldOpts, newOpts));
    EXPECT_EQ(model.lightIntegrationGroupingOptions().bySession, newOpts.bySession);

    stack.undo();
    EXPECT_EQ(model.lightIntegrationGroupingOptions().bySession, oldOpts.bySession);
}

TEST(SetStackingSettingsCommand, UndoRedoStackingSettings)
{
    IntegrationDataModel model;
    QUndoStack stack;

    GroupKey key{QStringLiteral("g1")};
    StackingSettings oldSettings;
    oldSettings.limitMemory = false;
    oldSettings.maxMemoryMb = 2048;

    StackingSettings newSettings = oldSettings;
    newSettings.limitMemory = true;
    newSettings.maxMemoryMb = 1024;

    stack.push(new SetStackingSettingsCommand(&model, FrameType::Light, key, oldSettings, newSettings));
    ASSERT_TRUE(model.stackingSettings(FrameType::Light).contains(key));
    EXPECT_TRUE(model.stackingSettings(FrameType::Light).at(key).limitMemory);
    EXPECT_EQ(model.stackingSettings(FrameType::Light).at(key).maxMemoryMb, 1024);

    stack.undo();
    EXPECT_FALSE(model.stackingSettings(FrameType::Light).at(key).limitMemory);
    EXPECT_EQ(model.stackingSettings(FrameType::Light).at(key).maxMemoryMb, 2048);
}

TEST(SetRegistrationSettingsCommand, UndoRedoRegistrationSettings)
{
    IntegrationDataModel model;
    QUndoStack stack;

    GroupKey key{QStringLiteral("reg1")};
    RegistrationSettings oldSettings;
    oldSettings.threshold = 3.0;

    RegistrationSettings newSettings = oldSettings;
    newSettings.threshold = 7.5;

    stack.push(new SetRegistrationSettingsCommand(&model, key, oldSettings, newSettings));
    ASSERT_TRUE(model.registrationSettings().contains(key));
    EXPECT_DOUBLE_EQ(model.registrationSettings().at(key).threshold, 7.5);

    stack.undo();
    EXPECT_DOUBLE_EQ(model.registrationSettings().at(key).threshold, 3.0);
}

TEST(SetLightCalibrationSettingsCommand, UndoRedoLightCalibrationSettings)
{
    IntegrationDataModel model;
    QUndoStack stack;

    GroupKey key{QStringLiteral("light_calib_1")};
    LightCalibrationSettings oldSettings;
    oldSettings.cosmeticCorrection = false;

    LightCalibrationSettings newSettings = oldSettings;
    newSettings.cosmeticCorrection = true;

    stack.push(new SetLightCalibrationSettingsCommand(&model, key, oldSettings, newSettings));
    ASSERT_TRUE(model.lightCalibrationSettings().contains(key));
    EXPECT_TRUE(model.lightCalibrationSettings().at(key).cosmeticCorrection);

    stack.undo();
    EXPECT_FALSE(model.lightCalibrationSettings().at(key).cosmeticCorrection);
}

TEST(SetCalibrationMatchingPolicyCommand, UndoRedoPolicy)
{
    IntegrationDataModel model;
    QUndoStack stack;

    CalibrationMatchingPolicy oldPolicy = model.calibrationMatchingPolicy();
    CalibrationMatchingPolicy newPolicy = oldPolicy;
    newPolicy.allowDarkExposureFallback = !oldPolicy.allowDarkExposureFallback;
    newPolicy.warnOnFallback = !oldPolicy.warnOnFallback;

    stack.push(new SetCalibrationMatchingPolicyCommand(&model, oldPolicy, newPolicy));
    EXPECT_EQ(model.calibrationMatchingPolicy().allowDarkExposureFallback, newPolicy.allowDarkExposureFallback);
    EXPECT_EQ(model.calibrationMatchingPolicy().warnOnFallback, newPolicy.warnOnFallback);

    stack.undo();
    EXPECT_EQ(model.calibrationMatchingPolicy().allowDarkExposureFallback, oldPolicy.allowDarkExposureFallback);
    EXPECT_EQ(model.calibrationMatchingPolicy().warnOnFallback, oldPolicy.warnOnFallback);
}

TEST(SetLightCalibrationCacheModeCommand, UndoRedoCacheMode)
{
    IntegrationDataModel model;
    QUndoStack stack;

    stack.push(new SetLightCalibrationCacheModeCommand(&model, LightCalibrationCacheMode::Disk,
                                                       LightCalibrationCacheMode::InMemory));
    EXPECT_EQ(model.lightCalibrationCacheMode(), LightCalibrationCacheMode::InMemory);

    stack.undo();
    EXPECT_EQ(model.lightCalibrationCacheMode(), LightCalibrationCacheMode::Disk);
}

TEST(ChangeFrameTypeCommand, UndoRedoFrameTypeMove)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto frame = makeFrame(FrameType::Light, QStringLiteral("move_light.fit"));
    model.addFrames(FrameType::Light, {frame});

    FrameTypeChange change{FrameType::Light, frame, FrameType::Dark};
    stack.push(new ChangeFrameTypeCommand(&model, {change}));

    EXPECT_EQ(model.lightFrames().size(), 0);
    EXPECT_EQ(model.darkFrames().size(), 1);

    stack.undo();
    EXPECT_EQ(model.lightFrames().size(), 1);
    EXPECT_EQ(model.darkFrames().size(), 0);
}

TEST(LoadPresetCommand, UndoRedoJsonState)
{
    IntegrationDataModel model;
    QUndoStack stack;

    auto frame = makeFrame(FrameType::Light, QStringLiteral("preset_light.fit"));
    model.addFrames(FrameType::Light, {frame});
    const QJsonObject oldState = model.toJson();

    IntegrationDataModel presetModel;
    presetModel.setOutputDirectory(QStringLiteral("C:/preset"));
    const QJsonObject presetState = presetModel.toJson();

    stack.push(new LoadPresetCommand(&model, oldState, presetState, QStringLiteral("PresetA")));
    EXPECT_EQ(model.outputDirectory(), QStringLiteral("C:/preset"));

    stack.undo();
    EXPECT_EQ(model.lightFrames().size(), 1);
}

TEST(ClearFrameQualityMetricsCommand, RedoClearsAndUndoRestoresMetrics)
{
    IntegrationDataModel model;
    QUndoStack stack;

    const FrameId id1(QDir::tempPath() + QDir::separator() + QStringLiteral("metrics1.fit"));
    const FrameId id2(QDir::tempPath() + QDir::separator() + QStringLiteral("metrics2.fit"));

    FrameQualityMetrics m1;
    m1.stars = 100;
    m1.fwhm = 2.1;
    m1.snr = 25.0;
    m1.eccentricity = 0.35;
    m1.succeeded = true;

    FrameQualityMetrics m2;
    m2.stars = 50;
    m2.fwhm = 3.2;
    m2.snr = 10.0;
    m2.eccentricity = 0.55;
    m2.succeeded = false;

    model.setFrameQualityMetrics(id1, m1);
    model.setFrameQualityMetrics(id2, m2);
    ASSERT_TRUE(model.frameQualityMetrics(id1).has_value());
    ASSERT_TRUE(model.frameQualityMetrics(id2).has_value());

    std::vector<FrameId> ids = {id1, id2};
    std::vector<std::pair<FrameId, FrameQualityMetrics>> oldMetrics = {{id1, m1}, {id2, m2}};
    stack.push(new ClearFrameQualityMetricsCommand(&model, ids, oldMetrics));

    EXPECT_FALSE(model.frameQualityMetrics(id1).has_value());
    EXPECT_FALSE(model.frameQualityMetrics(id2).has_value());

    stack.undo();
    ASSERT_TRUE(model.frameQualityMetrics(id1).has_value());
    ASSERT_TRUE(model.frameQualityMetrics(id2).has_value());
    EXPECT_EQ(model.frameQualityMetrics(id1)->stars, 100);
    EXPECT_EQ(model.frameQualityMetrics(id2)->stars, 50);

    stack.redo();
    EXPECT_FALSE(model.frameQualityMetrics(id1).has_value());
    EXPECT_FALSE(model.frameQualityMetrics(id2).has_value());
}

TEST(ClearFrameQualityMetricsCommand, UndoDoesNotInventMissingMetrics)
{
    IntegrationDataModel model;
    QUndoStack stack;

    const FrameId id1(QDir::tempPath() + QDir::separator() + QStringLiteral("metrics_present.fit"));
    const FrameId id2(QDir::tempPath() + QDir::separator() + QStringLiteral("metrics_missing.fit"));

    FrameQualityMetrics m1;
    m1.stars = 42;
    m1.fwhm = 1.9;
    model.setFrameQualityMetrics(id1, m1);

    ASSERT_TRUE(model.frameQualityMetrics(id1).has_value());
    ASSERT_FALSE(model.frameQualityMetrics(id2).has_value());

    std::vector<FrameId> ids = {id1, id2};
    std::vector<std::pair<FrameId, FrameQualityMetrics>> oldMetrics = {{id1, m1}};
    stack.push(new ClearFrameQualityMetricsCommand(&model, ids, oldMetrics));

    EXPECT_FALSE(model.frameQualityMetrics(id1).has_value());
    EXPECT_FALSE(model.frameQualityMetrics(id2).has_value());

    stack.undo();
    EXPECT_TRUE(model.frameQualityMetrics(id1).has_value());
    EXPECT_FALSE(model.frameQualityMetrics(id2).has_value());
}
