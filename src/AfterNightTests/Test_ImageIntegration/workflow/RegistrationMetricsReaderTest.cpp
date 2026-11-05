// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the alignment reads of the registration database: each light's transform in engine pixel coordinates, nothing
/// left behind when the database cannot be read, one database generation pinned for a group's reads, and the row a
/// light's two spellings both name chosen the same way every time.

#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsReader.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <unordered_map>

namespace
{

TEST(RegistrationMetricsReader, LoadTransformsReadsEachLightsTransform)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString db_path = QDir(root.path()).filePath(QStringLiteral("registration_metrics.db"));
    const QString group_key = QStringLiteral("reg|reader");
    const QString frame_path = QDir(root.path()).filePath(QStringLiteral("frame.fit"));

    an::RegistrationTransformRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.sourceToReference = {1.0, 0.0, 7.0, 0.0, 1.0, -5.0, 0.0, 0.0, 1.0};
    const an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(request);
    ASSERT_TRUE(transform.hasValue());
    {
        an::ui::RegistrationMetricsWriter writer(db_path, QStringLiteral("ReaderTestSeed"));
        ASSERT_TRUE(writer.initialize());
        an::ui::RegistrationMetricRecord metric;
        metric.frameId = FrameId(frame_path);
        metric.groupKey = group_key;
        metric.settingsVersion = 1;
        metric.metrics.succeeded = true;
        metric.transformJson =
            stage_utils::serializeTransformJson(*transform, stage_utils::SourceDistortionProvenance::explicitNone());
        QString error;
        ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    }

    stage_utils::RegistrationMetricsReader reader(db_path, QStringLiteral("ReaderTest"));
    ASSERT_TRUE(reader.open());
    std::unordered_map<QString, an::RegistrationTransform> transforms;
    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance;
    QString error;
    ASSERT_TRUE(reader.loadTransforms(group_key, 1, transforms, error, &provenance)) << error.toStdString();
    ASSERT_EQ(transforms.size(), 1u);
    const auto read = transforms.find(FrameId(frame_path).canonicalPath());
    ASSERT_NE(read, transforms.end());
    EXPECT_EQ(read->second.kind(), an::RegistrationTransformKind::Affine);
    EXPECT_DOUBLE_EQ(read->second.translation().x, 7.0);
    EXPECT_DOUBLE_EQ(read->second.translation().y, -5.0);
    ASSERT_EQ(provenance.size(), 1u);
    EXPECT_EQ(provenance.begin()->second.policy, stage_utils::SourceDistortionPolicy::ExplicitNone);
}

TEST(RegistrationMetricsReader, LoadTransformsClearsReusedOutputsWhenReaderIsClosed)
{
    stage_utils::RegistrationMetricsReader reader(QStringLiteral(":memory:"), QStringLiteral("ReaderClosedTest"));
    const QString frame_id = QStringLiteral("C:/Data/Light.fit");
    std::unordered_map<QString, an::RegistrationTransform> transforms = {{frame_id, an::RegistrationTransform{}}};
    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance = {
        {frame_id, stage_utils::SourceDistortionProvenance::explicitNone()}};
    QString error;

    EXPECT_FALSE(reader.loadTransforms(QStringLiteral("reg|closed"), 1, transforms, error, &provenance));
    EXPECT_TRUE(transforms.empty());
    EXPECT_TRUE(provenance.empty());
    EXPECT_FALSE(error.isEmpty());
}

TEST(RegistrationMetricsReader, ScopedReadSnapshotPinsOneDatabaseGeneration)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString db_path = QDir(root.path()).filePath(QStringLiteral("registration_metrics.db"));
    const QString group_key = QStringLiteral("reg|snapshot");

    const QString writer_connection = QStringLiteral("ReaderSnapshotWriter");
    {
        QSqlDatabase writer = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), writer_connection);
        writer.setDatabaseName(db_path);
        ASSERT_TRUE(writer.open());
        QSqlQuery query(writer);
        ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA journal_mode=WAL")));
        ASSERT_TRUE(query.exec(QStringLiteral("CREATE TABLE registration_metrics ("
                                              "group_key TEXT NOT NULL, settings_version INTEGER NOT NULL)")));
        ASSERT_TRUE(query.exec(QStringLiteral("INSERT INTO registration_metrics VALUES ('reg|snapshot', 1)")));

        {
            stage_utils::RegistrationMetricsReader reader(db_path, QStringLiteral("ReaderSnapshot"));
            ASSERT_TRUE(reader.open());
            auto snapshot = reader.beginReadSnapshot();
            ASSERT_TRUE(snapshot.has_value());
            EXPECT_EQ(reader.maxSettingsVersionForGroup(group_key), 1);

            ASSERT_TRUE(query.exec(QStringLiteral("INSERT INTO registration_metrics VALUES ('reg|snapshot', 2)")));
            EXPECT_EQ(reader.maxSettingsVersionForGroup(group_key), 1);
            snapshot.reset();
            EXPECT_EQ(reader.maxSettingsVersionForGroup(group_key), 2);
        }
        query = QSqlQuery();
        writer.close();
    }
    QSqlDatabase::removeDatabase(writer_connection);
}

TEST(RegistrationMetricsReader, EqualTimestampAliasesUseRawStoredSpellingAsFinalTieBreak)
{
    const QString forwardSlashAlias = QStringLiteral("c:/p121/reader/light.fit");
    const QString backslashAlias = QStringLiteral("c:\\p121\\reader\\light.fit");
    ASSERT_EQ(FrameId(forwardSlashAlias), FrameId(backslashAlias));
    ASSERT_LT(QString::compare(forwardSlashAlias, backslashAlias, Qt::CaseSensitive), 0);

    EXPECT_TRUE(stage_utils::preferPersistedRegistrationRow(100, forwardSlashAlias, 100, backslashAlias));
    EXPECT_FALSE(stage_utils::preferPersistedRegistrationRow(100, backslashAlias, 100, forwardSlashAlias));
}

} // namespace
