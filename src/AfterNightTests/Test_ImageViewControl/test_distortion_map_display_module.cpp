// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "controls/DistortionMapDisplayModule.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>

#include <cmath>
#include <optional>

namespace
{

using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::solutionOf;
using an::tests::astrometry::tangentPlaneCards;

/// A north-up solution of 1000 by 800 pixels about (120, 0), with a second-order forward polynomial when asked for.
std::optional<an::WcsSolution> makeSolution(const bool sip)
{
    return solutionOf(tangentPlaneCards(northUpPlane(500.0, 400.0, 120.0, 0.0, 0.01), sip), 1000u, 800u);
}

} // namespace

TEST(DistortionMapDisplayModule, DistortionDisplayModuleDrawsNothingWithoutSip)
{
    const std::optional<an::WcsSolution> wcs = makeSolution(false);
    ASSERT_TRUE(wcs.has_value());
    EXPECT_FALSE(wcs->distortion().hasForwardModel());

    const QVector<an::ui::DistortionVectorSample> samples =
        an::ui::detail::sampleDistortionVectors(wcs->distortion(), 1000, 800, 8, 8);
    EXPECT_TRUE(samples.isEmpty());
}

TEST(DistortionMapDisplayModule, DistortionVectorsMatchSipEvaluation)
{
    const std::optional<an::WcsSolution> wcs = makeSolution(true);
    ASSERT_TRUE(wcs.has_value());
    ASSERT_TRUE(wcs->distortion().hasForwardModel());

    const QVector<an::ui::DistortionVectorSample> samples =
        an::ui::detail::sampleDistortionVectors(wcs->distortion(), 1000, 800, 5, 4);
    ASSERT_EQ(samples.size(), 20);

    for (const an::ui::DistortionVectorSample& sample : samples)
    {
        const an::Result<an::PixelCoordinate> corrected = an::applySipForward(
            wcs->distortion(), an::PixelCoordinate{.x = sample.sourceImagePoint.x(), .y = sample.sourceImagePoint.y()});
        ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
        EXPECT_NEAR(sample.correctedImagePoint.x(), corrected->x, 1.0e-9);
        EXPECT_NEAR(sample.correctedImagePoint.y(), corrected->y, 1.0e-9);

        const double expected_magnitude =
            std::hypot(corrected->x - sample.sourceImagePoint.x(), corrected->y - sample.sourceImagePoint.y());
        EXPECT_NEAR(sample.magnitudePixels, expected_magnitude, 1.0e-12);
    }
}

TEST(DistortionMapDisplayModule, TheGridSpansTheFirstPixelCentreToTheLast)
{
    const std::optional<an::WcsSolution> wcs = makeSolution(true);
    ASSERT_TRUE(wcs.has_value());
    const QVector<an::ui::DistortionVectorSample> samples =
        an::ui::detail::sampleDistortionVectors(wcs->distortion(), 1000, 800, 5, 4);
    ASSERT_EQ(samples.size(), 20);
    EXPECT_DOUBLE_EQ(samples.front().sourceImagePoint.x(), 0.5);
    EXPECT_DOUBLE_EQ(samples.front().sourceImagePoint.y(), 0.5);
    EXPECT_DOUBLE_EQ(samples.back().sourceImagePoint.x(), 999.5);
    EXPECT_DOUBLE_EQ(samples.back().sourceImagePoint.y(), 799.5);
}

TEST(DistortionMapDisplayModule, SmallGridClampedToMinimumTwoByTwo)
{
    const std::optional<an::WcsSolution> wcs = makeSolution(true);
    ASSERT_TRUE(wcs.has_value());

    const QVector<an::ui::DistortionVectorSample> samples =
        an::ui::detail::sampleDistortionVectors(wcs->distortion(), 1000, 800, 1, 1);
    EXPECT_EQ(samples.size(), 4);
}

TEST(DistortionMapDisplayModule, ZeroSizeImageReturnsNoSamples)
{
    const std::optional<an::WcsSolution> wcs = makeSolution(true);
    ASSERT_TRUE(wcs.has_value());

    EXPECT_TRUE(an::ui::detail::sampleDistortionVectors(wcs->distortion(), 0, 800, 5, 5).isEmpty());
    EXPECT_TRUE(an::ui::detail::sampleDistortionVectors(wcs->distortion(), 1000, 0, 5, 5).isEmpty());
    EXPECT_TRUE(an::ui::detail::sampleDistortionVectors(wcs->distortion(), 0, 0, 5, 5).isEmpty());
}
