// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/CodecMetadataPolicy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
an::CodecCard card(an::CodecNamespace origin, const char* keyword, const char* value = "1")
{
    return *an::CodecCard::create(origin, keyword, value, "");
}

TEST(CodecMetadataPolicyTest, EveryFormatNamesTheVocabularyItsCardsBelongTo)
{
    EXPECT_EQ(an::codecNamespaceOf(an::CodecFormat::Fits), an::CodecNamespace::Fits);
    EXPECT_EQ(an::codecNamespaceOf(an::CodecFormat::Xisf), an::CodecNamespace::Xisf);
    EXPECT_EQ(an::codecNamespaceOf(an::CodecFormat::Tiff), an::CodecNamespace::Tiff);
    EXPECT_EQ(an::codecNamespaceOf(an::CodecFormat::Png), an::CodecNamespace::Png);
    EXPECT_EQ(an::codecNamespaceOf(an::CodecFormat::Jpeg), an::CodecNamespace::Jpeg);
    EXPECT_EQ(an::codecNamespaceOf(an::CodecFormat::Raw), an::CodecNamespace::CameraRaw);
    EXPECT_EQ(an::codecNamespaceOf(an::CodecFormat::Dump), an::CodecNamespace::Unspecified);
    EXPECT_EQ(an::codecNamespaceOf(an::CodecFormat::Unknown), an::CodecNamespace::Unspecified);
    EXPECT_EQ(an::codecNamespaceOf(static_cast<an::CodecFormat>(99u)), an::CodecNamespace::Unspecified);
    for (std::uint32_t i = 0u; i <= static_cast<std::uint32_t>(an::CardDisposition::UnsupportedDestination); ++i)
        EXPECT_NE(std::string_view(an::cardDispositionName(static_cast<an::CardDisposition>(i))),
                  "UnknownCardDisposition");
    EXPECT_STREQ(an::cardDispositionName(static_cast<an::CardDisposition>(200u)), "UnknownCardDisposition");
}

TEST(CodecMetadataPolicyTest, FitsAndXisfDestinationsTakeTheirOwnCardsExceptStructureAndIdentity)
{
    using an::CardDisposition;
    using an::CodecFormat;
    using an::CodecNamespace;
    EXPECT_EQ(an::cardDisposition(CodecFormat::Fits, card(CodecNamespace::Fits, "OBJECT")), CardDisposition::Written);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Fits, card(CodecNamespace::Fits, "HISTORY")), CardDisposition::Written);
    for (const auto keyword :
         {"SIMPLE",   "BITPIX",   "NAXIS",   "NAXIS1",   "NAXIS2",   "NAXIS3",   "EXTEND",  "BZERO",   "BSCALE",
          "BLANK",    "PCOUNT",   "GCOUNT",  "XTENSION", "END",      "ZIMAGE",   "ZBITPIX", "ZNAXIS",  "ZNAXIS1",
          "ZCMPTYPE", "ZTILE1",   "ZNAME1",  "ZVAL1",    "ZQUANTIZ", "ZDITHER0", "ZSIMPLE", "ZEXTEND", "ZPCOUNT",
          "ZGCOUNT",  "ZTENSION", "TFIELDS", "TTYPE1",   "TFORM1",   "THEAP",    "DATASUM", "CHECKSUM"})
    {
        EXPECT_EQ(an::cardDisposition(CodecFormat::Fits, card(CodecNamespace::Fits, keyword)),
                  CardDisposition::StructuralKeyword)
            << keyword;
        EXPECT_TRUE(an::isStructuralFitsKeyword(keyword)) << keyword;
    }
    EXPECT_FALSE(an::isStructuralFitsKeyword("NAXISX"));
    EXPECT_FALSE(an::isStructuralFitsKeyword("naxis"));
    EXPECT_FALSE(an::isStructuralFitsKeyword("DATE-OBS"));
    EXPECT_FALSE(an::isStructuralFitsKeyword("ZOOM"));
    EXPECT_EQ(an::cardDisposition(CodecFormat::Fits, card(CodecNamespace::Fits, "ANARTID")),
              CardDisposition::IdentityKeyword);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Fits, card(CodecNamespace::Fits, "ANRUNID")),
              CardDisposition::IdentityKeyword);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Fits, card(CodecNamespace::Tiff, "Exif.Image.Model")),
              CardDisposition::ForeignNamespace);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Fits, card(CodecNamespace::Xisf, "OBJECT")),
              CardDisposition::ForeignNamespace);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Fits, card(CodecNamespace::Unspecified, "OBJECT")),
              CardDisposition::ForeignNamespace);
    // XISF carries FITS keywords natively, so a FITS card travels to it under the same rules.
    EXPECT_EQ(an::cardDisposition(CodecFormat::Xisf, card(CodecNamespace::Xisf, "XISF:Property")),
              CardDisposition::Written);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Xisf, card(CodecNamespace::Fits, "OBJECT")), CardDisposition::Written);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Xisf, card(CodecNamespace::Fits, "NAXIS1")),
              CardDisposition::StructuralKeyword);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Xisf, card(CodecNamespace::Xisf, "ANARTID")),
              CardDisposition::IdentityKeyword);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Xisf, card(CodecNamespace::Fits, "ANRUNID")),
              CardDisposition::IdentityKeyword);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Xisf, card(CodecNamespace::Jpeg, "Exif.Image.Model")),
              CardDisposition::ForeignNamespace);
}

TEST(CodecMetadataPolicyTest, RasterDestinationsWriteTheirExifAllowlistsFromAnyRasterSource)
{
    using an::CardDisposition;
    using an::CodecFormat;
    using an::CodecNamespace;
    EXPECT_EQ(an::writableExifKeys(CodecFormat::Jpeg).size(), 65u);
    EXPECT_EQ(an::writableExifKeys(CodecFormat::Tiff).size(), 68u);
    EXPECT_EQ(an::writableExifKeys(CodecFormat::Png).size(), 16u);
    EXPECT_TRUE(an::writableExifKeys(CodecFormat::Fits).empty());
    EXPECT_TRUE(an::writableExifKeys(CodecFormat::Xisf).empty());
    EXPECT_TRUE(an::writableExifKeys(CodecFormat::Raw).empty());
    EXPECT_TRUE(an::writableExifKeys(CodecFormat::Dump).empty());
    EXPECT_TRUE(an::writableExifKeys(static_cast<CodecFormat>(77u)).empty());
    for (const auto format : {CodecFormat::Jpeg, CodecFormat::Tiff, CodecFormat::Png})
    {
        const auto keys = an::writableExifKeys(format);
        std::set<std::string_view> unique(keys.begin(), keys.end());
        EXPECT_EQ(unique.size(), keys.size());
        EXPECT_TRUE(std::is_sorted(keys.begin(), keys.end()));
        for (const auto key : keys)
        {
            EXPECT_TRUE(key.starts_with("Exif.")) << key;
            EXPECT_EQ(an::cardDisposition(format, card(an::codecNamespaceOf(format), std::string(key).c_str())),
                      CardDisposition::Written)
                << key;
        }
    }
    EXPECT_EQ(an::cardDisposition(CodecFormat::Jpeg, card(CodecNamespace::Tiff, "Exif.Image.Model")),
              CardDisposition::Written);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Jpeg, card(CodecNamespace::Png, "Exif.Image.Model")),
              CardDisposition::Written);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Jpeg, card(CodecNamespace::CameraRaw, "Exif.Image.Model")),
              CardDisposition::Written);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Jpeg, card(CodecNamespace::Jpeg, "Exif.Image.IPTCNAA")),
              CardDisposition::NotInAllowlist);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Tiff, card(CodecNamespace::Tiff, "Exif.Image.IPTCNAA")),
              CardDisposition::Written);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Tiff, card(CodecNamespace::Tiff, "Exif.Thumbnail.Compression")),
              CardDisposition::NotInAllowlist);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Png, card(CodecNamespace::Png, "Exif.Photo.Flash")),
              CardDisposition::NotInAllowlist);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Png, card(CodecNamespace::Jpeg, "Exif.Nikon.ShotInfo")),
              CardDisposition::NotInAllowlist);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Jpeg, card(CodecNamespace::Jpeg, "exif.image.model")),
              CardDisposition::NotInAllowlist);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Jpeg, card(CodecNamespace::Fits, "OBJECT")),
              CardDisposition::ForeignNamespace);
    EXPECT_EQ(an::cardDisposition(CodecFormat::Jpeg, card(CodecNamespace::Jpeg, "ANARTID")),
              CardDisposition::IdentityKeyword);
    for (const auto format : {CodecFormat::Raw, CodecFormat::Dump, CodecFormat::Unknown, static_cast<CodecFormat>(9u)})
        EXPECT_EQ(an::cardDisposition(format, card(CodecNamespace::Fits, "OBJECT")),
                  CardDisposition::UnsupportedDestination);
}

TEST(CodecMetadataPolicyTest, WritableCardsKeepOrderAndCountEveryDroppedCard)
{
    using an::CodecNamespace;
    const auto source = *an::CodecMetadata::create(
        {}, {card(CodecNamespace::Fits, "SIMPLE", "T"), card(CodecNamespace::Fits, "OBJECT", "M42"),
             card(CodecNamespace::Fits, "ANARTID", "abc"), card(CodecNamespace::Tiff, "Exif.Image.Model", "cam"),
             card(CodecNamespace::Fits, "FILTER", "Ha"), card(CodecNamespace::Fits, "ANRUNID", "run"),
             card(CodecNamespace::Fits, "NAXIS2", "5"), card(CodecNamespace::Fits, "OBJECT", "M42 again")});
    const auto report = an::writableCards(source, an::CodecFormat::Fits);
    ASSERT_EQ(report.cards.size(), 3u);
    EXPECT_EQ(report.cards[0].keyword(), "OBJECT");
    EXPECT_EQ(report.cards[1].keyword(), "FILTER");
    EXPECT_EQ(report.cards[2].value(), "M42 again");
    EXPECT_EQ(report.droppedForeign, 1u);
    EXPECT_EQ(report.droppedStructural, 2u);
    EXPECT_EQ(report.droppedIdentity, 2u);
    EXPECT_EQ(report.droppedNotAllowed, 0u);
    const auto raster = an::writableCards(source, an::CodecFormat::Png);
    ASSERT_EQ(raster.cards.size(), 1u);
    EXPECT_EQ(raster.cards.front().keyword(), "Exif.Image.Model");
    EXPECT_EQ(raster.droppedForeign, 7u);
    const auto unsupported = an::writableCards(source, an::CodecFormat::Raw);
    EXPECT_TRUE(unsupported.cards.empty());
    EXPECT_EQ(unsupported.droppedForeign, 0u);
    EXPECT_EQ(unsupported.droppedUnsupported, 8u);
}

TEST(CodecMetadataPolicyTest, OutputMetadataFiltersOnlyTheCardsAndNeverCarriesAnIntent)
{
    using an::CodecNamespace;
    const auto acquisition = *an::AcquisitionMetadata::create({.instrumentName = "ASI294MM", .exposureSeconds = 120.0});
    const auto provenance = *an::Provenance::create(an::ProvenanceSourceKind::File, "C:/lights/L_0001.fits");
    an::SourceEncoding encoding;
    encoding.sampleFormat = an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger};
    encoding.zeroOffset = 32768.0;
    const auto codec = *an::CodecMetadata::create(encoding, {card(CodecNamespace::Fits, "BZERO", "32768"),
                                                             card(CodecNamespace::Fits, "OBJECT", "M42"),
                                                             card(CodecNamespace::Fits, "ANARTID", "abc")});
    const auto source = an::ImageMetadata::create(acquisition, {}, {}, provenance, codec,
                                                  *an::OutputIntent::create({.sampleType = an::SampleType::Float32}));
    const auto output = an::outputMetadata(source, an::CodecFormat::Fits);
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    EXPECT_EQ(output->acquisition(), acquisition);
    EXPECT_EQ(output->semantics(), source.semantics());
    EXPECT_EQ(output->astrometry(), source.astrometry());
    EXPECT_EQ(output->provenance(), provenance);
    EXPECT_FALSE(output->outputIntent().has_value());
    EXPECT_EQ(output->codec().sourceEncoding(), encoding);
    ASSERT_EQ(output->codec().cards().size(), 1u);
    EXPECT_EQ(output->codec().cards().front().keyword(), "OBJECT");
    EXPECT_NE(output->codec().find(CodecNamespace::Fits, "OBJECT"), nullptr);
    EXPECT_EQ(output->codec().find(CodecNamespace::Fits, "BZERO"), nullptr);
    EXPECT_EQ(source.codec().cards().size(), 3u);
    const auto raw = an::outputMetadata(source, an::CodecFormat::Raw);
    ASSERT_TRUE(raw.hasValue());
    EXPECT_TRUE(raw->codec().cards().empty());
    EXPECT_EQ(raw->codec().sourceEncoding(), encoding);
}
TEST(CodecMetadataPolicyTest, ExifFieldsAreJoinedAndDescribedAsTheLibraryNamesThem)
{
    using an::CodecNamespace;
    const std::vector<an::CodecCard> cards = {
        *an::CodecCard::create(CodecNamespace::Fits, "OBJECT", "'M42'", ""),
        *an::CodecCard::create(CodecNamespace::Tiff, "Exif.Image.Model", "Canon EOS R5", "Ascii"),
        *an::CodecCard::create(CodecNamespace::Jpeg, "Exif.Photo.ExposureTime", "1/100", "Rational"),
        *an::CodecCard::create(CodecNamespace::Png, "Exif.Image.Orientation", "1", ""),
        *an::CodecCard::create(CodecNamespace::Tiff, "Exif.Image.ImageDescription", "Orion ", "Ascii"),
        *an::CodecCard::create(CodecNamespace::Tiff, "Exif.Image.ImageDescription", "Nebula", "continued"),
        *an::CodecCard::create(CodecNamespace::Tiff, "Exif.Photo.FNumber", "not a ratio", "Rational"),
        *an::CodecCard::create(CodecNamespace::Tiff, "Exif.NoSuchGroup.Thing", "x", "Ascii"),
        *an::CodecCard::create(CodecNamespace::Xisf, "OBSERVER", "someone", ""),
    };
    const an::Result<std::vector<an::ExifField>> fields = an::describeExifFields(cards);
    ASSERT_TRUE(fields.hasValue()) << fields.status().message();
    ASSERT_EQ(fields->size(), 6u);

    const an::ExifField& model = (*fields)[0];
    EXPECT_EQ(model.keyword, "Exif.Image.Model");
    EXPECT_EQ(model.value, "Canon EOS R5");
    EXPECT_EQ(model.typeName, "Ascii");
    EXPECT_EQ(model.tag, 0x0110u);
    EXPECT_EQ(model.tagName, "Model");
    EXPECT_EQ(model.tagLabel, "Model");
    EXPECT_FALSE(model.tagDescription.empty());
    EXPECT_EQ(model.count, 13u) << "an Ascii value counts its terminator";

    const an::ExifField& exposure = (*fields)[1];
    EXPECT_EQ(exposure.tag, 0x829Au);
    EXPECT_EQ(exposure.tagName, "ExposureTime");
    EXPECT_EQ(exposure.tagLabel, "Exposure Time");
    EXPECT_EQ(exposure.typeName, "Rational");
    EXPECT_EQ(exposure.count, 1u);

    const an::ExifField& orientation = (*fields)[2];
    EXPECT_EQ(orientation.tag, 0x0112u);
    EXPECT_EQ(orientation.typeName, "Short") << "a card stating no type takes the key's default";
    EXPECT_EQ(orientation.count, 1u);

    const an::ExifField& description = (*fields)[3];
    EXPECT_EQ(description.keyword, "Exif.Image.ImageDescription");
    EXPECT_EQ(description.value, "Orion Nebula") << "a continued field is one field";
    EXPECT_EQ(description.typeName, "Ascii");
    EXPECT_EQ(description.count, 13u);

    const an::ExifField& unreadable = (*fields)[4];
    EXPECT_EQ(unreadable.tagName, "FNumber");
    EXPECT_EQ(unreadable.count, 0u) << "a value its type cannot read counts nothing";

    const an::ExifField& unknown = (*fields)[5];
    EXPECT_EQ(unknown.keyword, "Exif.NoSuchGroup.Thing");
    EXPECT_EQ(unknown.value, "x");
    EXPECT_EQ(unknown.typeName, "Ascii");
    EXPECT_EQ(unknown.tag, 0u);
    EXPECT_TRUE(unknown.tagName.empty() && unknown.tagLabel.empty() && unknown.tagDescription.empty());
    EXPECT_EQ(unknown.count, 0u);

    const an::Result<std::vector<an::ExifField>> none = an::describeExifFields({});
    ASSERT_TRUE(none.hasValue());
    EXPECT_TRUE(none->empty());
}
} // namespace
