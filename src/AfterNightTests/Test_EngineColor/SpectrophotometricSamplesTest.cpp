// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the spectrophotometric sample builder over a planted colour field.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricSamples.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "ColorTestSupport.h"
#include "SpectrumCatalogTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <stop_token>
#include <vector>

namespace
{

using color_test::ColorField;
using color_test::ColorFieldRequest;
using color_test::frameOf;
using color_test::makeColorField;
using color_test::makeEngine;
using color_test::makeExecution;
using color_test::makeSolution;
using color_test::planarLayout;
using color_test::rgbDescriptor;
using spectrum_test::FixtureChunk;

struct SampleFixture
{
    ColorField field{};
    an::WcsSolution solution;
    an::SpectrumCatalog catalog{};
    an::WcsPreflightResult preflight{};
    std::vector<an::CatalogColorReference> colors{};
};

/// A planted field, a catalog holding every star but the last, and a hand-built acceptable preflight.
[[nodiscard]] SampleFixture makeFixture(const an::ExecutionContext& execution)
{
    ColorFieldRequest request;
    request.width = 320u;
    request.height = 320u;
    request.cell = 64u;
    request.noise = 0.0;
    request.redFactor = 1.3;
    request.blueFactor = 0.8;
    SampleFixture fixture{.field = makeColorField(request), .solution = makeSolution(320u, 320u)};
    FixtureChunk chunk;
    for (std::size_t index = 0u; index + 1u < fixture.field.stars.size(); ++index)
    {
        const color_test::PlantedColorStar& star = fixture.field.stars[index];
        const an::Result<an::SkyCoordinate> direction = fixture.solution.pixelToWorld({.x = star.x, .y = star.y});
        EXPECT_TRUE(direction.hasValue());
        chunk.stars.push_back(
            spectrum_test::starAt(direction->rightAscensionDegrees, direction->declinationDegrees, index));
    }
    fixture.catalog = spectrum_test::buildOrFail(execution, {chunk});

    fixture.preflight.report.source = an::WcsQualitySource::PreflightRematch;
    fixture.preflight.report.distortionModel = an::WcsDistortionModel::Tangent;
    // A hand-built report: the star count is what a real preflight over a wider field would report,
    // so the gate exercises its acceptance path rather than the small planted field's count.
    fixture.preflight.report.matchedStarCount = 40u;
    fixture.preflight.report.skyQuadrantsCovered = 4u;
    fixture.preflight.report.medianResidualPixels = 0.1;
    fixture.preflight.report.rmsResidualPixels = 0.1;
    fixture.preflight.report.maximumResidualPixels = 0.2;
    fixture.preflight.report.medianResidualArcseconds = 0.24;
    fixture.preflight.report.rmsResidualArcseconds = 0.24;
    fixture.preflight.report.maximumResidualArcseconds = 0.48;
    for (std::size_t index = 0u; index < fixture.field.stars.size(); ++index)
    {
        const color_test::PlantedColorStar& star = fixture.field.stars[index];
        an::PreflightMatch match;
        match.sourceIndex = static_cast<std::uint32_t>(index);
        match.recordIndex = static_cast<std::uint32_t>(index);
        match.sourceIdentifier = index + 1u < fixture.field.stars.size() ? chunk.stars[index].sourceIdentifier : 999u;
        match.detectedPosition = {.x = star.x, .y = star.y};
        match.projectedPosition = {.x = star.x + 0.05, .y = star.y - 0.05};
        match.residualPixels = std::hypot(0.05, 0.05);
        match.residualArcseconds = match.residualPixels * 2.4;
        match.fullWidthPixels = star.sigma * an::kGaussianFullWidthPerSigma;
        match.signalToNoise = 50.0;
        fixture.preflight.matches.push_back(match);
        const std::array<double, 3u> ratios = color_test::modelRatios(star.colorIndexBpMinusRp);
        fixture.colors.push_back(an::CatalogColorReference{.relativeToGreen = ratios, .available = true});
    }
    return fixture;
}

TEST(SpectrophotometricSamplesTest, BuildsAcceptedSamplesWithBothSidesOfTheColor)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext execution = makeExecution(engine, 6u);
    SampleFixture fixture = makeFixture(execution);
    ASSERT_GE(fixture.field.stars.size(), 9u);
    fixture.colors[1].available = false;
    fixture.colors[2].relativeToGreen[0] = -1.0;
    const an::ImageDescriptor descriptor = rgbDescriptor(320u, 320u);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(fixture.field.samples, layout, descriptor);

    an::SampleConstructionRequest request;
    request.aperture.radiusPolicy = an::ApertureRadiusPolicy::FullWidthScaledClamped;
    request.aperture.fullWidthRadiusScale = 2.0;
    request.aperture.maximumApertureRadiusPixels = 12.0;
    request.aperture.annulusPolicy = an::AnnulusRadiusPolicy::FixedPixels;
    request.aperture.annulusInnerRadiusPixels = 14.0;
    request.aperture.annulusOuterRadiusPixels = 22.0;
    const an::Result<an::SampleConstructionResult> built = an::buildSpectrophotometricSamples(
        execution, frame, fixture.preflight, fixture.catalog, fixture.colors, request);
    ASSERT_TRUE(built.hasValue()) << built.status().message();
    ASSERT_EQ(built->samples.size(), fixture.field.stars.size());
    EXPECT_EQ(built->acceptedCount + built->rejectedCount, built->samples.size());
    EXPECT_EQ(built->rejections[static_cast<std::size_t>(an::SampleRejectionReason::MissingCatalogSource)], 1u);
    EXPECT_EQ(built->rejections[static_cast<std::size_t>(an::SampleRejectionReason::CatalogColorUnavailable)], 2u);
    EXPECT_GE(built->acceptedCount, fixture.field.stars.size() - 3u);
    EXPECT_GT(built->workingBytes, 0u);

    const an::SpectrophotometricSample& missing = built->samples.back();
    EXPECT_FALSE(missing.accepted);
    EXPECT_EQ(missing.rejection, an::SampleRejectionReason::MissingCatalogSource);
    EXPECT_EQ(missing.sourceIdentifier, 999u);
    EXPECT_FALSE(built->samples[1].accepted);
    EXPECT_EQ(built->samples[1].rejection, an::SampleRejectionReason::CatalogColorUnavailable);
    EXPECT_EQ(built->samples[2].rejection, an::SampleRejectionReason::CatalogColorUnavailable);

    for (std::size_t index = 0u; index < built->samples.size(); ++index)
    {
        const an::SpectrophotometricSample& sample = built->samples[index];
        if (!sample.accepted)
        {
            continue;
        }
        const color_test::PlantedColorStar& star = fixture.field.stars[index];
        const std::array<double, 3u> ratios = color_test::modelRatios(star.colorIndexBpMinusRp);
        EXPECT_EQ(sample.rejection, an::SampleRejectionReason::None);
        EXPECT_EQ(sample.apertureRejection, an::ApertureRejectionReason::None);
        EXPECT_EQ(sample.sourceIdentifier, fixture.preflight.matches[index].sourceIdentifier);
        EXPECT_EQ(sample.recordIndex, index);
        EXPECT_NEAR(sample.imageRelativeToGreen[1], 1.0, 1.0e-12);
        EXPECT_NEAR(sample.imageRelativeToGreen[0], ratios[0] * 1.3, 0.02) << index;
        EXPECT_NEAR(sample.imageRelativeToGreen[2], ratios[2] * 0.8, 0.02) << index;
        EXPECT_NEAR(sample.catalogRelativeToGreen[0], ratios[0], 1.0e-12);
        EXPECT_NEAR(sample.catalogRelativeToGreen[2], ratios[2], 1.0e-12);
        EXPECT_NEAR(sample.aperture.channels[1].localBackground, 0.062, 1.0e-6);
        EXPECT_NEAR(sample.detectedPosition.x, star.x, 1.0e-12);
        EXPECT_NEAR(sample.residualArcseconds, std::hypot(0.05, 0.05) * 2.4, 1.0e-12);
    }

    const an::ExecutionContext single = makeExecution(engine, 1u);
    const an::Result<an::SampleConstructionResult> serial =
        an::buildSpectrophotometricSamples(single, frame, fixture.preflight, fixture.catalog, fixture.colors, request);
    ASSERT_TRUE(serial.hasValue());
    ASSERT_EQ(serial->samples.size(), built->samples.size());
    for (std::size_t index = 0u; index < serial->samples.size(); ++index)
    {
        EXPECT_EQ(serial->samples[index].accepted, built->samples[index].accepted);
        EXPECT_EQ(serial->samples[index].imageRelativeToGreen, built->samples[index].imageRelativeToGreen);
    }

    request.centroidSource = an::ApertureCentroidSource::CatalogProjectedPosition;
    const an::Result<an::SampleConstructionResult> projected = an::buildSpectrophotometricSamples(
        execution, frame, fixture.preflight, fixture.catalog, fixture.colors, request);
    ASSERT_TRUE(projected.hasValue()) << projected.status().message();
    EXPECT_NEAR(projected->samples[0].aperture.position.x, fixture.field.stars[0].x + 0.05, 1.0e-12);
    EXPECT_EQ(projected->acceptedCount, built->acceptedCount);
}

TEST(SpectrophotometricSamplesTest, ApertureRejectionsAndFluxFailuresAreCountedPerSample)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    SampleFixture fixture = makeFixture(execution);
    const an::ImageDescriptor descriptor = rgbDescriptor(320u, 320u);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(fixture.field.samples, layout, descriptor);

    an::SampleConstructionRequest request;
    request.aperture.radiusPolicy = an::ApertureRadiusPolicy::FixedPixels;
    request.aperture.apertureRadiusPixels = 4.0;
    request.aperture.annulusPolicy = an::AnnulusRadiusPolicy::FixedPixels;
    request.aperture.annulusInnerRadiusPixels = 14.0;
    request.aperture.annulusOuterRadiusPixels = 22.0;
    request.aperture.saturationThresholds = {0.2, 0.2, 0.2};
    const an::Result<an::SampleConstructionResult> saturated = an::buildSpectrophotometricSamples(
        execution, frame, fixture.preflight, fixture.catalog, fixture.colors, request);
    ASSERT_TRUE(saturated.hasValue()) << saturated.status().message();
    EXPECT_GT(saturated->apertureRejections[static_cast<std::size_t>(an::ApertureRejectionReason::Saturated)], 0u);
    EXPECT_EQ(saturated->rejections[static_cast<std::size_t>(an::SampleRejectionReason::ApertureRejected)],
              saturated->apertureRejections[static_cast<std::size_t>(an::ApertureRejectionReason::Saturated)]);
    for (const an::SpectrophotometricSample& sample : saturated->samples)
    {
        if (sample.rejection == an::SampleRejectionReason::ApertureRejected)
        {
            EXPECT_EQ(sample.apertureRejection, an::ApertureRejectionReason::Saturated);
            EXPECT_TRUE(sample.aperture.channels[0].saturated || sample.aperture.channels[1].saturated ||
                        sample.aperture.channels[2].saturated);
        }
    }

    an::WcsPreflightResult clipped = fixture.preflight;
    for (an::PreflightMatch& match : clipped.matches)
    {
        match.detectedPosition.y = 5.0;
    }
    request.aperture.saturationThresholds = {};
    const an::Result<an::SampleConstructionResult> edge =
        an::buildSpectrophotometricSamples(execution, frame, clipped, fixture.catalog, fixture.colors, request);
    ASSERT_TRUE(edge.hasValue()) << edge.status().message();
    EXPECT_EQ(edge->acceptedCount, 0u);
    EXPECT_EQ(edge->apertureRejections[static_cast<std::size_t>(an::ApertureRejectionReason::EdgeClipped)],
              edge->samples.size() - 1u);
    EXPECT_EQ(edge->rejections[static_cast<std::size_t>(an::SampleRejectionReason::MissingCatalogSource)], 1u);

    // A dark disk where the first star was: its green aperture sum falls below the sky.
    std::vector<float> holed = fixture.field.samples;
    const std::size_t planeSize = static_cast<std::size_t>(320u) * 320u;
    const color_test::PlantedColorStar& first = fixture.field.stars[0];
    for (std::int64_t row = static_cast<std::int64_t>(first.y) - 6; row <= static_cast<std::int64_t>(first.y) + 6;
         ++row)
    {
        for (std::int64_t column = static_cast<std::int64_t>(first.x) - 6;
             column <= static_cast<std::int64_t>(first.x) + 6; ++column)
        {
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                holed[channel * planeSize + static_cast<std::size_t>(row) * 320u + static_cast<std::size_t>(column)] =
                    0.0f;
            }
        }
    }
    const an::StarFrameView holedFrame = frameOf(holed, layout, descriptor);
    const an::Result<an::SampleConstructionResult> dark = an::buildSpectrophotometricSamples(
        execution, holedFrame, fixture.preflight, fixture.catalog, fixture.colors, request);
    ASSERT_TRUE(dark.hasValue()) << dark.status().message();
    EXPECT_EQ(dark->samples[0].rejection, an::SampleRejectionReason::NonPositiveGreenFlux);
    EXPECT_EQ(dark->rejections[static_cast<std::size_t>(an::SampleRejectionReason::NonPositiveGreenFlux)], 1u);
    EXPECT_LT(dark->samples[0].aperture.channels[1].flux, 0.0);
    EXPECT_STREQ(an::sampleRejectionReasonName(an::SampleRejectionReason::NonPositiveGreenFlux),
                 "NonPositiveGreenFlux");
    EXPECT_FALSE(an::isSampleRejectionReason(static_cast<an::SampleRejectionReason>(6u)));
}

TEST(SpectrophotometricSamplesTest, RefusesTheGateAndMalformedInputsBeforePlacingAnAperture)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    SampleFixture fixture = makeFixture(execution);
    const an::ImageDescriptor descriptor = rgbDescriptor(320u, 320u);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(fixture.field.samples, layout, descriptor);

    an::WcsPreflightResult unproven = fixture.preflight;
    unproven.report.source = an::WcsQualitySource::None;
    const an::Result<an::SampleConstructionResult> gated =
        an::buildSpectrophotometricSamples(execution, frame, unproven, fixture.catalog, fixture.colors);
    ASSERT_FALSE(gated.hasValue());
    EXPECT_EQ(gated.status().code(), an::ErrorCode::InvalidState);
    an::SampleConstructionRequest ungated;
    ungated.requireAcceptableWcsQuality = false;
    ungated.aperture.annulusPolicy = an::AnnulusRadiusPolicy::FixedPixels;
    ungated.aperture.annulusInnerRadiusPixels = 14.0;
    ungated.aperture.annulusOuterRadiusPixels = 22.0;
    ungated.aperture.radiusPolicy = an::ApertureRadiusPolicy::FixedPixels;
    ungated.aperture.apertureRadiusPixels = 4.0;
    const an::Result<an::SampleConstructionResult> allowed =
        an::buildSpectrophotometricSamples(execution, frame, unproven, fixture.catalog, fixture.colors, ungated);
    ASSERT_TRUE(allowed.hasValue()) << allowed.status().message();
    EXPECT_GT(allowed->acceptedCount, 0u);

    std::vector<an::CatalogColorReference> short_(fixture.colors.begin(), fixture.colors.end() - 1);
    EXPECT_EQ(an::buildSpectrophotometricSamples(execution, frame, fixture.preflight, fixture.catalog, short_)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    an::WcsPreflightResult none = fixture.preflight;
    none.matches.clear();
    EXPECT_EQ(an::buildSpectrophotometricSamples(execution, frame, none, fixture.catalog, {}).status().code(),
              an::ErrorCode::EmptySelection);
    an::SampleConstructionRequest badCentroid;
    badCentroid.centroidSource = static_cast<an::ApertureCentroidSource>(3u);
    EXPECT_EQ(an::buildSpectrophotometricSamples(execution, frame, fixture.preflight, fixture.catalog, fixture.colors,
                                                 badCentroid)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        an::buildSpectrophotometricSamples(execution, frame, fixture.preflight, an::SpectrumCatalog{}, fixture.colors)
            .status()
            .code(),
        an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::buildSpectrophotometricSamples(an::ExecutionContext{}, frame, fixture.preflight, fixture.catalog,
                                                 fixture.colors)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    const an::ImageDescriptor mono = rgbDescriptor(8u, 8u, an::SampleType::Float32, 1u);
    const an::ImageLayout monoLayout = planarLayout(mono);
    const std::vector<float> monoSamples(64u, 0.1f);
    EXPECT_EQ(an::buildSpectrophotometricSamples(execution, frameOf(monoSamples, monoLayout, mono), fixture.preflight,
                                                 fixture.catalog, fixture.colors)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(an::buildSpectrophotometricSamples(makeExecution(engine, 2u, stop.get_token()), frame, fixture.preflight,
                                                 fixture.catalog, fixture.colors)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
}

} // namespace
