// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the six-by-six filter table in AfterNightEngine/Image/CfaPattern.h.
//
// A named mosaic is validated by being one of four names. A tabulated one has thirty-six entries
// that arrive from a decoder, and the only thing standing between a transcription error and an
// image demosaiced with the wrong filters is this validation - so these cases exercise each rule
// separately, on a table that breaks exactly that rule and nothing else.
//
// Every mutation below starts from one real camera table and changes as little as possible, because
// a table that breaks three rules at once would pass whichever check happened to run first and
// prove nothing about the other two.

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

/// One real camera table: two rows of four greens around a red and a blue, a mixed row, and the
/// same again mirrored.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// The index one row and column of the block sits at.
///
/// @param row the row inside the block.
/// @param column the column inside the block.
/// @return the row-major index.
[[nodiscard]] constexpr std::size_t at(std::size_t row, std::size_t column)
{
    return row * an::kCfaTableSide + column;
}

TEST(CfaFilterTableTest, EveryFilterColourIsRecognisedAndNamedDistinctly)
{
    constexpr an::CfaColor kAll[] = {an::CfaColor::Red, an::CfaColor::Green, an::CfaColor::Blue};
    for (const an::CfaColor left : kAll)
    {
        EXPECT_TRUE(an::isCfaColor(left));
        for (const an::CfaColor right : kAll)
        {
            const bool sameName = std::strcmp(an::cfaColorName(left), an::cfaColorName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_STREQ(an::cfaColorName(an::CfaColor::Green), "Green");
    EXPECT_FALSE(an::isCfaColor(static_cast<an::CfaColor>(9)));
    EXPECT_STREQ(an::cfaColorName(static_cast<an::CfaColor>(9)), "UnknownCfaColor");
}

TEST(CfaFilterTableTest, ARealCameraTableIsAccepted)
{
    EXPECT_EQ(an::CfaFilterTable::check(kCameraTable), an::DescriptorError::None);
    const std::optional<an::CfaFilterTable> table = an::CfaFilterTable::create(kCameraTable);
    ASSERT_TRUE(table.has_value());
    EXPECT_TRUE(table->describesMosaic());
    EXPECT_EQ(table->entries(), kCameraTable);
}

TEST(CfaFilterTableTest, TheDefaultTableDescribesNothingAndSaysSo)
{
    // The default has to be a value that no camera reports, so that a descriptor with no table
    // cannot be confused with one carrying a described mosaic.
    constexpr an::CfaFilterTable table;
    EXPECT_FALSE(table.describesMosaic());
    EXPECT_EQ(an::CfaFilterTable::check(table.entries()), an::DescriptorError::CfaTableColorCountsWrong);
}

TEST(CfaFilterTableTest, AnEntryOutsideTheEnumerationIsRefusedBeforeTheCountsAreJudged)
{
    an::CfaColorEntries entries = kCameraTable;
    entries[at(2u, 2u)] = static_cast<an::CfaColor>(7);
    EXPECT_EQ(an::CfaFilterTable::check(entries), an::DescriptorError::UnknownCfaColor);
    EXPECT_FALSE(an::CfaFilterTable::create(entries).has_value());
}

TEST(CfaFilterTableTest, ABlockWithTheWrongColourTotalsIsRefused)
{
    an::CfaColorEntries entries = kCameraTable;
    entries[at(0u, 2u)] = an::CfaColor::Green;
    EXPECT_EQ(an::CfaFilterTable::check(entries), an::DescriptorError::CfaTableColorCountsWrong);
}

TEST(CfaFilterTableTest, ARowThatDoesNotSampleBothColoursEquallyIsRefused)
{
    // Swapping a red with a blue in a different row keeps the block totals at eight, twenty and
    // eight and breaks the per-line rule, which is exactly the case the totals cannot see.
    an::CfaColorEntries entries = kCameraTable;
    std::swap(entries[at(0u, 2u)], entries[at(1u, 2u)]);
    EXPECT_EQ(an::CfaFilterTable::check(entries), an::DescriptorError::CfaTableLineCountsWrong);
}

TEST(CfaFilterTableTest, AColumnThatDoesNotSampleBothColoursEquallyIsRefused)
{
    an::CfaColorEntries entries = kCameraTable;
    std::swap(entries[at(2u, 0u)], entries[at(2u, 1u)]);
    EXPECT_EQ(an::CfaFilterTable::check(entries), an::DescriptorError::CfaTableLineCountsWrong);
}

TEST(CfaFilterTableTest, AGreenWhoseNeighbourhoodNoGreenOfTheMosaicHasIsRefused)
{
    // No single exchange can reach this rule: swapping a red with a blue preserves the row counts
    // only when both sit in one row and the column counts only when both sit in one column. Two
    // exchanges in two rows, over the same pair of columns, preserve the block totals and every row
    // and column count - so the green-neighbourhood rule is the only one left to refuse it, which is
    // the whole reason that rule is checked rather than assumed to follow from the counts.
    an::CfaColorEntries entries = kCameraTable;
    std::swap(entries[at(2u, 1u)], entries[at(2u, 4u)]);
    std::swap(entries[at(5u, 1u)], entries[at(5u, 4u)]);
    EXPECT_EQ(an::CfaFilterTable::check(entries), an::DescriptorError::CfaTableGreenNeighbourhoodWrong);
}

TEST(CfaFilterTableTest, APeriodicEdgeGreenIsCheckedAgainstTheOppositeEdge)
{
    constexpr an::CfaColorEntries brokenAtThePeriodicEdge = {
        an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
        an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
        an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
        an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
        an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
        an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
        an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,
        an::CfaColor::Green};

    EXPECT_EQ(an::CfaFilterTable::check(brokenAtThePeriodicEdge), an::DescriptorError::CfaTableGreenNeighbourhoodWrong);
}

TEST(CfaFilterTableTest, TheBlockRepeatsSoAnEntryIsReadModuloItsSide)
{
    const std::optional<an::CfaFilterTable> table = an::CfaFilterTable::create(kCameraTable);
    ASSERT_TRUE(table.has_value());
    for (std::uint32_t row = 0u; row < an::kCfaTableSide; ++row)
    {
        for (std::uint32_t column = 0u; column < an::kCfaTableSide; ++column)
        {
            EXPECT_EQ(table->at(row, column), kCameraTable[at(row, column)]);
            EXPECT_EQ(table->at(row + an::kCfaTableSide, column + 3u * an::kCfaTableSide), table->at(row, column));
        }
    }
}

TEST(CfaFilterTableTest, TwoTablesAreEqualWhenEveryEntryAgrees)
{
    const std::optional<an::CfaFilterTable> first = an::CfaFilterTable::create(kCameraTable);
    const std::optional<an::CfaFilterTable> same = an::CfaFilterTable::create(kCameraTable);
    ASSERT_TRUE(first.has_value() && same.has_value());
    EXPECT_EQ(*first, *same);
    EXPECT_NE(*first, an::CfaFilterTable{});
}

TEST(CfaFilterTableTest, ATableIsUsableInAConstantExpression)
{
    static_assert(an::kCfaTableEntries == 36u);
    static_assert(an::CfaFilterTable::check(kCameraTable) == an::DescriptorError::None);
    static_assert(an::CfaFilterTable::create(kCameraTable).value().at(0u, 5u) == an::CfaColor::Blue);
    static_assert(!an::CfaFilterTable{}.describesMosaic());
    SUCCEED();
}

} // namespace
