// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Identity of the loaded engine binary.
///
/// What a measurement tool or a run record writes beside a number so that the number can be traced
/// to the build that produced it: the library's name, the configuration and the compiler it was
/// built with, and whether assertions were compiled in. Every value is fixed when the library is
/// compiled, so it describes the binary that actually ran rather than the program that loaded it.
/// `Core/Version.h` answers the neighbouring question - whether the caller's headers match that
/// binary - and `Core/DependencyReport.h` the third-party libraries inside it.

#pragma once

#include <AfterNightEngine/Core/Api.h>

/// The AfterNight engine namespace, which holds every declaration the engine publishes.
namespace an
{

/// Identity of the AfterNightEngine binary the caller has loaded.
///
/// Every pointer member refers to a string literal with static storage duration compiled into the
/// library, so the values outlive any caller and are never freed or mutated. They describe the
/// binary, not the calling program: a tool that links this library and reports these values
/// reports what actually ran, which is what makes a recorded measurement attributable to a build.
struct LibraryInfo
{
    /// Library name without platform decoration, so a report reads identically on every platform.
    const char* name;

    /// Build configuration this binary was compiled in: `"Release"` or `"Debug"`.
    const char* configuration;

    /// Compiler that produced this binary, including its version where the compiler exposes one.
    const char* compiler;

    /// True when this binary was compiled with debug assertions enabled.
    ///
    /// Timing taken from such a build measures the compiler rather than the engine, so a
    /// measurement tool is expected to refuse to publish a run whose engine reports true here.
    bool debugBuild;
};

/// The identity of this library.
///
/// Thread-safe, reentrant, and allocation-free; it cannot fail, because every value it returns is
/// a compile-time constant. Callable before any engine object exists.
///
/// @return the name, configuration, compiler, and debug flag of the binary that is running.
///         Every string member points into that binary and outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API LibraryInfo libraryInfo() noexcept;

} // namespace an
