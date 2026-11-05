// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimeZone>

#include "processes/ImageIntegration/persistence/AutosaveManager.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/GroupingOptions.h"

namespace
{

FrameInfo makeTestFrame(FrameType type, const QString& filename)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, QSize(100, 100), 10.0, -10.0, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("TestCamera"));

    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString());
}

} // namespace

class AutosaveRecoveryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dbPath = tempDir.filePath("autosave_test.db");
        database = std::make_unique<IntegrationDatabase>(dbPath);
        ASSERT_TRUE(database->initialize());
        model = std::make_unique<IntegrationDataModel>();
        autosave = std::make_unique<AutosaveManager>(model.get(), database.get(), "test_session", nullptr);
    }

    void TearDown() override
    {
        autosave->stop();
        autosave.reset();
        model.reset();
        database.reset();
    }

    QTemporaryDir tempDir;
    QString dbPath;
    std::unique_ptr<IntegrationDatabase> database;
    std::unique_ptr<IntegrationDataModel> model;
    std::unique_ptr<AutosaveManager> autosave;
};

TEST_F(AutosaveRecoveryTest, EmitsFailureSignalOnSaveError)
{
    // Point DB at a path whose parent directory does not exist.
    const QString badDbPath = tempDir.filePath("missing") + QDir::separator() + QStringLiteral("sub") +
                              QDir::separator() + QStringLiteral("autosave.db");
    IntegrationDatabase badDb(badDbPath);

    auto localModel = std::make_unique<IntegrationDataModel>();
    localModel->setOutputDirectory("/tmp/test_output");

    AutosaveManager badAutosave(localModel.get(), &badDb, "session", nullptr);
    QSignalSpy spy(&badAutosave, SIGNAL(autosaveFailed(QString)));

    badAutosave.triggerNow();

    EXPECT_EQ(spy.count(), 1);
    if (spy.count() == 1)
    {
        const auto args = spy.takeFirst();
        EXPECT_FALSE(args.at(0).toString().isEmpty());
    }
}

TEST_F(AutosaveRecoveryTest, SavesAndRestoresOutputDirectory)
{
    model->setOutputDirectory("/tmp/test_output");

    autosave->triggerNow();

    auto newModel = std::make_unique<IntegrationDataModel>();
    auto session = database->loadSession("test_session");
    ASSERT_TRUE(session.has_value());

    newModel->fromJson(*session);

    EXPECT_EQ(newModel->outputDirectory(), "/tmp/test_output");
}

TEST_F(AutosaveRecoveryTest, SavesAndRestoresFrames)
{
    auto light1 = makeTestFrame(FrameType::Light, QStringLiteral("light1.fit"));
    auto light2 = makeTestFrame(FrameType::Light, QStringLiteral("light2.fit"));
    auto dark1 = makeTestFrame(FrameType::Dark, QStringLiteral("dark1.fit"));
    auto bias1 = makeTestFrame(FrameType::Bias, QStringLiteral("bias1.fit"));
    auto flat1 = makeTestFrame(FrameType::Flat, QStringLiteral("flat1.fit"));

    model->addFrames(FrameType::Light, {light1, light2});
    model->addFrames(FrameType::Dark, {dark1});
    model->addFrames(FrameType::Bias, {bias1});
    model->addFrames(FrameType::Flat, {flat1});

    autosave->triggerNow();

    auto newModel = std::make_unique<IntegrationDataModel>();
    auto session = database->loadSession("test_session");
    ASSERT_TRUE(session.has_value());

    newModel->fromJson(*session);

    EXPECT_EQ(newModel->lightFrames().size(), 2);
    EXPECT_EQ(newModel->darkFrames().size(), 1);
    EXPECT_EQ(newModel->biasFrames().size(), 1);
    EXPECT_EQ(newModel->flatFrames().size(), 1);
}

TEST_F(AutosaveRecoveryTest, SavesAndRestoresGroupingOptions)
{
    GroupingOptions opts = model->groupingOptions(FrameType::Light);
    opts.byFilter = false;
    opts.byExposure = true;
    opts.byIsoGain = true;
    model->setGroupingOptions(FrameType::Light, opts);

    autosave->triggerNow();

    auto newModel = std::make_unique<IntegrationDataModel>();
    auto session = database->loadSession("test_session");
    ASSERT_TRUE(session.has_value());

    newModel->fromJson(*session);

    const auto& restored = newModel->groupingOptions(FrameType::Light);
    EXPECT_FALSE(restored.byFilter);
    EXPECT_TRUE(restored.byExposure);
    EXPECT_TRUE(restored.byIsoGain);
}

TEST_F(AutosaveRecoveryTest, SavesAndRestoresSubtractBiasFromDarkPolicy)
{
    CalibrationMatchingPolicy policy = model->calibrationMatchingPolicy();
    policy.subtractBiasFromDark = true;
    model->setCalibrationMatchingPolicy(policy);

    autosave->triggerNow();

    auto newModel = std::make_unique<IntegrationDataModel>();
    auto session = database->loadSession("test_session");
    ASSERT_TRUE(session.has_value());
    EXPECT_TRUE(session->value(QStringLiteral("calibrationPolicy"))
                    .toObject()
                    .value(QStringLiteral("subtractBiasFromDark"))
                    .toBool());

    newModel->fromJson(*session);

    EXPECT_TRUE(newModel->calibrationMatchingPolicy().subtractBiasFromDark);
}

TEST_F(AutosaveRecoveryTest, SavesAndRestoresCalibrationOverrides)
{
    LightGroupKey key;
    key.resolution = QStringLiteral("1920x1080");
    key.filter = QStringLiteral("L");
    key.objectName = QStringLiteral("M42");

    model->setCalibrationOverride(key, CalibrationOverrideType::Bias, QStringLiteral("/masters/bias.fit"));
    model->setCalibrationOverride(key, CalibrationOverrideType::Dark, QStringLiteral("/masters/dark.fit"));

    autosave->triggerNow();

    auto newModel = std::make_unique<IntegrationDataModel>();
    auto session = database->loadSession("test_session");
    ASSERT_TRUE(session.has_value());

    newModel->fromJson(*session);

    const auto& overrides = newModel->calibrationOverrides();
    EXPECT_EQ(overrides.size(), 2);
}

TEST_F(AutosaveRecoveryTest, HandlesCorruptedDatabase)
{
    // Use a separate path so we don't affect the fixture's database
    const QString corruptPath = tempDir.filePath("corrupted.db");

    // Create valid database first
    {
        IntegrationDatabase validDb(corruptPath);
        ASSERT_TRUE(validDb.initialize());
        ASSERT_TRUE(validDb.saveSession("test", QJsonObject{{"test", "data"}}));
    } // validDb closes here

    // Corrupt the file by overwriting with garbage
    {
        QFile file(corruptPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("CORRUPTED_DATA_NOT_SQLITE");
        file.close();
    }

    // Try to reinitialize - should fail gracefully
    IntegrationDatabase corruptedDb(corruptPath);
    EXPECT_FALSE(corruptedDb.initialize());
}

TEST_F(AutosaveRecoveryTest, HandlesEmptySession)
{
    // Set output directory to mark model as having unsaved changes
    // (otherwise autosave won't save anything)
    model->setOutputDirectory("/tmp/empty_session");
    autosave->triggerNow();

    auto newModel = std::make_unique<IntegrationDataModel>();
    auto session = database->loadSession("test_session");
    ASSERT_TRUE(session.has_value());

    // Should not crash when loading session with minimal data
    EXPECT_NO_THROW(newModel->fromJson(*session));

    // Session has no frames but does have the output directory
    EXPECT_TRUE(newModel->lightFrames().empty());
    EXPECT_EQ(newModel->outputDirectory(), QStringLiteral("/tmp/empty_session"));
}

TEST_F(AutosaveRecoveryTest, SavesAndRestoresMultipleSessions)
{
    // Create first session
    model->setOutputDirectory("/session1/output");
    autosave->triggerNow();

    // Create second session with different data
    auto autosave2 = std::make_unique<AutosaveManager>(model.get(), database.get(), "session2", nullptr);
    model->setOutputDirectory("/session2/output");
    autosave2->triggerNow();
    autosave2->stop();

    // Verify both sessions are independent
    auto session1 = database->loadSession("test_session");
    auto session2 = database->loadSession("session2");

    ASSERT_TRUE(session1.has_value());
    ASSERT_TRUE(session2.has_value());

    auto model1 = std::make_unique<IntegrationDataModel>();
    model1->fromJson(*session1);

    auto model2 = std::make_unique<IntegrationDataModel>();
    model2->fromJson(*session2);

    EXPECT_EQ(model1->outputDirectory(), "/session1/output");
    EXPECT_EQ(model2->outputDirectory(), "/session2/output");
}
