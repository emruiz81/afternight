// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one check that decides whether text may cross a path boundary.
//
// A path at the engine's boundary is UTF-8, without a NUL, and nothing else; the platform
// conversion belongs to whatever opens the file. Every boundary that takes a path applies the
// same check, so it is written once here rather than once per boundary.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace an
{

/// Whether text is well-formed UTF-8 with no NUL in it.
///
/// Overlong sequences, surrogate code points, values past U+10FFFF and truncated sequences are
/// refused, as is a NUL byte, which a platform path API would read as the end of the path.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param text the bytes to check.
/// @return true for well-formed UTF-8 without NUL; an empty view is well-formed.
[[nodiscard]] constexpr bool isValidUtf8WithoutNul(std::string_view text) noexcept
{
    for (std::size_t i = 0u; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0u)
            return false;
        if (first < 0x80u)
            continue;
        unsigned count = 0u;
        std::uint32_t value = 0u;
        std::uint32_t minimum = 0u;
        if (first >= 0xc2u && first <= 0xdfu)
        {
            count = 1u;
            value = first & 0x1fu;
            minimum = 0x80u;
        }
        else if (first >= 0xe0u && first <= 0xefu)
        {
            count = 2u;
            value = first & 0x0fu;
            minimum = 0x800u;
        }
        else if (first >= 0xf0u && first <= 0xf4u)
        {
            count = 3u;
            value = first & 0x07u;
            minimum = 0x10000u;
        }
        else
            return false;
        if (count > text.size() - i)
            return false;
        for (unsigned j = 0u; j < count; ++j)
        {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0u) != 0x80u)
                return false;
            value = (value << 6u) | (next & 0x3fu);
        }
        if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu))
            return false;
    }
    return true;
}

/// The longest path the engine's boundaries accept, in bytes.
inline constexpr std::size_t kMaxBoundaryPathBytes = 131072u;

/// Whether text is a path the engine's boundaries accept: nonempty, bounded, valid UTF-8, no NUL.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param path the bytes to check.
/// @return true when the path may cross the boundary.
[[nodiscard]] constexpr bool isBoundaryPath(std::string_view path) noexcept
{
    return !path.empty() && path.size() <= kMaxBoundaryPathBytes && isValidUtf8WithoutNul(path);
}

} // namespace an
