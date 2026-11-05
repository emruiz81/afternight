// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The channel scope and image difference rows and their engine-independent definitions.
#include "ChannelScopeOracle.h"
#include "ChannelScopeScenarios.h"
#include "DifferenceOracle.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace an::bench;

TEST(ChannelScopeOracleTest, BothRowsAreWellFormed)
{
    const std::vector<ScenarioSpec> rows = channelScopeScenarios();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].id, kChannelScopeLabLightness);
    EXPECT_EQ(rows[0].group, "color");
    EXPECT_TRUE(requiresChannelScopeOracle(rows[0]));
    EXPECT_FALSE(requiresDifferenceOracle(rows[0]));
    EXPECT_EQ(rows[0].tolerance, 1.0e-4);
    EXPECT_EQ(rows[1].id, kImageDifference);
    EXPECT_EQ(rows[1].group, "pointwise");
    EXPECT_TRUE(requiresDifferenceOracle(rows[1]));
    EXPECT_FALSE(requiresChannelScopeOracle(rows[1]));
    EXPECT_EQ(rows[0].correctnessClass, CorrectnessClass::AbsoluteRelative);
    EXPECT_EQ(rows[1].correctnessClass, CorrectnessClass::BitExact);
    for (const ScenarioSpec& row : rows)
    {
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_EQ(row.input.channels, 3u) << row.id;
        EXPECT_EQ(row.input.sampleType, SampleType::Float32) << row.id;
        EXPECT_TRUE(row.parameters.contains(kTimedBatchParameter)) << row.id;
    }
}

TEST(ChannelScopeOracleTest, AGammaOfOneIsTheRoundTripAndWhiteStaysWhite)
{
    const std::vector<std::array<long double, 3u>> colours{
        {0.5L, 0.5L, 0.5L}, {1.0L, 0.0L, 0.0L}, {0.02L, 0.6L, 0.3L}, {0.9L, 0.85L, 0.1L}, {0.001L, 0.002L, 0.003L}};
    for (const std::array<long double, 3u>& colour : colours)
    {
        const std::array<long double, 3u> back = channelScopeLightnessGamma(colour, 1.0L);
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_NEAR(static_cast<double>(back[channel]), static_cast<double>(colour[channel]), 1.0e-12);
        }
    }
    const std::array<long double, 3u> white = channelScopeLightnessGamma({1.0L, 1.0L, 1.0L}, 2.2L);
    for (const long double value : white)
    {
        EXPECT_NEAR(static_cast<double>(value), 1.0, 1.0e-12);
    }
}

TEST(ChannelScopeOracleTest, AGreyStaysGreyAndItsLightnessIsRaised)
{
    // A neutral grey has no opponent colour under the D65 white, so the process moves its lightness alone.
    const std::array<long double, 3u> grey = channelScopeLightnessGamma({0.2L, 0.2L, 0.2L}, 2.2L);
    EXPECT_NEAR(static_cast<double>(grey[0]), static_cast<double>(grey[1]), 1.0e-9);
    EXPECT_NEAR(static_cast<double>(grey[1]), static_cast<double>(grey[2]), 1.0e-9);
    EXPECT_GT(static_cast<double>(grey[0]), 0.2);
    // The sRGB value 0.2 is a CIE lightness of 21.2467; raised to one over 2.2 in the unit interval it is 49.4565,
    // which is the sRGB value 0.460966.
    EXPECT_NEAR(static_cast<double>(grey[0]), 0.460966, 1.0e-6);
}

TEST(ChannelScopeOracleTest, TheCheckClampsBothSidesAndMeasuresTheRest)
{
    constexpr std::uint32_t width = 3u;
    constexpr std::uint32_t height = 1u;
    const std::vector<float> input{0.2f, 0.9f, 0.5f, 0.2f, 0.05f, 0.5f, 0.2f, 0.05f, 0.5f};
    std::vector<float> output(input.size());
    for (std::size_t pixel = 0u; pixel < 3u; ++pixel)
    {
        const std::array<long double, 3u> expected = channelScopeLightnessGamma(
            {input[pixel], input[3u + pixel], input[6u + pixel]}, static_cast<long double>(kChannelScopeGamma));
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            output[channel * 3u + pixel] = static_cast<float>(expected[channel]);
        }
    }
    ResultRecord exact;
    checkChannelScopeOutput(input, width, height, output, kChannelScopeGamma, exact);
    ASSERT_TRUE(exact.correctnessMetric.has_value());
    EXPECT_LT(*exact.correctnessMetric, 1.0e-7);
    EXPECT_EQ(exact.counters.at("channelScopeOracleSamples"), 9.0);

    std::vector<float> clamped = output;
    for (float& value : clamped)
    {
        value = std::fmin(std::fmax(value, 0.0f), 1.0f);
    }
    ResultRecord heldToTheUnitInterval;
    checkChannelScopeOutput(input, width, height, clamped, kChannelScopeGamma, heldToTheUnitInterval);
    ASSERT_TRUE(heldToTheUnitInterval.correctnessMetric.has_value());
    EXPECT_LT(*heldToTheUnitInterval.correctnessMetric, 1.0e-7)
        << "a recomposition held to the unit interval still agrees";

    std::vector<float> wrong = output;
    wrong[4] += 0.01f;
    ResultRecord perturbed;
    checkChannelScopeOutput(input, width, height, wrong, kChannelScopeGamma, perturbed);
    ASSERT_TRUE(perturbed.correctnessMetric.has_value());
    EXPECT_NEAR(*perturbed.correctnessMetric, 0.01, 1.0e-6);
}

TEST(DifferenceOracleTest, TheSecondFrameIsTheFirstReadFromAFixedOffset)
{
    std::vector<float> first(40u);
    for (std::size_t index = 0u; index < first.size(); ++index)
    {
        first[index] = static_cast<float>(index);
    }
    const std::vector<float> second = differenceSecondInput(first, 4u);
    ASSERT_EQ(second.size(), first.size());
    // Seven rows of four and three samples on is an offset of 31.
    EXPECT_EQ(second[0], 31.0f);
    EXPECT_EQ(second[8], 39.0f);
    EXPECT_EQ(second[9], 0.0f);
    EXPECT_TRUE(differenceSecondInput({}, 4u).empty());
}

TEST(DifferenceOracleTest, TheCheckIsExactAndNamesWhatDiffers)
{
    const std::vector<float> first{0.1f, 0.5f, 0.25f, 0.75f};
    const std::vector<float> second{0.4f, 0.5f, 0.3f, 0.1f};
    std::vector<float> output(first.size());
    for (std::size_t index = 0u; index < first.size(); ++index)
    {
        output[index] =
            static_cast<float>(std::fabs(static_cast<double>(first[index]) - static_cast<double>(second[index])));
    }
    ResultRecord exact;
    checkImageDifferenceOutput(first, second, output, exact);
    ASSERT_TRUE(exact.correctnessMetric.has_value());
    EXPECT_EQ(*exact.correctnessMetric, 0.0);
    EXPECT_EQ(exact.counters.at("differenceOracleDifferingSamples"), 0.0);

    output[2] = std::nextafter(output[2], 1.0f);
    ResultRecord lastBit;
    checkImageDifferenceOutput(first, second, output, lastBit);
    ASSERT_TRUE(lastBit.correctnessMetric.has_value());
    EXPECT_GT(*lastBit.correctnessMetric, 0.0);
    EXPECT_EQ(lastBit.counters.at("differenceOracleDifferingSamples"), 1.0);

    ResultRecord short_;
    checkImageDifferenceOutput(first, second, std::vector<float>(3u), short_);
    ASSERT_TRUE(short_.correctnessMetric.has_value());
    EXPECT_EQ(*short_.correctnessMetric, 1.0);
}
