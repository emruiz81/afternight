// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The registration two plate solutions imply, spelled as a composition.
//
// Two answers are wanted from a pair of solutions: a registration, sampled as a grid of source
// positions through both solutions, fitted as a homography and gated on its residual; and a mosaic
// canvas, built by projecting every frame's boundary into a plane centred on the spherical mean of
// the field centres. Here those answers are the world-coordinate model, the registration fit, the
// frame bounds and the canvas plan composed by the caller - each tested on its own elsewhere in this
// suite and in the astrometry suite. These cases prove that the composition gives the observable
// answer, so no facade has to be written for it.

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationFit.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include "RegistrationTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace
{

using registration_test::makeEngine;
using registration_test::makeExecution;

/// Two point two arcseconds a pixel, in degrees: the plate scale of the solved frames the
/// world-coordinate model's real headers came from.
constexpr double kScaleDegrees = 2.2 / 3600.0;

constexpr std::uint32_t kReferenceWidth = 2048u;
constexpr std::uint32_t kReferenceHeight = 1536u;
constexpr std::uint32_t kSourceWidth = 1800u;
constexpr std::uint32_t kSourceHeight = 1400u;

constexpr double kReferenceRightAscension = 83.822083;
constexpr double kReferenceDeclination = -5.391111;

/// The number of grid columns and rows sampled per axis.
constexpr std::uint32_t kGridSamples = 10u;

/// A north-up, east-left solution of the reference frame.
[[nodiscard]] an::WcsSolution referenceSolution()
{
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 1024.0, .y = 768.0};
    request.referencePoint = {.rightAscensionDegrees = kReferenceRightAscension,
                              .declinationDegrees = kReferenceDeclination};
    request.linear = {.cd11 = -kScaleDegrees, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScaleDegrees};
    request.imageWidth = kReferenceWidth;
    request.imageHeight = kReferenceHeight;
    const an::Result<an::WcsSolution> made = an::WcsSolution::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.value();
}

/// A source frame of the same scale, rotated on the sky and pointed a little away from the
/// reference, optionally carrying its own plate distortion.
[[nodiscard]] an::WcsSolution sourceSolution(const double rotationDegrees, const double rightAscensionOffsetDegrees,
                                             const double declinationOffsetDegrees,
                                             const an::SipDistortion& distortion = {})
{
    const double angle = rotationDegrees * std::numbers::pi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 900.0, .y = 700.0};
    request.referencePoint = {.rightAscensionDegrees = kReferenceRightAscension + rightAscensionOffsetDegrees,
                              .declinationDegrees = kReferenceDeclination + declinationOffsetDegrees};
    request.linear = {.cd11 = -kScaleDegrees * cosine,
                      .cd12 = kScaleDegrees * sine,
                      .cd21 = kScaleDegrees * sine,
                      .cd22 = kScaleDegrees * cosine};
    request.imageWidth = kSourceWidth;
    request.imageHeight = kSourceHeight;
    request.distortion = distortion;
    const an::Result<an::WcsSolution> made = an::WcsSolution::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.value();
}

/// A second-order forward distortion centred on the source frame's reference pixel, strong enough
/// to move an edge position by more than a pixel.
[[nodiscard]] an::SipDistortion sourceDistortion()
{
    an::SipDistortionRequest request;
    request.referencePixel = {.x = 900.0, .y = 700.0};
    request.forwardX.order = 2u;
    request.forwardX.coefficients[2][0] = 2.0e-6;
    request.forwardX.coefficients[1][1] = -1.0e-6;
    request.forwardY.order = 2u;
    request.forwardY.coefficients[0][2] = 1.5e-6;
    request.forwardY.coefficients[1][1] = 0.5e-6;
    const an::Result<an::SipDistortion> made = an::SipDistortion::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::SipDistortion{};
}

/// The grid of source positions sampled: ten per axis, spread over the frame.
[[nodiscard]] std::vector<an::PixelCoordinate> sourceGrid()
{
    std::vector<an::PixelCoordinate> grid;
    grid.reserve(static_cast<std::size_t>(kGridSamples) * kGridSamples);
    for (std::uint32_t row = 0u; row < kGridSamples; ++row)
    {
        for (std::uint32_t column = 0u; column < kGridSamples; ++column)
        {
            const double fractionX = (static_cast<double>(column) + 0.5) / kGridSamples;
            const double fractionY = (static_cast<double>(row) + 0.5) / kGridSamples;
            grid.push_back({.x = fractionX * kSourceWidth, .y = fractionY * kSourceHeight});
        }
    }
    return grid;
}

/// Where a source position lands in the reference frame, by way of the sky.
[[nodiscard]] an::PixelCoordinate throughTheSky(const an::WcsSolution& source, const an::WcsSolution& reference,
                                                const an::PixelCoordinate& pixel)
{
    const an::Result<an::SkyCoordinate> direction = source.pixelToWorld(pixel);
    EXPECT_TRUE(direction.hasValue()) << direction.status().message();
    const an::Result<an::PixelCoordinate> landed = reference.worldToPixel(direction.value());
    EXPECT_TRUE(landed.hasValue()) << landed.status().message();
    return landed.value();
}

/// The correspondences the fit is given: every grid position, on the source side after its own
/// distortion has been undone, on the reference side after the sky.
struct Correspondences
{
    std::vector<an::PixelCoordinate> source;
    std::vector<an::PixelCoordinate> reference;
};

[[nodiscard]] Correspondences correspondencesBetween(const an::WcsSolution& source, const an::WcsSolution& reference,
                                                     const bool correctSourceDistortion)
{
    Correspondences pairs;
    for (const an::PixelCoordinate& pixel : sourceGrid())
    {
        an::PixelCoordinate sourceSide = pixel;
        if (correctSourceDistortion)
        {
            const an::Result<an::PixelCoordinate> corrected = an::applySipForward(source.distortion(), pixel);
            EXPECT_TRUE(corrected.hasValue()) << corrected.status().message();
            sourceSide = corrected.value();
        }
        pairs.source.push_back(sourceSide);
        pairs.reference.push_back(throughTheSky(source, reference, pixel));
    }
    return pairs;
}

[[nodiscard]] double distanceBetween(const an::PixelCoordinate& left, const an::PixelCoordinate& right)
{
    return std::hypot(left.x - right.x, left.y - right.y);
}

[[nodiscard]] std::array<an::PixelCoordinate, 4u> sourceCorners()
{
    return {an::PixelCoordinate{.x = 0.0, .y = 0.0}, an::PixelCoordinate{.x = kSourceWidth, .y = 0.0},
            an::PixelCoordinate{.x = kSourceWidth, .y = kSourceHeight},
            an::PixelCoordinate{.x = 0.0, .y = kSourceHeight}};
}

} // namespace

TEST(AstrometricRegistrationTest, TwoSolutionsFitTheProjectiveTransformBetweenTheirFrames)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::WcsSolution reference = referenceSolution();
    const an::WcsSolution source = sourceSolution(12.0, 0.03, 0.02);

    const Correspondences pairs = correspondencesBetween(source, reference, false);
    ASSERT_EQ(pairs.source.size(), static_cast<std::size_t>(kGridSamples) * kGridSamples);

    // Two tangent planes of one sphere are related by a projective transform exactly, so the linear
    // stage alone reproduces the sky's answer and no polynomial is needed to explain a residual.
    const an::Result<an::RegistrationFitResult> fit = an::fitRegistrationTransform(
        execution, pairs.source, pairs.reference, {.linearKind = an::RegistrationTransformKind::Projective});
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_EQ(fit->correspondenceCount, pairs.source.size());
    EXPECT_EQ(fit->polynomialOrder, 0u);
    EXPECT_LT(fit->rootMeanSquareResidual, 1.0e-5);
    EXPECT_LT(fit->maximumResidual, 1.0e-4);
    EXPECT_EQ(fit->transform.kind(), an::RegistrationTransformKind::Projective);

    // The fitted matrix reads back as the rotation and the unit scale the source was built with.
    EXPECT_NEAR(fit->transform.rotationDegrees(), 12.0, 0.01);
    EXPECT_NEAR(fit->transform.scale(), 1.0, 1.0e-5);

    // Positions the fit never saw - the frame's corners - land where the sky puts them.
    for (const an::PixelCoordinate& corner : sourceCorners())
    {
        const an::Result<an::PixelCoordinate> mapped = an::mapRegistrationPoint(fit->transform, corner);
        ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
        EXPECT_LT(distanceBetween(*mapped, throughTheSky(source, reference, corner)), 1.0e-3);
    }
}

TEST(AstrometricRegistrationTest, ADistortedSourceIsFittedOnItsCorrectedPositionsAndWarpedWithItsDistortion)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::WcsSolution reference = referenceSolution();
    const an::SipDistortion distortion = sourceDistortion();
    const an::WcsSolution source = sourceSolution(-4.0, -0.02, 0.015, distortion);
    ASSERT_TRUE(source.distortion().hasForwardModel());

    // Fitted on the raw grid, the distortion is left in the residual and a linear model cannot
    // explain it; fitted on the corrected grid, the residual is the sky's, which is nothing.
    const Correspondences raw = correspondencesBetween(source, reference, false);
    const an::Result<an::RegistrationFitResult> rawFit = an::fitRegistrationTransform(
        execution, raw.source, raw.reference, {.linearKind = an::RegistrationTransformKind::Projective});
    ASSERT_TRUE(rawFit.hasValue()) << rawFit.status().message();
    EXPECT_GT(rawFit->maximumResidual, 0.25);

    const Correspondences corrected = correspondencesBetween(source, reference, true);
    const an::Result<an::RegistrationFitResult> fit = an::fitRegistrationTransform(
        execution, corrected.source, corrected.reference, {.linearKind = an::RegistrationTransformKind::Projective});
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_LT(fit->rootMeanSquareResidual, 1.0e-5);
    EXPECT_NEAR(fit->transform.rotationDegrees(), -4.0, 0.01);

    // The distortion travels beside the transform rather than inside it: the bounds and the warp plan
    // take the frame's own model and undo it first, so a raw source corner lands where the sky says.
    const an::Result<an::RegistrationBounds> bounds =
        an::computeRegistrationBounds(fit->transform, kSourceWidth, kSourceHeight, {}, &distortion);
    ASSERT_TRUE(bounds.hasValue()) << bounds.status().message();
    const an::Result<std::array<an::PixelCoordinate, an::kRegistrationFrameCornerCount>> corners =
        an::mapRegistrationFrameCorners(fit->transform, kSourceWidth, kSourceHeight, {}, &distortion);
    ASSERT_TRUE(corners.hasValue()) << corners.status().message();
    const std::array<an::PixelCoordinate, 4u> rawCorners = sourceCorners();
    for (std::size_t index = 0u; index < rawCorners.size(); ++index)
    {
        const an::PixelCoordinate expected = throughTheSky(source, reference, rawCorners[index]);
        EXPECT_LT(distanceBetween((*corners)[index], expected), 1.0e-3);
        EXPECT_GE(expected.x, bounds->minimumX - 1.0e-6);
        EXPECT_LE(expected.x, bounds->maximumX + 1.0e-6);
        EXPECT_GE(expected.y, bounds->minimumY - 1.0e-6);
        EXPECT_LE(expected.y, bounds->maximumY + 1.0e-6);
    }

    const an::ImageDescriptor descriptor = registration_test::descriptorOf(kSourceWidth, kSourceHeight);
    const an::Result<an::RegistrationWarpPlan> planned = an::planRegistrationWarp(
        descriptor, fit->transform, distortion, {.width = kReferenceWidth, .height = kReferenceHeight});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_TRUE(planned->needsCoordinateMap());
    EXPECT_EQ(planned->distortion(), distortion);
}

TEST(AstrometricRegistrationTest, AUnionCanvasCarriesTheReferenceSolutionMovedToItsOrigin)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::WcsSolution reference = referenceSolution();
    // East is to the left of a north-up frame and declination grows with the row index, so a source
    // pointed east and north of the reference maps to negative columns and to rows past the
    // reference frame's last one.
    const an::WcsSolution source = sourceSolution(0.0, 0.6, 0.4);

    const Correspondences pairs = correspondencesBetween(source, reference, false);
    const an::Result<an::RegistrationFitResult> fit = an::fitRegistrationTransform(
        execution, pairs.source, pairs.reference, {.linearKind = an::RegistrationTransformKind::Projective});
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    ASSERT_LT(fit->rootMeanSquareResidual, 1.0e-5);

    // One rectangle per frame: the reference frame's own, and the source's mapped one.
    const an::Result<an::RegistrationBounds> referenceBounds =
        an::computeRegistrationBounds(an::RegistrationTransform{}, kReferenceWidth, kReferenceHeight);
    ASSERT_TRUE(referenceBounds.hasValue()) << referenceBounds.status().message();
    const an::Result<an::RegistrationBounds> sourceBounds =
        an::computeRegistrationBounds(fit->transform, kSourceWidth, kSourceHeight);
    ASSERT_TRUE(sourceBounds.hasValue()) << sourceBounds.status().message();
    EXPECT_LT(sourceBounds->minimumX, 0.0);
    EXPECT_GT(sourceBounds->maximumY, static_cast<double>(kReferenceHeight));

    const std::array<an::RegistrationBounds, 2u> bounds{*referenceBounds, *sourceBounds};
    const an::Result<an::RegistrationCanvas> canvas =
        an::planRegistrationCanvas(bounds, 0u, an::RegistrationCanvasMode::Union);
    ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();
    EXPECT_GT(canvas->width, kReferenceWidth);
    EXPECT_GT(canvas->height, kReferenceHeight);
    EXPECT_GT(canvas->origin.x, 0.0);
    EXPECT_DOUBLE_EQ(canvas->origin.y, 0.0);

    // The canvas keeps the reference projection: its solution is the reference's, with the reference
    // pixel moved by the canvas origin, at the canvas extent.
    const an::Result<an::WcsSolution> moved = an::translateWcsSolution(reference, canvas->origin.x, canvas->origin.y);
    ASSERT_TRUE(moved.hasValue()) << moved.status().message();
    an::WcsSolutionRequest canvasRequest = moved->toRequest();
    canvasRequest.imageWidth = canvas->width;
    canvasRequest.imageHeight = canvas->height;
    const an::Result<an::WcsSolution> canvasSolution = an::WcsSolution::create(canvasRequest);
    ASSERT_TRUE(canvasSolution.hasValue()) << canvasSolution.status().message();
    EXPECT_DOUBLE_EQ(canvasSolution->referencePixel().x, reference.referencePixel().x + canvas->origin.x);
    EXPECT_EQ(canvasSolution->linear(), reference.linear());

    // Every source corner and its centre land on the canvas at the position the canvas solution
    // gives the same direction, and inside the canvas.
    const std::array<an::PixelCoordinate, 4u> corners = sourceCorners();
    std::vector<an::PixelCoordinate> probes(corners.begin(), corners.end());
    probes.push_back({.x = 0.5 * kSourceWidth, .y = 0.5 * kSourceHeight});
    for (const an::PixelCoordinate& probe : probes)
    {
        const an::Result<an::PixelCoordinate> onCanvas =
            an::mapRegistrationPoint(fit->transform, probe, canvas->origin);
        ASSERT_TRUE(onCanvas.hasValue()) << onCanvas.status().message();
        EXPECT_LT(distanceBetween(*onCanvas, throughTheSky(source, *canvasSolution, probe)), 1.0e-3);
        EXPECT_GE(onCanvas->x, -1.0e-6);
        EXPECT_LE(onCanvas->x, static_cast<double>(canvas->width) + 1.0e-6);
        EXPECT_GE(onCanvas->y, -1.0e-6);
        EXPECT_LE(onCanvas->y, static_cast<double>(canvas->height) + 1.0e-6);
    }

    // The union origin is the exact negated minimum, which is rarely a whole pixel, so the reference
    // frame itself is resampled by that fraction, the canvas shifting it by the exact minimum. A
    // caller that would rather copy it rounds the origin up to the next pixel,
    // and the placement says which of the two it got.
    const an::RegistrationPlacement exact =
        an::resolveRegistrationPlacement(an::RegistrationTransform{}, canvas->origin);
    EXPECT_EQ(exact.route, an::RegistrationSamplingRoute::Warp);
    const an::PixelCoordinate wholeOrigin{.x = std::ceil(canvas->origin.x), .y = std::ceil(canvas->origin.y)};
    const an::RegistrationPlacement rounded =
        an::resolveRegistrationPlacement(an::RegistrationTransform{}, wholeOrigin);
    EXPECT_EQ(rounded.route, an::RegistrationSamplingRoute::DirectRectangle);
    EXPECT_EQ(rounded.offsetX, static_cast<std::int64_t>(wholeOrigin.x));
    EXPECT_EQ(rounded.offsetY, 0);
}
