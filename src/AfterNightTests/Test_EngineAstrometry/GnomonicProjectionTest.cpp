// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Astrometry/GnomonicProjection.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace
{

TEST(GnomonicProjectionTest, TheTangentPointIsExactlyThePlaneOrigin)
{
    const an::SkyCoordinate tangent{.rightAscensionDegrees = 83.63, .declinationDegrees = 22.01};
    const an::Result<an::GnomonicCoordinate> projected = an::projectGnomonic(tangent, tangent);
    ASSERT_TRUE(projected.hasValue()) << projected.status().message();
    EXPECT_DOUBLE_EQ(projected->xDegrees, 0.0);
    EXPECT_DOUBLE_EQ(projected->yDegrees, 0.0);

    const an::Result<an::SkyCoordinate> restored = an::inverseGnomonic(*projected, tangent);
    ASSERT_TRUE(restored.hasValue()) << restored.status().message();
    EXPECT_EQ(*restored, tangent);
}

TEST(GnomonicProjectionTest, EastAndNorthUseAngularTangentPlaneDegrees)
{
    constexpr double kOneRadianInDegrees = 180.0 / std::numbers::pi;
    const an::SkyCoordinate tangent{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};

    const an::Result<an::GnomonicCoordinate> east =
        an::projectGnomonic(an::SkyCoordinate{.rightAscensionDegrees = 45.0, .declinationDegrees = 0.0}, tangent);
    ASSERT_TRUE(east.hasValue()) << east.status().message();
    EXPECT_NEAR(east->xDegrees, kOneRadianInDegrees, 1.0e-13);
    EXPECT_NEAR(east->yDegrees, 0.0, 1.0e-13);

    const an::Result<an::GnomonicCoordinate> north =
        an::projectGnomonic(an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 45.0}, tangent);
    ASSERT_TRUE(north.hasValue()) << north.status().message();
    EXPECT_NEAR(north->xDegrees, 0.0, 1.0e-13);
    EXPECT_NEAR(north->yDegrees, kOneRadianInDegrees, 1.0e-13);
}

TEST(GnomonicProjectionTest, PlaneRoundTripsAcrossWrapAndPolarTangentPoints)
{
    const std::array<an::SkyCoordinate, 4u> tangentPoints{
        an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0},
        an::SkyCoordinate{.rightAscensionDegrees = 359.9, .declinationDegrees = -35.0},
        an::SkyCoordinate{.rightAscensionDegrees = 46.0, .declinationDegrees = 89.8},
        an::SkyCoordinate{.rightAscensionDegrees = 217.0, .declinationDegrees = -89.8}};
    const std::array<an::GnomonicCoordinate, 8u> planePoints{
        an::GnomonicCoordinate{.xDegrees = -500.0, .yDegrees = 120.0},
        an::GnomonicCoordinate{.xDegrees = -20.0, .yDegrees = -30.0},
        an::GnomonicCoordinate{.xDegrees = -0.001, .yDegrees = 0.003},
        an::GnomonicCoordinate{},
        an::GnomonicCoordinate{.xDegrees = 0.000001, .yDegrees = -0.000002},
        an::GnomonicCoordinate{.xDegrees = 15.0, .yDegrees = 9.0},
        an::GnomonicCoordinate{.xDegrees = 100.0, .yDegrees = -200.0},
        an::GnomonicCoordinate{.xDegrees = 10000.0, .yDegrees = 20000.0}};

    for (const an::SkyCoordinate& tangent : tangentPoints)
    {
        for (const an::GnomonicCoordinate& plane : planePoints)
        {
            const an::Result<an::SkyCoordinate> world = an::inverseGnomonic(plane, tangent);
            ASSERT_TRUE(world.hasValue()) << world.status().message();
            const an::Result<an::GnomonicCoordinate> projected = an::projectGnomonic(*world, tangent);
            ASSERT_TRUE(projected.hasValue()) << projected.status().message();

            const double scale = (std::max)({1.0, std::abs(plane.xDegrees), std::abs(plane.yDegrees)});
            EXPECT_NEAR(projected->xDegrees, plane.xDegrees, 3.0e-10 * scale);
            EXPECT_NEAR(projected->yDegrees, plane.yDegrees, 3.0e-10 * scale);
        }
    }
}

TEST(GnomonicProjectionTest, WorldRoundTripsAtTheRightAscensionSeamAndNearBothPoles)
{
    const an::SkyCoordinate tangent{.rightAscensionDegrees = 359.0, .declinationDegrees = 72.0};
    const std::array<an::SkyCoordinate, 6u> directions{
        an::SkyCoordinate{.rightAscensionDegrees = 1.0, .declinationDegrees = 71.5},
        an::SkyCoordinate{.rightAscensionDegrees = 320.0, .declinationDegrees = 89.999},
        an::SkyCoordinate{.rightAscensionDegrees = 40.0, .declinationDegrees = 80.0},
        an::SkyCoordinate{.rightAscensionDegrees = 359.000001, .declinationDegrees = 72.000001},
        an::SkyCoordinate{.rightAscensionDegrees = 200.0, .declinationDegrees = 88.0},
        an::SkyCoordinate{.rightAscensionDegrees = 179.0, .declinationDegrees = 89.0}};

    for (const an::SkyCoordinate& direction : directions)
    {
        ASSERT_LT(an::angularSeparationDegrees(direction, tangent), 90.0);
        const an::Result<an::GnomonicCoordinate> plane = an::projectGnomonic(direction, tangent);
        ASSERT_TRUE(plane.hasValue()) << plane.status().message();
        const an::Result<an::SkyCoordinate> restored = an::inverseGnomonic(*plane, tangent);
        ASSERT_TRUE(restored.hasValue()) << restored.status().message();
        EXPECT_LT(an::angularSeparationDegrees(direction, *restored), 2.0e-7);
    }
}

TEST(GnomonicProjectionTest, TheTangentHorizonAndItsBackHemisphereAreRefused)
{
    const an::SkyCoordinate tangent{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
    EXPECT_EQ(an::projectGnomonic(an::SkyCoordinate{.rightAscensionDegrees = 90.0, .declinationDegrees = 0.0}, tangent)
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::projectGnomonic(an::SkyCoordinate{.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}, tangent)
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);

    const an::SkyCoordinate tiltedTangent{.rightAscensionDegrees = 17.0, .declinationDegrees = 30.0};
    EXPECT_EQ(an::projectGnomonic(an::SkyCoordinate{.rightAscensionDegrees = 17.0, .declinationDegrees = -60.0},
                                  tiltedTangent)
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);

    const an::Result<an::GnomonicCoordinate> nearHorizon =
        an::projectGnomonic(an::SkyCoordinate{.rightAscensionDegrees = 89.999, .declinationDegrees = 0.0}, tangent);
    ASSERT_TRUE(nearHorizon.hasValue()) << nearHorizon.status().message();
    EXPECT_GT(nearHorizon->xDegrees, 3000000.0);
}

TEST(GnomonicProjectionTest, InvalidDirectionsAndPlaneValuesAreReturnedFailures)
{
    const an::SkyCoordinate tangent{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
    EXPECT_EQ(an::projectGnomonic(an::SkyCoordinate{.rightAscensionDegrees = 360.0, .declinationDegrees = 0.0}, tangent)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::projectGnomonic(an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 91.0}, tangent)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::inverseGnomonic(an::GnomonicCoordinate{.xDegrees = std::numeric_limits<double>::quiet_NaN()}, tangent)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::inverseGnomonic(an::GnomonicCoordinate{}, an::SkyCoordinate{.rightAscensionDegrees = -1.0}).status().code(),
        an::ErrorCode::InvalidArgument);
}

TEST(GnomonicProjectionTest, TheScaledInverseAcceptsEveryFinitePlaneMagnitude)
{
    const double largest = std::numeric_limits<double>::max();
    const an::Result<an::SkyCoordinate> direction =
        an::inverseGnomonic(an::GnomonicCoordinate{.xDegrees = largest, .yDegrees = -largest},
                            an::SkyCoordinate{.rightAscensionDegrees = 12.0, .declinationDegrees = -34.0});
    ASSERT_TRUE(direction.hasValue()) << direction.status().message();
    EXPECT_TRUE(an::isSkyCoordinate(*direction));
}

} // namespace
