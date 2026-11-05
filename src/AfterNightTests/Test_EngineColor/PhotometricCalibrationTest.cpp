// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the zero-point fit and the photometric cross-match.

#include <AfterNightEngine/Algorithms/Photometry/PhotometricCalibration.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "ColorTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

using color_test::buildOrFail;
using color_test::makeEngine;
using color_test::makeExecution;
using color_test::makeSolution;

/// The zero-point recipe, written out here as an independent oracle.
[[nodiscard]] std::optional<double> oracleZeroPoint(const std::vector<an::PhotometricMatch>& matches)
{
    std::vector<double> values;
    for (const an::PhotometricMatch& match : matches)
    {
        if (std::isfinite(match.instrumentalFlux) && match.instrumentalFlux > 0.0)
        {
            values.push_back(match.catalogMagnitude + 2.5 * std::log10(match.instrumentalFlux));
        }
    }
    if (values.empty())
    {
        return std::nullopt;
    }
    const auto median = [](std::vector<double> copy) {
        std::sort(copy.begin(), copy.end());
        const std::size_t middle = copy.size() / 2u;
        return copy.size() % 2u == 0u ? 0.5 * (copy[middle - 1u] + copy[middle]) : copy[middle];
    };
    for (int iteration = 0; iteration < 3 && values.size() > 2u; ++iteration)
    {
        const double center = median(values);
        std::vector<double> deviations;
        for (const double value : values)
        {
            deviations.push_back(std::abs(value - center));
        }
        const double sigma = (std::max)(1.4826 * median(deviations), 0.02);
        const double threshold = (std::max)(3.0 * sigma, 0.05);
        std::vector<double> next;
        for (const double value : values)
        {
            if (std::abs(value - center) <= threshold)
            {
                next.push_back(value);
            }
        }
        if (next.size() == values.size() || next.empty())
        {
            break;
        }
        values = next;
    }
    double sum = 0.0;
    for (const double value : values)
    {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

[[nodiscard]] an::DetectedStar starAt(double x, double y, double amplitude, double sigma)
{
    an::DetectedStar star{};
    star.positionX = x;
    star.positionY = y;
    star.psf.amplitude = amplitude;
    star.psf.scaleX = sigma;
    star.psf.scaleY = sigma;
    star.psf.background = 0.05;
    star.instrumentalMagnitude = -2.5 * std::log10(amplitude * 2.0 * std::numbers::pi * sigma * sigma);
    return star;
}

TEST(PhotometricCalibrationTest, ZeroPointFitMatchesTheIndependentOracleAndReportsItsCounts)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::PhotometricMatch> matches;
    for (int index = 0; index < 24; ++index)
    {
        const double magnitude = 9.0 + 0.2 * index;
        const double flux = std::pow(10.0, -0.4 * (magnitude - 21.5)) * (1.0 + 0.004 * ((index % 5) - 2));
        matches.push_back({.instrumentalFlux = flux, .catalogMagnitude = magnitude});
    }
    matches.push_back({.instrumentalFlux = 0.0, .catalogMagnitude = 12.0});
    matches.push_back({.instrumentalFlux = -3.0, .catalogMagnitude = 12.0});

    const an::Result<an::ZeroPointFitResult> fit = an::fitPhotometricZeroPoint(execution, matches);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    const std::optional<double> oracle = oracleZeroPoint(matches);
    ASSERT_TRUE(oracle.has_value());
    EXPECT_NEAR(fit->zeroPoint, *oracle, 1.0e-12);
    EXPECT_NEAR(fit->zeroPoint, 21.5, 0.01);
    EXPECT_EQ(fit->suppliedCount, 26u);
    EXPECT_EQ(fit->usableCount, 24u);
    EXPECT_EQ(fit->retainedCount, 24u);
    EXPECT_GE(fit->clipIterations, 1u);
    EXPECT_GT(fit->rmsResidual, 0.0);
    EXPECT_NEAR(fit->zeroPointStandardUncertainty, fit->rmsResidual / std::sqrt(24.0), 1.0e-15);
    EXPECT_GT(fit->workingBytes, 0u);
}

TEST(PhotometricCalibrationTest, ZeroPointFitRejectsAnOutlierAndSaysHowMany)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::PhotometricMatch> matches;
    for (int index = 0; index < 12; ++index)
    {
        const double magnitude = 10.0 + 0.25 * index;
        matches.push_back(
            {.instrumentalFlux = std::pow(10.0, -0.4 * (magnitude - 20.0)), .catalogMagnitude = magnitude});
    }
    matches.push_back({.instrumentalFlux = std::pow(10.0, -0.4 * (12.0 - 20.0)) * 10.0, .catalogMagnitude = 12.0});

    const an::Result<an::ZeroPointFitResult> fit = an::fitPhotometricZeroPoint(execution, matches);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_EQ(fit->usableCount, 13u);
    EXPECT_EQ(fit->retainedCount, 12u);
    EXPECT_NEAR(fit->zeroPoint, 20.0, 1.0e-9);
    EXPECT_NEAR(fit->rmsResidual, 0.0, 1.0e-9);
    EXPECT_NEAR(fit->zeroPointStandardUncertainty, 0.0, 1.0e-9);
    EXPECT_NEAR(fit->finalRobustSigma, an::kDefaultMinimumZeroPointRobustSigma, 1.0e-12);
}

TEST(PhotometricCalibrationTest, ZeroPointFitRefusesWhatItCannotFit)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const std::vector<an::PhotometricMatch> nothingUsable{{.instrumentalFlux = 0.0, .catalogMagnitude = 10.0},
                                                          {.instrumentalFlux = -1.0, .catalogMagnitude = 11.0}};
    const an::Result<an::ZeroPointFitResult> empty = an::fitPhotometricZeroPoint(execution, nothingUsable);
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::EmptySelection);

    const std::vector<an::PhotometricMatch> malformed{
        {.instrumentalFlux = 100.0, .catalogMagnitude = std::numeric_limits<double>::quiet_NaN()}};
    const an::Result<an::ZeroPointFitResult> data = an::fitPhotometricZeroPoint(execution, malformed);
    ASSERT_FALSE(data.hasValue());
    EXPECT_EQ(data.status().code(), an::ErrorCode::InvalidData);

    const std::vector<an::PhotometricMatch> one{{.instrumentalFlux = 100.0, .catalogMagnitude = 10.0}};
    an::ZeroPointFitRequest badRequest;
    badRequest.clipSigmas = 0.0;
    const an::Result<an::ZeroPointFitResult> argument = an::fitPhotometricZeroPoint(execution, one, badRequest);
    ASSERT_FALSE(argument.hasValue());
    EXPECT_EQ(argument.status().code(), an::ErrorCode::InvalidArgument);
    badRequest = {};
    badRequest.maximumClipIterations = 17u;
    const an::Result<an::ZeroPointFitResult> rounds = an::fitPhotometricZeroPoint(execution, one, badRequest);
    ASSERT_FALSE(rounds.hasValue());
    EXPECT_EQ(rounds.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<an::ZeroPointFitResult> single = an::fitPhotometricZeroPoint(execution, one);
    ASSERT_TRUE(single.hasValue()) << single.status().message();
    EXPECT_NEAR(single->zeroPoint, 15.0, 1.0e-12);
    EXPECT_EQ(single->retainedCount, 1u);
    EXPECT_EQ(single->zeroPointStandardUncertainty, 0.0);

    const an::Result<an::ZeroPointFitResult> uninitialized = an::fitPhotometricZeroPoint(an::ExecutionContext{}, one);
    ASSERT_FALSE(uninitialized.hasValue());
    EXPECT_EQ(uninitialized.status().code(), an::ErrorCode::NotInitialized);

    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext cancelled = makeExecution(engine, 2u, stop.get_token());
    const an::Result<an::ZeroPointFitResult> stopped = an::fitPhotometricZeroPoint(cancelled, one);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
}

TEST(PhotometricCalibrationTest, FluxEstimatorsReadTheStarTheyName)
{
    const an::DetectedStar star = starAt(10.0, 12.0, 0.4, 1.5);
    const std::optional<double> volume = an::starInstrumentalFlux(star, an::StarFluxEstimator::PsfVolume);
    ASSERT_TRUE(volume.has_value());
    EXPECT_NEAR(*volume, 0.4 * 2.0 * std::numbers::pi * 1.5 * 1.5, 1.0e-12);
    const std::optional<double> box = an::starInstrumentalFlux(star, an::StarFluxEstimator::BoxSum);
    ASSERT_TRUE(box.has_value());
    EXPECT_NEAR(*box, *volume, 1.0e-9);

    an::DetectedStar narrow = star;
    narrow.psf.scaleX = 0.1;
    narrow.psf.scaleY = -0.2;
    const std::optional<double> floored = an::starInstrumentalFlux(narrow, an::StarFluxEstimator::PsfVolume);
    ASSERT_TRUE(floored.has_value());
    EXPECT_NEAR(*floored, 0.4 * 2.0 * std::numbers::pi * 0.5 * 0.5, 1.0e-12);

    an::DetectedStar magnitudeless = star;
    magnitudeless.instrumentalMagnitude.reset();
    EXPECT_FALSE(an::starInstrumentalFlux(magnitudeless, an::StarFluxEstimator::BoxSum).has_value());
    EXPECT_FALSE(an::starInstrumentalFlux(star, static_cast<an::StarFluxEstimator>(7u)).has_value());
    EXPECT_STREQ(an::starFluxEstimatorName(an::StarFluxEstimator::PsfVolume), "PsfVolume");
    EXPECT_STREQ(an::starFluxEstimatorName(static_cast<an::StarFluxEstimator>(7u)), "UnknownStarFluxEstimator");
    EXPECT_FALSE(an::isStarFluxEstimator(static_cast<an::StarFluxEstimator>(2u)));
}

TEST(PhotometricCalibrationTest, CrossMatchPlacesStarsAndPairsFluxWithMagnitude)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 640u;
    constexpr std::uint32_t kHeight = 480u;
    const an::WcsSolution solution = makeSolution(kWidth, kHeight);

    std::vector<an::DetectedStar> stars;
    std::vector<an::CatalogEntry> entries;
    for (std::uint32_t index = 0u; index < 20u; ++index)
    {
        const double x = 40.5 + 28.0 * index;
        const double y = 60.5 + 19.0 * index;
        stars.push_back(starAt(x, y, 0.1 + 0.02 * index, 1.4));
        const an::Result<an::SkyCoordinate> direction = solution.pixelToWorld({.x = x + 0.2, .y = y - 0.1});
        ASSERT_TRUE(direction.hasValue());
        an::CatalogEntry entry;
        entry.identifier = "star-" + std::to_string(index);
        entry.coordinate = *direction;
        entry.magnitude = index == 3u ? std::optional<double>{} : std::optional<double>{10.0 + 0.1 * index};
        entries.push_back(std::move(entry));
    }
    an::CatalogEntry decoy;
    decoy.identifier = "decoy";
    decoy.coordinate = {.rightAscensionDegrees = 120.0, .declinationDegrees = -5.0};
    decoy.magnitude = 5.0;
    entries.push_back(std::move(decoy));
    stars.push_back(starAt(std::numeric_limits<double>::quiet_NaN(), 10.0, 0.2, 1.4));
    const an::SkyCatalog catalog = buildOrFail(execution, entries);

    const an::Result<an::PhotometricCrossMatchResult> matched =
        an::crossMatchDetectedStars(execution, solution, stars, catalog);
    ASSERT_TRUE(matched.hasValue()) << matched.status().message();
    EXPECT_EQ(matched->matches.matches.size(), 20u);
    EXPECT_EQ(matched->unplacedStarCount, 1u);
    EXPECT_EQ(matched->magnitudelessMatchCount, 1u);
    EXPECT_EQ(matched->fluxlessMatchCount, 0u);
    ASSERT_EQ(matched->photometry.size(), 19u);
    ASSERT_EQ(matched->photometryMatchIndices.size(), 19u);
    for (std::size_t index = 0u; index < matched->photometry.size(); ++index)
    {
        const an::CatalogMatch& match = matched->matches.matches[matched->photometryMatchIndices[index]];
        EXPECT_NE(match.entryIndex, 3u);
        const std::optional<double> flux =
            an::starInstrumentalFlux(stars[match.sourceIndex], an::StarFluxEstimator::PsfVolume);
        ASSERT_TRUE(flux.has_value());
        EXPECT_DOUBLE_EQ(matched->photometry[index].instrumentalFlux, *flux);
        EXPECT_DOUBLE_EQ(matched->photometry[index].catalogMagnitude, *catalog.entry(match.entryIndex)->magnitude);
        EXPECT_LT(match.separationArcseconds, 1.0);
    }

    const an::Result<an::ZeroPointFitResult> fit = an::fitPhotometricZeroPoint(execution, matched->photometry);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    EXPECT_EQ(fit->usableCount, 19u);

    const an::Result<an::PhotometricCrossMatchResult> none =
        an::crossMatchDetectedStars(execution, solution, std::span<const an::DetectedStar>{}, catalog);
    ASSERT_FALSE(none.hasValue());
    EXPECT_EQ(none.status().code(), an::ErrorCode::EmptySelection);
    an::PhotometricCrossMatchRequest bad;
    bad.fluxEstimator = static_cast<an::StarFluxEstimator>(9u);
    const an::Result<an::PhotometricCrossMatchResult> estimator =
        an::crossMatchDetectedStars(execution, solution, stars, catalog, bad);
    ASSERT_FALSE(estimator.hasValue());
    EXPECT_EQ(estimator.status().code(), an::ErrorCode::InvalidArgument);
    const an::Result<an::PhotometricCrossMatchResult> emptyCatalog =
        an::crossMatchDetectedStars(execution, solution, stars, an::SkyCatalog{});
    ASSERT_FALSE(emptyCatalog.hasValue());
    EXPECT_EQ(emptyCatalog.status().code(), an::ErrorCode::NotInitialized);
}

} // namespace
