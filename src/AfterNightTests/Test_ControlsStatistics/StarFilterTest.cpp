// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star filter both star-profiler windows share, over one detected star list: the figures it reads, their limits,
/// the ranges, the random share drawn from a seed - the same stars on every run and every platform - and the neighbours
/// of the stars kept, reached transitively through overlapping boxes.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "controls/StarFilter.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace
{

using an::tests::statistics::starAt;
using an::ui::StarFilterField;

/// A row of stars a hundred pixels apart, star i of amplitude i / 10, with a magnitude for all but the last.
[[nodiscard]] an::ui::StarList spacedStars(const std::size_t count)
{
    an::ui::StarList stars;
    for (std::size_t index = 0u; index < count; ++index)
    {
        stars.push_back(
            starAt(50.0 + 100.0 * static_cast<double>(index), 50.0, 2.0 + 0.5 * static_cast<double>(index), 2.0,
                   0.1 * static_cast<double>(index + 1u),
                   index + 1u < count ? std::optional<double>(-5.0 - static_cast<double>(index)) : std::nullopt));
    }
    return stars;
}

} // namespace

TEST(StarFilterTest, EachFieldReadsItsFigure)
{
    const an::DetectedStar star = starAt(10.0, 20.0, 4.0, 3.0, 0.8, -7.5);
    EXPECT_DOUBLE_EQ(*an::ui::starFilterValue(star, StarFilterField::Amplitude), 0.8);
    EXPECT_DOUBLE_EQ(*an::ui::starFilterValue(star, StarFilterField::Circularity), 0.8 / 1.1);
    EXPECT_DOUBLE_EQ(*an::ui::starFilterValue(star, StarFilterField::Radius), 1.75);
    EXPECT_DOUBLE_EQ(*an::ui::starFilterValue(star, StarFilterField::Magnitude), -7.5);
    EXPECT_DOUBLE_EQ(*an::ui::starFilterValue(star, StarFilterField::FullWidthX), 4.0);
    EXPECT_DOUBLE_EQ(*an::ui::starFilterValue(star, StarFilterField::FullWidthY), 3.0);
    EXPECT_FALSE(an::ui::starFilterValue(starAt(1.0, 1.0), StarFilterField::Magnitude).has_value());
}

TEST(StarFilterTest, TheLimitsAreOverTheStarsThatHaveTheFigure)
{
    const an::ui::StarList stars = spacedStars(5u);
    const std::optional<an::ui::StarFilterLimits> amplitude =
        an::ui::starFilterLimits(stars, StarFilterField::Amplitude);
    ASSERT_TRUE(amplitude.has_value());
    EXPECT_DOUBLE_EQ(amplitude->minimum, 0.1);
    EXPECT_DOUBLE_EQ(amplitude->maximum, 0.5);
    const std::optional<an::ui::StarFilterLimits> magnitude =
        an::ui::starFilterLimits(stars, StarFilterField::Magnitude);
    ASSERT_TRUE(magnitude.has_value());
    EXPECT_DOUBLE_EQ(magnitude->minimum, -8.0);
    EXPECT_DOUBLE_EQ(magnitude->maximum, -5.0);
    EXPECT_FALSE(an::ui::starFilterLimits({}, StarFilterField::Amplitude).has_value());
}

TEST(StarFilterTest, ARequestThatChangesNothingKeepsTheListItself)
{
    const auto stars = std::make_shared<const an::ui::StarList>(spacedStars(4u));
    an::ui::StarFilterRequest request;
    request.includeNeighbours = true;
    EXPECT_FALSE(an::ui::starFilterApplies(request));
    EXPECT_EQ(an::ui::filteredStarList(stars, request), stars);
    EXPECT_EQ(an::ui::filterStars(*stars, request), (std::vector<std::size_t>{0u, 1u, 2u, 3u}));
}

// A star is kept when every range holds it, the ends included; a star without the figure a range reads is not.
TEST(StarFilterTest, AStarIsKeptWhenEveryRangeHoldsIt)
{
    const an::ui::StarList stars = spacedStars(6u);
    an::ui::StarFilterRequest request;
    request.ranges.push_back({.field = StarFilterField::Amplitude, .lowest = 0.2, .highest = 0.5});
    EXPECT_EQ(an::ui::filterStars(stars, request), (std::vector<std::size_t>{1u, 2u, 3u, 4u}));

    request.ranges.push_back({.field = StarFilterField::FullWidthX, .lowest = 2.0, .highest = 3.5});
    EXPECT_EQ(an::ui::filterStars(stars, request), (std::vector<std::size_t>{1u, 2u, 3u}));

    an::ui::StarFilterRequest bright;
    bright.ranges.push_back({.field = StarFilterField::Magnitude, .lowest = -100.0, .highest = 100.0});
    EXPECT_EQ(an::ui::filterStars(stars, bright), (std::vector<std::size_t>{0u, 1u, 2u, 3u, 4u}));
}

// The random share keeps exactly the share asked of the stars the ranges kept - none of the others - and the same seed
// keeps the same stars.
TEST(StarFilterTest, TheRandomShareIsExactAndFollowsItsSeed)
{
    const an::ui::StarList stars = spacedStars(40u);
    an::ui::StarFilterRequest request;
    request.ranges.push_back({.field = StarFilterField::Amplitude, .lowest = 1.05, .highest = 10.0});
    request.keepPercent = 50.0;
    request.seed = 7u;
    const std::vector<std::size_t> first = an::ui::filterStars(stars, request);
    ASSERT_EQ(first.size(), 15u);
    for (const std::size_t index : first)
    {
        EXPECT_GE(index, 10u) << "a star the range left out was kept";
    }
    EXPECT_EQ(an::ui::filterStars(stars, request), first);

    request.seed = 8u;
    EXPECT_NE(an::ui::filterStars(stars, request), first);

    request.keepPercent = 0.0;
    EXPECT_TRUE(an::ui::filterStars(stars, request).empty());
    request.keepPercent = 10.0;
    request.ranges.clear();
    EXPECT_EQ(an::ui::filterStars(stars, request).size(), 4u);
}

// The selection a seed makes is stated, so it is the same on every platform: the draw is the specified generator's own
// output, not a standard distribution whose algorithm each library chooses.
TEST(StarFilterTest, TheRandomShareIsTheSameOnEveryPlatform)
{
    const an::ui::StarList stars = spacedStars(10u);
    an::ui::StarFilterRequest request;
    request.keepPercent = 30.0;
    request.seed = 42u;
    const std::vector<std::size_t> kept = an::ui::filterStars(stars, request);
    ASSERT_EQ(kept.size(), 3u);
    const std::set<std::size_t> chosen(kept.begin(), kept.end());
    EXPECT_EQ(chosen.size(), 3u);
    RecordProperty("chosen", ::testing::PrintToString(kept));
    EXPECT_EQ(kept, (std::vector<std::size_t>{0u, 4u, 6u})) << ::testing::PrintToString(kept);
}

// A star left out whose box overlaps a kept star's is kept, and so on along a chain of overlapping boxes; a star whose
// box touches none stays out.
TEST(StarFilterTest, NeighboursAreKeptAlongAChainOfOverlappingBoxes)
{
    an::ui::StarList stars;
    // A chain: each box reaches four pixels either side of its star, and the stars are seven apart.
    for (int index = 0; index < 5; ++index)
    {
        stars.push_back(starAt(100.0 + 7.0 * index, 100.0, 2.0, 2.0, index == 0 ? 0.9 : 0.1));
    }
    // An isolated star, and one whose box only touches the chain's last one.
    stars.push_back(starAt(400.0, 400.0, 2.0, 2.0, 0.1));
    stars.push_back(starAt(100.0 + 7.0 * 4 + 8.0, 100.0, 2.0, 2.0, 0.1));

    an::ui::StarFilterRequest request;
    request.ranges.push_back({.field = StarFilterField::Amplitude, .lowest = 0.5, .highest = 1.0});
    EXPECT_EQ(an::ui::filterStars(stars, request), (std::vector<std::size_t>{0u}));
    request.includeNeighbours = true;
    EXPECT_EQ(an::ui::filterStars(stars, request), (std::vector<std::size_t>{0u, 1u, 2u, 3u, 4u}));

    // A larger scale makes larger boxes, and the touching star overlaps.
    request.scaleFactor = 1.5;
    EXPECT_EQ(an::ui::filterStars(stars, request), (std::vector<std::size_t>{0u, 1u, 2u, 3u, 4u, 6u}));
}

TEST(StarFilterTest, TheFilteredListHoldsTheKeptStarsInOrder)
{
    const auto stars = std::make_shared<const an::ui::StarList>(spacedStars(5u));
    an::ui::StarFilterRequest request;
    request.ranges.push_back({.field = StarFilterField::Amplitude, .lowest = 0.25, .highest = 0.45});
    const an::ui::StarListPtr kept = an::ui::filteredStarList(stars, request);
    ASSERT_EQ(kept->size(), 2u);
    EXPECT_DOUBLE_EQ((*kept)[0].positionX, 250.0);
    EXPECT_DOUBLE_EQ((*kept)[1].positionX, 350.0);
    EXPECT_NE(kept, stars);
    EXPECT_TRUE(an::ui::filteredStarList(nullptr, request)->empty());
}
