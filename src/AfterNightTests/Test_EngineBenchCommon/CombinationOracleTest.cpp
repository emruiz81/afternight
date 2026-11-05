// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The image combination and hand-off rows, their derived inputs and their engine-independent definitions.
#include "CombinationOracle.h"
#include "CombinationScenarios.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace an::bench;

TEST(CombinationOracleTest, TheFiveRowsAreWellFormed)
{
    const std::vector<ScenarioSpec> rows = combinationScenarios();
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_EQ(rows[0].id, kLrgbCombine);
    EXPECT_EQ(rows[1].id, kBlendModes);
    EXPECT_EQ(rows[2].id, kStarRecombine);
    EXPECT_EQ(rows[3].id, kExternalHandoff);
    EXPECT_EQ(rows[4].id, kEdgeTighten);
    EXPECT_EQ(rows[0].group, "color");
    EXPECT_EQ(rows[1].group, "pointwise");
    EXPECT_EQ(rows[2].group, "stars");
    EXPECT_EQ(rows[3].group, "pointwise");
    EXPECT_EQ(rows[4].group, "filter");
    for (const ScenarioSpec& row : rows)
    {
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_TRUE(requiresCombinationOracle(row)) << row.id;
        EXPECT_EQ(row.input.channels, 3u) << row.id;
        EXPECT_EQ(row.input.sampleType, SampleType::Float32) << row.id;
        EXPECT_EQ(row.correctnessClass, CorrectnessClass::AbsoluteRelative) << row.id;
        EXPECT_GT(row.tolerance, 0.0) << row.id;
        EXPECT_TRUE(row.parameters.contains(kTimedBatchParameter)) << row.id;
    }
    ScenarioSpec other = rows[0];
    other.id = "color.channel_scope.lab_l.f32";
    EXPECT_FALSE(requiresCombinationOracle(other));
    EXPECT_EQ(std::string(kBlendModeLabels.front()), "Replace");
    EXPECT_EQ(std::string(kBlendModeLabels.back()), "Lightness");
}

TEST(CombinationOracleTest, TheDerivedInputsAreTheirFormulas)
{
    // A 2 by 1 colour frame, planar.
    const std::vector<float> color{0.2f, 0.9f, 0.4f, 0.8f, 0.1f, 0.6f};
    const std::vector<float> luminance = lrgbLuminanceFrame(color, 2u, 1u);
    ASSERT_EQ(luminance.size(), 2u);
    EXPECT_EQ(luminance[0], static_cast<float>(0.3 * 0.2f + 0.6 * 0.4f + 0.1 * 0.1f + 0.02));
    EXPECT_EQ(luminance[1], static_cast<float>(0.3 * 0.9f + 0.6 * 0.8f + 0.1 * 0.6f + 0.02));
    const std::vector<float> white{1.0f, 1.0f, 1.0f};
    EXPECT_EQ(lrgbLuminanceFrame(white, 1u, 1u)[0], 1.0f) << "held at one";

    const std::vector<float> second = blendSecondFrame(color, 2u, 1u);
    ASSERT_EQ(second.size(), 6u);
    // Channel zero is one less channel one, mirrored: pixel zero reads channel one's pixel one.
    EXPECT_EQ(second[0], static_cast<float>(1.0 - 0.8f));
    EXPECT_EQ(second[1], static_cast<float>(1.0 - 0.4f));
    EXPECT_EQ(second[4], static_cast<float>(1.0 - 0.9f)) << "channel two reads channel zero";

    const std::vector<float> starless = recombinationStarless(4u, 2u);
    ASSERT_EQ(starless.size(), 24u);
    EXPECT_EQ(starless[0], static_cast<float>(0.04));
    EXPECT_EQ(starless[8 + 3 + 4], static_cast<float>(0.04 + 0.03 * 3.0 / 4.0 + 0.02 * 1.0 / 2.0 + 0.01));

    const std::vector<float> mask = recombinationMask(std::vector<float>{0.1f, 0.3f}, 2u, 1u);
    EXPECT_EQ(mask[0], static_cast<float>(4.0 * static_cast<double>(0.1f)));
    EXPECT_EQ(mask[1], 1.0f);
}

TEST(CombinationOracleTest, TheBlendCheckHoldsEachModeToItsFormula)
{
    // One pixel per frame, three channels.
    const std::vector<float> target{0.5f, 0.25f, 0.8f};
    const std::vector<float> blend{0.4f, 0.0f, 1.0f};
    const auto check = [&](const char* label, const std::vector<float>& output) {
        std::size_t mode = 0u;
        while (std::string(kBlendModeLabels[mode]) != label)
        {
            ++mode;
        }
        BlendModesCheck running;
        checkBlendModeOutput(mode, target, blend, 1u, 1u, output, running);
        return running.largest;
    };
    EXPECT_LE(check("Multiply", {0.2f, 0.0f, 0.8f}), 1.0e-7);
    EXPECT_LE(check("Screen", {0.7f, 0.25f, 1.0f}), 1.0e-7);
    // A zero divisor answers zero, and a quotient above one is held there.
    EXPECT_LE(check("Divide", {1.0f, 0.0f, 0.8f}), 1.0e-7);
    EXPECT_LE(check("Linear Dodge/Add", {0.9f, 0.25f, 1.0f}), 1.0e-7);
    EXPECT_LE(check("Subtract", {0.1f, 0.25f, 0.0f}), 1.0e-7);
    // Colour dodge answers zero where the blend is one, and colour burn where it is zero.
    EXPECT_LE(check("Color Dodge", {0.5f / 0.6f, 0.25f, 0.0f}), 1.0e-7);
    EXPECT_LE(check("Color Burn", {0.0f, 0.0f, 0.8f}), 1.0e-6);
    EXPECT_GE(check("Multiply", {0.21f, 0.0f, 0.8f}), 0.0099);

    // A grey pixel over a grey pixel: Lightness takes the blend's lightness, so it is the blend's grey.
    const std::vector<float> grey{0.3f, 0.3f, 0.3f};
    const std::vector<float> lighter{0.7f, 0.7f, 0.7f};
    std::size_t lightness = 0u;
    while (std::string(kBlendModeLabels[lightness]) != "Lightness")
    {
        ++lightness;
    }
    BlendModesCheck running;
    checkBlendModeOutput(lightness, grey, lighter, 1u, 1u, lighter, running);
    EXPECT_LE(running.largest, 1.0e-6);
    ResultRecord result;
    finishBlendModesCheck(running, result);
    EXPECT_EQ(result.correctnessMetric, running.largest);
    EXPECT_EQ(result.counters["blendModesOracleSamples"], 3.0);
}

TEST(CombinationOracleTest, TheRecombinationCheckLeavesOutTheRowEnds)
{
    // No stars: nothing to tighten, so the result is the starless background whatever the mask.
    const std::uint32_t width = 6u;
    const std::uint32_t height = 4u;
    const std::vector<float> stars(3u * width * height, 0.0f);
    const std::vector<float> starless = recombinationStarless(width, height);
    const std::vector<float> mask(width * height, 0.5f);
    std::vector<float> output = starless;
    ResultRecord exact;
    checkRecombinationOutput(stars, starless, mask, width, height, output, exact);
    EXPECT_LE(*exact.correctnessMetric, 1.0e-7);
    EXPECT_EQ(exact.counters["recombinationOracleSamples"], 3.0 * (width - 2u) * height);
    // A difference in a row's first or last column is not compared; one inside is.
    output[0] += 0.5f;
    output[width - 1u] += 0.5f;
    ResultRecord ends;
    checkRecombinationOutput(stars, starless, mask, width, height, output, ends);
    EXPECT_LE(*ends.correctnessMetric, 1.0e-7);
    output[2] += 0.25f;
    ResultRecord inside;
    checkRecombinationOutput(stars, starless, mask, width, height, output, inside);
    EXPECT_NEAR(*inside.correctnessMetric, 0.25, 1.0e-6);
}

TEST(CombinationOracleTest, TheEdgeTightenCheckHoldsTheDefinitionAndLeavesOutTheRowEnds)
{
    // A flat frame has no edge inside it, so only its first and last rows - whose outside neighbours read as zero - are
    // tightened: each of their samples' four responses has magnitudes 0, 2, 1.5 and 1.5 there, so it falls by one half
    // of a twentieth of five.
    const std::uint32_t width = 6u;
    const std::uint32_t height = 4u;
    const std::vector<float> flat(3u * width * height, 0.5f);
    std::vector<float> output = flat;
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (const std::uint32_t y : {0u, height - 1u})
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                output[(c * height + y) * width + x] = 0.375f;
            }
        }
    }
    ResultRecord exact;
    checkEdgeTightenOutput(flat, width, height, output, exact);
    EXPECT_LE(*exact.correctnessMetric, 1.0e-7);
    EXPECT_EQ(exact.counters["edgeTightenOracleSamples"], 3.0 * (width - 2u) * height);
    // An untightened frame is not its own answer.
    ResultRecord untouched;
    checkEdgeTightenOutput(flat, width, height, flat, untouched);
    EXPECT_NEAR(*untouched.correctnessMetric, 0.125, 1.0e-7);
    // A difference in a row's first or last column is not compared; one inside is.
    output[width] += 0.5f;
    output[2u * width - 1u] += 0.5f;
    ResultRecord ends;
    checkEdgeTightenOutput(flat, width, height, output, ends);
    EXPECT_LE(*ends.correctnessMetric, 1.0e-7);
    output[width + 2u] += 0.25f;
    ResultRecord inside;
    checkEdgeTightenOutput(flat, width, height, output, inside);
    EXPECT_NEAR(*inside.correctnessMetric, 0.25, 1.0e-6);
}

TEST(CombinationOracleTest, TheHandOffCheckHoldsARoundTripToTheSource)
{
    // A frame of slowly varying samples comes back through the stretch and the sixteen-bit codes to within a code's
    // width of the inverse; one offset by a hundredth does not.
    const std::uint32_t width = 64u;
    const std::uint32_t height = 32u;
    std::vector<float> source(3u * width * height, 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>(0.05 + 0.4 * static_cast<double>(index % 997u) / 997.0);
    }
    ResultRecord close;
    checkHandoffOutput(source, width, height, source, close);
    EXPECT_LT(*close.correctnessMetric, 2.0e-3) << "the source itself is a round trip within the row's tolerance";
    std::vector<float> offset = source;
    for (float& value : offset)
    {
        value += 0.01f;
    }
    ResultRecord far;
    checkHandoffOutput(source, width, height, offset, far);
    EXPECT_GT(*far.correctnessMetric, 5.0e-3);
}
