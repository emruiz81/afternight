// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// An independent, uncompressed little-endian DNG writer for the RAW policy tests.
//
// No codec under test writes this input: the tags are spelled out from the DNG specification, as the RAW codec's own
// tests spell them. A mosaic is one plane of codes under a two-by-two filter pattern; a full-colour image is three
// interleaved channels with the linear photometric interpretation, which the camera RAW library develops with its
// full-colour processing. Every code is `black + (pixel + channel * 521) % 3900`, so each channel differs from the
// others and stays below the white level of 4095.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace an::tests
{

/// What a synthetic DNG states.
struct SyntheticDng
{
    std::uint32_t width = 64u;
    std::uint32_t height = 48u;
    /// Three linear channels rather than a mosaic.
    bool fullColor = false;
    /// The two-by-two filter pattern, top-left first, as the DNG colour indices 0 red, 1 green, 2 blue.
    std::array<std::uint8_t, 4> pattern{0u, 1u, 1u, 2u};
    std::uint32_t black = 100u;
    std::uint32_t white = 4095u;
    /// The as-shot neutral as three rationals, numerator then denominator; the camera's multipliers are their
    /// reciprocals.
    std::array<std::uint32_t, 6> asShotNeutral{1u, 2u, 1u, 1u, 1u, 3u};
    /// The orientation tag: 1 as stored, 6 a clockwise quarter turn.
    std::uint16_t orientation = 1u;
};

/// The code a synthetic DNG stores at a pixel, in a channel.
[[nodiscard]] inline std::uint16_t syntheticDngCode(const SyntheticDng& dng, std::uint32_t x, std::uint32_t y,
                                                    std::uint32_t channel)
{
    const std::uint32_t pixel = y * dng.width + x;
    return static_cast<std::uint16_t>(dng.black + (pixel + channel * 521u) % 3900u);
}

/// Writes the DNG to a path; false when it cannot be written.
[[nodiscard]] inline bool writeSyntheticDng(const SyntheticDng& dng, const std::filesystem::path& path)
{
    struct Tag
    {
        std::uint16_t key;
        std::uint16_t type;
        std::uint32_t count;
        std::vector<std::uint8_t> bytes;
    };
    const auto words = [](std::initializer_list<std::uint32_t> values, unsigned size = 4u) {
        std::vector<std::uint8_t> result;
        for (const std::uint32_t value : values)
        {
            for (unsigned i = 0u; i < size; ++i)
            {
                result.push_back(static_cast<std::uint8_t>(value >> (8u * i)));
            }
        }
        return result;
    };
    const std::uint32_t channels = dng.fullColor ? 3u : 1u;
    const auto& neutral = dng.asShotNeutral;
    std::vector<Tag> tags{
        {254, 4, 1, words({0})},
        {256, 4, 1, words({dng.width})},
        {257, 4, 1, words({dng.height})},
        {258, 3, channels, dng.fullColor ? words({16, 16, 16}, 2) : words({16}, 2)},
        {259, 3, 1, words({1}, 2)},
        {262, 3, 1, words({dng.fullColor ? 34892u : 32803u}, 2)},
        {271, 2, 5, {'T', 'e', 's', 't', 0}},
        {272, 2, 7, {'C', 'a', 'm', 'e', 'r', 'a', 0}},
        {273, 4, 1, words({0})},
        {274, 3, 1, words({dng.orientation}, 2)},
        {277, 3, 1, words({channels}, 2)},
        {278, 4, 1, words({dng.height})},
        {279, 4, 1, words({dng.width * dng.height * channels * 2u})},
        {284, 3, 1, words({1}, 2)},
        {50706, 1, 4, {1, 4, 0, 0}},
        {50707, 1, 4, {1, 1, 0, 0}},
        {50708, 2, 12, {'T', 'e', 's', 't', ' ', 'C', 'a', 'm', 'e', 'r', 'a', 0}},
        {50714, 5, 1, words({dng.black, 1})},
        {50717, 4, 1, words({dng.white})},
        {50721, 10, 9, words({1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1})},
        {50728, 5, 3, words({neutral[0], neutral[1], neutral[2], neutral[3], neutral[4], neutral[5]})},
        {50778, 3, 1, words({21}, 2)}};
    if (!dng.fullColor)
    {
        tags.push_back({33421, 3, 2, words({2, 2}, 2)});
        tags.push_back({33422, 1, 4, {dng.pattern.begin(), dng.pattern.end()}});
        tags.push_back({50710, 1, 3, {0, 1, 2}});
        tags.push_back({50711, 3, 1, words({1}, 2)});
    }
    std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.key < b.key; });
    std::vector<std::uint8_t> bytes{'I', 'I', 42, 0, 8, 0, 0, 0};
    const auto append = [&bytes](const auto& part) { bytes.insert(bytes.end(), part.begin(), part.end()); };
    append(words({static_cast<std::uint32_t>(tags.size())}, 2));
    const auto extraStart = 8u + 2u + static_cast<std::uint32_t>(tags.size()) * 12u + 4u;
    std::vector<std::uint8_t> extra;
    std::size_t stripOffset = 0u;
    for (const Tag& tag : tags)
    {
        append(words({tag.key, tag.type}, 2));
        append(words({tag.count}));
        if (tag.key == 273)
        {
            stripOffset = bytes.size();
        }
        if (tag.bytes.size() <= 4u)
        {
            append(tag.bytes);
            bytes.resize(bytes.size() + 4u - tag.bytes.size(), 0);
        }
        else
        {
            append(words({extraStart + static_cast<std::uint32_t>(extra.size())}));
            extra.insert(extra.end(), tag.bytes.begin(), tag.bytes.end());
            if (extra.size() % 2u != 0u)
            {
                extra.push_back(0);
            }
        }
    }
    append(words({0}));
    append(extra);
    const auto offset = words({static_cast<std::uint32_t>(bytes.size())});
    std::copy(offset.begin(), offset.end(), bytes.begin() + static_cast<std::ptrdiff_t>(stripOffset));
    for (std::uint32_t y = 0u; y < dng.height; ++y)
    {
        for (std::uint32_t x = 0u; x < dng.width; ++x)
        {
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                append(words({syntheticDngCode(dng, x, y, channel)}, 2));
            }
        }
    }
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return static_cast<bool>(output);
}

/// A path as the codec facade takes it: UTF-8.
[[nodiscard]] inline std::string utf8Path(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace an::tests
