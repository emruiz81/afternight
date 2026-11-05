// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the platform library's suite: owns the storage root the cases write under.
///
/// The suite links the platform library and nothing else of AfterNight's, and constructs no Qt object. Every
/// file it writes goes below a root it creates and removes: `AFTERNIGHT_TEST_STORAGE_ROOT` names that root
/// for the logging configuration's default paths, and the process temporary directory is redirected beside
/// it. A root the runner already supplies is used instead and left for the runner to remove.

#include <AfterNightPlatform/CrashHandler.h>
#include <AfterNightPlatform/Logging.h>

#include "../fixtures/ScopedTestTempDir.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace
{

[[nodiscard]] std::string environmentValue(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    std::string result;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr)
    {
        result = value;
    }
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

void setEnvironmentValue(const char* name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

/// Creates a unique storage root below the system temporary directory, or returns an empty path.
[[nodiscard]] std::filesystem::path createStorageRoot()
{
    std::error_code error;
    const std::filesystem::path base = std::filesystem::temp_directory_path(error);
    if (error)
    {
        return {};
    }
    const auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto candidate =
            base / ("AfterNight-Test_Platform-" + std::to_string(seed) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(candidate, error))
        {
            return candidate;
        }
    }
    return {};
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path ownedRoot;
    if (environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT").empty())
    {
        ownedRoot = createStorageRoot();
        if (ownedRoot.empty())
        {
            std::fprintf(stderr, "Test_Platform: could not create a storage root\n");
            return 1;
        }
        setEnvironmentValue("AFTERNIGHT_TEST_STORAGE_ROOT", ownedRoot.string());
    }

    int result = 1;
    {
        an::test::scoped_test_temp_dir temporaryDirectory("Test_Platform");
        ::testing::InitGoogleTest(&argc, argv);
        result = RUN_ALL_TESTS();

        // A case that failed half way may have left either service running; neither may outlive the root.
        an::platform::shutdownCrashHandler();
        an::platform::shutdownLogging();
    }

    if (!ownedRoot.empty())
    {
        std::error_code error;
        std::filesystem::remove_all(ownedRoot, error);
        if (error || std::filesystem::exists(ownedRoot))
        {
            std::fprintf(stderr, "Test_Platform: could not remove its storage root %s\n", ownedRoot.string().c_str());
            return 1;
        }
    }
    return result;
}
