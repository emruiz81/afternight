// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Canvas, placement, overlap, coordinate-map and warp tests for registration planning.

#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>

#include <AfterNightEngine/Algorithms/Registration/RegistrationOpenCvBridge.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include "RegistrationTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace
{

using registration_test::descriptorOf;
using registration_test::layoutOf;
using registration_test::makeEngine;
using registration_test::makeExecution;

[[nodiscard]] an::ValidityMaskView<std::uint8_t> outputMask(an::ValidityMask& mask)
{
    const std::optional<an::ValidityMaskView<std::uint8_t>> view = mask.view();
    EXPECT_TRUE(view.has_value());
    return *view;
}

[[nodiscard]] an::RegistrationTransform linearTransform(const an::RegistrationTransformKind kind,
                                                        const std::array<double, 9>& matrix)
{
    const an::Result<an::RegistrationTransform> made =
        an::RegistrationTransform::create({.linearKind = kind, .sourceToReference = matrix});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::RegistrationTransform{};
}

[[nodiscard]] an::RegistrationTransform identityPolynomialTransform()
{
    an::RegistrationTransformRequest request;
    request.polynomial.order = 2u;
    request.polynomial.x[1] = 1.0;
    request.polynomial.y[2] = 1.0;
    request.polynomial.normalizationScale = {64.0, 64.0};
    const an::Result<an::RegistrationTransform> made = an::RegistrationTransform::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::RegistrationTransform{};
}

[[nodiscard]] an::RegistrationBounds boundsOf(const double minX, const double minY, const double maxX,
                                              const double maxY)
{
    return {.minimumX = minX, .minimumY = minY, .maximumX = maxX, .maximumY = maxY};
}

} // namespace

TEST(RegistrationPlanTest, EveryModeAndRouteIsNamed)
{
    EXPECT_STREQ(an::registrationCanvasModeName(an::RegistrationCanvasMode::Reference), "Reference");
    EXPECT_STREQ(an::registrationCanvasModeName(an::RegistrationCanvasMode::Union), "Union");
    EXPECT_STREQ(an::registrationCanvasModeName(an::RegistrationCanvasMode::Intersection), "Intersection");
    EXPECT_STREQ(an::registrationCanvasModeName(static_cast<an::RegistrationCanvasMode>(6)),
                 "UnknownRegistrationCanvasMode");
    EXPECT_STREQ(an::registrationSamplingRouteName(an::RegistrationSamplingRoute::DirectRectangle), "DirectRectangle");
    EXPECT_STREQ(an::registrationSamplingRouteName(an::RegistrationSamplingRoute::Warp), "Warp");
    EXPECT_STREQ(an::registrationSamplingRouteName(static_cast<an::RegistrationSamplingRoute>(4)),
                 "UnknownRegistrationSamplingRoute");
    EXPECT_FALSE(an::isRegistrationCanvasMode(static_cast<an::RegistrationCanvasMode>(3)));
    EXPECT_FALSE(an::isRegistrationSamplingRoute(static_cast<an::RegistrationSamplingRoute>(2)));
}

TEST(RegistrationPlanTest, TheCanvasIsTheReferenceTheUnionOrTheIntersection)
{
    const std::array<an::RegistrationBounds, 3u> bounds{
        boundsOf(0.0, 0.0, 100.0, 80.0), boundsOf(-10.5, 5.0, 90.0, 95.25), boundsOf(20.0, -3.0, 130.0, 70.0)};

    const an::Result<an::RegistrationCanvas> reference =
        an::planRegistrationCanvas(bounds, 0u, an::RegistrationCanvasMode::Reference);
    ASSERT_TRUE(reference.hasValue()) << reference.status().message();
    EXPECT_EQ(reference->width, 100u);
    EXPECT_EQ(reference->height, 80u);
    EXPECT_DOUBLE_EQ(reference->origin.x, 0.0);
    EXPECT_DOUBLE_EQ(reference->origin.y, 0.0);

    const an::Result<an::RegistrationCanvas> united =
        an::planRegistrationCanvas(bounds, 0u, an::RegistrationCanvasMode::Union);
    ASSERT_TRUE(united.hasValue()) << united.status().message();
    EXPECT_DOUBLE_EQ(united->boundingBox.minimumX, -10.5);
    EXPECT_DOUBLE_EQ(united->boundingBox.maximumY, 95.25);
    EXPECT_DOUBLE_EQ(united->origin.x, 10.5);
    EXPECT_DOUBLE_EQ(united->origin.y, 3.0);
    EXPECT_EQ(united->width, 141u);
    EXPECT_EQ(united->height, 99u);

    const an::Result<an::RegistrationCanvas> shared =
        an::planRegistrationCanvas(bounds, 0u, an::RegistrationCanvasMode::Intersection);
    ASSERT_TRUE(shared.hasValue()) << shared.status().message();
    EXPECT_DOUBLE_EQ(shared->boundingBox.minimumX, 20.0);
    EXPECT_DOUBLE_EQ(shared->boundingBox.maximumX, 90.0);
    EXPECT_DOUBLE_EQ(shared->boundingBox.minimumY, 5.0);
    EXPECT_DOUBLE_EQ(shared->boundingBox.maximumY, 70.0);
    EXPECT_EQ(shared->width, 70u);
    EXPECT_EQ(shared->height, 65u);
}

TEST(RegistrationPlanTest, TheCanvasRefusesWhatItCannotHold)
{
    const std::array<an::RegistrationBounds, 2u> bounds{boundsOf(0.0, 0.0, 10.0, 10.0),
                                                        boundsOf(100.0, 100.0, 110.0, 110.0)};

    const an::Result<an::RegistrationCanvas> empty =
        an::planRegistrationCanvas({}, 0u, an::RegistrationCanvasMode::Union);
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationCanvas> unknownMode =
        an::planRegistrationCanvas(bounds, 0u, static_cast<an::RegistrationCanvasMode>(7));
    ASSERT_FALSE(unknownMode.hasValue());
    EXPECT_EQ(unknownMode.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationCanvas> outsideSet =
        an::planRegistrationCanvas(bounds, 5u, an::RegistrationCanvasMode::Reference);
    ASSERT_FALSE(outsideSet.hasValue());
    EXPECT_EQ(outsideSet.status().code(), an::ErrorCode::InvalidArgument);

    const std::array<an::RegistrationBounds, 1u> inverted{boundsOf(10.0, 0.0, 0.0, 10.0)};
    const an::Result<an::RegistrationCanvas> backwards =
        an::planRegistrationCanvas(inverted, 0u, an::RegistrationCanvasMode::Reference);
    ASSERT_FALSE(backwards.hasValue());
    EXPECT_EQ(backwards.status().code(), an::ErrorCode::InvalidArgument);

    const std::array<an::RegistrationBounds, 1u> notFinite{
        boundsOf(0.0, 0.0, std::numeric_limits<double>::infinity(), 10.0)};
    const an::Result<an::RegistrationCanvas> unbounded =
        an::planRegistrationCanvas(notFinite, 0u, an::RegistrationCanvasMode::Reference);
    ASSERT_FALSE(unbounded.hasValue());
    EXPECT_EQ(unbounded.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationCanvas> disjoint =
        an::planRegistrationCanvas(bounds, 0u, an::RegistrationCanvasMode::Intersection);
    ASSERT_FALSE(disjoint.hasValue());
    EXPECT_EQ(disjoint.status().code(), an::ErrorCode::EmptySelection);

    const std::array<an::RegistrationBounds, 1u> enormous{boundsOf(0.0, 0.0, 2.0e9, 10.0)};
    const an::Result<an::RegistrationCanvas> tooLarge =
        an::planRegistrationCanvas(enormous, 0u, an::RegistrationCanvasMode::Reference);
    ASSERT_FALSE(tooLarge.hasValue());
    EXPECT_EQ(tooLarge.status().code(), an::ErrorCode::LimitExceeded);
}

TEST(RegistrationPlanTest, OnlyAWholePixelShiftOfAnUnbentFrameIsReadWhereItIs)
{
    const an::RegistrationTransform identity;
    const an::RegistrationPlacement direct = an::resolveRegistrationPlacement(identity, {.x = 12.0, .y = -4.0});
    EXPECT_EQ(direct.route, an::RegistrationSamplingRoute::DirectRectangle);
    EXPECT_EQ(direct.offsetX, 12);
    EXPECT_EQ(direct.offsetY, -4);

    const an::RegistrationTransform shifted =
        linearTransform(an::RegistrationTransformKind::Affine, {1.0, 0.0, 6.0, 0.0, 1.0, 7.0, 0.0, 0.0, 1.0});
    const an::RegistrationPlacement shiftedPlacement = an::resolveRegistrationPlacement(shifted, {.x = 2.0, .y = 3.0});
    EXPECT_EQ(shiftedPlacement.route, an::RegistrationSamplingRoute::DirectRectangle);
    EXPECT_EQ(shiftedPlacement.offsetX, 8);
    EXPECT_EQ(shiftedPlacement.offsetY, 10);

    EXPECT_EQ(an::resolveRegistrationPlacement(identity, {.x = 12.5, .y = 0.0}).route,
              an::RegistrationSamplingRoute::Warp);
    EXPECT_EQ(
        an::resolveRegistrationPlacement(
            linearTransform(an::RegistrationTransformKind::Affine, {1.0, 0.02, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}), {})
            .route,
        an::RegistrationSamplingRoute::Warp);
    EXPECT_EQ(an::resolveRegistrationPlacement(linearTransform(an::RegistrationTransformKind::Projective,
                                                               {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}),
                                               {})
                  .route,
              an::RegistrationSamplingRoute::Warp);
    EXPECT_EQ(an::resolveRegistrationPlacement(identityPolynomialTransform(), {}).route,
              an::RegistrationSamplingRoute::Warp);

    an::SipDistortionRequest distortionRequest;
    distortionRequest.forwardX.order = 2u;
    distortionRequest.forwardX.coefficients[2][0] = 1.0e-6;
    const an::Result<an::SipDistortion> distortion = an::SipDistortion::create(distortionRequest);
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();
    EXPECT_EQ(an::resolveRegistrationPlacement(identity, {}, &distortion.value()).route,
              an::RegistrationSamplingRoute::Warp);
}

TEST(RegistrationPlanTest, OverlapDiscoveryFindsAndRanksTheSharedRectangles)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<an::RegistrationFootprint, 3u> footprints{
        an::RegistrationFootprint{.bounds = boundsOf(0.0, 0.0, 100.0, 100.0), .framePixels = 10000u},
        an::RegistrationFootprint{.bounds = boundsOf(50.0, 0.0, 150.0, 100.0), .framePixels = 10000u},
        an::RegistrationFootprint{.bounds = boundsOf(400.0, 400.0, 500.0, 500.0), .framePixels = 10000u}};
    const std::array<an::RegistrationBounds, 3u> allBounds{footprints[0].bounds, footprints[1].bounds,
                                                           footprints[2].bounds};
    const an::Result<an::RegistrationCanvas> canvas =
        an::planRegistrationCanvas(allBounds, 0u, an::RegistrationCanvasMode::Union);
    ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();

    std::array<an::RegistrationOverlap, 4u> found{};
    const an::Result<an::RegistrationOverlapReport> report = an::findRegistrationOverlaps(
        execution, footprints, *canvas, {.minimumOverlapPixels = 100u, .minimumOverlapFraction = 0.01}, found);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->pairsExamined, 3u);
    EXPECT_EQ(report->pairsAccepted, 1u);
    EXPECT_EQ(report->pairsWritten, 1u);
    EXPECT_FALSE(report->truncated);
    EXPECT_EQ(found[0].frameA, 0u);
    EXPECT_EQ(found[0].frameB, 1u);
    EXPECT_EQ(found[0].region.x, 50u);
    EXPECT_EQ(found[0].region.y, 0u);
    EXPECT_EQ(found[0].region.width, 50u);
    EXPECT_EQ(found[0].region.height, 100u);
    EXPECT_EQ(found[0].region.pixelCount(), 5000u);

    const an::Result<an::RegistrationOverlapReport> demanding = an::findRegistrationOverlaps(
        execution, footprints, *canvas, {.minimumOverlapPixels = 100u, .minimumOverlapFraction = 0.9}, found);
    ASSERT_TRUE(demanding.hasValue()) << demanding.status().message();
    EXPECT_EQ(demanding->pairsAccepted, 0u);

    const an::Result<an::RegistrationOverlapReport> counted = an::findRegistrationOverlaps(
        execution, footprints, *canvas, {.minimumOverlapPixels = 100u, .minimumOverlapFraction = 0.01}, {});
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(counted->pairsAccepted, 1u);
    EXPECT_EQ(counted->pairsWritten, 0u);
    EXPECT_TRUE(counted->truncated);
}

TEST(RegistrationPlanTest, OverlapDiscoveryRefusesAnUnusableSetAndStopsWhenCancelled)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<an::RegistrationFootprint, 2u> footprints{
        an::RegistrationFootprint{.bounds = boundsOf(0.0, 0.0, 100.0, 100.0), .framePixels = 10000u},
        an::RegistrationFootprint{.bounds = boundsOf(50.0, 0.0, 150.0, 100.0), .framePixels = 10000u}};
    const std::array<an::RegistrationBounds, 2u> allBounds{footprints[0].bounds, footprints[1].bounds};
    const an::Result<an::RegistrationCanvas> canvas =
        an::planRegistrationCanvas(allBounds, 0u, an::RegistrationCanvasMode::Union);
    ASSERT_TRUE(canvas.hasValue()) << canvas.status().message();
    std::array<an::RegistrationOverlap, 2u> found{};

    const an::Result<an::RegistrationOverlapReport> noContext =
        an::findRegistrationOverlaps(an::ExecutionContext{}, footprints, *canvas, {}, found);
    ASSERT_FALSE(noContext.hasValue());
    EXPECT_EQ(noContext.status().code(), an::ErrorCode::NotInitialized);

    const an::Result<an::RegistrationOverlapReport> badFraction = an::findRegistrationOverlaps(
        execution, footprints, *canvas, {.minimumOverlapPixels = 1u, .minimumOverlapFraction = 2.0}, found);
    ASSERT_FALSE(badFraction.hasValue());
    EXPECT_EQ(badFraction.status().code(), an::ErrorCode::InvalidArgument);

    const std::array<an::RegistrationFootprint, 2u> zeroPixels{
        an::RegistrationFootprint{.bounds = boundsOf(0.0, 0.0, 100.0, 100.0), .framePixels = 0u}, footprints[1]};
    const an::Result<an::RegistrationOverlapReport> emptyFrame =
        an::findRegistrationOverlaps(execution, zeroPixels, *canvas, {}, found);
    ASSERT_FALSE(emptyFrame.hasValue());
    EXPECT_EQ(emptyFrame.status().code(), an::ErrorCode::InvalidArgument);

    std::stop_source stop;
    const an::ExecutionContext cancelled = makeExecution(engine, 4u, stop.get_token());
    stop.request_stop();
    const an::Result<an::RegistrationOverlapReport> stopped =
        an::findRegistrationOverlaps(cancelled, footprints, *canvas, {}, found);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
}

TEST(RegistrationPlanTest, ADestinationToSourceMapHoldsTheSourcePositionOfEveryDestinationCentre)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t width = 9u;
    constexpr std::uint32_t height = 7u;
    std::vector<float> xMap(width * height, 0.0f);
    std::vector<float> yMap(width * height, 0.0f);

    an::RegistrationMapRequest request;
    request.width = width;
    request.height = height;
    request.xMap = xMap;
    request.yMap = yMap;
    request.postTranslation = {.x = 3.0, .y = -2.0};

    const an::RegistrationTransform identity;
    const an::Result<an::RegistrationMapReport> report =
        an::computeRegistrationDestinationToSourceMap(execution, identity, nullptr, request);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->mapped, static_cast<std::uint64_t>(width) * height);
    EXPECT_EQ(report->unmapped, 0u);
    EXPECT_EQ(report->newtonIterations, 0u);
    for (std::uint32_t row = 0u; row < height; ++row)
    {
        for (std::uint32_t column = 0u; column < width; ++column)
        {
            const std::size_t index = static_cast<std::size_t>(row) * width + column;
            EXPECT_NEAR(xMap[index], static_cast<float>(column) + 0.5f - 3.0f, 1.0e-4f);
            EXPECT_NEAR(yMap[index], static_cast<float>(row) + 0.5f + 2.0f, 1.0e-4f);
        }
    }
}

TEST(RegistrationPlanTest, ASourceToDestinationMapIsTheForwardDirection)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t width = 5u;
    constexpr std::uint32_t height = 4u;
    std::vector<float> xMap(width * height, 0.0f);
    std::vector<float> yMap(width * height, 0.0f);

    const an::RegistrationTransform scaled =
        linearTransform(an::RegistrationTransformKind::Affine, {2.0, 0.0, 1.0, 0.0, 2.0, -1.0, 0.0, 0.0, 1.0});
    an::RegistrationMapRequest request;
    request.width = width;
    request.height = height;
    request.xMap = xMap;
    request.yMap = yMap;

    const an::Result<an::RegistrationMapReport> report =
        an::computeRegistrationSourceToDestinationMap(execution, scaled, nullptr, request);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->mapped, static_cast<std::uint64_t>(width) * height);
    for (std::uint32_t row = 0u; row < height; ++row)
    {
        for (std::uint32_t column = 0u; column < width; ++column)
        {
            const std::size_t index = static_cast<std::size_t>(row) * width + column;
            EXPECT_NEAR(xMap[index], static_cast<float>(2.0 * (column + 0.5) + 1.0), 1.0e-4f);
            EXPECT_NEAR(yMap[index], static_cast<float>(2.0 * (row + 0.5) - 1.0), 1.0e-4f);
        }
    }
}

TEST(RegistrationPlanTest, ACoordinateMapAgreesWithThePointwiseInverseAndWithItselfAtEveryThreadCount)
{
    const an::EngineContext engine = makeEngine();
    constexpr std::uint32_t width = 40u;
    constexpr std::uint32_t height = 33u;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;

    an::RegistrationTransformRequest bent;
    bent.linearKind = an::RegistrationTransformKind::Affine;
    bent.sourceToReference = {1.01, -0.02, 3.0, 0.03, 0.99, -1.5, 0.0, 0.0, 1.0};
    bent.polynomial.order = 2u;
    bent.polynomial.x[1] = 1.0;
    bent.polynomial.y[2] = 1.0;
    bent.polynomial.x[3] = 0.02;
    bent.polynomial.y[5] = -0.015;
    bent.polynomial.normalizationScale = {64.0, 64.0};
    const an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(bent);
    ASSERT_TRUE(transform.hasValue()) << transform.status().message();

    const auto build = [&](const std::uint32_t concurrency, const an::RegistrationInverseSeed seed,
                           std::vector<float>& xMap, std::vector<float>& yMap) {
        const an::ExecutionContext execution = makeExecution(engine, concurrency);
        xMap.assign(pixels, 0.0f);
        yMap.assign(pixels, 0.0f);
        an::RegistrationMapRequest request;
        request.width = width;
        request.height = height;
        request.xMap = xMap;
        request.yMap = yMap;
        request.seed = seed;
        const an::Result<an::RegistrationMapReport> report =
            an::computeRegistrationDestinationToSourceMap(execution, *transform, nullptr, request);
        EXPECT_TRUE(report.hasValue()) << report.status().message();
        return report.hasValue() ? *report : an::RegistrationMapReport{};
    };

    std::vector<float> oneX;
    std::vector<float> oneY;
    std::vector<float> manyX;
    std::vector<float> manyY;
    const an::RegistrationMapReport single = build(1u, an::RegistrationInverseSeed::AuthoritativeSeed, oneX, oneY);
    const an::RegistrationMapReport parallel = build(8u, an::RegistrationInverseSeed::AuthoritativeSeed, manyX, manyY);
    EXPECT_EQ(oneX, manyX);
    EXPECT_EQ(oneY, manyY);
    EXPECT_EQ(single, parallel);
    EXPECT_GT(single.newtonIterations, 0u);

    for (std::uint32_t row = 0u; row < height; row += 7u)
    {
        for (std::uint32_t column = 0u; column < width; column += 5u)
        {
            const an::Result<an::RegistrationInverseResult> expected =
                an::mapRegistrationPointInverse(*transform, {.x = column + 0.5, .y = row + 0.5});
            ASSERT_TRUE(expected.hasValue()) << expected.status().message();
            const std::size_t index = static_cast<std::size_t>(row) * width + column;
            EXPECT_NEAR(oneX[index], static_cast<float>(expected->pixel.x), 1.0e-3f);
            EXPECT_NEAR(oneY[index], static_cast<float>(expected->pixel.y), 1.0e-3f);
        }
    }

    std::vector<float> warmX;
    std::vector<float> warmY;
    const an::RegistrationMapReport warm = build(4u, an::RegistrationInverseSeed::RowNeighbourWarmStart, warmX, warmY);
    EXPECT_GT(warm.warmSeedAccepted, 0u);
    EXPECT_EQ(warm.mapped, single.mapped);
    for (std::size_t index = 0u; index < pixels; ++index)
    {
        EXPECT_NEAR(warmX[index], oneX[index], 1.0e-3f);
        EXPECT_NEAR(warmY[index], oneY[index], 1.0e-3f);
    }
}

// A projective matrix whose last row is 0 0 1 divides every position by exactly one, and an affine
// map may skip that division only because it changes nothing. Declaring the same numbers both ways
// and requiring the same bits from the forward direction is what holds the affine path to it. The
// Newton solve of an inverse applies that same function to the forward matrix at every step. The
// inverse is not compared across the two kinds, because they invert their linear part by different
// formulas - a two-by-two inverse and a three-by-three adjugate - which round differently.
TEST(RegistrationPlanTest, AnAffineModelMapsForwardToTheBitsOfTheSameMatrixDeclaredProjective)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine, 4u);
    constexpr std::uint32_t width = 41u;
    constexpr std::uint32_t height = 29u;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const an::PixelCoordinate postTranslation{.x = 0.37, .y = -1.25};

    for (const std::uint32_t order : {0u, 2u, 3u})
    {
        an::RegistrationTransformRequest affine;
        affine.linearKind = an::RegistrationTransformKind::Affine;
        affine.sourceToReference = {1.013, -0.021, 3.17, 0.029, 0.987, -1.53, 0.0, 0.0, 1.0};
        if (order > 0u)
        {
            affine.polynomial.order = order;
            affine.polynomial.x[1] = 1.0;
            affine.polynomial.y[2] = 1.0;
            affine.polynomial.x[3] = 0.021;
            affine.polynomial.y[5] = -0.017;
            affine.polynomial.x[4] = 0.0031;
            affine.polynomial.normalizationOrigin = {19.5, 13.25};
            affine.polynomial.normalizationScale = {47.0, 31.0};
        }
        an::RegistrationTransformRequest projective = affine;
        projective.linearKind = an::RegistrationTransformKind::Projective;
        const an::Result<an::RegistrationTransform> affineModel = an::RegistrationTransform::create(affine);
        const an::Result<an::RegistrationTransform> projectiveModel = an::RegistrationTransform::create(projective);
        ASSERT_TRUE(affineModel.hasValue()) << affineModel.status().message();
        ASSERT_TRUE(projectiveModel.hasValue()) << projectiveModel.status().message();

        for (std::uint32_t row = 0u; row < height; row += 3u)
        {
            for (std::uint32_t column = 0u; column < width; column += 4u)
            {
                const an::PixelCoordinate position{.x = column + 0.25, .y = row - 0.75};
                const an::Result<an::PixelCoordinate> forwardA =
                    an::mapRegistrationPoint(*affineModel, position, postTranslation);
                const an::Result<an::PixelCoordinate> forwardP =
                    an::mapRegistrationPoint(*projectiveModel, position, postTranslation);
                ASSERT_TRUE(forwardA.hasValue() && forwardP.hasValue());
                EXPECT_EQ(std::memcmp(&*forwardA, &*forwardP, sizeof(an::PixelCoordinate)), 0)
                    << "order " << order << " at " << column << ',' << row;
            }
        }

        const auto map = [&](const an::RegistrationTransform& model, std::vector<float>& xMap,
                             std::vector<float>& yMap) {
            xMap.assign(pixels, 0.0f);
            yMap.assign(pixels, 0.0f);
            an::RegistrationMapRequest request;
            request.width = width;
            request.height = height;
            request.xMap = xMap;
            request.yMap = yMap;
            request.postTranslation = postTranslation;
            const an::Result<an::RegistrationMapReport> report =
                an::computeRegistrationSourceToDestinationMap(execution, model, nullptr, request);
            EXPECT_TRUE(report.hasValue()) << report.status().message();
            return report.hasValue() ? *report : an::RegistrationMapReport{};
        };
        std::vector<float> affineX;
        std::vector<float> affineY;
        std::vector<float> projectiveX;
        std::vector<float> projectiveY;
        const an::RegistrationMapReport affineReport = map(*affineModel, affineX, affineY);
        const an::RegistrationMapReport projectiveReport = map(*projectiveModel, projectiveX, projectiveY);
        EXPECT_EQ(affineReport, projectiveReport) << "order " << order;
        EXPECT_EQ(affineReport.mapped, pixels);
        EXPECT_EQ(std::memcmp(affineX.data(), projectiveX.data(), pixels * sizeof(float)), 0) << "order " << order;
        EXPECT_EQ(std::memcmp(affineY.data(), projectiveY.data(), pixels * sizeof(float)), 0) << "order " << order;
    }
}

TEST(RegistrationPlanTest, ACoordinateMapUndoesTheFramesOwnDistortionLast)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t width = 8u;
    constexpr std::uint32_t height = 8u;
    std::vector<float> xMap(width * height, 0.0f);
    std::vector<float> yMap(width * height, 0.0f);

    an::SipDistortionRequest distortionRequest;
    distortionRequest.referencePixel = {.x = 4.0, .y = 4.0};
    distortionRequest.forwardX.order = 2u;
    distortionRequest.forwardX.coefficients[2][0] = 1.0e-3;
    distortionRequest.forwardY.order = 2u;
    distortionRequest.forwardY.coefficients[0][2] = 2.0e-3;
    const an::Result<an::SipDistortion> distortion = an::SipDistortion::create(distortionRequest);
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();

    an::RegistrationMapRequest request;
    request.width = width;
    request.height = height;
    request.xMap = xMap;
    request.yMap = yMap;

    const an::RegistrationTransform identity;
    const an::Result<an::RegistrationMapReport> report =
        an::computeRegistrationDestinationToSourceMap(execution, identity, &distortion.value(), request);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->mapped, static_cast<std::uint64_t>(width) * height);

    for (std::uint32_t row = 0u; row < height; ++row)
    {
        for (std::uint32_t column = 0u; column < width; ++column)
        {
            const an::Result<an::SipInverseResult> expected =
                an::applySipInverse(*distortion, {.x = column + 0.5, .y = row + 0.5});
            ASSERT_TRUE(expected.hasValue()) << expected.status().message();
            const std::size_t index = static_cast<std::size_t>(row) * width + column;
            EXPECT_NEAR(xMap[index], static_cast<float>(expected->pixel.x), 1.0e-4f);
            EXPECT_NEAR(yMap[index], static_cast<float>(expected->pixel.y), 1.0e-4f);
        }
    }
}

TEST(RegistrationPlanTest, ACoordinateMapRefusesAnUnusableGrid)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> xMap(16u, 0.0f);
    std::vector<float> yMap(16u, 0.0f);
    const an::RegistrationTransform identity;

    an::RegistrationMapRequest zero;
    zero.width = 0u;
    zero.height = 4u;
    zero.xMap = xMap;
    zero.yMap = yMap;
    const an::Result<an::RegistrationMapReport> noExtent =
        an::computeRegistrationDestinationToSourceMap(execution, identity, nullptr, zero);
    ASSERT_FALSE(noExtent.hasValue());
    EXPECT_EQ(noExtent.status().code(), an::ErrorCode::InvalidArgument);

    an::RegistrationMapRequest wrongLength;
    wrongLength.width = 4u;
    wrongLength.height = 4u;
    wrongLength.xMap = std::span<float>(xMap).first(8u);
    wrongLength.yMap = yMap;
    const an::Result<an::RegistrationMapReport> shortPlane =
        an::computeRegistrationDestinationToSourceMap(execution, identity, nullptr, wrongLength);
    ASSERT_FALSE(shortPlane.hasValue());
    EXPECT_EQ(shortPlane.status().code(), an::ErrorCode::IncompatibleArguments);

    an::RegistrationMapRequest aliased;
    aliased.width = 4u;
    aliased.height = 4u;
    aliased.xMap = xMap;
    aliased.yMap = xMap;
    const an::Result<an::RegistrationMapReport> overlapping =
        an::computeRegistrationDestinationToSourceMap(execution, identity, nullptr, aliased);
    ASSERT_FALSE(overlapping.hasValue());
    EXPECT_EQ(overlapping.status().code(), an::ErrorCode::Aliasing);

    an::RegistrationMapRequest unknownSeed;
    unknownSeed.width = 4u;
    unknownSeed.height = 4u;
    unknownSeed.xMap = xMap;
    unknownSeed.yMap = yMap;
    unknownSeed.seed = static_cast<an::RegistrationInverseSeed>(9);
    const an::Result<an::RegistrationMapReport> badSeed =
        an::computeRegistrationDestinationToSourceMap(execution, identity, nullptr, unknownSeed);
    ASSERT_FALSE(badSeed.hasValue());
    EXPECT_EQ(badSeed.status().code(), an::ErrorCode::InvalidArgument);

    an::RegistrationMapRequest fine;
    fine.width = 4u;
    fine.height = 4u;
    fine.xMap = xMap;
    fine.yMap = yMap;
    const an::Result<an::RegistrationMapReport> noContext =
        an::computeRegistrationDestinationToSourceMap(an::ExecutionContext{}, identity, nullptr, fine);
    ASSERT_FALSE(noContext.hasValue());
    EXPECT_EQ(noContext.status().code(), an::ErrorCode::NotInitialized);
}

TEST(RegistrationPlanTest, AWholePixelPlacementCopiesTheFrameOntoTheCanvas)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor source = descriptorOf(4u, 3u);
    const an::ImageLayout sourceLayout = layoutOf(source);
    const an::ImageLayout destinationLayout = layoutOf(descriptorOf(8u, 6u));

    std::vector<float> samples(12u, 0.0f);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>(index + 1u);
    }
    std::vector<float> canvas(48u, -1.0f);

    const an::RegistrationTransform identity;
    const an::Result<an::RegistrationWarpPlan> planned = an::planRegistrationWarp(
        source, identity, {}, {.width = 8u, .height = 6u, .canvasOrigin = {.x = 2.0, .y = 1.0}});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->route(), an::RegistrationSamplingRoute::DirectRectangle);
    EXPECT_FALSE(planned->needsCoordinateMap());
    EXPECT_EQ(planned->destinationDescriptor().extent().width(), 8u);

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, destinationLayout.extent());
    ASSERT_TRUE(mask.hasValue()) << mask.status().message();
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    const an::Result<an::RegistrationWarpResult> warped =
        an::warpRegisteredImage(execution, an::SampleType::Float32, samples.data(), sourceLayout, canvas.data(),
                                destinationLayout, *planned, {}, maskView);
    ASSERT_TRUE(warped.hasValue()) << warped.status().message();
    EXPECT_EQ(warped->route, an::RegistrationSamplingRoute::DirectRectangle);
    EXPECT_EQ(warped->run.outputsWritten, 12u);

    for (std::uint32_t row = 0u; row < 6u; ++row)
    {
        for (std::uint32_t column = 0u; column < 8u; ++column)
        {
            const float value = canvas[static_cast<std::size_t>(row) * 8u + column];
            const bool inside = column >= 2u && column < 6u && row >= 1u && row < 4u;
            if (inside)
            {
                EXPECT_FLOAT_EQ(value, samples[static_cast<std::size_t>(row - 1u) * 4u + (column - 2u)]);
            }
            else
            {
                EXPECT_FLOAT_EQ(value, -1.0f);
            }
        }
    }
}

TEST(RegistrationPlanTest, WarpValidationIsAtomicForSampleSizesAliasingAndCancellation)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, 1u, stop.get_token());
    const auto descriptor = descriptorOf(4u, 1u);
    const auto layout = layoutOf(descriptor);
    const auto byteLayout = layoutOf(descriptorOf(4u, 1u, 1u, an::SampleType::UInt8));
    std::vector<float> source{1.f, 2.f, 3.f, 4.f, 5.f};
    std::vector<float> destination(8u, -1.f);
    auto mask = an::ValidityMask::create(engine, descriptor.extent());
    ASSERT_TRUE(mask.hasValue());
    auto bits = outputMask(*mask);
    bits.fill(true);
    for (const auto& transform : {an::RegistrationTransform{}, identityPolynomialTransform()})
    {
        const auto plan = an::planRegistrationWarp(descriptor, transform, {}, {.width = 4u, .height = 1u});
        ASSERT_TRUE(plan.hasValue());
        for (const bool wrongSource : {false, true})
        {
            const auto result = an::warpRegisteredImage(execution, an::SampleType::Float32, source.data(),
                                                        wrongSource ? byteLayout : layout, destination.data(),
                                                        wrongSource ? layout : byteLayout, *plan, {}, bits);
            EXPECT_EQ(result.code(), an::ErrorCode::IncompatibleArguments);
            EXPECT_EQ(destination, std::vector<float>(8u, -1.f));
            EXPECT_TRUE(bits.test(0u, 0u, 0u).value());
        }
        for (const std::size_t offset : {0u, 1u})
        {
            const auto result = an::warpRegisteredImage(execution, an::SampleType::Float32, source.data(), layout,
                                                        source.data() + offset, layout, *plan, {}, bits);
            EXPECT_EQ(result.code(), an::ErrorCode::Aliasing);
            EXPECT_EQ(source, (std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f}));
        }
    }
    const auto plan = an::planRegistrationWarp(descriptor, {}, {}, {.width = 4u, .height = 1u});
    stop.request_stop();
    EXPECT_EQ(an::warpRegisteredImage(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                      layout, *plan, {}, bits)
                  .code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(destination, std::vector<float>(8u, -1.f));
    EXPECT_TRUE(bits.test(0u, 0u, 0u).value());
}

TEST(RegistrationPlanTest, DirectAndMappedWarpsRespectBothChannelArrangements)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const auto descriptor = descriptorOf(4u, 2u, 3u);
    for (const auto& transform : {an::RegistrationTransform{}, identityPolynomialTransform()})
    {
        const auto plan = an::planRegistrationWarp(descriptor, transform, {}, {.width = 4u, .height = 2u});
        ASSERT_TRUE(plan.hasValue());
        for (const auto sourceArrangement : {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved})
        {
            for (const auto destinationArrangement :
                 {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved})
            {
                const auto sourceLayout = layoutOf(descriptor, sourceArrangement);
                const auto destinationLayout = layoutOf(descriptor, destinationArrangement);
                std::vector<float> source(24u), destination(24u, -1.f);
                for (std::size_t pixel = 0u; pixel < 8u; ++pixel)
                {
                    for (std::size_t channel = 0u; channel < 3u; ++channel)
                    {
                        source[sourceArrangement == an::StorageArrangement::Planar ? channel * 8u + pixel
                                                                                   : pixel * 3u + channel] =
                            static_cast<float>(channel * 10u + pixel + 1u);
                    }
                }
                auto mask = an::ValidityMask::create(engine, descriptor.extent());
                ASSERT_TRUE(mask.hasValue());
                const auto result =
                    an::warpRegisteredImage(execution, an::SampleType::Float32, source.data(), sourceLayout,
                                            destination.data(), destinationLayout, *plan, {}, outputMask(*mask));
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                EXPECT_EQ(result->run.outputsWritten, 24u);
                for (std::size_t pixel = 0u; pixel < 8u; ++pixel)
                {
                    for (std::size_t channel = 0u; channel < 3u; ++channel)
                    {
                        EXPECT_FLOAT_EQ(destination[destinationArrangement == an::StorageArrangement::Planar
                                                        ? channel * 8u + pixel
                                                        : pixel * 3u + channel],
                                        static_cast<float>(channel * 10u + pixel + 1u));
                    }
                }
            }
        }
    }
}

TEST(RegistrationPlanTest, DirectWarpHonorsValueValidityWithoutResurrectingNoData)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto descriptor = descriptorOf(3u, 1u);
    const auto layout = layoutOf(descriptor);
    const auto plan = an::planRegistrationWarp(descriptor, {}, {}, {.width = 3u, .height = 1u});
    const std::vector<float> source{0.f, .5f, std::numeric_limits<float>::quiet_NaN()};
    for (const auto rule : {an::ValidityRule::ZeroIsNoData, an::ValidityRule::FiniteOnly})
    {
        std::vector<float> destination(3u, -1.f);
        auto mask = an::ValidityMask::create(engine, descriptor.extent());
        const auto validity = an::ValidityDescriptor::create({.rule = rule});
        const auto result =
            an::warpRegisteredImage(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                    layout, *plan, {.descriptor = *validity}, outputMask(*mask));
        ASSERT_TRUE(result.hasValue());
        const auto rejected = rule == an::ValidityRule::ZeroIsNoData ? 0u : 2u;
        EXPECT_FALSE(outputMask(*mask).test(0u, rejected, 0u).value());
        EXPECT_FLOAT_EQ(destination[rejected], -1.f);
        EXPECT_TRUE(outputMask(*mask).test(0u, 1u, 0u).value());
    }
}

TEST(RegistrationPlanTest, WarpsReadEachMaskPlaneAndIntersectSharedOutputValidity)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto descriptor = descriptorOf(4u, 2u, 3u);
    auto inputMask = an::ValidityMask::create(engine, descriptor.extent());
    ASSERT_TRUE(inputMask.hasValue());
    auto inputBits = outputMask(*inputMask);
    inputBits.fill(true);
    ASSERT_TRUE(inputBits.setValid(1u, 1u, 0u, false));
    ASSERT_TRUE(inputBits.setValid(2u, 2u, 1u, false));
    const auto borrowed = inputBits.constView();
    const auto validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    for (const auto& transform : {an::RegistrationTransform{}, identityPolynomialTransform()})
    {
        const auto plan = an::planRegistrationWarp(descriptor, transform, {}, {.width = 4u, .height = 2u});
        for (const auto arrangement : {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved})
        {
            const auto layout = layoutOf(descriptor, arrangement);
            const std::vector<float> source(24u, .5f);
            for (const bool shared : {false, true})
            {
                auto mask = an::ValidityMask::create(engine, descriptorOf(4u, 2u, shared ? 1u : 3u).extent());
                std::vector<float> destination(24u, -1.f);
                const auto result = an::warpRegisteredImage(
                    execution, an::SampleType::Float32, source.data(), layout, destination.data(), layout, *plan,
                    {.descriptor = *validity, .mask = &borrowed}, outputMask(*mask));
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                EXPECT_EQ(result->run.outputsWritten, 22u);
                const auto bits = outputMask(*mask);
                EXPECT_FALSE(bits.test(shared ? 0u : 1u, 1u, 0u).value());
                EXPECT_FALSE(bits.test(shared ? 0u : 2u, 2u, 1u).value());
                if (!shared)
                {
                    EXPECT_TRUE(bits.test(0u, 1u, 0u).value());
                    EXPECT_TRUE(bits.test(1u, 2u, 1u).value());
                }
                EXPECT_FLOAT_EQ(destination[arrangement == an::StorageArrangement::Planar ? 9u : 4u], -1.f);
                EXPECT_FLOAT_EQ(destination[arrangement == an::StorageArrangement::Planar ? 22u : 20u], -1.f);
            }
        }
    }
}

TEST(RegistrationPlanTest, ALinearWarpResamplesThroughTheGeometryPlan)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor source = descriptorOf(8u, 2u);
    const an::ImageLayout layout = layoutOf(source);

    std::vector<float> samples(16u, 0.0f);
    for (std::uint32_t row = 0u; row < 2u; ++row)
    {
        for (std::uint32_t column = 0u; column < 8u; ++column)
        {
            samples[static_cast<std::size_t>(row) * 8u + column] = static_cast<float>(column);
        }
    }
    std::vector<float> canvas(16u, -1.0f);

    // Half a pixel to the right: every destination centre reads exactly between two source centres.
    const an::RegistrationTransform shifted =
        linearTransform(an::RegistrationTransformKind::Affine, {1.0, 0.0, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0});
    const an::Result<an::RegistrationWarpPlan> planned =
        an::planRegistrationWarp(source, shifted, {}, {.width = 8u, .height = 2u});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->route(), an::RegistrationSamplingRoute::Warp);
    EXPECT_FALSE(planned->needsCoordinateMap());

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
    ASSERT_TRUE(mask.hasValue()) << mask.status().message();
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    const an::Result<an::RegistrationWarpResult> warped = an::warpRegisteredImage(
        execution, an::SampleType::Float32, samples.data(), layout, canvas.data(), layout, *planned, {}, maskView);
    ASSERT_TRUE(warped.hasValue()) << warped.status().message();

    for (std::uint32_t column = 1u; column < 8u; ++column)
    {
        EXPECT_NEAR(canvas[column], static_cast<float>(column) - 0.5f, 1.0e-5f);
    }
}

TEST(RegistrationPlanTest, ACoordinateMapWarpReproducesAFrameItDoesNotMove)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor source = descriptorOf(16u, 12u);
    const an::ImageLayout layout = layoutOf(source);

    std::vector<float> samples(16u * 12u, 0.0f);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>((index * 7u) % 251u) * 0.125f;
    }
    std::vector<float> canvas(samples.size(), -1.0f);

    const an::RegistrationTransform polynomial = identityPolynomialTransform();
    const an::Result<an::RegistrationWarpPlan> planned =
        an::planRegistrationWarp(source, polynomial, {}, {.width = 16u, .height = 12u});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_TRUE(planned->needsCoordinateMap());

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
    ASSERT_TRUE(mask.hasValue()) << mask.status().message();
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    const an::Result<an::RegistrationWarpResult> warped = an::warpRegisteredImage(
        execution, an::SampleType::Float32, samples.data(), layout, canvas.data(), layout, *planned, {}, maskView);
    ASSERT_TRUE(warped.hasValue()) << warped.status().message();
    EXPECT_EQ(warped->route, an::RegistrationSamplingRoute::Warp);
    EXPECT_EQ(warped->mapping.mapped, samples.size());
    // The band holds coordinates, the two coordinate planes and the gathered samples, and this
    // frame is short enough that one band covers it.
    EXPECT_GT(warped->workingBytes, 0u);
    const std::size_t payloadBytes = samples.size() * (sizeof(an::KernelCoordinate) + 3u * sizeof(float));
    // A checked standard-library build asks the polymorphic resource for small iterator-proxy
    // allocations as well. The report deliberately counts those real context bytes, so require the
    // four image-sized buffers without assuming a particular standard-library bookkeeping policy.
    EXPECT_GE(warped->workingBytes, payloadBytes);

    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        EXPECT_NEAR(canvas[index], samples[index], 1.0e-4f);
    }
}

TEST(RegistrationPlanTest, ATallCanvasWarpsInBandsRatherThanMappingItAllAtOnce)
{
    const an::EngineContext engine = makeEngine(8u, 512u * 1024u * 1024u);
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t width = 2048u;
    constexpr std::uint32_t height = 512u;
    const an::ImageDescriptor source = descriptorOf(width, height);
    const an::ImageLayout layout = layoutOf(source);

    // The pattern varies down the canvas, so a band that mapped the wrong rows would show up as
    // repeated or shifted content rather than as the same number everywhere.
    std::vector<float> samples(static_cast<std::size_t>(width) * height, 0.0f);
    for (std::uint32_t row = 0u; row < height; ++row)
    {
        for (std::uint32_t column = 0u; column < width; ++column)
        {
            samples[static_cast<std::size_t>(row) * width + column] =
                static_cast<float>(row) + static_cast<float>(column) * 0.001f;
        }
    }
    std::vector<float> canvas(samples.size(), -1.0f);

    const an::Result<an::RegistrationWarpPlan> planned =
        an::planRegistrationWarp(source, identityPolynomialTransform(), {}, {.width = width, .height = height});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    ASSERT_TRUE(planned->needsCoordinateMap());

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
    ASSERT_TRUE(mask.hasValue()) << mask.status().message();
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    const an::Result<an::RegistrationWarpResult> warped = an::warpRegisteredImage(
        execution, an::SampleType::Float32, samples.data(), layout, canvas.data(), layout, *planned, {}, maskView);
    ASSERT_TRUE(warped.hasValue()) << warped.status().message();
    EXPECT_EQ(warped->mapping.mapped, samples.size());
    EXPECT_EQ(warped->run.outputsWritten, samples.size());

    // Whatever the canvas is, one band's memory is what the warp holds.
    const std::uint64_t wholeCanvas =
        static_cast<std::uint64_t>(samples.size()) * (sizeof(an::KernelCoordinate) + 3u * sizeof(float));
    EXPECT_LE(warped->workingBytes, 4u * 1024u * 1024u);
    EXPECT_LT(warped->workingBytes * 4u, wholeCanvas);

    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        ASSERT_NEAR(canvas[index], samples[index], 1.0e-3f) << "at index " << index;
    }
}

TEST(RegistrationPlanTest, ACoordinateMapWarpRefusesARepresentationTheGatherCannotRead)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor source = descriptorOf(8u, 8u, 1u, an::SampleType::UInt16);
    const an::ImageLayout layout = layoutOf(source);
    std::vector<std::uint16_t> samples(64u, 0u);
    std::vector<std::uint16_t> canvas(64u, 0u);

    const an::Result<an::RegistrationWarpPlan> planned =
        an::planRegistrationWarp(source, identityPolynomialTransform(), {}, {.width = 8u, .height = 8u});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, layout.extent());
    ASSERT_TRUE(mask.hasValue()) << mask.status().message();
    an::ValidityMaskView<std::uint8_t> maskView = outputMask(*mask);
    const an::Result<an::RegistrationWarpResult> warped = an::warpRegisteredImage(
        execution, an::SampleType::UInt16, samples.data(), layout, canvas.data(), layout, *planned, {}, maskView);
    ASSERT_FALSE(warped.hasValue());
    EXPECT_EQ(warped.status().code(), an::ErrorCode::Unsupported);
}

TEST(RegistrationPlanTest, PlanningRefusesAnUnusableRequest)
{
    const an::ImageDescriptor source = descriptorOf(8u, 8u);
    const an::RegistrationTransform identity;

    const an::Result<an::RegistrationWarpPlan> noExtent =
        an::planRegistrationWarp(source, identity, {}, {.width = 0u, .height = 8u});
    ASSERT_FALSE(noExtent.hasValue());
    EXPECT_EQ(noExtent.status().code(), an::ErrorCode::InvalidArgument);

    an::RegistrationWarpRequest unknownSeed;
    unknownSeed.seed = static_cast<an::RegistrationInverseSeed>(6);
    const an::Result<an::RegistrationWarpPlan> badSeed = an::planRegistrationWarp(source, identity, {}, unknownSeed);
    ASSERT_FALSE(badSeed.hasValue());
    EXPECT_EQ(badSeed.status().code(), an::ErrorCode::InvalidArgument);

    an::RegistrationWarpRequest notFinite;
    notFinite.canvasOrigin = {.x = std::numeric_limits<double>::quiet_NaN(), .y = 0.0};
    const an::Result<an::RegistrationWarpPlan> badOrigin = an::planRegistrationWarp(source, identity, {}, notFinite);
    ASSERT_FALSE(badOrigin.hasValue());
    EXPECT_EQ(badOrigin.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(RegistrationPlanTest, ATransformIsDescribedAsAMatrixWithoutCopyingIt)
{
    const an::RegistrationTransform affine =
        linearTransform(an::RegistrationTransformKind::Affine, {1.5, 0.25, 10.0, -0.25, 1.5, -4.0, 0.0, 0.0, 1.0});
    EXPECT_EQ(an::naturalRegistrationMatrixShape(affine.kind()), an::RegistrationMatrixShape::AffineTwoByThree);
    EXPECT_EQ(an::registrationMatrixRowCount(an::RegistrationMatrixShape::AffineTwoByThree), 2);
    EXPECT_EQ(an::registrationMatrixRowCount(an::RegistrationMatrixShape::ProjectiveThreeByThree), 3);
    EXPECT_EQ(an::registrationMatrixRowCount(static_cast<an::RegistrationMatrixShape>(5)), 0);

    const std::optional<an::OpenCvMatDescription> described = an::describeRegistrationOpenCvMatrix(
        affine, an::RegistrationMatrixShape::AffineTwoByThree, an::kOpenCvTypeLayout4);
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->rows, 2);
    EXPECT_EQ(described->cols, 3);
    EXPECT_EQ(described->stepBytes, 3u * sizeof(double));
    EXPECT_EQ(described->data, static_cast<const void*>(affine.matrix().data()));

    const std::optional<an::OpenCvMatDescription> asThreeByThree = an::describeRegistrationOpenCvMatrix(
        affine, an::RegistrationMatrixShape::ProjectiveThreeByThree, an::kOpenCvTypeLayout4);
    ASSERT_TRUE(asThreeByThree.has_value());
    EXPECT_EQ(asThreeByThree->rows, 3);
    EXPECT_EQ(asThreeByThree->data, described->data);

    const an::RegistrationTransform projective =
        linearTransform(an::RegistrationTransformKind::Projective, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0002, 0.0, 1.0});
    EXPECT_EQ(an::checkRegistrationOpenCvMatrix(projective, an::RegistrationMatrixShape::AffineTwoByThree,
                                                an::kOpenCvTypeLayout4),
              an::BridgeRefusal::ArrangementNotRepresentable);
    EXPECT_FALSE(an::describeRegistrationOpenCvMatrix(projective, an::RegistrationMatrixShape::AffineTwoByThree,
                                                      an::kOpenCvTypeLayout4)
                     .has_value());
    EXPECT_EQ(an::checkRegistrationOpenCvMatrix(identityPolynomialTransform(),
                                                an::RegistrationMatrixShape::ProjectiveThreeByThree,
                                                an::kOpenCvTypeLayout4),
              an::BridgeRefusal::ArrangementNotRepresentable);
    EXPECT_EQ(
        an::checkRegistrationOpenCvMatrix(affine, static_cast<an::RegistrationMatrixShape>(4), an::kOpenCvTypeLayout4),
        an::BridgeRefusal::ArrangementNotRepresentable);
}

TEST(RegistrationPlanTest, WritableMasksMustNotOverlapInputValidity)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto descriptor = descriptorOf(8u, 4u);
    const auto layout = layoutOf(descriptor);
    const std::vector<float> source(32u, 0.25f);
    for (const auto& transform : {an::RegistrationTransform{}, identityPolynomialTransform()})
    {
        const auto plan = an::planRegistrationWarp(descriptor, transform, {}, {.width = 8u, .height = 4u});
        ASSERT_TRUE(plan.hasValue());
        for (const std::size_t offset : {0u, 1u})
        {
            std::array<std::uint8_t, 5u> bits{255u, 255u, 255u, 255u, 255u};
            const auto inputMask = an::ValidityMaskView<const std::uint8_t>::create(
                std::span<const std::uint8_t>(bits.data(), 4u), descriptor.extent(), 1u, 4u);
            const auto output = an::ValidityMaskView<std::uint8_t>::create(
                std::span<std::uint8_t>(bits.data() + offset, 4u), descriptor.extent(), 1u, 4u);
            ASSERT_TRUE(inputMask.has_value());
            ASSERT_TRUE(output.has_value());
            const auto rule = an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
            std::vector<float> destination(32u, -1.0f);
            const auto refused =
                an::warpRegisteredImage(execution, an::SampleType::Float32, source.data(), layout, destination.data(),
                                        layout, *plan, {.descriptor = *rule, .mask = &*inputMask}, *output);
            EXPECT_EQ(refused.code(), an::ErrorCode::Aliasing);
            EXPECT_EQ(destination, std::vector<float>(32u, -1.0f));
            EXPECT_EQ(bits, (std::array<std::uint8_t, 5u>{255u, 255u, 255u, 255u, 255u}));
        }
    }
}
