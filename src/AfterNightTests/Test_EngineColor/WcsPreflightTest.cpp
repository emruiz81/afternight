// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the WCS preflight: source selection, the catalog re-match and the verdict.

#include <AfterNightEngine/Algorithms/Spectrophotometry/WcsPreflight.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "ColorTestSupport.h"
#include "SpectrumCatalogTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace
{

using color_test::makeEngine;
using color_test::makeExecution;
using color_test::makeSolution;
using spectrum_test::FixtureChunk;
using spectrum_test::FixtureSpectrumStar;
using spectrum_test::identifierAt;

constexpr std::uint32_t kWidth = 1024u;
constexpr std::uint32_t kHeight = 768u;

/// Forty pixel positions spread over all four quadrants.
[[nodiscard]] std::vector<an::PixelCoordinate> distributedPixels()
{
    std::vector<an::PixelCoordinate> pixels;
    for (std::uint32_t row = 0u; row < 5u; ++row)
    {
        for (std::uint32_t column = 0u; column < 8u; ++column)
        {
            pixels.push_back({.x = 70.5 + 125.0 * column + 3.0 * row, .y = 60.5 + 160.0 * row + 2.0 * column});
        }
    }
    return pixels;
}

/// A catalog whose stars sit at stated pixels of a solution, at the given epoch positions.
[[nodiscard]] an::SpectrumCatalog catalogAtPixels(const an::ExecutionContext& execution,
                                                  const an::WcsSolution& solution,
                                                  const std::vector<an::PixelCoordinate>& pixels,
                                                  const std::vector<float>& magnitudes = {},
                                                  const std::optional<an::PixelCoordinate>& epochShift = {})
{
    FixtureChunk chunk;
    for (std::size_t index = 0u; index < pixels.size(); ++index)
    {
        an::PixelCoordinate placed = pixels[index];
        if (epochShift.has_value())
        {
            placed.x += epochShift->x;
            placed.y += epochShift->y;
        }
        const an::Result<an::SkyCoordinate> direction = solution.pixelToWorld(placed);
        EXPECT_TRUE(direction.hasValue()) << direction.status().message();
        FixtureSpectrumStar star =
            spectrum_test::starAt(direction->rightAscensionDegrees, direction->declinationDegrees, index,
                                  magnitudes.empty() ? 12.0f : magnitudes[index]);
        chunk.stars.push_back(star);
    }
    return spectrum_test::buildOrFail(execution, {chunk});
}

[[nodiscard]] std::vector<an::PreflightSource> sourcesAt(const std::vector<an::PixelCoordinate>& pixels, double dx,
                                                         double dy)
{
    std::vector<an::PreflightSource> sources;
    for (const an::PixelCoordinate& pixel : pixels)
    {
        sources.push_back(
            {.position = {.x = pixel.x + dx, .y = pixel.y + dy}, .fullWidthPixels = 2.5, .signalToNoise = 40.0});
    }
    return sources;
}

TEST(WcsPreflightTest, RematchBuildsAnAcceptableReportWithResidualsAndIdentity)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::WcsSolution solution =
        makeSolution(kWidth, kHeight, 1.0, {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}, 0.0);
    const std::vector<an::PixelCoordinate> pixels = distributedPixels();
    const an::SpectrumCatalog catalog = catalogAtPixels(execution, solution, pixels);
    const std::vector<an::PreflightSource> sources = sourcesAt(pixels, 0.35, -0.25);

    an::WcsPreflightRequest request;
    request.matchRadiusArcseconds = 3.0;
    request.astrometricCatalogFamilyId = "gaia_dr3";
    request.astrometricCatalogManifestId = "manifest-test-001";
    request.wcsSolutionId = "synthetic-wcs";
    const an::Result<an::WcsPreflightResult> result =
        an::runWcsPreflight(execution, solution, sources, catalog, request);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->matches.size(), pixels.size());
    EXPECT_EQ(result->catalogCandidates, pixels.size());
    EXPECT_EQ(result->projectedInsideFrame, pixels.size());
    EXPECT_GE(result->candidatePairs, pixels.size());
    EXPECT_EQ(result->report.source, an::WcsQualitySource::PreflightRematch);
    EXPECT_EQ(result->report.distortionModel, an::WcsDistortionModel::Tangent);
    EXPECT_TRUE(result->report.centerProjectionValid);
    EXPECT_TRUE(result->report.cornerProjectionValid);
    EXPECT_TRUE(result->report.inverseProjectionSupported);
    EXPECT_TRUE(result->report.staleExternalWcs);
    EXPECT_EQ(result->report.matchedStarCount, pixels.size());
    EXPECT_EQ(result->report.skyQuadrantsCovered, 4u);
    EXPECT_EQ(result->report.astrometricCatalogManifestId, "manifest-test-001");
    EXPECT_EQ(result->report.wcsSolutionId, "synthetic-wcs");
    ASSERT_TRUE(result->report.medianResidualPixels.has_value());
    EXPECT_NEAR(*result->report.medianResidualPixels, std::hypot(0.35, 0.25), 1.0e-6);
    ASSERT_TRUE(result->report.rmsResidualArcseconds.has_value());
    EXPECT_LT(*result->report.rmsResidualArcseconds, 0.5);
    EXPECT_EQ(an::checkWcsQuality(result->report), an::WcsQualityVerdict::Acceptable);
    EXPECT_EQ(result->matches.front().sourceIndex, 0u);
    EXPECT_EQ(result->matches.front().sourceIdentifier,
              catalog.record(result->matches.front().recordIndex)->sourceIdentifier);
    EXPECT_NEAR(result->matches.front().fullWidthPixels, 2.5, 1.0e-12);
    EXPECT_TRUE(std::is_sorted(result->matches.begin(), result->matches.end(),
                               [](const an::PreflightMatch& left, const an::PreflightMatch& right) {
                                   return left.sourceIndex < right.sourceIndex;
                               }));
}

TEST(WcsPreflightTest, AutomaticMagnitudeLimitStopsWhenTheTargetDensityIsReached)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::WcsSolution solution =
        makeSolution(kWidth, kHeight, 1.0, {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}, 0.0);
    const std::vector<an::PixelCoordinate> pixels = distributedPixels();
    std::vector<float> magnitudes(pixels.size(), 16.0f);
    std::fill(magnitudes.begin(), magnitudes.begin() + 20, 12.5f);
    const an::SpectrumCatalog catalog = catalogAtPixels(execution, solution, pixels, magnitudes);
    const std::vector<an::PreflightSource> sources = sourcesAt(pixels, 0.1, -0.1);

    an::WcsPreflightRequest request;
    request.matchRadiusArcseconds = 3.0;
    request.autoMagnitude.enabled = true;
    request.autoMagnitude.minimumLimit = 10.0;
    request.autoMagnitude.step = 0.5;
    request.autoMagnitude.targetSourceCount = 20u;
    request.magnitudeLimit = 17.6;
    const an::Result<an::WcsPreflightResult> result =
        an::runWcsPreflight(execution, solution, sources, catalog, request);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_NEAR(result->selectedMagnitudeLimit, 12.5, 1.0e-12);
    EXPECT_EQ(result->report.matchedStarCount, 20u);

    request.autoMagnitude.targetSourceCount = 30u;
    const an::Result<an::WcsPreflightResult> ceiling =
        an::runWcsPreflight(execution, solution, sources, catalog, request);
    ASSERT_TRUE(ceiling.hasValue());
    EXPECT_NEAR(ceiling->selectedMagnitudeLimit, 16.0, 1.0e-12);
    EXPECT_EQ(ceiling->report.matchedStarCount, pixels.size());
}

TEST(WcsPreflightTest, ProperMotionIsAppliedBeforeProjection)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::WcsSolution solution =
        makeSolution(kWidth, kHeight, 1.0, {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}, 0.0);
    const std::vector<an::PixelCoordinate> pixels = distributedPixels();
    const std::vector<an::PreflightSource> sources = sourcesAt(pixels, 0.1, 0.0);

    FixtureChunk chunk;
    for (std::size_t index = 0u; index < pixels.size(); ++index)
    {
        const an::Result<an::SkyCoordinate> now = solution.pixelToWorld(pixels[index]);
        ASSERT_TRUE(now.hasValue());
        const double cosine = std::cos(now->declinationDegrees * std::numbers::pi / 180.0);
        const double motionDegrees = 400.0 * 10.0 / (3600.0 * 1000.0);
        FixtureSpectrumStar star =
            spectrum_test::starAt(now->rightAscensionDegrees - motionDegrees / cosine, now->declinationDegrees, index);
        star.properMotionRightAscension = 400.0f;
        star.properMotionDeclination = 0.0f;
        star.referenceEpochYear = 2016.0f;
        chunk.stars.push_back(star);
    }
    const an::SpectrumCatalog catalog = spectrum_test::buildOrFail(execution, {chunk});

    an::WcsPreflightRequest request;
    request.matchRadiusArcseconds = 1.0;
    request.residualThresholdPixels = 1.0;
    request.residualThresholdArcseconds = 1.0;
    request.observationEpochYear = 2026.0;
    request.observationEpoch = "2026.000000";
    request.properMotionModelVersion = "gaia_epoch_correction_v1";
    const an::Result<an::WcsPreflightResult> corrected =
        an::runWcsPreflight(execution, solution, sources, catalog, request);
    ASSERT_TRUE(corrected.hasValue()) << corrected.status().message();
    EXPECT_EQ(corrected->matches.size(), pixels.size());
    ASSERT_TRUE(corrected->report.rmsResidualPixels.has_value());
    EXPECT_LT(*corrected->report.rmsResidualPixels, 0.2);
    EXPECT_EQ(corrected->report.observationEpoch, "2026.000000");
    EXPECT_EQ(corrected->report.properMotionModelVersion, "gaia_epoch_correction_v1");

    request.observationEpochYear = 2016.0;
    const an::Result<an::WcsPreflightResult> uncorrected =
        an::runWcsPreflight(execution, solution, sources, catalog, request);
    ASSERT_TRUE(uncorrected.hasValue());
    EXPECT_EQ(uncorrected->matches.size(), 0u);
    EXPECT_EQ(an::checkWcsQuality(uncorrected->report), an::WcsQualityVerdict::TooFewMatchedStars);
}

TEST(WcsPreflightTest, VerdictsNameTheFirstFailingRule)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::WcsSolution solution =
        makeSolution(kWidth, kHeight, 1.0, {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}, 0.0);
    const std::vector<an::PixelCoordinate> pixels = distributedPixels();
    const an::SpectrumCatalog catalog = catalogAtPixels(execution, solution, pixels);

    const std::vector<an::PreflightSource> all = sourcesAt(pixels, 0.1, 0.1);
    const std::vector<an::PreflightSource> few(all.begin(), all.begin() + 10);
    const an::Result<an::WcsPreflightResult> tooFew = an::runWcsPreflight(execution, solution, few, catalog);
    ASSERT_TRUE(tooFew.hasValue()) << tooFew.status().message();
    EXPECT_EQ(tooFew->report.matchedStarCount, 10u);
    EXPECT_EQ(an::checkWcsQuality(tooFew->report), an::WcsQualityVerdict::TooFewMatchedStars);

    an::WcsPreflightRequest loose;
    loose.matchRadiusArcseconds = 8.0;
    const an::Result<an::WcsPreflightResult> large =
        an::runWcsPreflight(execution, solution, sourcesAt(pixels, 4.0, 0.0), catalog, loose);
    ASSERT_TRUE(large.hasValue()) << large.status().message();
    EXPECT_EQ(large->report.matchedStarCount, pixels.size());
    EXPECT_EQ(an::checkWcsQuality(large->report), an::WcsQualityVerdict::RmsResidualExceeded);

    an::WcsQualityReport report = large->report;
    report.rmsResidualPixels = 1.0;
    report.rmsResidualArcseconds = 1.0;
    report.maximumResidualPixels = 7.0;
    report.maximumResidualArcseconds = 7.0;
    EXPECT_EQ(an::checkWcsQuality(report), an::WcsQualityVerdict::MaximumResidualExceeded);
    report.maximumResidualPixels.reset();
    EXPECT_EQ(an::checkWcsQuality(report), an::WcsQualityVerdict::IncompleteResiduals);
    report.skyQuadrantsCovered = 2u;
    EXPECT_EQ(an::checkWcsQuality(report), an::WcsQualityVerdict::PoorSkyCoverage);
    report.matchedStarCount = 5u;
    EXPECT_EQ(an::checkWcsQuality(report), an::WcsQualityVerdict::TooFewMatchedStars);
    report.source = an::WcsQualitySource::PersistedSolverResiduals;
    EXPECT_EQ(an::checkWcsQuality(report), an::WcsQualityVerdict::StaleWithoutRematch);
    report.cornerProjectionValid = false;
    EXPECT_EQ(an::checkWcsQuality(report), an::WcsQualityVerdict::IncompleteProjection);
    report.distortionModel = an::WcsDistortionModel::Unsupported;
    EXPECT_EQ(an::checkWcsQuality(report), an::WcsQualityVerdict::UnsupportedDistortion);
    report.source = an::WcsQualitySource::None;
    EXPECT_EQ(an::checkWcsQuality(report), an::WcsQualityVerdict::NoProvenance);
    EXPECT_STREQ(an::wcsQualityVerdictName(an::WcsQualityVerdict::PoorSkyCoverage), "PoorSkyCoverage");
    EXPECT_FALSE(an::isWcsQualityVerdict(static_cast<an::WcsQualityVerdict>(10u)));
    EXPECT_FALSE(an::isWcsQualitySource(static_cast<an::WcsQualitySource>(4u)));
    EXPECT_FALSE(an::isWcsDistortionModel(static_cast<an::WcsDistortionModel>(4u)));

    an::WcsPreflightRequest bad;
    bad.matchRadiusArcseconds = 0.0;
    EXPECT_EQ(an::runWcsPreflight(execution, solution, few, catalog, bad).status().code(),
              an::ErrorCode::InvalidArgument);
    bad = {};
    bad.autoMagnitude.enabled = true;
    bad.autoMagnitude.minimumLimit = 20.0;
    EXPECT_EQ(an::runWcsPreflight(execution, solution, few, catalog, bad).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::runWcsPreflight(execution, solution, few, an::SpectrumCatalog{}).status().code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::runWcsPreflight(an::ExecutionContext{}, solution, few, catalog).status().code(),
              an::ErrorCode::NotInitialized);

    const an::Result<an::WcsPreflightResult> nothing =
        an::runWcsPreflight(execution, solution, std::vector<an::PreflightSource>{}, catalog);
    ASSERT_TRUE(nothing.hasValue());
    EXPECT_EQ(nothing->report.matchedStarCount, 0u);
    EXPECT_FALSE(nothing->report.rmsResidualPixels.has_value());
}

TEST(WcsPreflightTest, SourceSelectionFiltersSortsAndCaps)
{
    std::vector<an::DetectedStar> stars(6u);
    for (std::size_t index = 0u; index < stars.size(); ++index)
    {
        stars[index].positionX = 10.0 * static_cast<double>(index) + 0.5;
        stars[index].positionY = 20.5;
        stars[index].signalToNoise = 10.0 + 5.0 * static_cast<double>(index);
        stars[index].metrics.meanFullWidth = 2.0 + 0.1 * static_cast<double>(index);
    }
    stars[1].psf.saturated = true;
    stars[2].signalToNoise = 2.0;
    stars[3].positionX = std::numeric_limits<double>::quiet_NaN();
    stars[4].metrics.meanFullWidth = 0.0;
    stars[4].psf.fullWidthX = 3.0;
    stars[4].psf.fullWidthY = 4.0;

    const an::Result<std::vector<an::PreflightSource>> sources = an::makePreflightSources(stars);
    ASSERT_TRUE(sources.hasValue()) << sources.status().message();
    ASSERT_EQ(sources->size(), 3u);
    EXPECT_NEAR((*sources)[0].signalToNoise, 35.0, 1.0e-12);
    EXPECT_NEAR((*sources)[0].position.x, 50.5, 1.0e-12);
    EXPECT_NEAR((*sources)[1].signalToNoise, 30.0, 1.0e-12);
    EXPECT_NEAR((*sources)[1].fullWidthPixels, 3.5, 1.0e-12);
    EXPECT_NEAR((*sources)[2].signalToNoise, 10.0, 1.0e-12);

    an::PreflightSourceSelection selection;
    selection.rejectSaturated = false;
    selection.minimumSignalToNoise = 0.0;
    selection.sortBySignalToNoise = false;
    selection.maximumSources = 2u;
    const an::Result<std::vector<an::PreflightSource>> unsorted = an::makePreflightSources(stars, selection);
    ASSERT_TRUE(unsorted.hasValue());
    ASSERT_EQ(unsorted->size(), 2u);
    EXPECT_NEAR((*unsorted)[0].position.x, 0.5, 1.0e-12);
    EXPECT_NEAR((*unsorted)[1].position.x, 10.5, 1.0e-12);

    selection.maximumSources = 0u;
    EXPECT_EQ(an::makePreflightSources(stars, selection).status().code(), an::ErrorCode::InvalidArgument);
    selection.maximumSources.reset();
    selection.minimumSignalToNoise = -1.0;
    EXPECT_EQ(an::makePreflightSources(stars, selection).status().code(), an::ErrorCode::InvalidArgument);
}

} // namespace
