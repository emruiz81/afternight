// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The persisted logging configuration: where the log goes, how verbose it is and how it rotates.
///
/// The configuration is a small `key=value` text file read before Qt exists, so the very first records of a
/// run already go where the user asked. Its keys - `log_directory`, `log_level`, `max_file_size_mb` and
/// `max_files` - are a file format and keep their spelling; the C++ names follow the repository's naming
/// table. Unknown keys are ignored so an older build reads a newer file.
#pragma once

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/PlatformExport.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace an::platform
{

/// The logging settings as the configuration file stores them.
struct AFTERNIGHTPLATFORM_API LoggingConfig
{
    std::string logDirectory; ///< Directory holding `AfterNight.log` and its rotated siblings.
    std::string logLevel; ///< The file sink's threshold as a name that logLevelFromString reads.
    std::size_t maxFileSizeMb = 10; ///< Size in mebibytes at which the log file rotates, 1 to 1000.
    std::size_t maxFiles = 5; ///< Rotated files kept besides the active one, 1 to 100.

    /// Where the configuration file lives: `logging.conf` in the application's data directory.
    ///
    /// The data directory is `AFTERNIGHT_TEST_STORAGE_ROOT` when that variable is set and not empty, which is
    /// how a test keeps the production tree untouched; otherwise `%LOCALAPPDATA%\AfterNight` on Windows and
    /// `$HOME/.local/share/AfterNight` elsewhere, with the password database's home directory standing in for
    /// an unset `HOME`. The current directory is used when no home can be found.
    /// @return The configuration file's path.
    [[nodiscard]] static std::filesystem::path defaultConfigPath();

    /// Where log files go when the configuration names no directory: `logs` in the application's data
    /// directory, resolved as defaultConfigPath resolves it.
    /// @return The default log directory.
    [[nodiscard]] static std::filesystem::path defaultLogDirectory();
};

/// Reads a configuration file into `config`.
///
/// `config` is first reset to the defaults - the default log directory, `info`, 10 MiB and 5 files - so a
/// missing file or a missing key leaves a usable configuration. Keys are matched without case, blank lines
/// and lines starting with `#` or `;` are skipped, the level is stored lowercased, and a size or count outside
/// its range is clamped to it; one that does not parse falls back to its default.
/// @param configPath The file to read.
/// @param config Receives the configuration.
/// @return True when the file was opened and read, false when it could not be opened.
AFTERNIGHTPLATFORM_API bool loadLoggingConfig(const std::filesystem::path& configPath, LoggingConfig& config);

/// Reads the configuration file at LoggingConfig::defaultConfigPath into `config`.
/// @param config Receives the configuration, defaults included.
/// @return True when the file was opened and read.
AFTERNIGHTPLATFORM_API bool loadLoggingConfig(LoggingConfig& config);

/// Writes `config` to a configuration file, creating its directory when needed.
/// @param configPath The file to write; its contents are replaced.
/// @param config The configuration to store.
/// @return True when every line was written.
AFTERNIGHTPLATFORM_API bool saveLoggingConfig(const std::filesystem::path& configPath, const LoggingConfig& config);

/// Writes `config` to LoggingConfig::defaultConfigPath.
/// @param config The configuration to store.
/// @return True when every line was written.
AFTERNIGHTPLATFORM_API bool saveLoggingConfig(const LoggingConfig& config);

/// Creates the process logger from a configuration: the directory is created when missing, the file is
/// `AfterNight.log` inside it, and the ring buffer keeps 10000 records. Does nothing when the logger exists.
/// @param config The configuration to apply.
AFTERNIGHTPLATFORM_API void initializeLoggingFromConfig(const LoggingConfig& config);

/// Applies a configuration to the running logger through reconfigureFileSink, creating the directory when
/// missing; does nothing before initialization.
/// @param config The configuration to apply.
AFTERNIGHTPLATFORM_API void applyLoggingConfig(const LoggingConfig& config);

/// Reads a level name, without case: `trace`, `debug`, `info`, `warning` or `warn`, `error` or `err`,
/// `critical` or `exception`, and `off`.
/// @param text The name.
/// @return The level it names, or `Info` for any other text.
[[nodiscard]] AFTERNIGHTPLATFORM_API LogLevel logLevelFromString(const std::string& text);

/// The name the configuration file stores for a level: `trace`, `debug`, `info`, `warning`, `error`,
/// `critical` or `off`.
/// @param level The level.
/// @return Its name, or `info` for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::string logLevelToString(LogLevel level);

} // namespace an::platform
