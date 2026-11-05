// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <cstdint>
#include <vector>

#include "processes/ImageIntegration/persistence/AutosaveManager.h"
#include "processes/ImageIntegration/persistence/DatabaseSchema.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/persistence/PresetManager.h"
#include "processes/ImageIntegration/persistence/ProjectManager.h"
#include "processes/ImageIntegration/persistence/SessionDocument.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

namespace
{
QString databasePath(const QTemporaryDir& dir)
{
    return dir.path() + QDir::separator() + QStringLiteral("session.db");
}

QJsonObject makePayload(const QString& label)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("label"), label);
    return obj;
}
} // namespace

TEST(Persistence, SaveSessionResultProvidesErrorMessage)
{
    QTemporaryDir dir;
    const QString badPath = dir.path() + QDir::separator() + QStringLiteral("missing") + QDir::separator() +
                            QStringLiteral("sub") + QDir::separator() + QStringLiteral("session.db");

    IntegrationDatabase db(badPath);
    const auto result = db.saveSessionResult(QStringLiteral("session"), makePayload(QStringLiteral("x")));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}

// The const read accessors deliberately do NOT auto-initialize
// the way the write path does, so an empty read cannot be read as "nothing saved". These three pin
// both halves of that asymmetry so it cannot be "tidied" away in either direction.
TEST(Persistence, ReadAccessorsFailSoftOnDatabaseThatWasNeverOpened)
{
    QTemporaryDir dir;
    const QString path = databasePath(dir);
    IntegrationDatabase db(path);

    EXPECT_FALSE(db.loadSession(QStringLiteral("anything")).has_value());
    EXPECT_TRUE(db.listSessions().isEmpty());
    EXPECT_EQ(db.schemaVersion(), 0);
    EXPECT_TRUE(db.exportToJson().value(QStringLiteral("sessions")).toArray().isEmpty());

    // A read must never create the database file as a side effect.
    EXPECT_FALSE(QFile::exists(path));
}

TEST(Persistence, WriteAccessorsAutoInitializeWithoutAnExplicitOpen)
{
    QTemporaryDir dir;
    const QString path = databasePath(dir);
    IntegrationDatabase db(path);

    // No initialize() call: the write path is required to open the database and make its tables on demand.
    EXPECT_TRUE(db.saveSession(QStringLiteral("auto"), makePayload(QStringLiteral("v1"))));
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_EQ(db.schemaVersion(), persistence::kSchemaVersion);

    // The connection that write opened is what makes this read work.
    const auto loaded = db.loadSession(QStringLiteral("auto"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("label")).toString(), QStringLiteral("v1"));
}

TEST(Persistence, ReadAccessorsReturnEmptyForUnopenedConnectionEvenWhenDataExistsOnDisk)
{
    QTemporaryDir dir;
    const QString path = databasePath(dir);
    {
        IntegrationDatabase writer(path);
        ASSERT_TRUE(writer.initialize());
        ASSERT_TRUE(writer.saveSession(QStringLiteral("stored"), makePayload(QStringLiteral("payload"))));
    }
    ASSERT_TRUE(QFile::exists(path));

    IntegrationDatabase reader(path);

    // The session is on disk. These empty results are purely the unopened-connection precondition -
    // they are indistinguishable from the "no such session" answer, which is the whole point.
    EXPECT_FALSE(reader.loadSession(QStringLiteral("stored")).has_value());
    EXPECT_TRUE(reader.listSessions().isEmpty());
    EXPECT_EQ(reader.schemaVersion(), 0);
    EXPECT_TRUE(reader.exportToJson().value(QStringLiteral("sessions")).toArray().isEmpty());

    ASSERT_TRUE(reader.initialize());

    const auto loaded = reader.loadSession(QStringLiteral("stored"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("label")).toString(), QStringLiteral("payload"));

    const QStringList sessions = reader.listSessions();
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions.first(), QStringLiteral("stored"));
    EXPECT_EQ(reader.schemaVersion(), persistence::kSchemaVersion);
    EXPECT_EQ(reader.exportToJson().value(QStringLiteral("sessions")).toArray().size(), 1);
}

TEST(Persistence, SavesNewSession)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    QJsonObject payload = makePayload(QStringLiteral("session1"));
    EXPECT_TRUE(db.saveSession(QStringLiteral("session1"), payload));

    auto loaded = db.loadSession(QStringLiteral("session1"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("label")).toString(), QStringLiteral("session1"));
}

TEST(Persistence, OptionalCountersTrackDeterministicRepositoryStatementsWithoutChangingResults)
{
    QTemporaryDir dir;
    WorkflowExecutionCounterAccumulator counters;
    IntegrationDatabase db(databasePath(dir), &counters);
    ASSERT_TRUE(db.initialize());

    const std::uint64_t afterInitialize = counters.snapshot().sqliteStatementExecutions();
    const QJsonObject payload = makePayload(QStringLiteral("instrumented"));
    ASSERT_TRUE(db.saveSession(QStringLiteral("instrumented"), payload));

    // A save initializes: it reads which tables the database holds and the format it states before writing anything,
    // and then writes the session.
    constexpr std::uint64_t expectedSaveStatements = 3u;
    EXPECT_EQ(counters.snapshot().sqliteStatementExecutions() - afterInitialize, expectedSaveStatements);

    const std::uint64_t beforeLoad = counters.snapshot().sqliteStatementExecutions();
    const auto loaded = db.loadSession(QStringLiteral("instrumented"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("label")).toString(), QStringLiteral("instrumented"));
    EXPECT_EQ(counters.snapshot().sqliteStatementExecutions() - beforeLoad, 1u);
}

TEST(Persistence, LoadsExistingSession)
{
    QTemporaryDir dir;
    {
        IntegrationDatabase db(databasePath(dir));
        ASSERT_TRUE(db.initialize());
        EXPECT_TRUE(db.saveSession(QStringLiteral("session2"), makePayload(QStringLiteral("first"))));
    }

    IntegrationDatabase db2(databasePath(dir));
    ASSERT_TRUE(db2.initialize());
    auto loaded = db2.loadSession(QStringLiteral("session2"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("label")).toString(), QStringLiteral("first"));
}

TEST(Persistence, UpdatesExistingSession)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    EXPECT_TRUE(db.saveSession(QStringLiteral("session3"), makePayload(QStringLiteral("v1"))));
    EXPECT_TRUE(db.saveSession(QStringLiteral("session3"), makePayload(QStringLiteral("v2"))));

    auto loaded = db.loadSession(QStringLiteral("session3"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("label")).toString(), QStringLiteral("v2"));
}

TEST(Persistence, DeletesSession)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());
    EXPECT_TRUE(db.saveSession(QStringLiteral("session4"), makePayload(QStringLiteral("v1"))));

    EXPECT_TRUE(db.deleteSession(QStringLiteral("session4")));
    EXPECT_FALSE(db.loadSession(QStringLiteral("session4")).has_value());
}

TEST(Persistence, SavesFrameMetadata)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    QJsonObject metadata;
    metadata.insert(QStringLiteral("resolution"), QStringLiteral("100x100"));
    metadata.insert(QStringLiteral("exposure"), 10.0);

    QJsonObject payload;
    payload.insert(QStringLiteral("frameMetadata"), metadata);

    EXPECT_TRUE(db.saveSession(QStringLiteral("metadata"), payload));

    auto loaded = db.loadSession(QStringLiteral("metadata"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("frameMetadata")).toObject().value(QStringLiteral("resolution")).toString(),
              QStringLiteral("100x100"));
}

TEST(Persistence, SavesGroupingState)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    QJsonObject payload;
    payload.insert(QStringLiteral("grouping"), QJsonArray{QStringLiteral("A"), QStringLiteral("B")});
    EXPECT_TRUE(db.saveSession(QStringLiteral("grouping"), payload));

    auto loaded = db.loadSession(QStringLiteral("grouping"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("grouping")).toArray().size(), 2);
}

TEST(Persistence, SavesRegistrationMetrics)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    QJsonObject payload;
    payload.insert(QStringLiteral("registrationMetrics"), QJsonObject{{QStringLiteral("stars"), 120}});
    EXPECT_TRUE(db.saveSession(QStringLiteral("registration"), payload));

    auto loaded = db.loadSession(QStringLiteral("registration"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("registrationMetrics")).toObject().value(QStringLiteral("stars")).toInt(),
              120);
}

TEST(Persistence, SavesExecutionHistory)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    QJsonObject payload;
    payload.insert(QStringLiteral("history"), QJsonArray{QStringLiteral("stage1"), QStringLiteral("stage2")});
    EXPECT_TRUE(db.saveSession(QStringLiteral("history"), payload));

    auto loaded = db.loadSession(QStringLiteral("history"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("history")).toArray().size(), 2);
}

TEST(Persistence, SavesCalibrationOverrides)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    QJsonObject payload;
    payload.insert(QStringLiteral("overrides"), QJsonArray{QStringLiteral("flat_override.fit")});
    EXPECT_TRUE(db.saveSession(QStringLiteral("overrides"), payload));

    auto loaded = db.loadSession(QStringLiteral("overrides"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("overrides")).toArray().size(), 1);
}

TEST(Persistence, HandlesConcurrentAccess)
{
    QTemporaryDir dir;
    const QString path = databasePath(dir);

    IntegrationDatabase db1(path);
    IntegrationDatabase db2(path);
    ASSERT_TRUE(db1.initialize());
    ASSERT_TRUE(db2.initialize());

    EXPECT_TRUE(db1.saveSession(QStringLiteral("sessionA"), makePayload(QStringLiteral("A"))));
    auto loadedA = db2.loadSession(QStringLiteral("sessionA"));
    ASSERT_TRUE(loadedA.has_value());

    EXPECT_TRUE(db2.saveSession(QStringLiteral("sessionB"), makePayload(QStringLiteral("B"))));
    auto loadedB = db1.loadSession(QStringLiteral("sessionB"));
    ASSERT_TRUE(loadedB.has_value());
}

TEST(Persistence, HandlesCorruptedData)
{
    QTemporaryDir dir;
    const QString path = databasePath(dir);

    IntegrationDatabase db(path);
    ASSERT_TRUE(db.initialize());
    EXPECT_TRUE(db.saveSession(QStringLiteral("bad"), makePayload(QStringLiteral("good"))));

    {
        QSqlDatabase db_raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("corrupt_conn"));
        db_raw.setDatabaseName(path);
        ASSERT_TRUE(db_raw.open());
        QSqlQuery query(db_raw);
        query.prepare(QStringLiteral("UPDATE sessions SET data_json = :data WHERE name = :name"));
        query.bindValue(QStringLiteral(":data"), QStringLiteral("{not-json"));
        query.bindValue(QStringLiteral(":name"), QStringLiteral("bad"));
        ASSERT_TRUE(query.exec());
        db_raw.close();
        db_raw = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("corrupt_conn"));
    }

    auto loaded = db.loadSession(QStringLiteral("bad"));
    EXPECT_FALSE(loaded.has_value());
}

TEST(Persistence, ExportsToJson)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    EXPECT_TRUE(db.saveSession(QStringLiteral("one"), makePayload(QStringLiteral("one"))));
    EXPECT_TRUE(db.saveSession(QStringLiteral("two"), makePayload(QStringLiteral("two"))));

    QJsonObject export_json = db.exportToJson();
    QJsonArray sessions = export_json.value(QStringLiteral("sessions")).toArray();
    EXPECT_EQ(sessions.size(), 2);
}

// What one session database exports another imports - each session the stored document it was saved as. A session
// whose document states no format is refused naming it, and nothing of it is saved.
TEST(Persistence, ImportsFromJson)
{
    QTemporaryDir dir;
    QJsonObject exported;
    {
        IntegrationDatabase source(dir.path() + QDir::separator() + QStringLiteral("source.db"));
        ASSERT_TRUE(source.initialize());
        ASSERT_TRUE(source.saveSession(QStringLiteral("imported"), makePayload(QStringLiteral("value"))));
        exported = source.exportToJson();
    }

    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());
    EXPECT_TRUE(db.importFromJson(exported)) << db.lastError().toStdString();
    auto loaded = db.loadSession(QStringLiteral("imported"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->value(QStringLiteral("label")).toString(), QStringLiteral("value"));

    QJsonArray unstated;
    unstated.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("unstated")},
                                {QStringLiteral("data"), makePayload(QStringLiteral("value"))}});
    EXPECT_FALSE(db.importFromJson(QJsonObject{{QStringLiteral("sessions"), unstated}}));
    EXPECT_TRUE(db.lastError().contains(QStringLiteral("states no format version"))) << db.lastError().toStdString();
    EXPECT_FALSE(db.loadSession(QStringLiteral("unstated")).has_value());
}

TEST(Autosave, TriggersOnInterval)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    IntegrationDataModel model;
    AutosaveManager autosave(&model, &db, QStringLiteral("autosave"));

    QSignalSpy spy(&autosave, &AutosaveManager::autosaveCompleted);

    model.addFrames(FrameType::Light,
                    {FrameInfo(FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("auto.fit")),
                               QStringLiteral("auto.fit"),
                               FrameMetadata(FrameType::Light, QSize(100, 100), 10.0, std::nullopt, 100, 10, QString(),
                                             QString(), QDateTime(), QString()),
                               QStringLiteral("2025-01-01"), QString())});

    autosave.start(10);
    QTest::qWait(50);
    autosave.stop();

    EXPECT_GT(spy.count(), 0);
    auto loaded = db.loadSession(QStringLiteral("autosave"));
    EXPECT_TRUE(loaded.has_value());
}

TEST(Autosave, SkipsIdenticalSnapshotsOnManualTrigger)
{
    QTemporaryDir dir;
    IntegrationDatabase db(databasePath(dir));
    ASSERT_TRUE(db.initialize());

    IntegrationDataModel model;
    AutosaveManager autosave(&model, &db, QStringLiteral("autosave"));
    QSignalSpy spy(&autosave, &AutosaveManager::autosaveCompleted);

    model.setOutputDirectory(QStringLiteral("/tmp/autosave_dedup"));
    autosave.triggerNow();
    autosave.triggerNow();

    EXPECT_EQ(spy.count(), 1);
}

TEST(Persistence, MarkSavedClearsUnsavedChanges)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QStringLiteral("/tmp/project"));
    ASSERT_TRUE(model.hasUnsavedChanges());

    model.markSaved();
    EXPECT_FALSE(model.hasUnsavedChanges());
}

// =============================================================================
// Settings Persistence Tests
// =============================================================================

namespace
{
FrameInfo makeTestFrame(FrameType type, const QString& filename)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, QSize(100, 100), 30.0, std::nullopt, 100, 10, QStringLiteral("L"),
                           QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                           QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString());
}

RegistrationSettings::CometTrackingSettings makePersistedCometSettings(const FrameInfo& first, const FrameInfo& second)
{
    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{first.id.canonicalPath(), QPointF(11.0, 12.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{second.id.canonicalPath(), QPointF(15.0, 18.0),
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

void configurePersistedCometWorkflow(IntegrationDataModel& model, const QString& registration_mode,
                                     const QString& stacking_mode)
{
    model.setOutputDirectory(QDir::tempPath());
    const QString suffix = QStringLiteral("comet");

    const FrameInfo first = makeTestFrame(FrameType::Light, QStringLiteral("persisted_%1_a.fit").arg(suffix));
    const FrameInfo second = makeTestFrame(FrameType::Light, QStringLiteral("persisted_%1_b.fit").arg(suffix));
    model.addFrames(FrameType::Light, {first, second});

    ASSERT_FALSE(model.registrationGroups().empty());
    const GroupKey registration_group_key =
        RegistrationGroupEngine::toMachineGroupKey(model.registrationGroups().begin()->first);
    RegistrationSettings registration_settings;
    registration_settings.registrationMethod = QStringLiteral("comet_tracking");
    registration_settings.referenceImage = first.id.canonicalPath();
    registration_settings.cometTracking = makePersistedCometSettings(first, second);
    registration_settings.cometTracking->outputMode = registration_mode;
    model.setRegistrationSettings(registration_group_key, registration_settings);

    ASSERT_FALSE(model.integrationGroups().empty());
    const GroupKey integration_group_key = model.integrationGroups().front();
    StackingSettings stacking_settings;
    stacking_settings.cometStackingMode = stacking_mode;
    model.setStackingSettings(FrameType::Light, integration_group_key, stacking_settings);
}

void expectPersistedRegistrationCometMode(const IntegrationDataModel& model, const QString& expected_mode)
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

void expectPersistedCometMode(const IntegrationDataModel& model, const QString& expected_mode)
{
    ASSERT_FALSE(model.integrationGroups().empty());
    const GroupKey integration_group_key = model.integrationGroups().front();
    const auto& stacking_settings = model.stackingSettings(FrameType::Light);
    const auto stacking_it = stacking_settings.find(integration_group_key);
    ASSERT_TRUE(stacking_it != stacking_settings.end());
    EXPECT_EQ(stacking_it->second.cometStackingMode, expected_mode);
}

} // namespace

TEST(Persistence, LightCalibrationSettingsRoundTrip)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeTestFrame(FrameType::Light, QStringLiteral("light.fit"))});

    const GroupKey group_key = model.integrationGroups().front();

    LightCalibrationSettings settings;
    settings.cosmeticCorrection = true;
    settings.sigmaCold = 4.5;
    settings.sigmaHot = 5.5;
    settings.darkOptimization = true;
    settings.darkMultiplicationFactor = 0.85;
    settings.flatAutoLevel = false;
    settings.pedestal = 150.0;

    model.setLightCalibrationSettings(group_key, settings);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.lightCalibrationSettings();
    auto it = restored_settings.find(group_key);
    ASSERT_TRUE(it != restored_settings.end());

    EXPECT_TRUE(it->second.cosmeticCorrection);
    EXPECT_DOUBLE_EQ(it->second.sigmaCold, 4.5);
    EXPECT_DOUBLE_EQ(it->second.sigmaHot, 5.5);
    EXPECT_TRUE(it->second.darkOptimization);
    EXPECT_DOUBLE_EQ(it->second.darkMultiplicationFactor, 0.85);
    EXPECT_FALSE(it->second.flatAutoLevel);
    EXPECT_DOUBLE_EQ(it->second.pedestal, 150.0);
}

TEST(Persistence, CalibrationOverridesRoundTrip)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeTestFrame(FrameType::Light, QStringLiteral("light.fit"))});

    const auto& groups = model.lightGroups();
    ASSERT_FALSE(groups.empty());

    LightGroupKey lightKey = LightGroupKey::fromFrame(groups[0].frames.front());

    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias, QStringLiteral("bias_master.fit"));
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Dark, QStringLiteral("dark_master.fit"));
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Flat, QStringLiteral("flat_master.fit"));

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_overrides = restored.calibrationOverrides();
    EXPECT_EQ(restored_overrides.size(), 3u);

    bool found_bias = false;
    bool found_dark = false;
    bool found_flat = false;

    for (const auto& override : restored_overrides)
    {
        if (override.type == CalibrationOverrideType::Bias)
        {
            EXPECT_EQ(override.masterPath, QStringLiteral("bias_master.fit"));
            found_bias = true;
        }
        else if (override.type == CalibrationOverrideType::Dark)
        {
            EXPECT_EQ(override.masterPath, QStringLiteral("dark_master.fit"));
            found_dark = true;
        }
        else if (override.type == CalibrationOverrideType::Flat)
        {
            EXPECT_EQ(override.masterPath, QStringLiteral("flat_master.fit"));
            found_flat = true;
        }
    }

    EXPECT_TRUE(found_bias);
    EXPECT_TRUE(found_dark);
    EXPECT_TRUE(found_flat);
}

// A model whose JSON holds no light calibration settings and no calibration overrides reads with none of either, and
// the rest of it reads as written.
TEST(Persistence, AModelWithoutCalibrationSettingsOrOverridesReadsWithNone)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeTestFrame(FrameType::Light, QStringLiteral("light.fit"))});
    model.setOutputDirectory(QStringLiteral("D:/Output"));
    QJsonObject json = model.toJson();
    json.remove(QStringLiteral("lightCalibrationSettings"));
    json.remove(QStringLiteral("calibrationOverrides"));

    IntegrationDataModel restored;
    restored.fromJson(json);

    EXPECT_TRUE(restored.lightCalibrationSettings().empty());
    EXPECT_TRUE(restored.calibrationOverrides().empty());
    EXPECT_EQ(restored.outputDirectory(), QStringLiteral("D:/Output"));
    EXPECT_EQ(restored.frames(FrameType::Light).size(), model.frames(FrameType::Light).size());
}

TEST(Persistence, MissingQualityAndRejectionMapOptionsRetainFalseDefaults)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeTestFrame(FrameType::Light, QStringLiteral("light.fit"))});

    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings settings;
    settings.createQualityMaps = true;
    settings.createRejectionMaps = true;
    model.setStackingSettings(FrameType::Light, group_key, settings);

    QJsonObject json = model.toJson();
    QJsonObject stacking_root = json.value(QStringLiteral("stackingSettings")).toObject();
    QJsonArray light_entries = stacking_root.value(toString(FrameType::Light)).toArray();
    ASSERT_EQ(light_entries.size(), 1);

    QJsonObject record = light_entries.at(0).toObject();
    QJsonObject saved_settings = record.value(QStringLiteral("settings")).toObject();
    saved_settings.remove(QStringLiteral("createQualityMaps"));
    saved_settings.remove(QStringLiteral("createRejectionMaps"));
    record.insert(QStringLiteral("settings"), saved_settings);
    light_entries[0] = record;
    stacking_root.insert(toString(FrameType::Light), light_entries);
    json.insert(QStringLiteral("stackingSettings"), stacking_root);

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.stackingSettings(FrameType::Light);
    const auto it = restored_settings.find(group_key);
    ASSERT_NE(it, restored_settings.end());
    EXPECT_FALSE(it->second.createQualityMaps);
    EXPECT_FALSE(it->second.createRejectionMaps);
}

TEST(Persistence, SchemaV2_StackingSettingsAllFields)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeTestFrame(FrameType::Light, QStringLiteral("light.fit"))});

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
    settings.spatialRejectionClustering = true;
    settings.restackExcludingFlaggedFrames = true;
    settings.starCountScaling = QStringLiteral("Linear");
    settings.exponentialAlpha = 1.5;
    settings.minWeightFloor = 0.05;
    settings.perChannelFwhmWeighting = true;
    settings.localWeighting.enabled = true;
    settings.localWeighting.windowSize = 7;
    settings.localWeighting.gridSpacing = 40;
    settings.localWeighting.snrWeightExponent = 1.75;
    settings.normalizationMode = NormalizationMode::Local;
    settings.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
    settings.scaleEstimator = ScaleEstimatorMethod::IKSS;
    settings.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    settings.manualNormalizationReference = QStringLiteral("C:/data/light_ref.fit");
    settings.overlapNormalization.enabled = true;
    settings.overlapNormalization.minOverlapPixels = 2048;
    settings.overlapNormalization.minOverlapFraction = 0.075;

    settings.localNormalization.enabled = true;
    settings.localNormalization.gridSize = 8;
    settings.localNormalization.localScaleEnabled = true;
    settings.localNormalization.referenceStrategy = QStringLiteral("Best Frames");
    settings.localNormalization.maxIntegratedFrames = 32;
    settings.localNormalization.evaluationCriteria = QStringLiteral("FWHM");
    settings.localNormalization.scaleEvaluation = QStringLiteral("SNR");
    settings.localNormalization.scaleMode = QStringLiteral("Per Channel");
    settings.localNormalization.psfType = QStringLiteral("Moffat");
    settings.localNormalization.maxStars = 1000;
    settings.localNormalization.minDetectionSnr = 20.0;
    settings.localNormalization.allowClusteredSources = false;
    settings.localNormalization.lowClip = 0.0001;
    settings.localNormalization.highClip = 0.9;
    settings.localNormalization.hotPixelMedianRadius = 2;
    settings.localNormalization.noiseReductionRadius = 3;
    settings.localNormalization.localScaleSmoothingRadius = 2;

    settings.drizzle.enabled = true;
    settings.drizzle.scale = 2.0;
    settings.drizzle.pixelFraction = 0.8;
    settings.drizzle.kernel = QStringLiteral("Lanczos3");

    model.setStackingSettings(FrameType::Light, group_key, settings);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.stackingSettings(FrameType::Light);
    auto it = restored_settings.find(group_key);
    ASSERT_TRUE(it != restored_settings.end());

    const auto& s = it->second;
    EXPECT_EQ(s.stackMethod, QStringLiteral("Median"));
    EXPECT_EQ(s.rejectionMethod, QStringLiteral("Winsorized"));
    EXPECT_EQ(s.requestedRejectionMethod, QStringLiteral("Auto"));
    EXPECT_DOUBLE_EQ(s.sigmaLow, 2.5);
    EXPECT_DOUBLE_EQ(s.sigmaHigh, 3.5);
    EXPECT_EQ(s.weighting, QStringLiteral("PSF Signal"));
    EXPECT_TRUE(s.createRejectionMaps);
    EXPECT_TRUE(s.mergeRejectionMaps);
    EXPECT_TRUE(s.createQualityMaps);
    EXPECT_TRUE(s.spatialRejectionClustering);
    EXPECT_TRUE(s.restackExcludingFlaggedFrames);
    EXPECT_EQ(s.starCountScaling, QStringLiteral("Linear"));
    EXPECT_DOUBLE_EQ(s.exponentialAlpha, 1.5);
    EXPECT_DOUBLE_EQ(s.minWeightFloor, 0.05);
    EXPECT_TRUE(s.perChannelFwhmWeighting);
    EXPECT_TRUE(s.localWeighting.enabled);
    EXPECT_EQ(s.localWeighting.windowSize, 7);
    EXPECT_EQ(s.localWeighting.gridSpacing, 40);
    EXPECT_DOUBLE_EQ(s.localWeighting.snrWeightExponent, 1.75);
    EXPECT_EQ(s.normalizationMode, NormalizationMode::Local);
    EXPECT_EQ(s.globalNormalizationMethod, GlobalNormalizationMethod::MultiplicativeScaling);
    EXPECT_EQ(s.scaleEstimator, ScaleEstimatorMethod::IKSS);
    EXPECT_EQ(s.normalizationReferenceMode, NormalizationReferenceMode::Manual);
    EXPECT_EQ(s.manualNormalizationReference, QStringLiteral("C:/data/light_ref.fit"));
    EXPECT_TRUE(s.overlapNormalization.enabled);
    EXPECT_EQ(s.overlapNormalization.minOverlapPixels, 2048);
    EXPECT_DOUBLE_EQ(s.overlapNormalization.minOverlapFraction, 0.075);

    EXPECT_TRUE(s.localNormalization.enabled);
    EXPECT_EQ(s.localNormalization.gridSize, 8);
    EXPECT_TRUE(s.localNormalization.localScaleEnabled);
    EXPECT_EQ(s.localNormalization.referenceStrategy, QStringLiteral("Best Frames"));
    EXPECT_EQ(s.localNormalization.maxIntegratedFrames, 32);
    EXPECT_EQ(s.localNormalization.psfType, QStringLiteral("Moffat"));
    EXPECT_EQ(s.localNormalization.maxStars, 1000);
    EXPECT_DOUBLE_EQ(s.localNormalization.minDetectionSnr, 20.0);
    EXPECT_FALSE(s.localNormalization.allowClusteredSources);
    EXPECT_EQ(s.localNormalization.hotPixelMedianRadius, 2);
    EXPECT_EQ(s.localNormalization.localScaleSmoothingRadius, 2);

    EXPECT_TRUE(s.drizzle.enabled);
    EXPECT_DOUBLE_EQ(s.drizzle.scale, 2.0);
    EXPECT_DOUBLE_EQ(s.drizzle.pixelFraction, 0.8);
    EXPECT_EQ(s.drizzle.kernel, QStringLiteral("Lanczos3"));
}

TEST(Persistence, MultipleGroupSettingsRoundTrip)
{
    IntegrationDataModel model;

    GroupingOptions options;
    options.bySession = true;
    model.setGroupingOptions(FrameType::Light, options);

    model.addFrames(FrameType::Light, {makeTestFrame(FrameType::Light, QStringLiteral("light1.fit")),
                                       makeTestFrame(FrameType::Light, QStringLiteral("light2.fit"))});

    // Modify one frame to be in a different session so we get 2 groups
    model.updateFrameAttribute(QDir::tempPath() + QDir::separator() + QStringLiteral("light2.fit"),
                               FrameAttribute::UserSession, QStringLiteral("2025-01-02"));

    const auto& groups = model.lightGroups();
    ASSERT_EQ(groups.size(), 2u);

    // Set different settings for each group
    LightCalibrationSettings settings1;
    settings1.cosmeticCorrection = true;
    settings1.sigmaCold = 3.0;
    model.setLightCalibrationSettings(groups[0].key, settings1);

    LightCalibrationSettings settings2;
    settings2.cosmeticCorrection = false;
    settings2.sigmaCold = 5.0;
    model.setLightCalibrationSettings(groups[1].key, settings2);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.lightCalibrationSettings();
    EXPECT_EQ(restored_settings.size(), 2u);

    auto it1 = restored_settings.find(groups[0].key);
    auto it2 = restored_settings.find(groups[1].key);

    ASSERT_TRUE(it1 != restored_settings.end());
    ASSERT_TRUE(it2 != restored_settings.end());

    EXPECT_TRUE(it1->second.cosmeticCorrection);
    EXPECT_DOUBLE_EQ(it1->second.sigmaCold, 3.0);

    EXPECT_FALSE(it2->second.cosmeticCorrection);
    EXPECT_DOUBLE_EQ(it2->second.sigmaCold, 5.0);
}

TEST(Persistence, StackingSettingsRoundTripPreservesHdrExposureMerge)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {makeTestFrame(FrameType::Light, QStringLiteral("light_hdr.fit"))});

    const GroupKey group_key = model.integrationGroups().front();

    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.hdr.enabled = true;
    model.setStackingSettings(FrameType::Light, group_key, settings);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    const auto& restored_settings = restored.stackingSettings(FrameType::Light);
    auto it = restored_settings.find(group_key);
    ASSERT_TRUE(it != restored_settings.end());
    EXPECT_TRUE(it->second.hdr.enabled);
}

TEST(Persistence, PublicCometStackingModeRoundTripsNormally)
{
    IntegrationDataModel model;
    configurePersistedCometWorkflow(model, QStringLiteral("Comet"), QStringLiteral("Comet"));

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    expectPersistedCometMode(restored, QStringLiteral("Comet"));
}

TEST(Persistence, StarsPlusCometExperimentalTokenRoundTripsAsExplicitOptIn)
{
    const QString mode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
    IntegrationDataModel model;
    configurePersistedCometWorkflow(model, mode, QStringLiteral("Stars"));

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    expectPersistedRegistrationCometMode(restored, mode);
}

// A stacking-mode text no dialog offers is kept as written and reads as no mode the stack knows.
TEST(Persistence, AStackingModeNoDialogOffersRoundTripsAsUnknown)
{
    const QString unoffered_mode = QStringLiteral("Stars+Comet");
    IntegrationDataModel model;
    configurePersistedCometWorkflow(model, QStringLiteral("Comet"), unoffered_mode);

    const QJsonObject json = model.toJson();

    IntegrationDataModel restored;
    restored.fromJson(json);

    expectPersistedCometMode(restored, unoffered_mode);
}

namespace
{
/// The registration fields no other case holds to a store, each at a value other than its default: the star
/// detection's parameters, the chromatic correction's edge fraction and matched stars, and the comet's tracking and fit
/// options with an observation that states every field of its own.
RegistrationSettings registrationAwayFromItsDefaults(const FrameInfo& first, const FrameInfo& second)
{
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.referenceImage = first.id.canonicalPath();
    settings.radius = 9;
    settings.threshold = 0.4;
    settings.amplitudeRangeEnabled = true;
    settings.thresholdMinimum = 0.05;
    settings.thresholdMaximum = 0.8;
    settings.roundnessThreshold = 0.3;
    settings.roundnessRangeEnabled = true;
    settings.roundnessThresholdMaximum = 0.9;
    settings.convergence = 3;
    settings.relaxPsfChecks = true;
    settings.profileType = QStringLiteral("Moffat");
    settings.minimumBeta = 2.5;
    settings.chromaticAberration.enabled = true;
    settings.chromaticAberration.edgeFraction = 0.7;
    settings.chromaticAberration.minMatchedStars = 9;
    RegistrationSettings::CometTrackingSettings comet = makePersistedCometSettings(first, second);
    comet.observations[1].origin = RegistrationSettings::CometObservationOrigin::AcceptedAutomatic;
    comet.observations[1].enabled = false;
    comet.observations[1].apertureRadiusPixels = 6.5;
    comet.observations[1].note = QStringLiteral("faint coma");
    comet.trackingOptions.searchRadiusPixels = 48.0;
    comet.trackingOptions.minimumSnr = 5.0;
    comet.trackingOptions.maxBootstrapFrames = 12;
    comet.fitOptions.model = QStringLiteral("quadratic");
    comet.fitOptions.robustRejection = false;
    comet.fitOptions.outlierSigma = 2.5;
    settings.cometTracking = comet;
    return settings;
}

/// The local normalization's analysis fields no other case holds to a store, each at a value other than its default.
StackingSettings localNormalizationAwayFromItsDefaults()
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.localNormalization.gridInterpolation = QStringLiteral("Bilinear");
    settings.localNormalization.backgroundEstimator = QStringLiteral("Percentile");
    settings.localNormalization.backgroundPercentile = 0.4;
    settings.localNormalization.sigmaClipK = 2.5;
    settings.localNormalization.sigmaClipIterations = 5;
    settings.localNormalization.backgroundSmoothingRadius = 3;
    settings.localNormalization.psfType = QStringLiteral("Moffat");
    settings.localNormalization.moffatBeta = 2.8;
    return settings;
}

/// Holds a restored model's registration and local normalization fields to the ones stored.
void expectTheFieldsKept(const IntegrationDataModel& restored, const RegistrationSettings& registration,
                         const StackingSettings& stacking)
{
    ASSERT_FALSE(restored.registrationGroups().empty());
    const auto registrationIt = restored.registrationSettings().find(
        RegistrationGroupEngine::toMachineGroupKey(restored.registrationGroups().begin()->first));
    ASSERT_TRUE(registrationIt != restored.registrationSettings().end());
    const RegistrationSettings& kept = registrationIt->second;
    EXPECT_EQ(kept.radius, registration.radius);
    EXPECT_DOUBLE_EQ(kept.threshold, registration.threshold);
    EXPECT_EQ(kept.amplitudeRangeEnabled, registration.amplitudeRangeEnabled);
    EXPECT_DOUBLE_EQ(kept.thresholdMinimum, registration.thresholdMinimum);
    EXPECT_DOUBLE_EQ(kept.thresholdMaximum, registration.thresholdMaximum);
    EXPECT_DOUBLE_EQ(kept.roundnessThreshold, registration.roundnessThreshold);
    EXPECT_EQ(kept.roundnessRangeEnabled, registration.roundnessRangeEnabled);
    EXPECT_DOUBLE_EQ(kept.roundnessThresholdMaximum, registration.roundnessThresholdMaximum);
    EXPECT_EQ(kept.convergence, registration.convergence);
    EXPECT_EQ(kept.relaxPsfChecks, registration.relaxPsfChecks);
    EXPECT_EQ(kept.profileType, registration.profileType);
    EXPECT_DOUBLE_EQ(kept.minimumBeta, registration.minimumBeta);
    EXPECT_DOUBLE_EQ(kept.chromaticAberration.edgeFraction, registration.chromaticAberration.edgeFraction);
    EXPECT_EQ(kept.chromaticAberration.minMatchedStars, registration.chromaticAberration.minMatchedStars);
    EXPECT_TRUE(kept.chromaticAberration == registration.chromaticAberration);
    ASSERT_TRUE(kept.cometTracking.has_value());
    EXPECT_TRUE(kept.cometTracking->trackingOptions == registration.cometTracking->trackingOptions);
    EXPECT_TRUE(kept.cometTracking->fitOptions == registration.cometTracking->fitOptions);
    EXPECT_TRUE(kept.cometTracking->observations == registration.cometTracking->observations);
    EXPECT_TRUE(*kept.cometTracking == *registration.cometTracking);

    ASSERT_FALSE(restored.integrationGroups().empty());
    const auto stackingIt = restored.stackingSettings(FrameType::Light).find(restored.integrationGroups().front());
    ASSERT_TRUE(stackingIt != restored.stackingSettings(FrameType::Light).end());
    const LocalNormalizationSettings& local = stackingIt->second.localNormalization;
    const LocalNormalizationSettings& stated = stacking.localNormalization;
    EXPECT_EQ(local.gridInterpolation, stated.gridInterpolation);
    EXPECT_EQ(local.backgroundEstimator, stated.backgroundEstimator);
    EXPECT_DOUBLE_EQ(local.backgroundPercentile, stated.backgroundPercentile);
    EXPECT_DOUBLE_EQ(local.sigmaClipK, stated.sigmaClipK);
    EXPECT_EQ(local.sigmaClipIterations, stated.sigmaClipIterations);
    EXPECT_EQ(local.backgroundSmoothingRadius, stated.backgroundSmoothingRadius);
    EXPECT_EQ(local.psfType, stated.psfType);
    EXPECT_DOUBLE_EQ(local.moffatBeta, stated.moffatBeta);
}
} // namespace

// The registration's star detection parameters, its chromatic correction's edge fraction and matched stars, the
// comet's tracking and fit options and the local normalization's analysis fields survive every store a group's
// settings are kept in: the model's own JSON, the session database, a preset and a project file, each read back as
// the application reads it.
TEST(Persistence, TheDetectionCometAndLocalNormalizationFieldsSurviveEveryStore)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const FrameInfo first = makeTestFrame(FrameType::Light, QStringLiteral("stores_a.fit"));
    const FrameInfo second = makeTestFrame(FrameType::Light, QStringLiteral("stores_b.fit"));
    const auto withFrames = [&](IntegrationDataModel& model) {
        model.setOutputDirectory(dir.path());
        model.addFrames(FrameType::Light, {first, second});
    };
    IntegrationDataModel model;
    withFrames(model);
    ASSERT_FALSE(model.registrationGroups().empty());
    ASSERT_FALSE(model.integrationGroups().empty());
    const RegistrationSettings registration = registrationAwayFromItsDefaults(first, second);
    const StackingSettings stacking = localNormalizationAwayFromItsDefaults();
    model.setRegistrationSettings(RegistrationGroupEngine::toMachineGroupKey(model.registrationGroups().begin()->first),
                                  registration);
    model.setStackingSettings(FrameType::Light, model.integrationGroups().front(), stacking);

    {
        SCOPED_TRACE("the model's JSON");
        IntegrationDataModel restored;
        restored.fromJson(model.toJson());
        expectTheFieldsKept(restored, registration, stacking);
    }
    {
        SCOPED_TRACE("the session database");
        IntegrationDatabase database(databasePath(dir));
        ASSERT_TRUE(database.initialize());
        ASSERT_TRUE(database.saveSession(QStringLiteral("stores"), model.toJson()));
        const auto loaded = database.loadSession(QStringLiteral("stores"));
        ASSERT_TRUE(loaded.has_value());
        IntegrationDataModel restored;
        restored.fromJson(*loaded);
        expectTheFieldsKept(restored, registration, stacking);
    }
    {
        SCOPED_TRACE("a preset");
        PresetManager presets(QDir(dir.path()).filePath(QStringLiteral("presets")));
        ASSERT_TRUE(presets.savePreset(QStringLiteral("Stores"), model)) << presets.lastError().toStdString();
        IntegrationDataModel restored;
        withFrames(restored);
        ASSERT_TRUE(presets.loadPreset(QStringLiteral("Stores"), restored)) << presets.lastError().toStdString();
        expectTheFieldsKept(restored, registration, stacking);
    }
    {
        SCOPED_TRACE("a project file");
        const QString path = QDir(dir.path()).filePath(QStringLiteral("stores.json"));
        ProjectManager projects;
        QString error;
        ASSERT_TRUE(projects.saveAs(model.toJson(), path, &error)) << error.toStdString();
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const ImageIntegration::SessionDocumentResult read = ImageIntegration::readSessionDocument(
            QJsonDocument::fromJson(file.readAll()).object(), QStringLiteral("project 'stores'"));
        ASSERT_TRUE(read.ok()) << read.refusal.toStdString();
        IntegrationDataModel restored;
        restored.fromJson(*read.document);
        expectTheFieldsKept(restored, registration, stacking);
    }
}
