// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Lexical path helpers the product uses on paths it holds as UTF-8 text.
///
/// Both split at the last separator of either platform - `/` or `\` - whatever the platform the process runs
/// on, and neither touches the file system. They are text operations, not `std::filesystem` path semantics:
/// a path with no separator has no directory part to remove.
#pragma once

#include <AfterNightPlatform/PlatformExport.h>

#include <string>
#include <string_view>

namespace an::platform
{

/// The text after the last separator: a file's name with its extension.
/// @param path A path in either platform's separators.
/// @return The last component; the whole path when it has no separator, and empty when it ends in one.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::string fileNamePart(std::string_view path);

/// The text before the last separator: the directory holding the file.
/// @param path A path in either platform's separators.
/// @return Everything before the last separator, without it; the whole path, unchanged, when it has no
/// separator.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::string directoryPart(std::string_view path);

} // namespace an::platform
