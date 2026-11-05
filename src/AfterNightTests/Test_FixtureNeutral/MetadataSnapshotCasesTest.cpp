// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The frame-kind, combination and history case table: what the engine fixture executable reads must be well formed
// before it compares.

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/MetadataSnapshotCases.h"

#include <gtest/gtest.h>

#include <cmath>
#include <iterator>
#include <set>
#include <string>

namespace
{

namespace fixtures = an::tests::fixtures;

} // namespace

TEST(MetadataSnapshotCasesTest, EveryInputIsUniqueMaterializesAndIsNoCatalogueFixture)
{
    std::set<std::string> names;
    for (const fixtures::FixtureImage& image : fixtures::metadataSnapshotInputs())
    {
        EXPECT_TRUE(names.insert(image.name).second) << image.name << " is stated twice";
        EXPECT_EQ(fixtures::fixtureNamed(image.name), nullptr) << image.name << " is also a catalogue fixture";
        std::string error;
        EXPECT_TRUE(fixtures::materializeFixture(image, error).has_value()) << image.name << ": " << error;
        EXPECT_EQ(fixtures::metadataSnapshotInput(image.name), &image) << image.name;
    }
    EXPECT_EQ(fixtures::metadataSnapshotInput("no-such-input"), nullptr);
}

TEST(MetadataSnapshotCasesTest, EveryCaseIsUniqueAndReadsTheInputsItsKindNeeds)
{
    std::set<std::string> ids;
    std::set<fixtures::FixtureSampleType> historyTypes;
    for (const fixtures::MetadataSnapshotCase& item : fixtures::metadataSnapshotCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        const fixtures::FixtureImage* input = fixtures::metadataSnapshotInput(item.input);
        ASSERT_NE(input, nullptr) << item.id();
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_TRUE(item.source.find(".cpp:") != std::string::npos || item.source.find(".h:") != std::string::npos)
            << item.id();
        EXPECT_EQ(fixtures::metadataSnapshotCase(item.id()), &item) << item.id();
        switch (item.kind)
        {
        case fixtures::MetadataSnapshotCaseKind::FrameKind:
            EXPECT_FALSE(input->cards.empty()) << item.id() << ": a file's cards";
            EXPECT_TRUE(item.others.empty()) << item.id();
            break;
        case fixtures::MetadataSnapshotCaseKind::Combination:
            EXPECT_FALSE(item.others.empty()) << item.id() << ": a combination has two sources or more";
            for (const std::string& other : item.others)
            {
                EXPECT_NE(fixtures::metadataSnapshotInput(other), nullptr) << item.id() << ": " << other;
            }
            break;
        case fixtures::MetadataSnapshotCaseKind::HistoryRoundTrip:
            EXPECT_TRUE(input->cards.empty()) << item.id();
            EXPECT_TRUE(item.others.empty()) << item.id();
            historyTypes.insert(input->sampleType);
            break;
        }
    }
    EXPECT_EQ(historyTypes.size(), 8u) << "a history case of each of the eight resident types";
    EXPECT_EQ(fixtures::metadataSnapshotCase("frame.kind/no-such-input"), nullptr);
}

TEST(MetadataSnapshotCasesTest, AClassificationCarriesWhatItReads)
{
    for (const fixtures::MetadataSnapshotCase& item : fixtures::metadataSnapshotCases())
    {
        const bool accepted = item.classification == fixtures::ExpectationClass::Accepted;
        EXPECT_EQ(accepted, !item.acceptedReason.empty()) << item.id();
        EXPECT_EQ(accepted, item.acceptedBound > 0.0) << item.id();
        EXPECT_EQ(accepted, !item.differing.empty()) << item.id();
        EXPECT_NE(item.classification, fixtures::ExpectationClass::Tolerance)
            << item.id() << ": these computations agree exactly or differ on purpose";
        if (accepted)
        {
            EXPECT_FALSE(item.note.empty()) << item.id() << ": a bound says why";
        }
        if (item.kind == fixtures::MetadataSnapshotCaseKind::HistoryRoundTrip)
        {
            EXPECT_EQ(item.classification, fixtures::ExpectationClass::Exact)
                << item.id() << ": a history step restores every bit";
        }
        for (const std::uint32_t sample : item.differing)
        {
            const std::uint32_t width =
                item.kind == fixtures::MetadataSnapshotCaseKind::FrameKind
                    ? 1u
                    : 1u + 2u * static_cast<std::uint32_t>(std::size(fixtures::kCombinationKeywords));
            EXPECT_LT(sample, width) << item.id();
        }
    }
}

TEST(MetadataSnapshotCasesTest, TheHelpersReadBackWhatTheyWrote)
{
    EXPECT_EQ(fixtures::metadataCardValue(fixtures::FixtureCard{
                  .keyword = "OBSERVER", .value = "O'Brien", .kind = fixtures::FixtureCardKind::Text}),
              "'O''Brien'");
    EXPECT_EQ(fixtures::metadataCardValue(
                  fixtures::FixtureCard{.keyword = "GAIN", .value = "100", .kind = fixtures::FixtureCardKind::Integer}),
              "100");
    EXPECT_EQ(fixtures::metadataCardNumber(" '-10.0' "), std::optional<double>(-10.0));
    EXPECT_EQ(fixtures::metadataCardNumber("600.000000"), std::optional<double>(600.0));
    EXPECT_FALSE(fixtures::metadataCardNumber("'2026-01-10T20:00:00'").has_value());
    EXPECT_FALSE(fixtures::metadataCardNumber("").has_value());
    EXPECT_EQ(fixtures::frameKindCode("BIAS"), 0.0);
    EXPECT_EQ(fixtures::frameKindCode("LIGHT"), 3.0);
    EXPECT_EQ(fixtures::frameKindCode(""), -1.0);
    EXPECT_EQ(fixtures::frameKindCode("Light"), -1.0);
    EXPECT_TRUE(std::isnan(fixtures::metadataRow({std::nan("")}).value(0u, 0u, 0u)));
}
