// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The crash handler: its default configuration, what it refuses, and a real start, dump and stop.

#include "PlatformTestSupport.h"

#include "../fixtures/ScopedEnvVar.h"

#include <AfterNightPlatform/CrashHandler.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{

namespace platform = an::platform;

#ifdef _WIN32
constexpr const char* kHandlerFileName = "crashpad_handler.exe";
#else
constexpr const char* kHandlerFileName = "crashpad_handler";
#endif

[[nodiscard]] std::filesystem::path executablePath()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return length > 0 && length < MAX_PATH ? std::filesystem::path(path) : std::filesystem::path();
#else
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path() : path;
#endif
}

class CrashHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform::shutdownCrashHandler();
        m_directory = an::platform::test::caseDirectory();
    }

    void TearDown() override
    {
        platform::shutdownCrashHandler();
    }

    /// An existing file standing in for the handler executable, for the checks made before it is started.
    [[nodiscard]] std::filesystem::path standInHandler() const
    {
        const auto path = m_directory / kHandlerFileName;
        std::ofstream(path) << "stand-in";
        return path;
    }

    std::filesystem::path m_directory;
};

TEST_F(CrashHandlerTest, NothingRunsBeforeInitialization)
{
    EXPECT_FALSE(platform::isCrashHandlerInitialized());
    EXPECT_TRUE(platform::crashDatabasePath().empty());

    platform::setCrashAnnotation("key", "value");
    platform::dumpWithoutCrash();
    platform::shutdownCrashHandler();

    EXPECT_FALSE(platform::isCrashHandlerInitialized());
}

TEST_F(CrashHandlerTest, TheDefaultConfigurationStartsTheHandlerBesideTheApplication)
{
    const auto handler = standInHandler();

    const auto config = platform::defaultCrashHandlerConfig(m_directory / "AfterNight", "9.8.7");

    EXPECT_EQ(config.handlerPath, handler);
    EXPECT_EQ(config.productName, "AfterNight");
    EXPECT_EQ(config.productVersion, "9.8.7");
    EXPECT_FALSE(config.uploadEnabled);
    EXPECT_TRUE(config.uploadUrl.empty());
}

TEST_F(CrashHandlerTest, TheVersionIsTheCallersAndEmptyWhenNoneIsStated)
{
    EXPECT_TRUE(platform::defaultCrashHandlerConfig(m_directory / "AfterNight").productVersion.empty());
}

TEST_F(CrashHandlerTest, DumpsGoToTheUserDataDirectory)
{
    // Only a run under no test storage root names the user's directory; the case reads the name and writes nothing.
    TestHelpers::ScopedEnvVar noStorageRoot("AFTERNIGHT_TEST_STORAGE_ROOT", "");
#ifdef _WIN32
    const auto config = platform::defaultCrashHandlerConfig(m_directory / "AfterNight.exe");
    EXPECT_EQ(config.databasePath.filename(), "CrashDumps");
    EXPECT_EQ(config.databasePath.parent_path().filename(), "AfterNight");
    EXPECT_EQ(config.metricsPath, config.databasePath.parent_path() / "CrashMetrics");
#else
    {
        TestHelpers::ScopedEnvVar dataHome("XDG_DATA_HOME", (m_directory / "xdg").string().c_str());
        const auto config = platform::defaultCrashHandlerConfig(m_directory / "AfterNight");
        EXPECT_EQ(config.databasePath, m_directory / "xdg" / "AfterNight" / "CrashDumps");
        EXPECT_EQ(config.metricsPath, m_directory / "xdg" / "AfterNight" / "CrashMetrics");
    }
    {
        TestHelpers::ScopedEnvVar dataHome("XDG_DATA_HOME", "");
        TestHelpers::ScopedEnvVar home("HOME", (m_directory / "home").string().c_str());
        const auto config = platform::defaultCrashHandlerConfig(m_directory / "AfterNight");
        EXPECT_EQ(config.databasePath, m_directory / "home" / ".local" / "share" / "AfterNight" / "CrashDumps");
    }
#endif
}

// A process run under a test storage root keeps its crash database there, as its logs and settings are kept: the
// handler rewrites its settings at every start, so a database in the user's directory would be written by every run
// that starts it, crash or not.
TEST_F(CrashHandlerTest, DumpsGoUnderTheTestStorageRootWhenOneIsSet)
{
    const std::filesystem::path root = m_directory / "storage";
    TestHelpers::ScopedEnvVar storageRoot("AFTERNIGHT_TEST_STORAGE_ROOT", root.string().c_str());

    const auto config = platform::defaultCrashHandlerConfig(m_directory / "AfterNight");

    EXPECT_EQ(config.databasePath, root / "CrashDumps");
    EXPECT_EQ(config.metricsPath, root / "CrashMetrics");
}

TEST_F(CrashHandlerTest, AMissingHandlerIsRefusedBeforeTheDatabaseIsCreated)
{
    platform::CrashHandlerConfig config;
    config.handlerPath = m_directory / "no-such-handler";
    config.databasePath = m_directory / "database";

    EXPECT_FALSE(platform::initializeCrashHandler(config));

    EXPECT_FALSE(platform::isCrashHandlerInitialized());
    EXPECT_FALSE(std::filesystem::exists(config.databasePath));
}

#ifndef _WIN32
// A handler file the system will not execute - vcpkg installs Crashpad's without the permission - is refused by name
// before anything is started: started, Crashpad's intermediate process aborts with a core dump and says nothing.
TEST_F(CrashHandlerTest, AHandlerThatCannotBeExecutedIsRefusedBeforeItIsStarted)
{
    platform::CrashHandlerConfig config;
    config.handlerPath = standInHandler();
    std::filesystem::permissions(config.handlerPath,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    config.databasePath = m_directory / "database";

    EXPECT_FALSE(platform::initializeCrashHandler(config));

    EXPECT_FALSE(platform::isCrashHandlerInitialized());
    EXPECT_FALSE(std::filesystem::exists(config.databasePath));
}
#endif

TEST_F(CrashHandlerTest, AnEmptyHandlerOrDatabasePathIsRefused)
{
    platform::CrashHandlerConfig noHandler;
    noHandler.databasePath = m_directory / "database";
    EXPECT_FALSE(platform::initializeCrashHandler(noHandler));

    platform::CrashHandlerConfig noDatabase;
    noDatabase.handlerPath = standInHandler();
    EXPECT_FALSE(platform::initializeCrashHandler(noDatabase));

    EXPECT_FALSE(platform::isCrashHandlerInitialized());
}

TEST_F(CrashHandlerTest, TheHandlerStartsWritesADumpAndStops)
{
#if !defined(_WIN32) && !defined(__linux__)
    GTEST_SKIP() << "Crashpad runs only on Windows and Linux";
#endif
    auto config = platform::defaultCrashHandlerConfig(executablePath(), "0.0.0-test");
    if (!std::filesystem::exists(config.handlerPath))
    {
        GTEST_SKIP() << "no crashpad_handler at " << config.handlerPath.string()
                     << "; the deploy step puts one beside the suite";
    }
    config.databasePath = m_directory / "crash-database";
    config.metricsPath = m_directory / "crash-metrics";

    ASSERT_TRUE(platform::initializeCrashHandler(config));
    EXPECT_TRUE(platform::isCrashHandlerInitialized());
    EXPECT_EQ(platform::crashDatabasePath(), config.databasePath);
    EXPECT_TRUE(platform::initializeCrashHandler(config));

    platform::setCrashAnnotation("test_suite", "Test_Platform");
    platform::dumpWithoutCrash();

    bool dumpWritten = false;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(config.databasePath, error))
    {
        dumpWritten = dumpWritten || entry.path().extension() == ".dmp";
    }
    EXPECT_TRUE(dumpWritten) << "no minidump under " << config.databasePath.string();

    platform::shutdownCrashHandler();
    EXPECT_FALSE(platform::isCrashHandlerInitialized());
    EXPECT_TRUE(platform::crashDatabasePath().empty());
}

} // namespace
