// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The persisted logging configuration: its defaults, how the file is read and written, and how a configuration starts
/// or changes the logger.

#include "PlatformTestSupport.h"

#include "../fixtures/ScopedEnvVar.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/LoggingConfig.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

namespace platform = an::platform;

class LoggingConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform::shutdownLogging();
        m_directory = an::platform::test::caseDirectory();
    }

    void TearDown() override
    {
        platform::shutdownLogging();
    }

    [[nodiscard]] std::filesystem::path writeConfig(const std::string& text) const
    {
        const auto path = m_directory / "logging.conf";
        std::ofstream(path, std::ios::binary) << text;
        return path;
    }

    std::filesystem::path m_directory;
};

TEST_F(LoggingConfigTest, TheDefaultPathsLiveUnderTheStorageRoot)
{
    const auto root = an::platform::test::storageRoot();
    ASSERT_FALSE(root.empty());

    EXPECT_EQ(platform::LoggingConfig::defaultConfigPath(), root / "logging.conf");
    EXPECT_EQ(platform::LoggingConfig::defaultLogDirectory(), root / "logs");

    const auto elsewhere = m_directory / "elsewhere";
    TestHelpers::ScopedEnvVar redirected("AFTERNIGHT_TEST_STORAGE_ROOT", elsewhere.string().c_str());
    EXPECT_EQ(platform::LoggingConfig::defaultConfigPath(), elsewhere / "logging.conf");
}

TEST_F(LoggingConfigTest, AMissingFileLeavesTheDefaults)
{
    platform::LoggingConfig config;
    config.logDirectory = "stale";
    config.logLevel = "stale";
    config.maxFileSizeMb = 77;
    config.maxFiles = 77;

    EXPECT_FALSE(platform::loadLoggingConfig(m_directory / "absent.conf", config));

    EXPECT_EQ(config.logDirectory, platform::LoggingConfig::defaultLogDirectory().string());
    EXPECT_EQ(config.logLevel, "info");
    EXPECT_EQ(config.maxFileSizeMb, 10u);
    EXPECT_EQ(config.maxFiles, 5u);
}

TEST_F(LoggingConfigTest, KeysAreReadWithoutCaseAndCommentsAndUnknownKeysAreSkipped)
{
    const auto path = writeConfig("# a comment\n"
                                  "; another comment\n"
                                  "\n"
                                  "LOG_DIRECTORY =  /data/logs  \n"
                                  "Log_Level=DeBuG\r\n"
                                  "max_file_size_mb=25\n"
                                  "  max_files = 7\n"
                                  "some_future_key=whatever\n"
                                  "a line without a separator\n");
    platform::LoggingConfig config;

    ASSERT_TRUE(platform::loadLoggingConfig(path, config));

    EXPECT_EQ(config.logDirectory, "/data/logs");
    EXPECT_EQ(config.logLevel, "debug");
    EXPECT_EQ(config.maxFileSizeMb, 25u);
    EXPECT_EQ(config.maxFiles, 7u);
}

// A logging file as the Logging page writes it - level Debug, 25 MB files, seven kept - is read as it states them by
// the reader every start runs, its log directory kept as the file names it; saved again and read back, it says the
// same.
TEST_F(LoggingConfigTest, ALoggingFileThePageWroteIsReadAsItStatesIt)
{
    platform::LoggingConfig written;
    written.logDirectory = (m_directory / "storage" / "logs").generic_string();
    written.logLevel = "debug";
    written.maxFileSizeMb = 25;
    written.maxFiles = 7;
    const auto path = m_directory / "logging.conf";
    ASSERT_TRUE(platform::saveLoggingConfig(path, written));
    std::string directory;
    {
        std::ifstream stream(path);
        for (std::string line; std::getline(stream, line);)
        {
            if (line.rfind("log_directory=", 0) == 0)
            {
                directory = line.substr(std::string("log_directory=").size());
            }
        }
    }
    ASSERT_FALSE(directory.empty());

    platform::LoggingConfig config;
    ASSERT_TRUE(platform::loadLoggingConfig(path, config));
    EXPECT_EQ(config.logLevel, "debug");
    EXPECT_EQ(platform::logLevelFromString(config.logLevel), platform::LogLevel::Debug);
    EXPECT_EQ(config.maxFileSizeMb, 25u);
    EXPECT_EQ(config.maxFiles, 7u);
    EXPECT_EQ(config.logDirectory, directory);

    const auto again = m_directory / "again.conf";
    ASSERT_TRUE(platform::saveLoggingConfig(again, config));
    platform::LoggingConfig reread;
    ASSERT_TRUE(platform::loadLoggingConfig(again, reread));
    EXPECT_EQ(reread.logLevel, config.logLevel);
    EXPECT_EQ(reread.maxFileSizeMb, config.maxFileSizeMb);
    EXPECT_EQ(reread.maxFiles, config.maxFiles);
    EXPECT_EQ(reread.logDirectory, config.logDirectory);
}

TEST_F(LoggingConfigTest, OutOfRangeNumbersAreClampedAndUnreadableOnesFallBack)
{
    platform::LoggingConfig low;
    ASSERT_TRUE(platform::loadLoggingConfig(writeConfig("max_file_size_mb=0\nmax_files=0\n"), low));
    EXPECT_EQ(low.maxFileSizeMb, 1u);
    EXPECT_EQ(low.maxFiles, 1u);

    platform::LoggingConfig high;
    ASSERT_TRUE(platform::loadLoggingConfig(writeConfig("max_file_size_mb=5000\nmax_files=1000\n"), high));
    EXPECT_EQ(high.maxFileSizeMb, 1000u);
    EXPECT_EQ(high.maxFiles, 100u);

    platform::LoggingConfig garbled;
    ASSERT_TRUE(platform::loadLoggingConfig(writeConfig("max_file_size_mb=large\nmax_files=many\n"), garbled));
    EXPECT_EQ(garbled.maxFileSizeMb, 10u);
    EXPECT_EQ(garbled.maxFiles, 5u);
}

TEST_F(LoggingConfigTest, ASavedConfigurationReadsBackAndCreatesItsDirectory)
{
    platform::LoggingConfig saved;
    saved.logDirectory = "/var/log/afternight";
    saved.logLevel = "warning";
    saved.maxFileSizeMb = 42;
    saved.maxFiles = 9;
    const auto path = m_directory / "nested" / "deeper" / "logging.conf";

    ASSERT_TRUE(platform::saveLoggingConfig(path, saved));

    // The file is written in text mode, so Windows ends its lines with CR LF; the lines are what matter.
    std::string text = an::platform::test::readFile(path);
    std::erase(text, '\r');
    EXPECT_EQ(text, "# AfterNight Logging Configuration\n"
                    "# This file is auto-generated. Manual edits are preserved.\n"
                    "\n"
                    "log_directory=/var/log/afternight\n"
                    "log_level=warning\n"
                    "max_file_size_mb=42\n"
                    "max_files=9\n");
    platform::LoggingConfig loaded;
    ASSERT_TRUE(platform::loadLoggingConfig(path, loaded));
    EXPECT_EQ(loaded.logDirectory, saved.logDirectory);
    EXPECT_EQ(loaded.logLevel, saved.logLevel);
    EXPECT_EQ(loaded.maxFileSizeMb, saved.maxFileSizeMb);
    EXPECT_EQ(loaded.maxFiles, saved.maxFiles);
}

TEST_F(LoggingConfigTest, SavingFailsWhenItsDirectoryCannotBeCreated)
{
    std::ofstream(m_directory / "a-file") << "occupies the directory's name";

    EXPECT_FALSE(platform::saveLoggingConfig(m_directory / "a-file" / "logging.conf", platform::LoggingConfig{}));
}

TEST_F(LoggingConfigTest, LevelNamesAreReadWithoutCaseAndWrittenCanonically)
{
    EXPECT_EQ(platform::logLevelFromString("trace"), platform::LogLevel::Trace);
    EXPECT_EQ(platform::logLevelFromString("DEBUG"), platform::LogLevel::Debug);
    EXPECT_EQ(platform::logLevelFromString("Info"), platform::LogLevel::Info);
    EXPECT_EQ(platform::logLevelFromString("warning"), platform::LogLevel::Warn);
    EXPECT_EQ(platform::logLevelFromString("warn"), platform::LogLevel::Warn);
    EXPECT_EQ(platform::logLevelFromString("error"), platform::LogLevel::Error);
    EXPECT_EQ(platform::logLevelFromString("err"), platform::LogLevel::Error);
    EXPECT_EQ(platform::logLevelFromString("critical"), platform::LogLevel::Exception);
    EXPECT_EQ(platform::logLevelFromString("exception"), platform::LogLevel::Exception);
    EXPECT_EQ(platform::logLevelFromString("off"), platform::LogLevel::Off);
    EXPECT_EQ(platform::logLevelFromString("verbose"), platform::LogLevel::Info);
    EXPECT_EQ(platform::logLevelFromString(""), platform::LogLevel::Info);

    EXPECT_EQ(platform::logLevelToString(platform::LogLevel::Trace), "trace");
    EXPECT_EQ(platform::logLevelToString(platform::LogLevel::Debug), "debug");
    EXPECT_EQ(platform::logLevelToString(platform::LogLevel::Info), "info");
    EXPECT_EQ(platform::logLevelToString(platform::LogLevel::Warn), "warning");
    EXPECT_EQ(platform::logLevelToString(platform::LogLevel::Error), "error");
    EXPECT_EQ(platform::logLevelToString(platform::LogLevel::Exception), "critical");
    EXPECT_EQ(platform::logLevelToString(platform::LogLevel::Off), "off");
    EXPECT_EQ(platform::logLevelToString(static_cast<platform::LogLevel>(42)), "info");
}

TEST_F(LoggingConfigTest, AConfigurationStartsTheLoggerInItsDirectory)
{
    platform::LoggingConfig config;
    config.logDirectory = (m_directory / "created" / "logs").string();
    config.logLevel = "warning";

    platform::initializeLoggingFromConfig(config);
    ASSERT_TRUE(platform::isLoggingInitialized());
    LOG_WARNING("kept");
    LOG_INFO("filtered");
    platform::flushLogs();

    const std::string text = an::platform::test::readFile(m_directory / "created" / "logs" / "AfterNight.log");
    EXPECT_NE(text.find("kept"), std::string::npos) << text;
    EXPECT_EQ(text.find("filtered"), std::string::npos) << text;
}

TEST_F(LoggingConfigTest, ApplyingAConfigurationMovesTheRunningLogger)
{
    platform::LoggingConfig first;
    first.logDirectory = (m_directory / "first").string();
    first.logLevel = "info";
    platform::initializeLoggingFromConfig(first);

    platform::LoggingConfig second;
    second.logDirectory = (m_directory / "second").string();
    second.logLevel = "debug";
    second.maxFiles = 3;
    second.maxFileSizeMb = 2;
    platform::applyLoggingConfig(second);
    LOG_DEBUG("moved");
    platform::flushLogs();

    const std::string text = an::platform::test::readFile(m_directory / "second" / "AfterNight.log");
    EXPECT_NE(text.find("moved"), std::string::npos) << text;
    EXPECT_NE(text.find("maxFiles=3, maxSizeMB=2"), std::string::npos) << text;
}

TEST_F(LoggingConfigTest, ApplyingWithoutALoggerOnlyCreatesTheDirectory)
{
    platform::LoggingConfig config;
    config.logDirectory = (m_directory / "prepared").string();
    config.logLevel = "info";

    platform::applyLoggingConfig(config);

    EXPECT_TRUE(std::filesystem::is_directory(m_directory / "prepared"));
    EXPECT_FALSE(platform::isLoggingInitialized());
    EXPECT_FALSE(std::filesystem::exists(m_directory / "prepared" / "AfterNight.log"));
}

} // namespace
