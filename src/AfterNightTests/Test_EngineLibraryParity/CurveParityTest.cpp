// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The three curve interpolations against the numerical library that defines them.
//
// A person who draws a curve with the "cubic", "akima" or "steffen" mode expects that library's
// implementation of those rules, so the question this sweep answers is not whether this engine's
// curve is a reasonable cubic but whether it is the same cubic.
//
// The two sides evaluate the same piecewise polynomial in two algebraically equal forms: the library
// holds each segment as `y + dx*(b + dx*(c + dx*d))` and this engine holds it as a Hermite basis
// over the segment's two end derivatives. A cubic is determined by those four numbers, so the two
// differ only in the rounding of two polynomial evaluations, and the tolerance is that rounding
// times the number of terms - not a number chosen to pass. Anything the derivative rule itself got
// wrong is orders of magnitude larger than it and lands as a failure.
//
// The degenerate case is in the sweep on purpose. Where Akima's weight denominator vanishes that
// library makes the whole segment a straight line at its own secant, and the derivative it leaves at
// the shared node need not be the next segment's - so a per-node tangent could not reproduce it, and
// the "collinear run" case is the one that would catch a reimplementation that tried.

#include "ParityHarness.h"

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <gsl/gsl_errno.h>
#include <gsl/gsl_interp.h>
#include <gsl/gsl_spline.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace
{

using namespace parity_test;

constexpr const char* kDomain = "curve";

/// One named set of control points.
struct CurveCase
{
    /// What the case is called in the printed line.
    const char* label = "";

    /// The control points, with strictly increasing inputs and outputs inside [0, 1].
    std::vector<an::CurveControlPoint> points;
};

/// The cases the sweep runs, chosen for what each one can break.
///
/// @return the cases.
[[nodiscard]] std::vector<CurveCase> curveCases()
{
    std::vector<CurveCase> cases;
    cases.push_back({"identity", {{0.0, 0.0}, {0.2, 0.2}, {0.4, 0.4}, {0.6, 0.6}, {0.8, 0.8}, {1.0, 1.0}}});
    cases.push_back({"s-curve", {{0.0, 0.0}, {0.25, 0.12}, {0.5, 0.5}, {0.75, 0.88}, {1.0, 1.0}}});
    cases.push_back({"step", {{0.0, 0.0}, {0.25, 0.05}, {0.5, 0.1}, {0.75, 0.9}, {1.0, 1.0}}});
    cases.push_back({"uneven-spacing", {{0.0, 0.1}, {0.03, 0.4}, {0.31, 0.42}, {0.33, 0.9}, {0.9, 0.95}, {1.0, 0.2}}});
    // Three equal slopes then two more equal ones: the weight denominator vanishes on the later
    // segment and not on the earlier, which is where the two derivatives at one node part.
    cases.push_back({"collinear-run", {{0.0, 0.0}, {0.2, 0.1}, {0.4, 0.2}, {0.6, 0.3}, {0.8, 0.7}, {1.0, 1.0}}});
    cases.push_back({"flat-then-rise", {{0.0, 0.5}, {0.2, 0.5}, {0.4, 0.5}, {0.6, 0.5}, {0.8, 0.8}, {1.0, 1.0}}});
    return cases;
}

/// The library's own values for one rule over one set of points.
///
/// @param type the library's interpolation type.
/// @param points the control points.
/// @param inputs where to evaluate.
/// @return the values, as long as `inputs`.
[[nodiscard]] std::vector<float> libraryValues(const gsl_interp_type* type,
                                               const std::vector<an::CurveControlPoint>& points,
                                               const std::vector<double>& inputs)
{
    std::vector<double> x(points.size(), 0.0);
    std::vector<double> y(points.size(), 0.0);
    for (std::size_t index = 0u; index < points.size(); ++index)
    {
        x[index] = points[index].input;
        y[index] = points[index].output;
    }
    std::vector<float> values(inputs.size(), 0.0f);
    gsl_spline* const spline = gsl_spline_alloc(type, points.size());
    EXPECT_NE(spline, nullptr);
    if (spline == nullptr)
    {
        return values;
    }
    gsl_interp_accel* const accelerator = gsl_interp_accel_alloc();
    EXPECT_EQ(gsl_spline_init(spline, x.data(), y.data(), points.size()), GSL_SUCCESS);
    for (std::size_t index = 0u; index < inputs.size(); ++index)
    {
        values[index] = static_cast<float>(gsl_spline_eval(spline, inputs[index], accelerator));
    }
    gsl_interp_accel_free(accelerator);
    gsl_spline_free(spline);
    return values;
}

/// This engine's own values for one rule over one set of points.
///
/// @param interpolation which rule.
/// @param points the control points.
/// @param inputs where to evaluate.
/// @return the values, as long as `inputs`.
[[nodiscard]] std::vector<float> engineValues(const an::CurveInterpolation interpolation,
                                              const std::vector<an::CurveControlPoint>& points,
                                              const std::vector<double>& inputs)
{
    std::vector<float> values(inputs.size(), 0.0f);
    for (std::size_t index = 0u; index < inputs.size(); ++index)
    {
        const an::Result<double> value =
            an::evaluateCurve(points, interpolation, an::CurveDomainPolicy::Clamp, inputs[index]);
        EXPECT_TRUE(value.hasValue()) << value.status().message();
        values[index] = value.hasValue() ? static_cast<float>(*value) : 0.0f;
    }
    return values;
}

/// Where to evaluate a curve: every control point, and a fine sweep between the ends.
///
/// @param points the control points.
/// @return the inputs.
[[nodiscard]] std::vector<double> sampleInputs(const std::vector<an::CurveControlPoint>& points)
{
    std::vector<double> inputs;
    const double low = points.front().input;
    const double high = points.back().input;
    for (int step = 0; step <= 1000; ++step)
    {
        inputs.push_back(low + (high - low) * static_cast<double>(step) / 1000.0);
    }
    for (const an::CurveControlPoint& point : points)
    {
        inputs.push_back(point.input);
    }
    return inputs;
}

TEST(CurveParityTest, TheThreeRulesAgreeWithTheNumericalLibraryToTheRoundingOfTwoPolynomialForms)
{
    struct Rule
    {
        an::CurveInterpolation interpolation;
        const gsl_interp_type* type;
        const char* name;
        std::size_t minimumPoints;
    };
    const Rule rules[] = {
        {an::CurveInterpolation::NaturalCubic, gsl_interp_cspline, "cubic", an::kMinimumNaturalCubicCurvePoints},
        {an::CurveInterpolation::Akima, gsl_interp_akima, "akima", an::kMinimumAkimaCurvePoints},
        {an::CurveInterpolation::Steffen, gsl_interp_steffen, "steffen", an::kMinimumSteffenCurvePoints},
    };

    // Two evaluations of the same cubic in different orders: a handful of roundings of a value whose
    // magnitude is at most one, taken at single precision because that is what the comparison holds.
    constexpr double kFormRounding = 8.0e-7;

    for (const CurveCase& one : curveCases())
    {
        const std::vector<double> inputs = sampleInputs(one.points);
        for (const Rule& rule : rules)
        {
            if (one.points.size() < rule.minimumPoints)
            {
                continue;
            }
            const std::vector<float> engine = engineValues(rule.interpolation, one.points, inputs);
            const std::vector<float> library = libraryValues(rule.type, one.points, inputs);
            expectParity(kDomain, std::string(rule.name) + " | " + one.label, engine, library, kFormRounding);
        }
    }
}

TEST(CurveParityTest, AkimaKeepsTheLibrarysStraightSegmentRatherThanASmoothedOne)
{
    // The specific disagreement a per-node derivative would produce: on the collinear run, the
    // library's segment is straight and the node it ends at carries a different derivative on each
    // side. Evaluating densely inside that one segment is where a reimplementation parts company.
    const std::vector<an::CurveControlPoint> points{{0.0, 0.0}, {0.2, 0.1}, {0.4, 0.2},
                                                    {0.6, 0.3}, {0.8, 0.7}, {1.0, 1.0}};
    std::vector<double> inputs;
    for (int step = 0; step <= 500; ++step)
    {
        inputs.push_back(0.4 + 0.2 * static_cast<double>(step) / 500.0);
    }
    const std::vector<float> engine = engineValues(an::CurveInterpolation::Akima, points, inputs);
    const std::vector<float> library = libraryValues(gsl_interp_akima, points, inputs);
    const Comparison found = expectParity(kDomain, "akima | straight-segment", engine, library, 8.0e-7);
    // The segment really is the straight one, so the case is testing what it claims to.
    EXPECT_NEAR(library.front(), 0.2f, 1.0e-6f);
    EXPECT_NEAR(library.back(), 0.3f, 1.0e-6f);
    EXPECT_NEAR(library[found.count / 2u], 0.25f, 1.0e-6f);
}

} // namespace
