// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTimeZone>

#include "processes/ImageIntegration/persistence/PresetManager.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <vector>

namespace
{
FrameInfo makeLightFrame(const QString& path)
{
    FrameMetadata metadata(FrameType::Light, QSize(100, 100), 30.0, std::nullopt, 100, 10, QStringLiteral("L"),
                           QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                           QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString());
}

RegistrationSettings::CometTrackingSettings makePresetCometSettings(const FrameInfo& first, const FrameInfo& second)
{
    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{first.id.canonicalPath(), QPointF(21.0, 22.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{second.id.canonicalPath(), QPointF(25.0, 29.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
    };
    return settings;
}

bool hasValidationIssue(const WorkflowValidationResult& validation, FixTarget target, const QString& needle)
{
    for (const auto& issue : validation.issues)
    {
        if (issue.fixTarget == target && issue.message.contains(needle, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}

QString validationMessages(const WorkflowValidationResult& validation)
{
    QStringList messages;
    for (const auto& issue : validation.issues)
    {
        messages.push_back(issue.message);
    }
    return messages.join(QStringLiteral("\n"));
}

void configurePresetCometWorkflow(IntegrationDataModel& model, const QString& storage_dir, const QString& frame_suffix,
                                  const QString& registration_mode, const QString& stacking_mode)
{
    model.setOutputDirectory(storage_dir);

    const QDir dir(storage_dir);
    const FrameInfo first = makeLightFrame(dir.filePath(QStringLiteral("preset_%1_a.fit").arg(frame_suffix)));
    const FrameInfo second = makeLightFrame(dir.filePath(QStringLiteral("preset_%1_b.fit").arg(frame_suffix)));
    model.addFrames(FrameType::Light, {first, second});

    ASSERT_FALSE(model.registrationGroups().empty());
    const GroupKey registration_group_key =
        RegistrationGroupEngine::toMachineGroupKey(model.registrationGroups().begin()->first);
    RegistrationSettings registration_settings;
    registration_settings.registrationMethod = QStringLiteral("comet_tracking");
    registration_settings.referenceImage = first.id.canonicalPath();
    registration_settings.cometTracking = makePresetCometSettings(first, second);
    registration_settings.cometTracking->outputMode = registration_mode;
    model.setRegistrationSettings(registration_group_key, registration_settings);

    ASSERT_FALSE(model.integrationGroups().empty());
    const GroupKey integration_group_key = model.integrationGroups().front();
    StackingSettings stacking_settings;
    stacking_settings.cometStackingMode = stacking_mode;
    model.setStackingSettings(FrameType::Light, integration_group_key, stacking_settings);
}

void expectPresetRegistrationCometMode(const IntegrationDataModel& model, const QString& expected_mode)
{
    ASSERT_FALSE(model.registrationGroups().empty());
    const GroupKey registration_group_key =
        RegistrationGroupEngine::toMachineGroupKey(model.registrationGroups().begin()->first);
    const auto& registration_settings = model.registrationSettings();
    const auto registration_it = registration_settings.find(registration_group_key);
    ASSERT_TRUE(registration_it != registration_settings.end());
    ASSERT_TRUE(registration_it->second.cometTracking.has_value());
    EXPECT_EQ(registration_it->second.cometTracking->outputMode, expected_mode);
}

void expectPresetCometMode(const IntegrationDataModel& model, const QString& expected_mode)
{
    ASSERT_FALSE(model.integrationGroups().empty());
    const GroupKey integration_group_key = model.integrationGroups().front();
    const auto& stacking_settings = model.stackingSettings(FrameType::Light);
    const auto stacking_it = stacking_settings.find(integration_group_key);
    ASSERT_TRUE(stacking_it != stacking_settings.end());
    EXPECT_EQ(stacking_it->second.cometStackingMode, expected_mode);
}

} // namespace

class PresetManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tempDir = std::make_unique<QTemporaryDir>();
        manager = std::make_unique<PresetManager>(tempDir->path());
        model = std::make_unique<IntegrationDataModel>();
    }

    std::unique_ptr<QTemporaryDir> tempDir;
    std::unique_ptr<PresetManager> manager;
    std::unique_ptr<IntegrationDataModel> model;
};

TEST_F(PresetManagerTest, NoPresetsInitially)
{
    EXPECT_TRUE(manager->listPresets().isEmpty());
}

TEST_F(PresetManagerTest, CanSaveAndListPreset)
{
    EXPECT_TRUE(manager->savePreset("MyPreset", *model));
    QStringList presets = manager->listPresets();
    EXPECT_EQ(presets.size(), 1);
    EXPECT_EQ(presets.first(), "MyPreset");
}

TEST_F(PresetManagerTest, CanLoadPreset)
{
    model->setOutputDirectory("C:/PresetOutput");
    model->setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);
    EXPECT_TRUE(manager->savePreset("TestPreset", *model));

    model->setOutputDirectory("C:/CurrentOutput");
    model->addFrames(FrameType::Light,
                     {makeLightFrame(QDir::tempPath() + QDir::separator() + QStringLiteral("preset_light.fit"))});

    EXPECT_TRUE(manager->loadPreset("TestPreset", *model));
    EXPECT_EQ(model->outputDirectory(), "C:/CurrentOutput");
    EXPECT_EQ(model->lightCalibrationCacheMode(), LightCalibrationCacheMode::Disk);
    EXPECT_EQ(model->lightFrames().size(), 1);
}

TEST_F(PresetManagerTest, PublicCometPresetRoundTripsNormally)
{
    configurePresetCometWorkflow(*model, tempDir->path(), QStringLiteral("public_comet"), QStringLiteral("Comet"),
                                 QStringLiteral("Comet"));

    ASSERT_TRUE(manager->savePreset("PublicComet", *model));

    IntegrationDataModel restored;
    configurePresetCometWorkflow(restored, tempDir->path(), QStringLiteral("public_comet"), QStringLiteral("Comet"),
                                 QStringLiteral("Stars"));
    ASSERT_TRUE(manager->loadPreset("PublicComet", restored));
    expectPresetCometMode(restored, QStringLiteral("Comet"));
}

TEST_F(PresetManagerTest, StarsPlusCometExperimentalPresetRoundTripsAsExplicitOptIn)
{
    const QString mode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
    configurePresetCometWorkflow(*model, tempDir->path(), QStringLiteral("experimental_stars_comet"), mode,
                                 QStringLiteral("Stars"));

    ASSERT_TRUE(manager->savePreset("ExperimentalStarsComet", *model));

    IntegrationDataModel restored;
    configurePresetCometWorkflow(restored, tempDir->path(), QStringLiteral("experimental_stars_comet"),
                                 QStringLiteral("Comet"), QStringLiteral("Stars"));
    ASSERT_TRUE(manager->loadPreset("ExperimentalStarsComet", restored));
    expectPresetRegistrationCometMode(restored, mode);
}

TEST_F(PresetManagerTest, AnUnofferedStarsPlusCometPresetRemainsUnknown)
{
    const QString unoffered_mode = QStringLiteral("Stars+Comet");
    configurePresetCometWorkflow(*model, tempDir->path(), QStringLiteral("unoffered_stars_comet"),
                                 QStringLiteral("Comet"), unoffered_mode);

    ASSERT_TRUE(manager->savePreset("UnofferedStarsComet", *model));

    IntegrationDataModel restored;
    configurePresetCometWorkflow(restored, tempDir->path(), QStringLiteral("unoffered_stars_comet"),
                                 QStringLiteral("Comet"), QStringLiteral("Stars"));
    ASSERT_TRUE(manager->loadPreset("UnofferedStarsComet", restored));
    expectPresetCometMode(restored, unoffered_mode);
}

TEST_F(PresetManagerTest, SavesAndLoadsSubtractBiasFromDarkPolicy)
{
    CalibrationMatchingPolicy policy = model->calibrationMatchingPolicy();
    policy.subtractBiasFromDark = true;
    model->setCalibrationMatchingPolicy(policy);
    ASSERT_TRUE(manager->savePreset("CcdMode", *model));

    policy.subtractBiasFromDark = false;
    model->setCalibrationMatchingPolicy(policy);
    ASSERT_TRUE(manager->loadPreset("CcdMode", *model));

    EXPECT_TRUE(model->calibrationMatchingPolicy().subtractBiasFromDark);
}

TEST_F(PresetManagerTest, CanDeletePreset)
{
    manager->savePreset("ToDelete", *model);
    EXPECT_FALSE(manager->listPresets().isEmpty());

    EXPECT_TRUE(manager->deletePreset("ToDelete"));
    EXPECT_TRUE(manager->listPresets().isEmpty());
}

TEST_F(PresetManagerTest, OverwritesExistingPreset)
{
    model->setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);
    manager->savePreset("Overwrite", *model);

    model->setLightCalibrationCacheMode(LightCalibrationCacheMode::InMemory);
    manager->savePreset("Overwrite", *model);

    IntegrationDataModel loadedModel;
    manager->loadPreset("Overwrite", loadedModel);
    EXPECT_EQ(loadedModel.lightCalibrationCacheMode(), LightCalibrationCacheMode::InMemory);
}

TEST_F(PresetManagerTest, PresetExistsReturnsTrueForExisting)
{
    EXPECT_FALSE(manager->presetExists("TestPreset"));

    manager->savePreset("TestPreset", *model);

    EXPECT_TRUE(manager->presetExists("TestPreset"));
}

TEST_F(PresetManagerTest, PresetExistsReturnsFalseAfterDelete)
{
    manager->savePreset("TestPreset", *model);
    EXPECT_TRUE(manager->presetExists("TestPreset"));

    manager->deletePreset("TestPreset");
    EXPECT_FALSE(manager->presetExists("TestPreset"));
}

TEST_F(PresetManagerTest, SanitizeNameRemovesInvalidCharacters)
{
    // Test invalid filename characters
    QString sanitized = PresetManager::sanitizeName("test<>:\"/\\|?*name");
    EXPECT_EQ(sanitized, "testname");
}

TEST_F(PresetManagerTest, SanitizeNameTrimsWhitespace)
{
    QString sanitized = PresetManager::sanitizeName("  test preset  ");
    EXPECT_EQ(sanitized, "test preset");
}

TEST_F(PresetManagerTest, SanitizeNameTruncatesLongNames)
{
    QString longName(200, 'a');
    QString sanitized = PresetManager::sanitizeName(longName);
    EXPECT_EQ(sanitized.length(), 100);
}

TEST_F(PresetManagerTest, SanitizeNameReturnsEmptyForOnlyInvalidChars)
{
    QString sanitized = PresetManager::sanitizeName("<>:\"/\\|?*");
    EXPECT_TRUE(sanitized.isEmpty());
}

TEST_F(PresetManagerTest, SaveRejectsEmptyName)
{
    EXPECT_FALSE(manager->savePreset("", *model));
    EXPECT_FALSE(manager->savePreset("   ", *model));
}

TEST_F(PresetManagerTest, PublicApisRejectUnsafeNames)
{
    const QString escapedStem = QStringLiteral("escaped_") + QDir(tempDir->path()).dirName();
    const QString unsafeName = QStringLiteral("../") + escapedStem;
    const QString outsidePath = QDir(tempDir->path()).filePath(QStringLiteral("../") + escapedStem + ".json");
    QFile::remove(outsidePath);

    EXPECT_FALSE(manager->savePreset(unsafeName, *model));
    EXPECT_FALSE(manager->presetExists(unsafeName));
    EXPECT_FALSE(manager->loadPreset(unsafeName, *model));
    EXPECT_FALSE(manager->buildAppliedState(unsafeName, *model).has_value());
    EXPECT_FALSE(QFile::exists(outsidePath));
    QFile::remove(outsidePath);
}

TEST_F(PresetManagerTest, DeleteRejectsUnsafeNamesAndLeavesOutsideFileUntouched)
{
    const QString escapedStem = QStringLiteral("escaped_") + QDir(tempDir->path()).dirName();
    const QString unsafeName = QStringLiteral("../") + escapedStem;
    const QString outsidePath = QDir(tempDir->path()).filePath(QStringLiteral("../") + escapedStem + ".json");
    QFile::remove(outsidePath);
    QFile outside(outsidePath);
    ASSERT_TRUE(outside.open(QIODevice::WriteOnly | QIODevice::Truncate));
    outside.write("{}");
    outside.close();

    EXPECT_FALSE(manager->deletePreset(unsafeName));
    EXPECT_TRUE(QFile::exists(outsidePath));
    QFile::remove(outsidePath);
}

TEST_F(PresetManagerTest, LoadNonexistentPresetReturnsFalse)
{
    EXPECT_FALSE(manager->loadPreset("NonexistentPreset", *model));
}

TEST_F(PresetManagerTest, DeleteNonexistentPresetReturnsFalse)
{
    EXPECT_FALSE(manager->deletePreset("NonexistentPreset"));
}

TEST_F(PresetManagerTest, MultiplePresetsCanCoexist)
{
    manager->savePreset("Preset1", *model);
    manager->savePreset("Preset2", *model);
    manager->savePreset("Preset3", *model);

    QStringList presets = manager->listPresets();
    EXPECT_EQ(presets.size(), 3);
    EXPECT_TRUE(presets.contains("Preset1"));
    EXPECT_TRUE(presets.contains("Preset2"));
    EXPECT_TRUE(presets.contains("Preset3"));
}
