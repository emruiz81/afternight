// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What loading the platform library brings into a process: itself, and neither the engine nor Qt.
///
/// The build graph says what the library links; this asks the running process what the loader actually
/// mapped, which is the question the command-line tool and the runtime backend depend on - both load the
/// library without Qt, and the product loads it beside the engine.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fstream>
#endif

namespace
{

#ifdef _WIN32
[[nodiscard]] bool moduleLoaded(const wchar_t* name)
{
    return GetModuleHandleW(name) != nullptr;
}
#else
/// The file names of every shared object mapped into this process.
[[nodiscard]] std::vector<std::string> mappedLibraries()
{
    std::vector<std::string> names;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line))
    {
        const auto slash = line.find_last_of('/');
        if (slash != std::string::npos && line.find(".so", slash) != std::string::npos)
        {
            names.push_back(line.substr(slash + 1));
        }
    }
    return names;
}

[[nodiscard]] bool mapped(const std::vector<std::string>& names, const std::string& prefix)
{
    for (const auto& name : names)
    {
        if (name.rfind(prefix, 0) == 0)
        {
            return true;
        }
    }
    return false;
}
#endif

TEST(LibraryClosureTest, TheProcessHoldsThePlatformLibraryAndNeitherEngineNorQt)
{
#ifdef _WIN32
    EXPECT_TRUE(moduleLoaded(L"AfterNightPlatform.dll") || moduleLoaded(L"AfterNightPlatform_d.dll"));
    for (const wchar_t* name : {L"AfterNightEngine.dll", L"AfterNightEngine_d.dll", L"AfterNightEngine.dll",
                                L"AfterNightEngine_d.dll", L"Qt6Core.dll", L"Qt6Cored.dll"})
    {
        EXPECT_FALSE(moduleLoaded(name)) << "the platform suite's process loaded a module it must not reach";
    }
#else
    const auto names = mappedLibraries();
    EXPECT_TRUE(mapped(names, "libAfterNightPlatform"));
    EXPECT_FALSE(mapped(names, "libAfterNightEngine")) << "an engine is mapped into the platform suite";
    EXPECT_FALSE(mapped(names, "libQt6")) << "Qt is mapped into the platform suite";
#endif
}

} // namespace
