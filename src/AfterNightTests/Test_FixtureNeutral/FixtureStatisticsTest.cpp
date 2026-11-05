// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/common/FixtureImage.h"
#include "fixtures/common/FixtureStatistics.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureChannelStatistics;
using an::tests::fixtures::FixtureSamples;
using an::tests::fixtures::FixtureSampleType;

FixtureSamples row(FixtureSampleType sampleType, const std::vector<double>& values)
{
    FixtureSamples samples(static_cast<std::uint32_t>(values.size()), 1u, 1u, sampleType);
    for (std::uint32_t x = 0u; x < values.size(); ++x)
    {
        samples.setValue(x, 0u, 0u, values[x]);
    }
    return samples;
}

TEST(FixtureStatisticsTest, AnEvenCountHasTheMeanOfItsMiddleValuesAsMedian)
{
    const std::vector<FixtureChannelStatistics> statistics =
        an::tests::fixtures::computeFixtureStatistics(row(FixtureSampleType::UInt16, {4.0, 1.0, 3.0, 2.0}));
    ASSERT_EQ(statistics.size(), 1u);
    EXPECT_EQ(statistics[0].count, 4u);
    EXPECT_EQ(statistics[0].nonFinite, 0u);
    EXPECT_EQ(statistics[0].minimum, 1.0);
    EXPECT_EQ(statistics[0].maximum, 4.0);
    EXPECT_EQ(statistics[0].sum, 10.0);
    EXPECT_EQ(statistics[0].mean, 2.5);
    EXPECT_EQ(statistics[0].median, 2.5);
}

TEST(FixtureStatisticsTest, AnOddCountHasItsMiddleValueAsMedian)
{
    const std::vector<FixtureChannelStatistics> statistics =
        an::tests::fixtures::computeFixtureStatistics(row(FixtureSampleType::Int16, {5.0, -1.0, 3.0}));
    ASSERT_EQ(statistics.size(), 1u);
    EXPECT_EQ(statistics[0].median, 3.0);
    EXPECT_EQ(statistics[0].minimum, -1.0);
    EXPECT_DOUBLE_EQ(statistics[0].mean, 7.0 / 3.0);
}

TEST(FixtureStatisticsTest, NonFiniteSamplesAreCountedApartAndLeftOutOfEveryFigure)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::vector<FixtureChannelStatistics> statistics = an::tests::fixtures::computeFixtureStatistics(
        row(FixtureSampleType::Float32, {nan, 1.0, infinity, 3.0, -infinity}));
    ASSERT_EQ(statistics.size(), 1u);
    EXPECT_EQ(statistics[0].count, 2u);
    EXPECT_EQ(statistics[0].nonFinite, 3u);
    EXPECT_EQ(statistics[0].minimum, 1.0);
    EXPECT_EQ(statistics[0].maximum, 3.0);
    EXPECT_EQ(statistics[0].mean, 2.0);
    EXPECT_EQ(statistics[0].median, 2.0);

    const std::vector<FixtureChannelStatistics> none =
        an::tests::fixtures::computeFixtureStatistics(row(FixtureSampleType::Float64, {nan, infinity}));
    ASSERT_EQ(none.size(), 1u);
    EXPECT_EQ(none[0].count, 0u);
    EXPECT_EQ(none[0].nonFinite, 2u);
    EXPECT_EQ(none[0].mean, 0.0);
}

TEST(FixtureStatisticsTest, EveryChannelIsMeasuredOnItsOwn)
{
    FixtureSamples samples(2u, 2u, 3u, FixtureSampleType::UInt32);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 2u; ++x)
            {
                samples.setValue(x, y, channel, 1000.0 * (channel + 1u) + x + 2u * y);
            }
        }
    }
    const std::vector<FixtureChannelStatistics> statistics = an::tests::fixtures::computeFixtureStatistics(samples);
    ASSERT_EQ(statistics.size(), 3u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(statistics[channel].minimum, 1000.0 * (channel + 1u));
        EXPECT_EQ(statistics[channel].maximum, 1000.0 * (channel + 1u) + 3.0);
        EXPECT_EQ(statistics[channel].mean, 1000.0 * (channel + 1u) + 1.5);
    }
}

TEST(FixtureStatisticsTest, TheSumOfLargeIntegersIsExact)
{
    const std::vector<FixtureChannelStatistics> statistics = an::tests::fixtures::computeFixtureStatistics(
        row(FixtureSampleType::UInt32, {4294967295.0, 4294967295.0, 4294967295.0, 1.0}));
    ASSERT_EQ(statistics.size(), 1u);
    EXPECT_EQ(statistics[0].sum, 3.0 * 4294967295.0 + 1.0);
}

} // namespace
