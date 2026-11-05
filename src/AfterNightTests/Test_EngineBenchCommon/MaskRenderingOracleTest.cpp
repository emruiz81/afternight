// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The mask overlay and brush stroke rows and their engine-independent definitions.
#include "MaskRenderingOracle.h"
#include "MaskRenderingScenarios.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace an::bench;

TEST(MaskRenderingOracleTest, BothRowsAreWellFormed)
{
    const std::vector<ScenarioSpec> rows = maskRenderingScenarios();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].id, kMaskRenderTint);
    EXPECT_EQ(rows[0].group, "masks");
    EXPECT_TRUE(requiresMaskOverlayOracle(rows[0]));
    EXPECT_FALSE(requiresDabBlendOracle(rows[0]));
    EXPECT_EQ(rows[0].input.sampleType, SampleType::UInt8);
    EXPECT_EQ(rows[1].id, kDabBlend);
    EXPECT_EQ(rows[1].group, "pointwise");
    EXPECT_TRUE(requiresDabBlendOracle(rows[1]));
    EXPECT_FALSE(requiresMaskOverlayOracle(rows[1]));
    EXPECT_EQ(rows[1].input.sampleType, SampleType::Float32);
    for (const ScenarioSpec& row : rows)
    {
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_EQ(row.input.channels, 1u) << row.id;
        EXPECT_EQ(row.correctnessClass, CorrectnessClass::BitExact) << row.id;
        EXPECT_TRUE(row.parameters.contains(kTimedBatchParameter)) << row.id;
    }
}

TEST(MaskRenderingOracleTest, TheOverlayCheckIsExactAndCountsEveryDifferingPixel)
{
    const std::vector<std::uint8_t> mask{0u, 1u, 128u, 254u, 255u};
    std::vector<std::uint8_t> layer;
    for (const std::uint8_t code : mask)
    {
        layer.insert(layer.end(), {255u, 0u, 0u, static_cast<std::uint8_t>(255u - code)});
    }
    ResultRecord exact;
    checkMaskOverlayOutput(mask, layer, 255u, 0u, 0u, exact);
    EXPECT_EQ(exact.correctnessMetric, 0.0);
    EXPECT_EQ(exact.counters["maskOverlayOracleDifferingPixels"], 0.0);
    EXPECT_EQ(exact.counters["maskOverlayOraclePixels"], 5.0);

    // One alpha a code off and one green byte three off: two pixels differ, the largest by three.
    layer[2u * 4u + 3u] += 1u;
    layer[4u * 4u + 1u] = 3u;
    ResultRecord off;
    checkMaskOverlayOutput(mask, layer, 255u, 0u, 0u, off);
    EXPECT_EQ(off.correctnessMetric, 3.0);
    EXPECT_EQ(off.counters["maskOverlayOracleDifferingPixels"], 2.0);

    // A layer of the wrong size is a full-scale miss.
    layer.pop_back();
    ResultRecord short_;
    checkMaskOverlayOutput(mask, layer, 255u, 0u, 0u, short_);
    EXPECT_EQ(short_.correctnessMetric, 255.0);
}

TEST(MaskRenderingOracleTest, TheStrokesPointsAreQuarterPixelsZigzaggingInsideThePlane)
{
    const std::vector<DabBlendPoint> points = dabBlendStrokePoints(2048u, 2048u);
    ASSERT_EQ(points.size(), 24u);
    for (std::size_t index = 0u; index < points.size(); ++index)
    {
        EXPECT_EQ(points[index].x * 4.0, std::floor(points[index].x * 4.0)) << index;
        EXPECT_EQ(points[index].y * 4.0, std::floor(points[index].y * 4.0)) << index;
        EXPECT_GE(points[index].x, 64.0) << index;
        EXPECT_LE(points[index].x, 2048.0 - 64.0) << index;
        EXPECT_GT(points[index].y, 0.0) << index;
        EXPECT_LT(points[index].y, 2048.0) << index;
        if (index > 0u)
        {
            EXPECT_GT(points[index].x, points[index - 1u].x) << index;
            EXPECT_NE(points[index].y < 1024.0, points[index - 1u].y < 1024.0) << index;
        }
    }
}

TEST(MaskRenderingOracleTest, TheStrokeLeavesUncoveredPaintAndMovesCoveredPaintTowardsItsCoverage)
{
    // Large enough that every point of the zigzag lies inside the plane.
    const std::uint32_t side = 512u;
    const std::vector<float> paint(static_cast<std::size_t>(side) * side, 0.25f);
    const std::vector<float> painted = dabBlendExpected(paint, side, side);
    ASSERT_EQ(painted.size(), paint.size());
    const std::vector<DabBlendPoint> points = dabBlendStrokePoints(side, side);
    // Every stroke point's own pixel takes at least the dab centred on it, half the tip's peak, so it moves at least
    // four fifths of the way from a quarter to a half.
    for (const DabBlendPoint& point : points)
    {
        const auto x = static_cast<std::size_t>(std::lround(point.x));
        const auto y = static_cast<std::size_t>(std::lround(point.y));
        const float value = painted[y * side + x];
        EXPECT_GE(value, 0.25f + (0.5f - 0.25f) * 0.8f - 1.0e-6f) << point.x << "," << point.y;
        EXPECT_LE(value, 1.0f) << point.x << "," << point.y;
    }
    // The top-left corner is farther from the stroke than the brush reaches, so it keeps the paint.
    EXPECT_EQ(painted[0], 0.25f);
    EXPECT_EQ(painted[static_cast<std::size_t>(side) * side - 1u], 0.25f);
    std::size_t covered = 0u;
    for (const float value : painted)
    {
        EXPECT_TRUE(std::isfinite(value));
        covered += value != 0.25f ? 1u : 0u;
    }
    EXPECT_GT(covered, static_cast<std::size_t>(side) * side / 10u);
}

TEST(MaskRenderingOracleTest, TheStrokeCheckIsExactAndNamesWhatDiffers)
{
    const std::uint32_t side = 128u;
    std::vector<float> paint(static_cast<std::size_t>(side) * side, 0.0f);
    for (std::size_t index = 0u; index < paint.size(); ++index)
    {
        paint[index] = static_cast<float>(index % 97u) / 96.0f;
    }
    std::vector<float> painted = dabBlendExpected(paint, side, side);
    ResultRecord exact;
    checkDabBlendOutput(paint, side, side, painted, exact);
    EXPECT_EQ(exact.correctnessMetric, 0.0);
    EXPECT_EQ(exact.counters["dabBlendOracleDifferingSamples"], 0.0);
    painted[1000u] = std::nextafter(painted[1000u], 2.0f);
    ResultRecord off;
    checkDabBlendOutput(paint, side, side, painted, off);
    EXPECT_GT(off.correctnessMetric, 0.0);
    EXPECT_EQ(off.counters["dabBlendOracleDifferingSamples"], 1.0);
}
