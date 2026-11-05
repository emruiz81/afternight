// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/ScopedEnvVar.h"
#include "fixtures/harness/QtTestHarness.h"
#include "fixtures/harness/TestHarness.h"

#include <AfterNightPlatform/CrashHandler.h>
#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

namespace harness = an::tests::harness;

std::filesystem::path storageRoot()
{
    const std::optional<std::string> root = harness::environmentValue(harness::kStorageRootVariable);
    return root.has_value() ? std::filesystem::path(*root) : std::filesystem::path();
}

bool inside(const std::filesystem::path& path, const std::filesystem::path& directory)
{
    std::error_code error;
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, error);
    const std::filesystem::path canonicalDirectory = std::filesystem::weakly_canonical(directory, error);
    const auto [end, ignored] =
        std::mismatch(canonicalDirectory.begin(), canonicalDirectory.end(), canonicalPath.begin(), canonicalPath.end());
    return end == canonicalDirectory.end();
}

TEST(TestHarnessTest, TheSuiteRunsInsideAStorageRootWithATemporaryDirectoryOfItsOwn)
{
    const std::filesystem::path root = storageRoot();
    ASSERT_FALSE(root.empty());
    EXPECT_TRUE(std::filesystem::is_directory(root));
    // The operating system states a temporary directory with a trailing separator on Windows, where its last
    // component is then the one before it.
    std::filesystem::path temporary = std::filesystem::temp_directory_path();
    if (temporary.filename().empty())
    {
        temporary = temporary.parent_path();
    }
    EXPECT_EQ(temporary.filename().string().rfind("ANtmp-", 0), 0u) << temporary;
    EXPECT_TRUE(std::filesystem::is_directory(temporary));
}

TEST(TestHarnessTest, QtWasGivenTheRedirectedTemporaryDirectory)
{
    const std::filesystem::path qtTemporary = QDir::tempPath().toStdWString();
    EXPECT_EQ(qtTemporary.filename().string().rfind("ANtmp-", 0), 0u) << qtTemporary;
    EXPECT_TRUE(std::filesystem::equivalent(qtTemporary, std::filesystem::temp_directory_path()))
        << qtTemporary << " against " << std::filesystem::temp_directory_path();
}

TEST(TestHarnessTest, TheLoggerWritesIntoTheStorageRoot)
{
    ASSERT_TRUE(an::platform::isLoggingInitialized());
    LOG_INFO("TestHarnessTest writes one recognisable line");
    an::platform::flushLogs();
    const std::filesystem::path log = storageRoot() / "logs" / "AfterNight.log";
    ASSERT_TRUE(std::filesystem::is_regular_file(log)) << log;
    std::ifstream stream(log, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("TestHarnessTest writes one recognisable line"), std::string::npos);
}

TEST(TestHarnessTest, TheCrashHandlerWritesIntoTheStorageRoot)
{
    if (!an::platform::isCrashHandlerInitialized())
    {
        GTEST_SKIP() << "the crash handler did not start here, typically because no crashpad_handler is deployed "
                        "beside "
                     << harness::executablePath();
    }
    EXPECT_TRUE(inside(an::platform::crashDatabasePath(), storageRoot())) << an::platform::crashDatabasePath();
}

// Qt's own widget file dialog keeps its history in the native settings store, a file under the user's configuration
// directory where that store is not the registry, which a suite whose case opens one would otherwise rewrite.
TEST(TestHarnessTest, QtsOwnFileDialogStoreIsUnderTheStorageRoot)
{
#ifdef _WIN32
    GTEST_SKIP() << "Qt's native store is the registry on Windows, which no storage root redirects";
#else
    const QSettings dialogStore(QSettings::UserScope, QStringLiteral("QtProject"));
    const std::filesystem::path file = dialogStore.fileName().toStdString();
    EXPECT_TRUE(inside(file, storageRoot())) << file << " is outside " << storageRoot();
#endif
}

TEST(TestHarnessTest, TheApplicationIsHeadless)
{
    ASSERT_NE(QCoreApplication::instance(), nullptr);
    const QString platform = QGuiApplication::platformName();
    EXPECT_TRUE(platform == QStringLiteral("minimal") || platform == QStringLiteral("offscreen"))
        << platform.toStdString();
}

TEST(TestHarnessTest, AStorageRootTheRunnerDidNotSupplyIsOwnedRemovedAndForgotten)
{
    const std::filesystem::path outer = storageRoot();
    const std::filesystem::path outerTemporary = std::filesystem::temp_directory_path();
    std::filesystem::path owned;
    {
        TestHelpers::ScopedEnvVar cleared(harness::kStorageRootVariable, "");
        harness::TestStorage storage("Test_FixtureNeutralInner");
        ASSERT_TRUE(storage.valid());
        EXPECT_TRUE(storage.ownsRoot());
        owned = storage.root();
        EXPECT_NE(owned, outer);
        EXPECT_EQ(harness::environmentValue(harness::kStorageRootVariable).value_or(""), owned.string());
        EXPECT_NE(std::filesystem::temp_directory_path(), outerTemporary);
        EXPECT_TRUE(storage.release());
        EXPECT_FALSE(std::filesystem::exists(owned));
        EXPECT_FALSE(harness::environmentValue(harness::kStorageRootVariable).has_value());
        EXPECT_EQ(std::filesystem::temp_directory_path(), outerTemporary);
        EXPECT_TRUE(storage.release());
    }
    EXPECT_EQ(storageRoot(), outer);
}

TEST(TestHarnessTest, AStorageRootTheRunnerSuppliedIsLeftWhereItWas)
{
    const std::filesystem::path outer = storageRoot();
    {
        harness::TestStorage storage("Test_FixtureNeutralBorrowed");
        ASSERT_TRUE(storage.valid());
        EXPECT_FALSE(storage.ownsRoot());
        EXPECT_EQ(storage.root(), outer);
        EXPECT_TRUE(storage.release());
    }
    EXPECT_TRUE(std::filesystem::is_directory(outer));
    EXPECT_EQ(storageRoot(), outer);
}

} // namespace
