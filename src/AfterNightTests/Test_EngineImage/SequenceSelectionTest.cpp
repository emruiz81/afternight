// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/SequenceSelection.h>

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace
{
using namespace std::literals;

TEST(SequenceSelectionTest, EachSequenceExtensionNamesItsContainer)
{
    const std::array cases{
        std::pair{"night.ser"sv, an::SequenceFormat::Ser},
        std::pair{"C:\\captures\\NIGHT.SER"sv, an::SequenceFormat::Ser},
        std::pair{"lists/lights.anseq"sv, an::SequenceFormat::ImageList},
        std::pair{"lists\\Lights.AnSeq"sv, an::SequenceFormat::ImageList},
        std::pair{"planet.avi"sv, an::SequenceFormat::Avi},
        std::pair{"planet.mp4"sv, an::SequenceFormat::Mp4},
        std::pair{"planet.MOV"sv, an::SequenceFormat::Mp4},
        std::pair{"planet.mpg"sv, an::SequenceFormat::Video},
        std::pair{"planet.mpeg"sv, an::SequenceFormat::Video},
        std::pair{"planet.mkv"sv, an::SequenceFormat::Video},
        std::pair{"dir.with.dots/planet.webm"sv, an::SequenceFormat::Video},
        std::pair{"\xe6\x98\x9f/\xe6\x98\x9f.ser"sv, an::SequenceFormat::Ser},
    };
    for (const auto& [path, format] : cases)
    {
        const std::optional<an::SequenceFormat> named = an::sequenceFormatForPath(path);
        ASSERT_TRUE(named.has_value()) << path;
        EXPECT_EQ(*named, format) << path;
    }
}

TEST(SequenceSelectionTest, AnyOtherNameNamesNoContainer)
{
    const std::array names{
        "light.fits"sv,      "light"sv,         ""sv,         "archive.ser.bak"sv, "captures.ser/night"sv,
        "a\\b.mp4\\c"sv,     "trailing.dot."sv, "night.se"sv, "night.series"sv,    "night.mp4 "sv,
        "night.\xc3\xa5vi"sv};
    for (const std::string_view path : names)
        EXPECT_FALSE(an::sequenceFormatForPath(path).has_value()) << path;
    constexpr std::string_view withNul("night\0.ser", 10u);
    EXPECT_FALSE(an::sequenceFormatForPath(withNul).has_value());
}

TEST(SequenceSelectionTest, OnlyTheVideoContainersAreReadThroughFfmpeg)
{
    static_assert(an::readsThroughFfmpeg(an::SequenceFormat::Avi));
    static_assert(an::readsThroughFfmpeg(an::SequenceFormat::Mp4));
    static_assert(an::readsThroughFfmpeg(an::SequenceFormat::Video));
    static_assert(!an::readsThroughFfmpeg(an::SequenceFormat::Ser));
    static_assert(!an::readsThroughFfmpeg(an::SequenceFormat::ImageList));
    static_assert(!an::readsThroughFfmpeg(an::SequenceFormat::Fits));
    static_assert(!an::readsThroughFfmpeg(an::SequenceFormat::Dummy));
    for (const std::string_view path : {"a.avi"sv, "a.mp4"sv, "a.mov"sv, "a.mpg"sv, "a.mpeg"sv, "a.mkv"sv, "a.webm"sv})
    {
        const std::optional<an::SequenceFormat> named = an::sequenceFormatForPath(path);
        ASSERT_TRUE(named.has_value()) << path;
        EXPECT_TRUE(an::readsThroughFfmpeg(*named)) << path;
    }
    for (const std::string_view path : {"a.ser"sv, "a.anseq"sv})
    {
        const std::optional<an::SequenceFormat> named = an::sequenceFormatForPath(path);
        ASSERT_TRUE(named.has_value()) << path;
        EXPECT_FALSE(an::readsThroughFfmpeg(*named)) << path;
    }
}

} // namespace
