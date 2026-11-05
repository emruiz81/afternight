// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point of the runtime backend's suite.
//
// The suite runs under the engine-free harness's storage root and temporary directory: the runner's root when it gives
// one, otherwise one this process owns and removes. Started with AFTERNIGHT_RUNTIME_BACKEND_SCENARIO naming a
// scenario, the executable runs that scenario instead of the suite and answers through its exit code - which is how a
// case that starts and ends the backend's session, or the interpreter, gets a process of its own. The suite starts no
// logger and no crash handler, and no Qt.

#include "RuntimeBackendTestSupport.h"

#include "fixtures/harness/TestHarness.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>

namespace
{

TEST(TestStorageIsolation, UsesOwnedTemporaryDirectories)
{
    const std::string root = an::tests::runtime_backend::environmentText(an::tests::harness::kStorageRootVariable);
    ASSERT_FALSE(root.empty());
    EXPECT_TRUE(std::filesystem::path(root).is_absolute());
    EXPECT_TRUE(std::filesystem::is_directory(root));
    // Windows answers the temporary directory with a trailing separator, which leaves the path no file name of its own.
    std::filesystem::path temporary = std::filesystem::temp_directory_path();
    if (!temporary.has_filename())
    {
        temporary = temporary.parent_path();
    }
    EXPECT_EQ(temporary.filename().string().rfind("ANtmp-", 0), 0u)
        << "the temporary directory was not redirected: " << std::filesystem::temp_directory_path().string();
}

} // namespace

int main(int argc, char** argv)
{
    an::tests::harness::TestStorage storage("Test_ExtensionRuntimeBackend");
    if (!storage.valid())
    {
        std::fprintf(stderr, "Test_ExtensionRuntimeBackend: no storage root could be created\n");
        return 1;
    }
    const std::string scenario =
        an::tests::runtime_backend::environmentText(an::tests::runtime_backend::kScenarioVariable);
    if (!scenario.empty())
    {
        const int result = an::tests::runtime_backend::runScenarioInThisProcess(scenario);
        std::fflush(stdout);
        std::fflush(stderr);
        // A scenario ends its process here, without returning through the static destructors of the module it loaded
        // and the interpreter it may have left running: those are what its checks were about, not what ends it.
        std::_Exit(result);
    }
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    if (!storage.release())
    {
        std::fprintf(stderr, "Test_ExtensionRuntimeBackend: the storage root could not be removed\n");
    }
    // The shared runtime's interpreter and module stay in place until the process ends: CPython with NumPy loaded is
    // not restartable, and unloading the backend under a live interpreter would leave the module it registered behind.
    return result;
}
