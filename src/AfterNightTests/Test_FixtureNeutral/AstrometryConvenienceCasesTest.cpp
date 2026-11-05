// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The astrometry conveniences' case table: what the engine fixture executable reads must be well formed before it
// compares.

#include "fixtures/common/AstrometryConvenienceCases.h"
#include "fixtures/common/FixtureCatalogue.h"

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>

namespace
{

namespace fixtures = an::tests::fixtures;

} // namespace

TEST(AstrometryConvenienceCasesTest, EveryInputIsUniqueMaterializesAndIsNoCatalogueFixture)
{
    std::set<std::string> names;
    for (const fixtures::FixtureImage& image : fixtures::astrometryConvenienceInputs())
    {
        EXPECT_TRUE(names.insert(image.name).second) << image.name << " is stated twice";
        EXPECT_EQ(fixtures::fixtureNamed(image.name), nullptr) << image.name << " is also a catalogue fixture";
        std::string error;
        EXPECT_TRUE(fixtures::materializeFixture(image, error).has_value()) << image.name << ": " << error;
        EXPECT_EQ(fixtures::astrometryConvenienceInput(image.name), &image) << image.name;
    }
    EXPECT_EQ(fixtures::astrometryConvenienceInput("no-such-input"), nullptr);
}

TEST(AstrometryConvenienceCasesTest, EveryCaseIsUniqueAndReadsTheInputItsKindNeeds)
{
    std::set<std::string> ids;
    for (const fixtures::AstrometryCase& item : fixtures::astrometryConvenienceCases())
    {
        EXPECT_TRUE(ids.insert(item.id()).second) << item.id() << " is stated twice";
        const fixtures::FixtureImage* input = fixtures::astrometryConvenienceInput(item.input);
        ASSERT_NE(input, nullptr) << item.id();
        EXPECT_FALSE(item.text.empty()) << item.id();
        EXPECT_TRUE(item.source.find(".cpp:") != std::string::npos || item.source.find(".h:") != std::string::npos)
            << item.id();
        EXPECT_EQ(fixtures::astrometryConvenienceCase(item.id()), &item) << item.id();
        switch (item.kind)
        {
        case fixtures::AstrometryCaseKind::ParseRightAscension:
        case fixtures::AstrometryCaseKind::ParseDeclination:
        case fixtures::AstrometryCaseKind::EpochYear:
        case fixtures::AstrometryCaseKind::HeaderCards:
            EXPECT_EQ(input->sampleType, fixtures::FixtureSampleType::UInt8) << item.id() << ": a text input";
            EXPECT_EQ(input->height, 1u) << item.id();
            EXPECT_TRUE(input->cards.empty()) << item.id();
            break;
        case fixtures::AstrometryCaseKind::FormatRightAscension:
        case fixtures::AstrometryCaseKind::FormatDeclination:
            EXPECT_EQ(input->sampleType, fixtures::FixtureSampleType::Float64) << item.id() << ": one angle";
            EXPECT_EQ(input->width, 1u) << item.id();
            break;
        case fixtures::AstrometryCaseKind::MosaicCentre:
            EXPECT_EQ(input->sampleType, fixtures::FixtureSampleType::Float64) << item.id() << ": pairs of angles";
            EXPECT_EQ(input->width % 2u, 0u) << item.id();
            break;
        case fixtures::AstrometryCaseKind::PlateSolveHint:
        case fixtures::AstrometryCaseKind::ObservedPointing:
        case fixtures::AstrometryCaseKind::ScaleHint:
            EXPECT_FALSE(input->cards.empty()) << item.id() << ": a header's cards";
            EXPECT_EQ(item.solvedScale, 0.0) << item.id();
            break;
        case fixtures::AstrometryCaseKind::DerivedFocalLength:
            EXPECT_FALSE(input->cards.empty()) << item.id();
            EXPECT_GT(item.solvedScale, 0.0) << item.id() << ": a solved scale";
            break;
        case fixtures::AstrometryCaseKind::DetectionPolicy:
            EXPECT_EQ(input->pattern, fixtures::FixturePattern::StarField) << item.id();
            break;
        }
    }
    EXPECT_EQ(fixtures::astrometryConvenienceCase("hint.none/none"), nullptr);
}

TEST(AstrometryConvenienceCasesTest, AClassificationCarriesWhatItReads)
{
    for (const fixtures::AstrometryCase& item : fixtures::astrometryConvenienceCases())
    {
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, !item.acceptedReason.empty())
            << item.id();
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Accepted, item.acceptedBound > 0.0) << item.id();
        // The conveniences agree exactly or differ on purpose, but for the mosaic centre, whose answer the host's
        // trigonometric functions round and two compilers' libraries can place one unit apart in the last place.
        EXPECT_EQ(item.classification == fixtures::ExpectationClass::Tolerance,
                  item.kind == fixtures::AstrometryCaseKind::MosaicCentre)
            << item.id();
        if (item.classification == fixtures::ExpectationClass::Tolerance)
        {
            EXPECT_GT(item.absoluteTolerance, 0.0) << item.id();
            EXPECT_LE(item.absoluteTolerance, 1.0e-12)
                << item.id() << ": a tolerance is a last place, not a difference";
            EXPECT_EQ(item.relativeTolerance, 0.0) << item.id();
            EXPECT_FALSE(item.note.empty()) << item.id() << ": a tolerance says why";
        }
        if (item.classification == fixtures::ExpectationClass::Accepted)
        {
            EXPECT_FALSE(item.note.empty()) << item.id() << ": a bound says why";
        }
    }
}

TEST(AstrometryConvenienceCasesTest, TheInputHelpersReadBackWhatTheyWrote)
{
    const fixtures::FixtureImage* text = fixtures::astrometryConvenienceInput("dec-degree-sign");
    ASSERT_NE(text, nullptr);
    std::string error;
    const std::optional<fixtures::FixtureSamples> textSamples = fixtures::materializeFixture(*text, error);
    ASSERT_TRUE(textSamples.has_value()) << error;
    EXPECT_EQ(fixtures::astrometryInputText(*textSamples), "22\xC2\xB0"
                                                           "00'52.2\"");

    const fixtures::FixtureImage* numbers = fixtures::astrometryConvenienceInput("panels-across-zero-hours");
    ASSERT_NE(numbers, nullptr);
    const std::optional<fixtures::FixtureSamples> numberSamples = fixtures::materializeFixture(*numbers, error);
    ASSERT_TRUE(numberSamples.has_value()) << error;
    EXPECT_EQ(fixtures::astrometryInputNumbers(*numberSamples), (std::vector<double>{359.5, 10.0, 0.5, 10.0}));

    EXPECT_EQ(fixtures::astrometryCardValue(fixtures::FixtureCard{
                  .keyword = "OBSERVER", .value = "O'Brien", .kind = fixtures::FixtureCardKind::Text}),
              "'O''Brien'");
    EXPECT_EQ(fixtures::astrometryCardValue(
                  fixtures::FixtureCard{.keyword = "RA", .value = "83.5", .kind = fixtures::FixtureCardKind::Real}),
              "83.5");
    EXPECT_EQ(fixtures::astrometryCardNumber(" '4.63' "), std::optional<double>(4.63));
    EXPECT_EQ(fixtures::astrometryCardNumber("+1.0245E+003"), std::optional<double>(1024.5));
    EXPECT_FALSE(fixtures::astrometryCardNumber("'RA---TAN'").has_value());
    EXPECT_FALSE(fixtures::astrometryCardNumber("inf").has_value());
    EXPECT_FALSE(fixtures::astrometryCardNumber("").has_value());
    EXPECT_EQ(fixtures::astrometryTextRow("ab").value(1u, 0u, 0u), 98.0);
    EXPECT_TRUE(std::isnan(fixtures::astrometryRow({std::nan("")}).value(0u, 0u, 0u)));
}
