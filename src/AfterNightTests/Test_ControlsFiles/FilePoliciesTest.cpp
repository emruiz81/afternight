// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The settings an open and a save keep: the RAW settings read through their fixed table, the policy and its registry,
/// the export settings and the intent they give each container, the demosaic and Bayer-pattern text maps, and the
/// method a chosen one becomes over an X-Trans mosaic.

#include "Test_ControlsFiles/FilesTestContext.h"

#include "app/DemosaicTextMaps.h"
#include "app/FilePolicies.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QHash>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>

namespace
{

using an::tests::files::Channels;
using an::ui::RawWhiteBalanceSetting;

/// A settings store in memory.
class StoredSettings
{
public:
    [[nodiscard]] an::ui::SettingReader reader()
    {
        return [this](const QString& key, const QString& fallback) { return values.value(key, fallback); };
    }

    [[nodiscard]] an::ui::SettingWriter writer()
    {
        return [this](const QString& key, const QString& value) { values.insert(key, value); };
    }

    QHash<QString, QString> values;
};

/// A description of a representation and shape, with a filter array when asked.
an::ImageDescriptor described(an::SampleType type, Channels channels, bool mosaic = false)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(16u, 12u, an::tests::viewer::channelCountOf(channels), an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::CfaDescriptor> cfa =
        mosaic ? an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u) : std::optional<an::CfaDescriptor>{};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(an::ImageDescriptorRequest{
        .extent = *extent,
        .sampleType = type,
        .channelSchema = channels == Channels::Grey
                             ? an::ChannelSchema::Mono
                             : (channels == Channels::Rgb ? an::ChannelSchema::Rgb : an::ChannelSchema::Rgba),
        .colorSpace = an::ColorSpace::Unspecified,
        .numericDomain = an::NumericDomain::NormalizedLinear,
        .cfa = cfa.value_or(an::CfaDescriptor{}),
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// A sixteen-bit grey description stating an X-Trans sensor's six-by-six filter array.
an::ImageDescriptor describedXTrans()
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(36u, 24u, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::UInt16,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::tests::viewer::xtransFilterArray(),
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// The product's registry, for the intent cases.
const an::CodecRegistry& productRegistry()
{
    static const std::shared_ptr<const an::CodecRegistry> registry = []() {
        an::Result<std::shared_ptr<const an::CodecRegistry>> made = an::ui::createProductCodecRegistry();
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        return made.hasValue() ? *made : nullptr;
    }();
    return *registry;
}

} // namespace

// The white-balance mode is the integer the product always stored; anything else reads as daylight, as it did.
TEST(FilePoliciesTest, AStoredWhiteBalanceReadsThroughItsFixedTable)
{
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("0")), RawWhiteBalanceSetting::None);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("1")), RawWhiteBalanceSetting::Daylight);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("2")), RawWhiteBalanceSetting::CameraAsShot);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("3")), RawWhiteBalanceSetting::Automatic);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("4")), RawWhiteBalanceSetting::Custom);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("5")), RawWhiteBalanceSetting::Daylight);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("-1")), RawWhiteBalanceSetting::Daylight);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("none")), RawWhiteBalanceSetting::Daylight);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QString()), RawWhiteBalanceSetting::Daylight);
    for (const RawWhiteBalanceSetting mode :
         {RawWhiteBalanceSetting::None, RawWhiteBalanceSetting::Daylight, RawWhiteBalanceSetting::CameraAsShot,
          RawWhiteBalanceSetting::Automatic, RawWhiteBalanceSetting::Custom})
    {
        EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(an::ui::storedRawWhiteBalance(mode)), mode);
    }
}

// What is stored is what is read back, and nothing stored reads as the product's defaults: daylight, unit gains and
// auto-rotate on.
TEST(FilePoliciesTest, RawSettingsRoundTripAndDefault)
{
    StoredSettings store;
    const an::ui::RawOpenSettings defaults = an::ui::readRawOpenSettings(store.reader());
    EXPECT_EQ(defaults.whiteBalance, RawWhiteBalanceSetting::Daylight);
    EXPECT_EQ(defaults.customGains, (std::array<double, 3u>{1.0, 1.0, 1.0}));
    EXPECT_TRUE(defaults.autoRotate);

    const an::ui::RawOpenSettings custom{
        .whiteBalance = RawWhiteBalanceSetting::Custom, .customGains = {1.8, 1.0, 1.25}, .autoRotate = false};
    an::ui::writeRawOpenSettings(custom, store.writer());
    EXPECT_EQ(store.values.value(QString::fromLatin1(SettingsKeys::RAW_WHITE_BALANCE_MODE)), QStringLiteral("4"));
    EXPECT_EQ(an::ui::readRawOpenSettings(store.reader()), custom);
}

// A gain stored as zero, as less or as no number reads as one.
TEST(FilePoliciesTest, AGainThatIsNotPositiveReadsAsOne)
{
    StoredSettings store;
    store.values.insert(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_R), QStringLiteral("0"));
    store.values.insert(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_G), QStringLiteral("-2.5"));
    store.values.insert(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_B), QStringLiteral("abc"));
    EXPECT_EQ(an::ui::readRawOpenSettings(store.reader()).customGains, (std::array<double, 3u>{1.0, 1.0, 1.0}));

    // A policy made from settings holding such a gain is built, with the gain read as one.
    an::Result<std::shared_ptr<const an::ui::RawOpenPolicy>> policy = an::ui::RawOpenPolicy::create(
        an::ui::RawOpenSettings{.whiteBalance = RawWhiteBalanceSetting::Custom,
                                .customGains = {0.0, std::numeric_limits<double>::quiet_NaN(), 2.0}});
    ASSERT_TRUE(policy.hasValue()) << policy.status().message();
    EXPECT_EQ((*policy)->settings().customGains, (std::array<double, 3u>{1.0, 1.0, 2.0}));
}

// The policy carries the filter-array balance and the turn a read applies, and a registry of its own that reads every
// container the product reads.
TEST(FilePoliciesTest, APolicyCarriesItsTreatmentAndARegistry)
{
    an::Result<std::shared_ptr<const an::ui::RawOpenPolicy>> policy = an::ui::RawOpenPolicy::create(
        an::ui::RawOpenSettings{.whiteBalance = RawWhiteBalanceSetting::Automatic, .autoRotate = true});
    ASSERT_TRUE(policy.hasValue()) << policy.status().message();
    const an::ui::ViewRawTreatment treatment = (*policy)->treatment();
    EXPECT_EQ(treatment.mosaicBalance, an::CfaWhiteBalanceMode::Automatic);
    EXPECT_TRUE(treatment.autoRotate);
    for (const an::CodecFormat format : {an::CodecFormat::Fits, an::CodecFormat::Xisf, an::CodecFormat::Tiff,
                                         an::CodecFormat::Png, an::CodecFormat::Jpeg, an::CodecFormat::Raw})
    {
        EXPECT_TRUE((*policy)->registry().capabilities(format).has_value()) << an::codecFormatName(format);
    }
    const an::ui::ViewReadRequest request = (*policy)->readRequest(QStringLiteral("frame.dng"));
    EXPECT_EQ(request.path, QStringLiteral("frame.dng"));
    EXPECT_EQ(request.registry, &(*policy)->registry());
    ASSERT_TRUE(request.raw.has_value());
    EXPECT_EQ(request.raw->mosaicBalance, an::CfaWhiteBalanceMode::Automatic);
    EXPECT_FALSE(request.demosaic.has_value());

    // Two policies never share a registry: a policy published later cannot change a read on its way.
    an::Result<std::shared_ptr<const an::ui::RawOpenPolicy>> other =
        an::ui::RawOpenPolicy::create(an::ui::RawOpenSettings{});
    ASSERT_TRUE(other.hasValue());
    EXPECT_NE(&(*other)->registry(), &(*policy)->registry());
}

// The export settings read through their tables: the TIFF texts, an unknown one as LZW, the levels held to range.
TEST(FilePoliciesTest, ExportSettingsReadThroughTheirTables)
{
    StoredSettings store;
    const an::ui::ExportSettings defaults = an::ui::readExportSettings(store.reader());
    EXPECT_EQ(defaults.tiffCompression, an::OutputCompression::None);
    EXPECT_EQ(defaults.jpegQuality, 95u);
    EXPECT_EQ(defaults.pngCompressionLevel, 6u);
    EXPECT_TRUE(defaults.embedExif);
    EXPECT_FALSE(defaults.embedHistory);
    // XISF is written in the form every reader takes until the settings say otherwise.
    EXPECT_EQ(defaults.xisfCompression, an::OutputCompression::None);
    EXPECT_TRUE(defaults.xisfByteShuffle);
    EXPECT_EQ(defaults.xisfChecksum, an::OutputChecksum::None);
    const QString xisf = QString::fromLatin1(SettingsKeys::EXPORT_XISF_COMPRESSION);
    for (const auto& [text, compression] : std::vector<std::pair<QString, an::OutputCompression>>{
             {QStringLiteral("none"), an::OutputCompression::None},
             {QStringLiteral("zlib"), an::OutputCompression::Deflate},
             {QStringLiteral(" lz4 "), an::OutputCompression::Lz4},
             {QStringLiteral("lz4hc"), an::OutputCompression::Lz4Hc},
             {QStringLiteral("zstd"), an::OutputCompression::Zstd},
             {QStringLiteral("rice"), an::OutputCompression::None}})
    {
        store.values.insert(xisf, text);
        EXPECT_EQ(an::ui::readExportSettings(store.reader()).xisfCompression, compression) << text.toStdString();
    }
    const QString checksum = QString::fromLatin1(SettingsKeys::EXPORT_XISF_CHECKSUM);
    for (const auto& [text, algorithm] :
         std::vector<std::pair<QString, an::OutputChecksum>>{{QStringLiteral("sha256"), an::OutputChecksum::Sha256},
                                                             {QStringLiteral("none"), an::OutputChecksum::None},
                                                             {QStringLiteral("sha3_512"), an::OutputChecksum::None}})
    {
        store.values.insert(checksum, text);
        EXPECT_EQ(an::ui::readExportSettings(store.reader()).xisfChecksum, algorithm) << text.toStdString();
    }
    store.values.insert(QString::fromLatin1(SettingsKeys::EXPORT_XISF_BYTE_SHUFFLE), QStringLiteral("0"));
    EXPECT_FALSE(an::ui::readExportSettings(store.reader()).xisfByteShuffle);

    const QString tiff = QString::fromLatin1(SettingsKeys::EXPORT_TIFF_COMPRESSION);
    for (const auto& [text, compression] :
         std::vector<std::pair<QString, an::OutputCompression>>{{QStringLiteral("none"), an::OutputCompression::None},
                                                                {QStringLiteral("lzw"), an::OutputCompression::Lzw},
                                                                {QStringLiteral("zip"), an::OutputCompression::Deflate},
                                                                {QStringLiteral("jpeg"), an::OutputCompression::Jpeg},
                                                                {QStringLiteral("rle"), an::OutputCompression::Lzw}})
    {
        store.values.insert(tiff, text);
        EXPECT_EQ(an::ui::readExportSettings(store.reader()).tiffCompression, compression) << text.toStdString();
    }
    store.values.insert(QString::fromLatin1(SettingsKeys::EXPORT_JPEG_QUALITY), QStringLiteral("250"));
    store.values.insert(QString::fromLatin1(SettingsKeys::EXPORT_PNG_COMPRESSION), QStringLiteral("-3"));
    const an::ui::ExportSettings held = an::ui::readExportSettings(store.reader());
    EXPECT_EQ(held.jpegQuality, 100u);
    EXPECT_EQ(held.pngCompressionLevel, 0u);
}

// The representation a container holds nearest the image's: its own when it can, the narrowest at least as wide
// otherwise, the widest when none is - sixteen bits in a PNG for anything wider than eight, eight in a JPEG.
TEST(FilePoliciesTest, TheRepresentationIsTheNearestTheContainerHolds)
{
    const auto encodeOf = [](an::CodecFormat format) {
        const std::optional<an::CodecCapabilities> capabilities = productRegistry().capabilities(format);
        return capabilities.has_value() ? capabilities->encodeSamples : 0u;
    };
    const std::uint32_t png = encodeOf(an::CodecFormat::Png);
    const std::uint32_t jpeg = encodeOf(an::CodecFormat::Jpeg);
    const std::uint32_t tiff = encodeOf(an::CodecFormat::Tiff);
    EXPECT_EQ(an::ui::exportSampleType(an::SampleType::UInt8, png), an::SampleType::UInt8);
    EXPECT_EQ(an::ui::exportSampleType(an::SampleType::UInt16, png), an::SampleType::UInt16);
    EXPECT_EQ(an::ui::exportSampleType(an::SampleType::Float32, png), an::SampleType::UInt16);
    EXPECT_EQ(an::ui::exportSampleType(an::SampleType::Float64, png), an::SampleType::UInt16);
    EXPECT_EQ(an::ui::exportSampleType(an::SampleType::Int16, png), an::SampleType::UInt16);
    EXPECT_EQ(an::ui::exportSampleType(an::SampleType::Int8, png), an::SampleType::UInt8);
    for (const an::SampleType type : {an::SampleType::UInt8, an::SampleType::UInt16, an::SampleType::Int32,
                                      an::SampleType::Float32, an::SampleType::Float64})
    {
        EXPECT_EQ(an::ui::exportSampleType(type, jpeg), an::SampleType::UInt8) << an::sampleTypeName(type);
        EXPECT_EQ(an::ui::exportSampleType(type, tiff), type) << an::sampleTypeName(type);
    }
    EXPECT_FALSE(an::ui::exportSampleType(an::SampleType::Float32, 0u).has_value());
}

// The intent each container is written with: its representation, its compression and level from the settings, and
// the cards a raster file carries only when EXIF is embedded.
TEST(FilePoliciesTest, EachContainerIsWrittenWithTheSettingsIntent)
{
    an::ui::ExportSettings settings;
    settings.tiffCompression = an::OutputCompression::Deflate;
    settings.jpegQuality = 80u;
    settings.pngCompressionLevel = 3u;
    settings.embedExif = false;
    const an::ImageDescriptor colour = described(an::SampleType::Float32, Channels::Rgb);

    an::Result<an::OutputIntent> png =
        an::ui::buildExportIntent(settings, productRegistry(), colour, an::CodecFormat::Png);
    ASSERT_TRUE(png.hasValue()) << png.status().message();
    EXPECT_EQ(png->sampleType(), an::SampleType::UInt16);
    EXPECT_EQ(png->compression(), an::OutputCompression::Deflate);
    EXPECT_EQ(png->compressionLevel(), 3u);
    EXPECT_FALSE(png->preserveSourceCards());

    an::Result<an::OutputIntent> jpeg =
        an::ui::buildExportIntent(settings, productRegistry(), colour, an::CodecFormat::Jpeg);
    ASSERT_TRUE(jpeg.hasValue()) << jpeg.status().message();
    EXPECT_EQ(jpeg->sampleType(), an::SampleType::UInt8);
    EXPECT_EQ(jpeg->compression(), an::OutputCompression::Jpeg);
    EXPECT_EQ(jpeg->compressionLevel(), 80u);

    an::Result<an::OutputIntent> tiff =
        an::ui::buildExportIntent(settings, productRegistry(), colour, an::CodecFormat::Tiff);
    ASSERT_TRUE(tiff.hasValue()) << tiff.status().message();
    EXPECT_EQ(tiff->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(tiff->compression(), an::OutputCompression::Deflate);
    EXPECT_FALSE(tiff->compressionLevel().has_value());

    // FITS and XISF always carry the image's header, world coordinates included, whatever the EXIF setting says.
    for (const an::CodecFormat format : {an::CodecFormat::Fits, an::CodecFormat::Xisf})
    {
        an::Result<an::OutputIntent> intent = an::ui::buildExportIntent(settings, productRegistry(), colour, format);
        ASSERT_TRUE(intent.hasValue()) << intent.status().message();
        EXPECT_EQ(intent->sampleType(), an::SampleType::Float32);
        EXPECT_EQ(intent->compression(), an::OutputCompression::None);
        EXPECT_TRUE(intent->preserveSourceCards());
    }
    settings.embedExif = true;
    an::Result<an::OutputIntent> embedded =
        an::ui::buildExportIntent(settings, productRegistry(), colour, an::CodecFormat::Jpeg);
    ASSERT_TRUE(embedded.hasValue());
    EXPECT_TRUE(embedded->preserveSourceCards());
}

// An XISF file is written in the form the settings choose: their compression, byte shuffling beside a compression
// alone, and their checksum, compressed or not.
TEST(FilePoliciesTest, AnXisfFileIsWrittenInTheFormTheSettingsChoose)
{
    an::ui::ExportSettings settings;
    settings.xisfCompression = an::OutputCompression::Zstd;
    settings.xisfByteShuffle = false;
    settings.xisfChecksum = an::OutputChecksum::Sha256;
    const an::ImageDescriptor colour = described(an::SampleType::Float32, Channels::Rgb);
    an::Result<an::OutputIntent> compressed =
        an::ui::buildExportIntent(settings, productRegistry(), colour, an::CodecFormat::Xisf);
    ASSERT_TRUE(compressed.hasValue()) << compressed.status().message();
    EXPECT_EQ(compressed->compression(), an::OutputCompression::Zstd);
    EXPECT_EQ(compressed->byteShuffle(), false);
    EXPECT_FALSE(compressed->compressionBlockBytes().has_value()) << "the container's own subblock size";
    EXPECT_EQ(compressed->checksum(), an::OutputChecksum::Sha256);
    EXPECT_TRUE(compressed->preserveSourceCards());

    settings.xisfCompression = an::OutputCompression::None;
    an::Result<an::OutputIntent> plain =
        an::ui::buildExportIntent(settings, productRegistry(), colour, an::CodecFormat::Xisf);
    ASSERT_TRUE(plain.hasValue()) << plain.status().message();
    EXPECT_EQ(plain->compression(), an::OutputCompression::None);
    EXPECT_FALSE(plain->byteShuffle().has_value()) << "nothing to shuffle for";
    EXPECT_EQ(plain->checksum(), an::OutputChecksum::Sha256);

    // The other containers state none of it, whatever the XISF settings say.
    settings.xisfCompression = an::OutputCompression::Lz4;
    for (const an::CodecFormat format : {an::CodecFormat::Fits, an::CodecFormat::Tiff, an::CodecFormat::Png})
    {
        an::Result<an::OutputIntent> other = an::ui::buildExportIntent(settings, productRegistry(), colour, format);
        ASSERT_TRUE(other.hasValue()) << other.status().message();
        EXPECT_FALSE(other->byteShuffle().has_value());
        EXPECT_EQ(other->checksum(), an::OutputChecksum::None);
    }
}

// A TIFF asked for the JPEG transform at a representation wider than eight bits is written with LZW, and the intent
// says so; at eight bits it is the transform.
TEST(FilePoliciesTest, ATiffTooWideForTheJpegTransformIsWrittenWithLzw)
{
    an::ui::ExportSettings settings;
    settings.tiffCompression = an::OutputCompression::Jpeg;
    an::Result<an::OutputIntent> wide = an::ui::buildExportIntent(
        settings, productRegistry(), described(an::SampleType::UInt16, Channels::Grey), an::CodecFormat::Tiff);
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    EXPECT_EQ(wide->compression(), an::OutputCompression::Lzw);
    an::Result<an::OutputIntent> narrow = an::ui::buildExportIntent(
        settings, productRegistry(), described(an::SampleType::UInt8, Channels::Grey), an::CodecFormat::Tiff);
    ASSERT_TRUE(narrow.hasValue()) << narrow.status().message();
    EXPECT_EQ(narrow->compression(), an::OutputCompression::Jpeg);
}

// A mosaic cannot be a PNG or a JPEG, which cannot state its filter array, and is refused before any work; FITS, XISF
// and TIFF hold it. A container the product does not write is refused too.
TEST(FilePoliciesTest, AMosaicIsRefusedForAContainerThatCannotStateItsFilterArray)
{
    const an::ImageDescriptor mosaic = described(an::SampleType::UInt16, Channels::Grey, true);
    for (const an::CodecFormat format : {an::CodecFormat::Png, an::CodecFormat::Jpeg})
    {
        an::Result<an::OutputIntent> refused = an::ui::buildExportIntent({}, productRegistry(), mosaic, format);
        ASSERT_FALSE(refused.hasValue()) << an::codecFormatName(format);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
        EXPECT_NE(std::string(refused.status().message()).find("filter array"), std::string::npos);
        EXPECT_FALSE(an::ui::containerHoldsFilterArray(format, an::CfaPattern::Rggb));
    }
    for (const an::CodecFormat format : {an::CodecFormat::Fits, an::CodecFormat::Xisf, an::CodecFormat::Tiff})
    {
        EXPECT_TRUE(an::ui::buildExportIntent({}, productRegistry(), mosaic, format).hasValue())
            << an::codecFormatName(format);
        EXPECT_TRUE(an::ui::containerHoldsFilterArray(format, an::CfaPattern::Rggb));
    }
    for (const an::CodecFormat format : {an::CodecFormat::Raw, an::CodecFormat::Dump, an::CodecFormat::Unknown})
    {
        an::Result<an::OutputIntent> refused = an::ui::buildExportIntent(
            {}, productRegistry(), described(an::SampleType::Float32, Channels::Grey), format);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
    }
}

// FITS states a mosaic's filters in one two-by-two pattern card and its codec refuses a tabulated mosaic, but the
// export took FITS for a container that holds every mosaic, so a save of an X-Trans mosaic as FITS failed in the codec,
// in the codec's words, after the conversion. It is refused before any work, naming the arrangement and the containers
// that hold it; XISF and TIFF hold it, and FITS still holds a two-by-two mosaic.
TEST(FilePoliciesTest, AnXTransMosaicIsRefusedForFitsWhichStatesATwoByTwoPatternOnly)
{
    const an::ImageDescriptor xtrans = describedXTrans();
    ASSERT_TRUE(xtrans.cfa().hasFilterTable());
    EXPECT_FALSE(an::ui::containerHoldsFilterArray(an::CodecFormat::Fits, an::CfaPattern::XTrans));
    EXPECT_TRUE(an::ui::containerHoldsFilterArray(an::CodecFormat::Fits, an::CfaPattern::Rggb));
    EXPECT_TRUE(an::ui::containerHoldsFilterArray(an::CodecFormat::Xisf, an::CfaPattern::XTrans));
    EXPECT_TRUE(an::ui::containerHoldsFilterArray(an::CodecFormat::Tiff, an::CfaPattern::XTrans));
    EXPECT_FALSE(an::ui::containerHoldsFilterArray(an::CodecFormat::Png, an::CfaPattern::XTrans));
    EXPECT_FALSE(an::ui::containerHoldsFilterArray(an::CodecFormat::Jpeg, an::CfaPattern::XTrans));
    for (const an::CodecFormat format : {an::CodecFormat::Fits, an::CodecFormat::Png, an::CodecFormat::Jpeg})
    {
        an::Result<an::OutputIntent> refused = an::ui::buildExportIntent({}, productRegistry(), xtrans, format);
        ASSERT_FALSE(refused.hasValue()) << an::codecFormatName(format);
        EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported) << an::codecFormatName(format);
        const std::string message = refused.status().message();
        EXPECT_NE(message.find("X-Trans"), std::string::npos) << message;
        EXPECT_NE(message.find("XISF or TIFF"), std::string::npos) << message;
    }
    for (const an::CodecFormat format : {an::CodecFormat::Xisf, an::CodecFormat::Tiff})
    {
        an::Result<an::OutputIntent> held = an::ui::buildExportIntent({}, productRegistry(), xtrans, format);
        EXPECT_TRUE(held.hasValue()) << an::codecFormatName(format) << ": " << held.status().message();
    }
}

// The demosaic setting names one of the published methods; a name the product does not know - DCB, HPHD and IGV among
// them - reads as RCD, the setting's default.
TEST(FilePoliciesTest, TheDemosaicSettingNamesAPublishedMethod)
{
    EXPECT_EQ(an::ui::demosaicMethodFromSettingsKey(QStringLiteral("bilinear")).method, an::DemosaicMethod::Fast);
    EXPECT_EQ(an::ui::demosaicMethodFromSettingsKey(QStringLiteral(" VNG4 ")).method, an::DemosaicMethod::Vng4);
    EXPECT_EQ(an::ui::demosaicMethodFromSettingsKey(QStringLiteral("ahd")).method, an::DemosaicMethod::LuminanceAhd);
    EXPECT_EQ(an::ui::demosaicMethodFromSettingsKey(QStringLiteral("AMaZE")).method, an::DemosaicMethod::Amaze);
    EXPECT_EQ(an::ui::demosaicMethodFromSettingsKey(QStringLiteral("lmmse")).method, an::DemosaicMethod::Lmmse);
    EXPECT_EQ(an::ui::demosaicMethodFromSettingsKey(QStringLiteral("rcd")).method, an::DemosaicMethod::Rcd);
    for (const char* unknown : {"dcb", "hphd", "igv", "magic"})
    {
        EXPECT_EQ(an::ui::demosaicMethodFromSettingsKey(QString::fromLatin1(unknown)).method, an::DemosaicMethod::Rcd)
            << unknown;
    }
    // The viewer's entry point reads through the same table.
    EXPECT_EQ(an::ui::demosaicMethodForSetting(QStringLiteral("amaze")).method, an::DemosaicMethod::Amaze);
}

// A Bayer-pattern text names one of the four arrays, at the image's origin; "Auto" is the array the image states and
// is refused for an image that states none, never guessed.
TEST(FilePoliciesTest, ABayerPatternTextIsStatedOrTheImagesOwnAndNeverGuessed)
{
    const an::ImageDescriptor mosaic = described(an::SampleType::UInt16, Channels::Grey, true);
    const an::ImageDescriptor grey = described(an::SampleType::UInt16, Channels::Grey);
    for (const auto& [text, pattern] :
         std::vector<std::pair<QString, an::CfaPattern>>{{an::ui::kBayerPatternRggbText, an::CfaPattern::Rggb},
                                                         {an::ui::kBayerPatternBggrText, an::CfaPattern::Bggr},
                                                         {an::ui::kBayerPatternGbrgText, an::CfaPattern::Gbrg},
                                                         {an::ui::kBayerPatternGrbgText, an::CfaPattern::Grbg}})
    {
        const an::ui::BayerPatternChoice choice = an::ui::bayerPatternChoiceFromText(text);
        ASSERT_TRUE(choice.stated.has_value());
        EXPECT_EQ(*choice.stated, pattern);
        an::Result<an::CfaDescriptor> resolved = an::ui::resolveBayerPattern(choice, grey);
        ASSERT_TRUE(resolved.hasValue());
        EXPECT_EQ(resolved->pattern(), pattern);
    }
    const an::ui::BayerPatternChoice fromImage = an::ui::bayerPatternChoiceFromText(an::ui::kBayerPatternFromImageText);
    EXPECT_FALSE(fromImage.stated.has_value());
    EXPECT_TRUE(fromImage.known);
    an::Result<an::CfaDescriptor> stated = an::ui::resolveBayerPattern(fromImage, mosaic);
    ASSERT_TRUE(stated.hasValue());
    EXPECT_EQ(*stated, mosaic.cfa());
    an::Result<an::CfaDescriptor> refused = an::ui::resolveBayerPattern(fromImage, grey);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_FALSE(an::ui::bayerPatternChoiceFromText(QStringLiteral("XYZW")).known);
}

// Every method a setting or a dialog offers is written for a two-by-two Bayer block, and the product asked the engine
// for it over an X-Trans mosaic too, which the engine refuses, so no X-Trans image was ever demosaiced. Over a
// six-by-six table the two fast estimates become the tabulated fast one and every other method the three-pass
// Markesteijn estimate; a method that applies is kept, and a tabulated method over a two-by-two mosaic becomes a Bayer
// one, so every pairing answers with a method that applies.
TEST(FilePoliciesTest, AMethodChosenForABayerMosaicBecomesItsTabulatedCounterpartOverAnXTransOne)
{
    using an::DemosaicMethod;
    const an::CfaDescriptor xtrans = an::tests::viewer::xtransFilterArray();
    ASSERT_TRUE(xtrans.hasFilterTable());
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    static_assert(an::ui::demosaicMethodForMosaic(DemosaicMethod::Rcd,
                                                  *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u)) ==
                      DemosaicMethod::Rcd,
                  "the rule is a constant expression");

    EXPECT_EQ(an::ui::demosaicMethodForMosaic(DemosaicMethod::Fast, xtrans), DemosaicMethod::XTransFast);
    EXPECT_EQ(an::ui::demosaicMethodForMosaic(DemosaicMethod::Bilinear, xtrans), DemosaicMethod::XTransFast);
    for (const DemosaicMethod method : {DemosaicMethod::Rcd, DemosaicMethod::Amaze, DemosaicMethod::Vng4,
                                        DemosaicMethod::Lmmse, DemosaicMethod::LuminanceAhd})
    {
        EXPECT_EQ(an::ui::demosaicMethodForMosaic(method, xtrans), DemosaicMethod::MarkesteijnThreePass)
            << an::demosaicMethodName(method);
    }
    const std::array<DemosaicMethod, 3u> tabulated{DemosaicMethod::XTransFast, DemosaicMethod::MarkesteijnOnePass,
                                                   DemosaicMethod::MarkesteijnThreePass};
    const std::array<DemosaicMethod, 7u> bayer{DemosaicMethod::Bilinear,    DemosaicMethod::Fast,  DemosaicMethod::Rcd,
                                               DemosaicMethod::Amaze,       DemosaicMethod::Lmmse, DemosaicMethod::Vng4,
                                               DemosaicMethod::LuminanceAhd};
    for (const DemosaicMethod method : tabulated)
    {
        EXPECT_EQ(an::ui::demosaicMethodForMosaic(method, xtrans), method) << an::demosaicMethodName(method);
    }
    for (const DemosaicMethod method : bayer)
    {
        EXPECT_EQ(an::ui::demosaicMethodForMosaic(method, *rggb), method) << an::demosaicMethodName(method);
    }
    EXPECT_EQ(an::ui::demosaicMethodForMosaic(DemosaicMethod::XTransFast, *rggb), DemosaicMethod::Fast);
    EXPECT_EQ(an::ui::demosaicMethodForMosaic(DemosaicMethod::MarkesteijnOnePass, *rggb), DemosaicMethod::Rcd);
    EXPECT_EQ(an::ui::demosaicMethodForMosaic(DemosaicMethod::MarkesteijnThreePass, *rggb), DemosaicMethod::Rcd);

    // An image that states no filter array is answered with the method chosen, which the engine refuses by name.
    std::vector<DemosaicMethod> every(tabulated.begin(), tabulated.end());
    every.insert(every.end(), bayer.begin(), bayer.end());
    ASSERT_EQ(every.size(), static_cast<std::size_t>(DemosaicMethod::LuminanceAhd) + 1u);
    for (const DemosaicMethod method : every)
    {
        EXPECT_EQ(an::ui::demosaicMethodForMosaic(method, an::CfaDescriptor{}), method)
            << an::demosaicMethodName(method);
        for (const an::CfaDescriptor& mosaic : {xtrans, *rggb})
        {
            EXPECT_TRUE(an::demosaicMethodAppliesTo(an::ui::demosaicMethodForMosaic(method, mosaic), mosaic.pattern()))
                << an::demosaicMethodName(method) << " over " << an::cfaPatternName(mosaic.pattern());
        }
    }
}

// A stated two-by-two pattern over an X-Trans mosaic was placed at the image's origin and the six-by-six samples
// demosaiced under it, making colour of nothing the sensor recorded. Each stated pattern is refused over such an image,
// naming the arrangement; From metadata reads the image's own table, and a stated pattern over an image that states
// none or a two-by-two one is kept.
TEST(FilePoliciesTest, AStatedBayerPatternIsRefusedOverAnXTransMosaicByName)
{
    const an::ImageDescriptor xtrans = describedXTrans();
    ASSERT_TRUE(xtrans.cfa().hasFilterTable());
    for (const QString& text : {an::ui::kBayerPatternRggbText, an::ui::kBayerPatternBggrText,
                                an::ui::kBayerPatternGbrgText, an::ui::kBayerPatternGrbgText})
    {
        const an::Result<an::CfaDescriptor> refused =
            an::ui::resolveBayerPattern(an::ui::bayerPatternChoiceFromText(text), xtrans);
        ASSERT_FALSE(refused.hasValue()) << text.toStdString();
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments) << text.toStdString();
        EXPECT_NE(std::string(refused.status().message()).find("X-Trans"), std::string::npos)
            << refused.status().message();
    }
    const an::Result<an::CfaDescriptor> own =
        an::ui::resolveBayerPattern(an::ui::bayerPatternChoiceFromText(an::ui::kBayerPatternFromImageText), xtrans);
    ASSERT_TRUE(own.hasValue()) << own.status().message();
    EXPECT_EQ(*own, xtrans.cfa());
    EXPECT_TRUE(own->hasFilterTable());

    const an::ui::BayerPatternChoice bggr = an::ui::bayerPatternChoiceFromText(an::ui::kBayerPatternBggrText);
    const an::Result<an::CfaDescriptor> overGrey =
        an::ui::resolveBayerPattern(bggr, described(an::SampleType::UInt16, Channels::Grey));
    ASSERT_TRUE(overGrey.hasValue()) << overGrey.status().message();
    EXPECT_EQ(overGrey->pattern(), an::CfaPattern::Bggr);
    const an::Result<an::CfaDescriptor> overBayer =
        an::ui::resolveBayerPattern(bggr, described(an::SampleType::UInt16, Channels::Grey, true));
    ASSERT_TRUE(overBayer.hasValue()) << overBayer.status().message();
    EXPECT_EQ(overBayer->pattern(), an::CfaPattern::Bggr);
}
