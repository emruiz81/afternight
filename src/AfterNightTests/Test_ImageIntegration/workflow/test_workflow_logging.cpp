// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <AfterNightPlatform/Logging.h>
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include "../../fixtures/ScopedTempPath.h"

#include <spdlog/sinks/base_sink.h>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <sstream>
#include <mutex>
#include <string>
#include <vector>

namespace
{
/**
 * @brief The log file the suite itself is configured to write to.
 *
 * Mirrors the test harness, which points logging at
 * <AFTERNIGHT_TEST_STORAGE_ROOT>/logs/AfterNight.log. The reconfigure test below restores the file
 * sink to this file, both to leave the logger as it found it and because spdlog holds the rotating
 * file open until the sink is replaced -- the temporary log cannot be removed before then.
 */
[[nodiscard]] std::string suiteLogFilePath()
{
    std::filesystem::path storage_root;
#ifdef _WIN32
    char* storage_root_value = nullptr;
    size_t storage_root_length = 0;
    if (_dupenv_s(&storage_root_value, &storage_root_length, "AFTERNIGHT_TEST_STORAGE_ROOT") == 0 &&
        storage_root_value != nullptr)
    {
        storage_root = storage_root_value;
        std::free(storage_root_value);
    }
#else
    if (const char* storage_root_value = std::getenv("AFTERNIGHT_TEST_STORAGE_ROOT"))
    {
        storage_root = storage_root_value;
    }
#endif

    if (storage_root.empty())
    {
        return "test.log";
    }

    return (storage_root / "logs" / "AfterNight.log").string();
}

template <typename Mutex> class string_sink : public spdlog::sinks::base_sink<Mutex>
{
public:
    std::string str()
    {
        std::lock_guard<Mutex> lock(this->mutex_);
        return oss_.str();
    }

    void clear()
    {
        std::lock_guard<Mutex> lock(this->mutex_);
        oss_.str("");
        oss_.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        oss_ << fmt::to_string(formatted);
    }

    void flush_() override {}

    std::ostringstream oss_;
};

using string_sink_mt = string_sink<std::mutex>;

class ScopedLogCapture
{
public:
    explicit ScopedLogCapture()
    {
        m_sink = std::make_shared<string_sink_mt>();
        m_sink->set_pattern("%v"); // Just the message

        auto logger = an::platform::detail::gLogger;
        if (logger)
        {
            an::platform::setSessionConsoleSink(m_sink);

            // Ensure we capture everything
            m_oldLevel = logger->level();
            logger->set_level(spdlog::level::trace);
        }
    }

    ~ScopedLogCapture()
    {
        auto logger = an::platform::detail::gLogger;
        if (logger)
        {
            an::platform::clearSessionConsoleSink();
            logger->set_level(m_oldLevel);
        }
    }

    std::string captured() const
    {
        return m_sink->str();
    }

private:
    std::shared_ptr<string_sink_mt> m_sink;
    spdlog::level::level_enum m_oldLevel;
};
} // namespace

TEST(WorkflowLogging, TraceLevelNotSpammy)
{
    // Need to initialize logger if not already
    if (!an::platform::isLoggingInitialized())
    {
        an::platform::initializeLogging("test.log", 1, 1, 100, an::platform::LogLevel::Trace);
    }

    ScopedLogCapture capture;

    const int total = 100;
    const int batchSize = 10;

    for (int i = 1; i <= total; ++i)
    {
        stage_utils::logBatchProgress("Processing", i, total, batchSize);
    }

    std::string log = capture.captured();

    // Should contain:
    // 1/100
    // 10/100
    // 20/100
    // ...
    // 100/100

    EXPECT_TRUE(log.find("Processing 1/100") != std::string::npos);
    EXPECT_TRUE(log.find("Processing 10/100") != std::string::npos);
    EXPECT_TRUE(log.find("Processing 100/100") != std::string::npos);

    // Should NOT contain:
    // 2/100
    // 11/100
    EXPECT_TRUE(log.find("Processing 2/100") == std::string::npos);
    EXPECT_TRUE(log.find("Processing 11/100") == std::string::npos);
}

TEST(StageUtilities, DescribeReferenceSelectionIncludesSourceAndChosenFrames)
{
    StackingSettings settings;
    settings.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    settings.manualNormalizationReference = QStringLiteral("C:/lights/ref.fit");

    const std::vector<FrameId> refs = {FrameId(QStringLiteral("C:/lights/ref.fit")),
                                       FrameId(QStringLiteral("C:/lights/secondary.fit"))};

    const QString summary = stage_utils::describeReferenceSelection(settings, refs);

    EXPECT_TRUE(summary.contains(QStringLiteral("Manual")));
    EXPECT_TRUE(summary.contains(QStringLiteral("ref.fit")));
    EXPECT_TRUE(summary.contains(QStringLiteral("secondary.fit")));
    EXPECT_TRUE(summary.contains(QStringLiteral("reference count: 2")));
}

// The run log names the reference a group used and calls it manual only when it is the one chosen by hand. It called
// any reference manual while a manual reference was set, even one the group fell back from.
TEST(StageUtilities, DescribeReferenceSelectionSaysWhenTheManualReferenceIsNotTheOneUsed)
{
    StackingSettings settings;
    settings.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    settings.manualNormalizationReference = QStringLiteral("C:/lights/ref.fit");

    const QString used =
        stage_utils::describeReferenceSelection(settings, {FrameId(QStringLiteral("C:/lights/ref.fit"))});
    EXPECT_TRUE(used.startsWith(QStringLiteral("Manual | primary: ref.fit"))) << used.toStdString();

    const QString other =
        stage_utils::describeReferenceSelection(settings, {FrameId(QStringLiteral("C:/lights/other.fit"))});
    EXPECT_FALSE(other.startsWith(QStringLiteral("Manual |"))) << other.toStdString();
    EXPECT_TRUE(other.contains(QStringLiteral("ref.fit is not used"))) << other.toStdString();
    EXPECT_TRUE(other.contains(QStringLiteral("primary: other.fit"))) << other.toStdString();
}

// The run log names the global match local normalization runs - Additive With Scaling, with the group's estimator -
// whatever method the group stores for when it is turned off. It named the reference strategy alone.
TEST(StageUtilities, DescribeNormalizationSettingsNamesTheGlobalMatchLocalNormalizationRuns)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
    settings.scaleEstimator = ScaleEstimatorMethod::MAD;

    const QString described = stage_utils::describeNormalizationSettings(settings);
    EXPECT_TRUE(described.contains(QStringLiteral("Additive With Scaling, set by local normalization")))
        << described.toStdString();
    EXPECT_TRUE(described.contains(QStringLiteral("MAD"))) << described.toStdString();
    EXPECT_TRUE(described.contains(QStringLiteral("Integrate Best Frames"))) << described.toStdString();
    EXPECT_FALSE(described.contains(QStringLiteral("Multiplicative"))) << described.toStdString();
}

// A drizzled Local group's stack fits its own surfaces against the group's reference, so the run log names no
// reference strategy for it, which does not apply; a drizzled Global group is described as any Global group. The log
// named the strategy of every Local group.
TEST(StageUtilities, DescribeNormalizationSettingsSaysADrizzledLocalGroupsStackFitsItsSurfaces)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");

    const QString drizzled = stage_utils::describeNormalizationSettings(settings, true);
    EXPECT_TRUE(drizzled.contains(QStringLiteral("fitted by the drizzled stack"))) << drizzled.toStdString();
    EXPECT_TRUE(drizzled.contains(QStringLiteral("Additive With Scaling"))) << drizzled.toStdString();
    EXPECT_FALSE(drizzled.contains(QStringLiteral("Single Best Frame"))) << drizzled.toStdString();
    EXPECT_TRUE(
        stage_utils::describeNormalizationSettings(settings, false).contains(QStringLiteral("Single Best Frame")));

    settings.normalizationMode = NormalizationMode::Global;
    settings.localNormalization.enabled = false;
    EXPECT_EQ(stage_utils::describeNormalizationSettings(settings, true),
              stage_utils::describeNormalizationSettings(settings, false));
}

TEST(WorkflowLogging, ReconfigurePreservesStableSinkSlots)
{
    if (!an::platform::isLoggingInitialized())
    {
        an::platform::initializeLogging("test.log", 1, 1, 100, an::platform::LogLevel::Trace);
    }

    auto logger = an::platform::detail::gLogger;
    ASSERT_NE(logger, nullptr);
    ASSERT_EQ(logger->sinks().size(), 3u);

    auto* initialFileSlot = logger->sinks()[0].get();
    auto* initialConsoleSlot = logger->sinks()[2].get();

    auto captureSink = std::make_shared<string_sink_mt>();
    captureSink->set_pattern("%v");

    an::platform::setSessionConsoleSink(captureSink);

    EXPECT_EQ(logger->sinks().size(), 3u);
    EXPECT_EQ(logger->sinks()[0].get(), initialFileSlot);
    EXPECT_EQ(logger->sinks()[2].get(), initialConsoleSlot);

    const auto reconfiguredDirectory = an::tests::fixtures::makeScopedTempDir("afternight_test_reconfigured_log");
    const auto reconfiguredLog = reconfiguredDirectory.filePath("afternight_test_reconfigured.log").string();

    // Declared after the directory owner, so it is destroyed first: the file sink is pointed back at
    // the suite log (closing the rotating file) before the directory is removed. This runs on the
    // failing path too, which a trailing statement would not.
    struct RestoreSuiteFileSink
    {
        ~RestoreSuiteFileSink()
        {
            try
            {
                an::platform::reconfigureFileSink(suiteLogFilePath(), 5, 10, an::platform::LogLevel::Info);
            }
            catch (const std::exception& error)
            {
                ADD_FAILURE() << "Failed to restore the suite log file sink: " << error.what();
            }
        }
    } restore_suite_file_sink;

    an::platform::reconfigureFileSink(reconfiguredLog, 2, 1, an::platform::LogLevel::Debug);

    EXPECT_EQ(logger->sinks().size(), 3u);
    EXPECT_EQ(logger->sinks()[0].get(), initialFileSlot);
    EXPECT_EQ(logger->sinks()[2].get(), initialConsoleSlot);

    an::platform::clearSessionConsoleSink();
}
