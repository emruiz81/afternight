// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent histogram mathematics and full-output failure-path tests.
#include "HistogramStretchOracle.h"
#include "StretchOutputCheck.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace an::bench;

TEST(HistogramStretchOracleTest, KnownRationalValuesAndIdentityDoNotComeFromTheEngine)
{
    StretchSettings settings;
    settings.amount = 2.0f;
    settings.symmetry = 0.5f;
    const HistogramStretchOracle oracle(settings);
    // Primitive endpoints are +/-1/6 and primitive(1/4) is -1/8.
    EXPECT_NEAR(static_cast<double>(oracle.forward(0.25L)), 0.125, 1.0e-15);
    EXPECT_NEAR(static_cast<double>(oracle.inverse(0.125L)), 0.25, 1.0e-15);
    EXPECT_EQ(oracle.forward(-1.0L), 0.0L);
    EXPECT_EQ(oracle.inverse(2.0L), 1.0L);
    settings.amount = 0.0f;
    const HistogramStretchOracle identity(settings);
    EXPECT_EQ(identity.forward(0.25L), 0.25L);
    EXPECT_EQ(identity.inverse(0.25L), 0.25L);
}

TEST(HistogramStretchOracleTest, TangentPiecesAndSymmetryHaveContinuousMonotoneInverses)
{
    for (float amount : {0.0f, 0.1f, 2.0f, 3.0f, 20.0f})
        for (float symmetry : {0.0f, 0.05f, 0.25f, 0.5f, 1.0f})
        {
            StretchSettings settings;
            settings.amount = amount;
            settings.symmetry = symmetry;
            settings.lowProtection = symmetry / 2.0f;
            settings.highProtection = (1.0f + symmetry) / 2.0f;
            const HistogramStretchOracle oracle(settings);
            long double previous = -1.0L;
            for (int i = 0; i <= 4096; ++i)
            {
                const auto x = static_cast<long double>(i) / 4096.0L;
                const auto y = oracle.forward(x);
                EXPECT_GE(y, previous);
                EXPECT_NEAR(static_cast<double>(oracle.inverse(y)), static_cast<double>(x), 1.0e-12);
                previous = y;
            }
        }
}

TEST(HistogramStretchOracleTest, AnUnprobedPixelAndNonfiniteOutputFailCompleteEvidence)
{
    auto scenario = stretchScenarios()[6];
    scenario.input.width = 10001u;
    scenario.elementsProcessed = 10001u;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    const HistogramStretchOracle oracle(*settings);
    std::vector<float> input(10001u), output(10001u);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        input[i] = static_cast<float>(i) / 10000.0f;
        output[i] = static_cast<float>(oracle.inverse(input[i]));
    }
    ResultRecord record;
    checkHistogramStretchOutput(*settings, input, output, record);
    EXPECT_TRUE(histogramStretchEvidenceValid(scenario, record));
    EXPECT_EQ(record.counters["histogramOracleSamples"], 10001.0);
    const float saved = output[1]; // The old 4097-probe diagnostic does not visit this sample.
    for (float bad : {0.5f, -0.1f, std::numeric_limits<float>::quiet_NaN()})
    {
        output[1] = bad;
        checkHistogramStretchOutput(*settings, input, output, record);
        EXPECT_FALSE(histogramStretchEvidenceValid(scenario, record));
    }
    output[1] = saved;
    checkHistogramStretchOutput(*settings, input, output, record);
    for (const auto& [name, value] : record.counters)
    {
        auto missing = record;
        missing.counters.erase(name);
        EXPECT_FALSE(histogramStretchEvidenceValid(scenario, missing)) << name;
    }
    auto loosened = record;
    loosened.tolerance = 1.0;
    loosened.counters["histogramOracleMaxAbsoluteError"] = 1.1e-6;
    EXPECT_FALSE(histogramStretchEvidenceValid(scenario, loosened));
}

TEST(HistogramStretchOracleTest, ChunkedCompleteOutputWalkEqualsTheSerialWalkAcrossChunkBoundaries)
{
    // The forward direction, which skips the round trip, across four fixed chunks and a partial fifth.
    const std::size_t chunk = kStretchCheckChunkSamples;
    const std::size_t count = 4u * chunk + 31u;
    auto scenario = stretchScenarios()[6];
    scenario.parameters["function"] = "5";
    scenario.input.width = static_cast<std::uint32_t>(count);
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    const HistogramStretchOracle oracle(*settings);
    std::vector<float> input(count), output(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        input[i] = static_cast<float>(static_cast<double>(i) / static_cast<double>(count - 1u));
        output[i] = static_cast<float>(oracle.forward(input[i]));
    }
    output[chunk] = output[chunk - 1u] - 1.0e-5f;
    input[2u * chunk] = input[2u * chunk - 1u];
    output[2u * chunk] = std::nextafter(output[2u * chunk - 1u], 2.0f);
    output[3u * chunk + 5u] = std::numeric_limits<float>::quiet_NaN();
    // The serial definition, written independently of the chunked walk.
    long double maximum = 0.0L, squares = 0.0L;
    double invalid = 0.0, ordering = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!std::isfinite(output[i]))
        {
            invalid += 1.0;
            continue;
        }
        const long double deviation = std::abs(static_cast<long double>(output[i]) - oracle.forward(input[i]));
        maximum = (std::max)(maximum, deviation);
        squares += deviation * deviation;
        ordering += i != 0u && ((input[i] > input[i - 1] && output[i] + 2.0e-6f < output[i - 1]) ||
                                (input[i] == input[i - 1] && output[i] != output[i - 1]))
                        ? 1.0
                        : 0.0;
    }
    ResultRecord record;
    checkHistogramStretchOutput(*settings, input, output, record);
    EXPECT_EQ(record.counters["histogramOracleSamples"], static_cast<double>(count));
    EXPECT_EQ(record.counters["histogramOracleMaxAbsoluteError"], static_cast<double>(maximum));
    EXPECT_NEAR(record.counters["histogramOracleRmsError"], static_cast<double>(std::sqrt(squares / count)),
                static_cast<double>(std::sqrt(squares / count)) * 1.0e-12);
    EXPECT_EQ(record.counters["histogramRangeViolations"], invalid);
    EXPECT_EQ(record.counters["histogramOrderingViolations"], ordering);
    EXPECT_EQ(ordering, 2.0);
    EXPECT_EQ(record.counters["histogramRoundtripError"], 0.0);
}

TEST(HistogramStretchOracleTest, AnOutputJustOutsideTheIntervalFailsTheClamp)
{
    auto scenario = stretchScenarios()[6];
    scenario.input.width = 2;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings);
    const std::array<float, 2> input{0.0f, 1.0f};
    std::array<float, 2> output{5.587935092421503e-8f, 1.0000001192092896f};
    ResultRecord record;
    checkHistogramStretchOutput(*settings, input, output, record);
    EXPECT_FALSE(histogramStretchEvidenceValid(scenario, record));
    EXPECT_DOUBLE_EQ(record.counters["histogramRangeExcursion"], 1.1920928955078125e-7);
    EXPECT_DOUBLE_EQ(record.counters["histogramOracleMaxAbsoluteError"], 1.1920928955078125e-7);
}
