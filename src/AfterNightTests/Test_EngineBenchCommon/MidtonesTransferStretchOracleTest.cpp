// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent midtones transfer mathematics and full-output failure-path tests.
#include "MidtonesTransferStretchOracle.h"
#include "SyntheticFixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace an::bench;

namespace
{
StretchSettings transferSettings(float midtones, float shadows, float highlights)
{
    StretchSettings settings;
    settings.function = 8u;
    settings.midtones = midtones;
    settings.shadows = shadows;
    settings.highlights = highlights;
    return settings;
}

// Whether `inverse` is where the curve reaches `target`: within the tolerance, or with the curve
// crossing it within eight representable steps either side. A balance one millionth from either
// end rises up to a million times faster than the identity, so the few roundings of an inverse
// move the curve by more than any fixed tolerance.
::testing::AssertionResult reaches(const MidtonesTransferStretchOracle& oracle, long double inverse, long double target)
{
    constexpr long double tolerance = 1.0e-13L;
    if (std::abs(oracle.forward(inverse) - target) <= tolerance)
        return ::testing::AssertionSuccess();
    const long double step = std::nextafter(inverse, 2.0L) - inverse;
    const auto below = oracle.forward(inverse - 8.0L * step);
    const auto above = oracle.forward(inverse + 8.0L * step);
    if (below <= target + tolerance && above >= target - tolerance)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << "inverse " << static_cast<double>(inverse) << " maps to "
                                         << static_cast<double>(oracle.forward(inverse)) << ", not "
                                         << static_cast<double>(target);
}

ScenarioSpec rampScenario()
{
    auto scenario = stretchScenarios()[8];
    EXPECT_EQ(scenario.id, "stretch.function_8.f32");
    return scenario;
}
} // namespace

TEST(MidtonesTransferStretchOracleTest, KnownValuesFromTheDefinitionDoNotComeFromTheEngine)
{
    // Float32 parameters and inputs, each piece and each balance class, evaluated from the
    // definition (m - 1) x / ((2m - 1) x - m) in exact rational arithmetic, independently of both
    // engines and of this oracle's rearranged fraction. The inputs are declared Float32: as an 80-bit
    // long double their 18-digit spellings are not the Float32 values the outputs were computed at,
    // and a balance one millionth from one magnifies that difference a million times.
    struct Known
    {
        float midtones;
        float shadows;
        float highlights;
        float input;
        long double output;
    };
    constexpr std::array<Known, 23> known{{
        {0.25f, 0.0f, 1.0f, 1.25000000000000000e-01f, 0.3L},
        {0.25f, 0.0f, 1.0f, 2.50000000000000000e-01f, 0.5L},
        {0.25f, 0.0f, 1.0f, 5.00000000000000000e-01f, 0.75L},
        {0.25f, 0.0f, 1.0f, 7.50000000000000000e-01f, 0.9L},
        {0.03f, 0.02f, 1.0f, 2.50000003725290298e-02f, 0.1422287618581031296175445629528049869792L},
        {0.03f, 0.02f, 1.0f, 5.00000007450580597e-02f, 0.5052083492225424830444877615373235477338L},
        {0.03f, 0.02f, 1.0f, 5.00000000000000000e-01f, 0.9687890144577431976708226500134449921926L},
        {0.03f, 0.02f, 1.0f, 9.90000009536743164e-01f, 0.9996812582830520411542135454708138304866L},
        {0.25f, 0.125f, 0.75f, 1.00000001490116119e-01f, 0.0L},
        {0.25f, 0.125f, 0.75f, 2.50000000000000000e-01f, 0.4285714285714285714285714285714285714286L},
        {0.25f, 0.125f, 0.75f, 5.00000000000000000e-01f, 0.8181818181818181818181818181818181818182L},
        {0.25f, 0.125f, 0.75f, 8.00000011920928955e-01f, 1.0L},
        {0.999999f, 0.0f, 1.0f, 5.00000000000000000e-01f, 0.000001013278961181640625L},
        {0.999999f, 0.0f, 1.0f, 9.99998986721038818e-01f, 0.5L},
        {0.999999f, 0.0f, 1.0f, 9.99999940395355225e-01f, 0.9444444944829172969499829620103727060976L},
        {0.000001f, 0.0f, 1.0f, 9.99999997475242708e-07f, 0.5L},
        {0.000001f, 0.0f, 1.0f, 4.99999998737621354e-07f, 0.3333332222221484286277274511843824613901L},
        {0.000001f, 0.0f, 1.0f, 1.00000004749745131e-03f, 0.9990019960579039759767008596865587058160L},
        {0.5f, 0.2f, 0.6f, 3.00000011920928955e-01f, 0.2500000093132252604322372151489518132607L},
        {0.5f, 0.2f, 0.6f, 4.00000005960464478e-01f, 0.4999999813735494791355255697020963734785L},
        {0.0f, 0.0f, 1.0f, 3.00000011920928955e-01f, 1.0L},
        {1.0f, 0.0f, 1.0f, 3.00000011920928955e-01f, 0.0L},
        {1.0f, 0.0f, 1.0f, 9.99999940395355225e-01f, 0.0L},
    }};
    for (const Known& value : known)
    {
        const MidtonesTransferStretchOracle oracle(transferSettings(value.midtones, value.shadows, value.highlights));
        const long double actual = oracle.forward(value.input);
        EXPECT_NEAR(static_cast<double>(actual), static_cast<double>(value.output), 4.0e-16)
            << value.midtones << ' ' << value.shadows << ' ' << value.highlights << " at "
            << static_cast<double>(value.input);
        EXPECT_FALSE(std::signbit(actual)) << "a zero result is positive zero";
    }
}

TEST(MidtonesTransferStretchOracleTest, EndpointsAreTakenShadowsFirstAndEveryBalanceStaysInTheUnitInterval)
{
    // Equal endpoints are a step, with the endpoint itself below it.
    const MidtonesTransferStretchOracle step(transferSettings(0.25f, 0.375f, 0.375f));
    EXPECT_EQ(step.forward(0.375L), 0.0L);
    EXPECT_EQ(step.forward(std::nextafter(0.375f, 1.0f)), 1.0L);
    EXPECT_EQ(step.forward(-1.0L), 0.0L);
    EXPECT_EQ(step.forward(2.0L), 1.0L);
    const MidtonesTransferStretchOracle black(transferSettings(0.25f, 0.0f, 0.0f));
    EXPECT_EQ(black.forward(0.0L), 0.0L);
    EXPECT_EQ(black.forward(std::numeric_limits<float>::denorm_min()), 1.0L);
    // A balance of one half is the identity between the endpoints.
    const MidtonesTransferStretchOracle identity(transferSettings(0.5f, 0.0f, 1.0f));
    for (int i = 0; i <= 4096; ++i)
        EXPECT_NEAR(static_cast<double>(identity.forward(i / 4096.0L)), i / 4096.0, 2.0e-16) << i;
    // Every balance, including both ends and one millionth from them, rises monotonically from zero
    // to one without leaving the interval.
    for (const float midtones : {0.0f, 0.000001f, 0.03f, 0.25f, 0.5f, 0.75f, 0.999999f, 1.0f})
        for (const auto& [shadows, highlights] :
             std::vector<std::pair<float, float>>{{0.0f, 1.0f}, {0.125f, 0.75f}, {0.5f, 0.50000095367431640625f}})
        {
            const MidtonesTransferStretchOracle oracle(transferSettings(midtones, shadows, highlights));
            long double previous = 0.0L;
            for (int i = 0; i <= 65536; ++i)
            {
                const long double value = oracle.forward(i / 65536.0L);
                EXPECT_TRUE(value >= 0.0L && value <= 1.0L && value >= previous && !std::signbit(value))
                    << midtones << ' ' << shadows << ' ' << highlights << " at " << i;
                previous = value;
            }
            EXPECT_EQ(oracle.forward(shadows), 0.0L);
            EXPECT_EQ(oracle.forward(highlights), 1.0L);
        }
}

TEST(MidtonesTransferStretchOracleTest, InverseIsTheLeastInputReachingEachValue)
{
    for (const float midtones : {0.000001f, 0.03f, 0.25f, 0.5f, 0.999999f})
        for (const auto& [shadows, highlights] :
             std::vector<std::pair<float, float>>{{0.0f, 1.0f}, {0.02f, 1.0f}, {0.125f, 0.75f}})
        {
            const MidtonesTransferStretchOracle oracle(transferSettings(midtones, shadows, highlights));
            EXPECT_EQ(oracle.inverse(0.0L), static_cast<long double>(shadows));
            EXPECT_EQ(oracle.inverse(-1.0L), static_cast<long double>(shadows));
            EXPECT_EQ(oracle.inverse(1.0L), static_cast<long double>(highlights));
            for (int i = 1; i < 1024; ++i)
            {
                const long double target = i / 1024.0L;
                EXPECT_TRUE(reaches(oracle, oracle.inverse(target), target))
                    << midtones << ' ' << shadows << ' ' << highlights << " at " << i;
            }
        }
    // A step's generalized inverse is its step point whatever the value, and so is a balance at
    // either end, which is a step at one endpoint.
    EXPECT_EQ(MidtonesTransferStretchOracle(transferSettings(0.25f, 0.375f, 0.375f)).inverse(0.5L), 0.375L);
    EXPECT_EQ(MidtonesTransferStretchOracle(transferSettings(0.0f, 0.125f, 0.75f)).inverse(0.5L), 0.125L);
    EXPECT_EQ(MidtonesTransferStretchOracle(transferSettings(1.0f, 0.125f, 0.75f)).inverse(0.5L), 0.75L);
}

TEST(MidtonesTransferStretchOracleTest, AnUnprobedPixelAndNonfiniteOutputFailCompleteEvidence)
{
    auto scenario = rampScenario();
    scenario.input.width = 10001u;
    scenario.elementsProcessed = 10001u;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    ASSERT_TRUE(requiresMidtonesTransferStretchOracle(scenario));
    const MidtonesTransferStretchOracle oracle(*settings);
    std::vector<float> input(10001u), output(10001u);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        input[i] = static_cast<float>(i) / 10000.0f;
        output[i] = static_cast<float>(oracle.forward(input[i]));
    }
    ResultRecord record;
    checkMidtonesTransferStretchOutput(*settings, input, output, record);
    EXPECT_TRUE(midtonesTransferStretchEvidenceValid(scenario, record));
    EXPECT_EQ(record.counters["mtfOracleSamples"], 10001.0);
    EXPECT_EQ(record.counters["mtfOracleVersion"], 1.0);
    const float saved = output[1]; // The old 4097-probe diagnostic does not visit this sample.
    for (float bad : {0.5f, -0.1f, std::numeric_limits<float>::quiet_NaN()})
    {
        output[1] = bad;
        checkMidtonesTransferStretchOutput(*settings, input, output, record);
        EXPECT_FALSE(midtonesTransferStretchEvidenceValid(scenario, record));
    }
    output[1] = saved;
    checkMidtonesTransferStretchOutput(*settings, input, output, record);
    for (const auto& [name, value] : record.counters)
    {
        auto missing = record;
        missing.counters.erase(name);
        EXPECT_FALSE(midtonesTransferStretchEvidenceValid(scenario, missing)) << name;
    }
    auto loosened = record;
    loosened.tolerance = 1.0;
    loosened.counters["mtfOracleMaxAbsoluteError"] = 1.1e-6;
    EXPECT_FALSE(midtonesTransferStretchEvidenceValid(scenario, loosened));
    // Another family's scenario never borrows this evidence.
    EXPECT_FALSE(requiresMidtonesTransferStretchOracle(stretchScenarios()[3]));
    EXPECT_FALSE(midtonesTransferStretchEvidenceValid(stretchScenarios()[3], record));
}

TEST(MidtonesTransferStretchOracleTest, CountersAreTheSharedWalkUnderThisFamilysNames)
{
    auto scenario = rampScenario();
    scenario.input.width = 5u;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    const MidtonesTransferStretchOracle oracle(*settings);
    const std::array<float, 5> input{0.0f, 0.25f, 0.5f, 0.5f, 1.0f};
    std::array<float, 5> output{};
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = static_cast<float>(oracle.forward(input[i]));
    output[3] = std::nextafter(output[2], 2.0f); // equal inputs with unequal outputs
    output[4] = 0.5f; // endpoint, and below the sample before it
    ResultRecord record;
    checkMidtonesTransferStretchOutput(*settings, input, output, record);
    EXPECT_EQ(record.counters["mtfOracleSamples"], 5.0);
    EXPECT_EQ(record.counters["mtfEndpointViolations"], 1.0);
    EXPECT_EQ(record.counters["mtfOrderingViolations"], 2.0);
    EXPECT_EQ(record.counters["mtfRangeViolations"], 0.0);
    EXPECT_NEAR(record.counters["mtfOracleMaxAbsoluteError"], 0.5, 1.0e-12);
    EXPECT_FALSE(record.counters.contains("mtfRoundtripError")) << "the family has no inverse row";
    EXPECT_EQ(record.correctnessMetric, record.counters["mtfOracleMaxAbsoluteError"]);
}

TEST(MidtonesTransferStretchOracleTest, AnOutputJustOutsideTheIntervalFailsTheClamp)
{
    auto scenario = rampScenario();
    scenario.input.width = 2;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings);
    const std::array<float, 2> input{0.0f, 1.0f};
    std::array<float, 2> output{-5.587935092421503e-8f, 1.0000001192092896f};
    ResultRecord record;
    checkMidtonesTransferStretchOutput(*settings, input, output, record);
    EXPECT_FALSE(midtonesTransferStretchEvidenceValid(scenario, record));
    EXPECT_DOUBLE_EQ(record.counters["mtfRangeExcursion"], 1.1920928955078125e-7);
}

TEST(MidtonesTransferStretchOracleTest, BoundaryRowsCoverEveryBalanceClassAndEndpointLimit)
{
    std::set<std::tuple<std::string, std::string, std::string>> seen;
    std::set<std::string> ids;
    for (const auto& scenario : stretchScenarios())
    {
        if (!requiresMidtonesTransferStretchOracle(scenario) || scenario.corpusId != "synthetic.mtf_boundaries")
            continue;
        std::string error;
        ASSERT_TRUE(parseStretchSettings(scenario, error)) << error;
        EXPECT_TRUE(ids.insert(scenario.id).second);
        EXPECT_EQ(scenario.input.width, 4097u);
        const auto& p = scenario.parameters;
        seen.insert({p.at("midtones"), p.at("shadows"), p.at("highlights")});
    }
    EXPECT_EQ(ids.size(), 13u);
    for (const char* midtones : {"0", "0.000001", "0.5", "0.999999", "1"})
        EXPECT_TRUE(seen.contains({midtones, "0", "1"})) << midtones;
    for (const char* midtones : {"0.000001", "0.25", "0.999999"})
        EXPECT_TRUE(seen.contains({midtones, "0.125", "0.75"})) << midtones;
    for (const auto& endpoints :
         std::vector<std::tuple<std::string, std::string, std::string>>{{"0.03", "0.02", "1"},
                                                                        {"0.25", "0", "0.75"},
                                                                        {"0.25", "0.5", "0.50000095367431640625"},
                                                                        {"0.25", "0.375", "0.375"},
                                                                        {"0.25", "0", "0"}})
        EXPECT_TRUE(seen.contains(endpoints)) << std::get<1>(endpoints) << ' ' << std::get<2>(endpoints);
}

TEST(MidtonesTransferStretchOracleTest, RegisteredBoundaryFixtureContainsEachLandmarkAndItsFloatNeighbors)
{
    SyntheticFixtureSource source;
    std::size_t checked = 0u;
    for (const auto& scenario : stretchScenarios())
    {
        if (scenario.corpusId != "synthetic.mtf_boundaries")
            continue;
        std::string error;
        const auto settings = parseStretchSettings(scenario, error);
        ASSERT_TRUE(settings) << error;
        const MidtonesTransferStretchOracle oracle(*settings);
        Fixture fixture;
        ASSERT_TRUE(source.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error)) << error;
        std::vector<float> values(fixture.bytes.size() / sizeof(float));
        std::memcpy(values.data(), fixture.bytes.data(), fixture.bytes.size());
        std::vector<float> landmarks{0.0f, settings->shadows, settings->highlights, 1.0f};
        if (settings->shadows < settings->highlights && settings->midtones > 0.0f && settings->midtones < 1.0f)
            for (const long double level : {0.25L, 0.5L, 0.75L})
                landmarks.push_back(static_cast<float>(oracle.inverse(level)));
        for (const float center : landmarks)
            for (const float value : {std::nextafter(center, 0.0f), center, std::nextafter(center, 1.0f)})
            {
                // The fixture's images were rounded from exact values; this oracle's may differ by
                // one Float32 step.
                const auto found = std::find_if(values.begin(), values.end(), [&](float actual) {
                    return std::abs(actual - value) <= std::abs(std::nextafter(value, 1.0f) - value);
                });
                EXPECT_NE(found, values.end()) << scenario.id << " landmark " << value;
            }
        ++checked;
    }
    EXPECT_EQ(checked, 13u);
}

TEST(MidtonesTransferStretchOracleTest, NonQuantizedControlsCannotBeAdmittedAsUInt16Codes)
{
    SyntheticFixtureSource source;
    std::size_t controls = 0u;
    for (const auto& scenario : stretchScenarios())
    {
        if (!requiresMidtonesTransferStretchOracle(scenario) || scenario.corpusId != "synthetic.uniform_noise")
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
        ++controls;
    }
    EXPECT_EQ(controls, 2u);
}
