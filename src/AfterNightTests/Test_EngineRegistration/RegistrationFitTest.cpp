// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Linear and polynomial correspondence-fitting tests for the registration domain.

#include <AfterNightEngine/Algorithms/Registration/RegistrationFit.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include "RegistrationTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stop_token>
#include <vector>

namespace
{

/// A deterministic spread of positions over a one-thousand-pixel frame.
[[nodiscard]] std::vector<an::PixelCoordinate> makeGrid(const std::uint32_t columns, const std::uint32_t rows)
{
    std::vector<an::PixelCoordinate> points;
    points.reserve(static_cast<std::size_t>(columns) * rows);
    for (std::uint32_t row = 0u; row < rows; ++row)
    {
        for (std::uint32_t column = 0u; column < columns; ++column)
        {
            const double fractionX = columns == 1u ? 0.5 : static_cast<double>(column) / (columns - 1u);
            const double fractionY = rows == 1u ? 0.5 : static_cast<double>(row) / (rows - 1u);
            points.push_back({.x = 20.0 + fractionX * 960.0, .y = 15.0 + fractionY * 970.0});
        }
    }
    return points;
}

[[nodiscard]] an::PixelCoordinate applyMatrix(const std::array<double, 9>& matrix, const an::PixelCoordinate& point)
{
    const double denominator = matrix[6] * point.x + matrix[7] * point.y + matrix[8];
    return {.x = (matrix[0] * point.x + matrix[1] * point.y + matrix[2]) / denominator,
            .y = (matrix[3] * point.x + matrix[4] * point.y + matrix[5]) / denominator};
}

[[nodiscard]] std::vector<an::PixelCoordinate> mapAll(const std::vector<an::PixelCoordinate>& points,
                                                      const std::array<double, 9>& matrix)
{
    std::vector<an::PixelCoordinate> answer;
    answer.reserve(points.size());
    for (const an::PixelCoordinate& point : points)
    {
        answer.push_back(applyMatrix(matrix, point));
    }
    return answer;
}

/// Bends every position by a small quadratic in the normalized frame, which is what a curved field
/// looks like once translation, rotation and scale have been removed.
[[nodiscard]] std::vector<an::PixelCoordinate> bendAll(const std::vector<an::PixelCoordinate>& points,
                                                       const double strength)
{
    std::vector<an::PixelCoordinate> answer;
    answer.reserve(points.size());
    for (const an::PixelCoordinate& point : points)
    {
        const double nx = point.x / 1000.0;
        const double ny = point.y / 1000.0;
        answer.push_back({.x = point.x + 1000.0 * strength * (nx * nx - 0.4 * nx * ny),
                          .y = point.y + 1000.0 * strength * (0.7 * ny * ny + 0.3 * nx * ny)});
    }
    return answer;
}

[[nodiscard]] double residualOf(const an::RegistrationTransform& transform,
                                const std::vector<an::PixelCoordinate>& source,
                                const std::vector<an::PixelCoordinate>& reference)
{
    double worst = 0.0;
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        const an::Result<an::PixelCoordinate> mapped = an::mapRegistrationPoint(transform, source[index]);
        if (!mapped.hasValue())
        {
            return std::numeric_limits<double>::infinity();
        }
        worst = (std::max)(worst, std::hypot(mapped->x - reference[index].x, mapped->y - reference[index].y));
    }
    return worst;
}

constexpr std::array<double, 9> kTruthAffine{1.04, -0.11, 37.5, 0.09, 0.97, -21.25, 0.0, 0.0, 1.0};

} // namespace

TEST(RegistrationFitTest, MinimumControlPointsAndAutomaticThresholdsAreStated)
{
    EXPECT_EQ(an::minimumRegistrationControlPoints(an::RegistrationTransformKind::Similarity), 3u);
    EXPECT_EQ(an::minimumRegistrationControlPoints(an::RegistrationTransformKind::Affine), 3u);
    EXPECT_EQ(an::minimumRegistrationControlPoints(an::RegistrationTransformKind::Projective), 4u);
    EXPECT_EQ(an::minimumRegistrationControlPoints(an::RegistrationTransformKind::Polynomial4), 3u);
    EXPECT_EQ(an::minimumRegistrationControlPoints(static_cast<an::RegistrationTransformKind>(9)), 0u);

    EXPECT_EQ(an::automaticRegistrationPolynomialMinimumPoints(2u), 10u);
    EXPECT_EQ(an::automaticRegistrationPolynomialMinimumPoints(3u), 16u);
    EXPECT_EQ(an::automaticRegistrationPolynomialMinimumPoints(4u), 25u);
    EXPECT_EQ(an::automaticRegistrationPolynomialMinimumPoints(5u), 0u);

    EXPECT_STREQ(an::registrationPolynomialPolicyName(an::RegistrationPolynomialPolicy::LinearOnly), "LinearOnly");
    EXPECT_STREQ(an::registrationPolynomialPolicyName(an::RegistrationPolynomialPolicy::StatedOrder), "StatedOrder");
    EXPECT_STREQ(an::registrationPolynomialPolicyName(an::RegistrationPolynomialPolicy::AutomaticOrder),
                 "AutomaticOrder");
    EXPECT_STREQ(an::registrationPolynomialPolicyName(static_cast<an::RegistrationPolynomialPolicy>(8)),
                 "UnknownRegistrationPolynomialPolicy");
    EXPECT_FALSE(an::isRegistrationPolynomialPolicy(static_cast<an::RegistrationPolynomialPolicy>(3)));
}

TEST(RegistrationFitTest, ALinearFitRecoversTheModelItWasGeneratedFrom)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::PixelCoordinate> source = makeGrid(6u, 5u);
    const std::vector<an::PixelCoordinate> reference = mapAll(source, kTruthAffine);

    const an::Result<an::RegistrationFitResult> fitted = an::fitRegistrationTransform(
        execution, source, reference, {.linearKind = an::RegistrationTransformKind::Affine});
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_EQ(fitted->transform.kind(), an::RegistrationTransformKind::Affine);
    EXPECT_EQ(fitted->correspondenceCount, source.size());
    EXPECT_FALSE(fitted->transform.polynomial().order != 0u);
    EXPECT_LT(fitted->rootMeanSquareResidual, 1.0e-8);
    EXPECT_LT(fitted->maximumResidual, 1.0e-8);
    EXPECT_EQ(fitted->polynomialOrder, 0u);
    for (std::size_t index = 0u; index < 6u; ++index)
    {
        EXPECT_NEAR(fitted->transform.matrix()[index], kTruthAffine[index], 1.0e-9);
    }
}

TEST(RegistrationFitTest, SimilarityAndProjectiveModelsAreFittedFromTheirOwnMinimum)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);

    constexpr std::array<double, 9> similarity{0.8, -0.6, 40.0, 0.6, 0.8, -15.0, 0.0, 0.0, 1.0};
    const std::vector<an::PixelCoordinate> triangle{{100.0, 100.0}, {900.0, 150.0}, {300.0, 800.0}};
    const an::Result<an::RegistrationFitResult> similarityFit = an::fitRegistrationTransform(
        execution, triangle, mapAll(triangle, similarity), {.linearKind = an::RegistrationTransformKind::Similarity});
    ASSERT_TRUE(similarityFit.hasValue()) << similarityFit.status().message();
    EXPECT_LT(similarityFit->rootMeanSquareResidual, 1.0e-8);
    EXPECT_NEAR(similarityFit->transform.scale(), 1.0, 1.0e-9);

    constexpr std::array<double, 9> projective{1.1, 0.05, 12.0, -0.07, 0.95, 8.0, 0.00012, -0.00008, 1.0};
    const std::vector<an::PixelCoordinate> quadrilateral{
        {100.0, 100.0}, {900.0, 120.0}, {880.0, 900.0}, {120.0, 860.0}};
    const an::Result<an::RegistrationFitResult> projectiveFit =
        an::fitRegistrationTransform(execution, quadrilateral, mapAll(quadrilateral, projective),
                                     {.linearKind = an::RegistrationTransformKind::Projective});
    ASSERT_TRUE(projectiveFit.hasValue()) << projectiveFit.status().message();
    EXPECT_EQ(projectiveFit->transform.kind(), an::RegistrationTransformKind::Projective);
    EXPECT_LT(projectiveFit->rootMeanSquareResidual, 1.0e-6);
}

TEST(RegistrationFitTest, AStatedPolynomialOrderRemovesACurvedFieldsResidual)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::PixelCoordinate> source = makeGrid(8u, 7u);
    const std::vector<an::PixelCoordinate> reference = bendAll(mapAll(source, kTruthAffine), 0.02);

    an::RegistrationFitRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.polynomialPolicy = an::RegistrationPolynomialPolicy::StatedOrder;
    request.polynomialOrder = 2u;
    request.normalizationScale = {1000.0, 1000.0};

    const an::Result<an::RegistrationFitResult> fitted =
        an::fitRegistrationTransform(execution, source, reference, request);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_EQ(fitted->transform.kind(), an::RegistrationTransformKind::Polynomial2);
    EXPECT_EQ(fitted->polynomialOrder, 2u);
    EXPECT_EQ(fitted->polynomialInliers, source.size());
    EXPECT_GT(fitted->linearRootMeanSquareResidual, 1.0);
    EXPECT_LT(fitted->rootMeanSquareResidual, 1.0e-6);
    EXPECT_LT(residualOf(fitted->transform, source, reference), 1.0e-5);
}

TEST(RegistrationFitTest, TheAutomaticPolicyKeepsALinearFitWhenTheFieldIsNotCurved)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::PixelCoordinate> source = makeGrid(8u, 7u);
    const std::vector<an::PixelCoordinate> reference = mapAll(source, kTruthAffine);

    an::RegistrationFitRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.polynomialPolicy = an::RegistrationPolynomialPolicy::AutomaticOrder;
    request.normalizationScale = {1000.0, 1000.0};

    const an::Result<an::RegistrationFitResult> fitted =
        an::fitRegistrationTransform(execution, source, reference, request);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_TRUE(fitted->transform.isLinear());
    EXPECT_EQ(fitted->polynomialOrder, 0u);
    EXPECT_EQ(fitted->rejectedPolynomialOrders, 0u);
}

TEST(RegistrationFitTest, TheAutomaticPolicyAdoptsTheLowestOrderThatEarnsItsPlace)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::PixelCoordinate> source = makeGrid(8u, 7u);
    const std::vector<an::PixelCoordinate> reference = bendAll(mapAll(source, kTruthAffine), 0.02);

    an::RegistrationFitRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.polynomialPolicy = an::RegistrationPolynomialPolicy::AutomaticOrder;
    request.normalizationScale = {1000.0, 1000.0};

    const an::Result<an::RegistrationFitResult> fitted =
        an::fitRegistrationTransform(execution, source, reference, request);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_EQ(fitted->polynomialOrder, 2u);
    EXPECT_EQ(fitted->rejectedPolynomialOrders, 0u);
    EXPECT_LT(fitted->rootMeanSquareResidual, fitted->linearRootMeanSquareResidual * 0.8);
}

TEST(RegistrationFitTest, TheAutomaticPolicyReportsACurvedFieldItCannotFit)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::PixelCoordinate> source = makeGrid(3u, 2u);
    const std::vector<an::PixelCoordinate> reference = bendAll(mapAll(source, kTruthAffine), 0.05);

    an::RegistrationFitRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.polynomialPolicy = an::RegistrationPolynomialPolicy::AutomaticOrder;
    request.normalizationScale = {1000.0, 1000.0};

    const an::Result<an::RegistrationFitResult> fitted =
        an::fitRegistrationTransform(execution, source, reference, request);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_TRUE(fitted->transform.isLinear());
    EXPECT_GT(fitted->linearRootMeanSquareResidual, 0.5);
    EXPECT_EQ(fitted->rejectedPolynomialOrders, 3u);
}

TEST(RegistrationFitTest, ThePolynomialFitTrimsGrossOutliers)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);
    std::vector<an::PixelCoordinate> source = makeGrid(8u, 7u);
    std::vector<an::PixelCoordinate> reference = bendAll(mapAll(source, kTruthAffine), 0.02);
    reference[5].x += 90.0;
    reference[19].y -= 120.0;
    reference[41].x -= 75.0;

    an::RegistrationFitRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.polynomialPolicy = an::RegistrationPolynomialPolicy::StatedOrder;
    request.polynomialOrder = 2u;
    request.normalizationScale = {1000.0, 1000.0};

    const an::Result<an::RegistrationFitResult> fitted =
        an::fitRegistrationTransform(execution, source, reference, request);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_LT(fitted->polynomialInliers, static_cast<std::uint32_t>(source.size()));
    EXPECT_GE(fitted->polynomialInliers, an::registrationPolynomialTermCount(2u));
    EXPECT_GT(fitted->refinementPasses, 1u);
    EXPECT_LT(fitted->rootMeanSquareResidual, 1.0);
}

TEST(RegistrationFitTest, ThePolynomialStageAcceptsAnAlreadyFittedLinearSeed)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::PixelCoordinate> source = makeGrid(8u, 7u);
    const std::vector<an::PixelCoordinate> reference = bendAll(mapAll(source, kTruthAffine), 0.02);

    const an::Result<an::RegistrationTransform> seed = an::RegistrationTransform::create(
        {.linearKind = an::RegistrationTransformKind::Affine, .sourceToReference = kTruthAffine});
    ASSERT_TRUE(seed.hasValue()) << seed.status().message();

    an::RegistrationFitRequest request;
    request.polynomialOrder = 2u;
    request.normalizationScale = {1000.0, 1000.0};
    const an::Result<an::RegistrationFitResult> fitted =
        an::fitRegistrationPolynomial(execution, source, reference, *seed, request);
    ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
    EXPECT_EQ(fitted->polynomialOrder, 2u);
    EXPECT_LT(fitted->rootMeanSquareResidual, 1.0e-6);
    for (std::size_t index = 0u; index < 6u; ++index)
    {
        EXPECT_NEAR(fitted->transform.matrix()[index], kTruthAffine[index], 1.0e-12);
    }

    const an::Result<an::RegistrationFitResult> refusedSeed =
        an::fitRegistrationPolynomial(execution, source, reference, fitted->transform, request);
    ASSERT_FALSE(refusedSeed.hasValue());
    EXPECT_EQ(refusedSeed.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(RegistrationFitTest, TheAnswerDoesNotDependOnTheThreadCount)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const std::vector<an::PixelCoordinate> source = makeGrid(9u, 8u);
    const std::vector<an::PixelCoordinate> reference = bendAll(mapAll(source, kTruthAffine), 0.03);

    an::RegistrationFitRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.polynomialPolicy = an::RegistrationPolynomialPolicy::AutomaticOrder;
    request.normalizationScale = {1000.0, 1000.0};

    const an::ExecutionContext single = registration_test::makeExecution(engine, 1u);
    const an::ExecutionContext many = registration_test::makeExecution(engine, 8u);
    const an::Result<an::RegistrationFitResult> one = an::fitRegistrationTransform(single, source, reference, request);
    const an::Result<an::RegistrationFitResult> eight = an::fitRegistrationTransform(many, source, reference, request);
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    ASSERT_TRUE(eight.hasValue()) << eight.status().message();
    EXPECT_EQ(*one, *eight);
}

TEST(RegistrationFitTest, FittingRefusesEveryMalformedRequest)
{
    const an::EngineContext engine = registration_test::makeEngine();
    const an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::PixelCoordinate> source = makeGrid(6u, 5u);
    const std::vector<an::PixelCoordinate> reference = mapAll(source, kTruthAffine);

    const an::Result<an::RegistrationFitResult> noContext =
        an::fitRegistrationTransform(an::ExecutionContext{}, source, reference, {});
    ASSERT_FALSE(noContext.hasValue());
    EXPECT_EQ(noContext.status().code(), an::ErrorCode::NotInitialized);

    const an::Result<an::RegistrationFitResult> mismatched = an::fitRegistrationTransform(
        execution, source, std::span<const an::PixelCoordinate>(reference).first(reference.size() - 1u), {});
    ASSERT_FALSE(mismatched.hasValue());
    EXPECT_EQ(mismatched.status().code(), an::ErrorCode::IncompatibleArguments);

    const std::vector<an::PixelCoordinate> pair{{0.0, 0.0}, {100.0, 100.0}};
    const an::Result<an::RegistrationFitResult> tooFew = an::fitRegistrationTransform(execution, pair, pair, {});
    ASSERT_FALSE(tooFew.hasValue());
    EXPECT_EQ(tooFew.status().code(), an::ErrorCode::EmptySelection);

    const std::vector<an::PixelCoordinate> collinear{{0.0, 0.0}, {100.0, 100.0}, {200.0, 200.0}, {300.0, 300.0}};
    const an::Result<an::RegistrationFitResult> degenerate =
        an::fitRegistrationTransform(execution, collinear, collinear, {});
    ASSERT_FALSE(degenerate.hasValue());
    EXPECT_EQ(degenerate.status().code(), an::ErrorCode::InvalidData);

    const an::Result<an::RegistrationFitResult> polynomialKind = an::fitRegistrationTransform(
        execution, source, reference, {.linearKind = an::RegistrationTransformKind::Polynomial3});
    ASSERT_FALSE(polynomialKind.hasValue());
    EXPECT_EQ(polynomialKind.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationFitResult> unknownPolicy = an::fitRegistrationTransform(
        execution, source, reference, {.polynomialPolicy = static_cast<an::RegistrationPolynomialPolicy>(9)});
    ASSERT_FALSE(unknownPolicy.hasValue());
    EXPECT_EQ(unknownPolicy.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationFitResult> badOrder = an::fitRegistrationTransform(
        execution, source, reference,
        {.polynomialPolicy = an::RegistrationPolynomialPolicy::StatedOrder, .polynomialOrder = 5u});
    ASSERT_FALSE(badOrder.hasValue());
    EXPECT_EQ(badOrder.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationFitResult> badNormalization = an::fitRegistrationTransform(
        execution, source, reference,
        {.polynomialPolicy = an::RegistrationPolynomialPolicy::StatedOrder, .normalizationScale = {0.0, 1.0}});
    ASSERT_FALSE(badNormalization.hasValue());
    EXPECT_EQ(badNormalization.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationFitResult> badFactor =
        an::fitRegistrationTransform(execution, source, reference, {.automaticImprovementFactor = 1.5});
    ASSERT_FALSE(badFactor.hasValue());
    EXPECT_EQ(badFactor.status().code(), an::ErrorCode::InvalidArgument);

    std::vector<an::PixelCoordinate> nonFinite = source;
    nonFinite[2].x = std::numeric_limits<double>::quiet_NaN();
    const an::Result<an::RegistrationFitResult> badPoint =
        an::fitRegistrationTransform(execution, nonFinite, reference, {});
    ASSERT_FALSE(badPoint.hasValue());
    EXPECT_EQ(badPoint.status().code(), an::ErrorCode::InvalidData);
}

TEST(RegistrationFitTest, FittingStopsWhenTheOperationIsCancelled)
{
    const an::EngineContext engine = registration_test::makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = registration_test::makeExecution(engine, 4u, stop.get_token());
    const std::vector<an::PixelCoordinate> source = makeGrid(8u, 7u);
    const std::vector<an::PixelCoordinate> reference = bendAll(mapAll(source, kTruthAffine), 0.02);
    stop.request_stop();

    an::RegistrationFitRequest request;
    request.polynomialPolicy = an::RegistrationPolynomialPolicy::AutomaticOrder;
    request.normalizationScale = {1000.0, 1000.0};
    const an::Result<an::RegistrationFitResult> fitted =
        an::fitRegistrationTransform(execution, source, reference, request);
    ASSERT_FALSE(fitted.hasValue());
    EXPECT_EQ(fitted.status().code(), an::ErrorCode::Cancelled);
}
