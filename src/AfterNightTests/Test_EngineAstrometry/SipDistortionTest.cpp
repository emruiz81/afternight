// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the polynomial plate distortion, both its directions, and its maps.
//
// The cases here are mostly about the two things a distortion model gets wrong quietly. The first
// is which position a coefficient is measured from: a model whose reference pixel is a half pixel
// or a whole pixel away from the matrix's still evaluates, still produces smooth output, and is
// wrong everywhere by an amount nobody notices until a stack is soft. The second is the inverse: a
// header that carries no inverse polynomials is common, the Newton solve that stands in for them is
// the expensive path in the whole domain, and a caller cannot tell which route it got unless the
// engine says so - so the route is a returned fact and these cases check it rather than inferring
// it from a timing.

#include "CatalogTestSupport.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace
{

using catalog_test::makeEngine;
using catalog_test::makeExecution;

/// A second-order component whose only nonzero terms are the three quadratic ones.
///
/// The shape a real solver writes: the constant and linear terms are absorbed by the linear matrix,
/// so a writer omits them and the polynomial starts at total degree two.
[[nodiscard]] an::SipPolynomial quadratic(const double uu, const double uv, const double vv)
{
    an::SipPolynomial component;
    component.order = 2u;
    component.coefficients[2][0] = uu;
    component.coefficients[1][1] = uv;
    component.coefficients[0][2] = vv;
    return component;
}

/// A model with forward polynomials only, which is what forces the Newton route.
[[nodiscard]] an::SipDistortion forwardOnlyModel(const an::PixelCoordinate reference = {.x = 511.5, .y = 511.5})
{
    an::SipDistortionRequest request;
    request.referencePixel = reference;
    request.forwardX = quadratic(1.0e-6, -4.0e-7, 2.0e-7);
    request.forwardY = quadratic(3.0e-7, 8.0e-7, -1.2e-6);
    const an::Result<an::SipDistortion> model = an::SipDistortion::create(request);
    EXPECT_TRUE(model.hasValue()) << model.status().message();
    return model.hasValue() ? *model : an::SipDistortion{};
}

TEST(SipDistortionTest, ADefaultModelCorrectsNothingAndSaysSo)
{
    constexpr an::SipDistortion none;

    static_assert(!none.hasForwardModel());
    static_assert(!none.hasInverseModel());

    const an::Result<an::PixelCoordinate> forward = an::applySipForward(none, {.x = 12.25, .y = -3.5});
    ASSERT_TRUE(forward.hasValue()) << forward.status().message();
    EXPECT_EQ(*forward, (an::PixelCoordinate{.x = 12.25, .y = -3.5}));

    const an::Result<an::SipInverseResult> inverse = an::applySipInverse(none, {.x = 12.25, .y = -3.5});
    ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
    EXPECT_EQ(inverse->route, an::SipInverseRoute::Identity);
    EXPECT_EQ(inverse->pixel, (an::PixelCoordinate{.x = 12.25, .y = -3.5}));
    EXPECT_EQ(inverse->iterations, 0u);
    EXPECT_TRUE(std::isnan(inverse->residualPixels));
}

TEST(SipDistortionTest, TheOrderAndTheCoefficientsAreChecked)
{
    an::SipDistortionRequest request;
    request.referencePixel = {.x = 100.5, .y = 100.5};

    request.forwardX.order = 1u;
    EXPECT_EQ(an::SipDistortion::create(request).code(), an::ErrorCode::InvalidArgument);

    request.forwardX.order = an::kMaximumSipOrder + 1u;
    EXPECT_EQ(an::SipDistortion::create(request).code(), an::ErrorCode::InvalidArgument);

    request.forwardX = quadratic(1.0e-6, 0.0, 0.0);
    request.forwardX.coefficients[0][0] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::SipDistortion::create(request).code(), an::ErrorCode::InvalidArgument);

    // A coefficient of a degree the order excludes would be silently dropped by every evaluation,
    // so it is refused rather than ignored.
    request.forwardX = quadratic(1.0e-6, 0.0, 0.0);
    request.forwardX.coefficients[3][0] = 5.0e-9;
    EXPECT_EQ(an::SipDistortion::create(request).code(), an::ErrorCode::InvalidArgument);

    request.forwardX = quadratic(1.0e-6, 0.0, 0.0);
    request.referencePixel.x = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::SipDistortion::create(request).code(), an::ErrorCode::InvalidArgument);
}

TEST(SipDistortionTest, TheForwardCorrectionIsTheStatedPolynomialInOffsetsFromTheReferencePixel)
{
    const an::PixelCoordinate reference{.x = 511.5, .y = 511.5};
    an::SipDistortionRequest request;
    request.referencePixel = reference;
    request.forwardX = quadratic(1.0e-6, -4.0e-7, 2.0e-7);
    request.forwardY = quadratic(3.0e-7, 8.0e-7, -1.2e-6);
    const an::Result<an::SipDistortion> model = an::SipDistortion::create(request);
    ASSERT_TRUE(model.hasValue()) << model.status().message();

    const an::PixelCoordinate probe{.x = 900.5, .y = 120.5};
    const double u = probe.x - reference.x;
    const double v = probe.y - reference.y;
    const double expectedX = probe.x + (1.0e-6 * u * u) + (-4.0e-7 * u * v) + (2.0e-7 * v * v);
    const double expectedY = probe.y + (3.0e-7 * u * u) + (8.0e-7 * u * v) + (-1.2e-6 * v * v);

    const an::Result<an::PixelCoordinate> corrected = an::applySipForward(*model, probe);
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_DOUBLE_EQ(corrected->x, expectedX);
    EXPECT_DOUBLE_EQ(corrected->y, expectedY);

    // The reference pixel itself has zero offset, so a polynomial with no constant term leaves it
    // exactly where it was. This is the case a wrong pixel convention breaks first.
    const an::Result<an::PixelCoordinate> atReference = an::applySipForward(*model, reference);
    ASSERT_TRUE(atReference.hasValue()) << atReference.status().message();
    EXPECT_EQ(*atReference, reference);
}

TEST(SipDistortionTest, TheInversePolynomialsAreUsedWhenTheyAreThere)
{
    an::SipDistortionRequest request;
    request.referencePixel = {.x = 511.5, .y = 511.5};
    request.forwardX = quadratic(1.0e-6, 0.0, 0.0);
    request.forwardY = quadratic(0.0, 0.0, -1.0e-6);
    request.inverseX = quadratic(-1.0e-6, 0.0, 0.0);
    request.inverseY = quadratic(0.0, 0.0, 1.0e-6);
    const an::Result<an::SipDistortion> model = an::SipDistortion::create(request);
    ASSERT_TRUE(model.hasValue()) << model.status().message();
    EXPECT_TRUE(model->hasInverseModel());

    const an::Result<an::SipInverseResult> inverse = an::applySipInverse(*model, {.x = 800.5, .y = 300.5});
    ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
    EXPECT_EQ(inverse->route, an::SipInverseRoute::InverseCoefficients);
    EXPECT_EQ(inverse->iterations, 0u);
    // The written inverse is an approximation of the true one, so its round trip is measured and
    // reported rather than assumed to be zero.
    ASSERT_FALSE(std::isnan(inverse->residualPixels));
    EXPECT_LT(inverse->residualPixels, 1.0e-2);
}

TEST(SipDistortionTest, TheForwardModelIsSolvedByNewtonWhenNoInverseIsWritten)
{
    const an::SipDistortion model = forwardOnlyModel();
    ASSERT_TRUE(model.hasForwardModel());
    ASSERT_FALSE(model.hasInverseModel());

    for (const an::PixelCoordinate probe :
         {an::PixelCoordinate{.x = 0.5, .y = 0.5}, an::PixelCoordinate{.x = 511.5, .y = 511.5},
          an::PixelCoordinate{.x = 1023.5, .y = 1023.5}, an::PixelCoordinate{.x = 1023.5, .y = 0.5}})
    {
        const an::Result<an::SipInverseResult> inverse = an::applySipInverse(model, probe);
        ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
        EXPECT_EQ(inverse->route, an::SipInverseRoute::NewtonIteration);
        EXPECT_GE(inverse->iterations, 1u);
        EXPECT_LE(inverse->iterations, an::kMaximumSipNewtonIterations);
        EXPECT_LE(inverse->residualPixels, an::kSipInverseTolerancePixels);

        // The property that matters: the solved position, run forward, lands back on the request.
        const an::Result<an::PixelCoordinate> roundTrip = an::applySipForward(model, inverse->pixel);
        ASSERT_TRUE(roundTrip.hasValue()) << roundTrip.status().message();
        EXPECT_NEAR(roundTrip->x, probe.x, an::kSipInverseTolerancePixels);
        EXPECT_NEAR(roundTrip->y, probe.y, an::kSipInverseTolerancePixels);
    }
}

TEST(SipDistortionTest, TheNewtonSolveRefusesAModelThatDoesNotInvertThere)
{
    // A quadratic whose derivative cancels the identity makes the Jacobian singular on a whole
    // line; the correct answer there is a returned refusal, not a position from a divide by nearly
    // zero.
    an::SipDistortionRequest request;
    request.referencePixel = {.x = 0.0, .y = 0.0};
    request.forwardX = quadratic(-0.5, 0.0, 0.0);
    const an::Result<an::SipDistortion> model = an::SipDistortion::create(request);
    ASSERT_TRUE(model.hasValue()) << model.status().message();

    const an::Result<an::SipInverseResult> inverse = an::applySipInverse(*model, {.x = 1.0e6, .y = 0.0});
    ASSERT_FALSE(inverse.hasValue());
    EXPECT_EQ(inverse.code(), an::ErrorCode::InvalidData);
}

TEST(SipDistortionTest, ANonFinitePositionIsRefusedByBothDirections)
{
    const an::SipDistortion model = forwardOnlyModel();
    const an::PixelCoordinate broken{.x = std::numeric_limits<double>::quiet_NaN(), .y = 0.0};

    EXPECT_EQ(an::applySipForward(model, broken).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::applySipInverse(model, broken).code(), an::ErrorCode::InvalidArgument);
}

TEST(SipDistortionTest, TheRouteNamesAreTheEnumeratorsAndAnUnknownValueIsNamedAsOne)
{
    EXPECT_STREQ(an::sipInverseRouteName(an::SipInverseRoute::Identity), "Identity");
    EXPECT_STREQ(an::sipInverseRouteName(an::SipInverseRoute::InverseCoefficients), "InverseCoefficients");
    EXPECT_STREQ(an::sipInverseRouteName(an::SipInverseRoute::NewtonIteration), "NewtonIteration");
    EXPECT_STREQ(an::sipInverseRouteName(static_cast<an::SipInverseRoute>(97)), "UnknownSipInverseRoute");
    EXPECT_TRUE(an::isSipInverseRoute(an::SipInverseRoute::NewtonIteration));
    EXPECT_FALSE(an::isSipInverseRoute(static_cast<an::SipInverseRoute>(97)));
}

TEST(SipDistortionTest, TheUnmappedSentinelIsRecognisedRatherThanCompared)
{
    EXPECT_TRUE(an::isUnmappedCoordinate(an::unmappedCoordinate()));
    EXPECT_FALSE(an::isUnmappedCoordinate(0.0f));
    EXPECT_FALSE(an::isUnmappedCoordinate(-1.5f));
    // Why the sentinel is produced and asked rather than compared: it is not equal to itself, so a
    // caller that wrote the obvious equality would find no unmapped pixel in any map.
    EXPECT_FALSE(an::unmappedCoordinate() == an::unmappedCoordinate());
}

TEST(SipDistortionTest, AMapAgreesWithTheScalarCorrectionOfEveryPixelItCovers)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SipDistortion model = forwardOnlyModel({.x = 63.5, .y = 47.5});

    constexpr std::uint32_t width = 129u;
    constexpr std::uint32_t height = 97u;
    std::vector<float> xMap(static_cast<std::size_t>(width) * height, 0.0f);
    std::vector<float> yMap(static_cast<std::size_t>(width) * height, 0.0f);
    an::SipMapRequest request;
    request.width = width;
    request.height = height;
    request.xMap = std::span<float>(xMap);
    request.yMap = std::span<float>(yMap);

    const an::Result<an::SipMapReport> forward = an::computeSipForwardMap(execution, model, request);
    ASSERT_TRUE(forward.hasValue()) << forward.status().message();
    EXPECT_EQ(forward->mapped, static_cast<std::uint64_t>(width) * height);
    EXPECT_EQ(forward->unmapped, 0u);

    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const an::Result<an::PixelCoordinate> expected =
                an::applySipForward(model, {.x = static_cast<double>(x) + 0.5, .y = static_cast<double>(y) + 0.5});
            ASSERT_TRUE(expected.hasValue()) << expected.status().message();
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            EXPECT_FLOAT_EQ(xMap[index], static_cast<float>(expected->x));
            EXPECT_FLOAT_EQ(yMap[index], static_cast<float>(expected->y));
        }
    }
}

TEST(SipDistortionTest, TheInverseMapReportsTheRouteEveryPixelTookAndTheStepsItCost)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SipDistortion model = forwardOnlyModel({.x = 63.5, .y = 47.5});

    constexpr std::uint32_t width = 64u;
    constexpr std::uint32_t height = 48u;
    constexpr std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    std::vector<float> xMap(static_cast<std::size_t>(pixels), 0.0f);
    std::vector<float> yMap(static_cast<std::size_t>(pixels), 0.0f);
    an::SipMapRequest request;
    request.width = width;
    request.height = height;
    request.xMap = std::span<float>(xMap);
    request.yMap = std::span<float>(yMap);

    const an::Result<an::SipMapReport> inverse = an::computeSipInverseMap(execution, model, request);
    ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
    EXPECT_EQ(inverse->mapped, pixels);
    EXPECT_EQ(inverse->unmapped, 0u);
    EXPECT_EQ(inverse->newtonRoute, pixels);
    EXPECT_EQ(inverse->identityRoute, 0u);
    EXPECT_EQ(inverse->inverseCoefficientRoute, 0u);
    EXPECT_GE(inverse->newtonIterations, pixels);
    EXPECT_LE(inverse->maximumResidualPixels, an::kSipInverseTolerancePixels);

    // Every mapped entry is the position whose forward correction is this pixel's own centre.
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const an::Result<an::PixelCoordinate> roundTrip = an::applySipForward(
                model, {.x = static_cast<double>(xMap[index]), .y = static_cast<double>(yMap[index])});
            ASSERT_TRUE(roundTrip.hasValue()) << roundTrip.status().message();
            EXPECT_NEAR(roundTrip->x, static_cast<double>(x) + 0.5, 1.0e-2);
            EXPECT_NEAR(roundTrip->y, static_cast<double>(y) + 0.5, 1.0e-2);
        }
    }
}

TEST(SipDistortionTest, AMapRefusesAGridItsPlanesDoNotMatch)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SipDistortion model = forwardOnlyModel();

    std::vector<float> xMap(64u, 0.0f);
    std::vector<float> yMap(64u, 0.0f);
    an::SipMapRequest request;
    request.width = 8u;
    request.height = 8u;
    request.xMap = std::span<float>(xMap);
    request.yMap = std::span<float>(yMap);

    an::SipMapRequest empty = request;
    empty.width = 0u;
    EXPECT_EQ(an::computeSipInverseMap(execution, model, empty).code(), an::ErrorCode::InvalidArgument);

    an::SipMapRequest shortPlane = request;
    shortPlane.yMap = std::span<float>(yMap).first(32u);
    EXPECT_EQ(an::computeSipInverseMap(execution, model, shortPlane).code(), an::ErrorCode::IncompatibleArguments);

    an::SipMapRequest aliased = request;
    aliased.yMap = std::span<float>(xMap);
    EXPECT_EQ(an::computeSipInverseMap(execution, model, aliased).code(), an::ErrorCode::Aliasing);

    const an::ExecutionContext none;
    EXPECT_EQ(an::computeSipInverseMap(none, model, request).code(), an::ErrorCode::NotInitialized);
}

TEST(SipDistortionTest, AMapStopsWhenTheOperationIsCancelled)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, 4u, stop.get_token());
    const an::SipDistortion model = forwardOnlyModel({.x = 255.5, .y = 255.5});

    constexpr std::uint32_t extent = 512u;
    std::vector<float> xMap(static_cast<std::size_t>(extent) * extent, 0.0f);
    std::vector<float> yMap(static_cast<std::size_t>(extent) * extent, 0.0f);
    an::SipMapRequest request;
    request.width = extent;
    request.height = extent;
    request.xMap = std::span<float>(xMap);
    request.yMap = std::span<float>(yMap);

    stop.request_stop();
    EXPECT_EQ(an::computeSipInverseMap(execution, model, request).code(), an::ErrorCode::Cancelled);
}

} // namespace
