// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Matching, robust fitting, determinism and failure tests for star alignment.

#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "RegistrationTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stop_token>
#include <vector>

namespace
{

[[nodiscard]] std::vector<an::StarAlignmentPoint> makeField(std::size_t count = 48u)
{
    std::vector<an::StarAlignmentPoint> field;
    field.reserve(count);
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    for (std::size_t index = 0u; index < count; ++index)
    {
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        const double x = 20.0 + static_cast<double>(state % 1800u) * 0.5 + static_cast<double>(index) * 0.017;
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        const double y = 15.0 + static_cast<double>(state % 1400u) * 0.5 + static_cast<double>(index) * 0.029;
        field.push_back({.x = x, .y = y});
    }
    return field;
}

[[nodiscard]] an::StarAlignmentPoint apply(const std::array<double, 9>& matrix, const an::StarAlignmentPoint& point)
{
    const double denominator = matrix[6] * point.x + matrix[7] * point.y + matrix[8];
    return {.x = (matrix[0] * point.x + matrix[1] * point.y + matrix[2]) / denominator,
            .y = (matrix[3] * point.x + matrix[4] * point.y + matrix[5]) / denominator};
}

[[nodiscard]] std::vector<an::StarAlignmentPoint> transformed(const std::vector<an::StarAlignmentPoint>& points,
                                                              const std::array<double, 9>& matrix)
{
    std::vector<an::StarAlignmentPoint> answer;
    answer.reserve(points.size());
    for (const an::StarAlignmentPoint& point : points)
    {
        answer.push_back(apply(matrix, point));
    }
    return answer;
}

void expectMatrixNear(const an::StarTransform& actual, const std::array<double, 9>& expected, double tolerance)
{
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        EXPECT_NEAR(actual.sourceToReference[index], expected[index], tolerance) << index;
    }
}

[[nodiscard]] std::uint64_t nextState(std::uint64_t& state) noexcept
{
    state ^= state << 13u;
    state ^= state >> 7u;
    state ^= state << 17u;
    return state;
}

[[nodiscard]] std::vector<an::StarAlignmentPoint> integerField(std::size_t count, std::uint64_t state)
{
    std::vector<an::StarAlignmentPoint> field;
    field.reserve(count);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double x = 40.0 + static_cast<double>(nextState(state) % 900u);
        const double y = 30.0 + static_cast<double>(nextState(state) % 700u);
        field.push_back({.x = x, .y = y});
    }
    return field;
}

void append(std::vector<an::StarAlignmentPoint>& field, const std::vector<an::StarAlignmentPoint>& more)
{
    field.insert(field.end(), more.begin(), more.end());
}

} // namespace

TEST(StarAlignmentContractTest, NamesEveryModelAndTrianglePolicyAndPublishesAReadOnlyEffect)
{
    EXPECT_STREQ(an::starTransformModelName(an::StarTransformModel::Similarity), "Similarity");
    EXPECT_STREQ(an::starTransformModelName(an::StarTransformModel::Affine), "Affine");
    EXPECT_STREQ(an::starTransformModelName(an::StarTransformModel::Projective), "Projective");
    EXPECT_STREQ(an::starTransformModelName(static_cast<an::StarTransformModel>(99)), "UnknownStarTransformModel");
    EXPECT_FALSE(an::isStarTransformModel(static_cast<an::StarTransformModel>(99)));

    EXPECT_STREQ(an::starTrianglePolicyName(an::StarTrianglePolicy::Automatic), "Automatic");
    EXPECT_STREQ(an::starTrianglePolicyName(an::StarTrianglePolicy::Exhaustive), "Exhaustive");
    EXPECT_STREQ(an::starTrianglePolicyName(an::StarTrianglePolicy::Bounded), "Bounded");
    EXPECT_EQ(an::checkOperationEffect(an::kStarAlignmentEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kStarAlignmentEffect.samples, an::SampleEffect::Reads);
}

TEST(StarAlignmentFitTest, RecoversSimilarityAffineAndProjectiveCorrespondences)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::StarAlignmentPoint> source = makeField(24u);
    const std::array<std::array<double, 9>, 3> matrices{{
        {1.027054981, -0.218307276, 31.5, 0.218307276, 1.027054981, -17.25, 0.0, 0.0, 1.0},
        {1.03, -0.17, 41.0, 0.09, 0.96, -22.0, 0.0, 0.0, 1.0},
        {1.01, -0.04, 18.0, 0.03, 0.98, -12.0, 0.00004, -0.00003, 1.0},
    }};
    const std::array<an::StarTransformModel, 3> models{
        an::StarTransformModel::Similarity, an::StarTransformModel::Affine, an::StarTransformModel::Projective};

    for (std::size_t index = 0u; index < models.size(); ++index)
    {
        const std::vector<an::StarAlignmentPoint> reference = transformed(source, matrices[index]);
        an::StarAlignmentRequest request;
        request.model = models[index];
        request.minimumMatches = models[index] == an::StarTransformModel::Projective ? 8u : 6u;
        request.inlierThreshold = 0.01;
        an::Result<an::StarAlignmentResult> fitted = an::fitStarTransform(execution, source, reference, request);
        ASSERT_TRUE(fitted.hasValue()) << fitted.status().message();
        expectMatrixNear(fitted->transform, matrices[index], 2.0e-8);
        EXPECT_EQ(fitted->quality.inlierMatches, source.size());
        EXPECT_LT(fitted->quality.rootMeanSquareResidual, 1.0e-8);
        EXPECT_EQ(fitted->quality.ransacAttempts, 1u);
    }
}

TEST(StarAlignmentFitTest, DeterministicRansacRejectsOutliersAndReportsQuality)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::StarAlignmentPoint> source = makeField(40u);
    const std::array<double, 9> matrix{1.01, -0.08, 24.0, 0.05, 0.97, -11.0, 0.0, 0.0, 1.0};
    std::vector<an::StarAlignmentPoint> reference = transformed(source, matrix);
    for (std::size_t index : {3u, 9u, 17u, 28u, 35u})
    {
        reference[index].x += 100.0 + static_cast<double>(index);
        reference[index].y -= 70.0;
    }
    an::StarAlignmentRequest request;
    request.model = an::StarTransformModel::Affine;
    request.minimumMatches = 12u;
    request.inlierThreshold = 0.25;
    request.ransacIterations = 256u;
    request.seed = 8303u;

    an::Result<an::StarAlignmentResult> first = an::fitStarTransform(execution, source, reference, request);
    an::Result<an::StarAlignmentResult> second = an::fitStarTransform(execution, source, reference, request);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    expectMatrixNear(first->transform, matrix, 1.0e-9);
    EXPECT_EQ(first->quality.inlierMatches, 35u);
    EXPECT_EQ(first->quality.rejectedOutliers, 5u);
    EXPECT_EQ(first->matches, second->matches);
    EXPECT_EQ(first->transform, second->transform);
    EXPECT_EQ(first->quality, second->quality);
}

TEST(StarAlignmentFitTest, RefusesACollinearFieldAsDegenerateGeometry)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    std::vector<an::StarAlignmentPoint> source;
    std::vector<an::StarAlignmentPoint> reference;
    for (std::uint32_t index = 0u; index < 12u; ++index)
    {
        source.push_back({.x = static_cast<double>(index), .y = 2.0 * index + 1.0});
        reference.push_back({.x = 3.0 * index + 7.0, .y = 6.0 * index - 4.0});
    }
    an::StarAlignmentRequest request;
    request.model = an::StarTransformModel::Affine;
    request.minimumMatches = 6u;
    an::Result<an::StarAlignmentResult> fitted = an::fitStarTransform(execution, source, reference, request);
    ASSERT_FALSE(fitted.hasValue());
    EXPECT_EQ(fitted.status().code(), an::ErrorCode::InvalidData);
}

TEST(StarAlignmentMatchTest, MatchesBrightnessOrderedFieldsAndReturnsSourceOrderedInliers)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    std::vector<an::StarAlignmentPoint> source = makeField(52u);
    const std::array<double, 9> matrix{0.98, -0.12, 37.0, 0.12, 0.98, -19.0, 0.0, 0.0, 1.0};
    std::vector<an::StarAlignmentPoint> reference = transformed(source, matrix);
    reference.push_back({.x = 900.0, .y = 40.0});
    reference.push_back({.x = 110.0, .y = 760.0});
    an::StarAlignmentRequest request;
    request.model = an::StarTransformModel::Affine;
    request.minimumTriangleSide = 4.0;
    request.minimumMatches = 12u;
    request.inlierThreshold = 0.2;
    request.seed = 8311u;

    an::Result<an::StarAlignmentResult> aligned = an::alignStarFields(execution, source, reference, request);
    ASSERT_TRUE(aligned.hasValue()) << aligned.status().message();
    expectMatrixNear(aligned->transform, matrix, 1.0e-8);
    ASSERT_GE(aligned->matches.size(), 40u);
    EXPECT_GT(aligned->quality.workingBytes, 0u);
    EXPECT_TRUE(std::is_sorted(
        aligned->matches.begin(), aligned->matches.end(),
        [](const an::StarMatch& left, const an::StarMatch& right) { return left.sourceIndex < right.sourceIndex; }));
    EXPECT_EQ(aligned->quality.inlierMatches, aligned->matches.size());
}

TEST(StarAlignmentMatchTest, PreparedReferenceIsImmutableReusableAndFingerprintBound)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext one = registration_test::makeExecution(engine, 1u);
    an::ExecutionContext eight = registration_test::makeExecution(engine, 8u);
    const std::vector<an::StarAlignmentPoint> source = makeField(64u);
    const std::array<double, 9> matrix{1.02, -0.09, 15.0, 0.04, 0.99, -8.0, 0.0, 0.0, 1.0};
    const std::vector<an::StarAlignmentPoint> reference = transformed(source, matrix);
    an::StarAlignmentRequest request;
    request.model = an::StarTransformModel::Affine;
    request.minimumTriangleSide = 4.0;
    request.minimumMatches = 16u;
    request.seed = 99u;

    an::Result<an::PreparedStarAlignmentReference> prepared =
        an::prepareStarAlignmentReference(one, reference, request);
    ASSERT_TRUE(prepared.hasValue()) << prepared.status().message();
    EXPECT_TRUE(prepared->valid());
    EXPECT_EQ(prepared->pointCount(), reference.size());
    EXPECT_GT(prepared->triangleCount(), 0u);
    const std::uint64_t fingerprint = prepared->fingerprint();

    an::Result<an::StarAlignmentResult> serial = an::alignStarFields(one, source, *prepared);
    an::Result<an::StarAlignmentResult> parallel = an::alignStarFields(eight, source, *prepared);
    ASSERT_TRUE(serial.hasValue()) << serial.status().message();
    ASSERT_TRUE(parallel.hasValue()) << parallel.status().message();
    EXPECT_EQ(serial->matches, parallel->matches);
    EXPECT_EQ(serial->transform, parallel->transform);
    an::StarAlignmentQuality serialQuality = serial->quality;
    an::StarAlignmentQuality parallelQuality = parallel->quality;
    EXPECT_EQ(serialQuality.admittedConcurrency, 1u);
    EXPECT_GT(parallelQuality.admittedConcurrency, 1u);
    serialQuality.admittedConcurrency = 0u;
    parallelQuality.admittedConcurrency = 0u;
    EXPECT_EQ(serialQuality, parallelQuality);
    EXPECT_EQ(prepared->fingerprint(), fingerprint);

    std::vector<an::StarAlignmentPoint> changed = reference;
    changed[0].x += 0.125;
    an::Result<an::PreparedStarAlignmentReference> other = an::prepareStarAlignmentReference(one, changed, request);
    ASSERT_TRUE(other.hasValue());
    EXPECT_NE(other->fingerprint(), fingerprint);
}

TEST(StarAlignmentMatchTest, TheIndexedTriangleSearchKeepsWhatAnExhaustiveSearchKeeps)
{
    // The reference backend offers every reference triangle to every source triangle, which is the
    // answer the prepared index has to reproduce. Each field is built so that which neighbours a search
    // keeps reaches the answer: unmatched stars on both sides and a single vote per pairing let decoy
    // triangles reach the fit, where the counts of candidates and of rejected outliers record them; a
    // copy sheared to near the tolerance puts true partners at the edge of the radius; a mirror-symmetric
    // field ties every triangle with its mirror image at distance zero, so that with one neighbour the
    // index alone decides, at a tolerance small enough to give the grid its coarsest cells; and a
    // tolerance of a quarter fills sixteen-entry lists from far around each query.
    struct Case
    {
        const char* name = "";
        std::vector<an::StarAlignmentPoint> source;
        std::vector<an::StarAlignmentPoint> reference;
        an::StarAlignmentRequest request;
    };
    an::StarAlignmentRequest base;
    base.model = an::StarTransformModel::Similarity;
    base.minimumTriangleSide = 4.0;
    base.minimumVotes = 1u;
    base.minimumMatches = 8u;
    base.inlierThreshold = 0.5;
    base.minimumInlierFraction = 0.2;
    base.seed = 4127u;
    const std::array<double, 9> similarity{0.98, -0.12, 37.0, 0.12, 0.98, -19.0, 0.0, 0.0, 1.0};
    std::vector<Case> cases;

    Case decoys{.name = "decoys", .source = makeField(24u), .reference = {}, .request = base};
    decoys.reference = transformed(decoys.source, similarity);
    append(decoys.source, integerField(10u, 0x51ed270b27f4a3c1ull));
    append(decoys.reference, integerField(10u, 0x2545f4914f6cdd1dull));
    cases.push_back(decoys);

    Case sheared{.name = "sheared", .source = makeField(26u), .reference = {}, .request = base};
    sheared.reference = transformed(sheared.source, {1.04, 0.03, 20.0, -0.02, 0.97, -15.0, 0.0, 0.0, 1.0});
    append(sheared.source, integerField(6u, 0x9fb21c651e98df25ull));
    append(sheared.reference, integerField(6u, 0xd1b54a32d192ed03ull));
    sheared.request.model = an::StarTransformModel::Affine;
    sheared.request.triangleRatioTolerance = 0.02;
    cases.push_back(sheared);

    Case mirrored{
        .name = "mirrored", .source = integerField(12u, 0x94d049bb133111ebull), .reference = {}, .request = base};
    for (std::size_t index = 0u; index < 12u; ++index)
    {
        mirrored.source.push_back({.x = 1000.0 - mirrored.source[index].x, .y = mirrored.source[index].y});
    }
    for (const an::StarAlignmentPoint& point : mirrored.source)
    {
        mirrored.reference.push_back({.x = 800.0 - point.y, .y = point.x + 25.0});
    }
    append(mirrored.source, integerField(8u, 0xbf58476d1ce4e5b9ull));
    append(mirrored.reference, integerField(8u, 0x632be59bd9b4e019ull));
    mirrored.request.triangleRatioTolerance = 1.0e-6;
    mirrored.request.triangleToleranceMinimumScale = 0.25;
    mirrored.request.maximumTriangleNeighbours = 1u;
    cases.push_back(mirrored);

    Case wide{.name = "wide", .source = makeField(22u), .reference = {}, .request = base};
    wide.reference = transformed(wide.source, similarity);
    append(wide.source, integerField(6u, 0x8cb92ba72f3d8dd7ull));
    append(wide.reference, integerField(6u, 0xc2b2ae3d27d4eb4full));
    wide.request.triangleRatioTolerance = 0.25;
    wide.request.triangleToleranceMinimumScale = 0.25;
    wide.request.maximumTriangleNeighbours = 16u;
    wide.request.minimumVotes = 2u;
    cases.push_back(wide);

    // Preparation, the exhaustive search and the indexed one hold four, four and up to four workers.
    an::EngineContext engine = registration_test::makeEngine(12u);
    an::ExecutionContext preparation = registration_test::makeExecution(engine);
    an::Result<an::ExecutionContext> exhaustive =
        an::ExecutionContext::create(engine, {.maxConcurrency = 4u, .backend = an::ComputeBackend::Reference});
    ASSERT_TRUE(exhaustive.hasValue()) << exhaustive.status().message();
    std::size_t compared = 0u;
    for (const Case& scenario : cases)
    {
        an::Result<an::PreparedStarAlignmentReference> prepared =
            an::prepareStarAlignmentReference(preparation, scenario.reference, scenario.request);
        ASSERT_TRUE(prepared.hasValue()) << scenario.name << ": " << prepared.status().message();
        const an::Result<an::StarAlignmentResult> expected =
            an::alignStarFields(*exhaustive, scenario.source, *prepared);
        ASSERT_TRUE(expected.hasValue()) << scenario.name << ": " << expected.status().message();
        for (const std::uint32_t concurrency : {1u, 4u})
        {
            an::ExecutionContext indexed = registration_test::makeExecution(engine, concurrency);
            const an::Result<an::StarAlignmentResult> actual = an::alignStarFields(indexed, scenario.source, *prepared);
            ASSERT_TRUE(actual.hasValue()) << scenario.name << ": " << actual.status().message();
            EXPECT_EQ(actual->matches, expected->matches) << scenario.name;
            EXPECT_EQ(actual->transform, expected->transform) << scenario.name;
            an::StarAlignmentQuality actualQuality = actual->quality;
            an::StarAlignmentQuality expectedQuality = expected->quality;
            actualQuality.admittedConcurrency = 0u;
            expectedQuality.admittedConcurrency = 0u;
            EXPECT_EQ(actualQuality, expectedQuality)
                << scenario.name << ": candidates " << actualQuality.candidateMatches << " against "
                << expectedQuality.candidateMatches << ", rejected " << actualQuality.rejectedOutliers << " against "
                << expectedQuality.rejectedOutliers;
            ++compared;
        }
    }
    EXPECT_EQ(compared, 2u * cases.size());
}

TEST(StarAlignmentMatchTest, DissimilarFieldsAndCancelledPreparationReturnFailures)
{
    an::EngineContext engine = registration_test::makeEngine();
    an::ExecutionContext execution = registration_test::makeExecution(engine);
    const std::vector<an::StarAlignmentPoint> source = makeField(32u);
    std::vector<an::StarAlignmentPoint> reference = makeField(32u);
    for (std::size_t index = 0u; index < reference.size(); ++index)
    {
        reference[index].x = 4.0 * static_cast<double>(index) + std::sin(static_cast<double>(index));
        reference[index].y = 600.0 - 7.0 * static_cast<double>(index) + 0.3 * static_cast<double>(index * index);
    }
    an::StarAlignmentRequest request;
    request.minimumTriangleSide = 4.0;
    request.minimumMatches = 10u;
    request.inlierThreshold = 0.1;
    an::Result<an::StarAlignmentResult> missing = an::alignStarFields(execution, source, reference, request);
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::EmptySelection);

    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext cancelled = registration_test::makeExecution(engine, 4u, stop.get_token());
    an::Result<an::PreparedStarAlignmentReference> stopped =
        an::prepareStarAlignmentReference(cancelled, source, request);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
}

TEST(StarAlignmentMatchTest, MatchingHonoursCancellationAndTheEngineMemoryBudget)
{
    an::EngineContext preparationEngine = registration_test::makeEngine();
    an::ExecutionContext preparation = registration_test::makeExecution(preparationEngine);
    const std::vector<an::StarAlignmentPoint> source = makeField(48u);
    const std::array<double, 9> matrix{0.99, -0.07, 23.0, 0.07, 0.99, -14.0, 0.0, 0.0, 1.0};
    const std::vector<an::StarAlignmentPoint> reference = transformed(source, matrix);
    an::StarAlignmentRequest request;
    request.model = an::StarTransformModel::Similarity;
    request.minimumTriangleSide = 4.0;
    request.minimumMatches = 12u;
    an::Result<an::PreparedStarAlignmentReference> prepared =
        an::prepareStarAlignmentReference(preparation, reference, request);
    ASSERT_TRUE(prepared.hasValue()) << prepared.status().message();

    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext cancelled = registration_test::makeExecution(preparationEngine, 4u, stop.get_token());
    an::Result<an::StarAlignmentResult> stopped = an::alignStarFields(cancelled, source, *prepared);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);

    an::EngineContext boundedEngine = registration_test::makeEngine(4u, 4096u);
    an::ExecutionContext bounded = registration_test::makeExecution(boundedEngine);
    an::Result<an::StarAlignmentResult> refused = an::alignStarFields(bounded, source, *prepared);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    ASSERT_NE(boundedEngine.memoryResource(), nullptr);
    const an::MemoryCounters counters = boundedEngine.memoryResource()->counters();
    EXPECT_EQ(counters.liveBytes, 0u);
    EXPECT_EQ(counters.liveAllocations, 0u);
    EXPECT_GT(counters.deniedAllocations, 0u);
}

TEST(StarAlignmentContractTest, MappingRefusesAProjectivePointOnTheHorizon)
{
    an::StarTransform transform;
    transform.model = an::StarTransformModel::Projective;
    transform.sourceToReference = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, -2.0};
    an::Result<an::StarAlignmentPoint> mapped = an::transformStarPoint(transform, {.x = 2.0, .y = 5.0});
    ASSERT_FALSE(mapped.hasValue());
    EXPECT_EQ(mapped.status().code(), an::ErrorCode::InvalidData);
}
