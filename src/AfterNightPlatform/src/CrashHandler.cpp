// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Starting Crashpad's handler for this process, its annotations and its default configuration.

#include <AfterNightPlatform/CrashHandler.h>

#include <AfterNightPlatform/Logging.h>

#include <atomic>
#include <cstdlib>
#include <string>
#include <system_error>

// Crashpad is supported on Windows built with Visual Studio, and on Linux.
#if defined(WIN32) || defined(__linux__)
#define AFTERNIGHT_PLATFORM_HAS_CRASHPAD 1
#endif

#if AFTERNIGHT_PLATFORM_HAS_CRASHPAD
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
#include <unistd.h>
#endif

// Crashpad's annotation header calls strncpy, which the Microsoft runtime declares deprecated, and every
// warning here is an error. The deprecation is Crashpad's to answer, so it is silenced for its headers
// alone rather than for this library.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include <crashpad/client/annotation.h>
#include <crashpad/client/crash_report_database.h>
#include <crashpad/client/crashpad_client.h>
#include <crashpad/client/settings.h>
#include <crashpad/util/misc/capture_context.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <map>
#include <memory>
#include <mutex>
#include <vector>
#endif

namespace
{

// The crash handler may start before the logger does, so every message is guarded by it.
template <typename... Args> void logErrorIfReady(fmt::format_string<Args...> format, Args&&... args)
{
    if (an::platform::isLoggingInitialized())
    {
        LOG_ERROR(format, std::forward<Args>(args)...);
    }
}

template <typename... Args> void logWarningIfReady(fmt::format_string<Args...> format, Args&&... args)
{
    if (an::platform::isLoggingInitialized())
    {
        LOG_WARNING(format, std::forward<Args>(args)...);
    }
}

template <typename... Args> void logInfoIfReady(fmt::format_string<Args...> format, Args&&... args)
{
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO(format, std::forward<Args>(args)...);
    }
}

template <typename... Args> void logDebugIfReady(fmt::format_string<Args...> format, Args&&... args)
{
    if (an::platform::isLoggingInitialized())
    {
        LOG_DEBUG(format, std::forward<Args>(args)...);
    }
}

} // namespace

#if AFTERNIGHT_PLATFORM_HAS_CRASHPAD

namespace
{

// Crashpad's one client for this process.
crashpad::CrashpadClient gCrashpadClient;

std::atomic<bool> gIsInitialized{false};

std::filesystem::path gDatabasePath;

// Orders every annotation update against the others.
std::mutex gAnnotationsMutex;

constexpr std::size_t kMaxAnnotations = 64;
constexpr ::crashpad::Annotation::ValueSizeType kAnnotationValueSize = 256;

// Crashpad keeps a pointer to each annotation's name, so the names live here, one allocation each.
std::vector<std::unique_ptr<std::string>> gAnnotationKeys;

std::vector<std::unique_ptr<::crashpad::StringAnnotation<kAnnotationValueSize>>> gStringAnnotations;

// Finds an annotation by name, so an update reuses it instead of adding a second.
std::map<std::string, std::size_t> gAnnotationMap;

#ifdef WIN32
constexpr const wchar_t* kHandlerFileName = L"crashpad_handler.exe";
#else
constexpr const char* kHandlerFileName = "crashpad_handler";
#endif

[[nodiscard]] base::FilePath toBaseFilePath(const std::filesystem::path& path)
{
#ifdef WIN32
    return base::FilePath(path.wstring());
#else
    return base::FilePath(path.string());
#endif
}

#ifdef WIN32
/// The test storage root, when `AFTERNIGHT_TEST_STORAGE_ROOT` names one, read as the logging configuration reads it.
[[nodiscard]] std::filesystem::path testStorageRoot()
{
    wchar_t root[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"AFTERNIGHT_TEST_STORAGE_ROOT", root, MAX_PATH);
    return length > 0 && length < MAX_PATH ? std::filesystem::path(root) : std::filesystem::path();
}

[[nodiscard]] std::filesystem::path localAppDataPath()
{
    wchar_t* appDataPath = nullptr;
    std::filesystem::path result;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appDataPath)))
    {
        result = appDataPath;
        CoTaskMemFree(appDataPath);
    }

    return result;
}
#else
/// The test storage root, when `AFTERNIGHT_TEST_STORAGE_ROOT` names one, read as the logging configuration reads it.
[[nodiscard]] std::filesystem::path testStorageRoot()
{
    const char* root = std::getenv("AFTERNIGHT_TEST_STORAGE_ROOT");
    return root != nullptr && root[0] != '\0' ? std::filesystem::path(root) : std::filesystem::path();
}

[[nodiscard]] std::filesystem::path xdgDataPath()
{
    if (const char* xdgData = std::getenv("XDG_DATA_HOME"); xdgData != nullptr && *xdgData != '\0')
    {
        return std::filesystem::path(xdgData);
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
    {
        return std::filesystem::path(home) / ".local" / "share";
    }

    return {};
}

/// Whether the system will start the file: vcpkg installs Crashpad's handler without the execute permission, and a
/// handler the kernel refuses makes Crashpad's intermediate process abort with a core dump instead of reporting.
[[nodiscard]] bool isExecutable(const std::filesystem::path& path)
{
    return ::access(path.c_str(), X_OK) == 0;
}

/// The handler beside the application, or the one vcpkg installs in the repository three directories above
/// it, which is where a development build finds one before the deploy step copies it - when it can be executed.
[[nodiscard]] std::filesystem::path resolveHandlerPath(const std::filesystem::path& applicationDirectory)
{
    const auto candidate = applicationDirectory / kHandlerFileName;
    if (std::filesystem::exists(candidate))
    {
        return candidate;
    }

    auto repositoryRoot = applicationDirectory;
    for (int level = 0; level < 3 && repositoryRoot.has_parent_path(); ++level)
    {
        repositoryRoot = repositoryRoot.parent_path();
    }

    const auto vcpkgHandler =
        repositoryRoot / "vcpkg" / "installed" / "x64-linux" / "tools" / "crashpad" / kHandlerFileName;
    if (std::filesystem::exists(vcpkgHandler) && isExecutable(vcpkgHandler))
    {
        return vcpkgHandler;
    }

    return candidate;
}
#endif

} // namespace

namespace an::platform
{

bool initializeCrashHandler(const CrashHandlerConfig& config)
{
    if (gIsInitialized.load())
    {
        logWarningIfReady("Crashpad already initialized");
        return true;
    }

    if (config.handlerPath.empty())
    {
        logErrorIfReady("Crashpad handler path is empty");
        return false;
    }

    if (!std::filesystem::exists(config.handlerPath))
    {
        logErrorIfReady("Crashpad handler not found at: {}", config.handlerPath.string());
        return false;
    }
#ifndef WIN32
    if (!isExecutable(config.handlerPath))
    {
        logErrorIfReady("Crashpad handler at {} cannot be executed", config.handlerPath.string());
        return false;
    }
#endif

    if (config.databasePath.empty())
    {
        logErrorIfReady("Crashpad database path is empty");
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(config.databasePath, error);
    if (error)
    {
        logErrorIfReady("Failed to create crashpad database directory: {}", error.message());
        return false;
    }

    gDatabasePath = config.databasePath;

    const base::FilePath handlerPath = toBaseFilePath(config.handlerPath);
    const base::FilePath databasePath = toBaseFilePath(config.databasePath);
    base::FilePath metricsPath;

    if (!config.metricsPath.empty())
    {
        std::filesystem::create_directories(config.metricsPath, error);
        if (error)
        {
            logWarningIfReady("Failed to create crashpad metrics directory: {}", error.message());
        }
        metricsPath = toBaseFilePath(config.metricsPath);
    }

    // Key-value pairs embedded in every crash dump.
    std::map<std::string, std::string> annotations;
    annotations["product"] = config.productName;
    annotations["version"] = config.productVersion;

#ifdef _DEBUG
    annotations["build_type"] = "Debug";
#else
    annotations["build_type"] = "Release";
#endif

#ifdef _MSC_VER
    annotations["compiler"] = fmt::format("MSVC {}", _MSC_VER);
#elif defined(__clang__)
    annotations["compiler"] = fmt::format("Clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    annotations["compiler"] = fmt::format("GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif

    // Reports stay local, so they are never rate limited.
    std::vector<std::string> arguments;
    arguments.push_back("--no-rate-limit");

    const std::string url = config.uploadEnabled ? config.uploadUrl : "";

    logInfoIfReady("Initializing crashpad...");
    logDebugIfReady("  Handler: {}", config.handlerPath.string());
    logDebugIfReady("  Database: {}", config.databasePath.string());
    logDebugIfReady("  Upload enabled: {}", config.uploadEnabled);

#if defined(__linux__)
    // The main thread needs an alternate signal stack to report a stack overflow.
    (void)::crashpad::CrashpadClient::InitializeSignalStackForThread();
#endif

    const bool started =
        gCrashpadClient.StartHandler(handlerPath, databasePath, metricsPath, url, annotations, arguments,
                                     true, // restartable
                                     false, // asynchronous_start: wait until the handler is ready
                                     {} // attachments
        );

    if (!started)
    {
        logErrorIfReady("Failed to start crashpad handler");
        return false;
    }

    auto database = ::crashpad::CrashReportDatabase::Initialize(databasePath);
    if (database)
    {
        if (auto* settings = database->GetSettings(); settings != nullptr)
        {
            settings->SetUploadsEnabled(config.uploadEnabled);
        }
    }

    gIsInitialized.store(true);
    logInfoIfReady("Crashpad initialized successfully. Crash dumps will be stored in: {}",
                   config.databasePath.string());

    return true;
}

bool isCrashHandlerInitialized()
{
    return gIsInitialized.load();
}

void setCrashAnnotation(const std::string& key, const std::string& value)
{
    if (!gIsInitialized.load())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(gAnnotationsMutex);

    if (const auto existing = gAnnotationMap.find(key); existing != gAnnotationMap.end())
    {
        gStringAnnotations[existing->second]->Set(value.c_str());
        return;
    }

    if (gStringAnnotations.size() >= kMaxAnnotations)
    {
        logWarningIfReady("Maximum number of crash annotations reached ({})", kMaxAnnotations);
        return;
    }

    auto keyStorage = std::make_unique<std::string>(key);
    const char* keyName = keyStorage->c_str();
    gAnnotationKeys.push_back(std::move(keyStorage));

    auto annotation = std::make_unique<::crashpad::StringAnnotation<kAnnotationValueSize>>(keyName);
    annotation->Set(value.c_str());

    const std::size_t index = gStringAnnotations.size();
    gStringAnnotations.push_back(std::move(annotation));
    gAnnotationMap[key] = index;
}

std::filesystem::path crashDatabasePath()
{
    return gDatabasePath;
}

void dumpWithoutCrash()
{
    if (!gIsInitialized.load())
    {
        logWarningIfReady("Cannot create dump: crashpad not initialized");
        return;
    }

    logInfoIfReady("Creating manual crash dump...");

#ifdef WIN32
    // CRASHPAD_SIMULATE_CRASH expanded by hand, so its unqualified names cannot collide with ours.
    CONTEXT crashContext{};
    ::crashpad::CaptureContext(&crashContext);
    ::crashpad::CrashpadClient::DumpWithoutCrash(crashContext);
#else
    ::crashpad::NativeCPUContext crashContext{};
    ::crashpad::CaptureContext(&crashContext);
    ::crashpad::CrashpadClient::DumpWithoutCrash(&crashContext);
#endif

    logInfoIfReady("Manual crash dump created");
}

void shutdownCrashHandler()
{
    if (!gIsInitialized.load())
    {
        return;
    }

    logDebugIfReady("Shutting down crashpad...");

    {
        std::lock_guard<std::mutex> lock(gAnnotationsMutex);
        gAnnotationMap.clear();
        gStringAnnotations.clear();
        gAnnotationKeys.clear();
    }

    gIsInitialized.store(false);
    gDatabasePath.clear();

    logDebugIfReady("Crashpad shutdown complete");
}

CrashHandlerConfig defaultCrashHandlerConfig(const std::filesystem::path& applicationPath,
                                             const std::string& productVersion)
{
    CrashHandlerConfig config;

    std::error_code error;
    auto absolutePath = std::filesystem::absolute(applicationPath, error);
    if (!error && !absolutePath.empty())
    {
        absolutePath = absolutePath.lexically_normal();
    }
    else
    {
        absolutePath = applicationPath;
    }

    const std::filesystem::path applicationDirectory = absolutePath.parent_path();

#ifdef WIN32
    config.handlerPath = applicationDirectory / kHandlerFileName;

    const auto localAppData = localAppDataPath();
    if (!localAppData.empty())
    {
        config.databasePath = localAppData / "AfterNight" / "CrashDumps";
        config.metricsPath = localAppData / "AfterNight" / "CrashMetrics";
    }
    else
    {
        config.databasePath = applicationDirectory / "CrashDumps";
        config.metricsPath = applicationDirectory / "CrashMetrics";
    }
#else
    config.handlerPath = resolveHandlerPath(applicationDirectory);

    auto dataRoot = xdgDataPath();
    if (dataRoot.empty())
    {
        dataRoot = applicationDirectory;
    }

    config.databasePath = dataRoot / "AfterNight" / "CrashDumps";
    config.metricsPath = dataRoot / "AfterNight" / "CrashMetrics";
#endif

    // A process run under a test storage root keeps its crash database there, as it keeps its logs and settings: the
    // handler rewrites its settings at every start, crash or not.
    if (const std::filesystem::path root = testStorageRoot(); !root.empty())
    {
        config.databasePath = root / "CrashDumps";
        config.metricsPath = root / "CrashMetrics";
    }

    config.productName = "AfterNight";
    config.productVersion = productVersion;
    config.uploadEnabled = false;
    config.uploadUrl = "";

    return config;
}

} // namespace an::platform

#else // Crashpad is not supported on this platform.

namespace an::platform
{

bool initializeCrashHandler([[maybe_unused]] const CrashHandlerConfig& config)
{
    logWarningIfReady("Crashpad is not yet implemented for this platform");
    return false;
}

bool isCrashHandlerInitialized()
{
    return false;
}

void setCrashAnnotation([[maybe_unused]] const std::string& key, [[maybe_unused]] const std::string& value) {}

std::filesystem::path crashDatabasePath()
{
    return {};
}

void dumpWithoutCrash()
{
    logWarningIfReady("Crashpad dump_without_crash not available on this platform");
}

void shutdownCrashHandler() {}

CrashHandlerConfig defaultCrashHandlerConfig([[maybe_unused]] const std::filesystem::path& applicationPath,
                                             const std::string& productVersion)
{
    CrashHandlerConfig config;
    config.productName = "AfterNight";
    config.productVersion = productVersion;
    return config;
}

} // namespace an::platform

#endif // AFTERNIGHT_PLATFORM_HAS_CRASHPAD
