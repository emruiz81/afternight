// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whether a module mapped into this process is the engine, read from the loader rather than from a link line.
///
/// The runtime backend is loaded by name into a host it was not linked against, so nothing at build time says what
/// else that process holds; this asks the loader which modules are mapped, and which of them is the engine.
///
/// A module's file name is reduced to the project behind it - the `lib` prefix and the extension a shared library
/// carries, the version a Linux one can carry after it, and the `_d` a debug configuration adds - and compared
/// case-insensitively, because Windows file names are. The reduction is exposed so a suite can hold it to its cases.
#pragma once

#include <cctype>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>
#else
#include <link.h>
#endif

namespace an::sdk
{

/// Whether a module is the engine.
enum class EngineModule
{
    /// A module that is not the engine.
    None,

    /// The engine the runtime backend links.
    Engine,
};

/// Whether a module file name names the engine.
///
/// @param fileName one module's file name with no directory part.
/// @return what the name reduces to.
[[nodiscard]] inline EngineModule engineModuleOf(const std::string& fileName)
{
    std::string stem = fileName;
    const std::size_t firstDot = stem.find('.');
    if (firstDot != std::string::npos)
    {
        stem = stem.substr(0, firstDot);
    }
    if (stem.rfind("lib", 0) == 0)
    {
        stem = stem.substr(3);
    }
    if (stem.size() > 2u && stem.compare(stem.size() - 2u, 2u, "_d") == 0)
    {
        stem = stem.substr(0, stem.size() - 2u);
    }
    std::string folded;
    folded.reserve(stem.size());
    for (const char character : stem)
    {
        folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    if (folded == "afternightengine")
    {
        return EngineModule::Engine;
    }
    return EngineModule::None;
}

/// The file names of every module mapped into this process, in whatever order the platform reports them.
///
/// @return the names with no directory part; empty only when the platform refused to answer.
[[nodiscard]] inline std::vector<std::string> mappedModuleNames()
{
    std::vector<std::string> names;
#if defined(_WIN32)
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return names;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Module32FirstW(snapshot, &entry); more; more = Module32NextW(snapshot, &entry))
    {
        const std::wstring wide(entry.szModule);
        std::string narrow;
        narrow.reserve(wide.size());
        for (const wchar_t character : wide)
        {
            // The engine's file names are ASCII; a name with anything else is not one of them.
            narrow.push_back(character < 128 ? static_cast<char>(character) : '?');
        }
        names.push_back(narrow);
    }
    CloseHandle(snapshot);
#else
    dl_iterate_phdr(
        [](struct dl_phdr_info* info, std::size_t, void* context) {
            auto& collected = *static_cast<std::vector<std::string>*>(context);
            const std::string path(info->dlpi_name != nullptr ? info->dlpi_name : "");
            const std::size_t separator = path.find_last_of('/');
            collected.push_back(separator == std::string::npos ? path : path.substr(separator + 1u));
            return 0;
        },
        &names);
#endif
    return names;
}

} // namespace an::sdk
