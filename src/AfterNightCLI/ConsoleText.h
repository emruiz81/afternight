// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Text in and out of the command-line tool: its arguments as UTF-8, and what it prints written so a console shows it.
///
/// The engine takes every path as UTF-8. On Windows the narrow arguments a C++ `main` receives are in the console's
/// code page, which cannot spell most file names, so the tool reads its wide arguments and converts them. What it
/// prints is UTF-8 too - a path in a message, above all - and a Windows console shows UTF-8 bytes as the wrong
/// characters, so text bound for a console is written to it as UTF-16; text bound for a file or a pipe is written as
/// the UTF-8 bytes it is, which is what a script reading the output expects.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace an::cli
{

/// The process's arguments after its name, as UTF-8.
///
/// @param argumentCount what the entry point received as its count.
/// @param arguments what it received as its arguments: UTF-16 on Windows, the system's encoding elsewhere, which on
///        this product's Linux hosts is UTF-8.
/// @return every argument but the first, converted.
#ifdef _WIN32
[[nodiscard]] std::vector<std::string> utf8Arguments(int argumentCount, wchar_t** arguments);
#else
[[nodiscard]] std::vector<std::string> utf8Arguments(int argumentCount, char** arguments);
#endif

/// Writes UTF-8 text to the standard output whole, as UTF-16 when it is a Windows console.
///
/// @param text what to write.
void writeStandardOutput(std::string_view text) noexcept;

/// Writes UTF-8 text to the standard error whole, as UTF-16 when it is a Windows console.
///
/// @param text what to write.
void writeStandardError(std::string_view text) noexcept;

} // namespace an::cli
