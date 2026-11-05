// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The comet composite and rejection-map statistics rows, their derived inputs and their engine-independent definitions.
#include "StackingNumericsOracle.h"
#include "StackingNumericsScenarios.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace an::bench;

namespace
{

/// A quiet field of the stated extent with one bright pixel cluster.
std::vector<float> quietField(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
{
    std::vector<float> field(static_cast<std::size_t>(width) * height * channels, 0.02f);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 10u; y < 13u; ++y)
        {
            for (std::uint32_t x = 20u; x < 24u; ++x)
            {
                field[(static_cast<std::size_t>(channel) * height + y) * width + x] = 0.3f;
            }
        }
    }
    return field;
}

} // namespace

TEST(StackingNumericsOracleTest, TheTwoRowsAreWellFormed)
{
    const std::vector<ScenarioSpec> rows = stackingNumericsScenarios();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].id, kCometComposite);
    EXPECT_EQ(rows[1].id, kRejectionMapStatistics);
    for (const ScenarioSpec& row : rows)
    {
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_TRUE(requiresStackingNumericsOracle(row)) << row.id;
        EXPECT_EQ(row.group, "stacking") << row.id;
        EXPECT_EQ(row.input.sampleType, SampleType::Float32) << row.id;
        EXPECT_EQ(row.correctnessClass, CorrectnessClass::AbsoluteRelative) << row.id;
        EXPECT_GT(row.tolerance, 0.0) << row.id;
        EXPECT_TRUE(row.parameters.contains(kTimedBatchParameter)) << row.id;
    }
    EXPECT_EQ(rows[0].input.channels, 1u);
    EXPECT_EQ(rows[1].input.channels, 3u);
    ScenarioSpec other = rows[0];
    other.id = "stacking.average.8x_f32";
    EXPECT_FALSE(requiresStackingNumericsOracle(other));
}

TEST(StackingNumericsOracleTest, TheCometFramesAreTheFieldWithTheCometWhereEachRegistrationPutsIt)
{
    constexpr std::uint32_t kWidth = 1024u;
    constexpr std::uint32_t kHeight = 8u;
    std::vector<float> field(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < field.size(); ++index)
    {
        field[index] = 0.01f * static_cast<float>(index % kWidth) / static_cast<float>(kWidth);
    }
    // The comet's centre row is outside this field's rows, so its profile adds the same to both registrations of a
    // column at the same distance from its centre.
    EXPECT_DOUBLE_EQ(cometStarAlignedX(0u), kCometStartX);
    EXPECT_DOUBLE_EQ(cometStarAlignedX(3u), kCometStartX + 3.0 * kCometStep);
    const std::vector<float> star = cometStarAlignedFrame(field, kWidth, kHeight, 3u);
    const std::vector<float> comet = cometCometAlignedFrame(field, kWidth, kHeight, 3u);
    ASSERT_EQ(star.size(), field.size());
    ASSERT_EQ(comet.size(), field.size());
    const auto profile = [](double dx, double dy) {
        const double spread = 1.0 + (dx * dx + dy * dy) / (kCometWidth * kCometWidth);
        return kCometPeak / (spread * spread);
    };
    for (const std::uint32_t x : {0u, 100u, 432u, 1000u, 1023u})
    {
        const double dy = 0.5 - kCometY;
        EXPECT_EQ(star[x],
                  static_cast<float>(static_cast<double>(field[x]) + profile((x + 0.5) - cometStarAlignedX(3u), dy)))
            << x;
        // The comet-aligned frame reads the field 48 columns further right, the last column repeated.
        const std::uint32_t from = (std::min)(x + 48u, kWidth - 1u);
        EXPECT_EQ(comet[x],
                  static_cast<float>(static_cast<double>(field[from]) + profile((x + 0.5) - kCometStartX, dy)))
            << x;
    }
}

TEST(StackingNumericsOracleTest, TheCompositeCheckMeasuresTheDistanceFromTheDefinition)
{
    // A field the composite check accepts only for the composite it defines: a flat sky, where every split's sky is
    // the sky and both stacks are the sky plus their own comet.
    constexpr std::uint32_t kWidth = 1024u;
    constexpr std::uint32_t kHeight = 1024u;
    const std::vector<float> field(static_cast<std::size_t>(kWidth) * kHeight, 0.05f);
    std::vector<float> composite(field.size(), 0.0f);
    ResultRecord unrelated;
    checkCometCompositeOutput(field, kWidth, kHeight, composite, unrelated);
    ASSERT_TRUE(unrelated.correctnessMetric.has_value());
    EXPECT_GT(*unrelated.correctnessMetric, 0.04);

    // Away from every aperture the composite is the star stack, which is the field with no comet beyond the
    // profile's tail; setting the composite to the field there leaves only the comet region different.
    for (std::size_t index = 0u; index < composite.size(); ++index)
    {
        composite[index] = field[index];
    }
    ResultRecord close;
    checkCometCompositeOutput(field, kWidth, kHeight, composite, close);
    ASSERT_TRUE(close.correctnessMetric.has_value());
    EXPECT_LT(*close.correctnessMetric, *unrelated.correctnessMetric);
    EXPECT_GT(*close.correctnessMetric, 1.0e-3) << "the comet itself is still missing";
}

TEST(StackingNumericsOracleTest, TheRejectionMapsAreTheirFormulas)
{
    constexpr std::uint32_t kWidth = 200u;
    constexpr std::uint32_t kHeight = 180u;
    const std::vector<float> field = quietField(kWidth, kHeight, 3u);
    const std::vector<std::uint32_t> high = rejectionHighCounts(field, kWidth, kHeight, 3u);
    const std::vector<std::uint32_t> low = rejectionLowCounts(field, kWidth, kHeight, 3u);
    ASSERT_EQ(high.size(), field.size());
    ASSERT_EQ(low.size(), field.size());
    // The bright cluster is rejected high (0.3 - 0.12) / 0.04 + 1 = 5 times, the quiet sky never.
    EXPECT_EQ(high[10u * kWidth + 20u], 5u);
    EXPECT_EQ(high[0], 0u);
    // A block of the low map's grid is rejected twice.
    EXPECT_EQ(low[0], 2u);
    EXPECT_EQ(low[2u * kWidth + 2u], 2u);
    EXPECT_EQ(low[97u], 2u);
    const std::vector<float> fractions = rejectionFractionMap(high);
    EXPECT_EQ(fractions[10u * kWidth + 20u], static_cast<float>(5.0 / kRejectionFrames));
}

TEST(StackingNumericsOracleTest, TheStatisticsCheckCountsTheUnionAndTheClusters)
{
    // A 12 by 10 plane: a 3 by 3 cluster on the low map, a 4 by 2 cluster and a single on the high map, overlapping
    // the low cluster in one element.
    constexpr std::uint32_t kWidth = 12u;
    constexpr std::uint32_t kHeight = 10u;
    std::vector<std::uint32_t> low(kWidth * kHeight, 0u);
    std::vector<std::uint32_t> high(kWidth * kHeight, 0u);
    for (std::uint32_t y = 1u; y < 4u; ++y)
    {
        for (std::uint32_t x = 1u; x < 4u; ++x)
        {
            low[y * kWidth + x] = 1u;
        }
    }
    for (std::uint32_t y = 3u; y < 5u; ++y)
    {
        for (std::uint32_t x = 3u; x < 7u; ++x)
        {
            high[y * kWidth + x] = 2u;
        }
    }
    high[8u * kWidth + 10u] = 1u;
    // The union holds 9 + 8 + 1 - 1 elements; each map's cluster holds at least eight, the single does not count.
    const RejectionMapStatistics right{.eitherElements = 17.0,
                                       .eitherFraction = 17.0 / 120.0,
                                       .clusters = 2.0,
                                       .largestClusterPixels = 9.0,
                                       .largestClusterFraction = 9.0 / 120.0};
    ResultRecord exact;
    checkRejectionMapStatistics(low, high, kWidth, kHeight, 1u, right, exact);
    ASSERT_TRUE(exact.correctnessMetric.has_value());
    EXPECT_EQ(*exact.correctnessMetric, 0.0);
    RejectionMapStatistics wrong = right;
    wrong.clusters = 3.0;
    ResultRecord off;
    checkRejectionMapStatistics(low, high, kWidth, kHeight, 1u, wrong, off);
    ASSERT_TRUE(off.correctnessMetric.has_value());
    EXPECT_EQ(*off.correctnessMetric, 1.0);
}
