// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process logger's three sinks, their replacement at run time, and the level controls.

#include <AfterNightPlatform/Logging.h>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <array>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace an::platform
{

namespace
{

/// A sink whose target can be swapped while records flow through it.
///
/// The logger's sink list is fixed when the logger is built, so a sink that must change later - the log
/// file, the session console - is a slot that forwards to its current target. Filtering happens at the slot,
/// whose level is the one set_level changes; the target stays at its default trace level so the slot keeps
/// full control.
class ReplaceableSink final : public spdlog::sinks::sink
{
public:
    explicit ReplaceableSink(spdlog::sink_ptr sink = nullptr) : m_sink(std::move(sink)) {}

    void replaceSink(spdlog::sink_ptr sink)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sink = std::move(sink);
    }

    void log(const spdlog::details::log_msg& message) override
    {
        auto sink = currentSink();
        if (sink)
        {
            sink->log(message);
        }
    }

    void flush() override
    {
        auto sink = currentSink();
        if (sink)
        {
            sink->flush();
        }
    }

    void set_pattern(const std::string& pattern) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_sink)
        {
            m_sink->set_pattern(pattern);
        }
    }

    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_sink)
        {
            m_sink->set_formatter(std::move(formatter));
        }
    }

private:
    [[nodiscard]] spdlog::sink_ptr currentSink()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sink;
    }

    std::mutex m_mutex;
    spdlog::sink_ptr m_sink;
};

/// The spdlog level for each LogLevel, indexed by its value.
constexpr std::array<spdlog::level::level_enum, 7> kSpdlogLevels = {
    spdlog::level::off,  spdlog::level::critical, spdlog::level::err,  spdlog::level::warn,
    spdlog::level::info, spdlog::level::debug,    spdlog::level::trace};

/// The spdlog level for `level`, clamped to the enumeration.
[[nodiscard]] spdlog::level::level_enum spdlogLevel(const LogLevel level)
{
    int index = static_cast<int>(level);
    if (index < static_cast<int>(LogLevel::Off))
    {
        index = static_cast<int>(LogLevel::Off);
    }
    else if (index > static_cast<int>(LogLevel::Trace))
    {
        index = static_cast<int>(LogLevel::Trace);
    }
    return kSpdlogLevels[static_cast<std::size_t>(index)];
}

[[nodiscard]] spdlog::sink_ptr makeRotatingFileSink(const std::string& logFile, const std::size_t maxFiles,
                                                    const std::size_t maxFileSizeMb)
{
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFile, 1024 * 1024 * maxFileSizeMb, maxFiles);
    sink->set_pattern(std::string(kDefaultLogPattern));

    // spdlog only rotates within the current maximum, so files an earlier, larger maximum numbered beyond it
    // stay on disk until they are removed here. spdlog names a rotated file "stem.N.ext", such as
    // "AfterNight.6.log".
    const std::filesystem::path basePath(logFile);
    const auto stem = basePath.stem().string();
    const auto extension = basePath.extension().string();
    const auto directory = basePath.parent_path();
    std::error_code error;
    for (std::size_t index = maxFiles + 1;; ++index)
    {
        const auto surplus = directory / (stem + "." + std::to_string(index) + extension);
        if (!std::filesystem::exists(surplus, error))
        {
            break;
        }
        std::filesystem::remove(surplus, error);
    }

    return sink;
}

std::shared_ptr<ReplaceableSink> gFileSink;
std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> gRingBuffer;
std::shared_ptr<ReplaceableSink> gSessionConsoleSink;
bool gInitialized = false;

} // namespace

namespace detail
{
std::shared_ptr<spdlog::logger> gLogger;
} // namespace detail

void initializeLogging(const std::string& logFile, const std::size_t maxFiles, const std::size_t maxFileSizeMb,
                       const std::size_t maxRingBufferLines, const LogLevel level)
{
    if (gInitialized)
    {
        return;
    }

    gFileSink = std::make_shared<ReplaceableSink>(makeRotatingFileSink(logFile, maxFiles, maxFileSizeMb));
    gRingBuffer = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(maxRingBufferLines);
    gSessionConsoleSink = std::make_shared<ReplaceableSink>();

    const std::vector<spdlog::sink_ptr> sinks = {gFileSink, gRingBuffer, gSessionConsoleSink};
    detail::gLogger = std::make_shared<spdlog::logger>("", sinks.begin(), sinks.end());

    setLogLevel(LogLevel::Trace);
    setFileSinkLogLevel(level);

    gInitialized = true;
}

bool isLoggingInitialized()
{
    return gInitialized;
}

void shutdownLogging()
{
    if (!gInitialized)
    {
        return;
    }

    detail::gLogger->flush();
    detail::gLogger.reset();
    gFileSink.reset();
    gRingBuffer.reset();
    gSessionConsoleSink.reset();

    gInitialized = false;
}

void flushLogs()
{
    if (gInitialized)
    {
        detail::gLogger->flush();
    }
}

void setLogLevel(const LogLevel level)
{
    const auto spdlogThreshold = spdlogLevel(level);
    if (detail::gLogger)
    {
        detail::gLogger->set_level(spdlogThreshold);
        detail::gLogger->flush_on(spdlogThreshold);
    }
}

void setFileSinkLogLevel(const LogLevel level)
{
    if (gFileSink)
    {
        gFileSink->set_level(spdlogLevel(level));
    }
}

void reconfigureFileSink(const std::string& logFile, const std::size_t maxFiles, const std::size_t maxFileSizeMb,
                         const LogLevel level)
{
    if (!gInitialized || !detail::gLogger || !gFileSink)
    {
        return;
    }

    detail::gLogger->flush();
    gFileSink->replaceSink(makeRotatingFileSink(logFile, maxFiles, maxFileSizeMb));
    gFileSink->set_level(spdlogLevel(level));

    LOG_INFO("Logging reconfigured: file={}, maxFiles={}, maxSizeMB={}", logFile, maxFiles, maxFileSizeMb);
}

void setSessionConsoleSink(spdlog::sink_ptr sink)
{
    if (!gInitialized || !gSessionConsoleSink)
    {
        return;
    }

    gSessionConsoleSink->replaceSink(std::move(sink));
}

void clearSessionConsoleSink()
{
    if (!gInitialized || !gSessionConsoleSink)
    {
        return;
    }

    gSessionConsoleSink->replaceSink(nullptr);
}

void logToSink(const spdlog::sink_ptr& sink, const LogLevel level, const std::string& message)
{
    const spdlog::details::log_msg record("", spdlogLevel(level), message);
    sink->log(record);
}

std::vector<spdlog::details::log_msg_buffer> recentLogMessages(const std::size_t maxCount)
{
    if (!gRingBuffer)
    {
        return {};
    }

    return gRingBuffer->last_raw(maxCount);
}

} // namespace an::platform
