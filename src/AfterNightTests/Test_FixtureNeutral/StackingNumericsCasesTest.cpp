// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The stacking and drizzle numerics' case table: what the engine fixture executable reads must be well formed before
// it compares.

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/StackingNumericsCases.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace
{

namespace fixtures = an::tests::fixtures;

} // namespace

TEST(StackingNumericsCasesTest, EveryInputIsUniqueMaterializesAndIsNoCatalogueFixture)
{
    std::set<std::string> names;
    for (const fixtures::FixtureImage& image : fixtures::stackingNumericsInputs())
    {
        EXPECT_TRUE(names.insert(image.name).second) << image.name << " is stated twice";
        EXPECT_EQ(fixtures::fixtureNamed(image.name), nullptr) << image.name << " is also a catalogue fixture";
        std::string error;
        EXPECT_TRUE(fixtures::materializeFixture(image, error).has_value()) << image.name << ": " << error;
        EXPECT_EQ(fixtures::stackingNumericsInput(image.name), &image) << image.name;
    }
    EXPECT_EQ(fixtures::stackingNumericsInput("no-such-input"), nullptr);
}

TEST(StackingNumericsCasesTest, EveryCaseIsUniqueAndStatesWhatItsKindReads)
{
    std::set<std::string> ids;
    for (const fixtures::StackingNumericsCase& item : fixtures::stackingNumericsCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        EXPECT_NE(fixtures::stackingNumericsInput(item.input), nullptr) << item.id();
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_NE(item.source.find(".cpp:"), std::string::npos) << item.id();
        EXPECT_EQ(fixtures::stackingNumericsCase(item.id()), &item) << item.id();
        switch (item.kind)
        {
        case fixtures::StackingNumericsCaseKind::CometSplit:
            EXPECT_EQ(item.operation.rfind("comet.split", 0u), 0u) << item.id();
            EXPECT_EQ(item.apertures.size(), 1u) << item.id();
            EXPECT_TRUE(item.second.empty()) << item.id();
            break;
        case fixtures::StackingNumericsCaseKind::CometMask:
        case fixtures::StackingNumericsCaseKind::CometBlend:
            EXPECT_EQ(item.operation.rfind("comet.", 0u), 0u) << item.id();
            EXPECT_FALSE(item.apertures.empty()) << item.id();
            EXPECT_GE(item.scale, 1.0) << item.id();
            EXPECT_EQ(item.second.empty(), item.kind == fixtures::StackingNumericsCaseKind::CometMask) << item.id();
            break;
        case fixtures::StackingNumericsCaseKind::RejectionMaps: {
            EXPECT_EQ(item.operation.rfind("rejection.", 0u), 0u) << item.id();
            const fixtures::FixtureImage* low = fixtures::stackingNumericsInput(item.input);
            const fixtures::FixtureImage* high = fixtures::stackingNumericsInput(item.second);
            ASSERT_NE(high, nullptr) << item.id();
            EXPECT_EQ(low->width, high->width) << item.id();
            EXPECT_EQ(low->height, high->height) << item.id();
            EXPECT_EQ(low->channels, high->channels) << item.id();
            EXPECT_EQ(low->sampleType, fixtures::FixtureSampleType::UInt32) << item.id();
            EXPECT_GT(item.frames, 0u) << item.id();
            EXPECT_GE(item.minimum, 1u) << item.id();
            break;
        }
        case fixtures::StackingNumericsCaseKind::DrizzleCanvas:
            EXPECT_EQ(item.operation.rfind("canvas.", 0u), 0u) << item.id();
            EXPECT_TRUE(fixtures::stackingNumericsCanvasModeKnown(item.mode)) << item.id();
            EXPECT_FALSE(item.canvasFrames.empty()) << item.id();
            EXPECT_LT(item.referenceIndex, item.canvasFrames.size()) << item.id();
            break;
        case fixtures::StackingNumericsCaseKind::Weighting:
            EXPECT_EQ(item.operation.rfind("weighting.", 0u), 0u) << item.id();
            EXPECT_TRUE(item.weighting == "exposure" || item.weighting == "snr") << item.id();
            EXPECT_GE(item.figures.size(), 2u) << item.id();
            break;
        }
    }
    EXPECT_EQ(fixtures::stackingNumericsCase("comet.none/none"), nullptr);
    EXPECT_FALSE(fixtures::stackingNumericsCanvasModeKnown("reference"));
}

TEST(StackingNumericsCasesTest, AClassificationCarriesWhatItReads)
{
    for (const fixtures::StackingNumericsCase& item : fixtures::stackingNumericsCases())
    {
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, !item.acceptedReason.empty())
            << item.id();
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, item.acceptedBound > 0.0) << item.id();
        if (item.classification == fixtures::ExpectationClass::Tolerance)
        {
            EXPECT_GT(item.absoluteTolerance, 0.0) << item.id();
            EXPECT_GE(item.relativeTolerance, 0.0) << item.id();
            EXPECT_FALSE(item.note.empty()) << item.id() << ": a tolerance says why";
        }
        else
        {
            EXPECT_EQ(item.absoluteTolerance, 0.0) << item.id();
            EXPECT_EQ(item.relativeTolerance, 0.0) << item.id();
        }
        if (item.classification == fixtures::ExpectationClass::Accepted)
        {
            EXPECT_FALSE(item.note.empty()) << item.id() << ": a bound says why";
        }
    }
}
