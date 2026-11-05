// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the spectral response model: curves, the compiled response, synthetic
// photometry singly and in batches, the white reference and profile compatibility.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectralResponse.h>

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
#include <limits>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

using color_test::makeEngine;
using color_test::makeExecution;
using spectrum_test::FixtureChunk;

constexpr std::uint32_t kSampleCount = an::kXpSampledWavelengthCount;
constexpr double kStartNm = an::kXpSampledWavelengthStartNm;
constexpr double kEndNm = an::kXpSampledWavelengthEndNm;

[[nodiscard]] double wavelengthAt(std::uint32_t index)
{
    return an::spectralGridWavelengthNm(an::kXpSampledGrid, index);
}

[[nodiscard]] an::SpectralCurve constantCurve(const std::string& identifier, double value)
{
    an::SpectralCurve curve;
    curve.identifier = identifier;
    curve.version = "test-v1";
    curve.samples = {{.wavelengthNm = kStartNm, .value = value}, {.wavelengthNm = kEndNm, .value = value}};
    return curve;
}

[[nodiscard]] an::SpectralCurve boxCurve(const std::string& identifier, double startNm, double endNm)
{
    an::SpectralCurve curve;
    curve.identifier = identifier;
    curve.version = "test-v1";
    curve.samples = {{.wavelengthNm = startNm, .value = 1.0}, {.wavelengthNm = endNm, .value = 1.0}};
    return curve;
}

[[nodiscard]] an::ResponseProfile scaledRgbProfile(double red, double green, double blue)
{
    an::ResponseProfile profile;
    profile.identifier = "response.test.rgb";
    profile.version = "test-v1";
    profile.channels = {constantCurve("response.test.red", red), constantCurve("response.test.green", green),
                        constantCurve("response.test.blue", blue)};
    return profile;
}

[[nodiscard]] an::ResponseProfile bandedRgbProfile()
{
    an::ResponseProfile profile;
    profile.identifier = "response.test.banded";
    profile.version = "test-v1";
    profile.channels = {boxCurve("response.test.red.band", 640.0, 660.0),
                        boxCurve("response.test.green.band", 535.0, 555.0),
                        boxCurve("response.test.blue.band", 435.0, 455.0)};
    return profile;
}

[[nodiscard]] std::vector<float> constantSpectrum(double flux)
{
    return std::vector<float>(kSampleCount, static_cast<float>(flux));
}

[[nodiscard]] std::vector<float> rampSpectrum()
{
    std::vector<float> flux(kSampleCount, 0.0f);
    for (std::uint32_t index = 0u; index < kSampleCount; ++index)
    {
        flux[index] = static_cast<float>(wavelengthAt(index) / 100.0);
    }
    return flux;
}

[[nodiscard]] double expectedPhotonWeightedIntegral(double fluxScale, double responseScale)
{
    return fluxScale * responseScale * (kEndNm * kEndNm - kStartNm * kStartNm) * 0.5;
}

[[nodiscard]] double expectedUniformGridUncertainty(double fluxError, double responseScale)
{
    double variance = 0.0;
    for (std::uint32_t index = 0u; index < kSampleCount; ++index)
    {
        const double lambda = wavelengthAt(index);
        const double endpointScale = (index == 0u || index + 1u == kSampleCount) ? 0.5 : 1.0;
        const double weight = endpointScale * an::kXpSampledWavelengthStepNm * lambda * responseScale;
        variance += (fluxError * weight) * (fluxError * weight);
    }
    return std::sqrt(variance);
}

[[nodiscard]] an::PreparedResponse prepareOrFail(const an::ExecutionContext& execution,
                                                 const an::ResponseProfile& profile,
                                                 const an::ObservationModel& observation = {})
{
    const an::Result<an::PreparedResponse> prepared = an::prepareResponse(execution, profile, observation);
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    return prepared.hasValue() ? *prepared : an::PreparedResponse{};
}

TEST(SpectralResponseTest, CurveChecksNameTheFirstBrokenRuleAndEvaluationIsLinear)
{
    an::SpectralCurve triangle;
    triangle.identifier = "response.test.triangle";
    triangle.samples = {{.wavelengthNm = 400.0, .value = 0.0},
                        {.wavelengthNm = 500.0, .value = 1.0},
                        {.wavelengthNm = 600.0, .value = 0.0}};
    EXPECT_EQ(an::checkSpectralCurve(triangle), an::SpectralCurveError::None);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(triangle, 400.0), 0.0);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(triangle, 450.0), 0.5);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(triangle, 500.0), 1.0);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(triangle, 550.0), 0.5);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(triangle, 600.0), 0.0);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(triangle, 399.0), 0.0);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(triangle, 601.0), 0.0);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(triangle, std::numeric_limits<double>::quiet_NaN()), 0.0);

    an::SpectralCurve one = triangle;
    one.samples.resize(1u);
    EXPECT_EQ(an::checkSpectralCurve(one), an::SpectralCurveError::TooFewSamples);
    an::SpectralCurve nonFinite = triangle;
    nonFinite.samples[1].wavelengthNm = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::checkSpectralCurve(nonFinite), an::SpectralCurveError::NonFiniteSample);
    an::SpectralCurve flat = triangle;
    flat.samples[1].wavelengthNm = 400.0;
    EXPECT_EQ(an::checkSpectralCurve(flat), an::SpectralCurveError::NonIncreasingWavelength);
    an::SpectralCurve negative = triangle;
    negative.samples[1].value = -0.5;
    EXPECT_EQ(an::checkSpectralCurve(negative), an::SpectralCurveError::NegativeValue);
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(flat, 400.0), 0.0);
    EXPECT_STREQ(an::spectralCurveErrorName(an::SpectralCurveError::NonIncreasingWavelength),
                 "NonIncreasingWavelength");
    EXPECT_FALSE(an::isSpectralCurveError(static_cast<an::SpectralCurveError>(6u)));
    EXPECT_STREQ(an::responseProfileKindName(an::ResponseProfileKind::MonoSensorRgbFilters), "MonoSensorRgbFilters");
    EXPECT_FALSE(an::isResponseProfileKind(static_cast<an::ResponseProfileKind>(3u)));
}

TEST(SpectralResponseTest, PreparedResponseCompilesWeightsAndIntervals)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::PreparedResponse prepared = prepareOrFail(execution, scaledRgbProfile(2.0, 1.0, 0.5));
    EXPECT_TRUE(prepared.grid == an::kXpSampledGrid);
    EXPECT_EQ(prepared.profileIdentifier, "response.test.rgb");
    EXPECT_EQ(prepared.profileVersion, "test-v1");
    EXPECT_EQ(prepared.kind, an::ResponseProfileKind::ColorSensor);
    EXPECT_FALSE(prepared.atmosphereApplied);
    ASSERT_EQ(prepared.wavelengthsNm.size(), kSampleCount);
    EXPECT_DOUBLE_EQ(prepared.wavelengthsNm.front(), kStartNm);
    EXPECT_DOUBLE_EQ(prepared.wavelengthsNm.back(), kEndNm);
    const std::array<double, 3u> expected{2.0, 1.0, 0.5};
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        ASSERT_EQ(prepared.channelWeights[channel].size(), kSampleCount);
        for (const double weight : prepared.channelWeights[channel])
        {
            EXPECT_DOUBLE_EQ(weight, expected[channel]);
        }
        EXPECT_DOUBLE_EQ(prepared.integrationIntervals[channel].startNm, kStartNm);
        EXPECT_DOUBLE_EQ(prepared.integrationIntervals[channel].endNm, kEndNm);
    }

    an::SpectralGrid tiny;
    tiny.sampleCount = 3u;
    tiny.endNm = tiny.startNm + 2.0 * tiny.stepNm;
    EXPECT_EQ(an::prepareResponse(execution, scaledRgbProfile(1.0, 1.0, 1.0), {}, tiny).status().code(),
              an::ErrorCode::InvalidArgument);
    an::ResponseProfile malformed = scaledRgbProfile(1.0, 1.0, 1.0);
    malformed.channels[2].samples[1].wavelengthNm = kStartNm;
    const an::Result<an::PreparedResponse> refused = an::prepareResponse(execution, malformed);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(refused.status().message()).find("NonIncreasingWavelength"), std::string::npos);
    an::ResponseProfile badKind = scaledRgbProfile(1.0, 1.0, 1.0);
    badKind.kind = static_cast<an::ResponseProfileKind>(3u);
    EXPECT_EQ(an::prepareResponse(execution, badKind).status().code(), an::ErrorCode::InvalidArgument);
    an::ResponseProfile monoFilters = scaledRgbProfile(1.0, 1.0, 1.0);
    monoFilters.kind = an::ResponseProfileKind::MonoSensorRgbFilters;
    an::ObservationModel narrowband;
    narrowband.narrowband.enabled = true;
    EXPECT_EQ(an::prepareResponse(execution, monoFilters, narrowband).status().code(), an::ErrorCode::Unsupported);
    EXPECT_TRUE(an::prepareResponse(execution, monoFilters).hasValue());
    an::ObservationModel badAirmass;
    badAirmass.atmosphere.enabled = true;
    badAirmass.atmosphere.airmass = -1.0;
    const an::Result<an::PreparedResponse> airmass =
        an::prepareResponse(execution, scaledRgbProfile(1.0, 1.0, 1.0), badAirmass);
    ASSERT_FALSE(airmass.hasValue());
    EXPECT_EQ(airmass.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(airmass.status().message()).find("InvalidAirmass"), std::string::npos);
    EXPECT_EQ(an::prepareResponse(an::ExecutionContext{}, scaledRgbProfile(1.0, 1.0, 1.0)).status().code(),
              an::ErrorCode::NotInitialized);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(an::prepareResponse(makeExecution(engine, 2u, stop.get_token()), scaledRgbProfile(1.0, 1.0, 1.0))
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
}

TEST(SpectralResponseTest, IntegratesAConstantSpectrumWithPhotonWeighting)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::PreparedResponse prepared = prepareOrFail(execution, scaledRgbProfile(2.0, 1.0, 0.5));
    const std::vector<float> flux = constantSpectrum(3.0);
    const an::Result<an::SyntheticPhotometry> integrated = an::integrateSyntheticPhotometry(execution, prepared, flux);
    ASSERT_TRUE(integrated.hasValue()) << integrated.status().message();
    EXPECT_EQ(integrated->status, an::SyntheticPhotometryStatus::Integrated);
    const std::array<double, 3u> scales{2.0, 1.0, 0.5};
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        const double expected = expectedPhotonWeightedIntegral(3.0, scales[channel]);
        EXPECT_NEAR(integrated->counts[channel], expected, expected * 1.0e-9) << channel;
    }
    EXPECT_FALSE(integrated->uncertaintyAvailable);
    EXPECT_NEAR(integrated->relativeToGreen[0], 2.0, 1.0e-12);
    EXPECT_NEAR(integrated->relativeToGreen[1], 1.0, 1.0e-12);
    EXPECT_NEAR(integrated->relativeToGreen[2], 0.5, 1.0e-12);

    const an::PreparedResponse flat = prepareOrFail(execution, scaledRgbProfile(0.75, 0.75, 0.75));
    const std::vector<float> errors(kSampleCount, 0.25f);
    const an::Result<an::SyntheticPhotometry> propagated =
        an::integrateSyntheticPhotometry(execution, flat, constantSpectrum(2.0), errors);
    ASSERT_TRUE(propagated.hasValue()) << propagated.status().message();
    ASSERT_TRUE(propagated->uncertaintyAvailable);
    const double expectedUncertainty = expectedUniformGridUncertainty(0.25, 0.75);
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(propagated->counts[channel], expectedPhotonWeightedIntegral(2.0, 0.75),
                    expectedPhotonWeightedIntegral(2.0, 0.75) * 1.0e-9);
        EXPECT_NEAR(propagated->uncertainties[channel], expectedUncertainty, expectedUncertainty * 1.0e-9);
    }

    std::vector<float> nonFinite = constantSpectrum(1.0);
    nonFinite[10] = std::numeric_limits<float>::quiet_NaN();
    const an::Result<an::SyntheticPhotometry> invalid =
        an::integrateSyntheticPhotometry(execution, prepared, nonFinite);
    ASSERT_TRUE(invalid.hasValue());
    EXPECT_EQ(invalid->status, an::SyntheticPhotometryStatus::InvalidSpectrum);
    std::vector<float> negativeError(kSampleCount, 0.1f);
    negativeError[3] = -1.0f;
    const an::Result<an::SyntheticPhotometry> badError =
        an::integrateSyntheticPhotometry(execution, prepared, constantSpectrum(1.0), negativeError);
    ASSERT_TRUE(badError.hasValue());
    EXPECT_EQ(badError->status, an::SyntheticPhotometryStatus::InvalidSpectrum);
    const an::Result<an::SyntheticPhotometry> dark =
        an::integrateSyntheticPhotometry(execution, prepared, constantSpectrum(0.0));
    ASSERT_TRUE(dark.hasValue());
    EXPECT_EQ(dark->status, an::SyntheticPhotometryStatus::NonPositiveGreen);
    EXPECT_DOUBLE_EQ(dark->relativeToGreen[0], 1.0);
    EXPECT_STREQ(an::syntheticPhotometryStatusName(an::SyntheticPhotometryStatus::NonPositiveGreen),
                 "NonPositiveGreen");
    EXPECT_FALSE(an::isSyntheticPhotometryStatus(static_cast<an::SyntheticPhotometryStatus>(5u)));

    std::vector<float> shortFlux(kSampleCount - 1u, 1.0f);
    EXPECT_EQ(an::integrateSyntheticPhotometry(execution, prepared, shortFlux).status().code(),
              an::ErrorCode::InvalidArgument);
    std::vector<float> shortError(2u, 0.1f);
    EXPECT_EQ(an::integrateSyntheticPhotometry(execution, prepared, flux, shortError).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::integrateSyntheticPhotometry(an::ExecutionContext{}, prepared, flux).status().code(),
              an::ErrorCode::NotInitialized);
    an::PreparedResponse broken = prepared;
    broken.channelWeights[1].pop_back();
    EXPECT_EQ(an::integrateSyntheticPhotometry(execution, broken, flux).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(SpectralResponseTest, ResamplingUsesTheInterpolantAboveFivePointsAndLinearBelow)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::SpectralCurve triangle;
    triangle.identifier = "response.test.triangle";
    triangle.samples = {{.wavelengthNm = 400.0, .value = 0.0},
                        {.wavelengthNm = 500.0, .value = 1.0},
                        {.wavelengthNm = 600.0, .value = 0.0}};
    const an::Result<std::vector<double>> linear = an::resampleSpectralCurve(execution, triangle, an::kXpSampledGrid);
    ASSERT_TRUE(linear.hasValue()) << linear.status().message();
    ASSERT_EQ(linear->size(), kSampleCount);
    EXPECT_DOUBLE_EQ((*linear)[0], 0.0);
    EXPECT_DOUBLE_EQ((*linear)[57], 0.5);
    EXPECT_DOUBLE_EQ((*linear)[82], 1.0);
    EXPECT_DOUBLE_EQ((*linear)[132], 0.0);

    an::SpectralCurve quadratic;
    quadratic.identifier = "response.test.quadratic";
    for (std::uint32_t index = 0u; index < 11u; ++index)
    {
        const double lambda = 400.0 + 50.0 * static_cast<double>(index);
        const double centered = (lambda - 650.0) / 100.0;
        quadratic.samples.push_back({.wavelengthNm = lambda, .value = centered * centered});
    }
    const an::Result<std::vector<double>> akima = an::resampleSpectralCurve(execution, quadratic, an::kXpSampledGrid);
    ASSERT_TRUE(akima.hasValue()) << akima.status().message();
    EXPECT_NEAR((*akima)[97], 1.44, 1.0e-9);
    EXPECT_NEAR((*akima)[282], 6.25, 1.0e-9);
    EXPECT_NEAR((*akima)[157], 0.0, 1.0e-9);
    EXPECT_DOUBLE_EQ((*akima)[0], 0.0);
    EXPECT_DOUBLE_EQ((*akima)[kSampleCount - 1u], 0.0);

    an::SpectralGrid empty;
    empty.sampleCount = 0u;
    EXPECT_EQ(an::resampleSpectralCurve(execution, triangle, empty).status().code(), an::ErrorCode::InvalidArgument);
    an::SpectralCurve bad = triangle;
    bad.samples[2].value = -1.0;
    EXPECT_EQ(an::resampleSpectralCurve(execution, bad, an::kXpSampledGrid).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(SpectralResponseTest, CurvesComposeAndFilterResponsesCombine)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::array<an::SpectralCurve, 2u> components{constantCurve("sensor", 2.0), constantCurve("filter", 0.5)};
    const an::Result<an::SpectralCurve> composed =
        an::composeSpectralCurves(execution, components, "profile.red", "v7");
    ASSERT_TRUE(composed.hasValue()) << composed.status().message();
    EXPECT_EQ(composed->identifier, "profile.red");
    EXPECT_EQ(composed->version, "v7");
    ASSERT_EQ(composed->samples.size(), kSampleCount);
    for (std::uint32_t index = 0u; index < kSampleCount; ++index)
    {
        EXPECT_DOUBLE_EQ(composed->samples[index].wavelengthNm, wavelengthAt(index));
        EXPECT_DOUBLE_EQ(composed->samples[index].value, 1.0);
    }
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(*composed, 500.0), 1.0);
    EXPECT_EQ(an::composeSpectralCurves(execution, {}, "none", "v0").status().code(), an::ErrorCode::EmptySelection);

    const an::Result<an::SpectralCurve> combined =
        an::combineFilterResponses(constantCurve("sensor", 1.0), constantCurve("sensor+filter", 0.8),
                                   constantCurve("sensor+window", 0.5), "osc.filter.window", "v1");
    ASSERT_TRUE(combined.hasValue()) << combined.status().message();
    EXPECT_EQ(combined->identifier, "osc.filter.window");
    EXPECT_EQ(combined->samples.size(), 2u);
    EXPECT_NEAR(an::evaluateSpectralCurve(*combined, 500.0), 0.4, 1.0e-12);
    const an::Result<an::SpectralCurve> floored =
        an::combineFilterResponses(constantCurve("sensor", 0.0), constantCurve("sensor+filter", 0.8),
                                   constantCurve("sensor+window", 0.5), "osc.floored", "v1");
    ASSERT_TRUE(floored.hasValue());
    EXPECT_DOUBLE_EQ(an::evaluateSpectralCurve(*floored, 500.0), 0.0);
    EXPECT_EQ(an::combineFilterResponses(boxCurve("sensor", kStartNm, 600.0), boxCurve("filter", 700.0, kEndNm),
                                         constantCurve("window", 0.5), "osc.disjoint", "v1")
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
    an::SpectralCurve malformed = constantCurve("sensor", 1.0);
    malformed.samples.pop_back();
    EXPECT_EQ(an::combineFilterResponses(malformed, constantCurve("f", 1.0), constantCurve("w", 1.0), "x", "v1")
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(SpectralResponseTest, AtmosphereSuppressesBlueAndIsSkippedForMonochromeProfiles)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> flux = constantSpectrum(1.0);
    const an::PreparedResponse baseline = prepareOrFail(execution, bandedRgbProfile());
    const an::Result<an::SyntheticPhotometry> untouched = an::integrateSyntheticPhotometry(execution, baseline, flux);
    ASSERT_TRUE(untouched.hasValue());

    an::ObservationModel observation;
    observation.atmosphere.enabled = true;
    observation.atmosphere.airmass = 2.0;
    observation.atmosphere.pressureIsSeaLevel = false;
    const an::PreparedResponse corrected = prepareOrFail(execution, bandedRgbProfile(), observation);
    EXPECT_TRUE(corrected.atmosphereApplied);
    EXPECT_DOUBLE_EQ(corrected.airmass, 2.0);
    EXPECT_GT(corrected.atmosphereNormalization, 0.0);
    EXPECT_LE(corrected.atmosphereNormalization, 1.0);
    const an::Result<an::SyntheticPhotometry> extincted = an::integrateSyntheticPhotometry(execution, corrected, flux);
    ASSERT_TRUE(extincted.hasValue());
    EXPECT_LT(extincted->relativeToGreen[2], untouched->relativeToGreen[2]);
    EXPECT_GT(extincted->relativeToGreen[0], untouched->relativeToGreen[0]);

    an::ResponseProfile mono = bandedRgbProfile();
    mono.kind = an::ResponseProfileKind::MonoSensor;
    const an::PreparedResponse skipped = prepareOrFail(execution, mono, observation);
    EXPECT_FALSE(skipped.atmosphereApplied);
    const an::Result<an::SyntheticPhotometry> unchanged = an::integrateSyntheticPhotometry(execution, skipped, flux);
    ASSERT_TRUE(unchanged.hasValue());
    EXPECT_NEAR(unchanged->relativeToGreen[0], untouched->relativeToGreen[0], 1.0e-12);
    EXPECT_NEAR(unchanged->relativeToGreen[2], untouched->relativeToGreen[2], 1.0e-12);

    EXPECT_NEAR(an::airmassFromZenithAngle(0.0), 1.0, 1.0e-5);
    EXPECT_GT(an::airmassFromZenithAngle(60.0), 1.9);
    an::AtmosphericExtinction altitude;
    altitude.centerAltitudeDegrees = 30.0;
    EXPECT_DOUBLE_EQ(an::resolvedAirmass(altitude), an::airmassFromZenithAngle(60.0));
    EXPECT_DOUBLE_EQ(an::resolvedAirmass(an::AtmosphericExtinction{}),
                     an::airmassFromZenithAngle(an::kDefaultZenithAngleDegrees));
    an::AtmosphericExtinction stated;
    stated.airmass = 1.7;
    stated.centerAltitudeDegrees = 30.0;
    EXPECT_DOUBLE_EQ(an::resolvedAirmass(stated), 1.7);
    EXPECT_LT(an::atmosphericTransmittance(400.0, observation.atmosphere),
              an::atmosphericTransmittance(800.0, observation.atmosphere));
    EXPECT_LT(an::atmosphericTransmittance(800.0, observation.atmosphere), 1.0);

    an::ObservationModel model;
    model.atmosphere.enabled = true;
    model.atmosphere.observerHeightMeters = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::checkObservationModel(model), an::ObservationModelError::InvalidObserverHeight);
    model.atmosphere.observerHeightMeters = 10.0;
    model.atmosphere.pressureHectopascals = 0.0;
    EXPECT_EQ(an::checkObservationModel(model), an::ObservationModelError::InvalidPressure);
    model.atmosphere.pressureHectopascals = 1000.0;
    model.atmosphere.airmass = 0.0;
    EXPECT_EQ(an::checkObservationModel(model), an::ObservationModelError::InvalidAirmass);
    model.atmosphere.airmass.reset();
    model.atmosphere.centerAltitudeDegrees = 95.0;
    EXPECT_EQ(an::checkObservationModel(model), an::ObservationModelError::InvalidCenterAltitude);
    model.atmosphere.centerAltitudeDegrees.reset();
    model.narrowband.enabled = true;
    model.narrowband.passbands[1].bandwidthNm = 0.0;
    EXPECT_EQ(an::checkObservationModel(model), an::ObservationModelError::InvalidPassband);
    model.narrowband.enabled = false;
    model.atmosphere.enabled = false;
    model.atmosphere.pressureHectopascals = -5.0;
    EXPECT_EQ(an::checkObservationModel(model), an::ObservationModelError::None);
    EXPECT_STREQ(an::observationModelErrorName(an::ObservationModelError::InvalidPassband), "InvalidPassband");
    EXPECT_FALSE(an::isObservationModelError(static_cast<an::ObservationModelError>(6u)));
}

TEST(SpectralResponseTest, NarrowbandPassbandsConfineEachChannel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> ramp = rampSpectrum();
    an::ObservationModel observation;
    observation.narrowband.enabled = true;
    observation.narrowband.passbands = {an::NarrowbandPassband{.centerNm = 656.3, .bandwidthNm = 8.0},
                                        an::NarrowbandPassband{.centerNm = 500.7, .bandwidthNm = 8.0},
                                        an::NarrowbandPassband{.centerNm = 486.1, .bandwidthNm = 8.0}};
    const an::PreparedResponse prepared = prepareOrFail(execution, scaledRgbProfile(1.0, 1.0, 1.0), observation);
    EXPECT_NEAR(prepared.integrationIntervals[0].startNm, 652.3, 1.0e-12);
    EXPECT_NEAR(prepared.integrationIntervals[0].endNm, 660.3, 1.0e-12);
    const an::Result<an::SyntheticPhotometry> banded = an::integrateSyntheticPhotometry(execution, prepared, ramp);
    ASSERT_TRUE(banded.hasValue()) << banded.status().message();
    EXPECT_EQ(banded->status, an::SyntheticPhotometryStatus::Integrated);
    EXPECT_GT(banded->counts[0], banded->counts[1]);
    EXPECT_GT(banded->counts[1], banded->counts[2]);
    EXPECT_GT(banded->relativeToGreen[0], 1.0);
    EXPECT_LT(banded->relativeToGreen[2], 1.0);

    observation.narrowband.passbands[2] = observation.narrowband.passbands[1];
    const an::PreparedResponse doubled = prepareOrFail(execution, scaledRgbProfile(1.0, 1.0, 1.0), observation);
    const an::Result<an::SyntheticPhotometry> same = an::integrateSyntheticPhotometry(execution, doubled, ramp);
    ASSERT_TRUE(same.hasValue());
    EXPECT_NEAR(same->counts[1], same->counts[2], 1.0e-10);
    EXPECT_NEAR(same->relativeToGreen[2], 1.0, 1.0e-12);

    observation.narrowband.passbands[0] = an::NarrowbandPassband{.centerNm = 2000.0, .bandwidthNm = 8.0};
    const an::PreparedResponse outside = prepareOrFail(execution, scaledRgbProfile(1.0, 1.0, 1.0), observation);
    EXPECT_LE(outside.integrationIntervals[0].endNm, outside.integrationIntervals[0].startNm);
    const an::Result<an::SyntheticPhotometry> empty = an::integrateSyntheticPhotometry(execution, outside, ramp);
    ASSERT_TRUE(empty.hasValue());
    EXPECT_EQ(empty->status, an::SyntheticPhotometryStatus::Integrated);
    EXPECT_DOUBLE_EQ(empty->counts[0], 0.0);
    EXPECT_DOUBLE_EQ(empty->relativeToGreen[0], 0.0);
}

TEST(SpectralResponseTest, WhiteReferenceCountsAndGains)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::PreparedResponse prepared = prepareOrFail(execution, scaledRgbProfile(2.0, 1.0, 0.5));
    an::WhiteReferenceSpectrum white;
    white.identifier = "white.test.flat";
    white.version = "test-v1";
    white.samples = {{.wavelengthNm = kStartNm, .value = 4.0}, {.wavelengthNm = kEndNm, .value = 4.0}};
    const an::Result<std::array<double, 3u>> counts = an::integrateWhiteReference(execution, prepared, white);
    ASSERT_TRUE(counts.hasValue()) << counts.status().message();
    const std::array<double, 3u> scales{2.0, 1.0, 0.5};
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        const double expected = expectedPhotonWeightedIntegral(4.0, scales[channel]);
        EXPECT_NEAR((*counts)[channel], expected, expected * 1.0e-9);
    }
    const std::array<double, 3u> gains = an::whiteReferenceGains(*counts);
    EXPECT_NEAR(gains[0], 0.5, 1.0e-12);
    EXPECT_NEAR(gains[1], 1.0, 1.0e-12);
    EXPECT_NEAR(gains[2], 2.0, 1.0e-12);
    const an::Result<std::array<double, 3u>> resolved = an::whiteReferenceResponseGains(execution, prepared, white);
    ASSERT_TRUE(resolved.hasValue());
    EXPECT_EQ(*resolved, gains);

    an::WhiteReferenceSpectrum photon = white;
    photon.photonWeighted = true;
    const an::Result<std::array<double, 3u>> photonCounts = an::integrateWhiteReference(execution, prepared, photon);
    ASSERT_TRUE(photonCounts.hasValue());
    const double photonIntegral = 4.0 * (kEndNm - kStartNm);
    EXPECT_NEAR((*photonCounts)[0], photonIntegral * 2.0, photonIntegral * 1.0e-9);
    EXPECT_NEAR((*photonCounts)[1], photonIntegral, photonIntegral * 1.0e-9);
    EXPECT_NEAR((*photonCounts)[2], photonIntegral * 0.5, photonIntegral * 1.0e-9);

    const an::PreparedResponse darkRed = prepareOrFail(execution, scaledRgbProfile(0.0, 1.0, 0.5));
    EXPECT_EQ(an::whiteReferenceResponseGains(execution, darkRed, white).status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::whiteReferenceGains({0.0, 1.0, 1.0}), (std::array<double, 3u>{1.0, 1.0, 1.0}));
    an::WhiteReferenceSpectrum malformed = white;
    malformed.samples.pop_back();
    EXPECT_EQ(an::integrateWhiteReference(execution, prepared, malformed).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(SpectralResponseTest, BatchesIntegrateTheTableAndTheCatalogOnAnyThreadCount)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext execution = makeExecution(engine, 6u);
    const an::PreparedResponse prepared = prepareOrFail(execution, scaledRgbProfile(2.0, 1.0, 0.5));
    constexpr std::size_t kSpectra = 64u;
    std::vector<float> table(kSpectra * kSampleCount, 0.0f);
    std::vector<float> errors(kSpectra * kSampleCount, 0.01f);
    for (std::size_t spectrum = 0u; spectrum < kSpectra; ++spectrum)
    {
        const double slope = -0.5 + static_cast<double>(spectrum) / static_cast<double>(kSpectra);
        for (std::uint32_t index = 0u; index < kSampleCount; ++index)
        {
            const double normalized = (wavelengthAt(index) - kStartNm) / (kEndNm - kStartNm) * 2.0 - 1.0;
            table[spectrum * kSampleCount + index] = static_cast<float>(1.0 + slope * normalized);
        }
    }
    table[5u * kSampleCount + 7u] = std::numeric_limits<float>::infinity();
    for (std::uint32_t index = 0u; index < kSampleCount; ++index)
    {
        table[7u * kSampleCount + index] = 0.0f;
    }

    std::vector<an::SyntheticPhotometry> wide(kSpectra);
    const an::Result<an::SyntheticPhotometryBatchResult> ran =
        an::integrateSyntheticPhotometryTable(execution, prepared, table, errors, wide);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->integratedCount, kSpectra - 2u);
    EXPECT_EQ(ran->statuses[static_cast<std::size_t>(an::SyntheticPhotometryStatus::InvalidSpectrum)], 1u);
    EXPECT_EQ(ran->statuses[static_cast<std::size_t>(an::SyntheticPhotometryStatus::NonPositiveGreen)], 1u);
    EXPECT_GT(ran->workingBytes, 0u);
    EXPECT_EQ(wide[5].status, an::SyntheticPhotometryStatus::InvalidSpectrum);
    EXPECT_EQ(wide[7].status, an::SyntheticPhotometryStatus::NonPositiveGreen);
    for (std::size_t spectrum = 0u; spectrum < kSpectra; ++spectrum)
    {
        const std::span<const float> flux{table.data() + spectrum * kSampleCount, kSampleCount};
        const std::span<const float> error{errors.data() + spectrum * kSampleCount, kSampleCount};
        const an::Result<an::SyntheticPhotometry> single =
            an::integrateSyntheticPhotometry(execution, prepared, flux, error);
        ASSERT_TRUE(single.hasValue());
        EXPECT_EQ(single->status, wide[spectrum].status);
        EXPECT_EQ(single->counts, wide[spectrum].counts);
        EXPECT_EQ(single->uncertainties, wide[spectrum].uncertainties);
        EXPECT_EQ(single->relativeToGreen, wide[spectrum].relativeToGreen);
    }
    std::vector<an::SyntheticPhotometry> narrow(kSpectra);
    const an::ExecutionContext single = makeExecution(engine, 1u);
    ASSERT_TRUE(an::integrateSyntheticPhotometryTable(single, prepared, table, errors, narrow).hasValue());
    for (std::size_t spectrum = 0u; spectrum < kSpectra; ++spectrum)
    {
        EXPECT_EQ(narrow[spectrum].counts, wide[spectrum].counts);
        EXPECT_EQ(narrow[spectrum].uncertainties, wide[spectrum].uncertainties);
    }
    std::vector<an::SyntheticPhotometry> none;
    EXPECT_EQ(an::integrateSyntheticPhotometryTable(execution, prepared, {}, {}, none).status().code(),
              an::ErrorCode::EmptySelection);
    std::vector<an::SyntheticPhotometry> mismatched(kSpectra + 1u);
    EXPECT_EQ(an::integrateSyntheticPhotometryTable(execution, prepared, table, errors, mismatched).status().code(),
              an::ErrorCode::InvalidArgument);
    std::vector<float> shortErrors(3u, 0.1f);
    EXPECT_EQ(an::integrateSyntheticPhotometryTable(execution, prepared, table, shortErrors, wide).status().code(),
              an::ErrorCode::InvalidArgument);

    FixtureChunk chunk;
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        chunk.stars.push_back(spectrum_test::starAt(10.0 + static_cast<double>(index), 5.0, index));
    }
    const an::SpectrumCatalog catalog = spectrum_test::buildOrFail(execution, {chunk});
    std::vector<std::uint64_t> identifiers;
    for (const spectrum_test::FixtureSpectrumStar& star : chunk.stars)
    {
        identifiers.push_back(star.sourceIdentifier);
    }
    identifiers.push_back(999u);
    std::vector<an::SyntheticPhotometry> looked(identifiers.size());
    const an::Result<an::SyntheticPhotometryBatchResult> joined =
        an::integrateSyntheticPhotometryBatch(execution, prepared, catalog, identifiers, looked);
    ASSERT_TRUE(joined.hasValue()) << joined.status().message();
    EXPECT_EQ(joined->integratedCount, 8u);
    EXPECT_EQ(joined->statuses[static_cast<std::size_t>(an::SyntheticPhotometryStatus::MissingSource)], 1u);
    EXPECT_EQ(looked.back().status, an::SyntheticPhotometryStatus::MissingSource);
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        EXPECT_EQ(looked[index].status, an::SyntheticPhotometryStatus::Integrated);
        const std::uint32_t record = static_cast<std::uint32_t>(index);
        const an::Result<an::SyntheticPhotometry> direct =
            an::integrateSyntheticPhotometry(execution, prepared, catalog.flux(record), catalog.fluxError(record));
        ASSERT_TRUE(direct.hasValue());
        EXPECT_EQ(direct->counts, looked[index].counts);
        EXPECT_TRUE(looked[index].uncertaintyAvailable);
    }
    std::vector<an::SyntheticPhotometry> wrong(identifiers.size() - 1u);
    EXPECT_EQ(an::integrateSyntheticPhotometryBatch(execution, prepared, catalog, identifiers, wrong).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::integrateSyntheticPhotometryBatch(execution, prepared, an::SpectrumCatalog{}, identifiers, looked)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    an::SpectralGrid other = an::kXpSampledGrid;
    other.sampleCount = 100u;
    other.endNm = other.startNm + 99.0 * other.stepNm;
    const an::Result<an::PreparedResponse> foreign =
        an::prepareResponse(execution, scaledRgbProfile(1.0, 1.0, 1.0), {}, other);
    ASSERT_TRUE(foreign.hasValue());
    EXPECT_EQ(an::integrateSyntheticPhotometryBatch(execution, *foreign, catalog, identifiers, looked).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(SpectralResponseTest, CompatibilityVerdictsNameTheFirstFailingRule)
{
    an::ResponseProfile profile = scaledRgbProfile(1.0, 1.0, 1.0);
    an::ResponseProfileCapabilities capabilities;
    capabilities.compatibleColorSpaces = {an::ColorSpace::LinearRgb};
    capabilities.reviewed = true;
    an::FrameColorState frame;
    frame.colorSpace = an::ColorSpace::LinearRgb;
    EXPECT_EQ(an::checkResponseProfileCompatibility(profile, capabilities, frame),
              an::ResponseCompatibilityVerdict::Compatible);

    an::ResponseProfile anonymous = profile;
    anonymous.identifier.clear();
    EXPECT_EQ(an::checkResponseProfileCompatibility(anonymous, capabilities, frame),
              an::ResponseCompatibilityVerdict::MissingIdentity);
    capabilities.reviewed = false;
    EXPECT_EQ(an::checkResponseProfileCompatibility(profile, capabilities, frame),
              an::ResponseCompatibilityVerdict::UnreviewedAsset);
    capabilities.reviewed = true;
    frame.colorSpace = an::ColorSpace::DisplayRgb;
    EXPECT_EQ(an::checkResponseProfileCompatibility(profile, capabilities, frame),
              an::ResponseCompatibilityVerdict::IncompatibleColorSpace);
    frame.colorSpace = an::ColorSpace::LinearRgb;
    frame.rawWhiteBalanceApplied = true;
    EXPECT_EQ(an::checkResponseProfileCompatibility(profile, capabilities, frame),
              an::ResponseCompatibilityVerdict::RawWhiteBalanceNotModelled);
    capabilities.modelsRawWhiteBalance = true;
    EXPECT_EQ(an::checkResponseProfileCompatibility(profile, capabilities, frame),
              an::ResponseCompatibilityVerdict::Compatible);
    frame.cameraMatrixApplied = true;
    EXPECT_EQ(an::checkResponseProfileCompatibility(profile, capabilities, frame),
              an::ResponseCompatibilityVerdict::CameraMatrixNotModelled);
    capabilities.modelsCameraMatrix = true;
    EXPECT_EQ(an::checkResponseProfileCompatibility(profile, capabilities, frame),
              an::ResponseCompatibilityVerdict::Compatible);
    EXPECT_STREQ(an::responseCompatibilityVerdictName(an::ResponseCompatibilityVerdict::UnreviewedAsset),
                 "UnreviewedAsset");
    EXPECT_FALSE(an::isResponseCompatibilityVerdict(static_cast<an::ResponseCompatibilityVerdict>(6u)));
}

} // namespace
