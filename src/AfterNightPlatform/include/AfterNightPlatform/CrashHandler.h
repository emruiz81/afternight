// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process crash handler: Crashpad, started once per process, writing minidumps to a local database.
///
/// Crashpad runs where it is supported - Windows built with Visual Studio, and Linux - and elsewhere every
/// function below reports that it is not running. Dumps stay local: uploading is off unless a configuration
/// turns it on. The handler is process state owned by whatever starts the process - the application, a test
/// executable - and never by an engine.
#pragma once

#include <AfterNightPlatform/PlatformExport.h>

#include <filesystem>
#include <string>

namespace an::platform
{

/// How to start the crash handler.
struct CrashHandlerConfig
{
    std::filesystem::path handlerPath; ///< The `crashpad_handler` executable to start.
    std::filesystem::path databasePath; ///< Where minidumps are written; created when missing.
    std::filesystem::path metricsPath; ///< Where the handler keeps its metrics; empty for none.
    std::string productName = "AfterNight"; ///< The `product` annotation of every dump.
    std::string productVersion; ///< The `version` annotation of every dump, as the caller states it.
    bool uploadEnabled = false; ///< Whether the handler uploads dumps to `uploadUrl`.
    std::string uploadUrl; ///< The crash server, read only when `uploadEnabled` is true.
};

/// Starts the crash handler process and installs it for this process.
///
/// Call it as early as possible in `main`, before other threads exist. Every dump carries the `product` and
/// `version` annotations from `config`, the build type and the compiler. A second call while the handler runs
/// changes nothing and returns true. Failures are logged when the logger exists.
/// @param config How to start it; the handler must exist and the database path must not be empty.
/// @return True when the handler is running, false when it could not be started or Crashpad is unsupported.
[[nodiscard]] AFTERNIGHTPLATFORM_API bool initializeCrashHandler(const CrashHandlerConfig& config);

/// Whether the crash handler is running for this process.
/// @return True between a successful initializeCrashHandler and shutdownCrashHandler.
[[nodiscard]] AFTERNIGHTPLATFORM_API bool isCrashHandlerInitialized();

/// Adds or updates a key-value annotation that every later dump carries; does nothing when the handler is
/// not running. Safe from any thread.
///
/// At most 64 keys are kept - a further key is refused with a warning - and a value is truncated to 256
/// bytes.
/// @param key The annotation's name.
/// @param value Its value.
AFTERNIGHTPLATFORM_API void setCrashAnnotation(const std::string& key, const std::string& value);

/// Where the running handler writes minidumps.
/// @return The database directory, or an empty path when the handler is not running.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::filesystem::path crashDatabasePath();

/// Writes a minidump of the current state without crashing, for diagnosis; logs a warning and does nothing
/// when the handler is not running.
AFTERNIGHTPLATFORM_API void dumpWithoutCrash();

/// Marks the handler stopped and forgets the annotations; does nothing when it is not running.
///
/// The handler process itself keeps watching this process until it exits.
AFTERNIGHTPLATFORM_API void shutdownCrashHandler();

/// The configuration AfterNight starts its handler with.
///
/// The handler is `crashpad_handler` beside the application. On Linux, when it is not there, the copy vcpkg
/// installs under the repository three directories above is used, which is where a development build finds
/// it before the deploy step. Dumps go to `AfterNight/CrashDumps` and metrics to `AfterNight/CrashMetrics`
/// under `%LOCALAPPDATA%` on Windows and under `$XDG_DATA_HOME` - or `$HOME/.local/share` - elsewhere, and
/// beside the application when no such directory can be found; under a test storage root - the directory
/// `AFTERNIGHT_TEST_STORAGE_ROOT` names, as the logging configuration reads it - they go to its `CrashDumps` and
/// `CrashMetrics`. Uploading is off.
/// @param applicationPath The running executable, made absolute before use.
/// @param productVersion The `version` annotation: the version of the product the caller ships. This library
/// knows no product version of its own, so a caller that leaves it empty gets an empty annotation.
/// @return The configuration.
[[nodiscard]] AFTERNIGHTPLATFORM_API CrashHandlerConfig
defaultCrashHandlerConfig(const std::filesystem::path& applicationPath, const std::string& productVersion = {});

} // namespace an::platform
