// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process logger: where a record goes, which records pass, and how its sinks are replaced.

#include "PlatformTestSupport.h"

#include <AfterNightPlatform/Logging.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace
{

namespace platform = an::platform;
using an::platform::test::CaptureSink;

/// Every case starts with no logger and a directory of its own, and leaves no logger behind.
class LoggingTest : public ::testing::Test
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

    [[nodiscard]] std::filesystem::path logFile(const char* name = "AfterNight.log") const
    {
        return m_directory / name;
    }

    [[nodiscard]] static std::string fileText(const std::filesystem::path& path)
    {
        platform::flushLogs();
        return an::platform::test::readFile(path);
    }

    std::filesystem::path m_directory;
};

TEST_F(LoggingTest, MacrosAndControlsDoNothingWhileNoLoggerExists)
{
    ASSERT_FALSE(platform::isLoggingInitialized());
    ASSERT_EQ(platform::detail::gLogger, nullptr);

    LOG_ERROR("dropped {}", 1);
    LOG_WARNING("dropped");
    LOG_INFO("dropped");
    LOG_DEBUG("dropped");
    LOG_TRACE("dropped");
    LOG_ERROR_IF(true, "dropped");
    LOG_FLUSH;
    platform::flushLogs();
    platform::setFileSinkLogLevel(platform::LogLevel::Trace);
    auto capture = std::make_shared<CaptureSink>();
    platform::setSessionConsoleSink(capture);
    platform::clearSessionConsoleSink();
    platform::reconfigureFileSink(logFile().string(), 2, 1, platform::LogLevel::Info);

    EXPECT_FALSE(platform::isLoggingInitialized());
    EXPECT_TRUE(platform::recentLogMessages(0).empty());
    EXPECT_TRUE(capture->messages().empty());
    EXPECT_FALSE(std::filesystem::exists(logFile()));
}

TEST_F(LoggingTest, RecordsReachTheFileInTheDefaultPattern)
{
    platform::initializeLogging(logFile().string(), 5, 10, 100, platform::LogLevel::Trace);
    ASSERT_TRUE(platform::isLoggingInitialized());

    LOG_INFO("hello {}", 42);
    const std::string text = fileText(logFile());

    const std::regex line(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} - info  - \d+\s* - \d+\s* - hello 42)");
    EXPECT_TRUE(std::regex_search(text, line)) << text;
}

TEST_F(LoggingTest, TheWarnLevelIsSpelledWarn)
{
    platform::initializeLogging(logFile().string(), 5, 10, 100, platform::LogLevel::Trace);

    LOG_WARNING("careful");
    const std::string text = fileText(logFile());

    EXPECT_NE(text.find(" - warn  - "), std::string::npos) << text;
    EXPECT_EQ(text.find("warning"), std::string::npos) << text;
}

TEST_F(LoggingTest, TheFileSinkFiltersBelowItsLevelWhileTheConsoleSeesEverything)
{
    platform::initializeLogging(logFile().string(), 5, 10, 100, platform::LogLevel::Warn);
    auto capture = std::make_shared<CaptureSink>();
    platform::setSessionConsoleSink(capture);

    LOG_INFO("quiet");
    LOG_ERROR("loud");
    const std::string text = fileText(logFile());

    EXPECT_EQ(text.find("quiet"), std::string::npos) << text;
    EXPECT_NE(text.find("loud"), std::string::npos) << text;
    EXPECT_EQ(capture->messages(), (std::vector<std::string>{"quiet", "loud"}));
}

TEST_F(LoggingTest, ASecondInitializationKeepsTheFirstLogger)
{
    platform::initializeLogging(logFile("first.log").string(), 5, 10, 100, platform::LogLevel::Trace);
    platform::initializeLogging(logFile("second.log").string(), 5, 10, 100, platform::LogLevel::Trace);

    LOG_INFO("only once");

    EXPECT_NE(fileText(logFile("first.log")).find("only once"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(logFile("second.log")));
}

TEST_F(LoggingTest, ShutdownReleasesTheLoggerAndALaterInitializationStartsAgain)
{
    platform::initializeLogging(logFile("before.log").string(), 5, 10, 100, platform::LogLevel::Trace);
    LOG_INFO("before shutdown");
    platform::shutdownLogging();

    EXPECT_FALSE(platform::isLoggingInitialized());
    EXPECT_EQ(platform::detail::gLogger, nullptr);
    EXPECT_NE(an::platform::test::readFile(logFile("before.log")).find("before shutdown"), std::string::npos);
    LOG_INFO("dropped after shutdown");

    platform::initializeLogging(logFile("after.log").string(), 5, 10, 100, platform::LogLevel::Trace);
    LOG_INFO("after restart");
    EXPECT_NE(fileText(logFile("after.log")).find("after restart"), std::string::npos);
    EXPECT_EQ(an::platform::test::readFile(logFile("before.log")).find("dropped after shutdown"), std::string::npos);
}

TEST_F(LoggingTest, TheSessionConsoleReceivesRecordsUntilItIsCleared)
{
    platform::initializeLogging(logFile().string(), 5, 10, 100, platform::LogLevel::Trace);
    auto capture = std::make_shared<CaptureSink>();

    platform::setSessionConsoleSink(capture);
    LOG_INFO("one");
    platform::clearSessionConsoleSink();
    LOG_INFO("two");

    EXPECT_EQ(capture->messages(), (std::vector<std::string>{"one"}));
}

TEST_F(LoggingTest, ReplacingSinksKeepsTheLoggersThreeSlots)
{
    platform::initializeLogging(logFile().string(), 5, 10, 100, platform::LogLevel::Trace);
    const auto logger = platform::detail::gLogger;
    ASSERT_NE(logger, nullptr);
    ASSERT_EQ(logger->sinks().size(), 3u);
    const auto* fileSlot = logger->sinks()[0].get();
    const auto* ringSlot = logger->sinks()[1].get();
    const auto* consoleSlot = logger->sinks()[2].get();

    platform::setSessionConsoleSink(std::make_shared<CaptureSink>());
    platform::reconfigureFileSink(logFile("moved.log").string(), 2, 1, platform::LogLevel::Debug);
    platform::clearSessionConsoleSink();

    ASSERT_EQ(logger->sinks().size(), 3u);
    EXPECT_EQ(logger->sinks()[0].get(), fileSlot);
    EXPECT_EQ(logger->sinks()[1].get(), ringSlot);
    EXPECT_EQ(logger->sinks()[2].get(), consoleSlot);
}

TEST_F(LoggingTest, ReconfiguringMovesTheFileAndAnnouncesTheChangeThere)
{
    platform::initializeLogging(logFile("old.log").string(), 5, 10, 100, platform::LogLevel::Info);
    LOG_INFO("in the old file");

    const std::string moved = logFile("new.log").string();
    platform::reconfigureFileSink(moved, 2, 1, platform::LogLevel::Debug);
    LOG_DEBUG("in the new file");

    const std::string newText = fileText(moved);
    EXPECT_NE(newText.find("Logging reconfigured: file=" + moved + ", maxFiles=2, maxSizeMB=1"), std::string::npos)
        << newText;
    EXPECT_NE(newText.find("in the new file"), std::string::npos) << newText;
    const std::string oldText = an::platform::test::readFile(logFile("old.log"));
    EXPECT_NE(oldText.find("in the old file"), std::string::npos);
    EXPECT_EQ(oldText.find("in the new file"), std::string::npos);
}

TEST_F(LoggingTest, SurplusRotatedFilesAreRemovedUpToTheFirstGap)
{
    for (const char* name :
         {"AfterNight.1.log", "AfterNight.2.log", "AfterNight.3.log", "AfterNight.4.log", "AfterNight.6.log"})
    {
        std::ofstream(m_directory / name) << "rotated";
    }

    platform::initializeLogging(logFile().string(), 2, 10, 100, platform::LogLevel::Info);

    EXPECT_TRUE(std::filesystem::exists(m_directory / "AfterNight.1.log"));
    EXPECT_TRUE(std::filesystem::exists(m_directory / "AfterNight.2.log"));
    EXPECT_FALSE(std::filesystem::exists(m_directory / "AfterNight.3.log"));
    EXPECT_FALSE(std::filesystem::exists(m_directory / "AfterNight.4.log"));
    EXPECT_TRUE(std::filesystem::exists(m_directory / "AfterNight.6.log"));
}

TEST_F(LoggingTest, TheRingBufferReturnsTheNewestRecordsOldestFirst)
{
    platform::initializeLogging(logFile().string(), 5, 10, 100, platform::LogLevel::Info);
    LOG_INFO("first");
    LOG_INFO("second");
    LOG_INFO("third");

    const auto lastTwo = platform::recentLogMessages(2);
    ASSERT_EQ(lastTwo.size(), 2u);
    EXPECT_EQ(std::string(lastTwo[0].payload.data(), lastTwo[0].payload.size()), "second");
    EXPECT_EQ(std::string(lastTwo[1].payload.data(), lastTwo[1].payload.size()), "third");
    EXPECT_EQ(platform::recentLogMessages(0).size(), 3u);
}

TEST_F(LoggingTest, TheRingBufferKeepsNoMoreThanItsCapacity)
{
    platform::initializeLogging(logFile().string(), 5, 10, 2, platform::LogLevel::Info);
    LOG_INFO("first");
    LOG_INFO("second");
    LOG_INFO("third");

    const auto all = platform::recentLogMessages(0);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(std::string(all[0].payload.data(), all[0].payload.size()), "second");
}

TEST_F(LoggingTest, LogToSinkDeliversOneRecordAtTheGivenLevelWithoutALogger)
{
    auto capture = std::make_shared<CaptureSink>();
    const spdlog::sink_ptr sink = capture;

    platform::logToSink(sink, platform::LogLevel::Warn, "warned");
    platform::logToSink(sink, platform::LogLevel::Exception, "critical");
    platform::logToSink(sink, static_cast<platform::LogLevel>(42), "clamped");

    EXPECT_EQ(capture->messages(), (std::vector<std::string>{"warned", "critical", "clamped"}));
    EXPECT_EQ(capture->levels(), (std::vector<spdlog::level::level_enum>{spdlog::level::warn, spdlog::level::critical,
                                                                         spdlog::level::trace}));
}

TEST_F(LoggingTest, TheLoggersOwnLevelFiltersEverySinkAndIsClamped)
{
    platform::initializeLogging(logFile().string(), 5, 10, 100, platform::LogLevel::Trace);
    ASSERT_NE(platform::detail::gLogger, nullptr);
    auto capture = std::make_shared<CaptureSink>();
    platform::setSessionConsoleSink(capture);

    platform::setLogLevel(platform::LogLevel::Error);
    LOG_WARNING("below the logger");
    LOG_ERROR("at the logger");
    EXPECT_EQ(capture->messages(), (std::vector<std::string>{"at the logger"}));
    EXPECT_EQ(platform::detail::gLogger->level(), spdlog::level::err);

    platform::setLogLevel(static_cast<platform::LogLevel>(99));
    EXPECT_EQ(platform::detail::gLogger->level(), spdlog::level::trace);
    platform::setLogLevel(platform::LogLevel::Off);
    EXPECT_EQ(platform::detail::gLogger->level(), spdlog::level::off);
}

TEST_F(LoggingTest, TheFileSinkLevelIsSetAloneAndClamped)
{
    platform::initializeLogging(logFile().string(), 5, 10, 100, platform::LogLevel::Info);
    const auto logger = platform::detail::gLogger;
    ASSERT_NE(logger, nullptr);
    ASSERT_EQ(logger->sinks().size(), 3u);

    platform::setFileSinkLogLevel(platform::LogLevel::Error);
    EXPECT_EQ(logger->sinks()[0]->level(), spdlog::level::err);
    EXPECT_EQ(logger->level(), spdlog::level::trace);

    platform::setFileSinkLogLevel(static_cast<platform::LogLevel>(99));
    EXPECT_EQ(logger->sinks()[0]->level(), spdlog::level::trace);
}

} // namespace
