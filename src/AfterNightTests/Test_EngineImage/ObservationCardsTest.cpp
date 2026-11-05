// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/IO/ObservationCards.h.
//
// The observing context a derived FITS product carries: copied verbatim from a FITS source, derived
// from a camera file's Exif, and merged into a destination under a keep-or-replace rule. Every expected
// value is written out here from the rule it follows - a number formatted with at most six decimals, a
// string quoted with inner quotes doubled - rather than read back from the function under test.

#include <AfterNightEngine/IO/ObservationCards.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::CodecCard fits(std::string keyword, std::string value, std::string comment = {})
{
    return *an::CodecCard::create(an::CodecNamespace::Fits, std::move(keyword), std::move(value), std::move(comment));
}

[[nodiscard]] an::CodecCard exif(std::string keyword, std::string value)
{
    return *an::CodecCard::create(an::CodecNamespace::Jpeg, std::move(keyword), std::move(value), {});
}

[[nodiscard]] an::CodecMetadata metadataOf(std::vector<an::CodecCard> cards)
{
    an::Result<an::CodecMetadata> made = an::CodecMetadata::create({}, std::move(cards));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::CodecMetadata{};
}

/// The value of the first FITS card with this keyword, or an absent marker.
[[nodiscard]] std::string valueOf(const an::CodecMetadata& metadata, std::string_view keyword)
{
    const an::CodecCard* card = metadata.find(an::CodecNamespace::Fits, keyword);
    return card != nullptr ? card->value() : std::string("<absent>");
}

/// How many FITS cards carry this keyword.
[[nodiscard]] std::size_t countOf(const an::CodecMetadata& metadata, std::string_view keyword)
{
    return static_cast<std::size_t>(
        std::count_if(metadata.cards().begin(), metadata.cards().end(), [&](const an::CodecCard& card) {
            return card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword;
        }));
}

/// A number the way the carried cards spell one: six decimals, then no trailing zero or point.
[[nodiscard]] std::string sixDecimals(double value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(6) << value;
    std::string text = stream.str();
    while (text.size() > 1u && text.back() == '0')
    {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.')
    {
        text.pop_back();
    }
    return text;
}

/// The FITS value one card set gives a keyword once carried onto nothing.
[[nodiscard]] std::string carriedValue(std::vector<an::CodecCard> source, std::string_view keyword)
{
    const an::Result<an::CodecMetadata> carried =
        an::withObservationCards(an::CodecMetadata{}, metadataOf(std::move(source)));
    EXPECT_TRUE(carried.hasValue()) << carried.status().message();
    return carried.hasValue() ? valueOf(*carried, keyword) : std::string("<failed>");
}

TEST(ObservationCardsTest, TheCopiedKeywordsAreTheSiteOpticsPointingSensorAndFocuser)
{
    const std::span<const std::string_view> keywords = an::copiedObservationKeywords();
    ASSERT_EQ(keywords.size(), 44u);
    EXPECT_EQ(keywords.front(), "TELESCOP");
    EXPECT_EQ(keywords.back(), "FOCUSTEM");
    for (const std::string_view keyword : {"FOCALLEN", "SITELAT", "OBJCTRA", "PIERSIDE", "SET-TEMP", "FOCPOS"})
    {
        EXPECT_NE(std::find(keywords.begin(), keywords.end(), keyword), keywords.end()) << keyword;
    }
    for (const std::string_view keyword : {"NAXIS1", "CRPIX1", "CTYPE1", "BITPIX", "EXPOSURE"})
    {
        EXPECT_EQ(std::find(keywords.begin(), keywords.end(), keyword), keywords.end()) << keyword;
    }
    EXPECT_TRUE(an::isObservationCardPolicy(an::ObservationCardPolicy::KeepExisting));
    EXPECT_TRUE(an::isObservationCardPolicy(an::ObservationCardPolicy::Replace));
    EXPECT_FALSE(an::isObservationCardPolicy(static_cast<an::ObservationCardPolicy>(2u)));
}

TEST(ObservationCardsTest, AFitsSourceHandsOverItsContextVerbatimAndItsFactsAsTheyWereRead)
{
    const an::CodecMetadata source = metadataOf({
        fits("SIMPLE", "T"),
        fits("NAXIS1", "4144"),
        fits("TELESCOP", "'C11 EdgeHD'", "telescope"),
        fits("FOCALLEN", "2800."),
        fits("XBINNING", "2"),
        fits("SITELAT", "'+35 12 34.56'"),
        fits("OBSGEO-H", "1200"),
        fits("CAMMAKE", "ZWO"),
        fits("DATE-OBS", "'2024-05-01T20:31:02.123'"),
        fits("EXPTIME", "120.0"),
        fits("GAIN", "100"),
        fits("OFFSET", "30"),
        fits("INSTRUME", "'ASI2600MM Pro'"),
        fits("FILTER", "'Ha'"),
        fits("IMAGETYP", "'Light Frame'"),
        fits("OBJECT", "'NGC 7000'"),
        fits("SWCREATE", "'N.I.N.A.'"),
        fits("CCD-TEMP", "-10.0"),
        fits("DATE", "'2024-05-01T20:40:00'"),
        fits("CRPIX1", "2072.0"),
    });
    const an::CodecMetadata destination = metadataOf({fits("BITPIX", "-32"), fits("NAXIS1", "4144")});

    const an::Result<an::CodecMetadata> carried = an::withObservationCards(destination, source);
    ASSERT_TRUE(carried.hasValue()) << carried.status().message();

    EXPECT_EQ(valueOf(*carried, "TELESCOP"), "'C11 EdgeHD'");
    EXPECT_EQ(valueOf(*carried, "FOCALLEN"), "2800.");
    EXPECT_EQ(valueOf(*carried, "XBINNING"), "2");
    EXPECT_EQ(valueOf(*carried, "SITELAT"), "'+35 12 34.56'");
    EXPECT_EQ(valueOf(*carried, "OBSGEO-H"), "1200");
    EXPECT_EQ(valueOf(*carried, "CAMMAKE"), "'ZWO'");
    EXPECT_EQ(valueOf(*carried, "DATE-OBS"), "'2024-05-01T20:31:02.123'");
    EXPECT_EQ(valueOf(*carried, "EXPOSURE"), "120");
    EXPECT_EQ(valueOf(*carried, "GAIN"), "100");
    EXPECT_EQ(valueOf(*carried, "ISOSPEED"), "<absent>");
    EXPECT_EQ(valueOf(*carried, "OFFSET"), "30");
    EXPECT_EQ(valueOf(*carried, "INSTRUME"), "'ASI2600MM Pro'");
    EXPECT_EQ(valueOf(*carried, "FILTER"), "'Ha'");
    EXPECT_EQ(valueOf(*carried, "IMAGETYP"), "'LIGHT FRAME'");
    EXPECT_EQ(valueOf(*carried, "OBJECT"), "'NGC 7000'");
    // The value is read with quotes, spaces and dots stripped from either end.
    EXPECT_EQ(valueOf(*carried, "SWCREATE"), "'N.I.N.A'");
    EXPECT_EQ(valueOf(*carried, "CCD-TEMP"), "-10.0");
    // The creation time fills DATE-LOC; DATE-OBS was stated by the copy and is kept.
    EXPECT_EQ(valueOf(*carried, "DATE-LOC"), "'2024-05-01T20:40:00'");
    // A product's geometry and structure are its own.
    EXPECT_EQ(valueOf(*carried, "CRPIX1"), "<absent>");
    EXPECT_EQ(valueOf(*carried, "BITPIX"), "-32");
    EXPECT_EQ(countOf(*carried, "NAXIS1"), 1u);
    // Nothing from a camera file is derived from a FITS source.
    EXPECT_EQ(valueOf(*carried, "APTDIA"), "<absent>");

    // The context is appended after what the destination held, in the order the rule states it.
    ASSERT_GE(carried->cards().size(), 3u);
    EXPECT_EQ(carried->cards()[0].keyword(), "BITPIX");
    EXPECT_EQ(carried->cards()[1].keyword(), "NAXIS1");
    EXPECT_EQ(carried->cards()[2].keyword(), "TELESCOP");
    EXPECT_EQ(carried->cards()[2].comment(), "");
}

TEST(ObservationCardsTest, ACameraFilesContextIsDerivedFromItsExif)
{
    const an::CodecMetadata source = metadataOf({
        exif("Exif.Image.Make", "Canon"),
        exif("Exif.Image.Model", "Canon EOS R5"),
        exif("Exif.Photo.ExposureTime", "1/200"),
        exif("Exif.Photo.ISOSpeedRatings", "800"),
        exif("Exif.Image.Software", "Firmware 1.8.1"),
        exif("Exif.Photo.DateTimeOriginal", "2024:05:01 20:31:02"),
        exif("Exif.Photo.LensModel", "RF100-500mm F4.5-7.1 L IS USM"),
        exif("Exif.Photo.FocalLength", "500/1"),
        exif("Exif.Photo.FNumber", "71/10"),
        exif("Exif.Photo.FocalPlaneXResolution", "2400000/1000"),
        exif("Exif.Photo.FocalPlaneYResolution", "1600/1"),
        exif("Exif.Photo.FocalPlaneResolutionUnit", "3"),
        exif("Exif.GPSInfo.GPSLatitude", "35/1 12/1 3456/100"),
        exif("Exif.GPSInfo.GPSLatitudeRef", "S"),
        exif("Exif.GPSInfo.GPSLongitude", "138/1 43/1 0/1"),
        exif("Exif.GPSInfo.GPSLongitudeRef", "E"),
        exif("Exif.GPSInfo.GPSAltitude", "1200/1"),
        exif("Exif.GPSInfo.GPSAltitudeRef", "1"),
        exif("Exif.GPSInfo.GPSDateStamp", "2024:05:01"),
        exif("Exif.GPSInfo.GPSTimeStamp", "11/1 31/1 2/1"),
        exif("Exif.MakerNote.SensorTemperature", "32"),
    });
    const an::CodecMetadata destination = metadataOf({});

    const an::Result<an::CodecMetadata> kept = an::withObservationCards(destination, source);
    ASSERT_TRUE(kept.hasValue()) << kept.status().message();
    EXPECT_EQ(valueOf(*kept, "EXPOSURE"), "0.005");
    EXPECT_EQ(valueOf(*kept, "ISOSPEED"), "800");
    EXPECT_EQ(valueOf(*kept, "GAIN"), "<absent>");
    EXPECT_EQ(valueOf(*kept, "OFFSET"), "<absent>");
    EXPECT_EQ(valueOf(*kept, "INSTRUME"), "'Canon EOS R5'");
    EXPECT_EQ(valueOf(*kept, "SWCREATE"), "'Firmware 1.8.1'");
    EXPECT_EQ(valueOf(*kept, "CCD-TEMP"), "32");
    EXPECT_EQ(valueOf(*kept, "DATE-LOC"), "'2024-05-01T20:31:02'");
    // The creation time states DATE-OBS first, and a kept value is not replaced by the GPS time.
    EXPECT_EQ(valueOf(*kept, "DATE-OBS"), "'2024-05-01T20:31:02'");
    EXPECT_EQ(valueOf(*kept, "CAMMAKE"), "'Canon'");
    EXPECT_EQ(valueOf(*kept, "LENSMODE"), "'RF100-500mm F4.5-7.1 L IS USM'");
    EXPECT_EQ(valueOf(*kept, "FOCALLEN"), "500");
    const double aperture = 500.0 / 7.1;
    EXPECT_EQ(valueOf(*kept, "APTDIA"), sixDecimals(aperture));
    EXPECT_EQ(valueOf(*kept, "APTAREA"), sixDecimals(std::numbers::pi * 0.25 * aperture * aperture));
    // Ten millimetres to a centimetre, a thousand micrometres to a millimetre, over the resolution.
    EXPECT_EQ(valueOf(*kept, "XPIXSZ"), sixDecimals(10.0 * 1000.0 / 2400.0));
    EXPECT_EQ(valueOf(*kept, "YPIXSZ"), sixDecimals(10.0 * 1000.0 / 1600.0));
    EXPECT_EQ(valueOf(*kept, "SITELAT"), sixDecimals(-(35.0 + 12.0 / 60.0 + 34.56 / 3600.0)));
    EXPECT_EQ(valueOf(*kept, "SITELONG"), sixDecimals(138.0 + 43.0 / 60.0));
    EXPECT_EQ(valueOf(*kept, "SITEELEV"), "-1200");
    // A camera file hands over no FITS keyword verbatim and has no filter, object or frame type.
    EXPECT_EQ(valueOf(*kept, "TELESCOP"), "<absent>");
    EXPECT_EQ(valueOf(*kept, "FILTER"), "<absent>");
    EXPECT_EQ(valueOf(*kept, "OBJECT"), "<absent>");
    EXPECT_EQ(valueOf(*kept, "IMAGETYP"), "<absent>");

    const an::Result<an::CodecMetadata> replaced =
        an::withObservationCards(destination, source, an::ObservationCardPolicy::Replace);
    ASSERT_TRUE(replaced.hasValue()) << replaced.status().message();
    // Under replacement the GPS time, stated last, is the one that stands.
    EXPECT_EQ(valueOf(*replaced, "DATE-OBS"), "'2024-05-01T11:31:02'");
    EXPECT_EQ(countOf(*replaced, "DATE-OBS"), 1u);
}

TEST(ObservationCardsTest, ADestinationValueIsKeptOrReplacedInPlaceAndDuplicatesGoWithIt)
{
    const an::CodecMetadata source =
        metadataOf({fits("EXPOSURE", "300"), fits("FILTER", "'OIII'"), fits("OBJECT", "'M 31'"), fits("GAIN", "120")});
    const an::CodecMetadata destination =
        metadataOf({fits("EXPOSURE", "60", "seconds"), fits("FILTER", "'Ha'"), fits("HISTORY", "stacked"),
                    fits("FILTER", "'Ha duplicate'"), fits("OBJECT", "'-'"), fits("ISOSPEED", "''"),
                    *an::CodecCard::create(an::CodecNamespace::Tiff, "Exif.Image.Artist", "someone", {})});

    const an::Result<an::CodecMetadata> kept = an::withObservationCards(destination, source);
    ASSERT_TRUE(kept.hasValue()) << kept.status().message();
    EXPECT_EQ(valueOf(*kept, "EXPOSURE"), "60");
    EXPECT_EQ(valueOf(*kept, "FILTER"), "'Ha'");
    EXPECT_EQ(countOf(*kept, "FILTER"), 2u);
    // A "-" and an empty string state no value, so they are filled, in place.
    EXPECT_EQ(valueOf(*kept, "OBJECT"), "'M 31'");
    ASSERT_GE(kept->cards().size(), 5u);
    EXPECT_EQ(kept->cards()[4].keyword(), "OBJECT");
    // An empty ISOSPEED states no value either, so the gain is written beside it.
    EXPECT_EQ(valueOf(*kept, "GAIN"), "120");

    const an::Result<an::CodecMetadata> replaced =
        an::withObservationCards(destination, source, an::ObservationCardPolicy::Replace);
    ASSERT_TRUE(replaced.hasValue()) << replaced.status().message();
    ASSERT_GE(replaced->cards().size(), 5u);
    EXPECT_EQ(replaced->cards()[0].keyword(), "EXPOSURE");
    EXPECT_EQ(replaced->cards()[0].value(), "300");
    EXPECT_EQ(replaced->cards()[0].comment(), "seconds");
    EXPECT_EQ(replaced->cards()[1].keyword(), "FILTER");
    EXPECT_EQ(replaced->cards()[1].value(), "'OIII'");
    EXPECT_EQ(countOf(*replaced, "FILTER"), 1u);
    EXPECT_EQ(replaced->cards()[2].keyword(), "HISTORY");
    // A card of another namespace is kept exactly.
    const an::CodecCard* artist = replaced->find(an::CodecNamespace::Tiff, "Exif.Image.Artist");
    ASSERT_NE(artist, nullptr);
    EXPECT_EQ(artist->value(), "someone");
}

TEST(ObservationCardsTest, NothingIsWrittenForAValueThatIsNotThereAndAnUnknownPolicyIsRefused)
{
    const an::CodecMetadata source =
        metadataOf({fits("FILTER", "'-'"), fits("OBJECT", "''"), fits("EXPTIME", "unknown"), fits("TELESCOP", "   ")});
    const an::CodecMetadata destination = metadataOf({fits("BITPIX", "16")});
    const an::Result<an::CodecMetadata> carried = an::withObservationCards(destination, source);
    ASSERT_TRUE(carried.hasValue()) << carried.status().message();
    EXPECT_EQ(carried->cards().size(), 1u);

    const an::Result<an::CodecMetadata> nothing = an::withObservationCards(destination, an::CodecMetadata{});
    ASSERT_TRUE(nothing.hasValue()) << nothing.status().message();
    EXPECT_EQ(*nothing, destination);

    EXPECT_EQ(an::withObservationCards(destination, source, static_cast<an::ObservationCardPolicy>(2u)).status().code(),
              an::ErrorCode::InvalidArgument);

    // Only "-" is the marker for no value; any other text is a value.
    EXPECT_EQ(carriedValue({fits("OBJECT", "'N/A'")}, "OBJECT"), "'N/A'");

    // A value with a quote inside is quoted with that quote doubled.
    EXPECT_EQ(carriedValue({exif("Exif.Image.Model", "O'Brien Cam")}, "INSTRUME"), "'O''Brien Cam'");
}

TEST(ObservationCardsTest, AnIntegerIsTheOneItsTextBeginsWithAndTextWithoutOneStatesNone)
{
    EXPECT_EQ(carriedValue({fits("GAIN", "'139 e-/ADU'")}, "GAIN"), "139");
    EXPECT_EQ(carriedValue({fits("OFFSET", "+21")}, "OFFSET"), "21");

    // A stated ISO speed is the one read even when it holds no integer, so the gain beside it is not.
    const std::vector<an::CodecCard> automatic = {fits("ISOSPEED", "'auto'"), fits("GAIN", "100"),
                                                  fits("OFFSET", "unknown")};
    EXPECT_EQ(carriedValue(automatic, "ISOSPEED"), "<absent>");
    EXPECT_EQ(carriedValue(automatic, "GAIN"), "<absent>");
    EXPECT_EQ(carriedValue(automatic, "OFFSET"), "<absent>");

    // A camera file states no offset, even beside a FITS card that does; its filter is read from FITS cards
    // whatever else the source holds.
    const std::vector<an::CodecCard> mixed = {exif("Exif.Photo.ISOSpeedRatings", "400"), fits("OFFSET", "30"),
                                              fits("FILTER", "'L'")};
    EXPECT_EQ(carriedValue(mixed, "ISOSPEED"), "400");
    EXPECT_EQ(carriedValue(mixed, "OFFSET"), "<absent>");
    EXPECT_EQ(carriedValue(mixed, "FILTER"), "'L'");
}

TEST(ObservationCardsTest, ATimeIsReadInItsSpellingsAndOneThatNamesNoInstantStatesNothing)
{
    EXPECT_EQ(carriedValue({fits("DATE", "'2024:05:01 20:31:02'")}, "DATE-LOC"), "'2024-05-01T20:31:02'");
    EXPECT_EQ(carriedValue({fits("DATE", "'2024-05-01 20:31:02'")}, "DATE-LOC"), "'2024-05-01T20:31:02'");
    EXPECT_EQ(carriedValue({fits("DATE", "'2024-05-01T20:31:02.5Z'")}, "DATE-LOC"), "'2024-05-01T20:31:02'");
    EXPECT_EQ(carriedValue({fits("DATE", "'2024-02-29T23:59:60'")}, "DATE-LOC"), "'2024-02-29T23:59:60'");
    // DATE is read before DATE-LOC, and a DATE that states nothing gives way to it.
    EXPECT_EQ(carriedValue({fits("DATE", "'-'"), fits("DATE-LOC", "'2024-05-02T01:00:00'")}, "DATE-OBS"),
              "'2024-05-02T01:00:00'");
    EXPECT_EQ(carriedValue({fits("DATE", "'2023-02-29T10:00:00'")}, "DATE-LOC"), "<absent>");
    EXPECT_EQ(carriedValue({fits("DATE", "'2024-05-01T24:00:00'")}, "DATE-LOC"), "<absent>");
    EXPECT_EQ(carriedValue({fits("DATE", "'2024-5-1T20:31:02'")}, "DATE-LOC"), "<absent>");
    EXPECT_EQ(carriedValue({fits("DATE", "'yesterday'")}, "DATE-LOC"), "<absent>");

    const auto gpsTime = [](std::string date, std::string time) {
        return carriedValue(
            {exif("Exif.GPSInfo.GPSDateStamp", std::move(date)), exif("Exif.GPSInfo.GPSTimeStamp", std::move(time))},
            "DATE-OBS");
    };
    EXPECT_EQ(gpsTime("2024:05:01", "23/1 59/1 595/10"), "'2024-05-01T23:59:60'");
    EXPECT_EQ(gpsTime("2024-05-01", "7/1"), "'2024-05-01T07:00:00'");
    EXPECT_EQ(gpsTime("2024:05:01", "25/1 0/1 0/1"), "<absent>");
    EXPECT_EQ(gpsTime("2024:13:01", "1/1 0/1 0/1"), "<absent>");
    EXPECT_EQ(gpsTime("2024:05:01", "no time"), "<absent>");
}

TEST(ObservationCardsTest, WhatOneImageCannotPreserveIsRefusedByName)
{
    // Quoting adds two characters to a model that fills all but one byte of a card.
    const an::Result<an::CodecMetadata> tooLong = an::withObservationCards(
        an::CodecMetadata{}, metadataOf({exif("Exif.Image.Model", std::string(an::kMaxCodecCardTextBytes - 1u, 'x'))}));
    EXPECT_EQ(tooLong.status().code(), an::ErrorCode::LimitExceeded);

    std::vector<an::CodecCard> full;
    full.reserve(an::kMaxCodecCards);
    full.push_back(fits("FILTER", "'L'"));
    for (std::size_t i = 1u; i < an::kMaxCodecCards; ++i)
    {
        full.push_back(fits("HISTORY", "line " + std::to_string(i)));
    }
    const an::CodecMetadata fullDestination = metadataOf(std::move(full));
    const an::Result<an::CodecMetadata> tooMany =
        an::withObservationCards(fullDestination, metadataOf({fits("OBJECT", "'M 42'")}));
    EXPECT_EQ(tooMany.status().code(), an::ErrorCode::LimitExceeded);

    // A value that replaces a card in place needs no room.
    const an::Result<an::CodecMetadata> inPlace = an::withObservationCards(
        fullDestination, metadataOf({fits("FILTER", "'Ha'")}), an::ObservationCardPolicy::Replace);
    ASSERT_TRUE(inPlace.hasValue()) << inPlace.status().message();
    EXPECT_EQ(inPlace->cards().size(), an::kMaxCodecCards);
    EXPECT_EQ(valueOf(*inPlace, "FILTER"), "'Ha'");
}

} // namespace
