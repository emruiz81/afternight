// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the codec format names, their extension tables, and matching a path's
// extension against them.
//
// The tables are static and the match folds ASCII case byte by byte, so looking a path up allocates
// nothing and does not depend on the process locale.

#include <AfterNightEngine/IO/CodecFormat.h>

#include <array>

namespace an
{
const char* codecFormatName(CodecFormat format) noexcept
{
    switch (format)
    {
    case CodecFormat::Unknown:
        return "unknown";
    case CodecFormat::Fits:
        return "FITS";
    case CodecFormat::Xisf:
        return "XISF";
    case CodecFormat::Raw:
        return "RAW";
    case CodecFormat::Png:
        return "PNG";
    case CodecFormat::Jpeg:
        return "JPEG";
    case CodecFormat::Tiff:
        return "TIFF";
    case CodecFormat::Dump:
        return "Dump";
    }
    return "UnknownCodecFormat";
}

std::span<const std::string_view> codecExtensions(CodecFormat format) noexcept
{
    using namespace std::literals;
    static constexpr std::array kFits{".fits"sv, ".fit"sv, ".fts"sv};
    static constexpr std::array kXisf{".xisf"sv};
    static constexpr std::array kRaw{".nef"sv, ".crw"sv, ".cr2"sv, ".cr3"sv, ".dng"sv, ".arw"sv,
                                     ".sr2"sv, ".srf"sv, ".dcr"sv, ".k25"sv, ".kdc"sv, ".erf"sv,
                                     ".mrw"sv, ".orf"sv, ".pef"sv, ".raf"sv, ".raw"sv, ".x3f"sv};
    static constexpr std::array kPng{".png"sv};
    static constexpr std::array kJpeg{".jpg"sv, ".jpeg"sv, ".jpe"sv};
    static constexpr std::array kTiff{".tiff"sv, ".tif"sv};
    switch (format)
    {
    case CodecFormat::Fits:
        return kFits;
    case CodecFormat::Xisf:
        return kXisf;
    case CodecFormat::Raw:
        return kRaw;
    case CodecFormat::Png:
        return kPng;
    case CodecFormat::Jpeg:
        return kJpeg;
    case CodecFormat::Tiff:
        return kTiff;
    case CodecFormat::Unknown:
    case CodecFormat::Dump:
        return {};
    }
    return {};
}

CodecFormat codecFormatFromPath(std::string_view path) noexcept
{
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return CodecFormat::Unknown;
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return CodecFormat::Unknown;
    const auto extension = path.substr(dot);
    for (std::uint32_t index = 1u; index <= kCodecFormatCount; ++index)
    {
        const auto format = static_cast<CodecFormat>(index);
        for (const auto candidate : codecExtensions(format))
        {
            if (candidate.size() != extension.size())
                continue;
            bool same = true;
            for (std::size_t i = 0u; i < candidate.size(); ++i)
            {
                const char value = extension[i];
                const char lower = value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
                if (lower != candidate[i])
                {
                    same = false;
                    break;
                }
            }
            if (same)
                return format;
        }
    }
    return CodecFormat::Unknown;
}
} // namespace an
