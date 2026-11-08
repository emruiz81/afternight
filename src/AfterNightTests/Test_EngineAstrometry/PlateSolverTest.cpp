// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract, stage, failure, and deterministic fixture tests for the plate solver.

#include <AfterNightEngine/Algorithms/Astrometry/GnomonicProjection.h>
#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>
#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "CatalogTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

using catalog_test::buildOrFail;
using catalog_test::makeEngine;
using catalog_test::makeExecution;

constexpr std::uint32_t kWidth = 1024u;
constexpr std::uint32_t kHeight = 768u;
constexpr double kScaleArcsec = 2.4;
constexpr an::SkyCoordinate kTrueCenter{.rightAscensionDegrees = 83.6331, .declinationDegrees = 22.0145};

struct SolveFixture
{
    an::WcsSolution truth;
    std::vector<an::PlateSolveStar> imageStars;
    std::vector<an::CatalogEntry> catalogEntries;
};

[[nodiscard]] an::WcsSolution makeTruth(const an::SipDistortion& distortion = {},
                                        an::SkyCoordinate center = kTrueCenter, double scaleArcsec = kScaleArcsec)
{
    constexpr double angle = 17.0 * std::numbers::pi / 180.0;
    const double scaleDegrees = scaleArcsec / 3600.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 0.5 * kWidth, .y = 0.5 * kHeight};
    request.referencePoint = center;
    request.linear = {.cd11 = -scaleDegrees * cosine,
                      .cd12 = scaleDegrees * sine,
                      .cd21 = scaleDegrees * sine,
                      .cd22 = scaleDegrees * cosine};
    request.imageWidth = kWidth;
    request.imageHeight = kHeight;
    request.distortion = distortion;
    const an::Result<an::WcsSolution> made = an::WcsSolution::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.value();
}

[[nodiscard]] an::SipDistortion makeDistortion()
{
    an::SipDistortionRequest request;
    request.referencePixel = {.x = 0.5 * kWidth, .y = 0.5 * kHeight};
    request.forwardX.order = 2u;
    request.forwardX.coefficients[2u][0u] = 1.8e-6;
    request.forwardX.coefficients[0u][2u] = -5.0e-7;
    request.forwardY.order = 2u;
    request.forwardY.coefficients[1u][1u] = -1.4e-6;
    const an::Result<an::SipDistortion> made = an::SipDistortion::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::SipDistortion{};
}

[[nodiscard]] SolveFixture makeFixture(bool distorted = false, an::SkyCoordinate center = kTrueCenter,
                                       double scaleArcsec = kScaleArcsec)
{
    const an::WcsSolution truth =
        distorted ? makeTruth(makeDistortion(), center, scaleArcsec) : makeTruth({}, center, scaleArcsec);
    std::vector<an::PlateSolveStar> stars;
    std::vector<an::CatalogEntry> entries;
    stars.reserve(42u);
    entries.reserve(44u);
    std::uint32_t state = 0x6d2b79f5u;
    const auto nextCoordinate = [&state]() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    for (std::uint32_t ordinal = 0u; ordinal < 42u; ++ordinal)
    {
        // A regular lattice admits nearly exact but incorrect affine correspondences. Use the
        // same deterministic irregular geometry as the retained benchmark fixture so the chosen
        // correspondence is a solver result rather than a platform-dependent tie break.
        const an::PixelCoordinate pixel{.x = 48.5 + static_cast<double>(nextCoordinate() % 928000u) / 1000.0,
                                        .y = 48.5 + static_cast<double>(nextCoordinate() % 672000u) / 1000.0};
        const an::Result<an::SkyCoordinate> direction = truth.pixelToWorld(pixel);
        EXPECT_TRUE(direction.hasValue()) << direction.status().message();
        stars.push_back(an::PlateSolveStar{.position = pixel});

        an::CatalogEntry entry;
        entry.identifier = "fixture-" + std::to_string(ordinal);
        entry.coordinate = direction.value();
        entry.magnitude = 4.0 + 0.1 * ordinal;
        entries.push_back(std::move(entry));
    }

    an::CatalogEntry extended;
    extended.identifier = "extended-decoy";
    extended.coordinate = center;
    extended.magnitude = 1.0;
    extended.extent =
        an::CatalogExtent{.majorAxisArcminutes = 3.0, .minorAxisArcminutes = 2.0, .positionAngleDegrees = 10.0};
    entries.push_back(std::move(extended));

    an::CatalogEntry distant;
    distant.identifier = "distant-decoy";
    distant.coordinate = {.rightAscensionDegrees = 110.0, .declinationDegrees = -10.0};
    distant.magnitude = 2.0;
    entries.push_back(std::move(distant));
    return SolveFixture{.truth = truth, .imageStars = std::move(stars), .catalogEntries = std::move(entries)};
}

[[nodiscard]] an::PlateSolveRequest solveRequest(an::SkyCoordinate center = kTrueCenter,
                                                 double scaleArcsec = kScaleArcsec)
{
    an::PlateSolveRequest request;
    request.imageWidth = kWidth;
    request.imageHeight = kHeight;
    request.initialCenter = center;
    request.approximateScaleArcsecPerPixel = scaleArcsec;
    request.searchRadiusDegrees = 1.2;
    request.magnitudeLimit = 14.0;
    request.minimumMatchedStars = 12u;
    request.maximumRmsResidualPixels = 1.5;
    return request;
}

[[nodiscard]] an::SkyCatalog fixtureCatalog(const an::ExecutionContext& execution, const SolveFixture& fixture)
{
    return buildOrFail(execution, fixture.catalogEntries,
                       an::SkyCatalogRequest{.photometricBand = "G", .buildNameIndex = false});
}

TEST(PlateSolvePlanTest, SeparatesTheFieldRadiusFromTheTrialCenterSearch)
{
    an::PlateSolveRequest request;
    request.imageWidth = 1024u;
    request.imageHeight = 768u;
    request.initialCenter = kTrueCenter;
    request.approximateScaleArcsecPerPixel = 6.0;
    request.searchRadiusDegrees = 12.0;

    const an::Result<an::PlateSolvePlan> plan = an::planPlateSolve(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_NEAR(plan->catalogQueryRadiusDegrees(), 1.3333333333333333, 1.0e-12);
    EXPECT_EQ(plan->request().searchRadiusDegrees, 12.0);
    EXPECT_EQ(plan->trialCenterCount(), 37u);

    request.searchRadiusDegrees = 1.0;
    const an::Result<an::PlateSolvePlan> narrowerSearch = an::planPlateSolve(request);
    ASSERT_TRUE(narrowerSearch.hasValue()) << narrowerSearch.status().message();
    EXPECT_DOUBLE_EQ(narrowerSearch->catalogQueryRadiusDegrees(), plan->catalogQueryRadiusDegrees());
}

TEST(PlateSolvePlanTest, ValidatesEveryBoundAndStatesTheScaleDefault)
{
    an::PlateSolveRequest request;
    request.imageWidth = 100u;
    request.imageHeight = 80u;
    const an::Result<an::PlateSolvePlan> defaulted = an::planPlateSolve(request);
    ASSERT_TRUE(defaulted.hasValue()) << defaulted.status().message();
    EXPECT_DOUBLE_EQ(defaulted->effectiveScaleArcsecPerPixel(), an::kDefaultPlateSolveScaleArcsecPerPixel);

    request.imageWidth = 0u;
    EXPECT_EQ(an::planPlateSolve(request).code(), an::ErrorCode::InvalidArgument);
    request.imageWidth = 100u;
    request.initialCenter = an::SkyCoordinate{.rightAscensionDegrees = 360.0, .declinationDegrees = 0.0};
    EXPECT_EQ(an::planPlateSolve(request).code(), an::ErrorCode::InvalidArgument);
    request.initialCenter.reset();
    request.approximateScaleArcsecPerPixel = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::planPlateSolve(request).code(), an::ErrorCode::InvalidArgument);
    request.approximateScaleArcsecPerPixel = 2.0;
    request.minimumMatchedStars = 161u;
    EXPECT_EQ(an::planPlateSolve(request).code(), an::ErrorCode::InvalidArgument);
}

TEST(PlateSolveInputTest, SelectionIsBoundedSpatialAndCancellationUsesTheExecution)
{
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::PlateSolveRequest request = solveRequest();
    request.maximumImageStars = 12u;
    request.minimumMatchedStars = 8u;
    const an::Result<an::PlateSolvePlan> plan = an::planPlateSolve(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    // A lattice over the whole frame, which leaves 51 of its stars - fewer than sixty - inside the matching disc, so
    // the selection is the round selection over the frame.
    std::vector<an::PlateSolveStar> stars;
    for (std::uint32_t row = 0u; row < 10u; ++row)
    {
        for (std::uint32_t column = 0u; column < 10u; ++column)
        {
            stars.push_back({.position = {.x = 20.5 + 108.0 * column, .y = 20.5 + 80.0 * row}});
        }
    }
    const an::Result<an::PlateSolveInput> selected = an::preparePlateSolveInput(execution, stars, *plan);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->suppliedStarCount, 100u);
    EXPECT_EQ(selected->stars.size(), 12u);
    EXPECT_NE(selected->stars.front().position.x, selected->stars.back().position.x);

    stars.front().position.x = 0.0;
    EXPECT_EQ(an::preparePlateSolveInput(execution, stars, *plan).code(), an::ErrorCode::InvalidData);

    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext stopped = makeExecution(engine, 4u, stop.get_token());
    EXPECT_EQ(an::preparePlateSolveInput(stopped, selected->stars, *plan).code(), an::ErrorCode::Cancelled);
}

TEST(PlateSolveStageTest, CatalogSearchKeepsPointSourcesAndReportsItsWork)
{
    const SolveFixture fixture = makeFixture();
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = fixtureCatalog(execution, fixture);
    const an::Result<an::PlateSolvePlan> plan = an::planPlateSolve(solveRequest());
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::Result<an::PlateSolveInput> input = an::preparePlateSolveInput(execution, fixture.imageStars, *plan);
    ASSERT_TRUE(input.hasValue()) << input.status().message();

    const an::Result<an::PlateSolveCatalogSearchResult> search =
        an::searchPlateSolveCatalog(execution, catalog, *input, *plan, kTrueCenter);
    ASSERT_TRUE(search.hasValue()) << search.status().message();
    EXPECT_GE(search->stars.size(), 40u);
    EXPECT_EQ(search->rejectedExtendedEntries, 1u);
    EXPECT_EQ(search->rejectedProjectionEntries, 0u);
    EXPECT_GE(search->admittedCatalogEntries, fixture.imageStars.size() + 1u);
    EXPECT_FALSE(search->queryTruncated);
}

TEST(PlateSolveStageTest, MatchingAndFixedCenterFitRecoverTheSyntheticLinearWcs)
{
    const SolveFixture fixture = makeFixture();
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = fixtureCatalog(execution, fixture);
    const an::Result<an::PlateSolvePlan> plan = an::planPlateSolve(solveRequest());
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    const an::Result<an::PlateSolveInput> input = an::preparePlateSolveInput(execution, fixture.imageStars, *plan);
    ASSERT_TRUE(input.hasValue()) << input.status().message();
    const an::Result<an::PlateSolveCatalogSearchResult> search =
        an::searchPlateSolveCatalog(execution, catalog, *input, *plan, kTrueCenter);
    ASSERT_TRUE(search.hasValue()) << search.status().message();
    const an::Result<an::PlateSolveMatchResult> matches = an::matchPlateSolveStars(execution, *input, *search, *plan);
    ASSERT_TRUE(matches.hasValue()) << matches.status().message();
    EXPECT_GE(matches->matches.size(), 20u);
    EXPECT_GT(matches->imageTriangles, 0u);
    EXPECT_GT(matches->peakWorkingBytes, 0u);

    const an::Result<an::PlateSolveFitResult> fit = an::fitPlateSolveModel(execution, *matches, kTrueCenter, *plan);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_GE(fit->matches.size(), 20u);
    EXPECT_LT(fit->finalRmsResidualPixels, 1.0e-5);
    EXPECT_EQ(fit->sipOrder, 0u);
    EXPECT_NEAR(fit->solution.meanPixelScaleArcsecPerPixel(), kScaleArcsec, 1.0e-6);
    EXPECT_NEAR(fit->solution.orientationDegrees(), fixture.truth.orientationDegrees(), 1.0e-6);
}

TEST(PlateSolverTest, ExactHintProducesAFullWcsAndTypedDiagnostics)
{
    const SolveFixture fixture = makeFixture();
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = fixtureCatalog(execution, fixture);

    const an::Result<an::PlateSolveResult> solved =
        an::solvePlate(execution, catalog, fixture.imageStars, solveRequest());
    ASSERT_TRUE(solved.hasValue()) << solved.status().message();
    EXPECT_TRUE(solved->diagnostics.usedExactHint);
    EXPECT_FALSE(solved->diagnostics.usedNearSearch);
    EXPECT_EQ(solved->diagnostics.suppliedImageStars, fixture.imageStars.size());
    EXPECT_GE(solved->diagnostics.matchedStars, 20u);
    EXPECT_GE(solved->diagnostics.catalogQueries, 2u);
    EXPECT_GT(solved->diagnostics.peakWorkingBytes, 0u);
    EXPECT_NEAR(solved->solution.meanPixelScaleArcsecPerPixel(), kScaleArcsec, 1.0e-6);

    const an::Result<an::SkyCoordinate> center = solved->solution.fieldCenter();
    ASSERT_TRUE(center.hasValue()) << center.status().message();
    EXPECT_LT(an::angularSeparationDegrees(*center, kTrueCenter) * 3600.0, 1.0e-3);
}

TEST(PlateSolverTest, OffsetHintUsesTheDeterministicNearSearch)
{
    const SolveFixture fixture = makeFixture();
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = fixtureCatalog(execution, fixture);
    an::PlateSolveRequest request = solveRequest();
    const an::Result<an::SkyCoordinate> hint =
        an::inverseGnomonic(an::GnomonicCoordinate{.xDegrees = -1.2, .yDegrees = 0.0}, kTrueCenter);
    ASSERT_TRUE(hint.hasValue()) << hint.status().message();
    request.initialCenter = *hint;

    const an::Result<an::PlateSolveResult> solved = an::solvePlate(execution, catalog, fixture.imageStars, request);
    ASSERT_TRUE(solved.hasValue()) << solved.status().message();
    EXPECT_TRUE(solved->diagnostics.usedNearSearch);
    EXPECT_GT(solved->diagnostics.examinedTrialCenters, 1u);
    const an::Result<an::SkyCoordinate> center = solved->solution.fieldCenter();
    ASSERT_TRUE(center.hasValue()) << center.status().message();
    EXPECT_LT(an::angularSeparationDegrees(*center, kTrueCenter) * 3600.0, 1.0e-2);
}

TEST(PlateSolverTest, BlindSearchVisitsTheDocumentedHealpixSequence)
{
    const an::HealpixGrid grid{.order = 0u, .ordering = an::HealpixOrdering::Ring};
    const an::Result<an::SkyCoordinate> trueCenter =
        an::healpixPixelCenter(an::HealpixPixelIndex{.grid = grid, .value = 0u});
    ASSERT_TRUE(trueCenter.hasValue()) << trueCenter.status().message();
    constexpr double blindScaleArcsec = 270.0;
    const SolveFixture fixture = makeFixture(false, *trueCenter, blindScaleArcsec);
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = fixtureCatalog(execution, fixture);
    an::PlateSolveRequest request = solveRequest(*trueCenter, blindScaleArcsec);
    request.initialCenter.reset();

    const an::Result<an::PlateSolvePlan> plan = an::planPlateSolve(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    EXPECT_EQ(plan->blindHealpixOrder(), 0u);
    EXPECT_EQ(plan->trialCenterCount(), 12u);

    const an::Result<an::PlateSolveResult> solved = an::solvePlate(execution, catalog, fixture.imageStars, request);
    ASSERT_TRUE(solved.hasValue()) << solved.status().message();
    EXPECT_TRUE(solved->diagnostics.usedBlindSearch);
    EXPECT_FALSE(solved->diagnostics.usedExactHint);
    EXPECT_FALSE(solved->diagnostics.usedNearSearch);
    EXPECT_EQ(solved->diagnostics.examinedTrialCenters, 1u);
    const an::Result<an::SkyCoordinate> center = solved->solution.fieldCenter();
    ASSERT_TRUE(center.hasValue()) << center.status().message();
    EXPECT_LT(an::angularSeparationDegrees(*center, *trueCenter) * 3600.0, 1.0e-3);
}

TEST(PlateSolverTest, ARealPolynomialIsRetainedOnlyWhenItImprovesTheFit)
{
    const SolveFixture fixture = makeFixture(true);
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = fixtureCatalog(execution, fixture);

    const an::Result<an::PlateSolveResult> solved =
        an::solvePlate(execution, catalog, fixture.imageStars, solveRequest());
    ASSERT_TRUE(solved.hasValue()) << solved.status().message();
    EXPECT_TRUE(solved->diagnostics.fittedSipDistortion);
    EXPECT_TRUE(solved->solution.distortion().hasForwardModel());
    EXPECT_LT(solved->matches.front().residualPixels, 0.1);
}

TEST(PlateSolverTest, ResultIsIndependentOfTheSchedulerGrant)
{
    const SolveFixture fixture = makeFixture();
    an::EngineContext engine = makeEngine(8u);
    const an::SkyCatalog catalog = fixtureCatalog(makeExecution(engine), fixture);

    an::Result<an::PlateSolveResult> narrow = [&]() {
        const an::ExecutionContext execution = makeExecution(engine, 1u);
        return an::solvePlate(execution, catalog, fixture.imageStars, solveRequest());
    }();
    ASSERT_TRUE(narrow.hasValue()) << narrow.status().message();
    an::Result<an::PlateSolveResult> wide = [&]() {
        const an::ExecutionContext execution = makeExecution(engine, 8u);
        return an::solvePlate(execution, catalog, fixture.imageStars, solveRequest());
    }();
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();

    EXPECT_EQ(narrow->solution, wide->solution);
    ASSERT_EQ(narrow->matches.size(), wide->matches.size());
    for (std::size_t index = 0u; index < narrow->matches.size(); ++index)
    {
        EXPECT_EQ(narrow->matches[index].imageStarIndex, wide->matches[index].imageStarIndex);
        EXPECT_EQ(narrow->matches[index].catalogIndex, wide->matches[index].catalogIndex);
        EXPECT_DOUBLE_EQ(narrow->matches[index].residualPixels, wide->matches[index].residualPixels);
    }
}

TEST(PlateSolverTest, CancellationAndMemoryBudgetFailuresCrossThePublicBoundary)
{
    const SolveFixture fixture = makeFixture();
    an::EngineContext catalogEngine = makeEngine();
    const an::SkyCatalog catalog = fixtureCatalog(makeExecution(catalogEngine), fixture);

    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext stopped = makeExecution(catalogEngine, 4u, stop.get_token());
    EXPECT_EQ(an::solvePlate(stopped, catalog, fixture.imageStars, solveRequest()).code(), an::ErrorCode::Cancelled);

    an::EngineContext constrained = makeEngine(2u, 256u);
    const an::ExecutionContext littleMemory = makeExecution(constrained, 2u);
    EXPECT_EQ(an::solvePlate(littleMemory, catalog, fixture.imageStars, solveRequest()).code(),
              an::ErrorCode::MemoryBudgetExceeded);
}

TEST(PlateSolverTest, TooFewOrUnrelatedStarsReturnEmptySelection)
{
    const SolveFixture fixture = makeFixture();
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = fixtureCatalog(execution, fixture);

    const std::vector<an::PlateSolveStar> tooFew{{.position = {.x = 10.5, .y = 10.5}},
                                                 {.position = {.x = 20.5, .y = 20.5}}};
    EXPECT_EQ(an::solvePlate(execution, catalog, tooFew, solveRequest()).code(), an::ErrorCode::EmptySelection);

    std::vector<an::PlateSolveStar> unrelated;
    for (std::uint32_t index = 0u; index < 20u; ++index)
    {
        unrelated.push_back(
            {.position = {.x = 30.5 + 41.0 * index, .y = 40.5 + static_cast<double>((index * 97u) % 600u)}});
    }
    EXPECT_EQ(an::solvePlate(execution, catalog, unrelated, solveRequest()).code(), an::ErrorCode::EmptySelection);
}

/// A crowded field as a long exposure of the Milky Way records it: a rotated and mirrored frame of 2048 by 1536 pixels
/// at 1.1 arcseconds each, a catalog of thousands of stars down to magnitude 14 over the whole query cone, and a
/// detection list that ranks the stars by its own brightness - the catalog's band and the camera's differ by up to a
/// magnitude - misses some, finds a few that are not stars, and is cut at the brightest 512.
struct CrowdedField
{
    an::WcsSolution truth;
    std::vector<an::PlateSolveStar> detections;
    std::vector<an::CatalogEntry> catalogEntries;
};

constexpr std::uint32_t kCrowdedWidth = 2048u;
constexpr std::uint32_t kCrowdedHeight = 1536u;
constexpr double kCrowdedScaleArcsec = 1.1;
constexpr an::SkyCoordinate kCrowdedCenter{.rightAscensionDegrees = 161.1, .declinationDegrees = -59.9};

/// Marsaglia's xorshift, so the field is the same on every platform and standard library.
class FieldSampler
{
public:
    explicit FieldSampler(std::uint64_t seed) noexcept : m_state(seed) {}

    /// A value in (0, 1).
    [[nodiscard]] double uniform() noexcept
    {
        m_state ^= m_state << 13u;
        m_state ^= m_state >> 7u;
        m_state ^= m_state << 17u;
        return (static_cast<double>(m_state >> 11u) + 0.5) / 9007199254740992.0;
    }

    /// A standard normal value, by Box and Muller.
    [[nodiscard]] double normal() noexcept
    {
        const double radius = std::sqrt(-2.0 * std::log(uniform()));
        return radius * std::cos(2.0 * std::numbers::pi * uniform());
    }

private:
    std::uint64_t m_state;
};

[[nodiscard]] CrowdedField makeCrowdedField()
{
    // The orientation of a refractor's frame on a German mount, mirrored as many cameras write their rows.
    const double scaleDegrees = kCrowdedScaleArcsec / 3600.0;
    an::WcsSolutionRequest request;
    request.referencePixel = {.x = 0.5 * kCrowdedWidth, .y = 0.5 * kCrowdedHeight};
    request.referencePoint = kCrowdedCenter;
    request.linear = {.cd11 = -0.305 * scaleDegrees,
                      .cd12 = 0.9524 * scaleDegrees,
                      .cd21 = -0.9524 * scaleDegrees,
                      .cd22 = -0.305 * scaleDegrees};
    request.imageWidth = kCrowdedWidth;
    request.imageHeight = kCrowdedHeight;
    const an::Result<an::WcsSolution> truth = an::WcsSolution::create(request);
    EXPECT_TRUE(truth.hasValue()) << truth.status().message();

    CrowdedField field{.truth = truth.value()};
    FieldSampler sampler(0x9e3779b97f4a7c15ull);
    struct Ranked
    {
        double brightness = 0.0;
        an::PixelCoordinate position{};
    };
    std::vector<Ranked> recorded;
    // Uniform over a disc of 0.6 degrees, wider than the solver's query cone, with the counts of a field near the
    // galactic plane: ten times as many stars for every two and a half magnitudes.
    for (std::uint32_t ordinal = 0u; ordinal < 3200u; ++ordinal)
    {
        const double radius = 0.6 * std::sqrt(sampler.uniform());
        const double angle = 2.0 * std::numbers::pi * sampler.uniform();
        const an::Result<an::SkyCoordinate> direction = an::inverseGnomonic(
            an::GnomonicCoordinate{.xDegrees = radius * std::cos(angle), .yDegrees = radius * std::sin(angle)},
            kCrowdedCenter);
        EXPECT_TRUE(direction.hasValue()) << direction.status().message();
        const double magnitude = 14.0 + std::log10(sampler.uniform()) / 0.4;
        an::CatalogEntry entry;
        entry.identifier = "crowded-" + std::to_string(ordinal);
        entry.coordinate = direction.value();
        entry.magnitude = (std::max)(magnitude, 3.0);
        field.catalogEntries.push_back(entry);

        const double missed = sampler.uniform();
        const double colour = sampler.normal();
        const double jitterX = sampler.normal();
        const double jitterY = sampler.normal();
        const an::Result<an::PixelCoordinate> pixel = field.truth.worldToPixel(entry.coordinate);
        if (!pixel.hasValue() || missed < 0.15)
        {
            continue;
        }
        const an::PixelCoordinate detected{.x = pixel->x + 0.3 * jitterX, .y = pixel->y + 0.3 * jitterY};
        if (detected.x < 3.0 || detected.y < 3.0 || detected.x > kCrowdedWidth - 3.0 ||
            detected.y > kCrowdedHeight - 3.0)
        {
            continue;
        }
        recorded.push_back(Ranked{.brightness = *entry.magnitude + 0.6 * colour, .position = detected});
    }
    // Knots of nebulosity the detector takes for stars, as bright as the stars it ranks among.
    for (std::uint32_t ordinal = 0u; ordinal < 40u; ++ordinal)
    {
        recorded.push_back(Ranked{.brightness = 9.0 + 4.0 * sampler.uniform(),
                                  .position = {.x = 3.0 + (kCrowdedWidth - 6.0) * sampler.uniform(),
                                               .y = 3.0 + (kCrowdedHeight - 6.0) * sampler.uniform()}});
    }
    std::sort(recorded.begin(), recorded.end(),
              [](const Ranked& left, const Ranked& right) { return left.brightness < right.brightness; });
    for (std::size_t index = 0u; index < recorded.size() && index < 512u; ++index)
    {
        field.detections.push_back(an::PlateSolveStar{.position = recorded[index].position});
    }
    return field;
}

/// The request the product's internal solver makes: 256 image stars and 512 catalog stars, the catalog to magnitude
/// 14, a scale hint from a focal length and pixel size a little off, and a hint of the target's catalogued position.
[[nodiscard]] an::PlateSolveRequest crowdedRequest()
{
    an::PlateSolveRequest request;
    request.imageWidth = kCrowdedWidth;
    request.imageHeight = kCrowdedHeight;
    const an::Result<an::SkyCoordinate> hint =
        an::inverseGnomonic(an::GnomonicCoordinate{.xDegrees = -0.04, .yDegrees = 0.02}, kCrowdedCenter);
    EXPECT_TRUE(hint.hasValue());
    request.initialCenter = hint.value();
    request.approximateScaleArcsecPerPixel = kCrowdedScaleArcsec * 1.004;
    request.searchRadiusDegrees = 0.5;
    request.maximumImageStars = 256u;
    request.maximumCatalogStars = 512u;
    request.magnitudeLimit = 14.0;
    return request;
}

TEST(PlateSolverTest, ACrowdedFieldIsMatchedOverTheSameDiscOnBothSides)
{
    const CrowdedField field = makeCrowdedField();
    ASSERT_EQ(field.detections.size(), 512u);
    an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, field.catalogEntries,
                                               an::SkyCatalogRequest{.photometricBand = "G", .buildNameIndex = false});
    const an::PlateSolveRequest request = crowdedRequest();
    const an::Result<an::PlateSolvePlan> plan = an::planPlateSolve(request);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    const an::Result<an::PlateSolveResult> solved = an::solvePlate(execution, catalog, field.detections, request);
    ASSERT_TRUE(solved.hasValue()) << solved.status().message();
    EXPECT_TRUE(solved->diagnostics.usedExactHint);
    EXPECT_GE(solved->diagnostics.matchedStars, 60u);
    const an::Result<an::SkyCoordinate> center = solved->solution.fieldCenter();
    ASSERT_TRUE(center.hasValue()) << center.status().message();
    EXPECT_LT(an::angularSeparationDegrees(*center, kCrowdedCenter) * 3600.0, 0.5);
    EXPECT_NEAR(solved->solution.meanPixelScaleArcsecPerPixel(), kCrowdedScaleArcsec, 1.0e-3);
    EXPECT_TRUE(solved->solution.mirrored());

    // Both sides keep the brightest stars inside the largest circle about their centre inside the frame, in brightness
    // order, so the two lists rank the same stars first whatever the field's rotation.
    const double disc = 0.5 * (std::min)(kCrowdedWidth, kCrowdedHeight);
    const an::Result<an::PlateSolveInput> input = an::preparePlateSolveInput(execution, field.detections, *plan);
    ASSERT_TRUE(input.hasValue()) << input.status().message();
    ASSERT_EQ(input->stars.size(), 256u);
    std::size_t next = 0u;
    for (const an::PlateSolveStar& star : input->stars)
    {
        EXPECT_LE(std::hypot(star.position.x - 0.5 * kCrowdedWidth, star.position.y - 0.5 * kCrowdedHeight), disc);
        while (next < field.detections.size() && (field.detections[next].position.x != star.position.x ||
                                                  field.detections[next].position.y != star.position.y))
        {
            ++next;
        }
        ASSERT_LT(next, field.detections.size()) << "the selection keeps the supplied order";
    }
    const an::Result<an::PlateSolveCatalogSearchResult> search =
        an::searchPlateSolveCatalog(execution, catalog, *input, *plan, *request.initialCenter);
    ASSERT_TRUE(search.hasValue()) << search.status().message();
    EXPECT_GE(search->stars.size(), 60u);
    EXPECT_LE(search->stars.size(), 512u);
    double previous = -std::numeric_limits<double>::infinity();
    for (const an::PlateSolveCatalogStar& star : search->stars)
    {
        EXPECT_LE(std::hypot(star.matchingX, star.matchingY), disc);
        const double magnitude = catalog.entry(star.catalogIndex)->magnitude.value_or(99.0);
        EXPECT_GE(magnitude, previous);
        previous = magnitude;
    }
}

} // namespace

TEST(PlateSolverTest, HintedAngularRingsReachTheOppositeHemisphere)
{
    const an::SkyCoordinate center{.rightAscensionDegrees = 120.0, .declinationDegrees = 0.0};
    const auto fixture = makeFixture(false, center);
    auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto catalog = fixtureCatalog(execution, fixture);
    auto request = solveRequest();
    request.initialCenter = an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
    request.searchRadiusDegrees = 120.0;
    request.nearSearchRings = 1u;
    request.nearSearchSamplesPerRing = 4u;
    const auto solved = an::solvePlate(execution, catalog, fixture.imageStars, request);
    ASSERT_TRUE(solved.hasValue()) << solved.status().message();
    EXPECT_TRUE(solved->diagnostics.usedNearSearch);
    const auto recovered = solved->solution.fieldCenter();
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_LT(an::angularSeparationDegrees(*recovered, center) * 3600.0, 1.0e-2);
}
