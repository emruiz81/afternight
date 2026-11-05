// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent generalized-hyperbolic mathematics and full-output failure-path tests.
#include "GeneralizedHyperbolicStretchOracle.h"
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
// Whether `inverse` is where the curve reaches `target`: within the tolerance, or with the curve
// crossing it between the two neighbouring long doubles. A near-step curve at an extreme amount
// crosses most of the unit interval inside one representable step of its symmetry point.
::testing::AssertionResult reaches(const GeneralizedHyperbolicStretchOracle& oracle, long double inverse,
                                   long double target)
{
    constexpr long double tolerance = 1.0e-13L;
    if (std::abs(oracle.forward(inverse) - target) <= tolerance)
        return ::testing::AssertionSuccess();
    const auto below = oracle.forward(std::nextafter(inverse, -1.0L));
    const auto above = oracle.forward(std::nextafter(inverse, 2.0L));
    if (below <= target + tolerance && above >= target - tolerance)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << "inverse " << static_cast<double>(inverse) << " maps to "
                                         << static_cast<double>(oracle.forward(inverse)) << ", not "
                                         << static_cast<double>(target);
}

StretchSettings curveSettings(float amount, float bias, float symmetry, float low, float high)
{
    StretchSettings settings;
    settings.function = 0u;
    settings.amount = amount;
    settings.bias = bias;
    settings.symmetry = symmetry;
    settings.lowProtection = low;
    settings.highProtection = high;
    return settings;
}
} // namespace

TEST(GeneralizedHyperbolicStretchOracleTest, KnownFamilyValuesAtEveryBiasDoNotComeFromTheEngine)
{
    // Amount 2, symmetry 1/2 and protection at 1/4 and 3/4, so each sample lies on a different
    // piece: the low tangent, both sides of the symmetry point and the high tangent. Evaluated
    // from the family's definition in 90-digit decimal arithmetic, independently of the engine.
    struct Known
    {
        float bias;
        long double input;
        long double output;
    };
    constexpr std::array<Known, 20> known{{
        {-1.0f, 0.125L, 0.1127957622254027766877977649423713654416L},
        {-1.0f, 0.375L, 0.3489821182629825986258473299225299355348L},
        {-1.0f, 0.625L, 0.6510178817370174013741526700774700644652L},
        {-1.0f, 0.875L, 0.8872042377745972233122022350576286345584L},
        {-0.5f, 0.125L, 0.1111111111111111111111111111111111111111L},
        {-0.5f, 0.375L, 0.3456790123456790123456790123456790123457L},
        {-0.5f, 0.625L, 0.6543209876543209876543209876543209876543L},
        {-0.5f, 0.875L, 0.8888888888888888888888888888888888888889L},
        {0.0f, 0.125L, 0.1088166495983959631655432429888300040208L},
        {0.0f, 0.375L, 0.3412600761757854081611357676222076182740L},
        {0.0f, 0.625L, 0.6587399238242145918388642323777923817260L},
        {0.0f, 0.875L, 0.8911833504016040368344567570111699959792L},
        {0.5f, 0.125L, 0.1038961038961038961038961038961038961039L},
        {0.5f, 0.375L, 0.3296456629789963123296456629789963123296L},
        {0.5f, 0.625L, 0.6703543370210036876703543370210036876704L},
        {0.5f, 0.875L, 0.8961038961038961038961038961038961038961L},
        {1.0f, 0.125L, 0.1L},
        {1.0f, 0.375L, 0.32L},
        {1.0f, 0.625L, 0.68L},
        {1.0f, 0.875L, 0.9L},
    }};
    for (const Known& value : known)
    {
        const GeneralizedHyperbolicStretchOracle oracle(curveSettings(2.0f, value.bias, 0.5f, 0.25f, 0.75f));
        EXPECT_NEAR(static_cast<double>(oracle.forward(value.input)), static_cast<double>(value.output), 1.0e-15)
            << "bias " << value.bias << " at " << static_cast<double>(value.input);
        EXPECT_NEAR(static_cast<double>(oracle.inverse(value.output)), static_cast<double>(value.input), 1.0e-14)
            << "bias " << value.bias << " at " << static_cast<double>(value.output);
    }
    const GeneralizedHyperbolicStretchOracle oracle(curveSettings(2.0f, -0.5f, 0.5f, 0.25f, 0.75f));
    EXPECT_EQ(oracle.forward(-1.0L), 0.0L);
    EXPECT_EQ(oracle.forward(2.0L), 1.0L);
    EXPECT_EQ(oracle.inverse(-1.0L), 0.0L);
    EXPECT_EQ(oracle.inverse(2.0L), 1.0L);
    for (const float bias : {-1.0f, -0.5f, 0.0f, 1.0f})
    {
        const GeneralizedHyperbolicStretchOracle identity(curveSettings(0.0f, bias, 0.5f, 0.25f, 0.75f));
        EXPECT_EQ(identity.forward(0.3L), 0.3L);
        EXPECT_EQ(identity.inverse(0.3L), 0.3L);
        EXPECT_EQ(identity.maximumSlope(), 1.0L);
    }
}

TEST(GeneralizedHyperbolicStretchOracleTest, InverseKeepsItsPrecisionWhereTheCurveSaturates)
{
    // Protection at 1/8 and 3/4 around 3/8. At a large amount the curve lies within e^-250 of its
    // supremum over most of each half, so a value just above zero is the supremum less a rise far
    // below its rounding; the inverse must still find where the curve reaches it. Evaluated from
    // the family's definition in 90-digit decimal arithmetic.
    struct Known
    {
        float amount;
        float bias;
        float input;
        long double output;
    };
    const std::array<Known, 7> known{{
        {1000.0f, 0.0f, 1.0e-20f, 0.329641445288944553664415651256L},
        {1000.0f, 0.0f, 0.5f, 0.375L},
        {1000.0f, 0.0f, 0.99999994f, 0.390942385152878742116596338794L},
        {1000.0f, 0.001f, 1.0e-20f, 0.328597014576371627796898782043L},
        {1000.0f, -0.5f, 1.0e-20f, 6.33202537090e-19L},
        {100.0f, 0.0f, 1.0e-10f, 0.135650486319342905728993645423L},
        {100.0f, -0.999999f, 0.999f, 0.996905068547346546947425391626L},
    }};
    for (const Known& value : known)
    {
        const GeneralizedHyperbolicStretchOracle oracle(curveSettings(value.amount, value.bias, 0.375f, 0.125f, 0.75f));
        EXPECT_NEAR(static_cast<double>(oracle.inverse(value.input)), static_cast<double>(value.output),
                    (std::max)(1.0e-13, static_cast<double>(value.output) * 1.0e-12))
            << value.amount << ' ' << value.bias << ' ' << value.input;
    }
    // At the largest amount the interior of the range is reached within 1e-15 of the symmetry
    // point, not at a protection point: the curve is a step there, not flat on either side of it.
    for (const float bias : {0.0f, 1.0e-6f, 0.5f, -0.5f})
    {
        const GeneralizedHyperbolicStretchOracle oracle(
            curveSettings(std::numeric_limits<float>::max(), bias, 0.375f, 0.125f, 0.75f));
        for (const long double input : {0.25L, 0.75L, 1.0L - std::numeric_limits<float>::epsilon()})
            EXPECT_NEAR(static_cast<double>(oracle.inverse(input)), 0.375, 1.0e-15) << bias;
        // A negative bias saturates algebraically, leaving the smallest input on the low tangent.
        if (bias >= 0.0f)
            EXPECT_NEAR(static_cast<double>(oracle.inverse(std::numeric_limits<float>::denorm_min())), 0.375, 1.0e-15)
                << bias;
    }
}

TEST(GeneralizedHyperbolicStretchOracleTest, EachRegimeMeetsItsNeighbourContinuously)
{
    // Minus one and zero are where one member's formula becomes another's; a bias approaching
    // either from its neighbouring regime has to approach that curve, not jump or lose precision.
    for (const auto& [limit, near, bound] :
         std::vector<std::tuple<float, float, double>>{{-1.0f, std::nextafter(-1.0f, 0.0f), 1.0e-6},
                                                       {0.0f, -1.0e-7f, 1.0e-6},
                                                       {0.0f, 1.0e-7f, 1.0e-6},
                                                       {0.0f, -1.0e-30f, 1.0e-14},
                                                       {0.0f, 1.0e-30f, 1.0e-14},
                                                       {0.0f, -std::numeric_limits<float>::denorm_min(), 1.0e-14},
                                                       {0.0f, std::numeric_limits<float>::denorm_min(), 1.0e-14}})
        for (const float amount : {1.0e-6f, 3.0f, 100.0f})
        {
            const GeneralizedHyperbolicStretchOracle exact(curveSettings(amount, limit, 0.375f, 0.125f, 0.75f));
            const GeneralizedHyperbolicStretchOracle neighbour(curveSettings(amount, near, 0.375f, 0.125f, 0.75f));
            for (int i = 0; i <= 1024; ++i)
            {
                const auto x = static_cast<long double>(i) / 1024.0L;
                EXPECT_NEAR(static_cast<double>(exact.forward(x)), static_cast<double>(neighbour.forward(x)), bound)
                    << "bias " << near << " amount " << amount << " at " << static_cast<double>(x);
                EXPECT_NEAR(static_cast<double>(exact.inverse(x)), static_cast<double>(neighbour.inverse(x)),
                            amount == 100.0f ? 1.0e-4 : bound * 10.0)
                    << "bias " << near << " amount " << amount << " at " << static_cast<double>(x);
            }
        }
}

TEST(GeneralizedHyperbolicStretchOracleTest, EveryRegimeHasAContinuousMonotoneInverseAtLimitingAmounts)
{
    for (const float amount :
         {0.0f, 1.0e-20f, 1.0e-6f, 0.1f, 2.0f, 3.0f, 100.0f, 1.0e20f, std::numeric_limits<float>::max()})
        for (const float bias : {-1.0f, -0.999999f, -0.5f, -1.0e-6f, 0.0f, 1.0e-6f, 0.5f, 1.0f})
            for (const float symmetry : {0.0f, 0.05f, 0.25f, 0.5f, 1.0f})
            {
                const GeneralizedHyperbolicStretchOracle oracle(
                    curveSettings(amount, bias, symmetry, symmetry / 2.0f, (1.0f + symmetry) / 2.0f));
                EXPECT_EQ(oracle.forward(0.0L), 0.0L);
                EXPECT_EQ(oracle.forward(1.0L), 1.0L);
                ASSERT_TRUE(std::isfinite(oracle.maximumSlope()));
                long double previous = 0.0L, previousInverse = 0.0L;
                for (int i = 0; i <= 4096; ++i)
                {
                    const auto x = static_cast<long double>(i) / 4096.0L;
                    const auto y = oracle.forward(x);
                    ASSERT_TRUE(std::isfinite(y));
                    EXPECT_GE(y, previous) << amount << ' ' << bias << ' ' << symmetry << ' ' << static_cast<double>(x);
                    // The inverse of an exact image lands where the curve takes that value, even
                    // where a flat stretch makes the input itself unrecoverable.
                    EXPECT_TRUE(reaches(oracle, oracle.inverse(y), y))
                        << amount << ' ' << bias << ' ' << symmetry << ' ' << static_cast<double>(x);
                    const auto inverse = oracle.inverse(x);
                    ASSERT_TRUE(std::isfinite(inverse));
                    EXPECT_GE(inverse, previousInverse) << amount << ' ' << bias << ' ' << symmetry;
                    EXPECT_TRUE(reaches(oracle, inverse, x))
                        << amount << ' ' << bias << ' ' << symmetry << ' ' << static_cast<double>(x);
                    previous = y;
                    previousInverse = inverse;
                }
            }
}

TEST(GeneralizedHyperbolicStretchOracleTest, EachPieceBoundaryInvertsToItsProtectionOrSymmetryPoint)
{
    for (const float bias : {-1.0f, -0.999999f, -0.5f, -1.0e-6f, 0.0f, 1.0e-6f, 0.5f, 1.0f})
        for (const auto& [symmetry, low, high] : std::vector<std::tuple<float, float, float>>{
                 {0.375f, 0.125f, 0.75f}, {0.375f, 0.375f, 0.375f}, {0.0f, 0.0f, 0.75f}, {1.0f, 0.125f, 1.0f}})
        {
            const GeneralizedHyperbolicStretchOracle oracle(curveSettings(3.0f, bias, symmetry, low, high));
            for (const float point : {low, symmetry, high})
                EXPECT_NEAR(static_cast<double>(oracle.inverse(oracle.forward(point))), point, 1.0e-13)
                    << "bias " << bias << " at " << point;
        }
    // Coincident protection leaves the unit slope at the symmetry point on both sides.
    const GeneralizedHyperbolicStretchOracle coincident(curveSettings(3.0f, 0.0f, 0.375f, 0.375f, 0.375f));
    EXPECT_NEAR(static_cast<double>(coincident.forward(0.8L)), 0.8, 1.0e-15);
}

TEST(GeneralizedHyperbolicStretchOracleTest, AnUnprobedPixelAndNonfiniteOutputFailCompleteEvidence)
{
    auto scenario = stretchScenarios()[1];
    scenario.input.width = 10001u;
    scenario.elementsProcessed = 10001u;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    ASSERT_TRUE(requiresGeneralizedHyperbolicStretchOracle(scenario));
    const GeneralizedHyperbolicStretchOracle oracle(*settings);
    std::vector<float> input(10001u), output(10001u);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        input[i] = static_cast<float>(i) / 10000.0f;
        output[i] = static_cast<float>(oracle.inverse(input[i]));
    }
    ResultRecord record;
    checkGeneralizedHyperbolicStretchOutput(*settings, input, output, record);
    EXPECT_TRUE(generalizedHyperbolicStretchEvidenceValid(scenario, record));
    EXPECT_EQ(record.counters["ghsOracleSamples"], 10001.0);
    EXPECT_EQ(record.counters["ghsOracleVersion"], 1.0);
    const float saved = output[1]; // The old 4097-probe diagnostic does not visit this sample.
    for (float bad : {0.5f, -0.1f, std::numeric_limits<float>::quiet_NaN()})
    {
        output[1] = bad;
        checkGeneralizedHyperbolicStretchOutput(*settings, input, output, record);
        EXPECT_FALSE(generalizedHyperbolicStretchEvidenceValid(scenario, record));
    }
    output[1] = saved;
    checkGeneralizedHyperbolicStretchOutput(*settings, input, output, record);
    for (const auto& [name, value] : record.counters)
    {
        auto missing = record;
        missing.counters.erase(name);
        EXPECT_FALSE(generalizedHyperbolicStretchEvidenceValid(scenario, missing)) << name;
    }
    auto loosened = record;
    loosened.tolerance = 1.0;
    loosened.counters["ghsOracleMaxAbsoluteError"] = 1.1e-6;
    EXPECT_FALSE(generalizedHyperbolicStretchEvidenceValid(scenario, loosened));
    // Another family's scenario never borrows this evidence.
    EXPECT_FALSE(requiresGeneralizedHyperbolicStretchOracle(stretchScenarios()[3]));
    EXPECT_FALSE(generalizedHyperbolicStretchEvidenceValid(stretchScenarios()[3], record));
}

TEST(GeneralizedHyperbolicStretchOracleTest, CountersAreTheSharedWalkUnderThisFamilysNames)
{
    auto scenario = stretchScenarios()[0];
    scenario.input.width = 5u;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings) << error;
    const GeneralizedHyperbolicStretchOracle oracle(*settings);
    const std::array<float, 5> input{0.0f, 0.25f, 0.5f, 0.5f, 1.0f};
    std::array<float, 5> output{};
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = static_cast<float>(oracle.forward(input[i]));
    output[3] = std::nextafter(output[2], 2.0f); // equal inputs with unequal outputs
    output[4] = 0.5f; // endpoint, and below the sample before it
    ResultRecord record;
    checkGeneralizedHyperbolicStretchOutput(*settings, input, output, record);
    EXPECT_EQ(record.counters["ghsOracleSamples"], 5.0);
    EXPECT_EQ(record.counters["ghsEndpointViolations"], 1.0);
    EXPECT_EQ(record.counters["ghsOrderingViolations"], 2.0);
    EXPECT_EQ(record.counters["ghsRangeViolations"], 0.0);
    EXPECT_NEAR(record.counters["ghsOracleMaxAbsoluteError"], 0.5, 1.0e-12);
    EXPECT_EQ(record.counters.at("ghsRoundtripError"), 0.0) << "a forward row has no round trip";
    EXPECT_EQ(record.correctnessMetric, record.counters["ghsOracleMaxAbsoluteError"]);
}

TEST(GeneralizedHyperbolicStretchOracleTest, AnOutputJustOutsideTheIntervalFailsTheClamp)
{
    auto scenario = stretchScenarios()[1];
    scenario.input.width = 2;
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    ASSERT_TRUE(settings);
    const std::array<float, 2> input{0.0f, 1.0f};
    std::array<float, 2> output{5.587935092421503e-8f, 1.0000001192092896f};
    ResultRecord record;
    checkGeneralizedHyperbolicStretchOutput(*settings, input, output, record);
    EXPECT_FALSE(generalizedHyperbolicStretchEvidenceValid(scenario, record));
    EXPECT_DOUBLE_EQ(record.counters["ghsRangeExcursion"], 1.1920928955078125e-7);
}

TEST(GeneralizedHyperbolicStretchOracleTest, BoundaryRowsCoverEveryBiasRegimeAtEveryLimitingProfile)
{
    std::set<std::tuple<std::uint32_t, std::string, std::string, std::string>> seen;
    std::set<std::string> ids;
    for (const auto& scenario : stretchScenarios())
    {
        if (!requiresGeneralizedHyperbolicStretchOracle(scenario) || scenario.corpusId != "synthetic.ghs_boundaries")
            continue;
        std::string error;
        const auto settings = parseStretchSettings(scenario, error);
        ASSERT_TRUE(settings) << error;
        EXPECT_TRUE(ids.insert(scenario.id).second);
        const auto& p = scenario.parameters;
        seen.insert({settings->function, p.at("bias"), p.at("amount"), p.at("symmetry")});
    }
    EXPECT_EQ(ids.size(), 88u);
    for (const std::uint32_t function : {0u, 1u})
        for (const char* bias : {"-1", "-0.999999", "-0.5", "-0.000001", "0", "0.000001", "1"})
        {
            for (const char* amount : {"0.000001", "3", "100", "3.4028234663852886e38"})
                EXPECT_TRUE(seen.contains({function, bias, amount, "0.375"}))
                    << function << ' ' << bias << ' ' << amount;
            for (const char* symmetry : {"0", "1"})
                EXPECT_TRUE(seen.contains({function, bias, "3", symmetry}))
                    << function << ' ' << bias << ' ' << symmetry;
        }
}

TEST(GeneralizedHyperbolicStretchOracleTest, RegisteredBoundaryFixtureContainsBothDomainsAndTheirFloatNeighbors)
{
    SyntheticFixtureSource source;
    std::size_t checked = 0u;
    for (const auto& scenario : stretchScenarios())
    {
        if (scenario.corpusId != "synthetic.ghs_boundaries")
            continue;
        std::string error;
        const auto settings = parseStretchSettings(scenario, error);
        ASSERT_TRUE(settings) << error;
        const GeneralizedHyperbolicStretchOracle oracle(*settings);
        Fixture fixture;
        ASSERT_TRUE(source.acquire(scenario.corpusId, scenario.seed, scenario.input, fixture, error)) << error;
        std::vector<float> values(fixture.bytes.size() / sizeof(float));
        std::memcpy(values.data(), fixture.bytes.data(), fixture.bytes.size());
        for (auto x : {0.0f, settings->lowProtection, settings->symmetry, settings->highProtection, 1.0f})
            for (auto center : {x, static_cast<float>(oracle.forward(x))})
                for (auto value : {std::nextafter(center, 0.0f), center, std::nextafter(center, 1.0f)})
                {
                    // Near-identity curves can normalize a landmark differently below one Float32 ULP.
                    const auto found = std::find_if(values.begin(), values.end(), [&](float actual) {
                        return std::abs(actual - value) <= std::abs(std::nextafter(value, 1.0f) - value);
                    });
                    EXPECT_NE(found, values.end()) << scenario.id << " boundary " << value;
                }
        ++checked;
    }
    EXPECT_EQ(checked, 88u);
}

TEST(GeneralizedHyperbolicStretchOracleTest, NonQuantizedControlsCannotBeAdmittedAsUInt16Codes)
{
    SyntheticFixtureSource source;
    std::size_t controls = 0u;
    for (const auto& scenario : stretchScenarios())
    {
        if (!requiresGeneralizedHyperbolicStretchOracle(scenario) || scenario.corpusId != "synthetic.uniform_noise")
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
    EXPECT_EQ(controls, 4u);
}
