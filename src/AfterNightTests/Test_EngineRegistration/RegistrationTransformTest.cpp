// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Model, mapping, inversion, composition and bounds tests for the registration transform.

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] an::RegistrationTransform makeLinear(const an::RegistrationTransformKind kind,
                                                   const std::array<double, 9>& matrix)
{
    const an::Result<an::RegistrationTransform> made =
        an::RegistrationTransform::create({.linearKind = kind, .sourceToReference = matrix});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::RegistrationTransform{};
}

/// A polynomial whose two components reproduce their normalized input exactly.
[[nodiscard]] an::RegistrationPolynomial identityPolynomial(const std::uint32_t order,
                                                            const an::PixelCoordinate scale = {1.0, 1.0})
{
    an::RegistrationPolynomial polynomial;
    polynomial.order = order;
    polynomial.x[1] = 1.0;
    polynomial.y[2] = 1.0;
    polynomial.normalizationScale = scale;
    return polynomial;
}

/// A polynomial that bends its input by a small quadratic term on each axis.
[[nodiscard]] an::RegistrationPolynomial bendingPolynomial(const an::PixelCoordinate scale)
{
    an::RegistrationPolynomial polynomial = identityPolynomial(2u, scale);
    polynomial.x[3] = 0.031; // x^2
    polynomial.x[4] = -0.017; // x*y
    polynomial.y[5] = 0.024; // y^2
    polynomial.y[4] = 0.011; // x*y
    return polynomial;
}

constexpr double kTight = 1.0e-9;

} // namespace

TEST(RegistrationTransformTest, DefaultIsTheIdentityWithNoPolynomial)
{
    const an::RegistrationTransform transform;
    EXPECT_TRUE(transform.isLinear());
    EXPECT_EQ(transform.kind(), an::RegistrationTransformKind::Similarity);
    EXPECT_EQ(transform.polynomial().order, 0u);
    EXPECT_NEAR(transform.scale(), 1.0, kTight);
    EXPECT_NEAR(transform.rotationRadians(), 0.0, kTight);

    const an::Result<an::PixelCoordinate> mapped =
        an::mapRegistrationPoint(transform, an::PixelCoordinate{.x = 17.25, .y = -3.5});
    ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
    EXPECT_NEAR(mapped->x, 17.25, kTight);
    EXPECT_NEAR(mapped->y, -3.5, kTight);
}

TEST(RegistrationTransformTest, TermCountIsTheTriangularCountAndClosedAboveTheCeiling)
{
    EXPECT_EQ(an::registrationPolynomialTermCount(0u), 1u);
    EXPECT_EQ(an::registrationPolynomialTermCount(1u), 3u);
    EXPECT_EQ(an::registrationPolynomialTermCount(2u), 6u);
    EXPECT_EQ(an::registrationPolynomialTermCount(3u), 10u);
    EXPECT_EQ(an::registrationPolynomialTermCount(4u), 15u);
    EXPECT_EQ(an::registrationPolynomialTermCount(5u), 0u);
    EXPECT_EQ(an::kRegistrationPolynomialTerms, 15u);
}

TEST(RegistrationTransformTest, EveryKindAndSeedIsNamedAndUnknownValuesAreNamedToo)
{
    EXPECT_STREQ(an::registrationTransformKindName(an::RegistrationTransformKind::Similarity), "Similarity");
    EXPECT_STREQ(an::registrationTransformKindName(an::RegistrationTransformKind::Affine), "Affine");
    EXPECT_STREQ(an::registrationTransformKindName(an::RegistrationTransformKind::Projective), "Projective");
    EXPECT_STREQ(an::registrationTransformKindName(an::RegistrationTransformKind::Polynomial2), "Polynomial2");
    EXPECT_STREQ(an::registrationTransformKindName(an::RegistrationTransformKind::Polynomial3), "Polynomial3");
    EXPECT_STREQ(an::registrationTransformKindName(an::RegistrationTransformKind::Polynomial4), "Polynomial4");
    EXPECT_STREQ(an::registrationTransformKindName(static_cast<an::RegistrationTransformKind>(9)),
                 "UnknownRegistrationTransformKind");

    EXPECT_STREQ(an::registrationInverseSeedName(an::RegistrationInverseSeed::AuthoritativeSeed), "AuthoritativeSeed");
    EXPECT_STREQ(an::registrationInverseSeedName(an::RegistrationInverseSeed::RowNeighbourWarmStart),
                 "RowNeighbourWarmStart");
    EXPECT_STREQ(an::registrationInverseSeedName(static_cast<an::RegistrationInverseSeed>(7)),
                 "UnknownRegistrationInverseSeed");

    EXPECT_TRUE(an::isLinearRegistrationTransformKind(an::RegistrationTransformKind::Projective));
    EXPECT_FALSE(an::isLinearRegistrationTransformKind(an::RegistrationTransformKind::Polynomial2));
    EXPECT_FALSE(an::isRegistrationTransformKind(static_cast<an::RegistrationTransformKind>(6)));
    EXPECT_FALSE(an::isRegistrationInverseSeed(static_cast<an::RegistrationInverseSeed>(2)));
}

TEST(RegistrationTransformTest, KindComesFromTheLinearKindAndThePolynomialOrderTogether)
{
    for (const std::uint32_t order : {2u, 3u, 4u})
    {
        an::RegistrationTransformRequest request;
        request.linearKind = an::RegistrationTransformKind::Affine;
        request.polynomial = identityPolynomial(order);
        const an::Result<an::RegistrationTransform> made = an::RegistrationTransform::create(request);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        EXPECT_FALSE(made->isLinear());
        EXPECT_EQ(made->linearKind(), an::RegistrationTransformKind::Affine);
        EXPECT_EQ(static_cast<std::uint32_t>(made->kind()),
                  static_cast<std::uint32_t>(an::RegistrationTransformKind::Projective) + order - 1u);
    }
}

TEST(RegistrationTransformTest, CreateRefusesEveryMalformedModel)
{
    const auto refuse = [](const an::RegistrationTransformRequest& request, const an::ErrorCode expected) {
        const an::Result<an::RegistrationTransform> made = an::RegistrationTransform::create(request);
        ASSERT_FALSE(made.hasValue());
        EXPECT_EQ(made.status().code(), expected) << made.status().message();
    };

    an::RegistrationTransformRequest unknownKind;
    unknownKind.linearKind = static_cast<an::RegistrationTransformKind>(11);
    refuse(unknownKind, an::ErrorCode::InvalidArgument);

    an::RegistrationTransformRequest polynomialKind;
    polynomialKind.linearKind = an::RegistrationTransformKind::Polynomial2;
    refuse(polynomialKind, an::ErrorCode::InvalidArgument);

    an::RegistrationTransformRequest nonFinite;
    nonFinite.sourceToReference[4] = std::numeric_limits<double>::quiet_NaN();
    refuse(nonFinite, an::ErrorCode::InvalidArgument);

    an::RegistrationTransformRequest projectiveRowOnAffine;
    projectiveRowOnAffine.linearKind = an::RegistrationTransformKind::Affine;
    projectiveRowOnAffine.sourceToReference[6] = 1.0e-6;
    refuse(projectiveRowOnAffine, an::ErrorCode::InvalidArgument);

    an::RegistrationTransformRequest singular;
    singular.linearKind = an::RegistrationTransformKind::Affine;
    singular.sourceToReference = {1.0, 2.0, 0.0, 2.0, 4.0, 0.0, 0.0, 0.0, 1.0};
    refuse(singular, an::ErrorCode::InvalidData);

    an::RegistrationTransformRequest lowOrder;
    lowOrder.polynomial = identityPolynomial(1u);
    refuse(lowOrder, an::ErrorCode::InvalidArgument);

    an::RegistrationTransformRequest highOrder;
    highOrder.polynomial = identityPolynomial(5u);
    refuse(highOrder, an::ErrorCode::InvalidArgument);

    an::RegistrationTransformRequest outsideTriangle;
    outsideTriangle.polynomial = identityPolynomial(2u);
    outsideTriangle.polynomial.x[6] = 1.0e-30;
    refuse(outsideTriangle, an::ErrorCode::InvalidArgument);

    an::RegistrationTransformRequest zeroScale;
    zeroScale.polynomial = identityPolynomial(2u, {1.0, 0.0});
    refuse(zeroScale, an::ErrorCode::InvalidArgument);

    an::RegistrationTransformRequest nonFiniteOrigin;
    nonFiniteOrigin.polynomial = identityPolynomial(2u);
    nonFiniteOrigin.polynomial.normalizationOrigin = {std::numeric_limits<double>::infinity(), 0.0};
    refuse(nonFiniteOrigin, an::ErrorCode::InvalidArgument);
}

TEST(RegistrationTransformTest, ADeclaredProjectiveKindAcceptsAnIdentityBottomRow)
{
    const an::RegistrationTransform transform =
        makeLinear(an::RegistrationTransformKind::Projective, {2.0, 0.0, 3.0, 0.0, 2.0, 4.0, 0.0, 0.0, 1.0});
    EXPECT_EQ(transform.kind(), an::RegistrationTransformKind::Projective);
}

TEST(RegistrationTransformTest, DerivedScaleRotationAndTranslationComeFromTheMatrixColumns)
{
    const double angle = std::numbers::pi / 6.0;
    const double scale = 2.5;
    const an::RegistrationTransform similarity =
        makeLinear(an::RegistrationTransformKind::Similarity,
                   {scale * std::cos(angle), -scale * std::sin(angle), 11.0, scale * std::sin(angle),
                    scale * std::cos(angle), -7.0, 0.0, 0.0, 1.0});

    EXPECT_NEAR(similarity.scaleX(), scale, kTight);
    EXPECT_NEAR(similarity.scaleY(), scale, kTight);
    EXPECT_NEAR(similarity.scale(), scale, kTight);
    EXPECT_NEAR(similarity.rotationRadians(), angle, kTight);
    EXPECT_NEAR(similarity.rotationDegrees(), 30.0, 1.0e-7);
    EXPECT_NEAR(similarity.translation().x, 11.0, kTight);
    EXPECT_NEAR(similarity.translation().y, -7.0, kTight);
    EXPECT_NEAR(similarity.linearDeterminant(), scale * scale, kTight);

    // A shear whose row norms and column norms disagree, and disagree by swapping the two axes.
    // The horizontal axis is mapped to (1, 0.75), of length 1.25; reading the first row instead
    // reports the length of (1, 1.25) as the horizontal scale.
    const an::RegistrationTransform sheared =
        makeLinear(an::RegistrationTransformKind::Affine, {1.0, 1.25, 0.0, 0.75, 1.0, 0.0, 0.0, 0.0, 1.0});
    const double firstColumn = std::hypot(1.0, 0.75);
    const double secondColumn = std::hypot(1.25, 1.0);
    EXPECT_NEAR(sheared.scaleX(), firstColumn, kTight);
    EXPECT_NEAR(sheared.scaleY(), secondColumn, kTight);
    EXPECT_NEAR(sheared.scale(), (firstColumn + secondColumn) / 2.0, kTight);
    EXPECT_NEAR(sheared.rotationRadians(), std::atan2(0.75, 1.0), kTight);
    // What reading the first row would report for the horizontal axis, which is this matrix's
    // vertical column norm rather than its horizontal one.
    EXPECT_NEAR(std::hypot(1.0, 1.25), secondColumn, kTight);
}

TEST(RegistrationTransformTest, AMirroredTransformReportsANegativeDeterminant)
{
    const an::RegistrationTransform mirrored =
        makeLinear(an::RegistrationTransformKind::Affine, {-1.0, 0.0, 100.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0});
    EXPECT_LT(mirrored.linearDeterminant(), 0.0);
    EXPECT_NEAR(mirrored.scaleX(), 1.0, kTight);
}

TEST(RegistrationTransformTest, MappingAppliesTheMatrixThenThePolynomialThenTheTranslation)
{
    const std::array<double, 9> matrix{1.0, 0.0, 10.0, 0.0, 1.0, 20.0, 0.0, 0.0, 1.0};
    an::RegistrationTransformRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.sourceToReference = matrix;
    request.polynomial = identityPolynomial(2u, {100.0, 100.0});
    request.polynomial.x[3] = 0.5; // half of the squared normalized horizontal coordinate
    const an::Result<an::RegistrationTransform> made = an::RegistrationTransform::create(request);
    ASSERT_TRUE(made.hasValue()) << made.status().message();

    const an::PixelCoordinate postTranslation{.x = 1000.0, .y = -500.0};
    const an::Result<an::PixelCoordinate> mapped =
        an::mapRegistrationPoint(*made, an::PixelCoordinate{.x = 30.0, .y = 40.0}, postTranslation);
    ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();

    // Linear: (40, 60). Normalized: (0.4, 0.6). Polynomial x: 0.4 + 0.5 * 0.16 = 0.48, y: 0.6.
    // Denormalized: (48, 60). Then the translation.
    EXPECT_NEAR(mapped->x, 48.0 + 1000.0, 1.0e-9);
    EXPECT_NEAR(mapped->y, 60.0 - 500.0, 1.0e-9);

    // The translation is not folded into the matrix: a transform that carries it in its own
    // translation column would evaluate the polynomial on the translated position instead.
    std::array<double, 9> folded = matrix;
    folded[2] += postTranslation.x;
    folded[5] += postTranslation.y;
    an::RegistrationTransformRequest foldedRequest = request;
    foldedRequest.sourceToReference = folded;
    const an::Result<an::RegistrationTransform> foldedTransform = an::RegistrationTransform::create(foldedRequest);
    ASSERT_TRUE(foldedTransform.hasValue()) << foldedTransform.status().message();
    const an::Result<an::PixelCoordinate> foldedMapped =
        an::mapRegistrationPoint(*foldedTransform, an::PixelCoordinate{.x = 30.0, .y = 40.0});
    ASSERT_TRUE(foldedMapped.hasValue()) << foldedMapped.status().message();
    EXPECT_GT(std::abs(foldedMapped->x - mapped->x), 1.0);
}

TEST(RegistrationTransformTest, AProjectiveMappingDividesByItsOwnDenominator)
{
    const an::RegistrationTransform projective =
        makeLinear(an::RegistrationTransformKind::Projective, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.001, 0.0, 1.0});
    const an::Result<an::PixelCoordinate> mapped =
        an::mapRegistrationPoint(projective, an::PixelCoordinate{.x = 100.0, .y = 50.0});
    ASSERT_TRUE(mapped.hasValue()) << mapped.status().message();
    EXPECT_NEAR(mapped->x, 100.0 / 1.1, 1.0e-9);
    EXPECT_NEAR(mapped->y, 50.0 / 1.1, 1.0e-9);

    // The denominator vanishes at x = -1000, which is a position this transform does not map.
    const an::Result<an::PixelCoordinate> vanishing =
        an::mapRegistrationPoint(projective, an::PixelCoordinate{.x = -1000.0, .y = 0.0});
    ASSERT_FALSE(vanishing.hasValue());
    EXPECT_EQ(vanishing.status().code(), an::ErrorCode::InvalidData);
}

TEST(RegistrationTransformTest, MappingRefusesNonFinitePositionsAndTranslations)
{
    const an::RegistrationTransform transform;
    const an::Result<an::PixelCoordinate> badPoint = an::mapRegistrationPoint(
        transform, an::PixelCoordinate{.x = std::numeric_limits<double>::quiet_NaN(), .y = 0.0});
    ASSERT_FALSE(badPoint.hasValue());
    EXPECT_EQ(badPoint.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::PixelCoordinate> badTranslation =
        an::mapRegistrationPoint(transform, an::PixelCoordinate{.x = 1.0, .y = 1.0},
                                 an::PixelCoordinate{.x = 0.0, .y = std::numeric_limits<double>::infinity()});
    ASSERT_FALSE(badTranslation.hasValue());
    EXPECT_EQ(badTranslation.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(RegistrationTransformTest, ARunMapsEveryPositionAsTheSinglePositionMapDoes)
{
    // The run form checks the translation once and takes each position's arithmetic in the single
    // position's order, so every position that maps carries exactly the bits the single-position
    // map returns for it, and every one that map refuses becomes a counted pair of NaNs: across
    // every linear kind and polynomial order, a vanishing denominator, positions that are not
    // finite and a correction that overflows.
    std::vector<an::RegistrationTransform> transforms{
        makeLinear(an::RegistrationTransformKind::Similarity, {0.8, -0.6, 3.5, 0.6, 0.8, -2.25, 0.0, 0.0, 1.0}),
        makeLinear(an::RegistrationTransformKind::Affine, {1.01, -0.03, 2.5, 0.02, 0.98, -1.75, 0.0, 0.0, 1.0}),
        makeLinear(an::RegistrationTransformKind::Projective, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.001, 0.0, 1.0})};
    for (std::uint32_t order = an::kMinimumRegistrationPolynomialOrder;
         order <= an::kMaximumRegistrationPolynomialOrder; ++order)
    {
        an::RegistrationTransformRequest request;
        request.linearKind = an::RegistrationTransformKind::Projective;
        request.sourceToReference = {1.01, -0.03, 2.25, 0.02, 0.98, -1.75, 0.0001, -0.0002, 1.0};
        request.polynomial.order = order;
        request.polynomial.normalizationOrigin = {.x = 256.5, .y = 254.5};
        request.polynomial.normalizationScale = {.x = 250.0, .y = 192.0};
        for (std::size_t index = 0u; index < an::registrationPolynomialTermCount(order); ++index)
        {
            request.polynomial.x[index] = 0.0004 * static_cast<double>(index + 1u);
            request.polynomial.y[index] = -0.0003 * static_cast<double>(index + 2u);
        }
        request.polynomial.x[1] = 1.0;
        request.polynomial.y[2] = 1.0;
        const an::Result<an::RegistrationTransform> curved = an::RegistrationTransform::create(request);
        ASSERT_TRUE(curved.hasValue()) << "order " << order << ": " << curved.status().message();
        transforms.push_back(*curved);
    }
    std::vector<an::PixelCoordinate> positions;
    for (double y = -3.25; y < 520.0; y += 37.75)
        for (double x = -1.5; x < 530.0; x += 23.125)
            positions.push_back({.x = x, .y = y});
    positions.push_back({.x = -1000.0, .y = 12.0}); // where the projective denominator vanishes
    positions.push_back({.x = std::numeric_limits<double>::quiet_NaN(), .y = 4.0});
    positions.push_back({.x = 4.0, .y = std::numeric_limits<double>::infinity()});
    positions.push_back({.x = 1.0e200, .y = -1.0e200}); // a correction no double holds
    const an::PixelCoordinate translation{.x = -0.25, .y = 0.75};
    const auto bitsOf = [](double value) { return std::bit_cast<std::uint64_t>(value); };
    for (std::size_t which = 0u; which < transforms.size(); ++which)
    {
        const an::RegistrationTransform& transform = transforms[which];
        std::vector<an::PixelCoordinate> run = positions;
        const std::uint64_t unmapped = an::mapRegistrationPoints(transform, translation, run);
        std::uint64_t refused = 0u;
        for (std::size_t index = 0u; index < positions.size(); ++index)
        {
            const an::Result<an::PixelCoordinate> single =
                an::mapRegistrationPoint(transform, positions[index], translation);
            if (single.hasValue())
            {
                EXPECT_EQ(bitsOf(run[index].x), bitsOf(single->x)) << "transform " << which << " position " << index;
                EXPECT_EQ(bitsOf(run[index].y), bitsOf(single->y)) << "transform " << which << " position " << index;
            }
            else
            {
                EXPECT_TRUE(std::isnan(run[index].x) && std::isnan(run[index].y))
                    << "transform " << which << " position " << index;
                ++refused;
            }
        }
        EXPECT_EQ(unmapped, refused) << "transform " << which;
        EXPECT_GE(refused, 2u) << "transform " << which;
    }

    // A translation that is not finite maps nothing, and an empty run maps nothing either.
    std::vector<an::PixelCoordinate> run = positions;
    EXPECT_EQ(
        an::mapRegistrationPoints(transforms.back(), {.x = std::numeric_limits<double>::infinity(), .y = 0.0}, run),
        run.size());
    for (const an::PixelCoordinate& position : run)
        EXPECT_TRUE(std::isnan(position.x) && std::isnan(position.y));
    EXPECT_EQ(an::mapRegistrationPoints(transforms.front(), translation, {}), 0u);
}

TEST(RegistrationTransformTest, ALinearInverseIsExactAndIterationFree)
{
    const an::RegistrationTransform affine =
        makeLinear(an::RegistrationTransformKind::Affine, {1.5, -0.25, 30.0, 0.4, 1.1, -12.0, 0.0, 0.0, 1.0});
    const an::PixelCoordinate translation{.x = 7.5, .y = -2.25};
    const an::PixelCoordinate source{.x = 123.75, .y = 456.5};

    const an::Result<an::PixelCoordinate> forward = an::mapRegistrationPoint(affine, source, translation);
    ASSERT_TRUE(forward.hasValue()) << forward.status().message();
    const an::Result<an::RegistrationInverseResult> back =
        an::mapRegistrationPointInverse(affine, *forward, translation);
    ASSERT_TRUE(back.hasValue()) << back.status().message();
    EXPECT_NEAR(back->pixel.x, source.x, 1.0e-9);
    EXPECT_NEAR(back->pixel.y, source.y, 1.0e-9);
    EXPECT_EQ(back->iterations, 0u);
    EXPECT_FALSE(back->warmSeedAccepted);
    EXPECT_EQ(back->residualPixels, 0.0);
}

TEST(RegistrationTransformTest, APolynomialInverseSolvesToTheStatedTolerance)
{
    an::RegistrationTransformRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.sourceToReference = {1.02, -0.03, 12.0, 0.04, 0.99, -8.0, 0.0, 0.0, 1.0};
    request.polynomial = bendingPolynomial({1024.0, 1024.0});
    const an::Result<an::RegistrationTransform> made = an::RegistrationTransform::create(request);
    ASSERT_TRUE(made.hasValue()) << made.status().message();

    for (const an::PixelCoordinate source :
         {an::PixelCoordinate{0.5, 0.5}, an::PixelCoordinate{512.5, 300.5}, an::PixelCoordinate{1023.5, 1023.5}})
    {
        const an::Result<an::PixelCoordinate> forward = an::mapRegistrationPoint(*made, source);
        ASSERT_TRUE(forward.hasValue()) << forward.status().message();
        const an::Result<an::RegistrationInverseResult> back = an::mapRegistrationPointInverse(*made, *forward);
        ASSERT_TRUE(back.hasValue()) << back.status().message();
        EXPECT_GT(back->iterations, 0u);
        EXPECT_LE(back->residualPixels, an::kRegistrationInverseTolerancePixels);
        EXPECT_NEAR(back->pixel.x, source.x, 1.0e-3);
        EXPECT_NEAR(back->pixel.y, source.y, 1.0e-3);
    }
}

TEST(RegistrationTransformTest, AWarmSeedIsAHintAndNeverAnAuthority)
{
    an::RegistrationTransformRequest request;
    request.polynomial = bendingPolynomial({1024.0, 1024.0});
    const an::Result<an::RegistrationTransform> made = an::RegistrationTransform::create(request);
    ASSERT_TRUE(made.hasValue()) << made.status().message();

    const an::PixelCoordinate source{.x = 400.5, .y = 700.5};
    const an::Result<an::PixelCoordinate> forward = an::mapRegistrationPoint(*made, source);
    ASSERT_TRUE(forward.hasValue()) << forward.status().message();

    const an::Result<an::RegistrationInverseResult> cold = an::mapRegistrationPointInverse(*made, *forward);
    ASSERT_TRUE(cold.hasValue()) << cold.status().message();

    const an::PixelCoordinate goodSeed{.x = 399.5, .y = 700.5};
    const an::Result<an::RegistrationInverseResult> warm = an::mapRegistrationPointInverse(
        *made, *forward, {}, an::RegistrationInverseSeed::RowNeighbourWarmStart, &goodSeed);
    ASSERT_TRUE(warm.hasValue()) << warm.status().message();
    EXPECT_TRUE(warm->warmSeedAccepted);
    EXPECT_NEAR(warm->pixel.x, cold->pixel.x, 1.0e-4);
    EXPECT_NEAR(warm->pixel.y, cold->pixel.y, 1.0e-4);

    // A seed nowhere near the answer still produces an answer that inverts the map - a quadratic
    // correction has two preimages, and a solve started far away may land on the other one. That is
    // why the policy exists for a neighbouring position's answer and not as a general hint: what a
    // warm route is held to is the residual, not the branch.
    const an::PixelCoordinate uselessSeed{.x = 1.0e9, .y = -1.0e9};
    const an::Result<an::RegistrationInverseResult> distant = an::mapRegistrationPointInverse(
        *made, *forward, {}, an::RegistrationInverseSeed::RowNeighbourWarmStart, &uselessSeed);
    ASSERT_TRUE(distant.hasValue()) << distant.status().message();
    const an::Result<an::PixelCoordinate> distantForward = an::mapRegistrationPoint(*made, distant->pixel);
    ASSERT_TRUE(distantForward.hasValue()) << distantForward.status().message();
    EXPECT_NEAR(distantForward->x, forward->x, an::kRegistrationInverseFinalTolerancePixels);
    EXPECT_NEAR(distantForward->y, forward->y, an::kRegistrationInverseFinalTolerancePixels);

    // Asking for the warm route without a seed is the authoritative route.
    const an::Result<an::RegistrationInverseResult> seedless = an::mapRegistrationPointInverse(
        *made, *forward, {}, an::RegistrationInverseSeed::RowNeighbourWarmStart, nullptr);
    ASSERT_TRUE(seedless.hasValue()) << seedless.status().message();
    EXPECT_FALSE(seedless->warmSeedAccepted);
    EXPECT_EQ(seedless->iterations, cold->iterations);
}

TEST(RegistrationTransformTest, InverseRefusesUnknownSeedsAndNonFiniteArguments)
{
    const an::RegistrationTransform transform;
    const an::Result<an::RegistrationInverseResult> unknownSeed = an::mapRegistrationPointInverse(
        transform, an::PixelCoordinate{1.0, 1.0}, {}, static_cast<an::RegistrationInverseSeed>(5));
    ASSERT_FALSE(unknownSeed.hasValue());
    EXPECT_EQ(unknownSeed.status().code(), an::ErrorCode::InvalidArgument);

    const an::PixelCoordinate badSeed{.x = std::numeric_limits<double>::quiet_NaN(), .y = 0.0};
    const an::Result<an::RegistrationInverseResult> nonFiniteSeed = an::mapRegistrationPointInverse(
        transform, an::PixelCoordinate{1.0, 1.0}, {}, an::RegistrationInverseSeed::RowNeighbourWarmStart, &badSeed);
    ASSERT_FALSE(nonFiniteSeed.hasValue());
    EXPECT_EQ(nonFiniteSeed.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(RegistrationTransformTest, ComposingKeepsTheMoreGeneralKindAndRefusesAPolynomial)
{
    const an::RegistrationTransform translate =
        makeLinear(an::RegistrationTransformKind::Similarity, {1.0, 0.0, 5.0, 0.0, 1.0, 7.0, 0.0, 0.0, 1.0});
    const an::RegistrationTransform shear =
        makeLinear(an::RegistrationTransformKind::Affine, {1.0, 0.5, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0});

    const an::Result<an::RegistrationTransform> composed = an::composeRegistrationTransforms(translate, shear);
    ASSERT_TRUE(composed.hasValue()) << composed.status().message();
    EXPECT_EQ(composed->kind(), an::RegistrationTransformKind::Affine);

    const an::PixelCoordinate point{.x = 10.0, .y = 20.0};
    const an::Result<an::PixelCoordinate> viaFirst = an::mapRegistrationPoint(translate, point);
    ASSERT_TRUE(viaFirst.hasValue()) << viaFirst.status().message();
    const an::Result<an::PixelCoordinate> viaBoth = an::mapRegistrationPoint(shear, *viaFirst);
    ASSERT_TRUE(viaBoth.hasValue()) << viaBoth.status().message();
    const an::Result<an::PixelCoordinate> direct = an::mapRegistrationPoint(*composed, point);
    ASSERT_TRUE(direct.hasValue()) << direct.status().message();
    EXPECT_NEAR(direct->x, viaBoth->x, 1.0e-9);
    EXPECT_NEAR(direct->y, viaBoth->y, 1.0e-9);

    const an::RegistrationTransform projective =
        makeLinear(an::RegistrationTransformKind::Projective, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0005, 0.0, 1.0});
    const an::Result<an::RegistrationTransform> withProjective = an::composeRegistrationTransforms(shear, projective);
    ASSERT_TRUE(withProjective.hasValue()) << withProjective.status().message();
    EXPECT_EQ(withProjective->kind(), an::RegistrationTransformKind::Projective);

    an::RegistrationTransformRequest polynomialRequest;
    polynomialRequest.polynomial = identityPolynomial(2u);
    const an::Result<an::RegistrationTransform> polynomial = an::RegistrationTransform::create(polynomialRequest);
    ASSERT_TRUE(polynomial.hasValue()) << polynomial.status().message();
    const an::Result<an::RegistrationTransform> refused = an::composeRegistrationTransforms(*polynomial, shear);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
}

TEST(RegistrationTransformTest, InvertingKeepsTheProjectiveRowAndTheKind)
{
    const an::RegistrationTransform projective =
        makeLinear(an::RegistrationTransformKind::Projective, {1.2, 0.1, 30.0, -0.2, 0.9, 15.0, 0.0004, -0.0002, 1.0});
    const an::Result<an::RegistrationTransform> inverse = an::invertRegistrationTransform(projective);
    ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
    EXPECT_EQ(inverse->kind(), an::RegistrationTransformKind::Projective);

    const an::PixelCoordinate point{.x = 640.5, .y = 480.5};
    const an::Result<an::PixelCoordinate> forward = an::mapRegistrationPoint(projective, point);
    ASSERT_TRUE(forward.hasValue()) << forward.status().message();
    const an::Result<an::PixelCoordinate> back = an::mapRegistrationPoint(*inverse, *forward);
    ASSERT_TRUE(back.hasValue()) << back.status().message();
    EXPECT_NEAR(back->x, point.x, 1.0e-8);
    EXPECT_NEAR(back->y, point.y, 1.0e-8);

    const an::RegistrationTransform affine =
        makeLinear(an::RegistrationTransformKind::Affine, {2.0, 0.5, 3.0, -0.5, 2.0, 4.0, 0.0, 0.0, 1.0});
    const an::Result<an::RegistrationTransform> affineInverse = an::invertRegistrationTransform(affine);
    ASSERT_TRUE(affineInverse.hasValue()) << affineInverse.status().message();
    EXPECT_EQ(affineInverse->kind(), an::RegistrationTransformKind::Affine);

    an::RegistrationTransformRequest polynomialRequest;
    polynomialRequest.polynomial = identityPolynomial(3u);
    const an::Result<an::RegistrationTransform> polynomial = an::RegistrationTransform::create(polynomialRequest);
    ASSERT_TRUE(polynomial.hasValue()) << polynomial.status().message();
    const an::Result<an::RegistrationTransform> refused = an::invertRegistrationTransform(*polynomial);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
}

TEST(RegistrationTransformTest, EveryPolynomialOrderMapsAndInvertsAsItsOwnDefinition)
{
    // The evaluation is compiled once for each order a polynomial may carry, so each order is a
    // separate body of code that has to agree with the one definition of what the polynomial is.
    // This walks every one of them against a sum written out here, and then back through the
    // inverse, which is the only consumer that asks for the derivatives.
    for (std::uint32_t order = an::kMinimumRegistrationPolynomialOrder;
         order <= an::kMaximumRegistrationPolynomialOrder; ++order)
    {
        an::RegistrationPolynomial polynomial;
        polynomial.order = order;
        polynomial.normalizationOrigin = {.x = 512.0, .y = 384.0};
        polynomial.normalizationScale = {.x = 512.0, .y = 384.0};
        // A distinct nonzero coefficient on every term the order carries, small enough above the
        // linear pair that the correction stays invertible over the frame.
        const std::size_t terms = an::registrationPolynomialTermCount(order);
        for (std::size_t index = 0u; index < terms; ++index)
        {
            polynomial.x[index] = 0.004 * static_cast<double>(index + 1u);
            polynomial.y[index] = -0.003 * static_cast<double>(index + 2u);
        }
        polynomial.x[1] = 1.0;
        polynomial.y[2] = 1.0;

        an::RegistrationTransformRequest request;
        request.linearKind = an::RegistrationTransformKind::Affine;
        request.sourceToReference = {1.1, 0.05, 12.0, -0.04, 0.95, -7.0, 0.0, 0.0, 1.0};
        request.polynomial = polynomial;
        const an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(request);
        ASSERT_TRUE(transform.hasValue()) << "order " << order << ": " << transform.status().message();

        for (const an::PixelCoordinate source :
             {an::PixelCoordinate{.x = 0.5, .y = 0.5}, an::PixelCoordinate{.x = 300.25, .y = 700.75},
              an::PixelCoordinate{.x = 1023.5, .y = 767.5}})
        {
            const an::Result<an::PixelCoordinate> mapped = an::mapRegistrationPoint(*transform, source);
            ASSERT_TRUE(mapped.hasValue()) << "order " << order << ": " << mapped.status().message();

            // The same two sums, written as the contract states them: ascending total degree and,
            // within one degree, ascending vertical degree.
            const double linearX = 1.1 * source.x + 0.05 * source.y + 12.0;
            const double linearY = -0.04 * source.x + 0.95 * source.y - 7.0;
            const double nx = (linearX - polynomial.normalizationOrigin.x) / polynomial.normalizationScale.x;
            const double ny = (linearY - polynomial.normalizationOrigin.y) / polynomial.normalizationScale.y;
            double sumX = 0.0;
            double sumY = 0.0;
            std::size_t index = 0u;
            for (std::uint32_t degree = 0u; degree <= order; ++degree)
            {
                for (std::uint32_t verticalDegree = 0u; verticalDegree <= degree; ++verticalDegree)
                {
                    const double term = std::pow(nx, static_cast<double>(degree - verticalDegree)) *
                                        std::pow(ny, static_cast<double>(verticalDegree));
                    sumX += polynomial.x[index] * term;
                    sumY += polynomial.y[index] * term;
                    ++index;
                }
            }
            EXPECT_NEAR(mapped->x, polynomial.normalizationOrigin.x + sumX * polynomial.normalizationScale.x, 1.0e-9)
                << "order " << order;
            EXPECT_NEAR(mapped->y, polynomial.normalizationOrigin.y + sumY * polynomial.normalizationScale.y, 1.0e-9)
                << "order " << order;

            // What an inverse promises is a residual in the reference space, not exact recovery of
            // the source: the correction's normalization turns that residual into a source-space
            // distance the transform's own derivative decides. So the round trip is checked the way
            // the contract states it - map the answer forward again and require it to land on the
            // position that was inverted - and the source is only required back to a coarse pixel
            // fraction.
            const an::Result<an::RegistrationInverseResult> back = an::mapRegistrationPointInverse(*transform, *mapped);
            ASSERT_TRUE(back.hasValue()) << "order " << order << ": " << back.status().message();
            EXPECT_LE(back->residualPixels, an::kRegistrationInverseFinalTolerancePixels) << "order " << order;
            const an::Result<an::PixelCoordinate> again = an::mapRegistrationPoint(*transform, back->pixel);
            ASSERT_TRUE(again.hasValue()) << "order " << order << ": " << again.status().message();
            EXPECT_NEAR(again->x, mapped->x, an::kRegistrationInverseFinalTolerancePixels) << "order " << order;
            EXPECT_NEAR(again->y, mapped->y, an::kRegistrationInverseFinalTolerancePixels) << "order " << order;
            EXPECT_NEAR(back->pixel.x, source.x, 1.0e-3) << "order " << order;
            EXPECT_NEAR(back->pixel.y, source.y, 1.0e-3) << "order " << order;
        }
    }
}

TEST(RegistrationTransformTest, BoundsCoverTheFramesOwnRectangle)
{
    const an::RegistrationTransform identity;
    const an::Result<an::RegistrationBounds> bounds = an::computeRegistrationBounds(identity, 100u, 50u);
    ASSERT_TRUE(bounds.hasValue()) << bounds.status().message();
    EXPECT_NEAR(bounds->minimumX, 0.0, kTight);
    EXPECT_NEAR(bounds->minimumY, 0.0, kTight);
    EXPECT_NEAR(bounds->maximumX, 100.0, kTight);
    EXPECT_NEAR(bounds->maximumY, 50.0, kTight);
    EXPECT_NEAR(bounds->width(), 100.0, kTight);
    EXPECT_NEAR(bounds->height(), 50.0, kTight);

    // A quarter turn about the origin sends the frame into the second quadrant.
    const an::RegistrationTransform quarterTurn =
        makeLinear(an::RegistrationTransformKind::Similarity, {0.0, -1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0});
    const an::Result<an::RegistrationBounds> turned = an::computeRegistrationBounds(quarterTurn, 100u, 50u);
    ASSERT_TRUE(turned.hasValue()) << turned.status().message();
    EXPECT_NEAR(turned->minimumX, -50.0, kTight);
    EXPECT_NEAR(turned->maximumX, 0.0, kTight);
    EXPECT_NEAR(turned->minimumY, 0.0, kTight);
    EXPECT_NEAR(turned->maximumY, 100.0, kTight);

    const an::PixelCoordinate translation{.x = 1000.0, .y = 2000.0};
    const an::Result<an::RegistrationBounds> shifted = an::computeRegistrationBounds(identity, 100u, 50u, translation);
    ASSERT_TRUE(shifted.hasValue()) << shifted.status().message();
    EXPECT_NEAR(shifted->minimumX, 1000.0, kTight);
    EXPECT_NEAR(shifted->maximumY, 2050.0, kTight);
}

TEST(RegistrationTransformTest, BoundsSampleTheEdgesRatherThanTheCorners)
{
    // A polynomial correction bows the frame's edges outward, so a bound taken from the corners
    // alone is short of the real extent. This one adds `0.4 * ny * (1 - ny)` to the horizontal
    // coordinate and the mirror of it to the vertical, which is zero at every corner and a tenth
    // of the frame at the middle of each edge.
    an::RegistrationTransformRequest request;
    request.polynomial = identityPolynomial(2u, {1000.0, 1000.0});
    request.polynomial.x[2] = 0.4; // y
    request.polynomial.x[5] = -0.4; // y^2
    request.polynomial.y[1] = 0.4; // x
    request.polynomial.y[3] = -0.4; // x^2
    const an::Result<an::RegistrationTransform> bowed = an::RegistrationTransform::create(request);
    ASSERT_TRUE(bowed.hasValue()) << bowed.status().message();

    const an::Result<an::RegistrationBounds> few = an::computeRegistrationBounds(*bowed, 1000u, 1000u, {}, nullptr, 2u);
    ASSERT_TRUE(few.hasValue()) << few.status().message();
    const an::Result<an::RegistrationBounds> many =
        an::computeRegistrationBounds(*bowed, 1000u, 1000u, {}, nullptr, 201u);
    ASSERT_TRUE(many.hasValue()) << many.status().message();
    EXPECT_NEAR(few->maximumX, 1000.0, 1.0e-6);
    EXPECT_NEAR(many->maximumX, 1100.0, 1.0e-6);
    EXPECT_NEAR(many->maximumY, 1100.0, 1.0e-6);
}

TEST(RegistrationTransformTest, BoundsRefuseAnEmptyFrameAndAnUnusableSampleCount)
{
    const an::RegistrationTransform identity;
    const an::Result<an::RegistrationBounds> zeroWidth = an::computeRegistrationBounds(identity, 0u, 50u);
    ASSERT_FALSE(zeroWidth.hasValue());
    EXPECT_EQ(zeroWidth.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationBounds> oneSample =
        an::computeRegistrationBounds(identity, 10u, 10u, {}, nullptr, 1u);
    ASSERT_FALSE(oneSample.hasValue());
    EXPECT_EQ(oneSample.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::RegistrationBounds> tooMany =
        an::computeRegistrationBounds(identity, 10u, 10u, {}, nullptr, an::kMaximumRegistrationBoundarySamples + 1u);
    ASSERT_FALSE(tooMany.hasValue());
    EXPECT_EQ(tooMany.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(RegistrationTransformTest, BoundsAndCornersCorrectASourceDistortionFirst)
{
    an::SipDistortionRequest distortionRequest;
    distortionRequest.referencePixel = an::PixelCoordinate{.x = 500.0, .y = 500.0};
    distortionRequest.forwardX.order = 2u;
    distortionRequest.forwardX.coefficients[2][0] = 2.0e-5;
    distortionRequest.forwardY.order = 2u;
    distortionRequest.forwardY.coefficients[0][2] = 2.0e-5;
    const an::Result<an::SipDistortion> distortion = an::SipDistortion::create(distortionRequest);
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();

    const an::RegistrationTransform identity;
    const an::Result<an::RegistrationBounds> plain = an::computeRegistrationBounds(identity, 1000u, 1000u);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    const an::Result<an::RegistrationBounds> corrected =
        an::computeRegistrationBounds(identity, 1000u, 1000u, {}, &distortion.value());
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_GT(corrected->maximumX, plain->maximumX);
    EXPECT_GT(corrected->maximumY, plain->maximumY);
}

TEST(RegistrationTransformTest, CornersAreReportedClockwiseFromTheOrigin)
{
    const an::RegistrationTransform identity;
    const an::Result<std::array<an::PixelCoordinate, an::kRegistrationFrameCornerCount>> corners =
        an::mapRegistrationFrameCorners(identity, 640u, 480u);
    ASSERT_TRUE(corners.hasValue()) << corners.status().message();
    EXPECT_NEAR((*corners)[0].x, 0.0, kTight);
    EXPECT_NEAR((*corners)[0].y, 0.0, kTight);
    EXPECT_NEAR((*corners)[1].x, 640.0, kTight);
    EXPECT_NEAR((*corners)[1].y, 0.0, kTight);
    EXPECT_NEAR((*corners)[2].x, 640.0, kTight);
    EXPECT_NEAR((*corners)[2].y, 480.0, kTight);
    EXPECT_NEAR((*corners)[3].x, 0.0, kTight);
    EXPECT_NEAR((*corners)[3].y, 480.0, kTight);

    const an::Result<std::array<an::PixelCoordinate, an::kRegistrationFrameCornerCount>> refused =
        an::mapRegistrationFrameCorners(identity, 0u, 480u);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(RegistrationTransformTest, ARequestRoundTripsThroughToRequest)
{
    an::RegistrationTransformRequest request;
    request.linearKind = an::RegistrationTransformKind::Affine;
    request.sourceToReference = {1.1, 0.2, 3.0, -0.2, 1.1, 4.0, 0.0, 0.0, 1.0};
    request.polynomial = bendingPolynomial({2048.0, 2048.0});
    const an::Result<an::RegistrationTransform> made = an::RegistrationTransform::create(request);
    ASSERT_TRUE(made.hasValue()) << made.status().message();

    const an::Result<an::RegistrationTransform> again = an::RegistrationTransform::create(made->toRequest());
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    EXPECT_EQ(*again, *made);
}

TEST(RegistrationTransformTest, AStarFieldMatchIsTheRegistrationItsMatrixDescribes)
{
    // The matcher reports a star transform and everything that resamples a frame takes a registration
    // transform. The matrix is the same matrix, in the same pixel convention, and the model names the
    // same linear kind, so the conversion keeps both and checks the result like any other transform.
    const std::array<std::pair<an::StarTransformModel, an::RegistrationTransformKind>, 3u> kinds{
        std::pair{an::StarTransformModel::Similarity, an::RegistrationTransformKind::Similarity},
        std::pair{an::StarTransformModel::Affine, an::RegistrationTransformKind::Affine},
        std::pair{an::StarTransformModel::Projective, an::RegistrationTransformKind::Projective}};
    for (const auto& [model, kind] : kinds)
    {
        an::StarTransform matched{.model = model,
                                  .sourceToReference = {0.98, -0.17, 12.5, 0.17, 0.98, -4.25, 0.0, 0.0, 1.0}};
        if (model == an::StarTransformModel::Projective)
        {
            matched.sourceToReference[6] = 1.0e-6;
            matched.sourceToReference[7] = -2.0e-6;
        }
        const an::Result<an::RegistrationTransform> registered = an::registrationTransformOf(matched);
        ASSERT_TRUE(registered.hasValue()) << registered.status().message();
        EXPECT_EQ(registered->kind(), kind);
        EXPECT_EQ(registered->matrix(), matched.sourceToReference);

        // A point maps to the same place through either vocabulary.
        const an::Result<an::StarAlignmentPoint> viaStars =
            an::transformStarPoint(matched, an::StarAlignmentPoint{.x = 40.5, .y = 17.25});
        const an::Result<an::PixelCoordinate> viaRegistration =
            an::mapRegistrationPoint(*registered, an::PixelCoordinate{.x = 40.5, .y = 17.25});
        ASSERT_TRUE(viaStars.hasValue());
        ASSERT_TRUE(viaRegistration.hasValue());
        EXPECT_NEAR(viaStars->x, viaRegistration->x, 1.0e-12);
        EXPECT_NEAR(viaStars->y, viaRegistration->y, 1.0e-12);
    }

    // A match whose matrix is not a transform, or whose model is not one this build knows, is refused
    // rather than carried.
    const an::StarTransform singular{.model = an::StarTransformModel::Affine,
                                     .sourceToReference = {1.0, 2.0, 0.0, 2.0, 4.0, 0.0, 0.0, 0.0, 1.0}};
    EXPECT_EQ(an::registrationTransformOf(singular).status().code(), an::ErrorCode::InvalidData);
    const an::StarTransform unknown{.model = static_cast<an::StarTransformModel>(9u)};
    EXPECT_EQ(an::registrationTransformOf(unknown).status().code(), an::ErrorCode::InvalidArgument);
}
