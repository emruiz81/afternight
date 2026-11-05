// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application's logger: one spdlog logger per process, and the LOG_* macros every module logs through.
///
/// The logger writes to three sinks in a fixed order - a rotating log file, an in-memory ring buffer of
/// recent records, and a session-console slot the Qt console fills and empties - and each record reaches all
/// three. The file sink is the only one filtered by the configured level; the logger itself passes
/// everything at or above its own level, which initialization sets to trace.
///
/// Every LOG_* macro is safe before initialization and after shutdown: with no logger the record is
/// dropped and its arguments are never formatted. The macros keep their names because a macro is not a
/// type and the whole product calls them; the functions follow the repository's naming table.
///
/// The logger is process state with a named owner - the application or tool that initializes it - and not
/// an engine service: no engine configures or reads it.
#pragma once

#include <AfterNightPlatform/PlatformExport.h>

#define SPDLOG_FMT_EXTERNAL
#define SPDLOG_DISABLE_DEFAULT_LOGGER
#ifdef WIN32
#define SPDLOG_WCHAR_TO_UTF8_SUPPORT
#endif
#define SPDLOG_HEADER_ONLY

// Use "warn" instead of the default "warning" for the warn level.
#define SPDLOG_LEVEL_NAMES                                                                                    \
    {spdlog::string_view_t("trace", 5), spdlog::string_view_t("debug", 5), spdlog::string_view_t("info", 4),  \
     spdlog::string_view_t("warn", 4),  spdlog::string_view_t("error", 5), spdlog::string_view_t("fatal", 5), \
     spdlog::string_view_t("off", 3)}

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/xchar.h>

#include <spdlog/spdlog.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an::platform
{

/// What the LOG_* macros expand against. Not an interface: call the functions below instead.
namespace detail
{

/// The process logger, or null before initialization and after shutdown. Exported so a LOG_* expansion in
/// any module reaches the one logger without a call; written only by initializeLogging and shutdownLogging.
extern AFTERNIGHTPLATFORM_API std::shared_ptr<::spdlog::logger> gLogger;

/// Hands one preformatted record to the logger, or drops it when there is none.
/// @param level The record's severity.
/// @param message The record's text.
template <typename Message> inline void logIfReady(const ::spdlog::level::level_enum level, Message&& message)
{
    if (gLogger)
    {
        gLogger->log(level, std::forward<Message>(message));
    }
}

/// Formats and hands one record to the logger, or drops it unformatted when there is none.
/// @param level The record's severity.
/// @param format A compile-time checked fmt format string.
/// @param args The values the format string names.
template <typename... Args>
inline void logIfReady(const ::spdlog::level::level_enum level, ::spdlog::format_string_t<Args...> format,
                       Args&&... args)
{
    if (gLogger)
    {
        gLogger->log(level, format, std::forward<Args>(args)...);
    }
}

/// Flushes every sink of the logger, or does nothing when there is none.
inline void flushIfReady()
{
    if (gLogger)
    {
        gLogger->flush();
    }
}

} // namespace detail

/// The line layout of the log file: date and time with milliseconds, level, process, thread and message.
inline constexpr std::string_view kDefaultLogPattern = "%Y-%m-%d %H:%M:%S.%e - %-5l - %-5P - %-5t - %v";

/// The line layout of the session console, which shows neither the process nor the thread.
inline constexpr std::string_view kConsoleLogPattern = "%Y-%m-%d %H:%M:%S.%e - %-5l - %v";

/// A severity threshold, from `Off` (nothing passes) to `Trace` (everything passes).
///
/// `Exception` is spdlog's critical level. A value outside the enumeration is clamped by every function that
/// takes one: below `Off` means `Off`, above `Trace` means `Trace`.
enum class LogLevel : char
{
    Off = 0,
    Exception,
    Error,
    Warn,
    Info,
    Debug,
    Trace,
};

/// Creates the process logger with its file, ring-buffer and session-console sinks.
///
/// Does nothing when the logger already exists. The logger's own level becomes `Trace` and flushes at every
/// record; `level` filters the file sink only. Rotated files an earlier, larger setting left beside `logFile`
/// are removed, numbered from `maxFiles + 1` upward until the first number with no file. Call it from the
/// thread that starts the process, before any other thread logs.
/// @param logFile The log file; its directory must exist. spdlog throws `spdlog::spdlog_ex` when it cannot
/// open the file.
/// @param maxFiles How many rotated files to keep besides the active one.
/// @param maxFileSizeMb The size in mebibytes at which the file rotates.
/// @param maxRingBufferLines How many recent records the ring buffer keeps.
/// @param level The file sink's threshold.
AFTERNIGHTPLATFORM_API void initializeLogging(const std::string& logFile, std::size_t maxFiles = 5,
                                              std::size_t maxFileSizeMb = 10, std::size_t maxRingBufferLines = 10000,
                                              LogLevel level = LogLevel::Info);

/// Whether the process logger exists.
/// @return True between initializeLogging and shutdownLogging.
[[nodiscard]] AFTERNIGHTPLATFORM_API bool isLoggingInitialized();

/// Flushes and releases the process logger and its sinks; does nothing when there is none.
///
/// Call it after every other thread has stopped logging: a LOG_* expansion racing the release is not
/// synchronized.
AFTERNIGHTPLATFORM_API void shutdownLogging();

/// Flushes every sink of the process logger; does nothing when there is none.
AFTERNIGHTPLATFORM_API void flushLogs();

/// Sets the logger's own threshold and makes it flush at every record at or above that threshold.
///
/// Keep it at `Trace` and filter per sink, as initializeLogging does: a record the logger drops reaches no
/// sink at all, the session console included.
/// @param level The threshold, clamped to the enumeration.
AFTERNIGHTPLATFORM_API void setLogLevel(LogLevel level);

/// Sets the threshold of the file sink alone; does nothing before initialization.
/// @param level The threshold, clamped to the enumeration.
AFTERNIGHTPLATFORM_API void setFileSinkLogLevel(LogLevel level);

/// Makes `sink` receive every record the logger passes, replacing whatever the console slot held.
///
/// The slot is swapped under its own lock, so a record in flight reaches either the old sink or the new one.
/// Does nothing before initialization.
/// @param sink The sink to install; null empties the slot.
AFTERNIGHTPLATFORM_API void setSessionConsoleSink(spdlog::sink_ptr sink);

/// Empties the session-console slot; does nothing before initialization.
AFTERNIGHTPLATFORM_API void clearSessionConsoleSink();

/// Points the file sink at a new file with new rotation limits and a new threshold, without a restart.
///
/// The sink's slot in the logger keeps its identity: only its target changes. Pending records are flushed to
/// the old file first, and the change is logged to the new one. Does nothing before initialization.
/// @param logFile The new log file; its directory must exist.
/// @param maxFiles How many rotated files to keep besides the active one.
/// @param maxFileSizeMb The size in mebibytes at which the file rotates.
/// @param level The file sink's threshold, clamped to the enumeration.
AFTERNIGHTPLATFORM_API void reconfigureFileSink(const std::string& logFile, std::size_t maxFiles,
                                                std::size_t maxFileSizeMb, LogLevel level);

/// Hands one record directly to `sink`, bypassing the process logger.
///
/// The record carries an empty logger name. Works whether or not the logger exists.
/// @param sink The sink to write to; must not be null.
/// @param level The record's severity, clamped to the enumeration.
/// @param message The record's text.
AFTERNIGHTPLATFORM_API void logToSink(const spdlog::sink_ptr& sink, LogLevel level, const std::string& message);

/// The most recent records the ring buffer holds, oldest first.
/// @param maxCount The most records to return; zero returns everything the buffer holds.
/// @return At most `maxCount` records, or none before initialization.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::vector<spdlog::details::log_msg_buffer>
recentLogMessages(std::size_t maxCount);

} // namespace an::platform

#define LOG_ERROR(...) ::an::platform::detail::logIfReady(::spdlog::level::err, __VA_ARGS__)
#define LOG_WARNING(...) ::an::platform::detail::logIfReady(::spdlog::level::warn, __VA_ARGS__)
#define LOG_INFO(...) ::an::platform::detail::logIfReady(::spdlog::level::info, __VA_ARGS__)
#define LOG_DEBUG(...) ::an::platform::detail::logIfReady(::spdlog::level::debug, __VA_ARGS__)
#define LOG_TRACE(...) ::an::platform::detail::logIfReady(::spdlog::level::trace, __VA_ARGS__)

#define LOG_ERROR_IF(cond, ...) ((cond) ? (void)LOG_ERROR(__VA_ARGS__) : (void)0)
#define LOG_WARNING_IF(cond, ...) ((cond) ? (void)LOG_WARNING(__VA_ARGS__) : (void)0)
#define LOG_INFO_IF(cond, ...) ((cond) ? (void)LOG_INFO(__VA_ARGS__) : (void)0)
#define LOG_DEBUG_IF(cond, ...) ((cond) ? (void)LOG_DEBUG(__VA_ARGS__) : (void)0)
#define LOG_TRACE_IF(cond, ...) ((cond) ? (void)LOG_TRACE(__VA_ARGS__) : (void)0)

#define LOG_FLUSH ::an::platform::detail::flushIfReady()
