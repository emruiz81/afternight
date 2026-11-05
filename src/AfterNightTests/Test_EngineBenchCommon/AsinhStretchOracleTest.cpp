// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent asinh mathematics and full-output failure-path tests.
#include "AsinhStretchOracle.h"
#include "StretchOutputCheck.h"
#include "SyntheticFixtures.h"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace an::bench;

TEST(AsinhStretchOracleTest, KnownHyperbolicValuesAndIdentityDoNotComeFromTheEngine)
{
    StretchSettings settings;
    settings.amount = 2.0f;
    settings.symmetry = 0.5f;
    const AsinhStretchOracle oracle(settings);
    // 1/2 - asinh(1/2) / (2 asinh(1)), independently evaluated at 80 decimal digits.
    EXPECT_NEAR(static_cast<double>(oracle.forward(0.25L)),
                0.22701029838727549805503816256977246190728745618880971958018697195290470621328772, 1.0e-15);
    EXPECT_NEAR(static_cast<double>(oracle.inverse(
                    0.22701029838727549805503816256977246190728745618880971958018697195290470621328772L)),
                0.25, 1.0e-15);
    EXPECT_EQ(oracle.forward(-1.0L), 0.0L);
    EXPECT_EQ(oracle.inverse(2.0L), 1.0L);
    settings.amount = 0.0f;
    const AsinhStretchOracle identity(settings);
    EXPECT_EQ(identity.forward(0.25L), 0.25L);
    EXPECT_EQ(identity.inverse(0.25L), 0.25L);
}

TEST(AsinhStretchOracleTest, TangentPiecesAndSymmetryHaveContinuousMonotoneInverses)
{
    for (float amount : {0.0f, 1.0e-20f, 1.0e-6f, 0.1f, 2.0f, 3.0f, 100.0f, 1.0e20f, std::numeric_limits<float>::max()})
        for (float symmetry : {0.0f, 0.05f, 0.25f, 0.5f, 1.0f})
        {
            StretchSettings settings;
            settings.amount = amount;
            settings.symmetry = symmetry;
            settings.lowProtection = symmetry / 2.0f;
            settings.highProtection = (1.0f + symmetry) / 2.0f;
            const AsinhStretchOracle oracle(settings);
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

TEST(AsinhStretchOracleTest, AnUnprobedPixelAndNonfiniteOutputFailCompleteEvidence)
{
    auto scenario = stretchScenarios()[3];
    scenario.input.width = 10001u;
    scenario.elementsProcessed = 10001u;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    const AsinhStretchOracle oracle(*settings);
    std::vector<float> input(10001u), output(10001u);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        input[i] = static_cast<float>(i) / 10000.0f;
        output[i] = static_cast<float>(oracle.inverse(input[i]));
    }
    ResultRecord record;
    checkAsinhStretchOutput(*settings, input, output, record);
    EXPECT_TRUE(asinhStretchEvidenceValid(scenario, record));
    EXPECT_EQ(record.counters["asinhOracleSamples"], 10001.0);
    const float saved = output[1]; // The old 4097-probe diagnostic does not visit this sample.
    for (float bad : {0.5f, -0.1f, std::numeric_limits<float>::quiet_NaN()})
    {
        output[1] = bad;
        checkAsinhStretchOutput(*settings, input, output, record);
        EXPECT_FALSE(asinhStretchEvidenceValid(scenario, record));
    }
    output[1] = saved;
    checkAsinhStretchOutput(*settings, input, output, record);
    for (const auto& [name, value] : record.counters)
    {
        auto missing = record;
        missing.counters.erase(name);
        EXPECT_FALSE(asinhStretchEvidenceValid(scenario, missing)) << name;
    }
    auto loosened = record;
    loosened.tolerance = 1.0;
    loosened.counters["asinhOracleMaxAbsoluteError"] = 1.1e-6;
    EXPECT_FALSE(asinhStretchEvidenceValid(scenario, loosened));
}

namespace
{
// The complete-output walk written as one serial loop, independently of the chunked checker.
std::map<std::string, double> serialAsinhCounters(const StretchSettings& settings, const std::vector<float>& input,
                                                  const std::vector<float>& output)
{
    const AsinhStretchOracle oracle(settings);
    long double maximum = 0.0L, squares = 0.0L, roundtrip = 0.0L;
    double excursionMaximum = 0.0, invalid = 0.0, endpoints = 0.0, ordering = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (!std::isfinite(input[i]) || !std::isfinite(output[i]))
        {
            invalid += 1.0;
            continue;
        }
        const double excursion = (std::max)({0.0, -static_cast<double>(output[i]), output[i] - 1.0});
        excursionMaximum = (std::max)(excursionMaximum, excursion);
        invalid += excursion > 1.0e-6 ? 1.0 : 0.0;
        const long double error = std::abs(static_cast<long double>(output[i]) - oracle.inverse(input[i]));
        maximum = (std::max)(maximum, error);
        squares += error * error;
        roundtrip = (std::max)(roundtrip, std::abs(oracle.forward(output[i]) - std::clamp(input[i], 0.0f, 1.0f)));
        endpoints += (input[i] <= 0.0f && std::abs(output[i]) > 1.0e-6f) ||
                             (input[i] >= 1.0f && std::abs(output[i] - 1.0f) > 1.0e-6f)
                         ? 1.0
                         : 0.0;
        ordering += i != 0u && ((input[i] > input[i - 1] && output[i] + 2.0e-6f < output[i - 1]) ||
                                (input[i] < input[i - 1] && output[i] > output[i - 1] + 2.0e-6f) ||
                                (input[i] == input[i - 1] && output[i] != output[i - 1]))
                        ? 1.0
                        : 0.0;
    }
    return {{"asinhOracleMaxAbsoluteError", static_cast<double>(maximum)},
            {"asinhOracleRmsError", static_cast<double>(std::sqrt(squares / input.size()))},
            {"asinhRangeViolations", invalid},
            {"asinhRangeExcursion", excursionMaximum},
            {"asinhEndpointViolations", endpoints},
            {"asinhOrderingViolations", ordering},
            {"asinhRoundtripError", static_cast<double>(roundtrip)}};
}
} // namespace

TEST(AsinhStretchOracleTest, ChunkedCompleteOutputWalkEqualsTheSerialWalkAcrossChunkBoundaries)
{
    // Four fixed chunks and a partial fifth, with a defect planted at each chunk seam.
    const std::size_t chunk = kStretchCheckChunkSamples;
    const std::size_t count = 4u * chunk + 17u;
    auto scenario = stretchScenarios()[3];
    scenario.input.width = static_cast<std::uint32_t>(count);
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    const AsinhStretchOracle oracle(*settings);
    std::vector<float> input(count), output(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        input[i] = static_cast<float>(static_cast<double>(i) / static_cast<double>(count - 1u));
        output[i] = static_cast<float>(oracle.inverse(input[i]));
    }
    output[chunk] = output[chunk - 1u] - 1.0e-5f; // descends across the first seam
    input[2u * chunk] = input[2u * chunk - 1u]; // equal inputs straddling the second seam
    output[2u * chunk] = std::nextafter(output[2u * chunk - 1u], 2.0f);
    output[3u * chunk - 1u] = 1.5f; // out of range at the end of the third chunk
    output[3u * chunk + 5u] = std::numeric_limits<float>::quiet_NaN();
    input[count - 1u] = 1.0f;
    output[count - 1u] = 0.5f; // endpoint in the partial chunk
    ResultRecord first;
    checkAsinhStretchOutput(*settings, input, output, first);
    const auto serial = serialAsinhCounters(*settings, input, output);
    for (const auto& [name, value] : serial)
    {
        if (name == "asinhOracleRmsError")
            EXPECT_NEAR(first.counters[name], value, value * 1.0e-12) << name;
        else
            EXPECT_EQ(first.counters[name], value) << name;
    }
    EXPECT_EQ(first.counters["asinhOracleSamples"], static_cast<double>(count));
    EXPECT_EQ(first.counters["asinhOrderingViolations"], 4.0);
    EXPECT_EQ(first.counters["asinhEndpointViolations"], 1.0);
    ResultRecord second;
    checkAsinhStretchOutput(*settings, input, output, second);
    EXPECT_EQ(first.counters, second.counters) << "the walk must not depend on scheduling";
}

TEST(AsinhStretchOracleTest, AnOutputJustOutsideTheIntervalFailsTheClamp)
{
    auto scenario = stretchScenarios()[3];
    scenario.input.width = 2;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings);
    const std::array<float, 2> input{0.0f, 1.0f};
    std::array<float, 2> output{5.587935092421503e-8f, 1.0000001192092896f};
    ResultRecord record;
    checkAsinhStretchOutput(*settings, input, output, record);
    EXPECT_FALSE(asinhStretchEvidenceValid(scenario, record));
    EXPECT_DOUBLE_EQ(record.counters["asinhRangeExcursion"], 1.1920928955078125e-7);
    EXPECT_DOUBLE_EQ(record.counters["asinhOracleMaxAbsoluteError"], 1.1920928955078125e-7);
}

TEST(AsinhStretchOracleTest, RegisteredBoundaryFixtureContainsBothDomainsAndTheirFloatNeighbors)
{
    SyntheticFixtureSource source;
    for (const auto& scenario : stretchScenarios())
    {
        if (scenario.corpusId != "synthetic.asinh_boundaries")
            continue;
        std::string error;
        const auto settings = parseStretchSettings(scenario, error);
        ASSERT_TRUE(settings) << error;
        const AsinhStretchOracle oracle(*settings);
        Fixture fixture;
        ASSERT_TRUE(source.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error)) << error;
        std::vector<float> values(fixture.bytes.size() / sizeof(float));
        std::memcpy(values.data(), fixture.bytes.data(), fixture.bytes.size());
        for (auto x : {0.0f, settings->lowProtection, settings->symmetry, settings->highProtection, 1.0f})
            for (auto center : {x, static_cast<float>(oracle.forward(x))})
                for (auto value : {std::nextafter(center, 0.0f), center, std::nextafter(center, 1.0f)})
                {
                    // At zero/tiny amount, normalization can differ below one Float32 ULP.
                    const auto found = std::find_if(values.begin(), values.end(), [&](float actual) {
                        return std::abs(actual - value) <= std::abs(std::nextafter(value, 1.0f) - value);
                    });
                    EXPECT_NE(found, values.end()) << scenario.id << " boundary " << value;
                }
    }
}

TEST(AsinhStretchOracleTest, NonQuantizedControlsCannotBeAdmittedAsUInt16Codes)
{
    SyntheticFixtureSource source;
    for (const auto& scenario : stretchScenarios())
    {
        if (!requiresAsinhStretchOracle(scenario) || scenario.corpusId != "synthetic.uniform_noise")
            continue;
        Fixture fixture;
        std::string error;
        ASSERT_TRUE(source.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error));
        std::size_t nonQuantized = 0;
        for (std::size_t i = 0; i < fixture.bytes.size(); i += sizeof(float))
        {
            float value;
            std::memcpy(&value, fixture.bytes.data() + i, sizeof(float));
            const auto code = std::round(value * 65535.0f);
            nonQuantized += value != code / 65535.0f;
        }
        EXPECT_GT(nonQuantized, scenario.elementsProcessed / 2);
    }
}
