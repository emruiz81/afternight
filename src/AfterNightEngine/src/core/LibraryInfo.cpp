// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the identity the loaded library reports about itself.
//
// Every value is a constant of this translation unit, so the answer is the compiler and the
// configuration that built the library rather than anything about the process that loaded it.

#include <AfterNightEngine/Core/LibraryInfo.h>

namespace an
{
namespace
{

// The compiler identity is assembled from predefined macros at compile time rather than queried at
// run time, because the value has to describe the compiler that produced *this* translation unit.
// Anything resolved later would describe the host instead, which is the wrong thing to record next
// to a measurement.
#define AFTERNIGHTENGINE_STRINGIFY_VALUE(value) #value
#define AFTERNIGHTENGINE_STRINGIFY(value) AFTERNIGHTENGINE_STRINGIFY_VALUE(value)

#if defined(__clang__)
constexpr const char* kCompiler = "Clang " __clang_version__;
#elif defined(_MSC_VER)
constexpr const char* kCompiler = "MSVC " AFTERNIGHTENGINE_STRINGIFY(_MSC_FULL_VER);
#elif defined(__GNUC__)
constexpr const char* kCompiler = "GCC " AFTERNIGHTENGINE_STRINGIFY(__GNUC__) "." AFTERNIGHTENGINE_STRINGIFY(
    __GNUC_MINOR__) "." AFTERNIGHTENGINE_STRINGIFY(__GNUC_PATCHLEVEL__);
#else
constexpr const char* kCompiler = "unknown";
#endif

#undef AFTERNIGHTENGINE_STRINGIFY
#undef AFTERNIGHTENGINE_STRINGIFY_VALUE

// NDEBUG rather than _DEBUG is the test, because it is the standard macro that actually decides
// whether assertions are compiled in, and both toolchains in this repository define it in Release.
#if defined(NDEBUG)
constexpr bool kDebugBuild = false;
constexpr const char* kConfiguration = "Release";
#else
constexpr bool kDebugBuild = true;
constexpr const char* kConfiguration = "Debug";
#endif

} // namespace

LibraryInfo libraryInfo() noexcept
{
    LibraryInfo info{};
    info.name = "AfterNightEngine";
    info.configuration = kConfiguration;
    info.compiler = kCompiler;
    info.debugBuild = kDebugBuild;
    return info;
}

} // namespace an
