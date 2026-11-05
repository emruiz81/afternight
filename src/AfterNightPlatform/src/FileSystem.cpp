// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lexical path helpers.

#include <AfterNightPlatform/FileSystem.h>

namespace an::platform
{

namespace
{

/// Both platforms' separators, whichever platform this is.
constexpr std::string_view kSeparators = "/\\";

} // namespace

std::string fileNamePart(const std::string_view path)
{
    const auto separator = path.find_last_of(kSeparators);
    if (separator != std::string_view::npos)
    {
        return std::string(path.substr(separator + 1));
    }
    return std::string(path);
}

std::string directoryPart(const std::string_view path)
{
    const auto separator = path.find_last_of(kSeparators);
    if (separator != std::string_view::npos)
    {
        return std::string(path.substr(0, separator));
    }
    return std::string(path);
}

} // namespace an::platform
