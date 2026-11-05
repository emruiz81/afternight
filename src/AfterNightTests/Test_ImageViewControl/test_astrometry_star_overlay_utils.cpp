// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "controls/AstrometryStarOverlayUtils.h"

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>

#include <limits>

namespace
{

an::DetectedStar makeStar(const double x, const double y, const double fwhmX, const double fwhmY,
                          const double angle = 0.0)
{
    an::DetectedStar star;
    star.positionX = x;
    star.positionY = y;
    star.psf.fullWidthX = fwhmX;
    star.psf.fullWidthY = fwhmY;
    star.psf.angleDegrees = angle;
    return star;
}

} // namespace

TEST(AstrometryStarOverlayUtils, ProjectEllipseUsesScaledCenterAndPsfShape)
{
    // The star's position is an engine pixel coordinate - the first pixel's centre at one half - so it scales
    // straight onto the widget.
    const auto ellipse =
        an::ui::detail::projectAstrometryStarOverlayEllipse(makeStar(10.5, 20.25, 3.0, 1.5, 27.0), 2.0);

    EXPECT_TRUE(ellipse.isValid());
    EXPECT_DOUBLE_EQ(ellipse.center.x(), 21.0);
    EXPECT_DOUBLE_EQ(ellipse.center.y(), 40.5);
    EXPECT_DOUBLE_EQ(ellipse.semiMajorRadius, 12.0);
    EXPECT_DOUBLE_EQ(ellipse.axisRatio, 0.5);
    EXPECT_DOUBLE_EQ(ellipse.rotationDegrees, 63.0);
}

TEST(AstrometryStarOverlayUtils, ProjectEllipseFallsBackToUnitShapeForInvalidFwhm)
{
    const auto ellipse = an::ui::detail::projectAstrometryStarOverlayEllipse(makeStar(5.0, 7.0, 0.0, -2.0), 1.5);

    EXPECT_TRUE(ellipse.isValid());
    EXPECT_DOUBLE_EQ(ellipse.center.x(), 7.5);
    EXPECT_DOUBLE_EQ(ellipse.center.y(), 10.5);
    EXPECT_DOUBLE_EQ(ellipse.semiMajorRadius, 3.0);
    EXPECT_DOUBLE_EQ(ellipse.axisRatio, 1.0);
}

TEST(AstrometryStarOverlayUtils, ProjectEllipseRejectsZeroScaleFactor)
{
    const auto ellipse = an::ui::detail::projectAstrometryStarOverlayEllipse(makeStar(10.0, 20.0, 3.0, 2.0), 0.0);

    EXPECT_FALSE(ellipse.isValid());
}

TEST(AstrometryStarOverlayUtils, ProjectEllipseRejectsNegativeScaleFactor)
{
    const auto ellipse = an::ui::detail::projectAstrometryStarOverlayEllipse(makeStar(10.0, 20.0, 3.0, 2.0), -1.0);

    EXPECT_FALSE(ellipse.isValid());
}

TEST(AstrometryStarOverlayUtils, ProjectEllipseRejectsNaNScaleFactor)
{
    const auto ellipse = an::ui::detail::projectAstrometryStarOverlayEllipse(makeStar(10.0, 20.0, 3.0, 2.0),
                                                                             std::numeric_limits<double>::quiet_NaN());

    EXPECT_FALSE(ellipse.isValid());
}
