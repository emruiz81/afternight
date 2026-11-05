// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The tone curve rows and the engine-independent curves they are held to.
#include "CurveOracle.h"
#include "CurveScenarios.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace an::bench;

namespace
{

/// The rows' own points.
[[nodiscard]] std::vector<CurvePoint> rowPoints()
{
    return std::vector<CurvePoint>(kCurveRowPoints.begin(), kCurveRowPoints.end());
}

} // namespace

TEST(CurveOracleTest, TheSixteenRowsAreWellFormedAndCompared)
{
    const std::vector<ScenarioSpec> rows = curveScenarios();
    const std::vector<std::string> expected{"pointwise.curve_akima.f32",
                                            "pointwise.curve_natural_cubic.f32",
                                            "pointwise.curve_steffen.f32",
                                            "pointwise.curve_akima.complete.f32",
                                            "pointwise.curve_natural_cubic.complete.f32",
                                            "pointwise.curve_steffen.complete.f32",
                                            "pointwise.curve_akima.preview.f32",
                                            "pointwise.curve_akima.complete.u16",
                                            "pointwise.curve_akima.preview.u16",
                                            "pointwise.curve_akima.complete.eagle.f32",
                                            "pointwise.curve_akima.complete.eagle.u16",
                                            "pointwise.curve_akima.complete.carina.f32",
                                            "pointwise.curve_akima.complete.carina.u16",
                                            "pointwise.curve_akima.complete.ngc253_rgb.f32",
                                            "pointwise.curve_akima.complete.carina_calibrated.f32",
                                            "pointwise.gamma_curve_fused.f32"};
    ASSERT_EQ(rows.size(), expected.size());
    for (std::size_t index = 0u; index < rows.size(); ++index)
    {
        const ScenarioSpec& row = rows[index];
        EXPECT_EQ(row.id, expected[index]);
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_EQ(row.group, "pointwise") << row.id;
        EXPECT_EQ(row.correctnessClass, CorrectnessClass::AbsoluteRelative) << row.id;
        EXPECT_GT(row.tolerance, 0.0) << row.id;
        EXPECT_EQ(isCurveScenario(row.id), row.id != "pointwise.gamma_curve_fused.f32") << row.id;
        if (isCurveScenario(row.id))
        {
            CurveRule rule = CurveRule::Linear;
            EXPECT_TRUE(curveRuleNamed(row.parameters.at("interpolation"), rule)) << row.id;
            EXPECT_TRUE(row.parameters.contains(kCurveBoundaryParameter)) << row.id;
            const bool codes = row.parameters.at(kCurveSamplesParameter) == kCurveCodesAsGiven;
            EXPECT_EQ(row.parameters.at("tableEntries"), codes ? "65536" : "8193") << row.id;
            EXPECT_EQ(codes, row.id.ends_with(".u16")) << row.id;
            EXPECT_EQ(row.input.sampleType == SampleType::UInt16,
                      codes || row.parameters.at(kCurveSamplesParameter) == kCurveCodes ||
                          row.parameters.at(kCurveSamplesParameter) == kCurveBinnedColour)
                << row.id;
            const bool preview = row.parameters.at(kCurveBoundaryParameter) == kCurvePreview;
            if (preview)
            {
                EXPECT_DOUBLE_EQ(row.tolerance, 5e-3) << row.id;
            }
        }
    }
    EXPECT_EQ(rows[14].parameters.at(kCurveSamplesParameter), kCurveHeldToUnit);
    EXPECT_EQ(rows[14].input.sampleType, SampleType::Float32);
    EXPECT_EQ(rows[13].elementsProcessed, std::uint64_t{2772u} * 1842u * 3u);
    EXPECT_EQ(rows[12].bytesProcessed, std::uint64_t{8288u} * 5644u * 2u);
    EXPECT_EQ(rows[15].parameters.at("tableEntries"), "4097");
}

TEST(CurveOracleTest, EveryRuleIsTheLineThroughCollinearPoints)
{
    const std::vector<CurvePoint> points{{0.0, 0.2}, {0.2, 0.32}, {0.45, 0.47}, {0.7, 0.62}, {1.0, 0.8}};
    for (const CurveRule rule : {CurveRule::Linear, CurveRule::NaturalCubic, CurveRule::Akima, CurveRule::Steffen})
    {
        const CurveOracle oracle(points, rule);
        ASSERT_TRUE(oracle.valid());
        for (int step = 0; step <= 100; ++step)
        {
            const long double input = static_cast<long double>(step) / 100.0L;
            EXPECT_NEAR(static_cast<double>(oracle.curveValue(input)), static_cast<double>(0.2L + 0.6L * input), 1e-15)
                << static_cast<int>(rule) << " at " << step;
        }
    }
}

TEST(CurveOracleTest, EveryRulePassesThroughItsPointsAndHoldsTheEnds)
{
    const std::vector<CurvePoint> points = rowPoints();
    for (const CurveRule rule : {CurveRule::Linear, CurveRule::NaturalCubic, CurveRule::Akima, CurveRule::Steffen})
    {
        const CurveOracle oracle(points, rule);
        ASSERT_TRUE(oracle.valid());
        for (const CurvePoint& point : points)
        {
            EXPECT_NEAR(static_cast<double>(oracle.curveValue(point.input)), point.output, 1e-15);
        }
        EXPECT_EQ(oracle.curveValue(-0.5L), 0.0L);
        EXPECT_EQ(oracle.curveValue(1.5L), 1.0L);
        EXPECT_TRUE(oracle.outsidePoints(-0.001L));
        EXPECT_TRUE(oracle.outsidePoints(1.001L));
        EXPECT_FALSE(oracle.outsidePoints(1.0L));
        EXPECT_TRUE(std::isnan(oracle.heldValue(std::nanl(""))));
    }
}

// Each rule at the middle of the rows' first two segments, from its own paper's definition worked by hand: the
// Hermite midpoint is the mean of the ends plus an eighth of the width times the difference of the end slopes.
TEST(CurveOracleTest, EachRuleHasItsPublishedSlopesOnTheRowsPoints)
{
    const std::vector<CurvePoint> points = rowPoints();
    // Akima: the secants 0.48, 1.52, 1.52, 0.48 extended to -1.6 and -0.56 at the start, so the slopes at the first
    // three points are -0.04, 1.52 and 1.52, and the second segment is its own secant.
    const CurveOracle akima(points, CurveRule::Akima);
    EXPECT_NEAR(static_cast<double>(akima.curveValue(0.125L)), 0.06 + 0.125 * 0.25 * (-0.04 - 1.52), 1e-15);
    EXPECT_NEAR(static_cast<double>(akima.curveValue(0.375L)), 0.31, 1e-15);
    // Below zero near the start, which the held value clamps.
    EXPECT_LT(akima.curveValue(0.01L), 0.0L);
    EXPECT_EQ(akima.heldValue(0.01L), 0.0L);
    // Steffen: the end secant at the first point, then twice the smallest of the two secants and half the parabola's
    // slope: 0.48, 0.96 and 1.52.
    const CurveOracle steffen(points, CurveRule::Steffen);
    EXPECT_NEAR(static_cast<double>(steffen.curveValue(0.125L)), 0.06 + 0.125 * 0.25 * (0.48 - 0.96), 1e-15);
    EXPECT_NEAR(static_cast<double>(steffen.curveValue(0.375L)), 0.31 + 0.125 * 0.25 * (0.96 - 1.52), 1e-15);
    // The natural cubic: second derivatives 0, 6.24, 0, -6.24 and 0 from the tridiagonal system, so a segment's
    // middle lies a sixteenth of the width squared times the sum of its ends' second derivatives below its chord.
    const CurveOracle natural(points, CurveRule::NaturalCubic);
    EXPECT_NEAR(static_cast<double>(natural.curveValue(0.125L)), 0.06 - 0.0625 / 16.0 * 6.24, 1e-15);
    EXPECT_NEAR(static_cast<double>(natural.curveValue(0.375L)), 0.31 - 0.0625 / 16.0 * 6.24, 1e-15);
    EXPECT_NEAR(static_cast<double>(natural.curveValue(0.625L)), 0.69 + 0.0625 / 16.0 * 6.24, 1e-15);
}

TEST(CurveOracleTest, TheNaturalCubicIsSmoothAndFlatInCurvatureAtItsEnds)
{
    const std::vector<CurvePoint> points{{0.0, 0.0}, {0.1, 0.3}, {0.35, 0.4}, {0.6, 0.85}, {1.0, 0.9}};
    const CurveOracle natural(points, CurveRule::NaturalCubic);
    ASSERT_TRUE(natural.valid());
    // A cubic's second difference is exact, so these are the second derivatives to rounding.
    const long double step = 1e-4L;
    const auto second = [&](long double at) {
        return (natural.curveValue(at + step) - 2.0L * natural.curveValue(at) + natural.curveValue(at - step)) /
               (step * step);
    };
    // On an end segment the second derivative is a straight line, and it reaches zero at the end.
    EXPECT_NEAR(static_cast<double>(2.0L * second(0.02L) - second(0.04L)), 0.0, 1e-4);
    EXPECT_NEAR(static_cast<double>(2.0L * second(0.98L) - second(0.96L)), 0.0, 1e-4);
    EXPECT_GT(std::fabs(static_cast<double>(second(0.1L))), 1.0);
    // The slope is continuous through every interior point.
    const long double near = 1e-6L;
    for (const long double knot : {0.1L, 0.35L, 0.6L})
    {
        const long double left = (natural.curveValue(knot) - natural.curveValue(knot - near)) / near;
        const long double right = (natural.curveValue(knot + near) - natural.curveValue(knot)) / near;
        EXPECT_NEAR(static_cast<double>(left), static_cast<double>(right), 1e-3) << static_cast<double>(knot);
    }
}

TEST(CurveOracleTest, SteffensRuleNeverOvershootsMonotonePoints)
{
    const std::vector<CurvePoint> points{{0.0, 0.0}, {0.2, 0.01}, {0.25, 0.9}, {0.7, 0.95}, {1.0, 1.0}};
    const CurveOracle steffen(points, CurveRule::Steffen);
    ASSERT_TRUE(steffen.valid());
    long double previous = steffen.curveValue(0.0L);
    for (int step = 1; step <= 1000; ++step)
    {
        const long double value = steffen.curveValue(static_cast<long double>(step) / 1000.0L);
        EXPECT_GE(value, previous) << step;
        previous = value;
    }
    // The natural cubic through the same points does overshoot, which is what the comparison exists for.
    const CurveOracle natural(points, CurveRule::NaturalCubic);
    bool overshoots = false;
    for (int step = 0; step <= 1000; ++step)
    {
        const long double value = natural.curveValue(static_cast<long double>(step) / 1000.0L);
        overshoots = overshoots || value > 1.0L || value < 0.0L;
    }
    EXPECT_TRUE(overshoots);
}

TEST(CurveOracleTest, RefusesPointsThatDescribeNoCurve)
{
    const std::vector<CurvePoint> two{{0.0, 0.0}, {1.0, 1.0}};
    EXPECT_TRUE(CurveOracle(two, CurveRule::Linear).valid());
    EXPECT_FALSE(CurveOracle(two, CurveRule::Akima).valid());
    const std::vector<CurvePoint> repeated{{0.0, 0.0}, {0.5, 0.2}, {0.5, 0.4}, {1.0, 1.0}};
    EXPECT_FALSE(CurveOracle(repeated, CurveRule::NaturalCubic).valid());
    const std::vector<CurvePoint> infinite{{0.0, 0.0}, {0.5, INFINITY}, {1.0, 1.0}};
    EXPECT_FALSE(CurveOracle(infinite, CurveRule::Steffen).valid());
    CurveRule rule = CurveRule::Linear;
    EXPECT_FALSE(curveRuleNamed("monotone-cubic", rule));
    EXPECT_TRUE(curveRuleNamed("natural-cubic", rule));
    EXPECT_EQ(rule, CurveRule::NaturalCubic);
}

TEST(CurveOracleTest, TheCodesCheckReadsEveryCodeAsAFraction)
{
    const CurveOracle akima(rowPoints(), CurveRule::Akima);
    const std::vector<std::uint16_t> input{0u, 1u, 655u, 16384u, 32768u, 49151u, 65535u};
    std::vector<std::uint16_t> output;
    for (const std::uint16_t code : input)
    {
        output.push_back(static_cast<std::uint16_t>(std::lround(akima.heldValue(code / 65535.0L) * 65535.0L)));
    }
    CurveOutputCheck check;
    ASSERT_TRUE(checkCurveOutputCodes(akima, input, output, check));
    EXPECT_EQ(check.samples, input.size());
    EXPECT_LE(check.largest, 0.5 / 65535.0 + 1e-12);
    EXPECT_EQ(check.clampedInputs, 0u);
    EXPECT_EQ(check.heldOutputs, 2u);
    output[4] = static_cast<std::uint16_t>(output[4] + 3u);
    ASSERT_TRUE(checkCurveOutputCodes(akima, input, output, check));
    EXPECT_EQ(check.largestIndex, 4u);
    EXPECT_GT(check.largest, 2.0 / 65535.0);
}

TEST(CurveOracleTest, TheCompleteCheckComparesEverySampleAndCountsTheEnds)
{
    const CurveOracle akima(rowPoints(), CurveRule::Akima);
    const std::vector<float> input{-0.25f, 0.0f, 0.01f, 0.375f, 0.5f, 1.0f, 1.5f};
    std::vector<float> output;
    for (const float sample : input)
    {
        output.push_back(static_cast<float>(akima.heldValue(sample)));
    }
    CurveOutputCheck check;
    ASSERT_TRUE(checkCurveOutput(akima, input, output, check));
    EXPECT_EQ(check.samples, input.size());
    EXPECT_LT(check.largest, 1e-7);
    EXPECT_EQ(check.clampedInputs, 2u);
    EXPECT_EQ(check.heldOutputs, 1u);
    output[3] += 0.001f;
    ASSERT_TRUE(checkCurveOutput(akima, input, output, check));
    EXPECT_NEAR(check.largest, 0.001, 1e-6);
    EXPECT_EQ(check.largestIndex, 3u);
    output[4] = std::nanf("");
    ASSERT_TRUE(checkCurveOutput(akima, input, output, check));
    EXPECT_TRUE(std::isinf(check.largest));
    EXPECT_EQ(check.largestIndex, 4u);
    EXPECT_FALSE(checkCurveOutput(akima, input, std::span<const float>(output).first(3u), check));
}
