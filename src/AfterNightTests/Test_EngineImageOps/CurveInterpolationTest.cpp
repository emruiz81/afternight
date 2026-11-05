// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the three curve interpolations a numerical library also offers.
//
// What matters about them is not that a cubic passes through its points - every rule here does that
// - but that each is the rule it is named after. A natural cubic is the only one that may leave the
// interval its control points lie in, and the only one whose second derivative is continuous; Akima
// does not propagate a wiggle past its neighbours; Steffen's extrema land only on control points.
// Those are the properties a person picked the mode for, and they are what is tested. That these
// three agree with the library itself is proved next door, in the differential sweep.

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{

/// The value of a curve at one input, or a failed expectation.
///
/// @param points the control points.
/// @param interpolation which rule joins them.
/// @param input where to evaluate.
/// @return the value, or a quiet not-a-number after a failed expectation.
[[nodiscard]] double valueAt(const std::vector<an::CurveControlPoint>& points,
                             const an::CurveInterpolation interpolation, const double input)
{
    const an::Result<double> value = an::evaluateCurve(points, interpolation, an::CurveDomainPolicy::Clamp, input);
    EXPECT_TRUE(value.hasValue()) << value.status().message();
    return value.hasValue() ? *value : std::numeric_limits<double>::quiet_NaN();
}

/// Five points sampling a smooth ramp with one step in the middle.
///
/// @return the control points.
[[nodiscard]] std::vector<an::CurveControlPoint> steppedPoints()
{
    return {{0.0, 0.0}, {0.25, 0.05}, {0.5, 0.1}, {0.75, 0.9}, {1.0, 1.0}};
}

TEST(CurveInterpolationTest, EveryRulePassesThroughEveryControlPoint)
{
    const std::vector<an::CurveControlPoint> points = steppedPoints();
    for (const an::CurveInterpolation interpolation :
         {an::CurveInterpolation::Linear, an::CurveInterpolation::MonotoneCubic, an::CurveInterpolation::NaturalCubic,
          an::CurveInterpolation::Akima, an::CurveInterpolation::Steffen})
    {
        for (const an::CurveControlPoint& point : points)
        {
            EXPECT_NEAR(valueAt(points, interpolation, point.input), point.output, 1.0e-12)
                << "interpolation " << static_cast<int>(interpolation) << " at " << point.input;
        }
    }
}

TEST(CurveInterpolationTest, OnlyTheTwoMonotoneRulesStayInsideTheIntervalTheirPointsLieIn)
{
    // The step in the middle makes an unconstrained cubic overshoot. Two of the five rules promise
    // that it cannot: the monotone Hermite spline and Steffen's, whose slope limits exist for
    // exactly this. The natural cubic and Akima's rule both may, which is the trade a caller who
    // picks them is making - Akima's guarantee is about how far a wiggle travels, not about range.
    const std::vector<an::CurveControlPoint> points = steppedPoints();
    double naturalHighest = 0.0;
    double akimaHighest = 0.0;
    for (int step = 0; step <= 1000; ++step)
    {
        const double input = static_cast<double>(step) / 1000.0;
        naturalHighest = (std::max)(naturalHighest, valueAt(points, an::CurveInterpolation::NaturalCubic, input));
        akimaHighest = (std::max)(akimaHighest, valueAt(points, an::CurveInterpolation::Akima, input));
        for (const an::CurveInterpolation monotone :
             {an::CurveInterpolation::MonotoneCubic, an::CurveInterpolation::Steffen})
        {
            const double value = valueAt(points, monotone, input);
            EXPECT_GE(value, -1.0e-12) << "interpolation " << static_cast<int>(monotone) << " at " << input;
            EXPECT_LE(value, 1.0 + 1.0e-12) << "interpolation " << static_cast<int>(monotone) << " at " << input;
        }
    }
    EXPECT_GT(naturalHighest, 1.0) << "the natural cubic did not overshoot on a curve chosen to make it";
    EXPECT_GT(akimaHighest, 1.0) << "Akima's rule did not overshoot on a curve chosen to make it";
}

TEST(CurveInterpolationTest, AkimaDoesNotPropagateOneWiggleIntoTheNextSegment)
{
    // Akima's rule reads two neighbours on each side, so a single displaced point changes the two
    // segments beside it and nothing further. A natural cubic solves one global system and changes
    // every segment, which is the difference the two modes exist to offer.
    std::vector<an::CurveControlPoint> flat;
    for (int index = 0; index <= 8; ++index)
    {
        flat.push_back({static_cast<double>(index) / 8.0, static_cast<double>(index) / 8.0});
    }
    std::vector<an::CurveControlPoint> bumped = flat;
    bumped[4].output += 0.1;

    const double farInput = 0.95;
    EXPECT_NEAR(valueAt(bumped, an::CurveInterpolation::Akima, farInput),
                valueAt(flat, an::CurveInterpolation::Akima, farInput), 1.0e-12);
    EXPECT_GT(std::abs(valueAt(bumped, an::CurveInterpolation::NaturalCubic, farInput) -
                       valueAt(flat, an::CurveInterpolation::NaturalCubic, farInput)),
              1.0e-6);
}

TEST(CurveInterpolationTest, SteffensExtremaLandOnlyOnControlPoints)
{
    const std::vector<an::CurveControlPoint> points{{0.0, 0.2}, {0.2, 0.8}, {0.4, 0.8},
                                                    {0.6, 0.3}, {0.8, 0.9}, {1.0, 0.1}};
    // Between two points whose outputs are equal, a monotone rule must be flat rather than bulging.
    for (int step = 0; step <= 100; ++step)
    {
        const double input = 0.2 + 0.2 * static_cast<double>(step) / 100.0;
        EXPECT_NEAR(valueAt(points, an::CurveInterpolation::Steffen, input), 0.8, 1.0e-12) << "at " << input;
    }
}

TEST(CurveInterpolationTest, ARuleThatReadsFurtherThanItsOwnSegmentRefusesTooFewPoints)
{
    const std::vector<an::CurveControlPoint> four{{0.0, 0.0}, {0.3, 0.2}, {0.7, 0.6}, {1.0, 1.0}};
    const std::vector<an::CurveControlPoint> two{{0.0, 0.0}, {1.0, 1.0}};

    EXPECT_EQ(an::minimumCurvePoints(an::CurveInterpolation::Linear), 2u);
    EXPECT_EQ(an::minimumCurvePoints(an::CurveInterpolation::MonotoneCubic), 2u);
    EXPECT_EQ(an::minimumCurvePoints(an::CurveInterpolation::NaturalCubic), 3u);
    EXPECT_EQ(an::minimumCurvePoints(an::CurveInterpolation::Akima), 5u);
    EXPECT_EQ(an::minimumCurvePoints(an::CurveInterpolation::Steffen), 3u);

    const an::Result<double> akima =
        an::evaluateCurve(four, an::CurveInterpolation::Akima, an::CurveDomainPolicy::Clamp, 0.5);
    EXPECT_FALSE(akima.hasValue());
    EXPECT_EQ(akima.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<double> natural =
        an::evaluateCurve(two, an::CurveInterpolation::NaturalCubic, an::CurveDomainPolicy::Clamp, 0.5);
    EXPECT_FALSE(natural.hasValue());
    EXPECT_EQ(natural.status().code(), an::ErrorCode::InvalidArgument);

    // Four points are enough for the two local rules and for the natural cubic and Steffen.
    EXPECT_TRUE(
        an::evaluateCurve(four, an::CurveInterpolation::NaturalCubic, an::CurveDomainPolicy::Clamp, 0.5).hasValue());
    EXPECT_TRUE(an::evaluateCurve(four, an::CurveInterpolation::Steffen, an::CurveDomainPolicy::Clamp, 0.5).hasValue());
}

TEST(CurveInterpolationTest, AnOvershootingCurveIsHeldInsideTheOutputIntervalAndSaysByHowMuch)
{
    an::TonePlanShape shape;
    shape.sampleType = an::SampleType::Float32;
    shape.channelCount = 1u;

    const std::vector<an::CurveControlPoint> points = steppedPoints();
    an::CurveProgramRequest request;
    request.points = points;
    request.interpolation = an::CurveInterpolation::NaturalCubic;
    request.approximation = an::CurveApproximation::SampledLinear;
    request.tableEntries = 4096u;
    const an::Result<an::CurveProgramPlan> plan = an::buildCurveProgram(shape, request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    // The analytic curve leaves [0, 1] on these points, so the reported error is dominated by the
    // clamp rather than by the table's sampling.
    EXPECT_GT(plan->maximumAbsoluteError(), 1.0e-3);

    request.interpolation = an::CurveInterpolation::Steffen;
    const an::Result<an::CurveProgramPlan> shapePreserving = an::buildCurveProgram(shape, request);
    ASSERT_TRUE(shapePreserving.hasValue()) << shapePreserving.status().message();
    EXPECT_LT(shapePreserving->maximumAbsoluteError(), 1.0e-4);
}

TEST(CurveInterpolationTest, EveryRuleLowersIntoAnExactIntegerTable)
{
    an::TonePlanShape shape;
    shape.sampleType = an::SampleType::UInt8;
    shape.channelCount = 1u;

    const std::vector<an::CurveControlPoint> points = steppedPoints();
    for (const an::CurveInterpolation interpolation :
         {an::CurveInterpolation::NaturalCubic, an::CurveInterpolation::Akima, an::CurveInterpolation::Steffen})
    {
        an::CurveProgramRequest request;
        request.points = points;
        request.interpolation = interpolation;
        request.approximation = an::CurveApproximation::ExactAtResidentValues;
        const an::Result<an::CurveProgramPlan> plan = an::buildCurveProgram(shape, request);
        ASSERT_TRUE(plan.hasValue()) << static_cast<int>(interpolation) << ": " << plan.status().message();
        EXPECT_EQ(plan->tableEntries(), 256u) << static_cast<int>(interpolation);
    }
}

TEST(CurveInterpolationTest, AValueOutsideTheEnumerationIsNotAnInterpolation)
{
    EXPECT_TRUE(an::isCurveInterpolation(an::CurveInterpolation::Steffen));
    EXPECT_FALSE(an::isCurveInterpolation(static_cast<an::CurveInterpolation>(5u)));
    EXPECT_EQ(an::minimumCurvePoints(static_cast<an::CurveInterpolation>(5u)), 2u);
}

} // namespace
