// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the typed world-coordinate model and the transforms that move it.
//
// Two families of case. The first pins the derived numbers - scale, orientation, parity, field of
// view - against a matrix written by hand, because those are the numbers a user reads, and read from
// a third-party library's internals they can disagree with the distortion path. The second checks
// that a transform of the solution and the same
// transform of the pixels agree: a crop that moves the reference pixel the wrong way still projects,
// still looks smooth, and puts every star one crop-offset from where it is.

#include "CatalogTestSupport.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

namespace
{

/// The plate scale of the worked example, in degrees per pixel.
///
/// Two point two arcseconds a pixel: a short refractor with a modern sensor, and the scale of the
/// solved frames the real headers below came from.
constexpr double kScaleDegrees = 2.2 / 3600.0;

/// A north-up, east-left solution of a 2048 by 1536 frame.
///
/// The ordinary orientation: right ascension decreases with the column index, declination increases
/// with the row index, so the matrix determinant is negative and the sky is not mirrored.
[[nodiscard]] an::WcsSolution northUpSolution(const an::SipDistortion& distortion = {})
{
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 1024.0, .y = 768.0};
    request.referencePoint = {.rightAscensionDegrees = 83.822083, .declinationDegrees = -5.391111};
    request.linear = {.cd11 = -kScaleDegrees, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScaleDegrees};
    request.projection = an::WcsProjection::Gnomonic;
    request.imageWidth = 2048u;
    request.imageHeight = 1536u;
    request.distortion = distortion;
    const an::Result<an::WcsSolution> solution = an::WcsSolution::create(request);
    EXPECT_TRUE(solution.hasValue()) << solution.status().message();
    // A solution has no absent state, so a helper that could not build one has nothing to return;
    // `value()` throws and the case fails loudly rather than testing a substitute.
    return solution.value();
}

/// A second-order forward-only distortion centred on the frame's reference pixel.
[[nodiscard]] an::SipDistortion frameDistortion()
{
    an::SipDistortionRequest request;
    request.referencePixel = {.x = 1024.0, .y = 768.0};
    request.forwardX.order = 2u;
    request.forwardX.coefficients[2][0] = 5.0e-7;
    request.forwardX.coefficients[0][2] = -2.0e-7;
    request.forwardY.order = 2u;
    request.forwardY.coefficients[1][1] = 3.0e-7;
    const an::Result<an::SipDistortion> distortion = an::SipDistortion::create(request);
    EXPECT_TRUE(distortion.hasValue()) << distortion.status().message();
    return distortion.hasValue() ? *distortion : an::SipDistortion{};
}

TEST(WcsSolutionTest, TheProjectionIsStatedAndAnUnknownOneIsRefusedByName)
{
    static_assert(an::isWcsProjection(an::WcsProjection::Gnomonic));
    static_assert(!an::isWcsProjection(static_cast<an::WcsProjection>(42)));
    EXPECT_STREQ(an::wcsProjectionName(an::WcsProjection::Gnomonic), "Gnomonic");
    EXPECT_STREQ(an::wcsProjectionName(static_cast<an::WcsProjection>(42)), "UnknownWcsProjection");

    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 10.0, .y = 10.0};
    request.referencePoint = {.rightAscensionDegrees = 10.0, .declinationDegrees = 10.0};
    request.linear = {.cd11 = -kScaleDegrees, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScaleDegrees};
    request.imageWidth = 20u;
    request.imageHeight = 20u;
    request.projection = static_cast<an::WcsProjection>(42);
    EXPECT_EQ(an::WcsSolution::create(request).code(), an::ErrorCode::Unsupported);
}

TEST(WcsSolutionTest, ASolutionRefusesEveryFieldThatCannotDescribeAFrame)
{
    an::WcsSolutionRequest valid;
    valid.referencePixel = {.x = 10.0, .y = 10.0};
    valid.referencePoint = {.rightAscensionDegrees = 10.0, .declinationDegrees = 10.0};
    valid.linear = {.cd11 = -kScaleDegrees, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScaleDegrees};
    valid.imageWidth = 20u;
    valid.imageHeight = 20u;
    ASSERT_TRUE(an::WcsSolution::create(valid).hasValue());

    an::WcsSolutionRequest broken = valid;
    broken.referencePixel.x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::WcsSolution::create(broken).code(), an::ErrorCode::InvalidArgument);

    broken = valid;
    broken.referencePoint.declinationDegrees = 91.0;
    EXPECT_EQ(an::WcsSolution::create(broken).code(), an::ErrorCode::InvalidArgument);

    broken = valid;
    broken.referencePoint.rightAscensionDegrees = 360.0;
    EXPECT_EQ(an::WcsSolution::create(broken).code(), an::ErrorCode::InvalidArgument);

    broken = valid;
    broken.imageHeight = 0u;
    EXPECT_EQ(an::WcsSolution::create(broken).code(), an::ErrorCode::InvalidArgument);

    broken = valid;
    broken.linear.cd12 = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::WcsSolution::create(broken).code(), an::ErrorCode::InvalidArgument);

    // A matrix with no inverse maps the whole frame to one direction, which is data that describes
    // an impossible solution rather than an argument a caller spelled wrong.
    broken = valid;
    broken.linear = {.cd11 = kScaleDegrees, .cd12 = kScaleDegrees, .cd21 = kScaleDegrees, .cd22 = kScaleDegrees};
    EXPECT_EQ(an::WcsSolution::create(broken).code(), an::ErrorCode::InvalidData);
}

TEST(WcsSolutionTest, TheDerivedNumbersDescribeTheMatrixThatProducedThem)
{
    const an::WcsSolution solution = northUpSolution();

    EXPECT_DOUBLE_EQ(solution.pixelScaleXArcsecPerPixel(), 2.2);
    EXPECT_DOUBLE_EQ(solution.pixelScaleYArcsecPerPixel(), 2.2);
    EXPECT_DOUBLE_EQ(solution.meanPixelScaleArcsecPerPixel(), 2.2);

    // North up is zero: the orientation is the direction of increasing declination, not the angle of
    // the negative first matrix column, which would report this frame as rotated by half a turn.
    EXPECT_DOUBLE_EQ(solution.orientationDegrees(), 0.0);
    EXPECT_FALSE(solution.mirrored());

    const an::WcsFieldOfView field = solution.fieldOfViewArcmin();
    EXPECT_DOUBLE_EQ(field.widthArcmin, 2.2 * 2048.0 / 60.0);
    EXPECT_DOUBLE_EQ(field.heightArcmin, 2.2 * 1536.0 / 60.0);
}

TEST(WcsSolutionTest, OrientationTurnsWithTheMatrixAndParityFollowsItsDeterminant)
{
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 100.0, .y = 100.0};
    request.referencePoint = {.rightAscensionDegrees = 200.0, .declinationDegrees = 12.0};
    request.imageWidth = 200u;
    request.imageHeight = 200u;

    // North to the left: increasing declination runs toward the negative column direction, which is
    // a quarter turn from up in the direction this model measures.
    request.linear = {.cd11 = 0.0, .cd12 = -kScaleDegrees, .cd21 = -kScaleDegrees, .cd22 = 0.0};
    const an::Result<an::WcsSolution> northLeft = an::WcsSolution::create(request);
    ASSERT_TRUE(northLeft.hasValue()) << northLeft.status().message();
    EXPECT_DOUBLE_EQ(northLeft->orientationDegrees(), 90.0);
    EXPECT_FALSE(northLeft->mirrored());

    // The same frame with east and west exchanged: still north up, and now mirrored.
    request.linear = {.cd11 = kScaleDegrees, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScaleDegrees};
    const an::Result<an::WcsSolution> flipped = an::WcsSolution::create(request);
    ASSERT_TRUE(flipped.hasValue()) << flipped.status().message();
    EXPECT_DOUBLE_EQ(flipped->orientationDegrees(), 0.0);
    EXPECT_TRUE(flipped->mirrored());
}

TEST(WcsSolutionTest, TheReferencePixelPointsAtTheReferencePointAndTheTwoDirectionsUndoEachOther)
{
    for (const an::SipDistortion& distortion : {an::SipDistortion{}, frameDistortion()})
    {
        const an::WcsSolution solution = northUpSolution(distortion);

        const an::Result<an::SkyCoordinate> atReference = solution.pixelToWorld(solution.referencePixel());
        ASSERT_TRUE(atReference.hasValue()) << atReference.status().message();
        EXPECT_DOUBLE_EQ(atReference->rightAscensionDegrees, solution.referencePoint().rightAscensionDegrees);
        EXPECT_DOUBLE_EQ(atReference->declinationDegrees, solution.referencePoint().declinationDegrees);

        for (const an::PixelCoordinate probe :
             {an::PixelCoordinate{.x = 0.5, .y = 0.5}, an::PixelCoordinate{.x = 2047.5, .y = 1535.5},
              an::PixelCoordinate{.x = 1024.0, .y = 20.0}, an::PixelCoordinate{.x = 300.25, .y = 1200.75}})
        {
            const an::Result<an::SkyCoordinate> sky = solution.pixelToWorld(probe);
            ASSERT_TRUE(sky.hasValue()) << sky.status().message();
            const an::Result<an::PixelCoordinate> back = solution.worldToPixel(*sky);
            ASSERT_TRUE(back.hasValue()) << back.status().message();
            EXPECT_NEAR(back->x, probe.x, 1.0e-3);
            EXPECT_NEAR(back->y, probe.y, 1.0e-3);
        }
    }
}

TEST(WcsSolutionTest, OnePixelEastOfTheReferenceIsOnePlateScaleOfRightAscension)
{
    const an::WcsSolution solution = northUpSolution();
    const an::PixelCoordinate reference = solution.referencePixel();

    const an::Result<an::SkyCoordinate> left = solution.pixelToWorld({.x = reference.x - 1.0, .y = reference.y});
    ASSERT_TRUE(left.hasValue()) << left.status().message();
    const an::Result<an::SkyCoordinate> up = solution.pixelToWorld({.x = reference.x, .y = reference.y + 1.0});
    ASSERT_TRUE(up.hasValue()) << up.status().message();

    // A column step toward the left is a step east, which increases right ascension; the increase
    // is the plate scale divided by the cosine of the declination.
    const double expectedRightAscensionStep =
        kScaleDegrees / std::cos(solution.referencePoint().declinationDegrees * std::numbers::pi / 180.0);
    // The tolerance is above the projection's own second-order term over one pixel, which is what
    // separates a tangent plane from the sphere it touches.
    EXPECT_NEAR(left->rightAscensionDegrees - solution.referencePoint().rightAscensionDegrees,
                expectedRightAscensionStep, 1.0e-8);
    EXPECT_NEAR(up->declinationDegrees - solution.referencePoint().declinationDegrees, kScaleDegrees, 1.0e-8);
}

TEST(WcsSolutionTest, ADirectionBehindTheProjectionHorizonHasNoPixel)
{
    const an::WcsSolution solution = northUpSolution();
    const an::SkyCoordinate opposite{.rightAscensionDegrees = 263.822083, .declinationDegrees = 5.391111};
    EXPECT_EQ(solution.worldToPixel(opposite).code(), an::ErrorCode::OutOfRange);

    const an::SkyCoordinate broken{.rightAscensionDegrees = 400.0, .declinationDegrees = 0.0};
    EXPECT_EQ(solution.worldToPixel(broken).code(), an::ErrorCode::InvalidArgument);

    const an::PixelCoordinate nonFinite{.x = std::numeric_limits<double>::infinity(), .y = 0.0};
    EXPECT_EQ(solution.pixelToWorld(nonFinite).code(), an::ErrorCode::InvalidArgument);
}

TEST(WcsSolutionTest, TheFrameCentreAndTheQueryRadiusCoverTheWholeFrame)
{
    const an::WcsSolution solution = northUpSolution();

    const an::Result<an::SkyCoordinate> centre = solution.fieldCenter();
    ASSERT_TRUE(centre.hasValue()) << centre.status().message();

    const an::Result<double> radius = solution.queryRadiusDegrees(0.0);
    ASSERT_TRUE(radius.hasValue()) << radius.status().message();

    for (const an::PixelCoordinate corner :
         {an::PixelCoordinate{.x = 0.0, .y = 0.0}, an::PixelCoordinate{.x = 2048.0, .y = 0.0},
          an::PixelCoordinate{.x = 0.0, .y = 1536.0}, an::PixelCoordinate{.x = 2048.0, .y = 1536.0}})
    {
        const an::Result<an::SkyCoordinate> sky = solution.pixelToWorld(corner);
        ASSERT_TRUE(sky.hasValue()) << sky.status().message();
        EXPECT_LE(an::angularSeparationDegrees(*centre, *sky), *radius);
    }

    const an::Result<double> withMargin = solution.queryRadiusDegrees(60.0);
    ASSERT_TRUE(withMargin.hasValue()) << withMargin.status().message();
    EXPECT_NEAR(*withMargin - *radius, 60.0 / 3600.0, 1.0e-12);

    EXPECT_EQ(solution.queryRadiusDegrees(-1.0).code(), an::ErrorCode::InvalidArgument);
}

TEST(WcsSolutionTest, TheSummaryCarriesWhatTheFullSolutionDerives)
{
    const an::WcsSolution linear = northUpSolution();
    const an::Result<an::AstrometricSolution> plain = linear.toAstrometricSolution();
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    ASSERT_TRUE(plain->present());
    EXPECT_EQ(plain->referencePoint(), linear.referencePoint());
    EXPECT_DOUBLE_EQ(*plain->referencePixelX(), linear.referencePixel().x);
    EXPECT_DOUBLE_EQ(*plain->referencePixelY(), linear.referencePixel().y);
    EXPECT_DOUBLE_EQ(*plain->pixelScaleArcsecPerPixel(), 2.2);
    EXPECT_DOUBLE_EQ(*plain->orientationDegrees(), 0.0);
    EXPECT_FALSE(*plain->mirrored());
    EXPECT_FALSE(*plain->hasDistortion());

    const an::Result<an::AstrometricSolution> distorted = northUpSolution(frameDistortion()).toAstrometricSolution();
    ASSERT_TRUE(distorted.hasValue()) << distorted.status().message();
    EXPECT_TRUE(*distorted->hasDistortion());
}

TEST(WcsSolutionTest, DroppingTheDistortionKeepsTheLinearSolutionExactly)
{
    const an::WcsSolution distorted = northUpSolution(frameDistortion());
    const an::WcsSolution linear = distorted.withoutDistortion();

    EXPECT_FALSE(linear.distortion().hasForwardModel());
    EXPECT_EQ(linear.referencePixel(), distorted.referencePixel());
    EXPECT_EQ(linear.linear(), distorted.linear());
    EXPECT_EQ(linear, northUpSolution());
}

TEST(WcsSolutionTest, ACropMovesTheOriginAndNothingElse)
{
    const an::WcsSolution original = northUpSolution(frameDistortion());
    const an::Result<an::WcsSolution> cropped = an::cropWcsSolution(original, 300u, 200u, 512u, 384u);
    ASSERT_TRUE(cropped.hasValue()) << cropped.status().message();

    EXPECT_EQ(cropped->imageWidth(), 512u);
    EXPECT_EQ(cropped->imageHeight(), 384u);
    EXPECT_EQ(cropped->linear(), original.linear());
    EXPECT_DOUBLE_EQ(cropped->referencePixel().x, original.referencePixel().x - 300.0);
    EXPECT_DOUBLE_EQ(cropped->referencePixel().y, original.referencePixel().y - 200.0);
    EXPECT_DOUBLE_EQ(cropped->distortion().referencePixel().x, original.distortion().referencePixel().x - 300.0);

    // The kept pixel and its own new coordinate point at the same direction.
    const an::Result<an::SkyCoordinate> before = original.pixelToWorld({.x = 400.5, .y = 300.5});
    ASSERT_TRUE(before.hasValue()) << before.status().message();
    const an::Result<an::SkyCoordinate> after = cropped->pixelToWorld({.x = 100.5, .y = 100.5});
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_DOUBLE_EQ(before->rightAscensionDegrees, after->rightAscensionDegrees);
    EXPECT_DOUBLE_EQ(before->declinationDegrees, after->declinationDegrees);

    EXPECT_EQ(an::cropWcsSolution(original, 0u, 0u, 0u, 10u).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::cropWcsSolution(original, 2000u, 0u, 512u, 384u).code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::cropWcsSolution(original, 0u, 0u, 4096u, 384u).code(), an::ErrorCode::OutOfRange);
}

TEST(WcsSolutionTest, ATranslationMovesBothReferencePixelsTogether)
{
    const an::WcsSolution original = northUpSolution(frameDistortion());
    const an::Result<an::WcsSolution> moved = an::translateWcsSolution(original, 12.5, -8.0);
    ASSERT_TRUE(moved.hasValue()) << moved.status().message();

    EXPECT_DOUBLE_EQ(moved->referencePixel().x, original.referencePixel().x + 12.5);
    EXPECT_DOUBLE_EQ(moved->referencePixel().y, original.referencePixel().y - 8.0);
    EXPECT_DOUBLE_EQ(moved->distortion().referencePixel().x, original.distortion().referencePixel().x + 12.5);
    EXPECT_DOUBLE_EQ(moved->distortion().referencePixel().y, original.distortion().referencePixel().y - 8.0);

    const an::Result<an::SkyCoordinate> before = original.pixelToWorld({.x = 500.5, .y = 500.5});
    ASSERT_TRUE(before.hasValue()) << before.status().message();
    const an::Result<an::SkyCoordinate> after = moved->pixelToWorld({.x = 513.0, .y = 492.5});
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_DOUBLE_EQ(before->rightAscensionDegrees, after->rightAscensionDegrees);

    EXPECT_EQ(an::translateWcsSolution(original, std::numeric_limits<double>::quiet_NaN(), 0.0).code(),
              an::ErrorCode::InvalidArgument);
}

TEST(WcsSolutionTest, AResizeScalesTheMatrixAndTheDistortionTogether)
{
    const an::WcsSolution original = northUpSolution(frameDistortion());
    const an::Result<an::WcsSolution> doubled = an::resizeWcsSolution(original, 2.0, 2.0, 4096u, 3072u);
    ASSERT_TRUE(doubled.hasValue()) << doubled.status().message();

    EXPECT_DOUBLE_EQ(doubled->pixelScaleXArcsecPerPixel(), 1.1);
    EXPECT_DOUBLE_EQ(doubled->referencePixel().x, original.referencePixel().x * 2.0);
    EXPECT_EQ(doubled->imageWidth(), 4096u);

    // The same physical point of the frame maps to the same direction at both sampling densities.
    const an::Result<an::SkyCoordinate> before = original.pixelToWorld({.x = 400.0, .y = 300.0});
    ASSERT_TRUE(before.hasValue()) << before.status().message();
    const an::Result<an::SkyCoordinate> after = doubled->pixelToWorld({.x = 800.0, .y = 600.0});
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_NEAR(before->rightAscensionDegrees, after->rightAscensionDegrees, 1.0e-12);
    EXPECT_NEAR(before->declinationDegrees, after->declinationDegrees, 1.0e-12);

    // A linear solution has no polynomial to rescale, so anisotropic scaling is fine for it.
    const an::Result<an::WcsSolution> stretched = an::resizeWcsSolution(northUpSolution(), 2.0, 1.0, 4096u, 1536u);
    ASSERT_TRUE(stretched.hasValue()) << stretched.status().message();
    EXPECT_DOUBLE_EQ(stretched->pixelScaleXArcsecPerPixel(), 1.1);
    EXPECT_DOUBLE_EQ(stretched->pixelScaleYArcsecPerPixel(), 2.2);

    // With a polynomial it is refused rather than served with a model that no longer fits.
    EXPECT_EQ(an::resizeWcsSolution(original, 2.0, 1.0, 4096u, 1536u).code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::resizeWcsSolution(original, 0.0, 0.0, 10u, 10u).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::resizeWcsSolution(original, 2.0, 2.0, 0u, 10u).code(), an::ErrorCode::InvalidArgument);
}

// A resize that keeps the aspect ratio lands each side of its result on a whole pixel, so its two scales rarely agree
// exactly: 2048 by 1536 at seventy per cent is 1433 by 1075. While they agree within a thousandth of the larger the
// polynomial is carried - both reference pixels moved on each axis with the grid, every
// coefficient of total degree d in all four components multiplied by the mean of the two scales raised to 1 - d - and
// it still describes the frame. At thirty per cent, 614 by 460, the scales differ by more and the polynomial is
// refused; the linear solution still follows once the caller drops it.
TEST(WcsSolutionTest, AResizeWhoseScalesAgreeWithinAThousandthCarriesTheDistortionRescaledByTheirMean)
{
    an::SipDistortionRequest bend;
    bend.referencePixel = {.x = 1024.0, .y = 768.0};
    bend.forwardX.order = 3u;
    bend.forwardX.coefficients[2][0] = 5.0e-7;
    bend.forwardX.coefficients[0][2] = -2.0e-7;
    bend.forwardX.coefficients[2][1] = 4.0e-10;
    bend.forwardY.order = 2u;
    bend.forwardY.coefficients[1][1] = 3.0e-7;
    bend.inverseX.order = 3u;
    bend.inverseX.coefficients[2][0] = -5.0e-7;
    bend.inverseX.coefficients[0][2] = 2.0e-7;
    bend.inverseX.coefficients[3][0] = 6.0e-11;
    bend.inverseY.order = 2u;
    bend.inverseY.coefficients[1][1] = -3.0e-7;
    const an::Result<an::SipDistortion> distortion = an::SipDistortion::create(bend);
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();
    const an::WcsSolution original = northUpSolution(*distortion);

    const double scaleX = 1433.0 / 2048.0;
    const double scaleY = 1075.0 / 1536.0;
    ASSERT_NE(scaleX, scaleY);
    ASSERT_LT(std::abs(scaleX - scaleY) / (std::max)(scaleX, scaleY), 1.0e-3);
    const an::Result<an::WcsSolution> resized = an::resizeWcsSolution(original, scaleX, scaleY, 1433u, 1075u);
    ASSERT_TRUE(resized.hasValue()) << resized.status().message();

    EXPECT_EQ(resized->imageWidth(), 1433u);
    EXPECT_EQ(resized->imageHeight(), 1075u);
    EXPECT_DOUBLE_EQ(resized->referencePixel().x, original.referencePixel().x * scaleX);
    EXPECT_DOUBLE_EQ(resized->referencePixel().y, original.referencePixel().y * scaleY);
    EXPECT_DOUBLE_EQ(resized->linear().cd11, original.linear().cd11 / scaleX);
    EXPECT_DOUBLE_EQ(resized->linear().cd22, original.linear().cd22 / scaleY);
    ASSERT_TRUE(resized->distortion().hasForwardModel());
    ASSERT_TRUE(resized->distortion().hasInverseModel());
    EXPECT_EQ(resized->distortion().referencePixel(), resized->referencePixel());

    const double mean = 0.5 * (scaleX + scaleY);
    const std::pair<const an::SipPolynomial*, const an::SipPolynomial*> components[] = {
        {&original.distortion().forwardX(), &resized->distortion().forwardX()},
        {&original.distortion().forwardY(), &resized->distortion().forwardY()},
        {&original.distortion().inverseX(), &resized->distortion().inverseX()},
        {&original.distortion().inverseY(), &resized->distortion().inverseY()}};
    for (const auto& [before, after] : components)
    {
        EXPECT_EQ(after->order, before->order);
        for (std::size_t i = 0u; i < an::kSipCoefficientRowCount; ++i)
        {
            for (std::size_t j = 0u; j < an::kSipCoefficientRowCount; ++j)
            {
                SCOPED_TRACE(std::to_string(i) + "," + std::to_string(j));
                const double degree = static_cast<double>(i + j);
                EXPECT_DOUBLE_EQ(after->coefficients[i][j], before->coefficients[i][j] * std::pow(mean, 1.0 - degree));
            }
        }
    }
    // The mean is neither scale, so this is not the horizontal scale's rescaling.
    EXPECT_NE(resized->distortion().forwardX().coefficients[2][0],
              original.distortion().forwardX().coefficients[2][0] / scaleX);

    // A point of the frame and the place the resize put it point at the same direction, well inside the second of arc
    // the dropped polynomial would have moved it by near this corner.
    const an::PixelCoordinate probe{.x = 150.0, .y = 1400.0};
    const an::Result<an::SkyCoordinate> before = original.pixelToWorld(probe);
    ASSERT_TRUE(before.hasValue()) << before.status().message();
    const an::Result<an::SkyCoordinate> after = resized->pixelToWorld({.x = probe.x * scaleX, .y = probe.y * scaleY});
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_NEAR(before->rightAscensionDegrees, after->rightAscensionDegrees, 1.0e-6);
    EXPECT_NEAR(before->declinationDegrees, after->declinationDegrees, 1.0e-6);

    const double smallX = 614.0 / 2048.0;
    const double smallY = 460.0 / 1536.0;
    ASSERT_GT(std::abs(smallX - smallY) / (std::max)(smallX, smallY), 1.0e-3);
    EXPECT_EQ(an::resizeWcsSolution(original, smallX, smallY, 614u, 460u).code(), an::ErrorCode::Unsupported);
    const an::Result<an::WcsSolution> linear =
        an::resizeWcsSolution(original.withoutDistortion(), smallX, smallY, 614u, 460u);
    ASSERT_TRUE(linear.hasValue()) << linear.status().message();
    EXPECT_FALSE(linear->distortion().hasForwardModel() || linear->distortion().hasInverseModel());
}

TEST(WcsSolutionTest, ARotationTurnsTheMatrixAndRefusesToCarryAPolynomial)
{
    const an::WcsSolution original = northUpSolution();
    const an::Result<an::WcsSolution> turned = an::rotateWcsSolution(original, 90.0, 1536u, 2048u);
    ASSERT_TRUE(turned.hasValue()) << turned.status().message();

    // Turning the contents a quarter turn counter-clockwise turns celestial north with them.
    EXPECT_DOUBLE_EQ(turned->orientationDegrees(), 90.0);
    EXPECT_DOUBLE_EQ(turned->meanPixelScaleArcsecPerPixel(), original.meanPixelScaleArcsecPerPixel());
    EXPECT_EQ(turned->mirrored(), original.mirrored());

    // A pixel and the place the rotation put it point at the same direction. The frame centre is
    // the pivot, so the offset from it turns with the contents.
    const an::PixelCoordinate probe{.x = 1024.0 + 200.0, .y = 768.0 + 100.0};
    const an::PixelCoordinate rotated{.x = 768.0 - 100.0, .y = 1024.0 + 200.0};
    const an::Result<an::SkyCoordinate> before = original.pixelToWorld(probe);
    ASSERT_TRUE(before.hasValue()) << before.status().message();
    const an::Result<an::SkyCoordinate> after = turned->pixelToWorld(rotated);
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_NEAR(before->rightAscensionDegrees, after->rightAscensionDegrees, 1.0e-9);
    EXPECT_NEAR(before->declinationDegrees, after->declinationDegrees, 1.0e-9);

    // A rotation does not delete the whole solution: this refuses only the part it cannot carry, and
    // leaves the caller holding a solution it can still drop the model from.
    const an::WcsSolution distorted = northUpSolution(frameDistortion());
    EXPECT_EQ(an::rotateWcsSolution(distorted, 90.0, 1536u, 2048u).code(), an::ErrorCode::Unsupported);
    EXPECT_TRUE(an::rotateWcsSolution(distorted.withoutDistortion(), 90.0, 1536u, 2048u).hasValue());

    EXPECT_EQ(an::rotateWcsSolution(original, std::numeric_limits<double>::quiet_NaN(), 10u, 10u).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::rotateWcsSolution(original, 90.0, 0u, 10u).code(), an::ErrorCode::InvalidArgument);
}

} // namespace

TEST(WcsSolutionTest, DistortionMustShareTheLinearReferencePixel)
{
    auto request = northUpSolution().toRequest();
    for (const bool inverse : {false, true})
    {
        an::SipDistortionRequest sip;
        sip.referencePixel = request.referencePixel;
        auto& polynomial = inverse ? sip.inverseX : sip.forwardX;
        polynomial.order = 2u;
        polynomial.coefficients[2][0] = 0.00001;
        request.distortion = *an::SipDistortion::create(sip);
        ASSERT_TRUE(an::WcsSolution::create(request).hasValue());
        sip.referencePixel.x += 1.0;
        request.distortion = *an::SipDistortion::create(sip);
        EXPECT_EQ(an::WcsSolution::create(request).code(), an::ErrorCode::IncompatibleArguments);
    }
}
