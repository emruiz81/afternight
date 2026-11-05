// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/CfaPattern.h.
//
// A colour filter array is the one descriptor field that a crop invalidates. The pattern names
// which filter sits over the pixel at the mosaic origin, so removing an odd number of columns moves
// every pixel under a different filter, and a descriptor that carried only the pattern would then
// be quietly wrong. The phase is what survives the crop, which is why it exists here rather than
// being assumed to be zero.
//
// The cases below are mostly about the pieces being coherent: a phase inside the block it indexes,
// no phase at all when there is no mosaic to be out of step with, and - for the mosaic whose filters
// are a per-camera table rather than a name - a table that describes a filter array at all.

#include <AfterNightEngine/Image/CfaPattern.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

namespace
{

constexpr an::CfaPattern kAllPatterns[] = {an::CfaPattern::None, an::CfaPattern::Rggb, an::CfaPattern::Bggr,
                                           an::CfaPattern::Grbg, an::CfaPattern::Gbrg, an::CfaPattern::XTrans};

/// One camera table, used wherever a case needs a mosaic that is tabulated rather than named.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

TEST(CfaPatternTest, EveryPatternIsRecognisedAndNamedDistinctly)
{
    for (const an::CfaPattern left : kAllPatterns)
    {
        EXPECT_TRUE(an::isCfaPattern(left));
        for (const an::CfaPattern right : kAllPatterns)
        {
            const bool sameName = std::strcmp(an::cfaPatternName(left), an::cfaPatternName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_STREQ(an::cfaPatternName(an::CfaPattern::Rggb), "Rggb");
    EXPECT_FALSE(an::isCfaPattern(static_cast<an::CfaPattern>(80)));
    EXPECT_STREQ(an::cfaPatternName(static_cast<an::CfaPattern>(80)), "UnknownCfaPattern");
}

TEST(CfaPatternTest, EveryNamedMosaicRepeatsOnATwoByTwoBlockAndTheTabulatedOneOnASixBySix)
{
    EXPECT_EQ(an::cfaPatternBlockSize(an::CfaPattern::None), 0u);
    for (const an::CfaPattern pattern :
         {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
    {
        EXPECT_EQ(an::cfaPatternBlockSize(pattern), 2u) << an::cfaPatternName(pattern);
        EXPECT_FALSE(an::cfaPatternIsTabulated(pattern));
    }
    EXPECT_EQ(an::cfaPatternBlockSize(an::CfaPattern::XTrans), an::kCfaTableSide);
    EXPECT_TRUE(an::cfaPatternIsTabulated(an::CfaPattern::XTrans));
    EXPECT_FALSE(an::cfaPatternIsTabulated(an::CfaPattern::None));
    EXPECT_EQ(an::cfaPatternBlockSize(static_cast<an::CfaPattern>(80)), 0u);
    EXPECT_FALSE(an::cfaPatternIsTabulated(static_cast<an::CfaPattern>(80)));
}

TEST(CfaPatternTest, ATabulatedPatternCannotBeBuiltFromItsNameAlone)
{
    // The name fixes the filters of the four two-by-two arrangements and fixes nothing about a
    // per-camera table, so naming one without its entries is refused by its own reason rather than
    // accepted with an unstated table.
    EXPECT_EQ(an::CfaDescriptor::check(an::CfaPattern::XTrans, 0u, 0u), an::DescriptorError::CfaTableRequired);
    EXPECT_FALSE(an::CfaDescriptor::create(an::CfaPattern::XTrans, 0u, 0u).has_value());
}

TEST(CfaPatternTest, TheDefaultDescriptorCarriesTheTableThatDescribesNothing)
{
    constexpr an::CfaDescriptor cfa;
    EXPECT_FALSE(cfa.hasFilterTable());
    EXPECT_FALSE(cfa.filterTable().describesMosaic());
    EXPECT_EQ(an::CfaDescriptor::check(cfa), an::DescriptorError::None);
}

TEST(CfaPatternTest, EachNamedArrangementPutsItsFiltersWhereItsNameSaysTheyAre)
{
    const auto colorGrid = [](an::CfaPattern pattern) {
        const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, 0u, 0u);
        return std::array<an::CfaColor, 4>{cfa->colorAt(0u, 0u), cfa->colorAt(1u, 0u), cfa->colorAt(0u, 1u),
                                           cfa->colorAt(1u, 1u)};
    };
    const an::CfaColor r = an::CfaColor::Red;
    const an::CfaColor g = an::CfaColor::Green;
    const an::CfaColor b = an::CfaColor::Blue;
    EXPECT_EQ(colorGrid(an::CfaPattern::Rggb), (std::array<an::CfaColor, 4>{r, g, g, b}));
    EXPECT_EQ(colorGrid(an::CfaPattern::Bggr), (std::array<an::CfaColor, 4>{b, g, g, r}));
    EXPECT_EQ(colorGrid(an::CfaPattern::Grbg), (std::array<an::CfaColor, 4>{g, r, b, g}));
    EXPECT_EQ(colorGrid(an::CfaPattern::Gbrg), (std::array<an::CfaColor, 4>{g, b, r, g}));
}

TEST(CfaPatternTest, ThePhaseShiftsWhichFilterCoversThePixelAtTheOrigin)
{
    // The whole reason the phase exists: a frame cropped by one column is the same mosaic seen from
    // one filter over, and its descriptor has to say so rather than describing its neighbour.
    const std::optional<an::CfaDescriptor> whole = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    const std::optional<an::CfaDescriptor> cropped = an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 0u);
    ASSERT_TRUE(whole.has_value() && cropped.has_value());
    for (std::uint32_t x = 0u; x < 8u; ++x)
    {
        for (std::uint32_t y = 0u; y < 8u; ++y)
        {
            EXPECT_EQ(cropped->colorAt(x, y), whole->colorAt(x + 1u, y)) << x << "," << y;
        }
    }
}

TEST(CfaPatternTest, AnImageWithNoMosaicReportsOneFilterEverywhere)
{
    constexpr an::CfaDescriptor cfa;
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        for (std::uint32_t y = 0u; y < 4u; ++y)
        {
            EXPECT_EQ(cfa.colorAt(x, y), an::CfaColor::Green);
        }
    }
}

TEST(CfaPatternTest, ATabulatedDescriptorReadsItsFiltersOutOfItsTable)
{
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(kCameraTable, 0u, 0u);
    ASSERT_TRUE(cfa.has_value());
    EXPECT_EQ(cfa->pattern(), an::CfaPattern::XTrans);
    EXPECT_TRUE(cfa->hasFilterTable());
    EXPECT_TRUE(cfa->isMosaiced());
    for (std::uint32_t y = 0u; y < 2u * an::kCfaTableSide; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u * an::kCfaTableSide; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y % an::kCfaTableSide) * an::kCfaTableSide +
                                      static_cast<std::size_t>(x % an::kCfaTableSide);
            EXPECT_EQ(cfa->colorAt(x, y), kCameraTable[index]) << x << "," << y;
        }
    }
}

TEST(CfaPatternTest, ATabulatedPhaseOutsideTheSixBySixBlockIsRefused)
{
    EXPECT_EQ(an::CfaDescriptor::check(kCameraTable, an::kCfaTableSide, 0u),
              an::DescriptorError::CfaOriginOutOfPattern);
    EXPECT_EQ(an::CfaDescriptor::check(kCameraTable, 0u, an::kCfaTableSide),
              an::DescriptorError::CfaOriginOutOfPattern);
    EXPECT_FALSE(an::CfaDescriptor::create(kCameraTable, an::kCfaTableSide, 0u).has_value());
    EXPECT_TRUE(an::CfaDescriptor::create(kCameraTable, an::kCfaTableSide - 1u, an::kCfaTableSide - 1u).has_value());
}

TEST(CfaPatternTest, ATabulatedDescriptorDiffersFromOneCarryingAnotherTable)
{
    an::CfaColorEntries mirrored = kCameraTable;
    for (std::size_t row = 0u; row < an::kCfaTableSide; ++row)
    {
        for (std::size_t column = 0u; column < an::kCfaTableSide / 2u; ++column)
        {
            std::swap(mirrored[row * an::kCfaTableSide + column],
                      mirrored[row * an::kCfaTableSide + (an::kCfaTableSide - 1u - column)]);
        }
    }
    const std::optional<an::CfaDescriptor> first = an::CfaDescriptor::create(kCameraTable, 0u, 0u);
    const std::optional<an::CfaDescriptor> same = an::CfaDescriptor::create(kCameraTable, 0u, 0u);
    const std::optional<an::CfaDescriptor> other = an::CfaDescriptor::create(mirrored, 0u, 0u);
    ASSERT_TRUE(first.has_value() && same.has_value());
    ASSERT_TRUE(other.has_value()) << "the mirrored table is still a mosaic";
    EXPECT_EQ(*first, *same);
    EXPECT_NE(*first, *other);
}

TEST(CfaPatternTest, TheDefaultIsNoMosaicAtAll)
{
    constexpr an::CfaDescriptor cfa;
    EXPECT_EQ(cfa.pattern(), an::CfaPattern::None);
    EXPECT_EQ(cfa.originX(), 0u);
    EXPECT_EQ(cfa.originY(), 0u);
    EXPECT_FALSE(cfa.isMosaiced());
}

TEST(CfaPatternTest, APhaseInsideTheBlockIsAccepted)
{
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u; ++x)
        {
            const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::Bggr, x, y);
            ASSERT_TRUE(cfa.has_value()) << x << "," << y;
            EXPECT_EQ(cfa->pattern(), an::CfaPattern::Bggr);
            EXPECT_EQ(cfa->originX(), x);
            EXPECT_EQ(cfa->originY(), y);
            EXPECT_TRUE(cfa->isMosaiced());
        }
    }
}

TEST(CfaPatternTest, APhaseOutsideTheBlockIsRefused)
{
    EXPECT_EQ(an::CfaDescriptor::check(an::CfaPattern::Rggb, 2u, 0u), an::DescriptorError::CfaOriginOutOfPattern);
    EXPECT_EQ(an::CfaDescriptor::check(an::CfaPattern::Rggb, 0u, 2u), an::DescriptorError::CfaOriginOutOfPattern);
    EXPECT_FALSE(an::CfaDescriptor::create(an::CfaPattern::Rggb, 2u, 0u).has_value());
    EXPECT_FALSE(an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 2u).has_value());
}

TEST(CfaPatternTest, APhaseWithNoMosaicToBeOutOfStepWithIsRefused)
{
    EXPECT_EQ(an::CfaDescriptor::check(an::CfaPattern::None, 0u, 0u), an::DescriptorError::None);
    EXPECT_EQ(an::CfaDescriptor::check(an::CfaPattern::None, 1u, 0u), an::DescriptorError::CfaOriginNotZero);
    EXPECT_EQ(an::CfaDescriptor::check(an::CfaPattern::None, 0u, 1u), an::DescriptorError::CfaOriginNotZero);
    EXPECT_FALSE(an::CfaDescriptor::create(an::CfaPattern::None, 1u, 1u).has_value());
}

TEST(CfaPatternTest, APatternOutsideTheEnumerationIsRefusedBeforeItsPhaseIsJudged)
{
    // Facts before policy: an unknown pattern has no block size, so testing the phase against it
    // would report the wrong reason for the right refusal.
    EXPECT_EQ(an::CfaDescriptor::check(static_cast<an::CfaPattern>(80), 0u, 0u),
              an::DescriptorError::UnknownCfaPattern);
    EXPECT_EQ(an::CfaDescriptor::check(static_cast<an::CfaPattern>(80), 5u, 5u),
              an::DescriptorError::UnknownCfaPattern);
}

TEST(CfaPatternTest, TwoDescriptorsAreEqualWhenPatternAndPhaseAgree)
{
    const std::optional<an::CfaDescriptor> first = an::CfaDescriptor::create(an::CfaPattern::Grbg, 1u, 0u);
    const std::optional<an::CfaDescriptor> same = an::CfaDescriptor::create(an::CfaPattern::Grbg, 1u, 0u);
    const std::optional<an::CfaDescriptor> shifted = an::CfaDescriptor::create(an::CfaPattern::Grbg, 0u, 1u);
    const std::optional<an::CfaDescriptor> other = an::CfaDescriptor::create(an::CfaPattern::Gbrg, 1u, 0u);
    ASSERT_TRUE(first.has_value() && same.has_value() && shifted.has_value() && other.has_value());
    EXPECT_EQ(*first, *same);
    EXPECT_NE(*first, *shifted);
    EXPECT_NE(*first, *other);
}

TEST(CfaPatternTest, ADescriptorIsUsableInAConstantExpression)
{
    static_assert(an::cfaPatternBlockSize(an::CfaPattern::Gbrg) == 2u);
    static_assert(an::CfaDescriptor::check(an::CfaPattern::Bggr, 1u, 1u) == an::DescriptorError::None);
    static_assert(an::CfaDescriptor::create(an::CfaPattern::Bggr, 1u, 1u).value().isMosaiced());
    static_assert(an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u).value().colorAt(1u, 1u) ==
                  an::CfaColor::Blue);
    static_assert(an::CfaDescriptor::check(kCameraTable, 1u, 2u) == an::DescriptorError::None);
    static_assert(an::CfaDescriptor::create(kCameraTable, 0u, 0u).value().hasFilterTable());
    static_assert(an::CfaDescriptor::create(kCameraTable, 0u, 0u).value().colorAt(2u, 0u) == an::CfaColor::Red);
    SUCCEED();
}

} // namespace
