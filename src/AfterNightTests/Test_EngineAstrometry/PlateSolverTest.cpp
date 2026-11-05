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

    std::vector<an::PlateSolveStar> stars;
    for (std::uint32_t row = 0u; row < 10u; ++row)
    {
        for (std::uint32_t column = 0u; column < 10u; ++column)
        {
            stars.push_back({.position = {.x = 20.5 + 90.0 * column, .y = 20.5 + 70.0 * row}});
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
