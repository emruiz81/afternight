// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent statistical stretch analysis, curve and full-output failure-path tests.
#include "StatisticalStretchOracle.h"
#include "SyntheticFixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace an::bench;

namespace
{
StretchSettings statisticalSettings(float target, float sigmas = 5.0f)
{
    StretchSettings settings;
    settings.function = 9u;
    settings.targetMedian = target;
    settings.blackpointSigma = sigmas;
    return settings;
}

double sortedMedian(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const double lower = values[(values.size() - 1u) / 2u];
    const double upper = values[values.size() / 2u];
    return lower + 0.5 * (upper - lower);
}

/// A deterministic image of `count` values in the unit interval with repeats, both zeros and a
/// minimum placed where no sample index of the bounded stride lands.
std::vector<float> mixedValues(std::size_t count)
{
    std::vector<float> values(count);
    std::uint64_t state = 0x243F6A8885A308D3ull;
    for (std::size_t i = 0; i < count; ++i)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double unit = static_cast<double>(state >> 11) * (1.0 / 9007199254740992.0);
        // One in four values is a repeated code, so ranks fall inside runs of equal values.
        values[i] = (state >> 7) % 4u == 0u ? std::round(static_cast<float>(unit) * 64.0f) / 64.0f
                                            : 0.02f + 0.9f * static_cast<float>(unit);
    }
    values[0] = 0.0f;
    values[1] = -0.0f;
    values[count - 2u] = 0.004f;
    values[count / 3u + 1u] = -0.0009765625f;
    return values;
}

ScenarioSpec rampScenario()
{
    auto scenario = stretchScenarios()[9];
    EXPECT_EQ(scenario.id, "stretch.function_9.f32");
    return scenario;
}
} // namespace

TEST(StatisticalStretchOracleTest, MinimumAndMedianAreEveryFiniteSamplesOrderStatisticsNotAnEstimate)
{
    for (const std::size_t count : {std::size_t{1000003}, std::size_t{1000004}, std::size_t{257}, std::size_t{2}})
    {
        auto values = count > 4u ? mixedValues(count) : std::vector<float>{0.75f, 0.25f};
        values.push_back(std::numeric_limits<float>::quiet_NaN());
        values.push_back(std::numeric_limits<float>::infinity());
        const auto reference = measureStatisticalStretch(statisticalSettings(0.3f), values);
        std::vector<double> finite;
        for (const float value : values)
            if (std::isfinite(value))
                finite.push_back(value);
        EXPECT_EQ(reference.samples, finite.size());
        EXPECT_EQ(reference.minimum, *std::min_element(finite.begin(), finite.end())) << count;
        EXPECT_EQ(reference.maximum, *std::max_element(finite.begin(), finite.end())) << count;
        EXPECT_EQ(reference.median, sortedMedian(finite)) << count;
    }
    // The minimum is placed off every sampled index, so a sampled minimum would miss it.
    const auto values = mixedValues(1000003u);
    const std::size_t stride = (values.size() + kStatisticalStretchSampleLimit - 1u) / kStatisticalStretchSampleLimit;
    ASSERT_NE((values.size() / 3u + 1u) % stride, 0u);
    EXPECT_EQ(measureStatisticalStretch(statisticalSettings(0.3f), values).minimum, -0.0009765625);
}

TEST(StatisticalStretchOracleTest, SpreadIsTheLowerHalfDeviationOfTheBoundedStrideSample)
{
    const auto values = mixedValues(1000003u);
    const std::size_t stride = 3u;
    std::vector<double> sample;
    for (std::size_t i = 0; i < values.size(); i += stride)
        sample.push_back(values[i]);
    std::sort(sample.begin(), sample.end());
    const double sampleMedian = sortedMedian(sample);
    std::vector<double> lower(sample.begin(), std::upper_bound(sample.begin(), sample.end(), sampleMedian));
    const double lowerMedian = sortedMedian(lower);
    for (double& value : lower)
        value = std::abs(value - lowerMedian);
    const double sigma = sortedMedian(lower) * 1.4826;

    auto settings = statisticalSettings(0.3f, 0.25f);
    const auto reference = measureStatisticalStretch(settings, values);
    EXPECT_EQ(reference.sampledValues, sample.size());
    EXPECT_LE(reference.sampledValues, kStatisticalStretchSampleLimit);
    EXPECT_EQ(reference.lowerHalfSigma, sigma);
    EXPECT_EQ(reference.blackPoint, (std::min)((std::max)(reference.minimum, reference.median - 0.25 * sigma), 0.99));
    settings.noBlackClip = true;
    EXPECT_EQ(measureStatisticalStretch(settings, values).blackPoint, reference.minimum);
    settings.noBlackClip = false;
    settings.blackpointSigma = 1000.0f;
    EXPECT_EQ(measureStatisticalStretch(settings, values).blackPoint, reference.minimum);
}

TEST(StatisticalStretchOracleTest, CurveValuesFromTheDefinitionSendTheMedianToTheTarget)
{
    // Black point 0.25 and median 0.4375 rescale the median to one quarter; with a target of one
    // half the fraction is 0.375 r / (0.125 + 0.25 r), exact at these dyadic inputs.
    StatisticalStretchOracle oracle(statisticalSettings(0.5f), 0.4375, 0.25);
    EXPECT_EQ(oracle.forward(0.4375L), 0.5L);
    EXPECT_EQ(oracle.forward(0.625L), 0.75L);
    EXPECT_EQ(oracle.forward(0.25L), 0.0L);
    EXPECT_EQ(oracle.forward(1.0L), 1.0L);
    // Everything at or below the black point is cut, and nothing above one exceeds it.
    EXPECT_EQ(oracle.forward(0.1L), 0.0L);
    EXPECT_EQ(oracle.forward(-0.5L), 0.0L);
    EXPECT_EQ(oracle.forward(1.5L), 1.0L);
    for (long double value = 0.0L; value < 1.0L; value += 0.0078125L)
        EXPECT_LE(oracle.forward(value), oracle.forward(value + 0.0078125L)) << static_cast<double>(value);
    // A median above the target compresses rather than lifts and still reaches one at one.
    StatisticalStretchOracle compress(statisticalSettings(0.25f), 0.625, 0.0);
    EXPECT_NEAR(static_cast<double>(compress.forward(0.625L)), 0.25, 1.0e-15);
    EXPECT_NEAR(static_cast<double>(compress.forward(1.0L)), 1.0, 1.0e-15);
}

TEST(StatisticalStretchOracleTest, AMarginFarBelowTheBlackPointIsCutWhereTheUncutFractionPassesItsPole)
{
    // A background of 0.05 with a black point of 0.04 rescales the median to 0.01/0.96; below
    // about 0.0158 the uncut fraction's denominator changes sign and a black margin maps above one.
    const double blackPoint = 0.04;
    const double median = 0.05;
    const long double m = (median - blackPoint) / (1.0 - blackPoint);
    const long double t = 0.3L;
    const long double r = (0.0L - blackPoint) / (1.0L - blackPoint);
    const long double uncut = (m - 1.0L) * t * r / (m * (t + r - 1.0L) - t * r);
    EXPECT_GT(uncut, 1.0L) << "the fraction without the cut maps black above white";
    StatisticalStretchOracle oracle(statisticalSettings(0.3f), median, blackPoint);
    EXPECT_EQ(oracle.forward(0.0L), 0.0L);
    EXPECT_EQ(oracle.forward(0.01L), 0.0L);
    EXPECT_NEAR(static_cast<double>(oracle.forward(0.05L)), static_cast<double>(0.3f), 1.0e-15);
}

TEST(StatisticalStretchOracleTest, HighlightCurveAndNormalizationFollowTheTransfer)
{
    auto settings = statisticalSettings(0.5f);
    settings.applyCurves = true;
    settings.curvesBoost = 0.5f;
    StatisticalStretchOracle curves(settings, 0.4375, 0.25);
    // Up to the target the six-point curve is the identity; above it the points are lifted.
    EXPECT_EQ(curves.forward(0.4375L), 0.5L);
    EXPECT_EQ(curves.forward(0.25L), 0.0L);
    EXPECT_EQ(curves.forward(1.0L), 1.0L);
    // The transfer maps 0.625 to 0.75: halfway between the points at 0.625 and 0.875, which are
    // lifted to 0.625^0.5 and 0.875^0.25.
    const long double lifted = std::sqrt(0.625L) + 0.5L * (std::pow(0.875L, 0.25L) - std::sqrt(0.625L));
    EXPECT_NEAR(static_cast<double>(curves.forward(0.625L)), static_cast<double>(lifted), 1.0e-15);
    settings.curvesBoost = 0.0f;
    StatisticalStretchOracle identity(settings, 0.4375, 0.25);
    EXPECT_NEAR(static_cast<double>(identity.forward(0.625L)), 0.75, 1.0e-15);

    auto normalized = statisticalSettings(0.5f);
    normalized.normalize = true;
    StatisticalStretchOracle oracle(normalized, 0.4375, 0.25);
    const std::vector<float> input{0.25f, 0.4375f, 0.625f};
    oracle.measureDivisor(input);
    EXPECT_EQ(oracle.divisor(), 0.75L);
    EXPECT_EQ(oracle.forward(0.625L), 1.0L);
    EXPECT_EQ(oracle.forward(0.4375L), 0.5L / 0.75L);
}

TEST(StatisticalStretchOracleTest, CompleteEvidenceRefusesAnyWrongPixelOrMissingCounter)
{
    auto scenario = rampScenario();
    scenario.input.width = 10001u;
    scenario.elementsProcessed = 10001u;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    ASSERT_TRUE(requiresStatisticalStretchOracle(scenario));
    std::vector<float> input(10001u), output(10001u);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i) / 10000.0f;
    const auto reference = measureStatisticalStretch(*settings, input);
    StatisticalStretchOracle oracle(*settings, reference.median, reference.blackPoint);
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = static_cast<float>(oracle.forward(input[i]));
    ResultRecord record;
    checkStatisticalStretchOutput(*settings, input, output, record);
    EXPECT_TRUE(statisticalStretchEvidenceValid(scenario, record));
    EXPECT_EQ(record.counters["statisticalOracleSamples"], 10001.0);
    const float saved = output[1];
    for (float bad : {0.5f, -0.1f, std::numeric_limits<float>::quiet_NaN()})
    {
        output[1] = bad;
        checkStatisticalStretchOutput(*settings, input, output, record);
        EXPECT_FALSE(statisticalStretchEvidenceValid(scenario, record));
    }
    output[1] = saved;
    checkStatisticalStretchOutput(*settings, input, output, record);
    ASSERT_TRUE(statisticalStretchEvidenceValid(scenario, record));
    for (const auto& [name, value] : record.counters)
    {
        auto missing = record;
        missing.counters.erase(name);
        EXPECT_FALSE(statisticalStretchEvidenceValid(scenario, missing)) << name;
    }
    auto loosened = record;
    loosened.tolerance = 1.0;
    loosened.counters["statisticalOracleMaxAbsoluteError"] = 1.1e-6;
    EXPECT_FALSE(statisticalStretchEvidenceValid(scenario, loosened));
    EXPECT_FALSE(requiresStatisticalStretchOracle(stretchScenarios()[8]));
    EXPECT_FALSE(statisticalStretchEvidenceValid(stretchScenarios()[8], record));
}

TEST(StatisticalStretchOracleTest, AnOutputStretchedFromAnEstimatedMedianFailsTheTier)
{
    auto scenario = rampScenario();
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings);
    std::vector<float> input(257u);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i) / 256.0f;
    // A median read from a histogram of this ramp is 0.501953125 where the exact median is 0.5: half of one of its
    // 257 bins. A stretch from it is wrong by far more than the tier.
    const auto reference = measureStatisticalStretch(*settings, input);
    ASSERT_EQ(reference.median, 0.5);
    const double estimated = 0.501953125;
    StatisticalStretchOracle oracle(
        *settings, estimated,
        (std::min)((std::max)(reference.minimum, estimated - 5.0 * reference.lowerHalfSigma), 0.99));
    std::vector<float> output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = static_cast<float>(oracle.forward(input[i]));
    ResultRecord record;
    checkStatisticalStretchOutput(*settings, input, output, record);
    EXPECT_GT(record.counters.at("statisticalOracleMaxAbsoluteError"), 1.0e-3);
    EXPECT_FALSE(statisticalStretchEvidenceValid(scenario, record));
}

TEST(StatisticalStretchOracleTest, CountersAreTheSharedWalkUnderThisFamilysNames)
{
    auto scenario = rampScenario();
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    const std::vector<float> input{0.0f, 0.25f, 0.5f, 0.5f, 1.0f};
    const auto reference = measureStatisticalStretch(*settings, input);
    StatisticalStretchOracle oracle(*settings, reference.median, reference.blackPoint);
    std::vector<float> output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = static_cast<float>(oracle.forward(input[i]));
    output[3] = std::nextafter(output[2], 2.0f); // equal inputs with unequal outputs
    output[4] = 0.25f; // endpoint, and below the sample before it
    ResultRecord record;
    checkStatisticalStretchOutput(*settings, input, output, record);
    EXPECT_EQ(record.counters["statisticalOracleSamples"], 5.0);
    EXPECT_EQ(record.counters["statisticalEndpointViolations"], 1.0);
    EXPECT_EQ(record.counters["statisticalOrderingViolations"], 2.0);
    EXPECT_EQ(record.counters["statisticalRangeViolations"], 0.0);
    EXPECT_NEAR(record.counters["statisticalOracleMaxAbsoluteError"], 0.75, 1.0e-12);
    EXPECT_FALSE(record.counters.contains("statisticalRoundtripError")) << "the family has no inverse row";
    EXPECT_EQ(record.correctnessMetric, record.counters["statisticalOracleMaxAbsoluteError"]);
    EXPECT_EQ(record.counters["statisticalReferenceMedian"], 0.5);
    EXPECT_EQ(record.counters["statisticalNormalizationDivisor"], 1.0);
}

TEST(StatisticalStretchOracleTest, SkyFixtureHasABlackMarginColdPixelsStarsAndANonQuantizedBackground)
{
    SyntheticFixtureSource source;
    std::size_t rows = 0u;
    for (const auto& scenario : stretchScenarios())
    {
        if (scenario.corpusId != "synthetic.statistical_sky")
            continue;
        ++rows;
        Fixture fixture;
        std::string error;
        ASSERT_TRUE(source.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error)) << error;
        std::vector<float> values(fixture.bytes.size() / sizeof(float));
        std::memcpy(values.data(), fixture.bytes.data(), fixture.bytes.size());
        ASSERT_EQ(values.size(), 1024u * 1024u);
        std::size_t margin = 0u, cold = 0u, stars = 0u, nonQuantized = 0u;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            const std::size_t x = i % 1024u;
            const std::size_t y = i / 1024u;
            const bool inMargin = x < 16u || y < 16u || x >= 1008u || y >= 1008u;
            if (inMargin)
            {
                EXPECT_EQ(values[i], 0.0f);
                ++margin;
                continue;
            }
            cold += values[i] == 0.01f;
            stars += values[i] >= 0.3f;
            nonQuantized += values[i] != std::round(values[i] * 65535.0f) / 65535.0f;
        }
        EXPECT_EQ(margin, 1024u * 1024u - 992u * 992u);
        EXPECT_GT(cold, 100u);
        EXPECT_GT(stars, 200u);
        EXPECT_GT(nonQuantized, values.size() / 2u);
        const auto reference = measureStatisticalStretch(statisticalSettings(0.3f), values);
        EXPECT_EQ(reference.minimum, 0.0);
        EXPECT_NEAR(reference.median, 0.05, 1.0e-3);
        EXPECT_GT(reference.blackPoint, 0.03) << "the margin and cold pixels lie far below the black point";
    }
    EXPECT_EQ(rows, 3u);
}
