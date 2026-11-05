// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The registration metrics database in this version's format: a new one made at format 11 with every table its writer
/// fills; one of another format, or whose tables are not the ones its format states, refused before anything is created
/// or written, naming the database, the format found and the one this version reads, and left byte for byte as it was
/// with nothing written beside it.

#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>

#include <gtest/gtest.h>

namespace
{

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

int userVersion(const QString& path)
{
    core::SqliteConnection connection(path, QStringLiteral("MetricsFormatTestVersion"));
    if (!connection.openReadOnly())
    {
        return -1;
    }
    QSqlQuery query(connection.db());
    return query.exec(QStringLiteral("PRAGMA user_version")) && query.next() ? query.value(0).toInt() : -1;
}

void execute(const QString& path, const QStringList& statements)
{
    core::SqliteConnection connection(path, QStringLiteral("MetricsFormatTestExec"));
    ASSERT_TRUE(connection.open());
    for (const QString& statement : statements)
    {
        QSqlQuery query(connection.db());
        ASSERT_TRUE(query.exec(statement)) << statement.toStdString() << ": " << query.lastError().text().toStdString();
    }
}

class RegistrationMetricsFormatTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_database = QDir(m_root.path()).filePath(QStringLiteral("registration_metrics.db"));
    }

    /// A database this version's writer made and filled with one frame's record, then closed.
    void makeCurrentDatabase() const
    {
        an::ui::RegistrationMetricsWriter writer(m_database);
        QString error;
        ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
        execute(m_database, {QStringLiteral("INSERT INTO registration_metrics (frame_id, group_key, settings_version, "
                                            "stars, fwhm, snr, eccentricity, succeeded, transform_json, updated_at) "
                                            "VALUES ('D:/lights/a.fit', 'group', 1, 120, 2.5, 40.0, 0.2, 1, '', 0)")});
    }

    [[nodiscard]] QStringList filesBeside() const
    {
        return QDir(m_root.path()).entryList(QDir::Files, QDir::Name);
    }

    QTemporaryDir m_root;
    QString m_database;
};

TEST_F(RegistrationMetricsFormatTest, ANewDatabaseIsMadeAtFormatElevenWithEveryTable)
{
    {
        an::ui::RegistrationMetricsWriter writer(m_database);
        QString error;
        ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
    }
    EXPECT_EQ(userVersion(m_database), stage_utils::kRegistrationMetricsSchemaVersion);
    const stage_utils::RegistrationMetricsSchemaInspection inspection =
        stage_utils::inspectRegistrationMetricsSchema(m_database, QStringLiteral("MetricsFormatTestInspect"));
    EXPECT_TRUE(inspection.isCurrent());

    // Opened again, it is this version's own and is written to as it is.
    an::ui::RegistrationMetricsWriter again(m_database);
    QString error;
    EXPECT_TRUE(again.initialize(&error)) << error.toStdString();
}

TEST_F(RegistrationMetricsFormatTest, ADatabaseOfAnotherFormatIsRefusedAndLeftByteForByte)
{
    for (const int format : {10, 12})
    {
        ASSERT_TRUE(!QFileInfo::exists(m_database) || QFile::remove(m_database));
        makeCurrentDatabase();
        execute(m_database, {QStringLiteral("PRAGMA user_version = %1").arg(format)});
        const QByteArray before = fileSha256(m_database);
        {
            an::ui::RegistrationMetricsWriter writer(m_database);
            QString error;
            EXPECT_FALSE(writer.initialize(&error)) << format;
            EXPECT_TRUE(error.contains(QDir::toNativeSeparators(m_database))) << error.toStdString();
            EXPECT_TRUE(error.contains(QStringLiteral("written in format %1").arg(format))) << error.toStdString();
            EXPECT_TRUE(error.contains(QStringLiteral("this version of AfterNight reads format 11")))
                << error.toStdString();
        }
        EXPECT_EQ(fileSha256(m_database), before) << format;
        EXPECT_EQ(userVersion(m_database), format);
        EXPECT_EQ(filesBeside(), QStringList{QStringLiteral("registration_metrics.db")}) << format;
    }
}

TEST_F(RegistrationMetricsFormatTest, ADatabaseWhoseTablesAreNotItsFormatsIsRefusedAndLeftByteForByte)
{
    // Format 11 stated over a registration table without the columns that format's table has.
    execute(m_database, {QStringLiteral("CREATE TABLE registration_metrics (frame_id TEXT NOT NULL, group_key TEXT NOT "
                                        "NULL, transform_json TEXT)"),
                         QStringLiteral("INSERT INTO registration_metrics VALUES ('D:/lights/a.fit', 'group', '')"),
                         QStringLiteral("PRAGMA user_version = 11")});
    const QByteArray before = fileSha256(m_database);
    {
        an::ui::RegistrationMetricsWriter writer(m_database);
        QString error;
        EXPECT_FALSE(writer.initialize(&error));
        EXPECT_TRUE(error.contains(QDir::toNativeSeparators(m_database))) << error.toStdString();
        EXPECT_TRUE(error.contains(QStringLiteral("format 11"))) << error.toStdString();
    }
    EXPECT_EQ(fileSha256(m_database), before);
    EXPECT_EQ(filesBeside(), QStringList{QStringLiteral("registration_metrics.db")});
}

} // namespace
