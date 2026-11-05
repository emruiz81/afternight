// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for reading a frame's kind from the cards its codec preserved.
//
// The reading has three parts and each is pinned here: which words of a value name which kind, in which order
// they are tried; which cards are consulted, in which namespaces and in which order; and what the caller's
// fallback does. The spellings are the ones capture programs write - MaxIm, N.I.N.A., SGPro and APT write
// `IMAGETYP`, INDI writes `FRAME`, PixInsight's masters carry an XISF `imageType` - and the attribute's default,
// which the XISF codec preserves exactly as a stated value, is held to saying nothing.

#include <AfterNightEngine/IO/FrameTypeCards.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

/// A card in a namespace, built or the test fails.
[[nodiscard]] an::CodecCard card(an::CodecNamespace origin, std::string keyword, std::string value)
{
    an::Result<an::CodecCard> made = an::CodecCard::create(origin, std::move(keyword), std::move(value), "");
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : *an::CodecCard::create(an::CodecNamespace::Fits, "X", "", "");
}

/// A FITS card, built or the test fails.
[[nodiscard]] an::CodecCard fits(std::string keyword, std::string value)
{
    return card(an::CodecNamespace::Fits, std::move(keyword), std::move(value));
}

/// A card set holding these cards in order.
[[nodiscard]] an::CodecMetadata cards(std::vector<an::CodecCard> held)
{
    an::Result<an::CodecMetadata> made = an::CodecMetadata::create({}, std::move(held));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::CodecMetadata{};
}

} // namespace

TEST(FrameTypeCardsTest, ReadsTheKindEveryCaptureProgramSpells)
{
    struct Spelling
    {
        std::string_view text;
        an::FrameType kind;
    };
    const Spelling spellings[] = {
        {"Light", an::FrameType::Light},
        {"LIGHT", an::FrameType::Light},
        {"'Light Frame'", an::FrameType::Light},
        {"light frame", an::FrameType::Light},
        {"Science", an::FrameType::Light},
        {"'SCIENCE '", an::FrameType::Light},
        {"Dark", an::FrameType::Dark},
        {"'Dark Frame'", an::FrameType::Dark},
        {"Master Dark", an::FrameType::Dark},
        {"MasterDark", an::FrameType::Dark},
        {"Flat", an::FrameType::Flat},
        {"'Flat Field'", an::FrameType::Flat},
        {"Flat Frame", an::FrameType::Flat},
        {"MasterFlat", an::FrameType::Flat},
        {"Bias", an::FrameType::Bias},
        {"'Bias Frame'", an::FrameType::Bias},
        {"Master Bias", an::FrameType::Bias},
        {"Zero", an::FrameType::Bias},
        {"Offset", an::FrameType::Bias},
        {"MasterOffset", an::FrameType::Bias},
        // A dark taken for flats is a dark, whichever word comes first.
        {"Dark Flat", an::FrameType::Dark},
        {"FlatDark", an::FrameType::Dark},
        {"'Flat Dark'", an::FrameType::Dark},
        // The bias words come first of all.
        {"Bias Dark", an::FrameType::Bias},
        {"", an::FrameType::Unknown},
        {"-", an::FrameType::Unknown},
        {"''", an::FrameType::Unknown},
        {"DefectMap", an::FrameType::Unknown},
        {"RejectionMapHigh", an::FrameType::Unknown},
        {"Calibration", an::FrameType::Unknown},
        {"Lite", an::FrameType::Unknown},
    };
    for (const Spelling& spelling : spellings)
    {
        EXPECT_EQ(an::classifyFrameTypeText(spelling.text), spelling.kind) << '"' << spelling.text << '"';
    }
}

TEST(FrameTypeCardsTest, ConsultsTheImageTypeCardThenFrameThenTheXisfAttribute)
{
    const an::FrameTypeReading imageType =
        an::readFrameTypeCards(cards({fits("FRAME", "'Flat'"), fits("IMAGETYP", "'Dark Frame'")}));
    EXPECT_EQ(imageType,
              (an::FrameTypeReading{
                  .type = an::FrameType::Dark, .card = an::FrameTypeCard::ImageType, .spelling = "'Dark Frame'"}));

    const an::FrameTypeReading frame = an::readFrameTypeCards(
        cards({card(an::CodecNamespace::Xisf, "ImageType", "MasterBias"), fits("FRAME", "'Light'")}));
    EXPECT_EQ(frame, (an::FrameTypeReading{
                         .type = an::FrameType::Light, .card = an::FrameTypeCard::Frame, .spelling = "'Light'"}));

    const an::FrameTypeReading attribute =
        an::readFrameTypeCards(cards({card(an::CodecNamespace::Xisf, "ImageType", "MasterDark")}));
    EXPECT_EQ(attribute,
              (an::FrameTypeReading{
                  .type = an::FrameType::Dark, .card = an::FrameTypeCard::XisfImageType, .spelling = "MasterDark"}));
}

TEST(FrameTypeCardsTest, PassesOverACardThatNamesNoKind)
{
    const an::FrameTypeReading reading = an::readFrameTypeCards(
        cards({fits("IMAGETYP", "'Calibration'"), fits("FRAME", "'Flat'"), fits("OBJECT", "'Dark nebula'")}));
    EXPECT_EQ(reading.type, an::FrameType::Flat);
    EXPECT_EQ(reading.card, an::FrameTypeCard::Frame);
    EXPECT_EQ(reading.spelling, "'Flat'");

    const an::FrameTypeReading blank = an::readFrameTypeCards(cards({fits("IMAGETYP", "''"), fits("FRAME", "-")}));
    EXPECT_EQ(blank, an::FrameTypeReading{});
}

TEST(FrameTypeCardsTest, ReadsOnlyTheFirstCardOfEachKeyword)
{
    const an::FrameTypeReading first =
        an::readFrameTypeCards(cards({fits("IMAGETYP", "'Light'"), fits("IMAGETYP", "'Dark'")}));
    EXPECT_EQ(first.type, an::FrameType::Light);

    // A later card of a keyword whose first card names nothing is not consulted.
    const an::FrameTypeReading later =
        an::readFrameTypeCards(cards({fits("IMAGETYP", "'Junk'"), fits("IMAGETYP", "'Dark'")}));
    EXPECT_EQ(later, an::FrameTypeReading{});
}

TEST(FrameTypeCardsTest, AnXisfLightIsTheAttributesDefaultAndStatesNothing)
{
    EXPECT_EQ(an::readFrameTypeCards(cards({card(an::CodecNamespace::Xisf, "ImageType", "Light")})),
              an::FrameTypeReading{});

    const an::FrameTypeReading stated = an::readFrameTypeCards(
        cards({card(an::CodecNamespace::Xisf, "ImageType", "Light"), fits("IMAGETYP", "'Light Frame'")}));
    EXPECT_EQ(stated.type, an::FrameType::Light);
    EXPECT_EQ(stated.card, an::FrameTypeCard::ImageType);
}

TEST(FrameTypeCardsTest, ReadsEachKeywordInItsOwnNamespaceOnly)
{
    const an::FrameTypeReading foreign = an::readFrameTypeCards(cards({
        card(an::CodecNamespace::Xisf, "IMAGETYP", "Dark"),
        card(an::CodecNamespace::Tiff, "FRAME", "Flat"),
        card(an::CodecNamespace::Unspecified, "IMAGETYP", "Bias"),
        fits("ImageType", "MasterDark"),
        fits("imagetyp", "'Light'"),
    }));
    EXPECT_EQ(foreign, an::FrameTypeReading{});
}

TEST(FrameTypeCardsTest, FallsBackToTheCallersKindOnlyWhenNoCardStatesOne)
{
    const an::CodecMetadata stated = cards({fits("IMAGETYP", "'Bias'")});
    const an::CodecMetadata silent = cards({fits("OBJECT", "'M 31'")});
    for (const an::FrameType fallback :
         {an::FrameType::Unknown, an::FrameType::Light, an::FrameType::Dark, an::FrameType::Flat, an::FrameType::Bias})
    {
        EXPECT_EQ(an::resolveFrameType(stated, fallback), an::FrameType::Bias);
        EXPECT_EQ(an::resolveFrameType(silent, fallback), fallback);
        EXPECT_EQ(an::resolveFrameType(an::CodecMetadata{}, fallback), fallback);
    }
}

TEST(FrameTypeCardsTest, NamesItsCardsAndKeywords)
{
    const std::span<const std::string_view> keywords = an::frameTypeKeywords();
    ASSERT_EQ(keywords.size(), 3u);
    EXPECT_EQ(keywords[0], "IMAGETYP");
    EXPECT_EQ(keywords[1], "FRAME");
    EXPECT_EQ(keywords[2], "ImageType");

    EXPECT_STREQ(an::frameTypeCardName(an::FrameTypeCard::None), "None");
    EXPECT_STREQ(an::frameTypeCardName(an::FrameTypeCard::ImageType), "ImageType");
    EXPECT_STREQ(an::frameTypeCardName(an::FrameTypeCard::Frame), "Frame");
    EXPECT_STREQ(an::frameTypeCardName(an::FrameTypeCard::XisfImageType), "XisfImageType");
    EXPECT_STREQ(an::frameTypeCardName(static_cast<an::FrameTypeCard>(4u)), "UnknownFrameTypeCard");
    EXPECT_TRUE(an::isFrameTypeCard(an::FrameTypeCard::XisfImageType));
    EXPECT_FALSE(an::isFrameTypeCard(static_cast<an::FrameTypeCard>(4u)));
}

TEST(FrameTypeCardsTest, TheProjectionsOwnReadingIsUnchanged)
{
    // The codec projection keeps its own reading of IMAGETYP, which the frame-kind reading does not replace:
    // it reads the light words first and knows neither `Zero` nor `Science`.
    const an::CodecMetadata zero = cards({fits("IMAGETYP", "'Zero'")});
    EXPECT_EQ(an::projectAcquisition(zero).frameType, an::FrameType::Unknown);
    EXPECT_EQ(an::readFrameTypeCards(zero).type, an::FrameType::Bias);
}
