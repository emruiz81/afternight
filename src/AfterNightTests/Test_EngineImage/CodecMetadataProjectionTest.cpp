// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>

#include <gtest/gtest.h>

#include "CodecAllocationFault.h"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace
{
an::CodecCard card(an::CodecNamespace origin, const char* keyword, const char* value)
{
    return *an::CodecCard::create(origin, keyword, value, "");
}

const an::CodecCard* findCard(const std::vector<an::CodecCard>& cards, std::string_view keyword)
{
    for (const auto& value : cards)
        if (value.keyword() == keyword)
            return &value;
    return nullptr;
}

an::CodecMetadata fitsFixture()
{
    using an::CodecNamespace;
    return *an::CodecMetadata::create({}, {card(CodecNamespace::Fits, "SIMPLE", "T"),
                                           card(CodecNamespace::Fits, "INSTRUME", "'ASI294MM Pro'"),
                                           card(CodecNamespace::Fits, "TELESCOP", "'Esprit 100'"),
                                           card(CodecNamespace::Fits, "OBSERVER", "'E. Ruiz'"),
                                           card(CodecNamespace::Fits, "OBJECT", "'M42'"),
                                           card(CodecNamespace::Fits, "FILTER", "'Ha'"),
                                           card(CodecNamespace::Fits, "EXPOSURE", "120.0"),
                                           card(CodecNamespace::Fits, "EXPTIME", "999"),
                                           card(CodecNamespace::Fits, "CCD-TEMP", "-10.5"),
                                           card(CodecNamespace::Fits, "EGAIN", "0.4"),
                                           card(CodecNamespace::Fits, "XBINNING", "2"),
                                           card(CodecNamespace::Fits, "YBINNING", "2"),
                                           card(CodecNamespace::Fits, "DATE-OBS", "'2026-09-10T21:14:05.250'"),
                                           card(CodecNamespace::Fits, "ISOSPEED", "800"),
                                           card(CodecNamespace::Fits, "GAIN", "120"),
                                           card(CodecNamespace::Fits, "OFFSET", "30"),
                                           card(CodecNamespace::Fits, "IMAGETYP", "'Light Frame'"),
                                           card(CodecNamespace::Fits, "SWCREATE", "'N.I.N.A.'"),
                                           card(CodecNamespace::Fits, "DATE", "'2026-09-10T21:20:00'"),
                                           card(CodecNamespace::Fits, "DATE-LOC", "'2026-09-10T18:20:00'")});
}

TEST(CodecMetadataProjectionTest, LongFrameTypeRecognitionPreservesCaseInsensitivePrecedence)
{
    for (const auto& [name, expected] : {std::pair{"lIgHt", an::FrameType::Light},
                                         {"dArK", an::FrameType::Dark},
                                         {"fLaT", an::FrameType::Flat},
                                         {"bIaS", an::FrameType::Bias},
                                         {"OfFsEt", an::FrameType::Bias}})
    {
        const auto text = std::string(1024u, ' ') + name;
        const auto source = *an::CodecMetadata::create({}, {card(an::CodecNamespace::Fits, "IMAGETYP", text.c_str())});
        EXPECT_EQ(an::projectAcquisition(source).frameType, expected);
    }
}

#if defined(_MSC_VER) && defined(_DEBUG)
TEST(CodecMetadataProjectionTest, AllocationFailuresReachTheBoundaryInsteadOfTerminating)
{
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(77); });
            const std::string text = "LIGHT" + std::string(1024u, 'x');
            for (const auto* keyword : {"IMAGETYP", "ISOSPEED", "XBINNING"})
            {
                const auto source =
                    *an::CodecMetadata::create({}, {card(an::CodecNamespace::Fits, keyword, text.c_str()),
                                                    card(an::CodecNamespace::Fits, "YBINNING", "1")});
                for (std::size_t ordinal = 1u; ordinal <= 12u; ++ordinal)
                {
                    codec_test::AllocationFault fault(ordinal);
                    const auto result = an::evaluateAcrossBoundary({"metadata-test", "project", an::ErrorDomain::Io},
                                                                   [&] { return an::projectAcquisition(source); });
                    if (!result.hasValue() && result.status().code() != an::ErrorCode::AllocationFailed)
                        std::_Exit(1);
                }
            }
            std::_Exit(0);
        },
        testing::ExitedWithCode(0), "");
}
#endif

TEST(CodecMetadataProjectionTest, ProjectsEveryFitsSpellingIntoTypedFacts)
{
    const auto projection = an::projectAcquisition(fitsFixture());
    EXPECT_EQ(projection.acquisition.instrumentName, "ASI294MM Pro");
    EXPECT_EQ(projection.acquisition.telescopeName, "Esprit 100");
    EXPECT_EQ(projection.acquisition.observerName, "E. Ruiz");
    EXPECT_EQ(projection.acquisition.objectName, "M42");
    EXPECT_EQ(projection.acquisition.filterName, "Ha");
    ASSERT_TRUE(projection.acquisition.exposureSeconds.has_value());
    EXPECT_DOUBLE_EQ(*projection.acquisition.exposureSeconds, 120.0);
    ASSERT_TRUE(projection.acquisition.sensorTemperatureCelsius.has_value());
    EXPECT_DOUBLE_EQ(*projection.acquisition.sensorTemperatureCelsius, -10.5);
    ASSERT_TRUE(projection.acquisition.gainElectronsPerAdu.has_value());
    EXPECT_DOUBLE_EQ(*projection.acquisition.gainElectronsPerAdu, 0.4);
    ASSERT_TRUE(projection.acquisition.binning.has_value());
    EXPECT_EQ(projection.acquisition.binning->horizontal, 2u);
    EXPECT_EQ(projection.acquisition.binning->vertical, 2u);
    ASSERT_TRUE(projection.acquisition.exposureStart.has_value());
    EXPECT_EQ(projection.acquisition.exposureStart->nanosecondsSinceEpoch, 1789074845250000000LL);
    EXPECT_EQ(projection.isoSpeed, 800);
    EXPECT_EQ(projection.gainSetting, 120);
    EXPECT_EQ(projection.cameraOffset, 30);
    EXPECT_EQ(projection.frameType, an::FrameType::Light);
    EXPECT_EQ(projection.frameTypeText, "Light Frame");
    EXPECT_EQ(projection.software, "N.I.N.A.");
    ASSERT_TRUE(projection.creationTime.has_value());
    EXPECT_EQ(projection.creationTime->nanosecondsSinceEpoch, 1789075200000000000LL);
    EXPECT_EQ(projection.consultedCards, 17u);
    EXPECT_EQ(projection.malformedCards, 0u);
    const auto typed = an::AcquisitionMetadata::create(projection.acquisition);
    ASSERT_TRUE(typed.hasValue()) << typed.status().message();
    EXPECT_EQ(typed->instrumentName(), "ASI294MM Pro");
}

TEST(CodecMetadataProjectionTest, FallbackSpellingsMalformedValuesAndFrameTypesAreHandledOneByOne)
{
    using an::CodecNamespace;
    const auto fallback = an::projectAcquisition(*an::CodecMetadata::create(
        {},
        {card(CodecNamespace::Fits, "EXPOSURE", "abc"), card(CodecNamespace::Fits, "EXPTIME", "30"),
         card(CodecNamespace::Fits, "SOFTWARE", "'PixInsight'"), card(CodecNamespace::Fits, "SWCREATE", "'N.I.N.A.'"),
         card(CodecNamespace::Fits, "DATE-LOC", "'2026-09-10 21:15:00'"), card(CodecNamespace::Fits, "XBINNING", "2"),
         card(CodecNamespace::Fits, "IMAGETYP", "'Offset'"), card(CodecNamespace::Fits, "ISOSPEED", "'x'"),
         card(CodecNamespace::Fits, "GAIN", "'120'"), card(CodecNamespace::Fits, "EGAIN", "0")}));
    ASSERT_TRUE(fallback.acquisition.exposureSeconds.has_value());
    EXPECT_DOUBLE_EQ(*fallback.acquisition.exposureSeconds, 30.0);
    EXPECT_EQ(fallback.software, "PixInsight");
    ASSERT_TRUE(fallback.creationTime.has_value());
    EXPECT_EQ(fallback.creationTime->nanosecondsSinceEpoch, 1789074900000000000LL);
    EXPECT_FALSE(fallback.acquisition.binning.has_value());
    EXPECT_EQ(fallback.frameType, an::FrameType::Bias);
    EXPECT_EQ(fallback.frameTypeText, "Offset");
    EXPECT_FALSE(fallback.isoSpeed.has_value());
    EXPECT_EQ(fallback.gainSetting, 120);
    // A gain of zero electrons per code is a number the typed record refuses, so it is skipped like
    // an unreadable one: a projection that handed it on would make the whole file unreadable at the
    // decoder that builds the record.
    EXPECT_FALSE(fallback.acquisition.gainElectronsPerAdu.has_value());
    EXPECT_EQ(fallback.malformedCards, 4u);
    EXPECT_EQ(fallback.consultedCards, 5u);
    EXPECT_TRUE(an::AcquisitionMetadata::create(fallback.acquisition).hasValue());
    for (const auto [text, type] :
         {std::pair{"LIGHT", an::FrameType::Light}, std::pair{"dark", an::FrameType::Dark},
          std::pair{"Flat Field", an::FrameType::Flat}, std::pair{"BIAS", an::FrameType::Bias},
          std::pair{"Master Dark", an::FrameType::Dark}, std::pair{"unknown", an::FrameType::Unknown},
          std::pair{"", an::FrameType::Unknown}})
    {
        const auto typed =
            an::projectAcquisition(*an::CodecMetadata::create({}, {card(CodecNamespace::Fits, "IMAGETYP", text)}));
        EXPECT_EQ(typed.frameType, type) << text;
        EXPECT_EQ(typed.frameTypeText, text);
    }
    for (std::uint32_t i = 0u; i <= static_cast<std::uint32_t>(an::FrameType::Bias); ++i)
    {
        EXPECT_TRUE(an::isFrameType(static_cast<an::FrameType>(i)));
        EXPECT_NE(std::string_view(an::frameTypeName(static_cast<an::FrameType>(i))), "UnknownFrameType");
    }
    EXPECT_FALSE(an::isFrameType(static_cast<an::FrameType>(9u)));
    EXPECT_STREQ(an::frameTypeName(static_cast<an::FrameType>(9u)), "UnknownFrameType");
    EXPECT_STREQ(an::frameTypeName(an::FrameType::Bias), "Bias");
    const auto empty = an::projectAcquisition({});
    EXPECT_EQ(empty.consultedCards, 0u);
    EXPECT_EQ(empty.malformedCards, 0u);
    EXPECT_TRUE(empty.acquisition.instrumentName.empty());
    EXPECT_FALSE(empty.acquisition.exposureSeconds.has_value());
    EXPECT_EQ(empty.frameType, an::FrameType::Unknown);
}

TEST(CodecMetadataProjectionTest, AValueTheAcquisitionRecordRefusesIsSkippedSoTheNextSpellingAnswers)
{
    // Capture programs write `EXPTIME = 0` for a bias frame. The typed record refuses a zero-length
    // exposure, and a decoder builds the record from this projection, so a projection that handed
    // the zero on would leave the frame's pixels unreadable over one descriptive card.
    using an::CodecNamespace;
    const auto bias = an::projectAcquisition(*an::CodecMetadata::create(
        {}, {card(CodecNamespace::Fits, "EXPTIME", "0"), card(CodecNamespace::Fits, "CCD-TEMP", "-300"),
             card(CodecNamespace::Fits, "EGAIN", "-1.5"), card(CodecNamespace::Fits, "IMAGETYP", "'Bias Frame'")}));
    EXPECT_FALSE(bias.acquisition.exposureSeconds.has_value());
    EXPECT_FALSE(bias.acquisition.sensorTemperatureCelsius.has_value());
    EXPECT_FALSE(bias.acquisition.gainElectronsPerAdu.has_value());
    EXPECT_EQ(bias.frameType, an::FrameType::Bias);
    EXPECT_EQ(bias.malformedCards, 3u);
    EXPECT_TRUE(an::AcquisitionMetadata::create(bias.acquisition).hasValue());

    // A refused value is skipped exactly as an unreadable one is, so a later spelling still answers.
    const auto fallback = an::projectAcquisition(*an::CodecMetadata::create(
        {}, {card(CodecNamespace::Tiff, "Exif.Photo.ExposureTime", "0"), card(CodecNamespace::Fits, "EXPOSURE", "0.0"),
             card(CodecNamespace::Fits, "EXPTIME", "45")}));
    ASSERT_TRUE(fallback.acquisition.exposureSeconds.has_value());
    EXPECT_DOUBLE_EQ(*fallback.acquisition.exposureSeconds, 45.0);
    EXPECT_EQ(fallback.malformedCards, 2u);
    EXPECT_EQ(fallback.consultedCards, 1u);

    // The boundaries are the record's own: any positive exposure and gain, and any temperature above
    // absolute zero, are facts.
    const auto edge = an::projectAcquisition(*an::CodecMetadata::create(
        {}, {card(CodecNamespace::Fits, "EXPTIME", "0.000001"), card(CodecNamespace::Fits, "CCD-TEMP", "-273.14"),
             card(CodecNamespace::Fits, "EGAIN", "0.01")}));
    EXPECT_EQ(edge.malformedCards, 0u);
    EXPECT_TRUE(edge.acquisition.exposureSeconds.has_value());
    EXPECT_TRUE(edge.acquisition.sensorTemperatureCelsius.has_value());
    EXPECT_TRUE(edge.acquisition.gainElectronsPerAdu.has_value());
    EXPECT_TRUE(an::AcquisitionMetadata::create(edge.acquisition).hasValue());
}

TEST(CodecMetadataProjectionTest, ExifSpellingsTakePrecedenceAndUnrelatedNamespacesAreNotConsulted)
{
    using an::CodecNamespace;
    const auto projection = an::projectAcquisition(*an::CodecMetadata::create(
        {},
        {card(CodecNamespace::Fits, "INSTRUME", "'ASI'"), card(CodecNamespace::Jpeg, "Exif.Image.Model", "Canon EOS R"),
         card(CodecNamespace::Tiff, "Exif.Photo.ExposureTime", "1/250"),
         card(CodecNamespace::Png, "Exif.Photo.ISOSpeedRatings", "800"),
         card(CodecNamespace::CameraRaw, "Exif.Image.Software", "DPP"),
         card(CodecNamespace::Jpeg, "Exif.Photo.DateTimeOriginal", "2026:09:10 21:14:05"),
         card(CodecNamespace::Jpeg, "Exif.Photo.DateTimeDigitized", "2026:09:10 21:15:00"),
         card(CodecNamespace::Jpeg, "Exif.MakerNote.SensorTemperature", "35"),
         card(CodecNamespace::Ser, "EXPOSURE", "5"), card(CodecNamespace::Unspecified, "OBJECT", "ignored"),
         card(CodecNamespace::Xisf, "FILTER", "ignored")}));
    EXPECT_EQ(projection.acquisition.instrumentName, "Canon EOS R");
    ASSERT_TRUE(projection.acquisition.exposureSeconds.has_value());
    EXPECT_DOUBLE_EQ(*projection.acquisition.exposureSeconds, 0.004);
    EXPECT_EQ(projection.isoSpeed, 800);
    EXPECT_EQ(projection.software, "DPP");
    ASSERT_TRUE(projection.acquisition.exposureStart.has_value());
    EXPECT_EQ(projection.acquisition.exposureStart->nanosecondsSinceEpoch, 1789074845000000000LL);
    ASSERT_TRUE(projection.creationTime.has_value());
    EXPECT_EQ(projection.creationTime->nanosecondsSinceEpoch, 1789074900000000000LL);
    ASSERT_TRUE(projection.acquisition.sensorTemperatureCelsius.has_value());
    EXPECT_DOUBLE_EQ(*projection.acquisition.sensorTemperatureCelsius, 35.0);
    EXPECT_TRUE(projection.acquisition.objectName.empty());
    EXPECT_TRUE(projection.acquisition.filterName.empty());
    EXPECT_EQ(projection.consultedCards, 7u);
    EXPECT_EQ(projection.malformedCards, 0u);
}

TEST(CodecMetadataProjectionTest, StatesTypedFactsAsCardsAndRoundTripsThroughItself)
{
    using an::CodecNamespace;
    const auto projection = an::projectAcquisition(fitsFixture());
    const auto fits = an::acquisitionCards(projection, CodecNamespace::Fits);
    ASSERT_TRUE(fits.hasValue()) << fits.status().message();
    ASSERT_NE(findCard(*fits, "INSTRUME"), nullptr);
    EXPECT_EQ(findCard(*fits, "INSTRUME")->value(), "ASI294MM Pro");
    EXPECT_EQ(findCard(*fits, "INSTRUME")->origin(), CodecNamespace::Fits);
    EXPECT_EQ(findCard(*fits, "TELESCOP")->value(), "Esprit 100");
    EXPECT_EQ(findCard(*fits, "OBSERVER")->value(), "E. Ruiz");
    EXPECT_EQ(findCard(*fits, "OBJECT")->value(), "M42");
    EXPECT_EQ(findCard(*fits, "FILTER")->value(), "Ha");
    EXPECT_EQ(findCard(*fits, "EXPOSURE")->value(), "120");
    EXPECT_EQ(findCard(*fits, "EXPTIME"), nullptr);
    EXPECT_EQ(findCard(*fits, "CCD-TEMP")->value(), "-10.5");
    EXPECT_EQ(findCard(*fits, "EGAIN")->value(), "0.4");
    EXPECT_EQ(findCard(*fits, "XBINNING")->value(), "2");
    EXPECT_EQ(findCard(*fits, "YBINNING")->value(), "2");
    EXPECT_EQ(findCard(*fits, "DATE-OBS")->value(), "2026-09-10T21:14:05.25");
    EXPECT_EQ(findCard(*fits, "ISOSPEED")->value(), "800");
    EXPECT_EQ(findCard(*fits, "GAIN")->value(), "120");
    EXPECT_EQ(findCard(*fits, "OFFSET")->value(), "30");
    EXPECT_EQ(findCard(*fits, "IMAGETYP")->value(), "LIGHT");
    EXPECT_EQ(findCard(*fits, "SWCREATE")->value(), "N.I.N.A.");
    EXPECT_EQ(findCard(*fits, "SOFTWARE"), nullptr);
    EXPECT_EQ(findCard(*fits, "DATE")->value(), "2026-09-10T21:20:00");
    EXPECT_EQ(fits->size(), 17u);
    for (const auto& value : *fits)
        EXPECT_FALSE(value.comment().empty()) << value.keyword();
    const auto again = an::projectAcquisition(*an::CodecMetadata::create({}, *fits));
    EXPECT_EQ(*an::AcquisitionMetadata::create(again.acquisition),
              *an::AcquisitionMetadata::create(projection.acquisition));
    EXPECT_EQ(again.isoSpeed, projection.isoSpeed);
    EXPECT_EQ(again.gainSetting, projection.gainSetting);
    EXPECT_EQ(again.cameraOffset, projection.cameraOffset);
    EXPECT_EQ(again.frameType, projection.frameType);
    EXPECT_EQ(again.software, projection.software);
    EXPECT_EQ(again.creationTime, projection.creationTime);
    EXPECT_EQ(again.malformedCards, 0u);
    const auto exif = an::acquisitionCards(projection, CodecNamespace::Jpeg);
    ASSERT_TRUE(exif.hasValue()) << exif.status().message();
    ASSERT_NE(findCard(*exif, "Exif.Image.Model"), nullptr);
    EXPECT_EQ(findCard(*exif, "Exif.Image.Model")->value(), "ASI294MM Pro");
    EXPECT_EQ(findCard(*exif, "Exif.Image.Model")->origin(), CodecNamespace::Jpeg);
    EXPECT_EQ(findCard(*exif, "Exif.Photo.ExposureTime")->value(), "120");
    EXPECT_EQ(findCard(*exif, "Exif.MakerNote.SensorTemperature")->value(), "-10.5");
    EXPECT_EQ(findCard(*exif, "Exif.Photo.ISOSpeedRatings")->value(), "800");
    EXPECT_EQ(findCard(*exif, "Exif.Image.Software")->value(), "N.I.N.A.");
    EXPECT_EQ(findCard(*exif, "Exif.Photo.DateTimeOriginal")->value(), "2026:09:10 21:14:05");
    EXPECT_EQ(findCard(*exif, "Exif.Photo.DateTimeDigitized")->value(), "2026:09:10 21:20:00");
    EXPECT_EQ(findCard(*exif, "OBJECT"), nullptr);
    EXPECT_EQ(findCard(*exif, "Exif.Image.Make"), nullptr);
    EXPECT_EQ(exif->size(), 7u);
    const auto exifAgain = an::projectAcquisition(*an::CodecMetadata::create({}, *exif));
    EXPECT_EQ(exifAgain.acquisition.instrumentName, "ASI294MM Pro");
    ASSERT_TRUE(exifAgain.acquisition.exposureSeconds.has_value());
    EXPECT_DOUBLE_EQ(*exifAgain.acquisition.exposureSeconds, 120.0);
    EXPECT_EQ(exifAgain.acquisition.exposureStart->nanosecondsSinceEpoch, 1789074845000000000LL);
    EXPECT_EQ(exifAgain.isoSpeed, 800);
    an::AcquisitionProjection fraction;
    fraction.acquisition.exposureSeconds = 0.004;
    const auto shortExposure = an::acquisitionCards(fraction, CodecNamespace::Tiff);
    ASSERT_TRUE(shortExposure.hasValue());
    ASSERT_EQ(shortExposure->size(), 1u);
    EXPECT_EQ(shortExposure->front().value(), "1/250");
    EXPECT_EQ(shortExposure->front().origin(), CodecNamespace::Tiff);
    EXPECT_TRUE(an::acquisitionCards({}, CodecNamespace::Png)->empty());
    EXPECT_TRUE(an::acquisitionCards({}, CodecNamespace::Fits)->empty());
    for (const auto target : {CodecNamespace::Ser, CodecNamespace::Unspecified, CodecNamespace::CameraRaw,
                              CodecNamespace::Xisf, static_cast<CodecNamespace>(77u)})
        EXPECT_EQ(an::acquisitionCards(projection, target).status().code(), an::ErrorCode::Unsupported);
    an::AcquisitionProjection oversized;
    oversized.acquisition.objectName = std::string(an::kMaxCodecCardTextBytes + 1u, 'x');
    EXPECT_EQ(an::acquisitionCards(oversized, CodecNamespace::Fits).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CodecMetadataProjectionTest, DateAndNumberSpellingsParseExactlyOrAreRefused)
{
    for (const auto [text, nanoseconds] :
         {std::pair{"2026-09-10T21:14:05.250", 1789074845250000000LL},
          std::pair{"2026-09-10T21:14:05", 1789074845000000000LL},
          std::pair{"2026-09-10 21:14:05", 1789074845000000000LL},
          std::pair{"2026:09:10 21:14:05", 1789074845000000000LL},
          std::pair{" '2026-09-10T21:14:05' ", 1789074845000000000LL}, std::pair{"2026-09-10", 1788998400000000000LL},
          std::pair{"1969-12-31T23:59:59", -1000000000LL}, std::pair{"2000-02-29T00:00:00", 951782400000000000LL},
          std::pair{"2026-09-10T21:14:05.000000001", 1789074845000000001LL},
          std::pair{"2026-09-10T21:14:05.1234567891", 1789074845123456789LL},
          // The spellings capture programs write beside the standard's: a lowercase z, a decimal comma, and a zone
          // offset after the time of day, with or without its colon or its minutes, which the instant is moved by to
          // UTC.
          std::pair{"2026-09-10T21:14:05z", 1789074845000000000LL},
          std::pair{"2026-09-10T21:14:05,250", 1789074845250000000LL},
          std::pair{"2026-09-11T02:44:05+05:30", 1789074845000000000LL},
          std::pair{"2026-09-11T02:44:05.250+0530", 1789074845250000000LL},
          std::pair{"2026-09-10T16:14:05-05:00", 1789074845000000000LL},
          std::pair{"2026-09-10T16:14:05-05", 1789074845000000000LL},
          std::pair{"2026-09-10T21:14:05+00:00", 1789074845000000000LL},
          std::pair{"2026-09-10 23:14:05+02:00", 1789074845000000000LL}})
    {
        const auto parsed = an::parseDateCard(text);
        ASSERT_TRUE(parsed.hasValue()) << text << ": " << parsed.status().message();
        EXPECT_EQ(parsed->nanosecondsSinceEpoch, nanoseconds) << text;
    }
    for (const auto text : {"",
                            "abc",
                            "2026-13-01T00:00:00",
                            "2026-02-30",
                            "2026-09-10T24:00:00",
                            "2026-09-10T21:60:00",
                            "2026-09-10T21:14:61",
                            "2026-09-10T21:14",
                            "2026-09-1",
                            "2026-09-10T21:14:05Z+01",
                            "20260910T211405",
                            "2026-09-10T21:14:05.",
                            "2026-09-10T21:14:05.abc",
                            "999999-01-01",
                            "2026-09-10T21:14:05 trailing",
                            "2026-09-10T21:14:05,",
                            "2026-09-10T21:14:05+5",
                            "2026-09-10T21:14:05+05:3",
                            "2026-09-10T21:14:05+0530Z",
                            "2026-09-10T21:14:05+24:00",
                            "2026-09-10T21:14:05+05:60",
                            "2026-09-10T21:14:05+",
                            "2026-09-10+05:00"})
    {
        const auto refused = an::parseDateCard(text);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData) << text;
    }
    EXPECT_EQ(an::formatDateCard({1789074845250000000LL}, an::CodecNamespace::Fits), "2026-09-10T21:14:05.25");
    EXPECT_EQ(an::formatDateCard({1789074845000000000LL}, an::CodecNamespace::Fits), "2026-09-10T21:14:05");
    EXPECT_EQ(an::formatDateCard({1789074845000000001LL}, an::CodecNamespace::Fits), "2026-09-10T21:14:05.000000001");
    EXPECT_EQ(an::formatDateCard({-1000000000LL}, an::CodecNamespace::Fits), "1969-12-31T23:59:59");
    EXPECT_EQ(an::formatDateCard({-1500000000LL}, an::CodecNamespace::Fits), "1969-12-31T23:59:58.5");
    EXPECT_EQ(an::formatDateCard({951782400000000000LL}, an::CodecNamespace::Xisf), "2000-02-29T00:00:00");
    EXPECT_EQ(an::formatDateCard({1789074845250000000LL}, an::CodecNamespace::Jpeg), "2026:09:10 21:14:05");
    EXPECT_EQ(an::formatDateCard({1789074845250000000LL}, an::CodecNamespace::Tiff), "2026:09:10 21:14:05");
    EXPECT_EQ(an::formatDateCard({1789074845250000000LL}, an::CodecNamespace::Png), "2026:09:10 21:14:05");
    EXPECT_EQ(an::formatDateCard({1789074845250000000LL}, an::CodecNamespace::CameraRaw), "2026:09:10 21:14:05");
    EXPECT_EQ(an::formatDateCard({1789074845250000000LL}, an::CodecNamespace::Ser), "2026-09-10T21:14:05.25");
    for (const auto [text, value] :
         {std::pair{"0.004", 0.004}, std::pair{"1/250", 0.004}, std::pair{" 12 ", 12.0}, std::pair{"1e-3", 0.001},
          std::pair{"'120.0'", 120.0}, std::pair{"-10.5", -10.5}, std::pair{"3 / 4", 0.75}, std::pair{"+7", 7.0}})
    {
        const auto parsed = an::parseNumericCard(text);
        ASSERT_TRUE(parsed.hasValue()) << text;
        EXPECT_DOUBLE_EQ(*parsed, value) << text;
    }
    for (const auto text : {"", "abc", "1/0", "inf", "nan", "1/", "/2", "1/2/3", "12x", "0x10", "1e400"})
        EXPECT_EQ(an::parseNumericCard(text).status().code(), an::ErrorCode::InvalidData) << text;
}
} // namespace
