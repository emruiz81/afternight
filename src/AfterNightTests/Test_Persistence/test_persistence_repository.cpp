// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_persistence_repository.cpp
 * @brief Packet 7A — Persistence API Consolidation tests.
 *
 * Verifies that all concrete persistence repositories:
 *  - Route SQL through PersistenceDbExecutor (via PersistenceBaseRepository).
 *  - Correctly implement generic upsert / select / remove patterns.
 *
 * Uses InMemoryDatabase so no disk I/O is performed.
 */
#include <gtest/gtest.h>

#include <QSqlQuery>
#include <QSqlError>
#include <QString>

#include "controls/PersistenceDbExecutor.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/ThumbnailPersistence.h"
#include "fixtures/InMemoryDatabase.h"

// ============================================================
// Fixture
// ============================================================

class PersistenceRepositoryTest : public ::testing::Test
{
protected:
    InMemoryDatabase db;
};

// ============================================================
// PersistenceRepository_GenericUpsert
// ============================================================

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericUpsert_GlobalSettings)
{
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("key1"), QStringLiteral("value1")));

    const QString result = repo.select(QStringLiteral("key1"));
    EXPECT_EQ(result, QStringLiteral("value1"));
}

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericUpsert_OverwritesExisting)
{
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("k"), QStringLiteral("v1")));
    EXPECT_TRUE(repo.upsert(QStringLiteral("k"), QStringLiteral("v2")));

    EXPECT_EQ(repo.select(QStringLiteral("k")), QStringLiteral("v2"));
}

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericUpsert_ProcessSettings)
{
    ProcessSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("MyProcess"), 0, 800, 600, QStringLiteral("{}")));

    int state = -1, w = 0, h = 0;
    QString json;
    ASSERT_TRUE(repo.select(QStringLiteral("MyProcess"), state, w, h, json));
    EXPECT_EQ(state, 0);
    EXPECT_EQ(w, 800);
    EXPECT_EQ(h, 600);
    EXPECT_EQ(json, QStringLiteral("{}"));
}

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericUpsert_FolderTracker)
{
    FolderTrackerPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("export"), QStringLiteral("/home/user/output")));
    EXPECT_EQ(repo.select(QStringLiteral("export")), QStringLiteral("/home/user/output"));
}

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericUpsert_ThumbnailPersistence)
{
    ThumbnailPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("image.fit"), QStringLiteral("image_thumb.png")));
    EXPECT_EQ(repo.select(QStringLiteral("image.fit")), QStringLiteral("image_thumb.png"));
}

// ============================================================
// PersistenceRepository_GenericSelect
// ============================================================

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericSelect_ReturnsDefaultWhenMissing)
{
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    const QString result = repo.select(QStringLiteral("nonexistent"), QStringLiteral("default_val"));
    EXPECT_EQ(result, QStringLiteral("default_val"));
}

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericSelect_MultipleKeys)
{
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("a"), QStringLiteral("1")));
    EXPECT_TRUE(repo.upsert(QStringLiteral("b"), QStringLiteral("2")));
    EXPECT_TRUE(repo.upsert(QStringLiteral("c"), QStringLiteral("3")));

    EXPECT_EQ(repo.select(QStringLiteral("a")), QStringLiteral("1"));
    EXPECT_EQ(repo.select(QStringLiteral("b")), QStringLiteral("2"));
    EXPECT_EQ(repo.select(QStringLiteral("c")), QStringLiteral("3"));
}

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericSelect_FolderTrackerEmptyOnMiss)
{
    FolderTrackerPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.select(QStringLiteral("missing_context")).isEmpty());
}

// ============================================================
// PersistenceRepository_GenericRemove
// ============================================================

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericRemove_GlobalSettings)
{
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("to_remove"), QStringLiteral("data")));
    EXPECT_TRUE(repo.remove(QStringLiteral("to_remove")));

    EXPECT_TRUE(repo.select(QStringLiteral("to_remove")).isEmpty());
}

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericRemove_ProcessSettings)
{
    ProcessSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("Proc"), 0, 400, 300, QStringLiteral("{}")));
    EXPECT_TRUE(repo.remove(QStringLiteral("Proc")));

    int state = -1, w = 0, h = 0;
    QString json;
    // select returns true even when no row found (returns empty defaults)
    ASSERT_TRUE(repo.select(QStringLiteral("Proc"), state, w, h, json));
    EXPECT_EQ(state, 0);
    EXPECT_EQ(w, 0);
    EXPECT_EQ(h, 0);
    EXPECT_TRUE(json.isEmpty());
}

TEST_F(PersistenceRepositoryTest, PersistenceRepository_GenericRemove_ClearAll)
{
    GlobalSettingsPersistence repo(db.executor());
    ASSERT_TRUE(repo.initializeTable());

    EXPECT_TRUE(repo.upsert(QStringLiteral("x"), QStringLiteral("1")));
    EXPECT_TRUE(repo.upsert(QStringLiteral("y"), QStringLiteral("2")));
    EXPECT_TRUE(repo.clearAll());

    EXPECT_TRUE(repo.select(QStringLiteral("x")).isEmpty());
    EXPECT_TRUE(repo.select(QStringLiteral("y")).isEmpty());
}

// ============================================================
// PersistenceDbExecutor_RejectsCallsAfterShutdown
// ============================================================

TEST(PersistenceDbExecutor_RejectsCallsAfterShutdown, ThrowsStdRuntimeErrorAfterTeardown)
{
    InMemoryDatabase db;
    ASSERT_TRUE(db.isOpen());

    // Explicitly shut down the executor.
    db.executor().shutdown();

    // Any subsequent execute() call must throw.
    EXPECT_THROW(db.executor().execute([](QSqlDatabase&) {}), std::runtime_error);
}

TEST(PersistenceDbExecutor_RejectsCallsAfterShutdown, IsOpenReturnsFalseAfterShutdown)
{
    InMemoryDatabase db;
    ASSERT_TRUE(db.isOpen());

    db.executor().shutdown();

    EXPECT_FALSE(db.isOpen());
}
