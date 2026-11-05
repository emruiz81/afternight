// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "controls/CoordinateGridDisplayModule.h"

#include <cmath>
#include <limits>
#include <optional>

namespace
{

using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::solutionOf;
using an::tests::astrometry::tangentPlaneCards;

/// A north-up solution of 1000 by 800 pixels of 0.01 degrees about a reference at CRPIX (500, 400) - the engine's
/// (499.5, 399.5).
std::optional<an::WcsSolution> makeSolution(const double ra, const double dec, const double degreesPerPixel = 0.01)
{
    return solutionOf(tangentPlaneCards(northUpPlane(500.0, 400.0, ra, dec, degreesPerPixel)), 1000u, 800u);
}

} // namespace

// --- Grid spacing tests ---

TEST(CoordinateGridDisplayModule, GridSpacingAdaptsToFov)
{
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(15.0), 2.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(5.0), 30.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(1.0), 5.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(10.0 / 60.0), 1.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(2.0 / 60.0), 10.0 / 3600.0);
}

TEST(CoordinateGridDisplayModule, GridSpacingAtBoundaryValues)
{
    // Exactly at boundary values
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(10.0), 30.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(2.0), 5.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(30.0 / 60.0), 1.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(5.0 / 60.0), 10.0 / 3600.0);
}

TEST(CoordinateGridDisplayModule, GridSpacingForInvalidInputs)
{
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(-1.0), 1.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(0.0), 1.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(std::numeric_limits<double>::quiet_NaN()),
                     1.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(std::numeric_limits<double>::infinity()),
                     1.0 / 60.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::chooseCoordinateGridSpacingDegrees(-std::numeric_limits<double>::infinity()),
                     1.0 / 60.0);
}

// --- Grid projection tests ---

TEST(CoordinateGridDisplayModule, GridLinesProjectCorrectlyForKnownWcs)
{
    const std::optional<an::WcsSolution> wcs = makeSolution(120.0, 0.0);
    ASSERT_TRUE(wcs.has_value());

    // The reference meridian is the column through the reference pixel's centre, at 499.5.
    const auto ra_line = an::ui::detail::projectCoordinateGridLine(*wcs, an::ui::CoordinateGridAxis::RightAscension,
                                                                   120.0, -1.0, 1.0, 1.0, 8);
    ASSERT_GE(ra_line.points.size(), 3);
    for (const QPointF& point : ra_line.points)
    {
        EXPECT_NEAR(point.x(), 499.5, 1e-6);
    }
    EXPECT_LT(ra_line.points.front().y(), ra_line.points.back().y());

    // The equator through the reference is the row through its centre, at 399.5 - 799 at a scale of two.
    const auto dec_line = an::ui::detail::projectCoordinateGridLine(*wcs, an::ui::CoordinateGridAxis::Declination, 0.0,
                                                                    119.0, 121.0, 2.0, 8);
    ASSERT_GE(dec_line.points.size(), 3);
    for (const QPointF& point : dec_line.points)
    {
        EXPECT_NEAR(point.y(), 799.0, 1e-6);
    }
    EXPECT_GT(dec_line.points.front().x(), dec_line.points.back().x());
}

TEST(CoordinateGridDisplayModule, GridLinesAtRaZeroCrossing)
{
    // WCS centered near RA=1 deg so grid lines straddle RA=0/360
    const std::optional<an::WcsSolution> wcs = makeSolution(1.0, 10.0);
    ASSERT_TRUE(wcs.has_value());

    // Project a line at RA=0 (which is the same as RA=360)
    const auto ra_line = an::ui::detail::projectCoordinateGridLine(*wcs, an::ui::CoordinateGridAxis::RightAscension,
                                                                   0.0, 8.0, 12.0, 1.0, 16);
    ASSERT_GE(ra_line.points.size(), 3);

    // Also project with RA=360 — should yield the same normalized coordinate
    const auto ra_line_360 = an::ui::detail::projectCoordinateGridLine(*wcs, an::ui::CoordinateGridAxis::RightAscension,
                                                                       360.0, 8.0, 12.0, 1.0, 16);
    EXPECT_DOUBLE_EQ(ra_line.coordinateDegrees, ra_line_360.coordinateDegrees);
    ASSERT_EQ(ra_line.points.size(), ra_line_360.points.size());
    for (qsizetype index = 0; index < ra_line.points.size(); ++index)
    {
        EXPECT_EQ(ra_line.points[index], ra_line_360.points[index]);
    }
}

TEST(CoordinateGridDisplayModule, GridLinesAtHighDeclination)
{
    // WCS centered at Dec=+85 — RA lines should converge but not produce NaN
    const std::optional<an::WcsSolution> wcs = makeSolution(180.0, 85.0, 0.001);
    ASSERT_TRUE(wcs.has_value());

    const auto ra_line = an::ui::detail::projectCoordinateGridLine(*wcs, an::ui::CoordinateGridAxis::RightAscension,
                                                                   180.0, 84.0, 86.0, 1.0, 16);
    ASSERT_GE(ra_line.points.size(), 3);
    for (const QPointF& point : ra_line.points)
    {
        EXPECT_TRUE(std::isfinite(point.x()));
        EXPECT_TRUE(std::isfinite(point.y()));
    }
}

TEST(CoordinateGridDisplayModule, ALineBehindTheProjectionsHorizonKeepsOnlyItsVisibleSamples)
{
    // The gnomonic projection places only the hemisphere about its reference; the samples of a meridian on the far
    // side are left out rather than drawn mirrored.
    const std::optional<an::WcsSolution> wcs = makeSolution(120.0, 0.0);
    ASSERT_TRUE(wcs.has_value());
    const auto behind = an::ui::detail::projectCoordinateGridLine(*wcs, an::ui::CoordinateGridAxis::RightAscension,
                                                                  300.0, -10.0, 10.0, 1.0, 8);
    EXPECT_TRUE(behind.points.isEmpty());
}

// --- Label formatting tests ---

TEST(CoordinateGridDisplayModule, FormatRaLabelAtZero)
{
    EXPECT_EQ(an::ui::detail::formatCoordinateGridRaLabel(0.0, 2.0), QStringLiteral("00h 00m"));
}

TEST(CoordinateGridDisplayModule, FormatRaLabelAt180Degrees)
{
    // 180 deg = 12h
    EXPECT_EQ(an::ui::detail::formatCoordinateGridRaLabel(180.0, 2.0), QStringLiteral("12h 00m"));
}

TEST(CoordinateGridDisplayModule, FormatRaLabelWithSeconds)
{
    // 187.5 deg = 12h 30m, at fine spacing that shows seconds
    EXPECT_EQ(an::ui::detail::formatCoordinateGridRaLabel(187.5, 1.0 / 60.0), QStringLiteral("12h 30m 00.0s"));
}

TEST(CoordinateGridDisplayModule, FormatRaLabelNegativeWraps)
{
    // -15 deg wraps to 345 deg = 23h
    EXPECT_EQ(an::ui::detail::formatCoordinateGridRaLabel(-15.0, 2.0), QStringLiteral("23h 00m"));
}

TEST(CoordinateGridDisplayModule, FormatRaLabelRoundsOnceIntoEveryField)
{
    // 12h 29m 59.96s at a tenth of a second is 12h 30m 00.0s, never 29m 60.0s.
    EXPECT_EQ(an::ui::detail::formatCoordinateGridRaLabel((12.0 + 29.0 / 60.0 + 59.96 / 3600.0) * 15.0, 1.0 / 60.0),
              QStringLiteral("12h 30m 00.0s"));
}

TEST(CoordinateGridDisplayModule, FormatDecLabelPositive)
{
    EXPECT_EQ(an::ui::detail::formatCoordinateGridDecLabel(45.5, 2.0), QStringLiteral("+45° 30'"));
}

TEST(CoordinateGridDisplayModule, FormatDecLabelNegative)
{
    EXPECT_EQ(an::ui::detail::formatCoordinateGridDecLabel(-45.5, 2.0), QStringLiteral("-45° 30'"));
}

TEST(CoordinateGridDisplayModule, FormatDecLabelZero)
{
    EXPECT_EQ(an::ui::detail::formatCoordinateGridDecLabel(0.0, 2.0), QStringLiteral("+00° 00'"));
}

TEST(CoordinateGridDisplayModule, FormatDecLabelWithSeconds)
{
    EXPECT_EQ(an::ui::detail::formatCoordinateGridDecLabel(45.0 + 30.0 / 60.0 + 15.0 / 3600.0, 1.0 / 60.0),
              QStringLiteral("+45° 30' 15.0\""));
}

TEST(CoordinateGridDisplayModule, FormatDecLabelClampsAt90)
{
    EXPECT_EQ(an::ui::detail::formatCoordinateGridDecLabel(95.0, 2.0), QStringLiteral("+90° 00'"));
}

TEST(CoordinateGridDisplayModule, FormatDecLabelClampsAtMinus90)
{
    EXPECT_EQ(an::ui::detail::formatCoordinateGridDecLabel(-95.0, 2.0), QStringLiteral("-90° 00'"));
}

TEST(CoordinateGridDisplayModule, FormatDecLabelNaN)
{
    EXPECT_EQ(an::ui::detail::formatCoordinateGridDecLabel(std::numeric_limits<double>::quiet_NaN(), 2.0),
              QStringLiteral("+00° 00'"));
}

// --- Angular distance tests ---

TEST(CoordinateGridDisplayModule, ShortestAngularDistanceSimple)
{
    EXPECT_NEAR(an::ui::detail::shortestAngularDistanceDegrees(10.0, 20.0), 10.0, 1e-10);
    EXPECT_NEAR(an::ui::detail::shortestAngularDistanceDegrees(20.0, 10.0), -10.0, 1e-10);
}

TEST(CoordinateGridDisplayModule, ShortestAngularDistanceAcrossZero)
{
    EXPECT_NEAR(an::ui::detail::shortestAngularDistanceDegrees(350.0, 10.0), 20.0, 1e-10);
    EXPECT_NEAR(an::ui::detail::shortestAngularDistanceDegrees(10.0, 350.0), -20.0, 1e-10);
}
