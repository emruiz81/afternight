// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/LibraryInfo.h.

#include <AfterNightEngine/Core/LibraryInfo.h>

#include <gtest/gtest.h>

#include <cstring>

namespace
{

TEST(LibraryInfoTest, ReportsTheEngineName)
{
    const an::LibraryInfo info = an::libraryInfo();
    ASSERT_NE(info.name, nullptr);
    EXPECT_STREQ(info.name, "AfterNightEngine");
}

TEST(LibraryInfoTest, ConfigurationAgreesWithTheDebugFlag)
{
    const an::LibraryInfo info = an::libraryInfo();
    ASSERT_NE(info.configuration, nullptr);
    EXPECT_STREQ(info.configuration, info.debugBuild ? "Debug" : "Release");
}

TEST(LibraryInfoTest, ConfigurationMatchesTheConfigurationOfItsCaller)
{
    // A Release executable loading a Debug engine, or the reverse, produces timings that measure
    // the compiler and correctness results that measure the assertion set. Catching the mismatch
    // needs a comparison against something outside the library, which is what this test is.
    const an::LibraryInfo info = an::libraryInfo();
#if defined(NDEBUG)
    EXPECT_FALSE(info.debugBuild);
    EXPECT_STREQ(info.configuration, "Release");
#else
    EXPECT_TRUE(info.debugBuild);
    EXPECT_STREQ(info.configuration, "Debug");
#endif
}

TEST(LibraryInfoTest, NamesTheCompilerThatProducedTheBinary)
{
    const an::LibraryInfo info = an::libraryInfo();
    ASSERT_NE(info.compiler, nullptr);
    EXPECT_GT(std::strlen(info.compiler), 0u);
    // "unknown" is the fallback for a compiler the library does not recognise. Reaching it on a
    // supported toolchain would mean a recorded run names no compiler at all.
    EXPECT_STRNE(info.compiler, "unknown");
}

TEST(LibraryInfoTest, StringsHaveStaticStorageDuration)
{
    // The header promises string literals compiled into the library, so a caller may hold them
    // indefinitely. Pointer equality across calls is the observable half of that promise; a
    // per-call allocation would both leak here and dangle for the caller.
    const an::LibraryInfo first = an::libraryInfo();
    const an::LibraryInfo second = an::libraryInfo();
    EXPECT_EQ(first.name, second.name);
    EXPECT_EQ(first.configuration, second.configuration);
    EXPECT_EQ(first.compiler, second.compiler);
    EXPECT_EQ(first.debugBuild, second.debugBuild);
}

} // namespace
