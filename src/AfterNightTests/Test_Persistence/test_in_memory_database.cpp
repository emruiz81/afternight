// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_in_memory_database.cpp
 * @brief Packet 7D — InMemoryDatabase mock tests.
 *
 * Verifies that InMemoryDatabase:
 *  - Opens an in-memory SQLite connection (no disk I/O).
 *  - Exposes a working PersistenceDbExecutor.
 *  - Supports full CRUD operations via persistence repository classes.
 *  - Multiple instances are independent (separate in-memory databases).
 */
#include <gtest/gtest.h>

#include <QSqlQuery>
#include <QSqlError>
#include <QString>

#include "controls/PersistenceDbExecutor.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"
#include "fixtures/InMemoryDatabase.h"

// ============================================================
// Basic open / executor checks
// ============================================================

TEST(InMemoryDatabase, IsOpenAfterConstruction)
{
    InMemoryDatabase db;
    EXPECT_TRUE(db.isOpen());
}

TEST(InMemoryDatabase, ExecutorIsAvailable)
{
    InMemoryDatabase db;
    // Run a trivial query to confirm the executor is functional.
    bool ran = false;
    db.executor().execute([&ran](QSqlDatabase& dbConn) {
        QSqlQuery q(dbConn);
        ran = q.exec(QStringLiteral("SELECT 1"));
    });
    EXPECT_TRUE(ran);
}

// ============================================================
// CRUD via GlobalSettingsPersistence
// ============================================================

TEST(InMemoryDatabase, SupportsCRUD_Insert)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("theme"), QStringLiteral("dark")));
    EXPECT_EQ(repo.select(QStringLiteral("theme")), QStringLiteral("dark"));
}

TEST(InMemoryDatabase, SupportsCRUD_Update)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("theme"), QStringLiteral("dark")));
    EXPECT_TRUE(repo.upsert(QStringLiteral("theme"), QStringLiteral("light")));
    EXPECT_EQ(repo.select(QStringLiteral("theme")), QStringLiteral("light"));
}

TEST(InMemoryDatabase, SupportsCRUD_Delete)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("key"), QStringLiteral("val")));
    EXPECT_TRUE(repo.remove(QStringLiteral("key")));
    EXPECT_TRUE(repo.select(QStringLiteral("key")).isEmpty());
}

TEST(InMemoryDatabase, SupportsCRUD_ClearAll)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("a"), QStringLiteral("1")));
    EXPECT_TRUE(repo.upsert(QStringLiteral("b"), QStringLiteral("2")));
    EXPECT_TRUE(repo.clearAll());

    EXPECT_TRUE(repo.select(QStringLiteral("a")).isEmpty());
    EXPECT_TRUE(repo.select(QStringLiteral("b")).isEmpty());
}

// ============================================================
// Multiple repositories on the same in-memory database
// ============================================================

TEST(InMemoryDatabase, MultipleRepositoriesShareSameConnection)
{
    InMemoryDatabase db;
    GlobalSettingsPersistence settings(db.executor());
    ThumbnailPersistence thumbnails(db.executor());

    ASSERT_TRUE(settings.initializeTable());
    ASSERT_TRUE(thumbnails.initializeTable());

    EXPECT_TRUE(settings.upsert(QStringLiteral("lang"), QStringLiteral("en")));
    EXPECT_TRUE(thumbnails.upsert(QStringLiteral("img.fit"), QStringLiteral("img_t.png")));

    EXPECT_EQ(settings.select(QStringLiteral("lang")), QStringLiteral("en"));
    EXPECT_EQ(thumbnails.select(QStringLiteral("img.fit")), QStringLiteral("img_t.png"));
}

// ============================================================
// Independence — each InMemoryDatabase instance is isolated
// ============================================================

TEST(InMemoryDatabase, InstancesAreIsolated)
{
    InMemoryDatabase db1;
    InMemoryDatabase db2;

    GlobalSettingsPersistence repo1(db1.executor());
    GlobalSettingsPersistence repo2(db2.executor());

    ASSERT_TRUE(repo1.initializeTable());
    ASSERT_TRUE(repo2.initializeTable());

    EXPECT_TRUE(repo1.upsert(QStringLiteral("x"), QStringLiteral("only_in_db1")));

    // repo2 must not see the value stored in repo1.
    EXPECT_TRUE(repo2.select(QStringLiteral("x")).isEmpty());
}
