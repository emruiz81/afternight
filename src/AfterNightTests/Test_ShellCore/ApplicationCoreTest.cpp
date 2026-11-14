// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application without a window: its stores, opened before any widget exists and under the storage root the run
/// was given; the folders it keeps there; and the lists it reads back from them.

#include "ShellCoreTestSupport.h"

#include "core/ApplicationCore.h"
#include "services/FavoriteProcessesModel.h"
#include "services/RecentFilesModel.h"
#include "services/RecentProcessesModel.h"

#include "app/RuntimeStorageCoordinator.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <deque>
#include <set>
#include <string>
#include <thread>

namespace
{

using an::tests::shellcore::ScopedStorageRoot;
using an::ui::shell::ApplicationCore;

/// Whether `path` is `folder` or lies below it, however either is spelled.
[[nodiscard]] bool isWithin(const QString& path, const QString& folder)
{
    const QString canonicalFolder = QDir(folder).canonicalPath();
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    return !canonicalFolder.isEmpty() && !canonicalPath.isEmpty() &&
           (canonicalPath == canonicalFolder || canonicalPath.startsWith(canonicalFolder + QLatin1Char('/')));
}

class ApplicationCoreTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
};

// A shell is chosen from a setting, and the style it asks for is set before the first widget: the stores open with no
// widget made, and make none.
TEST_F(ApplicationCoreTest, OpensItsStoresBeforeAnyWidgetExists)
{
    ASSERT_TRUE(QApplication::allWidgets().isEmpty()) << "the shell core's cases make no widget";
    ApplicationCore core;
    EXPECT_FALSE(core.storesOpen());
    EXPECT_EQ(core.globalSettingsStore(), nullptr);

    core.openStores();

    EXPECT_TRUE(core.storesOpen());
    EXPECT_NE(core.globalSettingsStore(), nullptr);
    EXPECT_NE(core.processSettingsStore(), nullptr);
    EXPECT_NE(core.folderStore(), nullptr);
    EXPECT_NE(core.thumbnailStore(), nullptr);
    EXPECT_TRUE(QApplication::allWidgets().isEmpty()) << "opening the stores made a widget";
    EXPECT_TRUE(core.settingsStoreNotice().isEmpty()) << "a new database is this version's own";
    EXPECT_EQ(core.guiThreadId(), std::this_thread::get_id());
    EXPECT_TRUE(core.isGuiThread());
}

TEST_F(ApplicationCoreTest, OwnsItsStoresUnderTheTestStorageRoot)
{
    ApplicationCore core;
    core.openStores();

    EXPECT_EQ(QDir(core.localDataPath()).canonicalPath(), QDir(m_root.path()).canonicalPath());
    EXPECT_TRUE(QFileInfo::exists(QDir(m_root.path()).filePath(QStringLiteral("afternight_data.db"))));

    // A setting written through the core is the store's.
    an::ui::IGlobalSettingsProvider* const settings = core.globalSettings();
    ASSERT_NE(settings, nullptr);
    ASSERT_TRUE(settings->upsert(QStringLiteral("shellcore/case"), QStringLiteral("kept")));
    EXPECT_EQ(core.globalSettingsStore()->select(QStringLiteral("shellcore/case")), QStringLiteral("kept"));

    // The temporary and thumbnail folders are made under the root.
    const QString temporary = core.runtimeStorage().getTempDirPath();
    EXPECT_TRUE(QDir(temporary).exists());
    EXPECT_TRUE(isWithin(temporary, m_root.path())) << temporary.toStdString();
    EXPECT_TRUE(core.isTempFile(QDir(temporary).filePath(QStringLiteral("step.xisf")).toStdString()));
    EXPECT_FALSE(core.isTempFile(QDir(m_root.path()).filePath(QStringLiteral("light.fits")).toStdString()));
    const QString thumbnails = core.runtimeStorage().getThumbnailsDirPath();
    EXPECT_TRUE(QDir(thumbnails).exists());
    EXPECT_TRUE(isWithin(thumbnails, m_root.path())) << thumbnails.toStdString();

    // So is the recent files' list.
    core.recentFiles().entriesForEdit() = {QDir(m_root.path()).filePath(QStringLiteral("light.fits")).toStdString()};
    core.recentFiles().save();
    EXPECT_TRUE(QFileInfo::exists(QDir(m_root.path()).filePath(QStringLiteral("recent_files"))));

    core.closeStores();
    EXPECT_FALSE(core.storesOpen());
    EXPECT_EQ(core.globalSettingsStore(), nullptr);
    EXPECT_EQ(core.folderStore(), nullptr);
    EXPECT_EQ(core.globalSettings()->select(QStringLiteral("shellcore/case"), QStringLiteral("closed")),
              QStringLiteral("closed"))
        << "a closed store answers the default";
    core.closeStores();
}

// What one run kept, the next reads back: the recent files as the stores open, the last folder, and the process lists
// when they are read.
TEST_F(ApplicationCoreTest, TheListsAndTheLastFolderAreReadBackFromTheStores)
{
    const std::string light = QDir(m_root.path()).filePath(QStringLiteral("light.fits")).toStdString();
    const QString folder = QDir(m_root.path()).filePath(QStringLiteral("lights"));
    ASSERT_TRUE(QDir().mkpath(folder));
    {
        ApplicationCore first;
        first.openStores();
        first.recentFiles().entriesForEdit() = {light};
        first.recentFiles().save();
        EXPECT_TRUE(first.recentProcesses().record("curves", "Curves"));
        first.favoriteProcesses().setIds({"levels"});
        first.setLastOpenDirectory(folder);
        EXPECT_EQ(first.lastOpenDirectory(), folder.toStdString());
        first.closeStores();
    }

    ApplicationCore later;
    later.openStores();
    EXPECT_EQ(later.recentFiles().entries(), (std::deque<std::string>{light}));
    EXPECT_EQ(later.lastOpenDirectory(), folder.toStdString());
    later.recentProcesses().load();
    ASSERT_EQ(later.recentProcesses().entries().size(), 1u);
    EXPECT_EQ(later.recentProcesses().entries().front().id, "curves");
    later.favoriteProcesses().load();
    EXPECT_EQ(later.favoriteProcesses().ids(), (std::set<std::string>{"levels"}));
}

// A folder on another machine is never remembered as the last one opened.
TEST_F(ApplicationCoreTest, DoesNotRememberAFolderOnAnotherMachine)
{
    const QString local = QDir(m_root.path()).filePath(QStringLiteral("darks"));
    ASSERT_TRUE(QDir().mkpath(local));
    {
        ApplicationCore first;
        first.openStores();
        first.setLastOpenDirectory(local);
        first.setLastOpenDirectory(QStringLiteral("//server/share/lights"));
        EXPECT_EQ(first.lastOpenDirectory(), "//server/share/lights") << "the run keeps it";
        first.closeStores();
    }

    ApplicationCore later;
    later.openStores();
    EXPECT_EQ(later.lastOpenDirectory(), local.toStdString()) << "the store does not";
}

} // namespace
