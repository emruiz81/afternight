// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/RegistrationWorkAnalyzer.h"
#include "processes/core/RegistrationMetricsWriter.h"

namespace
{
class ScopedSqliteDb
{
public:
    explicit ScopedSqliteDb(const QString& path)
        : m_connectionName(QStringLiteral("reg_work_%1").arg(s_counter.fetch_add(1, std::memory_order_relaxed))),
          m_db(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName))
    {
        m_db.setDatabaseName(path);
        m_opened = m_db.open();
    }

    ~ScopedSqliteDb()
    {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    [[nodiscard]] QSqlDatabase& db() noexcept
    {
        return m_db;
    }
    [[nodiscard]] bool isOpen() const noexcept
    {
        return m_opened;
    }

private:
    inline static std::atomic<int> s_counter{0};

    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_opened = false;
};

void createRegistrationTables(QSqlDatabase& db)
{
    QSqlQuery q(db);
    ASSERT_TRUE(
        q.exec(QStringLiteral("CREATE TABLE registration_group_state ("
                              "group_key TEXT, settings_version INTEGER, detection_hash TEXT, registration_hash TEXT, "
                              "reference_frame_id TEXT, light_files_hash TEXT, calibration_hash TEXT)")));
    ASSERT_TRUE(q.exec(QStringLiteral("CREATE TABLE star_detection_cache ("
                                      "group_key TEXT, detection_hash TEXT, frame_id TEXT, input_path TEXT, "
                                      "input_mtime INTEGER, input_size_bytes INTEGER, "
                                      "input_modified_at_ms INTEGER, input_identity_token TEXT, "
                                      "updated_at INTEGER)")));
    ASSERT_TRUE(q.exec(QStringLiteral("CREATE TABLE registration_metrics ("
                                      "group_key TEXT, settings_version INTEGER, frame_id TEXT, succeeded INTEGER, "
                                      "updated_at INTEGER)")));
}

WorkflowManifest::RegistrationGroup makeGroup(const QString& framePath)
{
    WorkflowManifest::RegistrationGroup group;
    group.groupKey = GroupKey{QStringLiteral("reg|group")};
    group.lightFiles = {FrameId(framePath)};
    group.settings.version = 1;
    group.referenceFrame = FrameId(framePath);
    return group;
}

void insertMatchingState(QSqlDatabase& db, const WorkflowManifest::RegistrationGroup& group, bool succeeded,
                         const QString& calibrationHash = {})
{
    using ImageIntegration::RegistrationHashUtils::hashFrameIdList;
    using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
    using ImageIntegration::RegistrationHashUtils::registrationTransformHash;

    const QString detectionHash = registrationDetectionHash(group.settings);
    const QString registrationHash = registrationTransformHash(group.settings);
    const QString filesHash = hashFrameIdList(group.lightFiles);
    const QString referenceFrame = group.referenceFrame->canonicalPath();
    const QString frameId = group.lightFiles.front().canonicalPath();
    const QFileInfo inputInfo(frameId);
    const qint64 mtime = inputInfo.lastModified().toSecsSinceEpoch();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO registration_group_state (group_key, settings_version, detection_hash, "
                             "registration_hash, reference_frame_id, light_files_hash, calibration_hash) "
                             "VALUES (:group_key, :version, :det, :reg, :ref, :files, :calibration)"));
    q.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
    q.bindValue(QStringLiteral(":version"), group.settings.version);
    q.bindValue(QStringLiteral(":det"), detectionHash);
    q.bindValue(QStringLiteral(":reg"), registrationHash);
    q.bindValue(QStringLiteral(":ref"), referenceFrame);
    q.bindValue(QStringLiteral(":files"), filesHash);
    q.bindValue(QStringLiteral(":calibration"), calibrationHash);
    ASSERT_TRUE(q.exec());

    q.prepare(QStringLiteral("INSERT INTO star_detection_cache (group_key, detection_hash, frame_id, input_path, "
                             "input_mtime, input_size_bytes, input_modified_at_ms, input_identity_token, updated_at) "
                             "VALUES (:group_key, :det, :frame_id, :input_path, :mtime, :size_bytes, "
                             ":modified_at_ms, :identity_token, 100)"));
    q.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
    q.bindValue(QStringLiteral(":det"), detectionHash);
    q.bindValue(QStringLiteral(":frame_id"), frameId);
    q.bindValue(QStringLiteral(":input_path"), frameId);
    q.bindValue(QStringLiteral(":mtime"), mtime);
    q.bindValue(QStringLiteral(":size_bytes"), inputInfo.size());
    q.bindValue(QStringLiteral(":modified_at_ms"), inputInfo.lastModified().toMSecsSinceEpoch());
    q.bindValue(QStringLiteral(":identity_token"), ArtifactTracker::fileIdentityForPath(frameId));
    ASSERT_TRUE(q.exec());

    q.prepare(QStringLiteral("INSERT INTO registration_metrics (group_key, settings_version, frame_id, succeeded, "
                             "updated_at) VALUES (:group_key, :version, :frame_id, :succeeded, 100)"));
    q.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
    q.bindValue(QStringLiteral(":version"), group.settings.version);
    q.bindValue(QStringLiteral(":frame_id"), frameId);
    q.bindValue(QStringLiteral(":succeeded"), succeeded ? 1 : 0);
    ASSERT_TRUE(q.exec());
}

void insertMatchingStateForFrames(QSqlDatabase& db, const WorkflowManifest::RegistrationGroup& group,
                                  const std::vector<FrameId>& persistedFrames)
{
    using ImageIntegration::RegistrationHashUtils::hashFrameIdList;
    using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
    using ImageIntegration::RegistrationHashUtils::registrationTransformHash;

    const QString detectionHash = registrationDetectionHash(group.settings);
    const QString registrationHash = registrationTransformHash(group.settings);
    const QString filesHash = hashFrameIdList(persistedFrames);
    const QString referenceFrame = group.referenceFrame->canonicalPath();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO registration_group_state (group_key, settings_version, detection_hash, "
                             "registration_hash, reference_frame_id, light_files_hash, calibration_hash) "
                             "VALUES (:group_key, :version, :det, :reg, :ref, :files, '')"));
    q.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
    q.bindValue(QStringLiteral(":version"), group.settings.version);
    q.bindValue(QStringLiteral(":det"), detectionHash);
    q.bindValue(QStringLiteral(":reg"), registrationHash);
    q.bindValue(QStringLiteral(":ref"), referenceFrame);
    q.bindValue(QStringLiteral(":files"), filesHash);
    ASSERT_TRUE(q.exec());

    for (const auto& frame : persistedFrames)
    {
        const QString frameId = frame.canonicalPath();
        const QFileInfo inputInfo(frameId);
        const qint64 mtime = inputInfo.lastModified().toSecsSinceEpoch();

        q.prepare(QStringLiteral("INSERT INTO star_detection_cache (group_key, detection_hash, frame_id, input_path, "
                                 "input_mtime, input_size_bytes, input_modified_at_ms, input_identity_token, "
                                 "updated_at) VALUES (:group_key, :det, :frame_id, :input_path, :mtime, :size_bytes, "
                                 ":modified_at_ms, :identity_token, 100)"));
        q.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
        q.bindValue(QStringLiteral(":det"), detectionHash);
        q.bindValue(QStringLiteral(":frame_id"), frameId);
        q.bindValue(QStringLiteral(":input_path"), frameId);
        q.bindValue(QStringLiteral(":mtime"), mtime);
        q.bindValue(QStringLiteral(":size_bytes"), inputInfo.size());
        q.bindValue(QStringLiteral(":modified_at_ms"), inputInfo.lastModified().toMSecsSinceEpoch());
        q.bindValue(QStringLiteral(":identity_token"), ArtifactTracker::fileIdentityForPath(frameId));
        ASSERT_TRUE(q.exec());

        q.prepare(QStringLiteral("INSERT INTO registration_metrics (group_key, settings_version, frame_id, succeeded, "
                                 "updated_at) VALUES (:group_key, :version, :frame_id, 1, 100)"));
        q.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
        q.bindValue(QStringLiteral(":version"), group.settings.version);
        q.bindValue(QStringLiteral(":frame_id"), frameId);
        ASSERT_TRUE(q.exec());
    }
}

void writeTestFile(const QString& path, const QByteArray& bytes = QByteArray("frame"))
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(bytes), bytes.size());
}

void updateCachedInputFacts(QSqlDatabase& db, const QString& inputPath)
{
    const QFileInfo info(inputPath);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE star_detection_cache SET input_path = :input_path, input_mtime = :mtime, "
                                 "input_size_bytes = :size_bytes, input_modified_at_ms = :modified_at_ms, "
                                 "input_identity_token = :identity_token"));
    query.bindValue(QStringLiteral(":input_path"), inputPath);
    query.bindValue(QStringLiteral(":mtime"), info.lastModified().toSecsSinceEpoch());
    query.bindValue(QStringLiteral(":size_bytes"), info.size());
    query.bindValue(QStringLiteral(":modified_at_ms"), info.lastModified().toMSecsSinceEpoch());
    query.bindValue(QStringLiteral(":identity_token"), ArtifactTracker::fileIdentityForPath(inputPath));
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();
}

void seedStageCompatibleSnapshotState(const WorkflowManifest& manifest, const bool plain_registration_hash)
{
    using ImageIntegration::RegistrationHashUtils::effectiveRegistrationHash;
    using ImageIntegration::RegistrationHashUtils::hashFrameIdList;
    using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
    using ImageIntegration::RegistrationHashUtils::registrationTransformHash;

    ASSERT_EQ(manifest.registrationGroups.size(), 1u);
    const auto& group = manifest.registrationGroups.front();
    const QString database_path = QDir(manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
    an::ui::RegistrationMetricsWriter writer(database_path, QStringLiteral("RegistrationWorkAnalyzerSnapshotSeed"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationGroupState state;
    state.groupKey = group.groupKey.toString();
    state.settingsVersion = group.settings.version;
    state.detectionHash = registrationDetectionHash(group.settings);
    state.registrationHash = plain_registration_hash
                                 ? registrationTransformHash(group.settings)
                                 : effectiveRegistrationHash(group.settings, manifest.plateSolve, QString());
    state.referenceFrameId =
        group.settings.autoReferenceDuringRegistration ? QString() : group.referenceFrame.value().canonicalPath();
    state.lightFilesHash = hashFrameIdList(group.lightFiles);
    state.calibrationHash =
        computeLightsCalibrationHashForSubset(manifest.lightsCalibration, group.lightFiles, nullptr);
    state.updatedAt = 1000;
    ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();

    std::vector<an::ui::RegistrationMetricRecord> metrics;
    std::vector<an::ui::RegistrationStarCacheEntry> stars;
    metrics.reserve(group.lightFiles.size());
    stars.reserve(group.lightFiles.size());
    for (const auto& frame : group.lightFiles)
    {
        an::ui::RegistrationMetricRecord metric;
        metric.frameId = frame;
        metric.groupKey = state.groupKey;
        metric.settingsVersion = state.settingsVersion;
        metric.metrics.succeeded = true;
        metric.updatedAt = state.updatedAt;
        metrics.push_back(std::move(metric));

        an::ui::RegistrationStarCacheEntry cache;
        cache.frameId = frame;
        cache.groupKey = state.groupKey;
        cache.detectionHash = state.detectionHash;
        cache.inputPath = frame.canonicalPath();
        cache.inputMtime = QFileInfo(frame.canonicalPath()).lastModified().toSecsSinceEpoch();
        cache.inputSizeBytes = QFileInfo(frame.canonicalPath()).size();
        cache.inputModifiedAtMs = QFileInfo(frame.canonicalPath()).lastModified().toMSecsSinceEpoch();
        cache.inputIdentityToken = ArtifactTracker::fileIdentityForPath(frame.canonicalPath());
        cache.starsBlob = QByteArray("cache");
        cache.updatedAt = state.updatedAt;
        stars.push_back(std::move(cache));
    }
    ASSERT_TRUE(writer.upsertMetricsForGroup(state.groupKey, state.settingsVersion, metrics, &error))
        << error.toStdString();
    ASSERT_TRUE(writer.upsertStarCacheForGroup(state.groupKey, state.detectionHash, stars, &error))
        << error.toStdString();
}
} // namespace

TEST(RegistrationWorkAnalyzer, RequiresWorkWhenNoPreviousStateExists)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString framePath = QDir(dir.path()).filePath(QStringLiteral("light.fit"));
    QFile frame(framePath);
    ASSERT_TRUE(frame.open(QIODevice::WriteOnly | QIODevice::Truncate));
    frame.write("frame");
    frame.close();

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const auto analysis = analyzeRegistrationWork(db.db(), makeGroup(framePath));
    EXPECT_TRUE(analysis.needsWork);
    ASSERT_EQ(analysis.reasons.size(), 1u);
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::NoPreviousState);
}

TEST(RegistrationWorkAnalyzer, ReturnsNoWorkWhenStateMatchesCurrentInputs)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString framePath = QDir(dir.path()).filePath(QStringLiteral("light.fit"));
    QFile frame(framePath);
    ASSERT_TRUE(frame.open(QIODevice::WriteOnly | QIODevice::Truncate));
    frame.write("frame");
    frame.close();

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const auto group = makeGroup(framePath);
    insertMatchingState(db.db(), group, true);

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_FALSE(analysis.needsWork);
    EXPECT_TRUE(analysis.reasons.empty());
}

TEST(RegistrationWorkAnalyzer, TreatsUnavailableCachedInputIdentityAsChanged)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("identity_unavailable.fit"));
    writeTestFile(frame_path);

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const auto group = makeGroup(frame_path);
    insertMatchingState(db.db(), group, true);
    QSqlQuery query(db.db());
    ASSERT_TRUE(query.exec(QStringLiteral("UPDATE star_detection_cache SET input_identity_token = ''")))
        << query.lastError().text().toStdString();

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_TRUE(analysis.needsWork);
    ASSERT_EQ(analysis.reasons.size(), 1u);
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::ChangedInputs);
}

TEST(RegistrationWorkAnalyzer, DetectsChangedCachedRegistrationInputInsteadOfOriginalLight)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("light.fit"));
    const QString calibrated_path = QDir(dir.path()).filePath(QStringLiteral("calibrated.fit"));
    writeTestFile(frame_path);
    writeTestFile(calibrated_path, QByteArray("calibrated-v1"));

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const auto group = makeGroup(frame_path);
    insertMatchingState(db.db(), group, true);
    updateCachedInputFacts(db.db(), calibrated_path);

    writeTestFile(calibrated_path, QByteArray("calibrated-v2-with-a-different-size"));
    QFile calibrated(calibrated_path);
    ASSERT_TRUE(calibrated.open(QIODevice::ReadWrite));
    ASSERT_TRUE(calibrated.setFileTime(QDateTime::currentDateTime().addSecs(2), QFileDevice::FileModificationTime));
    calibrated.close();

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_TRUE(analysis.needsWork);
    ASSERT_FALSE(analysis.reasons.empty());
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::ChangedInputs);
}

TEST(RegistrationWorkAnalyzer, DetectsSameSizeSameMtimeCachedInputReplacement)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString framePath = QDir(dir.path()).filePath(QStringLiteral("identity_light.fit"));
    const QString replacementPath = QDir(dir.path()).filePath(QStringLiteral("identity_replacement.fit"));
    writeTestFile(framePath, QByteArray("first"));
    const QString originalIdentity = ArtifactTracker::fileIdentityForPath(framePath);
    if (originalIdentity.isEmpty())
    {
        GTEST_SKIP() << "Native file identity is unavailable on this platform.";
    }

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());
    const auto group = makeGroup(framePath);
    insertMatchingState(db.db(), group, true);

    const QDateTime originalMtime = QFileInfo(framePath).lastModified();
    writeTestFile(replacementPath, QByteArray("other"));
    QFile replacement(replacementPath);
    ASSERT_TRUE(replacement.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replacement.setFileTime(originalMtime, QFileDevice::FileModificationTime));
    replacement.close();
    ASSERT_TRUE(QFile::remove(framePath));
    ASSERT_TRUE(QFile::rename(replacementPath, framePath));
    ASSERT_EQ(QFileInfo(framePath).size(), 5);
    ASSERT_EQ(QFileInfo(framePath).lastModified().toMSecsSinceEpoch(), originalMtime.toMSecsSinceEpoch());
    ASSERT_NE(ArtifactTracker::fileIdentityForPath(framePath), originalIdentity);

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_TRUE(analysis.needsWork);
    ASSERT_FALSE(analysis.reasons.empty());
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::ChangedInputs);
}

TEST(RegistrationWorkAnalyzer, DetectsDeletedCachedRegistrationInput)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("light.fit"));
    const QString calibrated_path = QDir(dir.path()).filePath(QStringLiteral("calibrated.fit"));
    writeTestFile(frame_path);
    writeTestFile(calibrated_path, QByteArray("calibrated"));

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const auto group = makeGroup(frame_path);
    insertMatchingState(db.db(), group, true);
    updateCachedInputFacts(db.db(), calibrated_path);
    ASSERT_TRUE(QFile::remove(calibrated_path));

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_TRUE(analysis.needsWork);
    ASSERT_FALSE(analysis.reasons.empty());
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::ChangedInputs);
}

TEST(RegistrationWorkAnalyzer, AGroupContractWithoutAManifestDoesNotCompareUnavailableCalibrationHash)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString framePath = QDir(dir.path()).filePath(QStringLiteral("calibrated_light.fit"));
    writeTestFile(framePath);

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const auto group = makeGroup(framePath);
    insertMatchingState(db.db(), group, true, QStringLiteral("persisted-calibration-hash"));

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_FALSE(analysis.needsWork);
    EXPECT_TRUE(analysis.reasons.empty());
}

TEST(RegistrationWorkAnalyzer, ReportsFailedMetricsAsRequiredWork)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString framePath = QDir(dir.path()).filePath(QStringLiteral("light.fit"));
    QFile frame(framePath);
    ASSERT_TRUE(frame.open(QIODevice::WriteOnly | QIODevice::Truncate));
    frame.write("frame");
    frame.close();

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const auto group = makeGroup(framePath);
    insertMatchingState(db.db(), group, false);

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_TRUE(analysis.needsWork);
    ASSERT_EQ(analysis.reasons.size(), 1u);
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::FailedMetrics);
    EXPECT_EQ(analysis.reasons.front().count, 1);
}

TEST(RegistrationWorkAnalyzer, IgnoredFramesDoNotForceRequiredWork)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString framePath = QDir(dir.path()).filePath(QStringLiteral("light.fit"));
    QFile frame(framePath);
    ASSERT_TRUE(frame.open(QIODevice::WriteOnly | QIODevice::Truncate));
    frame.write("frame");
    frame.close();

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const auto group = makeGroup(framePath);
    insertMatchingState(db.db(), group, false);

    RegistrationWorkCurrentState currentState;
    currentState.groupKey = group.groupKey.toString();
    currentState.settingsVersion = group.settings.version;
    currentState.detectionHash = ImageIntegration::RegistrationHashUtils::registrationDetectionHash(group.settings);
    currentState.registrationHash = ImageIntegration::RegistrationHashUtils::registrationTransformHash(group.settings);
    currentState.referenceFrameId = group.referenceFrame->canonicalPath();
    currentState.lightFilesHash = ImageIntegration::RegistrationHashUtils::hashFrameIdList(group.lightFiles);
    currentState.lightFiles = group.lightFiles;
    QString aliasIgnoredFrameId = group.lightFiles.front().canonicalPath();
    aliasIgnoredFrameId.replace(QLatin1Char('/'), QLatin1Char('\\'));
    ASSERT_EQ(FrameId(aliasIgnoredFrameId), group.lightFiles.front());
    ASSERT_NE(aliasIgnoredFrameId, group.lightFiles.front().canonicalPath());
    currentState.ignoredFrameIds.insert(aliasIgnoredFrameId);

    const auto analysis = analyzeRegistrationWork(db.db(), currentState);
    EXPECT_FALSE(analysis.needsWork);
    EXPECT_TRUE(analysis.reasons.empty());
}

TEST(RegistrationWorkAnalyzer, FrameListChangeKeepsPerFrameDiagnostics)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frameAPath = QDir(dir.path()).filePath(QStringLiteral("light_a.fit"));
    QFile frameA(frameAPath);
    ASSERT_TRUE(frameA.open(QIODevice::WriteOnly | QIODevice::Truncate));
    frameA.write("frame-a");
    frameA.close();

    const QString frameBPath = QDir(dir.path()).filePath(QStringLiteral("light_b.fit"));
    QFile frameB(frameBPath);
    ASSERT_TRUE(frameB.open(QIODevice::WriteOnly | QIODevice::Truncate));
    frameB.write("frame-b");
    frameB.close();

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    WorkflowManifest::RegistrationGroup group = makeGroup(frameAPath);
    group.lightFiles.push_back(FrameId(frameBPath));

    insertMatchingStateForFrames(db.db(), group, {FrameId(frameAPath)});

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_TRUE(analysis.needsWork);

    ASSERT_GE(analysis.reasons.size(), 3u);
    EXPECT_EQ(analysis.reasons[0].code, RegistrationWorkReasonCode::FrameListChanged);
    EXPECT_EQ(analysis.reasons[1].code, RegistrationWorkReasonCode::MissingCachedStars);
    EXPECT_EQ(analysis.reasons[1].count, 1);
    EXPECT_EQ(analysis.reasons[2].code, RegistrationWorkReasonCode::MissingMetrics);
    EXPECT_EQ(analysis.reasons[2].count, 1);
}

TEST(RegistrationWorkAnalyzer, WindowsAliasIdentityRowsUseNewestStarAndMetricPayloads)
{
    ScopedSqliteDb db(QStringLiteral(":memory:"));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    const QString current_path = QStringLiteral("C:/Current/Analyzer/Light.fit");
    const QString alias_path = QStringLiteral("c:\\current\\analyzer\\light.fit");
    const auto group = makeGroup(current_path);
    insertMatchingState(db.db(), group, false);

    QSqlQuery query(db.db());
    query.prepare(QStringLiteral("DELETE FROM star_detection_cache WHERE frame_id = :frame_id"));
    query.bindValue(QStringLiteral(":frame_id"), FrameId(current_path).canonicalPath());
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();

    query.prepare(QStringLiteral("INSERT INTO star_detection_cache "
                                 "(group_key, detection_hash, frame_id, input_path, input_mtime, updated_at) "
                                 "VALUES (:group_key, :detection_hash, :frame_id, :input_path, 0, 200)"));
    query.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
    query.bindValue(QStringLiteral(":detection_hash"),
                    ImageIntegration::RegistrationHashUtils::registrationDetectionHash(group.settings));
    query.bindValue(QStringLiteral(":frame_id"), alias_path);
    query.bindValue(QStringLiteral(":input_path"), current_path);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();

    query.prepare(QStringLiteral("INSERT INTO registration_metrics "
                                 "(group_key, settings_version, frame_id, succeeded, updated_at) "
                                 "VALUES (:group_key, :settings_version, :frame_id, 1, 200)"));
    query.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
    query.bindValue(QStringLiteral(":settings_version"), group.settings.version);
    query.bindValue(QStringLiteral(":frame_id"), alias_path);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();

    const auto analysis = analyzeRegistrationWork(db.db(), group);
    EXPECT_TRUE(analysis.needsWork);
    ASSERT_EQ(analysis.reasons.size(), 1u);
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::ChangedInputs);
}

TEST(RegistrationWorkAnalyzer, SnapshotManifestContractReusesEffectiveHashAndAutoReferenceState)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("effective_auto_reference.fit"));
    writeTestFile(frame_path);

    WorkflowManifest manifest;
    manifest.outputDirectory = dir.path();
    auto group = makeGroup(frame_path);
    group.settings.autoReferenceDuringRegistration = true;
    manifest.registrationGroups.push_back(group);
    seedStageCompatibleSnapshotState(manifest, false);

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    const auto analyses = analyzeRegistrationGroupsWork(*snapshot, manifest);
    ASSERT_EQ(analyses.size(), 1u);
    EXPECT_FALSE(analyses.front().analysis.needsWork);
    EXPECT_TRUE(analyses.front().analysis.reasons.empty());
}

TEST(RegistrationWorkAnalyzer, SnapshotManifestContractDetectsDeletedCachedRegistrationInput)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("snapshot_original.fit"));
    const QString calibrated_path = QDir(dir.path()).filePath(QStringLiteral("snapshot_calibrated.fit"));
    writeTestFile(frame_path);
    writeTestFile(calibrated_path, QByteArray("calibrated"));

    WorkflowManifest manifest;
    manifest.outputDirectory = dir.path();
    const auto group = makeGroup(frame_path);
    manifest.registrationGroups.push_back(group);
    seedStageCompatibleSnapshotState(manifest, false);

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_metrics.db")));
    ASSERT_TRUE(db.isOpen());
    updateCachedInputFacts(db.db(), calibrated_path);
    ASSERT_TRUE(QFile::remove(calibrated_path));

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    const auto analysis = analyzeRegistrationWork(*snapshot, manifest, group);
    EXPECT_TRUE(analysis.needsWork);
    ASSERT_FALSE(analysis.reasons.empty());
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::ChangedInputs);
}

TEST(RegistrationWorkAnalyzer, SnapshotManifestContractAcceptsPlainHashWithDefaultPlateSolveSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("plain_hash_default_plate_solve.fit"));
    writeTestFile(frame_path);

    WorkflowManifest manifest;
    manifest.outputDirectory = dir.path();
    manifest.registrationGroups.push_back(makeGroup(frame_path));
    seedStageCompatibleSnapshotState(manifest, true);

    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    const auto analyses = analyzeRegistrationGroupsWork(*snapshot, manifest);
    ASSERT_EQ(analyses.size(), 1u);
    EXPECT_FALSE(analyses.front().analysis.needsWork);
    EXPECT_TRUE(analyses.front().analysis.reasons.empty());
}

TEST(RegistrationWorkAnalyzer, SnapshotManifestContractDetectsCalibrationOnlyChange)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("calibration_change_light.fit"));
    const QString bias_path = QDir(dir.path()).filePath(QStringLiteral("calibration_change_bias.fit"));
    writeTestFile(frame_path);
    writeTestFile(bias_path, QByteArray("bias"));

    WorkflowManifest manifest;
    manifest.outputDirectory = dir.path();
    const auto group = makeGroup(frame_path);
    manifest.registrationGroups.push_back(group);
    WorkflowManifest::LightsCalibrationParams assignment;
    assignment.lightPath = FrameId(frame_path);
    assignment.biasPath = FrameId(bias_path);
    manifest.lightsCalibration.push_back(assignment);
    seedStageCompatibleSnapshotState(manifest, false);

    WorkflowManifest changed = manifest;
    changed.lightsCalibration.front().settings.pedestal = 42.0;
    const auto snapshot = ExecutionPreparationSnapshot::build(changed, std::nullopt);
    const auto analyses = analyzeRegistrationGroupsWork(*snapshot, changed);
    ASSERT_EQ(analyses.size(), 1u);
    EXPECT_TRUE(analyses.front().analysis.needsWork);
    ASSERT_EQ(analyses.front().analysis.reasons.size(), 1u);
    EXPECT_EQ(analyses.front().analysis.reasons.front().code, RegistrationWorkReasonCode::CalibrationChanged);
}

TEST(RegistrationWorkAnalyzer, DatabaseManifestContractDetectsCalibrationOnlyChange)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("database_calibration_change_light.fit"));
    const QString bias_path = QDir(dir.path()).filePath(QStringLiteral("database_calibration_change_bias.fit"));
    writeTestFile(frame_path);
    writeTestFile(bias_path, QByteArray("bias"));

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_work.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    WorkflowManifest manifest;
    auto group = makeGroup(frame_path);
    manifest.registrationGroups.push_back(group);
    WorkflowManifest::LightsCalibrationParams assignment;
    assignment.lightPath = FrameId(frame_path);
    assignment.biasPath = FrameId(bias_path);
    manifest.lightsCalibration.push_back(assignment);
    const QString calibration_hash =
        computeLightsCalibrationHashForSubset(manifest.lightsCalibration, group.lightFiles, nullptr);
    insertMatchingState(db.db(), group, true, calibration_hash);

    manifest.lightsCalibration.front().settings.pedestal = 42.0;
    const RegistrationWorkAnalysis analysis = analyzeRegistrationWork(db.db(), manifest, group);

    EXPECT_TRUE(analysis.needsWork);
    ASSERT_EQ(analysis.reasons.size(), 1u);
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::CalibrationChanged);
}

TEST(RegistrationWorkAnalyzer, DatabaseManifestContractDetectsEffectivePlateSolveChange)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString frame_path = QDir(dir.path()).filePath(QStringLiteral("database_plate_solve_change_light.fit"));
    writeTestFile(frame_path);

    ScopedSqliteDb db(QDir(dir.path()).filePath(QStringLiteral("registration_work.db")));
    ASSERT_TRUE(db.isOpen());
    createRegistrationTables(db.db());

    WorkflowManifest manifest;
    auto group = makeGroup(frame_path);
    manifest.registrationGroups.push_back(group);
    insertMatchingState(db.db(), group, true);

    manifest.plateSolve.searchRadius += 1.0;
    const RegistrationWorkAnalysis analysis = analyzeRegistrationWork(db.db(), manifest, group);

    EXPECT_TRUE(analysis.needsWork);
    ASSERT_FALSE(analysis.reasons.empty());
    EXPECT_EQ(analysis.reasons.front().code, RegistrationWorkReasonCode::RegistrationSettingsChanged);
}
