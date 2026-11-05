// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration's stored sessions, presets and project files in this version's format: the session database at
/// format 3 and a preset's envelope at format 3, each read back as the model held it; a database, a preset or a project
/// file of another format, or one that states none, refused with the format found and the one this version reads and
/// left byte for byte as it was; and a database of another format set aside beside itself, under a name that states its
/// format, so a new one can be made in its place.

#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/persistence/PresetManager.h"
#include "processes/ImageIntegration/persistence/ProjectManager.h"
#include "processes/ImageIntegration/persistence/SessionDocument.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>

#include <gtest/gtest.h>

namespace
{

const QString kSessionName = QStringLiteral("last_session");

/// A file's bytes' SHA-256, in hex; empty when it cannot be read.
QByteArray fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

/// A model's JSON as this version's model holds it: one light group's stacking settings set away from every default.
QJsonObject modelJson()
{
    StackingSettings stacking;
    stacking.stackMethod = QStringLiteral("Median");
    stacking.rejectionMethod = QStringLiteral("Linear Fit");
    stacking.weighting = QStringLiteral("Noise");
    stacking.starCountScaling = QStringLiteral("Linear");
    stacking.normalizationMode = NormalizationMode::Local;
    stacking.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
    stacking.scaleEstimator = ScaleEstimatorMethod::MAD;
    stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    stacking.drizzle.enabled = true;
    stacking.drizzle.kernel = QStringLiteral("Gaussian");
    stacking.drizzle.cfaPattern = QStringLiteral("GBRG");

    IntegrationDataModel model;
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::InMemory);
    model.setStackingSettings(FrameType::Light, GroupKey{QStringLiteral("4096x3072|-|-|-|Ha|NGC 7000|-")}, stacking);
    return model.toJson();
}

QJsonObject lightStacking(const QJsonObject& document)
{
    return document.value(QStringLiteral("stackingSettings"))
        .toObject()
        .value(QStringLiteral("LIGHT"))
        .toArray()
        .first()
        .toObject()
        .value(QStringLiteral("settings"))
        .toObject();
}

void execute(const QString& path, const QString& statement)
{
    core::SqliteConnection connection(path, QStringLiteral("SessionStoreFormatTestExec"));
    ASSERT_TRUE(connection.open());
    QSqlQuery query(connection.db());
    ASSERT_TRUE(query.exec(statement)) << statement.toStdString();
}

/// A session database as a writer of another format would leave it: the tables this version's database has, the
/// format it states - or none - and one session holding the model's JSON as the model holds it.
void makeDatabase(const QString& path, const std::optional<int> format)
{
    if (format.has_value())
    {
        execute(path, QStringLiteral("CREATE TABLE schema_info (version INTEGER NOT NULL)"));
        execute(path, QStringLiteral("INSERT INTO schema_info(version) VALUES (%1)").arg(*format));
    }
    execute(path, QStringLiteral("CREATE TABLE sessions (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL "
                                 "UNIQUE, data_json TEXT NOT NULL, updated_at INTEGER NOT NULL)"));
    core::SqliteConnection connection(path, QStringLiteral("SessionStoreFormatTestInsert"));
    ASSERT_TRUE(connection.open());
    QSqlQuery insert(connection.db());
    insert.prepare(QStringLiteral("INSERT INTO sessions(name, data_json, updated_at) VALUES (:name, :data, 0)"));
    insert.bindValue(QStringLiteral(":name"), kSessionName);
    insert.bindValue(QStringLiteral(":data"),
                     QString::fromUtf8(QJsonDocument(modelJson()).toJson(QJsonDocument::Compact)));
    ASSERT_TRUE(insert.exec());
}

class SessionDatabaseFormatTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_database = QDir(m_root.path()).filePath(QStringLiteral("image_integration_sessions.db"));
    }

    [[nodiscard]] QStringList filesBeside() const
    {
        return QDir(m_root.path()).entryList(QDir::Files, QDir::Name);
    }

    QTemporaryDir m_root;
    QString m_database;
};

TEST_F(SessionDatabaseFormatTest, ANewDatabaseIsMadeAtFormatThreeAndReadsBackWhatWasSaved)
{
    IntegrationDatabase database(m_database);
    ASSERT_TRUE(database.initialize()) << database.lastError().toStdString();
    EXPECT_EQ(database.schemaVersion(), 3);
    EXPECT_FALSE(database.otherFormat().has_value());

    const QJsonObject model = modelJson();
    ASSERT_TRUE(database.saveSession(kSessionName, model)) << database.lastError().toStdString();
    const SessionLoadResult loaded = database.loadSessionResult(kSessionName);
    ASSERT_TRUE(loaded.model.has_value()) << loaded.error.toStdString();
    EXPECT_EQ(*loaded.model, model);
    database.close();

    // The row holds the stored document: its format stated and every engine-facing choice by its key.
    core::SqliteConnection connection(m_database, QStringLiteral("SessionStoreFormatTestRead"));
    ASSERT_TRUE(connection.openReadOnly());
    QSqlQuery row(connection.db());
    ASSERT_TRUE(row.exec(QStringLiteral("SELECT data_json FROM sessions")) && row.next());
    const QJsonObject stored = QJsonDocument::fromJson(row.value(0).toString().toUtf8()).object();
    EXPECT_EQ(stored.value(QLatin1String(ImageIntegration::kSessionSchemaKey)).toInt(),
              ImageIntegration::kSessionSchemaVersion);
    EXPECT_EQ(lightStacking(stored).value(QStringLiteral("rejectionMethod")).toString(),
              QStringLiteral("linear-fit-clipping"));
    EXPECT_EQ(lightStacking(stored).value(QStringLiteral("normalizationMode")).toString(), QStringLiteral("local"));
}

TEST_F(SessionDatabaseFormatTest, ADatabaseOfAnotherFormatIsRefusedAndLeftByteForByte)
{
    for (const int format : {2, 4})
    {
        ASSERT_TRUE(!QFileInfo::exists(m_database) || QFile::remove(m_database));
        makeDatabase(m_database, format);
        const QByteArray before = fileSha256(m_database);
        {
            IntegrationDatabase database(m_database);
            EXPECT_FALSE(database.initialize()) << format;
            const QString error = database.lastError();
            EXPECT_TRUE(error.contains(QStringLiteral("written in format %1").arg(format))) << error.toStdString();
            EXPECT_TRUE(error.contains(QStringLiteral("this version of AfterNight reads format 3")))
                << error.toStdString();
            EXPECT_TRUE(error.contains(QDir::toNativeSeparators(m_database))) << error.toStdString();
            EXPECT_EQ(database.otherFormat(), std::optional<int>(format));
            EXPECT_FALSE(database.saveSession(QStringLiteral("other"), modelJson()));
            database.close();
        }
        EXPECT_EQ(fileSha256(m_database), before) << format;
        EXPECT_EQ(filesBeside(), QStringList{QFileInfo(m_database).fileName()}) << format;
    }
}

TEST_F(SessionDatabaseFormatTest, ADatabaseThatStatesNoFormatIsRefusedAndLeftByteForByte)
{
    makeDatabase(m_database, std::nullopt);
    const QByteArray before = fileSha256(m_database);
    {
        IntegrationDatabase database(m_database);
        EXPECT_FALSE(database.initialize());
        EXPECT_TRUE(database.lastError().contains(QStringLiteral("states no format version")))
            << database.lastError().toStdString();
        EXPECT_EQ(database.otherFormat(), std::optional<int>(0));
        database.close();
    }
    EXPECT_EQ(fileSha256(m_database), before);
    EXPECT_EQ(filesBeside(), QStringList{QFileInfo(m_database).fileName()});
}

TEST_F(SessionDatabaseFormatTest, ADatabaseOfAnotherFormatIsSetAsideAndANewOneMadeInItsPlace)
{
    makeDatabase(m_database, 2);
    const QByteArray original = fileSha256(m_database);

    IntegrationDatabase database(m_database);
    ASSERT_FALSE(database.initialize());
    const QString setAside = database.setAside();
    EXPECT_EQ(setAside, QDir(m_root.path()).filePath(QStringLiteral("image_integration_sessions.format2.db")));
    EXPECT_EQ(fileSha256(setAside), original);
    EXPECT_FALSE(QFileInfo::exists(m_database));

    ASSERT_TRUE(database.initialize()) << database.lastError().toStdString();
    EXPECT_EQ(database.schemaVersion(), 3);
    EXPECT_TRUE(database.listSessions().isEmpty());
    database.close();

    // A second store of that format is set aside beside the first, which keeps its name and its bytes.
    ASSERT_TRUE(QFile::remove(m_database));
    makeDatabase(m_database, 2);
    IntegrationDatabase again(m_database);
    ASSERT_FALSE(again.initialize());
    EXPECT_EQ(again.setAside(),
              QDir(m_root.path()).filePath(QStringLiteral("image_integration_sessions.format2-2.db")));
    EXPECT_EQ(fileSha256(setAside), original);
}

TEST_F(SessionDatabaseFormatTest, AStoredSessionOfAnotherFormatIsRefusedAndKept)
{
    {
        IntegrationDatabase database(m_database);
        ASSERT_TRUE(database.initialize());
        ASSERT_TRUE(database.saveSession(kSessionName, modelJson()));
    }
    // The model's JSON stored as the row itself states no format.
    {
        core::SqliteConnection connection(m_database, QStringLiteral("SessionStoreFormatTestUpdate"));
        ASSERT_TRUE(connection.open());
        QSqlQuery update(connection.db());
        update.prepare(QStringLiteral("UPDATE sessions SET data_json = :data"));
        update.bindValue(QStringLiteral(":data"),
                         QString::fromUtf8(QJsonDocument(modelJson()).toJson(QJsonDocument::Compact)));
        ASSERT_TRUE(update.exec());
    }
    const QByteArray before = fileSha256(m_database);

    IntegrationDatabase database(m_database);
    ASSERT_TRUE(database.initialize());
    const SessionLoadResult loaded = database.loadSessionResult(kSessionName);
    EXPECT_FALSE(loaded.model.has_value());
    EXPECT_TRUE(loaded.error.contains(QStringLiteral("session 'last_session' states no format version")))
        << loaded.error.toStdString();
    EXPECT_TRUE(loaded.error.contains(QStringLiteral("this version of AfterNight reads format 3")))
        << loaded.error.toStdString();
    database.close();
    EXPECT_EQ(fileSha256(m_database), before);
}

TEST_F(SessionDatabaseFormatTest, AValueNoTableKnowsRefusesTheSave)
{
    IntegrationDatabase database(m_database);
    ASSERT_TRUE(database.initialize());
    QJsonObject unknown = modelJson();
    QJsonObject root = unknown.value(QStringLiteral("stackingSettings")).toObject();
    QJsonArray lights = root.value(QStringLiteral("LIGHT")).toArray();
    QJsonObject record = lights.first().toObject();
    QJsonObject settings = record.value(QStringLiteral("settings")).toObject();
    settings.insert(QStringLiteral("weighting"), QStringLiteral("Brightness"));
    record.insert(QStringLiteral("settings"), settings);
    lights.replace(0, record);
    root.insert(QStringLiteral("LIGHT"), lights);
    unknown.insert(QStringLiteral("stackingSettings"), root);
    const DatabaseResult refused = database.saveSessionResult(QStringLiteral("unknown"), unknown);
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(refused.errorMessage.contains(QStringLiteral("'Brightness'"))) << refused.errorMessage.toStdString();
    EXPECT_FALSE(database.loadSession(QStringLiteral("unknown")).has_value());
}

class PresetFormatTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_presets = QDir(m_root.path()).filePath(QStringLiteral("presets"));
        ASSERT_TRUE(QDir().mkpath(m_presets));
        m_preset = QDir(m_presets).filePath(QStringLiteral("Narrowband.json"));
    }

    void writePreset(const QJsonObject& content) const
    {
        QFile file(m_preset);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray bytes = QJsonDocument(content).toJson();
        ASSERT_EQ(file.write(bytes), bytes.size());
    }

    QTemporaryDir m_root;
    QString m_presets;
    QString m_preset;
};

TEST_F(PresetFormatTest, APresetIsAnEnvelopeAtFormatThreeThatReadsBackTheSameConfiguration)
{
    PresetManager presets(m_presets);
    IntegrationDataModel model;
    model.fromJson(modelJson());
    ASSERT_TRUE(presets.savePreset(QStringLiteral("Narrowband"), model)) << presets.lastError().toStdString();
    QFile file(m_preset);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject envelope = QJsonDocument::fromJson(file.readAll()).object();
    EXPECT_EQ(envelope.value(QStringLiteral("presetSchema")).toInt(), ImageIntegration::kSessionSchemaVersion);
    EXPECT_EQ(envelope.value(QStringLiteral("configuration"))
                  .toObject()
                  .value(QLatin1String(ImageIntegration::kSessionSchemaKey))
                  .toInt(),
              ImageIntegration::kSessionSchemaVersion);

    IntegrationDataModel reloaded;
    ASSERT_TRUE(presets.loadPreset(QStringLiteral("Narrowband"), reloaded)) << presets.lastError().toStdString();
    EXPECT_EQ(reloaded.toJson(), model.toJson());
}

TEST_F(PresetFormatTest, APresetThatStatesNoFormatIsRefusedAndLeftByteForByte)
{
    // The configuration itself, with no envelope around it.
    writePreset(modelJson());
    const QByteArray before = fileSha256(m_preset);
    PresetManager presets(m_presets);
    IntegrationDataModel model;
    EXPECT_FALSE(presets.buildAppliedState(QStringLiteral("Narrowband"), model).has_value());
    const QString error = presets.lastError();
    EXPECT_TRUE(error.contains(QStringLiteral("preset 'Narrowband'"))) << error.toStdString();
    EXPECT_TRUE(error.contains(QDir::toNativeSeparators(m_preset))) << error.toStdString();
    EXPECT_TRUE(error.contains(QStringLiteral("states no format version"))) << error.toStdString();
    EXPECT_TRUE(error.contains(QStringLiteral("this version of AfterNight reads format 3"))) << error.toStdString();
    EXPECT_EQ(fileSha256(m_preset), before);
    EXPECT_EQ(QDir(m_presets).entryList(QDir::Files), QStringList{QStringLiteral("Narrowband.json")});
}

TEST_F(PresetFormatTest, APresetOfAnotherFormatIsRefusedAndLeftByteForByte)
{
    for (const int format : {2, 4})
    {
        writePreset(
            QJsonObject{{QStringLiteral("presetSchema"), format}, {QStringLiteral("configuration"), modelJson()}});
        const QByteArray before = fileSha256(m_preset);
        PresetManager presets(m_presets);
        IntegrationDataModel model;
        EXPECT_FALSE(presets.buildAppliedState(QStringLiteral("Narrowband"), model).has_value()) << format;
        const QString error = presets.lastError();
        EXPECT_TRUE(error.contains(QStringLiteral("written in format %1").arg(format))) << error.toStdString();
        EXPECT_TRUE(error.contains(QStringLiteral("this version of AfterNight reads format 3"))) << error.toStdString();
        EXPECT_EQ(fileSha256(m_preset), before) << format;
        EXPECT_EQ(QDir(m_presets).entryList(QDir::Files), QStringList{QStringLiteral("Narrowband.json")}) << format;
    }
}

TEST_F(PresetFormatTest, APresetWhoseConfigurationIsOfAnotherFormatIsRefused)
{
    QJsonObject configuration = modelJson();
    configuration.insert(QLatin1String(ImageIntegration::kSessionSchemaKey), 2);
    writePreset(QJsonObject{{QStringLiteral("presetSchema"), ImageIntegration::kSessionSchemaVersion},
                            {QStringLiteral("configuration"), configuration}});
    const QByteArray before = fileSha256(m_preset);
    PresetManager presets(m_presets);
    IntegrationDataModel model;
    EXPECT_FALSE(presets.buildAppliedState(QStringLiteral("Narrowband"), model).has_value());
    EXPECT_TRUE(presets.lastError().contains(QStringLiteral("written in format 2")))
        << presets.lastError().toStdString();
    EXPECT_EQ(fileSha256(m_preset), before);
}

class ProjectFileFormatTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_project = QDir(m_root.path()).filePath(QStringLiteral("M31 mosaic.json"));
    }

    void writeProject(const QByteArray& bytes) const
    {
        QFile file(m_project);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }

    QTemporaryDir m_root;
    QString m_project;
};

TEST_F(ProjectFileFormatTest, AProjectIsWrittenAtFormatThreeAndOpensAsTheModelHeldIt)
{
    const QJsonObject model = modelJson();
    ProjectManager projects;
    QString error;
    ASSERT_TRUE(projects.saveAs(model, m_project, &error)) << error.toStdString();
    QFile file(m_project);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject stored = QJsonDocument::fromJson(file.readAll()).object();
    EXPECT_EQ(stored.value(QLatin1String(ImageIntegration::kSessionSchemaKey)).toInt(),
              ImageIntegration::kSessionSchemaVersion);

    const ProjectReadResult read = ProjectManager::readProject(m_project);
    ASSERT_TRUE(read.model.has_value()) << read.refusal.toStdString();
    EXPECT_EQ(*read.model, model);
}

TEST_F(ProjectFileFormatTest, AProjectOfAnotherFormatIsRefusedNamingTheFileAndBothFormats)
{
    QJsonObject older = modelJson();
    older.insert(QLatin1String(ImageIntegration::kSessionSchemaKey), 2);
    writeProject(QJsonDocument(older).toJson());
    const QByteArray before = fileSha256(m_project);

    const ProjectReadResult read = ProjectManager::readProject(m_project);
    EXPECT_FALSE(read.model.has_value());
    EXPECT_TRUE(read.refusal.contains(QDir::toNativeSeparators(m_project))) << read.refusal.toStdString();
    EXPECT_TRUE(read.refusal.contains(QStringLiteral("written in format 2"))) << read.refusal.toStdString();
    EXPECT_TRUE(read.refusal.contains(QStringLiteral("this version of AfterNight reads format 3")))
        << read.refusal.toStdString();
    EXPECT_EQ(fileSha256(m_project), before);

    // The model's JSON saved as it is states no format.
    writeProject(QJsonDocument(modelJson()).toJson());
    const ProjectReadResult unstated = ProjectManager::readProject(m_project);
    EXPECT_FALSE(unstated.model.has_value());
    EXPECT_TRUE(unstated.refusal.contains(QStringLiteral("states no format version")))
        << unstated.refusal.toStdString();
}

TEST_F(ProjectFileFormatTest, AFileThatIsNotAJsonProjectIsRefusedNamingIt)
{
    const QString database = QDir(m_root.path()).filePath(QStringLiteral("project.db"));
    execute(database, QStringLiteral("CREATE TABLE sessions (name TEXT)"));
    const ProjectReadResult read = ProjectManager::readProject(database);
    EXPECT_FALSE(read.model.has_value());
    EXPECT_TRUE(read.refusal.contains(QDir::toNativeSeparators(database))) << read.refusal.toStdString();
    EXPECT_TRUE(read.refusal.contains(QStringLiteral("JSON"))) << read.refusal.toStdString();

    writeProject(QByteArrayLiteral("not a document"));
    const ProjectReadResult unreadable = ProjectManager::readProject(m_project);
    EXPECT_FALSE(unreadable.model.has_value());
    EXPECT_TRUE(unreadable.refusal.contains(QDir::toNativeSeparators(m_project))) << unreadable.refusal.toStdString();
}

} // namespace
