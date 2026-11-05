// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The text helpers the product still needs once the engine stops supplying them: prefix and suffix tests
/// that can ignore case, an upper-case copy, and UTF-8 conversions to and from the platform's wide strings.
///
/// Comparison and case mapping work byte by byte through the C library's classification of `unsigned char`,
/// so they fold ASCII letters and leave every byte of a multi-byte UTF-8 sequence as it is.
#pragma once

#include <AfterNightPlatform/PlatformExport.h>

#include <string>
#include <string_view>

namespace an::platform
{

/// Whether a comparison tells upper-case ASCII letters from lower-case ones.
enum class CaseSensitivity
{
    Sensitive,
    Insensitive,
};

/// Whether `text` begins with `prefix`.
/// @param text The text to test.
/// @param prefix The beginning to look for. An empty prefix matches nothing.
/// @param sensitivity Whether letter case must match.
/// @return True when `prefix` is not empty and `text` begins with it.
[[nodiscard]] AFTERNIGHTPLATFORM_API bool startsWith(std::string_view text, std::string_view prefix,
                                                     CaseSensitivity sensitivity = CaseSensitivity::Sensitive);

/// Whether `text` ends with `suffix`.
/// @param text The text to test.
/// @param suffix The ending to look for. An empty suffix matches nothing.
/// @param sensitivity Whether letter case must match.
/// @return True when `suffix` is not empty and `text` ends with it.
[[nodiscard]] AFTERNIGHTPLATFORM_API bool endsWith(std::string_view text, std::string_view suffix,
                                                   CaseSensitivity sensitivity = CaseSensitivity::Sensitive);

/// A copy of `text` with every lower-case letter the C library knows made upper case.
/// @param text The text to copy.
/// @return The upper-case copy, the same length as `text`.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::string toUpper(std::string_view text);

/// Converts a wide string - UTF-16 on Windows, UTF-32 elsewhere - to UTF-8.
///
/// On Windows the system converter replaces what it cannot represent. Elsewhere iconv converts, and a
/// conversion it refuses throws `std::runtime_error`.
/// @param text The wide text.
/// @return Its UTF-8 bytes; empty for empty text.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::string wideToUtf8(std::wstring_view text);

/// Converts UTF-8 to a wide string - UTF-16 on Windows, UTF-32 elsewhere.
///
/// On Windows the system converter replaces an invalid sequence. Elsewhere iconv converts, and a sequence it
/// refuses throws `std::runtime_error`.
/// @param text The UTF-8 bytes.
/// @return The wide text; empty for empty input.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::wstring utf8ToWide(std::string_view text);

} // namespace an::platform
