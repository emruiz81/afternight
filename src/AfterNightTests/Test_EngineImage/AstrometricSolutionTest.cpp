// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/AstrometricSolution.h.
//
// The state that matters most here is absence. Most images have no solution, and the failure this
// type exists to prevent is a set of zeroes that reads as a valid pointing at the celestial origin -
// so every accessor of an absent solution is checked to answer nothing rather than a plausible
// number.
//
// The range rules are checked rather than normalised, and the cases say why in both directions: a
// right ascension of 400 degrees is the same direction as 40 and would make two solutions of one
// frame compare unequal, and a declination of 100 is not a direction at all.
//
// The reference pixel is deliberately unbounded, and there is a case for that too: a crop moves the
// reference point outside the frame it describes, and a solution that survives a crop is the point
// of having a typed one.

#include <AfterNightEngine/Image/AstrometricSolution.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>

namespace
{

/// A solution request that is valid before a case spoils one field of it.
///
/// @return the request.
[[nodiscard]] an::AstrometricSolutionRequest solvedRequest()
{
    return an::AstrometricSolutionRequest{
        .referencePoint = an::SkyCoordinate{.rightAscensionDegrees = 350.85, .declinationDegrees = 61.25},
        .referencePixelX = 2048.5,
        .referencePixelY = 1536.5,
        .pixelScaleArcsecPerPixel = 1.35,
        .orientationDegrees = 91.5,
        .mirrored = false,
        .hasDistortion = true};
}

TEST(AstrometricSolutionTest, AnAbsentSolutionAnswersNothingRatherThanZero)
{
    const an::AstrometricSolution solution;
    EXPECT_FALSE(solution.present());
    EXPECT_FALSE(solution.referencePoint().has_value());
    EXPECT_FALSE(solution.referencePixelX().has_value());
    EXPECT_FALSE(solution.referencePixelY().has_value());
    EXPECT_FALSE(solution.pixelScaleArcsecPerPixel().has_value());
    EXPECT_FALSE(solution.orientationDegrees().has_value());
    EXPECT_FALSE(solution.mirrored().has_value());
    EXPECT_FALSE(solution.hasDistortion().has_value());
    EXPECT_FALSE(solution.toRequest().has_value());
}

TEST(AstrometricSolutionTest, ASolvedFrameCarriesEveryNumberItReported)
{
    an::Result<an::AstrometricSolution> solution = an::AstrometricSolution::create(solvedRequest());
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();

    EXPECT_TRUE(solution->present());
    ASSERT_TRUE(solution->referencePoint().has_value());
    EXPECT_DOUBLE_EQ(solution->referencePoint()->rightAscensionDegrees, 350.85);
    EXPECT_DOUBLE_EQ(solution->referencePoint()->declinationDegrees, 61.25);
    EXPECT_DOUBLE_EQ(*solution->referencePixelX(), 2048.5);
    EXPECT_DOUBLE_EQ(*solution->referencePixelY(), 1536.5);
    EXPECT_DOUBLE_EQ(*solution->pixelScaleArcsecPerPixel(), 1.35);
    EXPECT_DOUBLE_EQ(*solution->orientationDegrees(), 91.5);
    EXPECT_FALSE(*solution->mirrored());
    EXPECT_TRUE(*solution->hasDistortion());
}

TEST(AstrometricSolutionTest, TheSolutionRoundTripsThroughItsRequest)
{
    an::Result<an::AstrometricSolution> solution = an::AstrometricSolution::create(solvedRequest());
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();

    const std::optional<an::AstrometricSolutionRequest> request = solution->toRequest();
    ASSERT_TRUE(request.has_value());
    an::Result<an::AstrometricSolution> rebuilt = an::AstrometricSolution::create(*request);
    ASSERT_TRUE(rebuilt.hasValue()) << rebuilt.status().message();
    EXPECT_EQ(*rebuilt, *solution);
}

TEST(AstrometricSolutionTest, ARightAscensionOutsideOneTurnIsRefusedRatherThanWrapped)
{
    for (const double ascension : {-0.1, 360.0, 400.0, std::numeric_limits<double>::infinity(), std::nan("")})
    {
        an::AstrometricSolutionRequest request = solvedRequest();
        request.referencePoint.rightAscensionDegrees = ascension;

        an::Result<an::AstrometricSolution> refused = an::AstrometricSolution::create(request);
        ASSERT_FALSE(refused.hasValue()) << ascension;
        EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument) << ascension;
        const an::StatusContextField* const field = refused.status().findContext(an::ContextKey::Parameter);
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->text, "referencePoint");
    }
    EXPECT_TRUE(an::AstrometricSolution::create([] {
                    an::AstrometricSolutionRequest request = solvedRequest();
                    request.referencePoint.rightAscensionDegrees = 0.0;
                    return request;
                }())
                    .hasValue());
}

TEST(AstrometricSolutionTest, ADeclinationOutsideThePolesIsNotADirection)
{
    for (const double declination : {-90.1, 90.1, 100.0, std::nan("")})
    {
        an::AstrometricSolutionRequest request = solvedRequest();
        request.referencePoint.declinationDegrees = declination;

        an::Result<an::AstrometricSolution> refused = an::AstrometricSolution::create(request);
        ASSERT_FALSE(refused.hasValue()) << declination;
        EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument) << declination;
    }

    for (const double declination : {-90.0, 0.0, 90.0})
    {
        an::AstrometricSolutionRequest request = solvedRequest();
        request.referencePoint.declinationDegrees = declination;
        EXPECT_TRUE(an::AstrometricSolution::create(request).hasValue()) << declination;
    }
}

TEST(AstrometricSolutionTest, AScaleAndAnOrientationHaveTheirOwnRanges)
{
    for (const double scale : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::nan("")})
    {
        an::AstrometricSolutionRequest request = solvedRequest();
        request.pixelScaleArcsecPerPixel = scale;

        an::Result<an::AstrometricSolution> refused = an::AstrometricSolution::create(request);
        ASSERT_FALSE(refused.hasValue()) << scale;
        const an::StatusContextField* const field = refused.status().findContext(an::ContextKey::Parameter);
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->text, "pixelScaleArcsecPerPixel");
    }

    for (const double orientation : {-0.1, 360.0, std::nan("")})
    {
        an::AstrometricSolutionRequest request = solvedRequest();
        request.orientationDegrees = orientation;

        an::Result<an::AstrometricSolution> refused = an::AstrometricSolution::create(request);
        ASSERT_FALSE(refused.hasValue()) << orientation;
        const an::StatusContextField* const field = refused.status().findContext(an::ContextKey::Parameter);
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->text, "orientationDegrees");
    }
}

TEST(AstrometricSolutionTest, AReferencePixelOutsideTheFrameIsWhatACropLeavesBehind)
{
    an::AstrometricSolutionRequest request = solvedRequest();
    request.referencePixelX = -512.25;
    request.referencePixelY = 100000.0;

    an::Result<an::AstrometricSolution> solution = an::AstrometricSolution::create(request);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();
    EXPECT_DOUBLE_EQ(*solution->referencePixelX(), -512.25);
    EXPECT_DOUBLE_EQ(*solution->referencePixelY(), 100000.0);
}

TEST(AstrometricSolutionTest, AReferencePixelThatIsNotANumberIsRefused)
{
    an::AstrometricSolutionRequest column = solvedRequest();
    column.referencePixelX = std::nan("");
    EXPECT_FALSE(an::AstrometricSolution::create(column).hasValue());

    an::AstrometricSolutionRequest row = solvedRequest();
    row.referencePixelY = -std::numeric_limits<double>::infinity();
    EXPECT_FALSE(an::AstrometricSolution::create(row).hasValue());
}

TEST(AstrometricSolutionTest, TwoAbsentSolutionsAreEqualAndAbsenceNeverEqualsPresence)
{
    const an::AstrometricSolution absent;
    const an::AstrometricSolution alsoAbsent;
    EXPECT_EQ(absent, alsoAbsent);

    an::Result<an::AstrometricSolution> present = an::AstrometricSolution::create(solvedRequest());
    ASSERT_TRUE(present.hasValue());
    EXPECT_NE(absent, *present);

    an::AstrometricSolutionRequest mirrored = solvedRequest();
    mirrored.mirrored = true;
    an::Result<an::AstrometricSolution> flipped = an::AstrometricSolution::create(mirrored);
    ASSERT_TRUE(flipped.hasValue());
    EXPECT_NE(*flipped, *present);
}

TEST(AstrometricSolutionTest, TheDistortionFlagSaysAModelExistsWithoutDescribingIt)
{
    an::AstrometricSolutionRequest linear = solvedRequest();
    linear.hasDistortion = false;
    an::Result<an::AstrometricSolution> withoutModel = an::AstrometricSolution::create(linear);
    ASSERT_TRUE(withoutModel.hasValue());
    ASSERT_TRUE(withoutModel->hasDistortion().has_value());
    EXPECT_FALSE(*withoutModel->hasDistortion());

    an::Result<an::AstrometricSolution> withModel = an::AstrometricSolution::create(solvedRequest());
    ASSERT_TRUE(withModel.hasValue());
    EXPECT_TRUE(*withModel->hasDistortion());
    EXPECT_NE(*withModel, *withoutModel);
}

} // namespace
