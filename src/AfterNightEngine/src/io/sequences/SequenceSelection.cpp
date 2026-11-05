// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - choosing a sequence format from a path's extension, without opening anything.
//
// The table is compared without regard to ASCII case against the extension of the path's last
// component.

#include <AfterNightEngine/IO/SequenceSelection.h>

#include <array>
#include <cstddef>
#include <utility>

namespace an
{
namespace
{
bool sameIgnoringAsciiCase(std::string_view text, std::string_view lowercase) noexcept
{
    if (text.size() != lowercase.size())
        return false;
    for (std::size_t i = 0u; i < text.size(); ++i)
    {
        const char value = text[i];
        const char lower = value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
        if (lower != lowercase[i])
            return false;
    }
    return true;
}
} // namespace

std::optional<SequenceFormat> sequenceFormatForPath(std::string_view path) noexcept
{
    using namespace std::literals;
    static constexpr std::array kExtensions{
        std::pair{".ser"sv, SequenceFormat::Ser},    std::pair{".anseq"sv, SequenceFormat::ImageList},
        std::pair{".avi"sv, SequenceFormat::Avi},    std::pair{".mp4"sv, SequenceFormat::Mp4},
        std::pair{".mov"sv, SequenceFormat::Mp4},    std::pair{".mpg"sv, SequenceFormat::Video},
        std::pair{".mpeg"sv, SequenceFormat::Video}, std::pair{".mkv"sv, SequenceFormat::Video},
        std::pair{".webm"sv, SequenceFormat::Video}};
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return std::nullopt;
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return std::nullopt;
    const std::string_view extension = path.substr(dot);
    for (const auto& [candidate, format] : kExtensions)
        if (sameIgnoringAsciiCase(extension, candidate))
            return format;
    return std::nullopt;
}
} // namespace an
