// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The command-line tool's arguments as UTF-8, and its output written so a console shows it.

#include "ConsoleText.h"

#include <AfterNightPlatform/Text.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace an::cli
{
namespace
{

#ifdef _WIN32
/// Writes UTF-8 to a standard handle: as UTF-16 to a console, which shows UTF-8 bytes as the wrong characters, and as
/// the bytes themselves to anything else.
void writeHandle(DWORD which, std::string_view text) noexcept
{
    const HANDLE handle = GetStdHandle(which);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE || text.empty())
    {
        return;
    }
    DWORD mode = 0u;
    if (GetConsoleMode(handle, &mode) != 0)
    {
        try
        {
            const std::wstring wide = an::platform::utf8ToWide(text);
            std::size_t offset = 0u;
            while (offset < wide.size())
            {
                DWORD written = 0u;
                const DWORD chunk = static_cast<DWORD>((std::min)(wide.size() - offset, std::size_t{16384u}));
                if (WriteConsoleW(handle, wide.data() + offset, chunk, &written, nullptr) == 0 || written == 0u)
                {
                    return;
                }
                offset += written;
            }
        }
        catch (const std::exception&)
        {
            // A message that cannot be converted is not shown; the exit code still says what happened.
        }
        return;
    }
    std::size_t offset = 0u;
    while (offset < text.size())
    {
        DWORD written = 0u;
        const DWORD chunk = static_cast<DWORD>((std::min)(text.size() - offset, std::size_t{1u} << 20u));
        if (WriteFile(handle, text.data() + offset, chunk, &written, nullptr) == 0 || written == 0u)
        {
            return;
        }
        offset += written;
    }
}
#else
void writeStream(std::FILE* stream, std::string_view text) noexcept
{
    if (!text.empty())
    {
        const std::size_t written = std::fwrite(text.data(), 1u, text.size(), stream);
        static_cast<void>(written);
    }
    std::fflush(stream);
}
#endif

} // namespace

#ifdef _WIN32
std::vector<std::string> utf8Arguments(int argumentCount, wchar_t** arguments)
{
    std::vector<std::string> converted;
    for (int index = 1; index < argumentCount; ++index)
    {
        converted.push_back(an::platform::wideToUtf8(arguments[index]));
    }
    return converted;
}
#else
std::vector<std::string> utf8Arguments(int argumentCount, char** arguments)
{
    std::vector<std::string> converted;
    for (int index = 1; index < argumentCount; ++index)
    {
        converted.emplace_back(arguments[index]);
    }
    return converted;
}
#endif

void writeStandardOutput(std::string_view text) noexcept
{
#ifdef _WIN32
    writeHandle(STD_OUTPUT_HANDLE, text);
#else
    writeStream(stdout, text);
#endif
}

void writeStandardError(std::string_view text) noexcept
{
#ifdef _WIN32
    writeHandle(STD_ERROR_HANDLE, text);
#else
    writeStream(stderr, text);
#endif
}

} // namespace an::cli
