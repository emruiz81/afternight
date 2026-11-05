// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_schema_versioning.cpp
 * @brief The format a settings database states, and how one is made.
 *
 * A database that holds no table is made at the format in one transaction, its tables and the format it states
 * together; one at the format is opened as it is; one of another format - older, newer, or holding tables but stating
 * none - is neither read nor written; and a database whose tables cannot be made keeps nothing.
 *
 * Uses InMemoryDatabase so no disk I/O is performed.
 */
#include <gtest/gtest.h>

#include <QSqlQuery>
#include <QString>

#include "controls/DbSchemaVersionManager.h"
#include "controls/PersistenceDbExecutor.h"
#include "fixtures/InMemoryDatabase.h"

namespace
{

constexpr int kFormat = 2;

bool createItems(QSqlDatabase& db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"));
}

/// The format the database states, or -1 when it states none.
int statedFormat(InMemoryDatabase& db)
{
    int format = -1;
    db.executor().execute([&format](QSqlDatabase& conn) {
        QSqlQuery q(conn);
        if (q.exec(QStringLiteral("SELECT version FROM migrations")) && q.next())
        {
            format = q.value(0).toInt();
        }
    });
    return format;
}

/// The names of the tables the database holds, in order.
QStringList tables(InMemoryDatabase& db)
{
    QStringList names;
    db.executor().execute([&names](QSqlDatabase& conn) {
        QSqlQuery q(conn);
        if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' "
                                  "ORDER BY name")))
        {
            while (q.next())
            {
                names << q.value(0).toString();
            }
        }
    });
    return names;
}

} // namespace

TEST(SettingsDatabaseFormat, ADatabaseThatHoldsNoTableIsMadeAtTheFormat)
{
    InMemoryDatabase db;
    DbSchemaVersionManager format(db.executor(), kFormat, createItems);

    const DbSchemaVersionManager::Result opened = format.initialize();
    EXPECT_EQ(opened.outcome, DbSchemaVersionManager::Outcome::Created);
    EXPECT_EQ(opened.foundFormat, kFormat);
    EXPECT_EQ(statedFormat(db), kFormat);
    EXPECT_EQ(tables(db), (QStringList{QStringLiteral("items"), QStringLiteral("migrations")}));
}

TEST(SettingsDatabaseFormat, ADatabaseAtTheFormatIsOpenedAsItIs)
{
    InMemoryDatabase db;
    ASSERT_EQ(DbSchemaVersionManager(db.executor(), kFormat, createItems).initialize().outcome,
              DbSchemaVersionManager::Outcome::Created);
    db.executor().execute([](QSqlDatabase& conn) {
        QSqlQuery q(conn);
        ASSERT_TRUE(q.exec(QStringLiteral("INSERT INTO items (name) VALUES ('kept')")));
    });

    const DbSchemaVersionManager::Result opened =
        DbSchemaVersionManager(db.executor(), kFormat, createItems).initialize();
    EXPECT_EQ(opened.outcome, DbSchemaVersionManager::Outcome::Current);
    EXPECT_EQ(opened.foundFormat, kFormat);
    db.executor().execute([](QSqlDatabase& conn) {
        QSqlQuery q(conn);
        ASSERT_TRUE(q.exec(QStringLiteral("SELECT name FROM items")));
        ASSERT_TRUE(q.next());
        EXPECT_EQ(q.value(0).toString(), QStringLiteral("kept"));
        EXPECT_FALSE(q.next());
    });
}

TEST(SettingsDatabaseFormat, ADatabaseOfAnotherFormatIsNeitherReadNorWritten)
{
    for (const int other : {1, 3})
    {
        InMemoryDatabase db;
        db.executor().execute([other](QSqlDatabase& conn) {
            QSqlQuery q(conn);
            ASSERT_TRUE(q.exec(QStringLiteral("CREATE TABLE migrations (version INTEGER NOT NULL DEFAULT 0)")));
            ASSERT_TRUE(q.exec(QStringLiteral("INSERT INTO migrations (version) VALUES (%1)").arg(other)));
            ASSERT_TRUE(q.exec(QStringLiteral("CREATE TABLE settings (key TEXT, value TEXT)")));
        });

        const DbSchemaVersionManager::Result opened =
            DbSchemaVersionManager(db.executor(), kFormat, createItems).initialize();
        EXPECT_EQ(opened.outcome, DbSchemaVersionManager::Outcome::OtherFormat) << other;
        EXPECT_EQ(opened.foundFormat, other);
        EXPECT_EQ(statedFormat(db), other);
        EXPECT_EQ(tables(db), (QStringList{QStringLiteral("migrations"), QStringLiteral("settings")}));
    }
}

TEST(SettingsDatabaseFormat, ADatabaseThatHoldsTablesButStatesNoFormatIsAnotherFormats)
{
    InMemoryDatabase db;
    db.executor().execute([](QSqlDatabase& conn) {
        QSqlQuery q(conn);
        ASSERT_TRUE(q.exec(QStringLiteral("CREATE TABLE settings (key TEXT, value TEXT)")));
    });

    const DbSchemaVersionManager::Result opened =
        DbSchemaVersionManager(db.executor(), kFormat, createItems).initialize();
    EXPECT_EQ(opened.outcome, DbSchemaVersionManager::Outcome::OtherFormat);
    EXPECT_EQ(opened.foundFormat, 0);
    EXPECT_EQ(tables(db), QStringList{QStringLiteral("settings")});
}

TEST(SettingsDatabaseFormat, ADatabaseWhoseTablesCannotBeMadeKeepsNothing)
{
    InMemoryDatabase db;
    const DbSchemaVersionManager::Result opened =
        DbSchemaVersionManager(db.executor(), kFormat, [](QSqlDatabase& conn) {
            QSqlQuery q(conn);
            return q.exec(QStringLiteral("CREATE TABLE first (id INTEGER)")) &&
                   q.exec(QStringLiteral("THIS IS NOT SQL"));
        }).initialize();
    EXPECT_EQ(opened.outcome, DbSchemaVersionManager::Outcome::Failed);
    EXPECT_TRUE(tables(db).isEmpty());
}
