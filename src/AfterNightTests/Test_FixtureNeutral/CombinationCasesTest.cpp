// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The combination and hand-off case table: what the engine fixture executable reads must be well formed before it
// compares.

#include "fixtures/common/CombinationCases.h"
#include "fixtures/common/FixtureCatalogue.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;

const std::set<std::string>& blendLabels()
{
    static const std::set<std::string> labels = {
        "Replace",       "Darken/Min",  "Multiply",   "Color Burn",  "Linear Burn",
        "Darker Color",  "Lighten/Max", "Screen",     "Color Dodge", "Linear Dodge/Add",
        "Lighter Color", "Overlay",     "Soft Light", "Hard Light",  "Vivid Light",
        "Linear Light",  "Pin Light",   "Difference", "Exclusion",   "Subtract",
        "Divide",        "Power",       "Arctan",     "Hue",         "Saturation",
        "Color",         "Lightness"};
    return labels;
}

bool unit(double value)
{
    return value >= 0.0 && value <= 1.0;
}

} // namespace

TEST(CombinationCasesTest, TheProductsTwentySevenBlendLabelsAreTheOnesTheCasesMayName)
{
    EXPECT_EQ(blendLabels().size(), 27u);
}

TEST(CombinationCasesTest, EveryCaseIsUniqueAndNamesInputsOfOneSize)
{
    std::set<std::string> ids;
    for (const fixtures::CombinationCase& item : fixtures::combinationCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        EXPECT_EQ(fixtures::combinationCase(item.id()), &item) << item.id();
        const fixtures::FixtureImage* input = fixtures::fixtureNamed(item.input);
        ASSERT_NE(input, nullptr) << item.id();
        const char* prefix = nullptr;
        switch (item.kind)
        {
        case fixtures::CombinationCaseKind::Blend:
            prefix = "blend.";
            break;
        case fixtures::CombinationCaseKind::Lrgb:
            prefix = "lrgb.";
            break;
        case fixtures::CombinationCaseKind::Recombination:
            prefix = "recombination.";
            break;
        case fixtures::CombinationCaseKind::Handoff:
            prefix = "handoff.";
            break;
        }
        ASSERT_NE(prefix, nullptr) << item.id();
        EXPECT_EQ(item.operation.rfind(prefix, 0u), 0u) << item.id();
        EXPECT_EQ(item.kind == fixtures::CombinationCaseKind::Handoff, item.second.empty()) << item.id();
        if (!item.second.empty())
        {
            const fixtures::FixtureImage* second = fixtures::fixtureNamed(item.second);
            ASSERT_NE(second, nullptr) << item.id();
            EXPECT_EQ(second->width, input->width) << item.id();
            EXPECT_EQ(second->height, input->height) << item.id();
        }
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_NE(item.source.find(".cpp:"), std::string::npos) << item.id();
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, !item.acceptedReason.empty())
            << item.id();
        if (item.classification == fixtures::ExpectationClass::Tolerance)
        {
            EXPECT_GT(item.absoluteTolerance, 0.0) << item.id();
        }
        else
        {
            EXPECT_EQ(item.absoluteTolerance, 0.0) << item.id();
            EXPECT_EQ(item.relativeTolerance, 0.0) << item.id();
        }
        if (item.classification == fixtures::ExpectationClass::Accepted)
        {
            EXPECT_GT(item.acceptedBound, 0.0) << item.id();
        }
        else
        {
            EXPECT_EQ(item.acceptedBound, 0.0) << item.id();
        }
        EXPECT_FALSE(item.note.empty()) << item.id() << ": a classification says why";
        EXPECT_STREQ(fixtures::combinationExpectationFileOf(item), item.kind == fixtures::CombinationCaseKind::Handoff
                                                                       ? fixtures::kHandoffExpectationFile
                                                                       : fixtures::kCombinationExpectationFile)
            << item.id();
    }
    EXPECT_EQ(fixtures::combinationCase("blend.none/none"), nullptr);
}

// Every setting a case states is one the product's dialogs can state.
TEST(CombinationCasesTest, EverySettingIsOneTheProductOffers)
{
    for (const fixtures::CombinationCase& item : fixtures::combinationCases())
    {
        switch (item.kind)
        {
        case fixtures::CombinationCaseKind::Blend: {
            const fixtures::BlendCaseSettings& blend = item.blend;
            EXPECT_EQ(blendLabels().count(blend.mode), 1u) << item.id();
            EXPECT_EQ((std::set<std::string>{"None", "Gaussian Blur", "High Pass", "Hue", "Color"}).count(blend.filter),
                      1u)
                << item.id();
            for (const double value :
                 {blend.targetBlackPoint, blend.targetHighlights, blend.targetMidtones, blend.blendBlackPoint,
                  blend.blendHighlights, blend.blendMidtones, blend.filterSaturation, blend.opacity})
            {
                EXPECT_TRUE(unit(value)) << item.id();
            }
            EXPECT_LE(blend.targetBlackPoint, blend.targetHighlights) << item.id();
            EXPECT_LE(blend.blendBlackPoint, blend.blendHighlights) << item.id();
            EXPECT_GE(blend.filterRadius, 0.1) << item.id() << ": the dialog's smallest radius";
            EXPECT_LE(blend.filterRadius, 1000.0) << item.id() << ": the dialog's largest radius";
            EXPECT_GE(blend.filterHue, 0.0) << item.id();
            EXPECT_LE(blend.filterHue, 360.0) << item.id();
            break;
        }
        case fixtures::CombinationCaseKind::Lrgb:
            for (const double value : {item.lrgb.lightnessWeight, item.lrgb.redWeight, item.lrgb.greenWeight,
                                       item.lrgb.blueWeight, item.lrgb.lightnessMidtones, item.lrgb.saturationMidtones})
            {
                EXPECT_TRUE(unit(value)) << item.id();
            }
            break;
        case fixtures::CombinationCaseKind::Recombination:
            if (!item.recombination.mask.empty())
            {
                const fixtures::FixtureImage* mask = fixtures::fixtureNamed(item.recombination.mask);
                const fixtures::FixtureImage* input = fixtures::fixtureNamed(item.input);
                ASSERT_NE(mask, nullptr) << item.id();
                EXPECT_EQ(mask->width, input->width) << item.id();
                EXPECT_EQ(mask->height, input->height) << item.id();
            }
            EXPECT_GE(item.recombination.tightenEdges, 0.0) << item.id();
            break;
        case fixtures::CombinationCaseKind::Handoff:
            EXPECT_EQ((std::set<std::string>{"none", "unlinked", "linked"}).count(item.handoff.stretch), 1u)
                << item.id();
            // The star mask is the only linked hand-off, and its mask is the tool's own answer: nothing comes back.
            if (item.handoff.stretch == "linked")
            {
                EXPECT_EQ(item.handoff.output, fixtures::HandoffCaseOutput::Handed) << item.id();
            }
            break;
        }
    }
}
