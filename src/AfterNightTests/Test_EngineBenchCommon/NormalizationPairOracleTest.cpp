// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Normalization of one captured light to another: the rows and the definition the engine is held to.
#include "NormalizationPairOracle.h"
#include "NormalizationPairScenarios.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace an::bench;

TEST(NormalizationPairOracleTest, TheRowsMatchIndependentLightsUnderEachMethod)
{
    const std::vector<ScenarioSpec> rows = normalizationPairScenarios();
    ASSERT_EQ(rows.size(), 4u);
    std::set<std::string> methods;
    for (const ScenarioSpec& row : rows)
    {
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_EQ(row.group, "normalization") << row.id;
        EXPECT_EQ(row.corpusId, kNormalizationPairReference) << row.id;
        EXPECT_NE(row.parameters.at(kNormalizationTargetParameter), kNormalizationPairReference) << row.id;
        EXPECT_EQ(row.parameters.at("estimator"), "MedianAbsoluteDeviation") << row.id;
        EXPECT_EQ(row.parameters.at("validity"), "zero-is-no-data") << row.id;
        EXPECT_EQ(row.correctnessClass, CorrectnessClass::EstimatorTolerance) << row.id;
        methods.insert(row.parameters.at("method"));
        EXPECT_TRUE(normalizationPairMatch(row.parameters.at("method"), {}, {}).has_value()) << row.id;
    }
    EXPECT_EQ(methods, (std::set<std::string>{"Additive", "Multiplicative", "AdditiveScaling"}));
    EXPECT_EQ(rows[3].parameters.at(kNormalizationTargetParameter), kNormalizationPairRotated);
}

TEST(NormalizationPairOracleTest, ALevelIsTheExactMedianAndDeviationOfTheData)
{
    // Zeros are no data and are left out; the even count takes the mean of the two middle values.
    const std::vector<float> samples = {0.0f, 0.5f, 0.1f, 0.0f, 0.3f, 0.2f, std::nanf(""), 0.4f};
    const NormalizationPairLevel level = normalizationPairLevel(samples);
    EXPECT_EQ(level.samples, 5u);
    EXPECT_DOUBLE_EQ(level.median, static_cast<double>(0.3f));
    // Deviations from 0.3: 0.2, 0.2, 0, 0.1, 0.1.
    EXPECT_NEAR(level.deviation, 0.1, 1e-7);
    const std::vector<float> even = {0.1f, 0.2f, 0.3f, 0.4f};
    EXPECT_NEAR(normalizationPairLevel(even).median, 0.25, 1e-7);
    const NormalizationPairLevel empty = normalizationPairLevel(std::vector<float>{0.0f, 0.0f});
    EXPECT_EQ(empty.samples, 0u);
    EXPECT_EQ(empty.median, 0.0);
}

TEST(NormalizationPairOracleTest, EachMethodMatchesTheLevelsAsDefined)
{
    const NormalizationPairLevel reference{.median = 0.06, .deviation = 0.004, .samples = 10u};
    const NormalizationPairLevel target{.median = 0.05, .deviation = 0.002, .samples = 10u};
    const auto additive = normalizationPairMatch("Additive", reference, target);
    ASSERT_TRUE(additive.has_value());
    EXPECT_DOUBLE_EQ(additive->scale, 1.0);
    EXPECT_DOUBLE_EQ(additive->offset, 0.01);
    const auto multiplicative = normalizationPairMatch("Multiplicative", reference, target);
    ASSERT_TRUE(multiplicative.has_value());
    EXPECT_DOUBLE_EQ(multiplicative->scale, 0.06 / 0.05);
    EXPECT_DOUBLE_EQ(multiplicative->offset, 0.0);
    const auto scaling = normalizationPairMatch("AdditiveScaling", reference, target);
    ASSERT_TRUE(scaling.has_value());
    EXPECT_DOUBLE_EQ(scaling->scale, 2.0);
    // The target's median lands on the reference's.
    EXPECT_DOUBLE_EQ(target.median * scaling->scale + scaling->offset, reference.median);

    // A zero spread or a zero target median leaves the factor at one.
    const NormalizationPairLevel flat{.median = 0.05, .deviation = 0.0, .samples = 10u};
    EXPECT_DOUBLE_EQ(normalizationPairMatch("AdditiveScaling", reference, flat)->scale, 1.0);
    EXPECT_DOUBLE_EQ(normalizationPairMatch("AdditiveScaling", flat, target)->scale, 1.0);
    const NormalizationPairLevel dark{.median = 0.0, .deviation = 0.002, .samples = 10u};
    EXPECT_DOUBLE_EQ(normalizationPairMatch("Multiplicative", reference, dark)->scale, 1.0);
    EXPECT_FALSE(normalizationPairMatch("MultiplicativeScaling", reference, target).has_value());
}

TEST(NormalizationPairOracleTest, ANormalizedSampleIsHeldToTheMatchAndAZeroStaysAZero)
{
    const NormalizationPairMatch match{.scale = 2.0, .offset = -0.01};
    const std::vector<float> target = {0.0f, 0.05f, 0.25f, 0.005f};
    std::vector<float> normalized = {0.0f, 0.09f, 0.49f, 0.0f};
    // The last sample lands on zero: zero and the smallest normal are both accepted.
    EXPECT_LT(normalizationPairError(normalized, target, match), 1e-7);
    normalized[3] = (std::numeric_limits<float>::min)();
    EXPECT_LT(normalizationPairError(normalized, target, match), 1e-7);

    std::vector<float> moved = normalized;
    moved[2] += 0.001f;
    EXPECT_NEAR(normalizationPairError(moved, target, match), 0.001, 1e-7);
    std::vector<float> filled = normalized;
    filled[0] = 0.01f;
    EXPECT_EQ(normalizationPairError(filled, target, match), (std::numeric_limits<double>::max)());
    std::vector<float> broken = normalized;
    broken[1] = std::numeric_limits<float>::infinity();
    EXPECT_EQ(normalizationPairError(broken, target, match), (std::numeric_limits<double>::max)());
    EXPECT_EQ(normalizationPairError(std::span<const float>(normalized).first(2u), target, match),
              (std::numeric_limits<double>::max)());
}
