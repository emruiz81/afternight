// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include "controls/FolderTrackerPersistence.h"
#include "controls/PersistenceDbExecutor.h"
#include "app/PersistenceCoordinator.h"
#include "app/FileManager.h"

namespace
{

// ---------------------------------------------------------------------------
// Lightweight in-memory database for each test.
// ---------------------------------------------------------------------------
class InMemoryDb
{
public:
    InMemoryDb()
        : m_executor(std::make_unique<PersistenceDbExecutor>(
              QStringLiteral(":memory:"),
              QStringLiteral("folder_path_tracking_%1").arg(s_counter.fetch_add(1, std::memory_order_relaxed))))
    {
    }

    [[nodiscard]] PersistenceDbExecutor& executor() noexcept
    {
        return *m_executor;
    }

private:
    static inline std::atomic<int> s_counter{0};
    std::unique_ptr<PersistenceDbExecutor> m_executor;
};

// ---------------------------------------------------------------------------
// FolderTrackerPersistence round-trip tests
// ---------------------------------------------------------------------------

TEST(FolderPathTracking, UpsertAndSelectRoundTrip)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    const QString key = QStringLiteral("__TEST_CONTEXT__");
    const QString value = QStringLiteral("/home/user/images");

    EXPECT_TRUE(tracker.upsert(key, value));
    EXPECT_EQ(tracker.select(key), value);
}

TEST(FolderPathTracking, UpsertOverwritesPreviousValue)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    const QString key = QStringLiteral("__TEST_CONTEXT__");
    EXPECT_TRUE(tracker.upsert(key, QStringLiteral("/old/path")));
    EXPECT_TRUE(tracker.upsert(key, QStringLiteral("/new/path")));
    EXPECT_EQ(tracker.select(key), QStringLiteral("/new/path"));
}

TEST(FolderPathTracking, SelectReturnsEmptyForMissingKey)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    EXPECT_TRUE(tracker.select(QStringLiteral("__NONEXISTENT__")).isEmpty());
}

TEST(FolderPathTracking, SelectReturnsDefaultForMissingKey)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    const QString defaultVal = QStringLiteral("/fallback");
    EXPECT_EQ(tracker.select(QStringLiteral("__NONEXISTENT__"), defaultVal), defaultVal);
}

// ---------------------------------------------------------------------------
// Multiple independent contexts don't interfere
// ---------------------------------------------------------------------------

TEST(FolderPathTracking, IndependentContextsDoNotInterfere)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    const QString keyA = QStringLiteral("__CONTEXT_A__");
    const QString keyB = QStringLiteral("__CONTEXT_B__");

    EXPECT_TRUE(tracker.upsert(keyA, QStringLiteral("/path/a")));
    EXPECT_TRUE(tracker.upsert(keyB, QStringLiteral("/path/b")));

    EXPECT_EQ(tracker.select(keyA), QStringLiteral("/path/a"));
    EXPECT_EQ(tracker.select(keyB), QStringLiteral("/path/b"));
}

// ---------------------------------------------------------------------------
// Context keys used by the application (regression guard)
// ---------------------------------------------------------------------------

TEST(FolderPathTracking, AllExpectedContextKeysCanBeStoredAndRetrieved)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    // Context keys used across the UI — adding a new dialog should add its key here.
    static constexpr const char* kContextKeys[] = {
        "__GLOBAL__",
        "__IMAGE_CALIB_ADD_FILES__",
        "__IMAGE_CALIB_FOLDER__",
        "__IMAGE_REG_FOLDER__",
        "__EXPORT_SEQUENCE_CONTROL__",
        "__IMAGE_ANALYZER_ADD_FOLDER__",
        "__IMAGE_INTEGRATION_IMPORT__",
        "__IMAGE_INTEGRATION_PROJECT__",
        "__CALIBRATION_MASTER_BROWSE__",
        "__MASK_DRAWING_BROWSE__",
        "__IMAGE_HISTORY_SAVE__",
        "__QUALITY_CHART_EXPORT__",
        "__SYSTEM_INFO_EXPORT__",
        "__SETTINGS_IMPORT_EXPORT__",
    };

    for (const char* key : kContextKeys)
    {
        const QString qKey = QString::fromLatin1(key);
        const QString testPath = QStringLiteral("/test/") + qKey;
        EXPECT_TRUE(tracker.upsert(qKey, testPath)) << "Failed to upsert context key: " << key;
        EXPECT_EQ(tracker.select(qKey), testPath) << "Failed to select context key: " << key;
    }
}

// ---------------------------------------------------------------------------
// Path resolution logic — verifies the resolve pattern used by dialog sites
// ---------------------------------------------------------------------------

TEST(FolderPathTracking, ResolvedPathFallsBackWhenDirectoryDoesNotExist)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    const QString key = QStringLiteral("__TEST_RESOLVE__");
    tracker.upsert(key, QStringLiteral("/absolutely/nonexistent/path/42xyz"));

    // Simulate the resolve pattern used at every dialog site:
    QString resolved = tracker.select(key);
    if (!resolved.isEmpty() && !QDir(resolved).exists())
    {
        resolved.clear();
    }

    EXPECT_TRUE(resolved.isEmpty()) << "Expected fallback to empty when persisted directory no longer exists";
}

TEST(FolderPathTracking, ResolvedPathUsesPersistedValueWhenDirectoryExists)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    const QString key = QStringLiteral("__TEST_RESOLVE__");
    tracker.upsert(key, tempDir.path());

    QString resolved = tracker.select(key);
    if (!resolved.isEmpty() && !QDir(resolved).exists())
    {
        resolved.clear();
    }

    EXPECT_EQ(resolved, tempDir.path());
}

// ---------------------------------------------------------------------------
// Remove and clearAll
// ---------------------------------------------------------------------------

TEST(FolderPathTracking, RemoveDeletesSingleContext)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    const QString key = QStringLiteral("__TO_REMOVE__");
    tracker.upsert(key, QStringLiteral("/some/path"));
    EXPECT_TRUE(tracker.remove(key));
    EXPECT_TRUE(tracker.select(key).isEmpty());
}

TEST(FolderPathTracking, ClearAllRemovesEverything)
{
    InMemoryDb db;
    FolderTrackerPersistence tracker(db.executor());
    ASSERT_TRUE(tracker.initializeTable());

    tracker.upsert(QStringLiteral("__A__"), QStringLiteral("/a"));
    tracker.upsert(QStringLiteral("__B__"), QStringLiteral("/b"));
    EXPECT_TRUE(tracker.clearAll());
    EXPECT_TRUE(tracker.select(QStringLiteral("__A__")).isEmpty());
    EXPECT_TRUE(tracker.select(QStringLiteral("__B__")).isEmpty());
}

// ---------------------------------------------------------------------------
// UNC / network path filter (Windows-oriented but runs on all platforms)
// ---------------------------------------------------------------------------

TEST(FolderPathTracking, SkipUNCPathsForPersistence)
{
    EXPECT_TRUE(an::ui::shouldSkipPersistLastOpenDirectory("\\\\server\\share"));
    EXPECT_TRUE(an::ui::shouldSkipPersistLastOpenDirectory("\\\\192.168.1.1\\data"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("/home/user/images"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("C:\\Users\\user\\Pictures"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory(""));
}

// ---------------------------------------------------------------------------
// Path extraction helper — verifies QFileInfo gives directory for file paths
// ---------------------------------------------------------------------------

TEST(FolderPathTracking, ExtractDirectoryFromFilePath)
{
    // When a user selects a file, we persist the parent directory.
    // Use QDir::temp() so the base path is a real, absolute directory on every OS.
    const QDir dir = QDir::temp();
    const QString filePath = dir.absoluteFilePath(QStringLiteral("m31.fits"));
    EXPECT_EQ(QFileInfo(filePath).absolutePath(), dir.absolutePath());
}

TEST(FolderPathTracking, DirectoryPathStaysUnchanged)
{
    // When a user selects a directory, it should stay as-is.
    const QString dirPath = QStringLiteral("/home/user/images");
    const QFileInfo info(dirPath);
    // For a directory path, absolutePath() returns the parent.
    // But QDir(path).absolutePath() returns the directory itself.
    EXPECT_EQ(QDir(dirPath).absolutePath(), dirPath);
}

// ---------------------------------------------------------------------------
// updateRecentFileList helper (FileManager.h)
// ---------------------------------------------------------------------------

TEST(FolderPathTracking, UpdateRecentFileListPromotesExistingEntry)
{
    std::deque<std::string> recent = {"a.fits", "b.fits", "c.fits"};
    const bool changed = an::ui::updateRecentFileList(recent, "b.fits", 10);
    EXPECT_TRUE(changed);
    EXPECT_EQ(recent.front(), "b.fits");
    EXPECT_EQ(recent.size(), 3u);
}

TEST(FolderPathTracking, UpdateRecentFileListIgnoresDuplicate)
{
    std::deque<std::string> recent = {"a.fits", "b.fits"};
    const bool changed = an::ui::updateRecentFileList(recent, "a.fits", 10);
    EXPECT_FALSE(changed);
    EXPECT_EQ(recent.front(), "a.fits");
}

} // anonymous namespace
