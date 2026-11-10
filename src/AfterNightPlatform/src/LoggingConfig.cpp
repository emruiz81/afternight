// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Reading, writing and applying the persisted logging configuration.

#include <AfterNightPlatform/LoggingConfig.h>

#include <AfterNightPlatform/Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace an::platform
{

namespace
{

/// The size of each rotated file and the number kept, when the file names none.
constexpr std::size_t kDefaultMaxFileSizeMb = 10;
constexpr std::size_t kDefaultMaxFiles = 5;

/// How many records the ring buffer keeps for a logger started from a configuration.
constexpr std::size_t kRingBufferLines = 10000;

/// The file every configuration logs to, inside its directory.
constexpr const char* kLogFileName = "AfterNight.log";

[[nodiscard]] std::string trim(const std::string& text)
{
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

[[nodiscard]] std::string toLower(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

/// The application's data directory: the test storage root when one is set, the user's data directory
/// otherwise, and the current directory when there is no home to put it in.
[[nodiscard]] std::filesystem::path applicationDataDirectory()
{
#ifdef WIN32
    wchar_t testStorageRoot[MAX_PATH] = {};
    const DWORD testStorageRootLength =
        GetEnvironmentVariableW(L"AFTERNIGHT_TEST_STORAGE_ROOT", testStorageRoot, MAX_PATH);
    if (testStorageRootLength > 0 && testStorageRootLength < MAX_PATH)
    {
        return std::filesystem::path(testStorageRoot);
    }

    wchar_t* path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
    {
        std::filesystem::path appDataPath(path);
        CoTaskMemFree(path);
        return appDataPath / "AfterNight";
    }
    return std::filesystem::current_path();
#else
    if (const char* testStorageRoot = std::getenv("AFTERNIGHT_TEST_STORAGE_ROOT");
        testStorageRoot != nullptr && testStorageRoot[0] != '\0')
    {
        return std::filesystem::path(testStorageRoot);
    }

    const char* homeDirectory = std::getenv("HOME");
    if (homeDirectory == nullptr)
    {
        if (const passwd* account = getpwuid(getuid()); account != nullptr)
        {
            homeDirectory = account->pw_dir;
        }
    }
    if (homeDirectory != nullptr)
    {
        return std::filesystem::path(homeDirectory) / ".local" / "share" / "AfterNight";
    }
    return std::filesystem::current_path();
#endif
}

/// Splits one `key=value` line, trimming both halves; false when there is no `=` or no key.
[[nodiscard]] bool parseLine(const std::string& line, std::string& key, std::string& value)
{
    const auto separator = line.find('=');
    if (separator == std::string::npos)
    {
        return false;
    }
    key = trim(line.substr(0, separator));
    value = trim(line.substr(separator + 1));
    return !key.empty();
}

/// Reads a count, clamped to [minimum, maximum]; `fallback` when the text is not a number.
[[nodiscard]] std::size_t parseBoundedCount(const std::string& value, const std::size_t minimum,
                                            const std::size_t maximum, const std::size_t fallback)
{
    try
    {
        return std::clamp<std::size_t>(std::stoull(value), minimum, maximum);
    }
    catch (...)
    {
        return fallback;
    }
}

} // namespace

std::filesystem::path LoggingConfig::defaultConfigPath()
{
    return applicationDataDirectory() / "logging.conf";
}

std::filesystem::path LoggingConfig::defaultLogDirectory()
{
    return applicationDataDirectory() / "logs";
}

LogLevel logLevelFromString(const std::string& text)
{
    const std::string lower = toLower(text);
    if (lower == "trace")
    {
        return LogLevel::Trace;
    }
    if (lower == "debug")
    {
        return LogLevel::Debug;
    }
    if (lower == "info")
    {
        return LogLevel::Info;
    }
    if (lower == "warning" || lower == "warn")
    {
        return LogLevel::Warn;
    }
    if (lower == "error" || lower == "err")
    {
        return LogLevel::Error;
    }
    if (lower == "critical" || lower == "exception")
    {
        return LogLevel::Exception;
    }
    if (lower == "off")
    {
        return LogLevel::Off;
    }
    return LogLevel::Info;
}

std::string logLevelToString(const LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warn:
        return "warning";
    case LogLevel::Error:
        return "error";
    case LogLevel::Exception:
        return "critical";
    case LogLevel::Off:
        return "off";
    }
    return "info";
}

bool loadLoggingConfig(const std::filesystem::path& configPath, LoggingConfig& config)
{
    config.logDirectory = LoggingConfig::defaultLogDirectory().string();
    config.logLevel = "info";
    config.maxFileSizeMb = kDefaultMaxFileSizeMb;
    config.maxFiles = kDefaultMaxFiles;

    std::ifstream file(configPath);
    if (!file.is_open())
    {
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }

        std::string key;
        std::string value;
        if (!parseLine(trimmed, key, value))
        {
            continue;
        }

        // A key this build does not know is ignored, so an older build reads a newer file.
        const std::string lowerKey = toLower(key);
        if (lowerKey == "log_directory")
        {
            config.logDirectory = value;
        }
        else if (lowerKey == "log_level")
        {
            config.logLevel = toLower(value);
        }
        else if (lowerKey == "max_file_size_mb")
        {
            config.maxFileSizeMb = parseBoundedCount(value, 1, 1000, kDefaultMaxFileSizeMb);
        }
        else if (lowerKey == "max_files")
        {
            config.maxFiles = parseBoundedCount(value, 1, 100, kDefaultMaxFiles);
        }
    }

    return true;
}

bool loadLoggingConfig(LoggingConfig& config)
{
    return loadLoggingConfig(LoggingConfig::defaultConfigPath(), config);
}

bool saveLoggingConfig(const std::filesystem::path& configPath, const LoggingConfig& config)
{
    std::error_code error;
    std::filesystem::create_directories(configPath.parent_path(), error);
    if (error)
    {
        return false;
    }

    std::ofstream file(configPath);
    if (!file.is_open())
    {
        return false;
    }

    file << "# AfterNight Logging Configuration\n";
    file << "# This file is auto-generated. Manual edits are preserved.\n";
    file << "\n";
    file << "log_directory=" << config.logDirectory << "\n";
    file << "log_level=" << config.logLevel << "\n";
    file << "max_file_size_mb=" << config.maxFileSizeMb << "\n";
    file << "max_files=" << config.maxFiles << "\n";

    return file.good();
}

bool saveLoggingConfig(const LoggingConfig& config)
{
    return saveLoggingConfig(LoggingConfig::defaultConfigPath(), config);
}

std::filesystem::path initializeLoggingFromConfig(const LoggingConfig& config)
{
    if (isLoggingInitialized())
    {
        return {};
    }
    const LogLevel level = logLevelFromString(config.logLevel);
    const std::filesystem::path configured = std::filesystem::path(config.logDirectory) / kLogFileName;
    const std::filesystem::path fallback = LoggingConfig::defaultLogDirectory() / kLogFileName;
    // The first directory that holds the file wins; a refused one is named in the first record, since the logger
    // that could say so does not exist before one of them is opened.
    std::vector<std::filesystem::path> candidates{configured};
    if (fallback != configured)
    {
        candidates.push_back(fallback);
    }
    std::string refusal;
    for (const std::filesystem::path& candidate : candidates)
    {
        std::error_code error;
        std::filesystem::create_directories(candidate.parent_path(), error);
        try
        {
            initializeLogging(candidate.string(), config.maxFiles, config.maxFileSizeMb, kRingBufferLines, level);
        }
        catch (const std::exception& failure)
        {
            if (refusal.empty())
            {
                refusal = failure.what();
            }
            continue;
        }
        if (candidate != configured)
        {
            LOG_WARNING("The log folder '{}' cannot be written ({}); the log is written to '{}' instead.",
                        config.logDirectory, refusal, candidate.parent_path().string());
        }
        return candidate;
    }
    initializeLogging(std::string(), config.maxFiles, config.maxFileSizeMb, kRingBufferLines, level);
    LOG_WARNING("Neither the log folder '{}' nor '{}' can be written ({}); no log file is written in this session.",
                config.logDirectory, fallback.parent_path().string(), refusal);
    return {};
}

bool applyLoggingConfig(const LoggingConfig& config)
{
    std::error_code error;
    std::filesystem::create_directories(config.logDirectory, error);

    const std::filesystem::path logFile = std::filesystem::path(config.logDirectory) / kLogFileName;
    return reconfigureFileSink(logFile.string(), config.maxFiles, config.maxFileSizeMb,
                               logLevelFromString(config.logLevel));
}

} // namespace an::platform
