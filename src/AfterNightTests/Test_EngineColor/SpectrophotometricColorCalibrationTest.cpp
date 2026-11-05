// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the spectrophotometric colour calibration: the plan, the matched-colour
// prediction, the apply tail, calibration from samples, the whole planted-field calibration, its
// refusals and the transition it records.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h>

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricCalibrationTransition.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "ColorTestSupport.h"
#include "SpectrumCatalogTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

using color_test::fieldRange;
using color_test::fieldUnit;
using color_test::frameOf;
using color_test::makeEngine;
using color_test::makeExecution;
using color_test::makeSolution;
using color_test::meanOver;
using color_test::planarLayout;
using color_test::rgbDescriptor;

constexpr std::uint32_t kWidth = 640u;
constexpr std::uint32_t kHeight = 480u;
constexpr std::uint32_t kCell = 64u;
constexpr std::array<double, 3u> kBackground{0.050, 0.062, 0.041};
constexpr double kStartNm = an::kXpSampledWavelengthStartNm;
constexpr double kEndNm = an::kXpSampledWavelengthEndNm;

[[nodiscard]] an::SpectralCurve bandpass(const std::string& identifier, std::vector<an::SpectralSample> samples)
{
    an::SpectralCurve curve;
    curve.identifier = identifier;
    curve.version = "test-v1";
    curve.samples = std::move(samples);
    return curve;
}

[[nodiscard]] an::ResponseProfile colorDiscriminatingProfile()
{
    an::ResponseProfile profile;
    profile.identifier = "response.test.color-bands";
    profile.version = "test-v1";
    profile.channels[0] = bandpass("response.test.red-band", {{.wavelengthNm = kStartNm, .value = 0.0},
                                                              {.wavelengthNm = 600.0, .value = 0.0},
                                                              {.wavelengthNm = 680.0, .value = 1.0},
                                                              {.wavelengthNm = 820.0, .value = 1.0},
                                                              {.wavelengthNm = kEndNm, .value = 0.0}});
    profile.channels[1] = bandpass("response.test.green-band", {{.wavelengthNm = kStartNm, .value = 0.0},
                                                                {.wavelengthNm = 480.0, .value = 0.0},
                                                                {.wavelengthNm = 540.0, .value = 1.0},
                                                                {.wavelengthNm = 620.0, .value = 1.0},
                                                                {.wavelengthNm = 700.0, .value = 0.0},
                                                                {.wavelengthNm = kEndNm, .value = 0.0}});
    profile.channels[2] = bandpass("response.test.blue-band", {{.wavelengthNm = kStartNm, .value = 0.0},
                                                               {.wavelengthNm = 380.0, .value = 1.0},
                                                               {.wavelengthNm = 500.0, .value = 1.0},
                                                               {.wavelengthNm = 560.0, .value = 0.0},
                                                               {.wavelengthNm = kEndNm, .value = 0.0}});
    return profile;
}

[[nodiscard]] an::ResponseProfile unitProfile()
{
    an::ResponseProfile profile;
    profile.identifier = "response.test.unit";
    profile.version = "test-v1";
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        profile.channels[channel] =
            bandpass("response.test.unit." + std::to_string(channel),
                     {{.wavelengthNm = kStartNm, .value = 1.0}, {.wavelengthNm = kEndNm, .value = 1.0}});
    }
    return profile;
}

[[nodiscard]] an::WhiteReferenceSpectrum flatWhite(double value)
{
    an::WhiteReferenceSpectrum white;
    white.identifier = "white.test.flat";
    white.version = "test-v1";
    white.samples = {{.wavelengthNm = kStartNm, .value = value}, {.wavelengthNm = kEndNm, .value = value}};
    return white;
}

[[nodiscard]] std::vector<float> linearSpectrum(double slope)
{
    std::vector<float> flux;
    flux.reserve(an::kXpSampledWavelengthCount);
    for (std::uint32_t index = 0u; index < an::kXpSampledWavelengthCount; ++index)
    {
        const double wavelength = an::spectralGridWavelengthNm(an::kXpSampledGrid, index);
        const double normalized = (wavelength - kStartNm) / (kEndNm - kStartNm) * 2.0 - 1.0;
        flux.push_back(static_cast<float>(1.0 + slope * normalized));
    }
    return flux;
}

struct PlantedSpectralStar
{
    double x = 0.0;
    double y = 0.0;
    double sigma = 1.6;
    std::array<double, 3u> amplitudes{};
    std::uint64_t identifier = 0u;
    std::array<double, 3u> catalogRatios{1.0, 1.0, 1.0};
};

struct EndToEndFixture
{
    std::vector<float> samples{};
    std::vector<PlantedSpectralStar> stars{};
    an::WcsSolution solution =
        makeSolution(kWidth, kHeight, 1.0, {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}, 0.0);
    an::SpectrumCatalogManifest manifest{};
    an::SpectrumCatalog catalog{};
    an::ResponseProfile profile{};
    an::WhiteReferenceSpectrum white{};
    double redFactor = 1.0;
    double blueFactor = 1.0;
};

/// Renders one round Gaussian into three planes.
void plant(std::vector<float>& samples, const PlantedSpectralStar& star)
{
    const std::size_t planeSize = static_cast<std::size_t>(kWidth) * kHeight;
    const double reach = 5.0 * star.sigma;
    const auto left = static_cast<std::int64_t>(std::floor(star.x - reach));
    const auto right = static_cast<std::int64_t>(std::ceil(star.x + reach));
    const auto top = static_cast<std::int64_t>(std::floor(star.y - reach));
    const auto bottom = static_cast<std::int64_t>(std::ceil(star.y + reach));
    const double inverse = 1.0 / (2.0 * star.sigma * star.sigma);
    for (std::int64_t row = (std::max)(std::int64_t{0}, top); row <= (std::min)(std::int64_t{kHeight} - 1, bottom);
         ++row)
    {
        for (std::int64_t column = (std::max)(std::int64_t{0}, left);
             column <= (std::min)(std::int64_t{kWidth} - 1, right); ++column)
        {
            const double dx = static_cast<double>(column) + 0.5 - star.x;
            const double dy = static_cast<double>(row) + 0.5 - star.y;
            const double shape = std::exp(-(dx * dx + dy * dy) * inverse);
            const std::size_t at = static_cast<std::size_t>(row) * kWidth + static_cast<std::size_t>(column);
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                samples[channel * planeSize + at] += static_cast<float>(star.amplitudes[channel] * shape);
            }
        }
    }
}

/// A planted field whose stars carry spectra, rendered so every star's channel ratios are the
/// spectrum's predicted ratios times two known factors, and the catalog that holds those spectra.
[[nodiscard]] EndToEndFixture makeFixture(const an::ExecutionContext& execution, double redFactor, double blueFactor)
{
    EndToEndFixture fixture;
    fixture.redFactor = redFactor;
    fixture.blueFactor = blueFactor;
    fixture.profile = colorDiscriminatingProfile();
    fixture.white = flatWhite(1.0);
    fixture.solution =
        makeSolution(kWidth, kHeight, 1.0, {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}, 0.0);
    const an::Result<an::PreparedResponse> prepared = an::prepareResponse(execution, fixture.profile);
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    fixture.manifest = spectrum_test::parseOrFail(execution, {spectrum_test::FixtureChunk{}});

    const std::size_t planeSize = static_cast<std::size_t>(kWidth) * kHeight;
    fixture.samples.assign(planeSize * 3u, 0.0f);
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        std::fill(fixture.samples.begin() + static_cast<std::ptrdiff_t>(channel * planeSize),
                  fixture.samples.begin() + static_cast<std::ptrdiff_t>((channel + 1u) * planeSize),
                  static_cast<float>(kBackground[channel]));
    }
    an::SpectrumChunk chunk;
    chunk.chunkIndex = 0;
    chunk.grid = fixture.manifest.grid;
    const std::uint32_t cellsX = kWidth / kCell;
    const std::uint32_t cellsY = kHeight / kCell;
    std::uint64_t ordinal = 0u;
    for (std::uint32_t cellY = 1u; cellY + 1u < cellsY; ++cellY)
    {
        for (std::uint32_t cellX = 1u; cellX + 1u < cellsX; ++cellX)
        {
            const std::uint64_t key = 977u + (static_cast<std::uint64_t>(cellY) << 24u) + (cellX << 4u);
            PlantedSpectralStar star;
            star.x = (static_cast<double>(cellX) + 0.3 + 0.4 * fieldUnit(key + 1u)) * kCell;
            star.y = (static_cast<double>(cellY) + 0.3 + 0.4 * fieldUnit(key + 2u)) * kCell;
            const double green = fieldRange(key + 3u, 0.15, 0.45);
            const double slope = fieldRange(key + 4u, -0.55, 0.55);
            const std::vector<float> flux = linearSpectrum(slope);
            const an::Result<an::SyntheticPhotometry> predicted =
                an::integrateSyntheticPhotometry(execution, *prepared, flux);
            EXPECT_TRUE(predicted.hasValue()) << predicted.status().message();
            EXPECT_EQ(predicted->status, an::SyntheticPhotometryStatus::Integrated);
            star.catalogRatios = predicted->relativeToGreen;
            star.amplitudes = {green * star.catalogRatios[0] * redFactor, green,
                               green * star.catalogRatios[2] * blueFactor};
            const an::Result<an::SkyCoordinate> direction = fixture.solution.pixelToWorld({.x = star.x, .y = star.y});
            EXPECT_TRUE(direction.hasValue()) << direction.status().message();
            star.identifier =
                spectrum_test::identifierAt(direction->rightAscensionDegrees, direction->declinationDegrees, ordinal);
            ++ordinal;
            plant(fixture.samples, star);
            fixture.stars.push_back(star);

            an::SpectrumRecord record;
            record.sourceIdentifier = star.identifier;
            record.coordinate = *direction;
            record.magnitudeG = 12.0;
            record.chunkIndex = 0;
            record.recordIndex = static_cast<std::uint32_t>(chunk.records.size());
            chunk.records.push_back(record);
            chunk.flux.insert(chunk.flux.end(), flux.begin(), flux.end());
            chunk.fluxError.insert(chunk.fluxError.end(), flux.size(), 1.0e-3f);
        }
    }
    // A little noise, so every detected star carries a signal-to-noise figure the source selection
    // can read; a noiseless field has none.
    for (std::size_t index = 0u; index < fixture.samples.size(); ++index)
    {
        fixture.samples[index] +=
            static_cast<float>(0.0012 * color_test::fieldNormal(977u * 1000003u + static_cast<std::uint64_t>(index)));
    }
    std::vector<an::SpectrumChunk> chunks;
    chunks.push_back(std::move(chunk));
    an::Result<an::SpectrumCatalog> built = an::buildSpectrumCatalog(execution, std::move(chunks), fixture.manifest);
    EXPECT_TRUE(built.hasValue()) << built.status().message();
    if (built.hasValue())
    {
        fixture.catalog = std::move(*built);
    }
    return fixture;
}

[[nodiscard]] an::SpectrophotometricColorCalibrationRequest fieldRequest()
{
    an::SpectrophotometricColorCalibrationRequest request;
    request.preflight.matchRadiusArcseconds = 3.0;
    request.preflight.astrometricCatalogFamilyId = "gaia_dr3";
    request.preflight.wcsSolutionId = "synthetic-wcs";
    request.fit.minimumSamples = 20u;
    request.fit.minimumCatalogRatioSpan = 0.05;
    return request;
}

[[nodiscard]] std::array<double, 3u> maxNormalized(const std::array<double, 3u>& raw)
{
    const double largest = (std::max)((std::max)(raw[0], raw[1]), raw[2]);
    return {raw[0] / largest, raw[1] / largest, raw[2] / largest};
}

/// The gains a perfect calibration of the fixture recovers: one over the white reference's catalog
/// ratio times the planted factor, per colour channel, normalized over the three.
[[nodiscard]] std::array<double, 3u> expectedGains(const an::SpectrophotometricColorCalibrationPlan& plan,
                                                   const EndToEndFixture& fixture)
{
    const double whiteRed = plan.whiteReferenceGains[1] / plan.whiteReferenceGains[0];
    const double whiteBlue = plan.whiteReferenceGains[1] / plan.whiteReferenceGains[2];
    return maxNormalized({1.0 / (whiteRed * fixture.redFactor), 1.0, 1.0 / (whiteBlue * fixture.blueFactor)});
}

[[nodiscard]] an::SpectrophotometricColorCalibrationPlan
planOrFail(const an::ExecutionContext& execution, const EndToEndFixture& fixture,
           const an::SpectrophotometricColorCalibrationRequest& request)
{
    const an::Result<an::SpectrophotometricColorCalibrationPlan> plan = an::planSpectrophotometricColorCalibration(
        execution, fixture.profile, fixture.white, {}, fixture.manifest, request);
    EXPECT_TRUE(plan.hasValue()) << plan.status().message();
    return plan.hasValue() ? *plan : an::SpectrophotometricColorCalibrationPlan{};
}

class RecordingProgressSink final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        ++count;
        lastCompleted = report.completed;
        lastTotal = report.total;
        if (checking)
        {
            wrongTotal = wrongTotal || report.total != an::kSpectrophotometricCalibrationStageCount;
            if (report.completed < stages.size())
            {
                stages[report.completed] = true;
            }
        }
    }

    std::uint32_t count = 0u;
    std::uint64_t lastCompleted = 0u;
    std::uint64_t lastTotal = 0u;
    bool checking = false;
    bool wrongTotal = false;
    std::array<bool, an::kSpectrophotometricCalibrationStageCount + 1u> stages{};
};

TEST(SpectrophotometricColorCalibrationTest, PlanCompilesTheResponseAnchorsTheFitAndFixesTheIdentity)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const EndToEndFixture fixture = makeFixture(execution, 1.0, 1.0);
    const an::SpectrophotometricColorCalibrationRequest request = fieldRequest();
    const an::SpectrophotometricColorCalibrationPlan plan = planOrFail(execution, fixture, request);
    EXPECT_EQ(plan.identity.responseProfileId, "response.test.color-bands");
    EXPECT_EQ(plan.identity.responseProfileVersion, "test-v1");
    EXPECT_EQ(plan.identity.whiteReferenceId, "white.test.flat");
    EXPECT_EQ(plan.identity.whiteReferenceVersion, "test-v1");
    EXPECT_EQ(plan.identity.catalogFamilyId, fixture.manifest.catalogFamilyId);
    EXPECT_EQ(plan.identity.catalogManifestId, fixture.manifest.manifestId);
    EXPECT_TRUE(plan.response.grid == fixture.manifest.grid);
    const an::Result<std::array<double, 3u>> gains =
        an::whiteReferenceResponseGains(execution, plan.response, fixture.white);
    ASSERT_TRUE(gains.hasValue());
    EXPECT_EQ(plan.whiteReferenceGains, *gains);
    EXPECT_EQ(plan.request.fit.whiteReferenceGains, *gains);
    EXPECT_EQ(plan.whiteReferenceGains, an::whiteReferenceGains(plan.whiteReferenceCounts));
    EXPECT_EQ(plan.request.samples.requireAcceptableWcsQuality, request.requireAcceptableWcsQuality);
    EXPECT_EQ(plan.request.fit.minimumSamples, 20u);
    EXPECT_STREQ(an::kSpectrophotometricColorCalibrationAlgorithmVersion, "spcc-sampled-xp-color-calibration-v3");

    const auto refuses = [&](const an::SpectrophotometricColorCalibrationRequest& bad) {
        return an::planSpectrophotometricColorCalibration(execution, fixture.profile, fixture.white, {},
                                                          fixture.manifest, bad)
            .status()
            .code();
    };
    an::SpectrophotometricColorCalibrationRequest bad = request;
    bad.filterName = "Ha 7nm";
    EXPECT_EQ(refuses(bad), an::ErrorCode::Unsupported);
    bad.allowNarrowbandInput = true;
    EXPECT_TRUE(
        an::planSpectrophotometricColorCalibration(execution, fixture.profile, fixture.white, {}, fixture.manifest, bad)
            .hasValue());
    bad = request;
    bad.clipping = static_cast<an::ColorCalibrationClipping>(3u);
    EXPECT_EQ(refuses(bad), an::ErrorCode::InvalidArgument);
    bad = request;
    bad.maximumStars = 0u;
    EXPECT_EQ(refuses(bad), an::ErrorCode::InvalidArgument);
    bad = request;
    bad.background.mode = static_cast<an::ColorCalibrationBackgroundMode>(2u);
    EXPECT_EQ(refuses(bad), an::ErrorCode::InvalidArgument);
    bad = request;
    bad.fit.minimumSamples = 0u;
    EXPECT_EQ(refuses(bad), an::ErrorCode::InvalidArgument);
    bad = request;
    bad.sourceSelection.minimumSignalToNoise = -1.0;
    EXPECT_EQ(refuses(bad), an::ErrorCode::InvalidArgument);
    bad = request;
    bad.preflight.matchRadiusArcseconds = 0.0;
    EXPECT_EQ(refuses(bad), an::ErrorCode::InvalidArgument);
    bad = request;
    bad.samples.centroidSource = static_cast<an::ApertureCentroidSource>(3u);
    EXPECT_EQ(refuses(bad), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planSpectrophotometricColorCalibration(an::ExecutionContext{}, fixture.profile, fixture.white, {},
                                                         fixture.manifest, request)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    an::ResponseProfile darkRed = fixture.profile;
    darkRed.channels[0] =
        bandpass("dark", {{.wavelengthNm = kStartNm, .value = 0.0}, {.wavelengthNm = kEndNm, .value = 0.0}});
    EXPECT_EQ(
        an::planSpectrophotometricColorCalibration(execution, darkRed, fixture.white, {}, fixture.manifest, request)
            .status()
            .code(),
        an::ErrorCode::InvalidData);
}

TEST(SpectrophotometricColorCalibrationTest, WholeCalibrationRecoversPlantedFactorsAndNeutralizesTheSky)
{
    const an::EngineContext engine = makeEngine(8u);
    RecordingProgressSink progress;
    an::ExecutionOptions options;
    options.maxConcurrency = 6u;
    options.progressSink = &progress;
    const an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(made.hasValue());
    const an::ExecutionContext& execution = *made;
    const EndToEndFixture fixture = makeFixture(execution, 1.6, 0.7);
    ASSERT_GE(fixture.stars.size(), 30u);
    const an::SpectrophotometricColorCalibrationPlan plan = planOrFail(execution, fixture, fieldRequest());
    const an::ImageDescriptor descriptor = rgbDescriptor(kWidth, kHeight);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(fixture.samples, layout, descriptor);
    std::vector<float> destination(fixture.samples.size(), 0.0f);
    an::StarDetectionRequest detection;
    detection.relaxedChecks = true;

    progress.checking = true;
    const auto discardedBefore = execution.operationCounters().discardedProgressReports;
    const an::Result<an::SpectrophotometricColorCalibrationResult> calibrated = an::calibrateSpectrophotometricColor(
        execution, plan, frame, destination.data(), destination.size() * sizeof(float), fixture.solution,
        fixture.catalog, detection);
    ASSERT_TRUE(calibrated.hasValue()) << calibrated.status().message();
    EXPECT_GE(calibrated->detectedStars, fixture.stars.size());
    EXPECT_GE(calibrated->preflightSources, 20u);
    EXPECT_GE(calibrated->preflight.matches.size(), 20u);
    EXPECT_EQ(calibrated->qualityVerdict, an::WcsQualityVerdict::Acceptable);
    EXPECT_EQ(calibrated->prediction.integratedCount, calibrated->preflight.matches.size());
    EXPECT_GE(calibrated->samples.acceptedCount, 20u);
    ASSERT_TRUE(calibrated->fit.accepted()) << an::spectrophotometricFitVerdictName(calibrated->fit.verdict);
    const std::array<double, 3u> expected = expectedGains(plan, fixture);
    EXPECT_NEAR(calibrated->fit.gains[0], expected[0], 0.01);
    EXPECT_NEAR(calibrated->fit.gains[1], expected[1], 0.01);
    EXPECT_NEAR(calibrated->fit.gains[2], expected[2], 0.01);
    EXPECT_LT(calibrated->fit.red.rmsLogResidual, 0.02);
    ASSERT_TRUE(calibrated->applied.background.has_value());
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(calibrated->applied.background->levels[channel], kBackground[channel], 1.0e-3);
    }
    EXPECT_EQ(calibrated->applied.apply.samplesWritten, destination.size());
    const double pedestal = an::meanBackgroundPedestal(calibrated->applied.background->levels);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(meanOver(destination, kWidth, kHeight, channel, 8u, 8u, 40u, 40u), pedestal, 2.0e-4) << channel;
    }
    EXPECT_GT(progress.count, 0u);
    EXPECT_FALSE(progress.wrongTotal);
    for (const bool delivered : progress.stages)
    {
        EXPECT_TRUE(delivered);
    }
    EXPECT_EQ(execution.operationCounters().discardedProgressReports, discardedBefore);
    EXPECT_EQ(progress.lastCompleted, an::kSpectrophotometricCalibrationStageCount);
    EXPECT_EQ(progress.lastTotal, an::kSpectrophotometricCalibrationStageCount);

    std::vector<float> serialDestination(fixture.samples.size(), 0.0f);
    const an::ExecutionContext single = makeExecution(engine, 1u);
    const an::Result<an::SpectrophotometricColorCalibrationResult> serial = an::calibrateSpectrophotometricColor(
        single, plan, frame, serialDestination.data(), serialDestination.size() * sizeof(float), fixture.solution,
        fixture.catalog, detection);
    ASSERT_TRUE(serial.hasValue()) << serial.status().message();
    EXPECT_EQ(serial->fit.gains, calibrated->fit.gains);
    EXPECT_EQ(serial->applied.offsets, calibrated->applied.offsets);
    EXPECT_EQ(serialDestination, destination);

    std::vector<float> inPlace = fixture.samples;
    const an::StarFrameView own = frameOf(inPlace, layout, descriptor);
    const an::Result<an::SpectrophotometricColorCalibrationResult> overwritten =
        an::calibrateSpectrophotometricColor(execution, plan, own, inPlace.data(), inPlace.size() * sizeof(float),
                                             fixture.solution, fixture.catalog, detection);
    ASSERT_TRUE(overwritten.hasValue()) << overwritten.status().message();
    EXPECT_EQ(inPlace, destination);
}

TEST(SpectrophotometricColorCalibrationTest, PredictionApplyAndSampleStagesAreReachableOnTheirOwn)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const EndToEndFixture fixture = makeFixture(execution, 1.0, 1.0);
    const an::SpectrophotometricColorCalibrationPlan plan = planOrFail(execution, fixture, fieldRequest());

    std::vector<an::PreflightMatch> matches(2u);
    matches[0].sourceIdentifier = fixture.stars[0].identifier;
    matches[1].sourceIdentifier = 999u;
    const an::Result<an::MatchedColorPrediction> predicted =
        an::predictMatchedCatalogColors(execution, plan.response, fixture.catalog, matches);
    ASSERT_TRUE(predicted.hasValue()) << predicted.status().message();
    ASSERT_EQ(predicted->colors.size(), 2u);
    EXPECT_TRUE(predicted->colors[0].available);
    EXPECT_NEAR(predicted->colors[0].relativeToGreen[0], fixture.stars[0].catalogRatios[0], 1.0e-12);
    EXPECT_FALSE(predicted->colors[1].available);
    EXPECT_EQ(predicted->batch.integratedCount, 1u);
    EXPECT_EQ(predicted->batch.statuses[static_cast<std::size_t>(an::SyntheticPhotometryStatus::MissingSource)], 1u);
    EXPECT_EQ(an::predictMatchedCatalogColors(execution, plan.response, fixture.catalog, {}).status().code(),
              an::ErrorCode::EmptySelection);

    const an::ImageDescriptor descriptor = rgbDescriptor(8u, 8u);
    const an::ImageLayout layout = planarLayout(descriptor);
    std::vector<float> uniform(8u * 8u * 3u, 0.0f);
    std::fill(uniform.begin(), uniform.begin() + 64, 0.4f);
    std::fill(uniform.begin() + 64, uniform.begin() + 128, 0.2f);
    std::fill(uniform.begin() + 128, uniform.end(), 0.1f);
    const an::StarFrameView frame = frameOf(uniform, layout, descriptor);
    std::vector<float> destination(uniform.size(), 0.0f);
    an::SpectrophotometricBackgroundRequest neutralize;
    const an::Result<an::SpectrophotometricApplyResult> applied = an::applySpectrophotometricGains(
        execution, frame, destination.data(), destination.size() * sizeof(float), {0.5, 1.0, 2.0}, neutralize);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    ASSERT_TRUE(applied->background.has_value());
    EXPECT_NEAR(applied->background->levels[0], 0.4, 1.0e-6);
    EXPECT_NEAR(applied->background->levels[2], 0.1, 1.0e-6);
    const double pedestal = an::meanBackgroundPedestal(applied->background->levels);
    for (std::size_t index = 0u; index < destination.size(); ++index)
    {
        EXPECT_NEAR(destination[index], pedestal, 1.0e-6) << index;
    }
    EXPECT_EQ(applied->apply.samplesWritten, destination.size());
    neutralize.neutralize = false;
    neutralize.mode = static_cast<an::ColorCalibrationBackgroundMode>(2u);
    EXPECT_EQ(an::applySpectrophotometricGains(execution, frame, destination.data(), destination.size() * sizeof(float),
                                               {1.0, 1.0, 1.0}, neutralize)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    EndToEndFixture unit = fixture;
    unit.profile = unitProfile();
    an::SpectrophotometricColorCalibrationRequest request;
    request.fit.minimumSamples = 5u;
    request.fit.minimumCatalogRatioSpan = 0.1;
    request.background.neutralize = false;
    const an::SpectrophotometricColorCalibrationPlan flatPlan = planOrFail(execution, unit, request);
    EXPECT_EQ(flatPlan.whiteReferenceGains, (std::array<double, 3u>{1.0, 1.0, 1.0}));
    std::vector<an::SpectrophotometricSample> samples;
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        const double offset = static_cast<double>(index);
        an::SpectrophotometricSample sample;
        sample.sourceIdentifier = 900000000000ull + index;
        sample.accepted = true;
        sample.catalogRelativeToGreen = {0.7 + 0.06 * offset, 1.0, 1.35 - 0.05 * offset};
        sample.imageRelativeToGreen = {(0.7 + 0.06 * offset) * 2.0, 1.0, (1.35 - 0.05 * offset) * 0.5};
        samples.push_back(sample);
    }
    std::vector<float> written(uniform.size(), 0.0f);
    const an::Result<an::SpectrophotometricSampleCalibrationResult> fromSamples =
        an::calibrateSpectrophotometricColorFromSamples(execution, flatPlan, frame, written.data(),
                                                        written.size() * sizeof(float), samples);
    ASSERT_TRUE(fromSamples.hasValue()) << fromSamples.status().message();
    ASSERT_TRUE(fromSamples->fit.accepted());
    const std::array<double, 3u> expected = maxNormalized({0.5, 1.0, 2.0});
    EXPECT_NEAR(fromSamples->fit.gains[0], expected[0], 1.0e-12);
    EXPECT_NEAR(fromSamples->fit.gains[2], expected[2], 1.0e-12);
    EXPECT_FALSE(fromSamples->applied.background.has_value());
    EXPECT_NEAR(written[0], 0.1, 1.0e-6);
    EXPECT_NEAR(written[64], 0.1, 1.0e-6);
    EXPECT_NEAR(written[128], 0.1, 1.0e-6);

    std::vector<float> untouched(uniform.size(), 0.0f);
    const std::vector<an::SpectrophotometricSample> few(samples.begin(), samples.begin() + 3);
    const an::Result<an::SpectrophotometricSampleCalibrationResult> refused =
        an::calibrateSpectrophotometricColorFromSamples(execution, flatPlan, frame, untouched.data(),
                                                        untouched.size() * sizeof(float), few);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
    EXPECT_NE(std::string(refused.status().message()).find("InsufficientSamples"), std::string::npos);
    EXPECT_TRUE(std::all_of(untouched.begin(), untouched.end(), [](const float value) { return value == 0.0f; }));
}

TEST(SpectrophotometricColorCalibrationTest, TransitionRecordsTheRecipeAndSetsTheFlag)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    request.processingState.flatFielded = true;
    const an::Result<an::ImageSemantics> input = an::ImageSemantics::create(request);
    ASSERT_TRUE(input.hasValue());
    an::SpectrophotometricCalibrationIdentity identity;
    identity.responseProfileId = "response.test.color-bands";
    identity.responseProfileVersion = "test-v1";
    identity.whiteReferenceId = "white.test.flat";
    identity.whiteReferenceVersion = "test-v2";
    identity.catalogFamilyId = an::kXpCatalogFamilyId;
    identity.catalogManifestId = "manifest-test-001";
    const an::ColorChannelGains gains{0.8, 1.0, 0.9};
    const an::ColorChannelOffsets offsets{0.01, 0.02, 0.03};
    const std::string detail = an::spectrophotometricCalibrationLineageDetail(identity, gains, offsets);
    EXPECT_NE(detail.find(an::kSpectrophotometricColorCalibrationAlgorithmVersion), std::string::npos);
    EXPECT_NE(detail.find("response.test.color-bands@test-v1"), std::string::npos);
    EXPECT_NE(detail.find("white.test.flat@test-v2"), std::string::npos);
    EXPECT_NE(detail.find("manifest-test-001"), std::string::npos);
    EXPECT_NE(detail.find("gains=0.8"), std::string::npos);
    EXPECT_NE(detail.find("offsets=0.01"), std::string::npos);

    const an::Result<an::MetadataTransition> transition =
        an::spectrophotometricCalibrationTransition(*input, identity, gains, offsets);
    ASSERT_TRUE(transition.hasValue()) << transition.status().message();
    EXPECT_TRUE(an::changesMeaning(*transition));
    ASSERT_TRUE(transition->semantics.has_value());
    EXPECT_TRUE(transition->semantics->processingState().colorCalibrated);
    EXPECT_TRUE(transition->semantics->processingState().flatFielded);
    EXPECT_EQ(transition->semantics->transferFunction(), an::TransferFunction::Linear);
    ASSERT_TRUE(transition->record.has_value());
    EXPECT_EQ(transition->record->operation(), "an::calibrateSpectrophotometricColor");
    EXPECT_EQ(transition->record->detail(), detail);
    EXPECT_FALSE(transition->astrometry.has_value());
    EXPECT_FALSE(transition->clearCodecMetadata);
}

TEST(SpectrophotometricColorCalibrationTest, RefusalsAreNamedAndNothingIsWrittenBeforeTheApply)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const EndToEndFixture fixture = makeFixture(execution, 1.3, 0.8);
    const an::SpectrophotometricColorCalibrationRequest request = fieldRequest();
    const an::SpectrophotometricColorCalibrationPlan plan = planOrFail(execution, fixture, request);
    const an::ImageDescriptor descriptor = rgbDescriptor(kWidth, kHeight);
    const an::ImageLayout layout = planarLayout(descriptor);
    const an::StarFrameView frame = frameOf(fixture.samples, layout, descriptor);
    std::vector<float> destination(fixture.samples.size(), 0.0f);
    const std::uint64_t bytes = destination.size() * sizeof(float);
    an::StarDetectionRequest detection;
    detection.relaxedChecks = true;
    const auto untouched = [&destination]() {
        return std::all_of(destination.begin(), destination.end(), [](const float value) { return value == 0.0f; });
    };

    const an::WcsSolution shifted = makeSolution(
        kWidth, kHeight, 1.0, {.rightAscensionDegrees = 180.0 + 30.0 / 3600.0, .declinationDegrees = 0.0}, 0.0);
    const an::Result<an::SpectrophotometricColorCalibrationResult> gated = an::calibrateSpectrophotometricColor(
        execution, plan, frame, destination.data(), bytes, shifted, fixture.catalog, detection);
    ASSERT_FALSE(gated.hasValue());
    EXPECT_EQ(gated.status().code(), an::ErrorCode::InvalidState);
    EXPECT_NE(std::string(gated.status().message()).find("TooFewMatchedStars"), std::string::npos);
    EXPECT_TRUE(untouched());

    an::SpectrophotometricColorCalibrationRequest lenient = request;
    lenient.requireAcceptableWcsQuality = false;
    const an::SpectrophotometricColorCalibrationPlan lenientPlan = planOrFail(execution, fixture, lenient);
    EXPECT_FALSE(lenientPlan.request.samples.requireAcceptableWcsQuality);
    const an::Result<an::SpectrophotometricColorCalibrationResult> unmatched = an::calibrateSpectrophotometricColor(
        execution, lenientPlan, frame, destination.data(), bytes, shifted, fixture.catalog, detection);
    ASSERT_FALSE(unmatched.hasValue());
    EXPECT_EQ(unmatched.status().code(), an::ErrorCode::EmptySelection);
    EXPECT_TRUE(untouched());

    an::SpectrophotometricColorCalibrationRequest starved = request;
    starved.fit.minimumSamples = 1000u;
    starved.fit.lowSupportSamples = 1000u;
    const an::SpectrophotometricColorCalibrationPlan starvedPlan = planOrFail(execution, fixture, starved);
    const an::Result<an::SpectrophotometricColorCalibrationResult> unfitted = an::calibrateSpectrophotometricColor(
        execution, starvedPlan, frame, destination.data(), bytes, fixture.solution, fixture.catalog, detection);
    ASSERT_FALSE(unfitted.hasValue());
    EXPECT_EQ(unfitted.status().code(), an::ErrorCode::InvalidData);
    EXPECT_NE(std::string(unfitted.status().message()).find("InsufficientSamples"), std::string::npos);
    EXPECT_TRUE(untouched());

    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(an::calibrateSpectrophotometricColor(makeExecution(engine, 2u, stop.get_token()), plan, frame,
                                                   destination.data(), bytes, fixture.solution, fixture.catalog,
                                                   detection)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(an::calibrateSpectrophotometricColor(execution, plan, frame, destination.data(), bytes, fixture.solution,
                                                   an::SpectrumCatalog{}, detection)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::calibrateSpectrophotometricColor(an::ExecutionContext{}, plan, frame, destination.data(), bytes,
                                                   fixture.solution, fixture.catalog, detection)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::calibrateSpectrophotometricColor(execution, plan, frame, destination.data(), bytes - 1u,
                                                   fixture.solution, fixture.catalog, detection)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    const an::ImageDescriptor display = rgbDescriptor(kWidth, kHeight, an::SampleType::Float32, 3u,
                                                      an::NumericDomain::NormalizedLinear, an::ColorSpace::DisplayRgb);
    const an::StarFrameView displayFrame = frameOf(fixture.samples, planarLayout(display), display);
    EXPECT_EQ(an::calibrateSpectrophotometricColor(execution, plan, displayFrame, destination.data(), bytes,
                                                   fixture.solution, fixture.catalog, detection)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);
    const an::ImageDescriptor mono = rgbDescriptor(8u, 8u, an::SampleType::Float32, 1u);
    const an::ImageLayout monoLayout = planarLayout(mono);
    const std::vector<float> monoSamples(64u, 0.1f);
    EXPECT_EQ(an::calibrateSpectrophotometricColor(execution, plan, frameOf(monoSamples, monoLayout, mono),
                                                   destination.data(), bytes, fixture.solution, fixture.catalog,
                                                   detection)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);
    const an::ImageDescriptor integer = rgbDescriptor(8u, 8u, an::SampleType::UInt16, 3u);
    const an::ImageLayout integerLayout = planarLayout(integer);
    const std::vector<std::uint16_t> integerSamples(8u * 8u * 3u, 1000u);
    EXPECT_EQ(an::calibrateSpectrophotometricColor(execution, plan, frameOf(integerSamples, integerLayout, integer),
                                                   destination.data(), bytes, fixture.solution, fixture.catalog,
                                                   detection)
                  .status()
                  .code(),
              an::ErrorCode::Unsupported);
    EXPECT_TRUE(untouched());
}

} // namespace
