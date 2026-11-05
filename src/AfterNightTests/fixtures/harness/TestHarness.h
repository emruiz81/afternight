// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The engine-free test harness: the storage root a suite writes into, its temporary directory, its logger and its
// crash handler, started in the order the platform services need and taken down in reverse.
//
// It names no engine. A suite composes it with the fixture adapter it needs and says which product version its crash
// dumps carry; a Qt suite uses `QtTestHarness`, which adds the application.
// The order is the point: the storage root and the temporary directory exist before anything asks where to write,
// the logger writes into the root before the crash handler logs its start, and everything that holds a file under
// the root is stopped before the root is removed.

#include "../ScopedTestTempDir.h"

#include <AfterNightPlatform/CrashHandler.h>
#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/LoggingConfig.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace an::tests::harness
{

/// The environment variable the runners set to a suite's own storage root.
inline constexpr const char* kStorageRootVariable = "AFTERNIGHT_TEST_STORAGE_ROOT";

/// What a suite is called, and the product version its crash dumps carry.
struct SuiteIdentity
{
    std::string name;
    std::string productVersion;
};

/// An environment variable's value, or nothing when it is unset or empty.
[[nodiscard]] inline std::optional<std::string> environmentValue(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0u;
    std::optional<std::string> result;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr && value[0] != '\0')
    {
        result = std::string(value);
    }
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

/// Sets an environment variable for this process, or removes it for an empty value.
inline void setEnvironmentValue(const char* name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    if (value.empty())
    {
        unsetenv(name);
    }
    else
    {
        setenv(name, value.c_str(), 1);
    }
#endif
}

/// The running executable's path, or empty when the operating system does not say.
[[nodiscard]] inline std::filesystem::path executablePath()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0u || length >= MAX_PATH)
    {
        return {};
    }
    return std::filesystem::path(path);
#elif defined(__linux__)
    std::error_code error;
    std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path() : path;
#else
    return {};
#endif
}

/// The storage root a suite writes into, and the temporary directory it uses.
///
/// The runners give every suite its own `AFTERNIGHT_TEST_STORAGE_ROOT`. A suite run by hand gets one of its own
/// here, `AfterNight-<suite>-...` under the system's temporary directory, set in the environment for the process;
/// `release` removes it with everything in it and restores the variable. Either way the process-wide temporary
/// directory is redirected into a short directory this object owns - before anything, Qt included, has asked
/// where it is - and restored by `release`.
class TestStorage
{
public:
    explicit TestStorage(std::string_view suiteName)
    {
        if (const std::optional<std::string> supplied = environmentValue(kStorageRootVariable))
        {
            m_root = std::filesystem::path(*supplied);
        }
        else
        {
            createOwnedRoot(suiteName);
        }
        m_temporary.emplace(std::string(suiteName).c_str());
    }

    ~TestStorage()
    {
        (void)release();
    }

    TestStorage(const TestStorage&) = delete;
    TestStorage& operator=(const TestStorage&) = delete;

    /// Whether the root exists as a directory.
    [[nodiscard]] bool valid() const noexcept
    {
        std::error_code error;
        return !m_released && !m_root.empty() && std::filesystem::is_directory(m_root, error);
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept
    {
        return m_root;
    }

    /// Whether this object created the root, rather than the runner.
    [[nodiscard]] bool ownsRoot() const noexcept
    {
        return m_ownsRoot;
    }

    /// Restores the temporary directory, removes a root this object created and restores the variable. Anything
    /// holding a file under the root - the logger - must be stopped first. False when an owned root could not be
    /// removed; calling it again does nothing.
    [[nodiscard]] bool release() noexcept
    {
        if (m_released)
        {
            return m_removed;
        }
        m_released = true;
        m_temporary.reset();
        if (!m_ownsRoot)
        {
            return m_removed;
        }
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
        m_removed = !error && !std::filesystem::exists(m_root, error);
        try
        {
            setEnvironmentValue(kStorageRootVariable, m_previousRoot);
        }
        catch (...)
        {
            m_removed = false;
        }
        return m_removed;
    }

private:
    void createOwnedRoot(std::string_view suiteName)
    {
        m_previousRoot = environmentValue(kStorageRootVariable).value_or(std::string());
        std::error_code error;
        const std::filesystem::path base = std::filesystem::temp_directory_path(error);
        if (error)
        {
            return;
        }
        const auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const std::filesystem::path candidate = base / ("AfterNight-" + std::string(suiteName) + "-" +
                                                            std::to_string(seed) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate, error) && !error)
            {
                m_root = candidate;
                m_ownsRoot = true;
                setEnvironmentValue(kStorageRootVariable, m_root.string());
                return;
            }
        }
    }

    std::filesystem::path m_root;
    std::string m_previousRoot;
    bool m_ownsRoot = false;
    bool m_released = false;
    bool m_removed = true;
    std::optional<an::test::scoped_test_temp_dir> m_temporary;
};

/// Starts the platform logger with its file under `<root>/logs`, at level info; with an empty root, starts
/// nothing. Returns whether the logger is running afterwards.
inline bool startTestLogging(const std::filesystem::path& root)
{
    if (root.empty())
    {
        return false;
    }
    an::platform::LoggingConfig config;
    config.logDirectory = (root / "logs").string();
    config.logLevel = "info";
    an::platform::initializeLoggingFromConfig(config);
    return an::platform::isLoggingInitialized();
}

/// Starts the platform crash handler for a suite, annotated with the suite's name and operating system, its dumps
/// and metrics under the root; with an empty root, where the platform library's defaults put them. Returns
/// whether the handler started.
inline bool startTestCrashHandler(const std::filesystem::path& root, const SuiteIdentity& identity)
{
    try
    {
        const std::filesystem::path application = executablePath();
        if (application.empty())
        {
            std::fprintf(stderr, "Warning: the executable's path is unknown, so no crash handler was started\n");
            return false;
        }
        an::platform::CrashHandlerConfig config =
            an::platform::defaultCrashHandlerConfig(application, identity.productVersion);
        if (!root.empty())
        {
            config.databasePath = root / "crash_dumps";
            config.metricsPath = root / "crash_metrics";
        }
        if (!an::platform::initializeCrashHandler(config))
        {
            std::fprintf(stderr, "Warning: the crash handler did not start\n");
            return false;
        }
        an::platform::setCrashAnnotation("test_suite", identity.name);
#ifdef _WIN32
        an::platform::setCrashAnnotation("os", "Windows");
#else
        an::platform::setCrashAnnotation("os", "Linux");
#endif
        return true;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Warning: starting the crash handler threw: %s\n", error.what());
        return false;
    }
    catch (...)
    {
        std::fprintf(stderr, "Warning: starting the crash handler threw\n");
        return false;
    }
}

/// A suite's process services without Qt: the storage root, then the logger, then the crash handler. `finish`
/// stops them in reverse and removes an owned root.
class TestHarness
{
public:
    explicit TestHarness(SuiteIdentity identity) : m_identity(std::move(identity)), m_storage(m_identity.name)
    {
        if (!m_storage.valid())
        {
            std::fprintf(stderr, "%s: no storage root could be created\n", m_identity.name.c_str());
            return;
        }
        m_logging = startTestLogging(m_storage.root());
        m_crashHandler = startTestCrashHandler(m_storage.root(), m_identity);
    }

    ~TestHarness()
    {
        (void)finish(0);
    }

    TestHarness(const TestHarness&) = delete;
    TestHarness& operator=(const TestHarness&) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return m_storage.valid();
    }

    [[nodiscard]] const SuiteIdentity& identity() const noexcept
    {
        return m_identity;
    }

    [[nodiscard]] const TestStorage& storage() const noexcept
    {
        return m_storage;
    }

    [[nodiscard]] bool loggingStarted() const noexcept
    {
        return m_logging;
    }

    [[nodiscard]] bool crashHandlerStarted() const noexcept
    {
        return m_crashHandler;
    }

    /// Stops the crash handler and the logger and releases the storage. Returns `result`, or 1 when an owned root
    /// could not be removed; calling it again returns what the first call did.
    [[nodiscard]] int finish(int result) noexcept
    {
        if (m_finished)
        {
            return m_result;
        }
        m_finished = true;
        if (m_crashHandler)
        {
            an::platform::shutdownCrashHandler();
        }
        an::platform::shutdownLogging();
        m_result = m_storage.release() ? result : 1;
        return m_result;
    }

private:
    SuiteIdentity m_identity;
    TestStorage m_storage;
    bool m_logging = false;
    bool m_crashHandler = false;
    bool m_finished = false;
    int m_result = 0;
};

} // namespace an::tests::harness
