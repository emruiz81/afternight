// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The product's settings as values an open and a save keep: the RAW policy and its registry, and the export intent.

#include "app/FilePolicies.h"

#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>

#include <AfterNightPlatform/Logging.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace an::ui
{

namespace
{

/// The eight representations, in the order the engine enumerates them.
constexpr std::array<an::SampleType, 8u> kSampleTypes{
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

/// The three kinds of number a representation holds.
enum class NumberKind : std::uint8_t
{
    Unsigned,
    Signed,
    Floating
};

[[nodiscard]] constexpr NumberKind kindOf(an::SampleType type) noexcept
{
    if (an::isFloatingSampleType(type))
    {
        return NumberKind::Floating;
    }
    return an::isSignedSampleType(type) ? NumberKind::Signed : NumberKind::Unsigned;
}

/// How much a candidate is preferred among candidates of one width for an image: the image's own kind first, then
/// unsigned integers, signed integers and floating point. Lower is preferred.
[[nodiscard]] constexpr int kindRank(an::SampleType candidate, an::SampleType source) noexcept
{
    const NumberKind kind = kindOf(candidate);
    if (kind == kindOf(source))
    {
        return 0;
    }
    return kind == NumberKind::Unsigned ? 1 : kind == NumberKind::Signed ? 2 : 3;
}

/// A stored gain, or one when the store holds no positive finite number.
[[nodiscard]] double storedGain(const QString& stored)
{
    bool ok = false;
    const double value = stored.trimmed().toDouble(&ok);
    return ok && std::isfinite(value) && value > 0.0 ? value : 1.0;
}

/// A stored integer held to a range, or the default when the store holds no integer.
[[nodiscard]] std::uint32_t storedLevel(const QString& stored, std::uint32_t low, std::uint32_t high,
                                        std::uint32_t fallback)
{
    bool ok = false;
    const int value = stored.trimmed().toInt(&ok);
    if (!ok)
    {
        return fallback;
    }
    return static_cast<std::uint32_t>(
        std::clamp<long long>(value, static_cast<long long>(low), static_cast<long long>(high)));
}

/// The mosaic balance a setting asks the filter-array step for.
[[nodiscard]] an::CfaWhiteBalanceMode mosaicBalanceOf(RawWhiteBalanceSetting mode) noexcept
{
    switch (mode)
    {
    case RawWhiteBalanceSetting::None:
        return an::CfaWhiteBalanceMode::None;
    case RawWhiteBalanceSetting::Daylight:
        return an::CfaWhiteBalanceMode::Daylight;
    case RawWhiteBalanceSetting::CameraAsShot:
        return an::CfaWhiteBalanceMode::Camera;
    case RawWhiteBalanceSetting::Automatic:
        return an::CfaWhiteBalanceMode::Automatic;
    case RawWhiteBalanceSetting::Custom:
        return an::CfaWhiteBalanceMode::Custom;
    }
    return an::CfaWhiteBalanceMode::Daylight;
}

/// The full-colour balance a setting configures the camera RAW adapter with.
[[nodiscard]] an::RawFullColorWhiteBalance fullColourBalanceOf(RawWhiteBalanceSetting mode) noexcept
{
    switch (mode)
    {
    case RawWhiteBalanceSetting::None:
        return an::RawFullColorWhiteBalance::None;
    case RawWhiteBalanceSetting::Daylight:
        return an::RawFullColorWhiteBalance::Daylight;
    case RawWhiteBalanceSetting::CameraAsShot:
        return an::RawFullColorWhiteBalance::Camera;
    case RawWhiteBalanceSetting::Automatic:
        return an::RawFullColorWhiteBalance::Automatic;
    case RawWhiteBalanceSetting::Custom:
        return an::RawFullColorWhiteBalance::Custom;
    }
    return an::RawFullColorWhiteBalance::Daylight;
}

/// The settings with every gain positive and finite.
[[nodiscard]] RawOpenSettings checkedSettings(RawOpenSettings settings) noexcept
{
    for (double& gain : settings.customGains)
    {
        if (!std::isfinite(gain) || gain <= 0.0)
        {
            gain = 1.0;
        }
    }
    return settings;
}

/// A failure the policies decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

} // namespace

RawWhiteBalanceSetting rawWhiteBalanceFromStored(const QString& stored) noexcept
{
    bool ok = false;
    const int value = stored.trimmed().toInt(&ok);
    if (!ok)
    {
        return RawWhiteBalanceSetting::Daylight;
    }
    switch (value)
    {
    case 0:
        return RawWhiteBalanceSetting::None;
    case 1:
        return RawWhiteBalanceSetting::Daylight;
    case 2:
        return RawWhiteBalanceSetting::CameraAsShot;
    case 3:
        return RawWhiteBalanceSetting::Automatic;
    case 4:
        return RawWhiteBalanceSetting::Custom;
    default:
        break;
    }
    return RawWhiteBalanceSetting::Daylight;
}

QString storedRawWhiteBalance(RawWhiteBalanceSetting mode)
{
    switch (mode)
    {
    case RawWhiteBalanceSetting::None:
        return QStringLiteral("0");
    case RawWhiteBalanceSetting::Daylight:
        return QStringLiteral("1");
    case RawWhiteBalanceSetting::CameraAsShot:
        return QStringLiteral("2");
    case RawWhiteBalanceSetting::Automatic:
        return QStringLiteral("3");
    case RawWhiteBalanceSetting::Custom:
        return QStringLiteral("4");
    }
    return QStringLiteral("1");
}

RawOpenSettings readRawOpenSettings(const SettingReader& read)
{
    RawOpenSettings settings;
    if (!read)
    {
        return settings;
    }
    settings.whiteBalance = rawWhiteBalanceFromStored(
        read(QString::fromLatin1(SettingsKeys::RAW_WHITE_BALANCE_MODE), storedRawWhiteBalance(settings.whiteBalance)));
    settings.customGains = {
        storedGain(read(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_R), QStringLiteral("1.0"))),
        storedGain(read(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_G), QStringLiteral("1.0"))),
        storedGain(read(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_B), QStringLiteral("1.0")))};
    settings.autoRotate =
        read(QString::fromLatin1(SettingsKeys::RAW_AUTO_ROTATE),
             SettingsDefaults::RAW_AUTO_ROTATE ? QStringLiteral("1") : QStringLiteral("0")) == QStringLiteral("1");
    return settings;
}

void writeRawOpenSettings(const RawOpenSettings& settings, const SettingWriter& write)
{
    if (!write)
    {
        return;
    }
    write(QString::fromLatin1(SettingsKeys::RAW_AUTO_ROTATE),
          settings.autoRotate ? QStringLiteral("1") : QStringLiteral("0"));
    write(QString::fromLatin1(SettingsKeys::RAW_WHITE_BALANCE_MODE), storedRawWhiteBalance(settings.whiteBalance));
    write(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_R), QString::number(settings.customGains[0], 'f', 6));
    write(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_G), QString::number(settings.customGains[1], 'f', 6));
    write(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_B), QString::number(settings.customGains[2], 'f', 6));
}

an::Result<std::shared_ptr<const an::CodecRegistry>> createProductCodecRegistry(const an::RawCodecOptions& raw)
{
    const std::array<an::Result<std::shared_ptr<const an::ImageCodec>>, 6u> adapters{
        an::createFitsCodec(), an::createXisfCodec(), an::createTiffCodec(),
        an::createPngCodec(),  an::createJpegCodec(), an::createRawCodec(raw)};
    std::vector<std::shared_ptr<const an::ImageCodec>> codecs;
    codecs.reserve(adapters.size());
    for (const an::Result<std::shared_ptr<const an::ImageCodec>>& adapter : adapters)
    {
        if (!adapter.hasValue())
        {
            return adapter.status();
        }
        codecs.push_back(*adapter);
    }
    return an::CodecRegistry::create(codecs);
}

RawOpenPolicy::RawOpenPolicy(const RawOpenSettings& settings, std::shared_ptr<const an::CodecRegistry> registry)
    : m_settings(settings), m_registry(std::move(registry)),
      m_treatment(ViewRawTreatment{.mosaicBalance = mosaicBalanceOf(settings.whiteBalance),
                                   .customGains = settings.customGains,
                                   .autoRotate = settings.autoRotate})
{
}

an::Result<std::shared_ptr<const RawOpenPolicy>> RawOpenPolicy::create(const RawOpenSettings& settings)
{
    const RawOpenSettings checked = checkedSettings(settings);
    an::RawCodecOptions options;
    options.fullColorWhiteBalance = fullColourBalanceOf(checked.whiteBalance);
    options.fullColorMultipliers[0] = checked.customGains[0];
    options.fullColorMultipliers[1] = checked.customGains[1];
    options.fullColorMultipliers[2] = checked.customGains[2];
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = createProductCodecRegistry(options);
    if (!registry.hasValue())
    {
        return registry.status();
    }
    return std::shared_ptr<const RawOpenPolicy>(new RawOpenPolicy(checked, std::move(*registry)));
}

ExportSettings readExportSettings(const SettingReader& read)
{
    ExportSettings settings;
    if (!read)
    {
        return settings;
    }
    settings.tiffCompression =
        tiffCompressionFromStored(read(QString::fromLatin1(SettingsKeys::EXPORT_TIFF_COMPRESSION),
                                       QString::fromLatin1(SettingsDefaults::EXPORT_TIFF_COMPRESSION)));
    settings.xisfCompression =
        xisfCompressionFromStored(read(QString::fromLatin1(SettingsKeys::EXPORT_XISF_COMPRESSION),
                                       QString::fromLatin1(SettingsDefaults::EXPORT_XISF_COMPRESSION)));
    settings.xisfByteShuffle =
        read(QString::fromLatin1(SettingsKeys::EXPORT_XISF_BYTE_SHUFFLE),
             SettingsDefaults::EXPORT_XISF_BYTE_SHUFFLE ? QStringLiteral("1") : QStringLiteral("0")) ==
        QStringLiteral("1");
    settings.xisfChecksum = xisfChecksumFromStored(read(QString::fromLatin1(SettingsKeys::EXPORT_XISF_CHECKSUM),
                                                        QString::fromLatin1(SettingsDefaults::EXPORT_XISF_CHECKSUM)));
    settings.jpegQuality = storedLevel(read(QString::fromLatin1(SettingsKeys::EXPORT_JPEG_QUALITY),
                                            QString::number(SettingsDefaults::EXPORT_JPEG_QUALITY)),
                                       1u, 100u, static_cast<std::uint32_t>(SettingsDefaults::EXPORT_JPEG_QUALITY));
    settings.pngCompressionLevel =
        storedLevel(read(QString::fromLatin1(SettingsKeys::EXPORT_PNG_COMPRESSION),
                         QString::number(SettingsDefaults::EXPORT_PNG_COMPRESSION)),
                    0u, 9u, static_cast<std::uint32_t>(SettingsDefaults::EXPORT_PNG_COMPRESSION));
    settings.embedExif =
        read(QString::fromLatin1(SettingsKeys::EXPORT_EMBED_EXIF),
             SettingsDefaults::EXPORT_EMBED_EXIF ? QStringLiteral("1") : QStringLiteral("0")) == QStringLiteral("1");
    settings.embedHistory =
        read(QString::fromLatin1(SettingsKeys::EXPORT_EMBED_HISTORY),
             SettingsDefaults::EXPORT_EMBED_HISTORY ? QStringLiteral("1") : QStringLiteral("0")) == QStringLiteral("1");
    return settings;
}

an::OutputCompression tiffCompressionFromStored(const QString& stored)
{
    const QString text = stored.trimmed();
    if (text == QStringLiteral("none"))
    {
        return an::OutputCompression::None;
    }
    if (text == QStringLiteral("zip"))
    {
        return an::OutputCompression::Deflate;
    }
    if (text == QStringLiteral("jpeg"))
    {
        return an::OutputCompression::Jpeg;
    }
    return an::OutputCompression::Lzw;
}

QString storedTiffCompression(const an::OutputCompression compression)
{
    switch (compression)
    {
    case an::OutputCompression::None:
        return QStringLiteral("none");
    case an::OutputCompression::Deflate:
        return QStringLiteral("zip");
    case an::OutputCompression::Jpeg:
        return QStringLiteral("jpeg");
    default:
        return QStringLiteral("lzw");
    }
}

namespace
{

/// The XISF compressions the setting names, with the text each is stored as.
constexpr std::array<std::pair<an::OutputCompression, const char*>, 5u> kXisfCompressionTexts{
    {{an::OutputCompression::None, "none"},
     {an::OutputCompression::Deflate, "zlib"},
     {an::OutputCompression::Lz4, "lz4"},
     {an::OutputCompression::Lz4Hc, "lz4hc"},
     {an::OutputCompression::Zstd, "zstd"}}};

/// The XISF checksums the setting names, with the text each is stored as.
constexpr std::array<std::pair<an::OutputChecksum, const char*>, 2u> kXisfChecksumTexts{
    {{an::OutputChecksum::None, "none"}, {an::OutputChecksum::Sha256, "sha256"}}};

} // namespace

an::OutputCompression xisfCompressionFromStored(const QString& stored)
{
    const QString text = stored.trimmed();
    for (const auto& [compression, name] : kXisfCompressionTexts)
    {
        if (text == QLatin1String(name))
        {
            return compression;
        }
    }
    return an::OutputCompression::None;
}

QString storedXisfCompression(const an::OutputCompression compression)
{
    for (const auto& [named, name] : kXisfCompressionTexts)
    {
        if (named == compression)
        {
            return QString::fromLatin1(name);
        }
    }
    return QStringLiteral("none");
}

an::OutputChecksum xisfChecksumFromStored(const QString& stored)
{
    const QString text = stored.trimmed();
    for (const auto& [checksum, name] : kXisfChecksumTexts)
    {
        if (text == QLatin1String(name))
        {
            return checksum;
        }
    }
    return an::OutputChecksum::None;
}

QString storedXisfChecksum(const an::OutputChecksum checksum)
{
    for (const auto& [named, name] : kXisfChecksumTexts)
    {
        if (named == checksum)
        {
            return QString::fromLatin1(name);
        }
    }
    return QStringLiteral("none");
}

void writeExportSettings(const ExportSettings& settings, const SettingWriter& write)
{
    if (!write)
    {
        return;
    }
    write(QString::fromLatin1(SettingsKeys::EXPORT_TIFF_COMPRESSION), storedTiffCompression(settings.tiffCompression));
    write(QString::fromLatin1(SettingsKeys::EXPORT_XISF_COMPRESSION), storedXisfCompression(settings.xisfCompression));
    write(QString::fromLatin1(SettingsKeys::EXPORT_XISF_BYTE_SHUFFLE),
          settings.xisfByteShuffle ? QStringLiteral("1") : QStringLiteral("0"));
    write(QString::fromLatin1(SettingsKeys::EXPORT_XISF_CHECKSUM), storedXisfChecksum(settings.xisfChecksum));
    write(QString::fromLatin1(SettingsKeys::EXPORT_JPEG_QUALITY), QString::number(settings.jpegQuality));
    write(QString::fromLatin1(SettingsKeys::EXPORT_PNG_COMPRESSION), QString::number(settings.pngCompressionLevel));
    write(QString::fromLatin1(SettingsKeys::EXPORT_EMBED_EXIF),
          settings.embedExif ? QStringLiteral("1") : QStringLiteral("0"));
    write(QString::fromLatin1(SettingsKeys::EXPORT_EMBED_HISTORY),
          settings.embedHistory ? QStringLiteral("1") : QStringLiteral("0"));
}

std::optional<an::SampleType> exportSampleType(an::SampleType source, std::uint32_t encodeSamples) noexcept
{
    std::array<an::SampleType, 8u> encodable{};
    std::size_t count = 0u;
    bool holdsSource = false;
    for (const an::SampleType type : kSampleTypes)
    {
        if ((encodeSamples & an::codecSampleBit(an::rawSampleFormatOf(type))) != 0u)
        {
            encodable[count++] = type;
            holdsSource = holdsSource || type == source;
        }
    }
    if (count == 0u)
    {
        return std::nullopt;
    }
    if (holdsSource)
    {
        return source;
    }
    // The narrowest at least as wide as the image; the widest when none is. Among one width, by kind.
    const std::uint32_t sourceWidth = an::sampleTypeSizeBytes(source);
    an::SampleType chosen = encodable[0];
    for (std::size_t index = 1u; index < count; ++index)
    {
        const an::SampleType candidate = encodable[index];
        const std::uint32_t width = an::sampleTypeSizeBytes(candidate);
        const std::uint32_t chosenWidth = an::sampleTypeSizeBytes(chosen);
        const bool holdsImage = width >= sourceWidth;
        const bool chosenHoldsImage = chosenWidth >= sourceWidth;
        bool better = false;
        if (holdsImage != chosenHoldsImage)
        {
            better = holdsImage;
        }
        else if (width != chosenWidth)
        {
            better = holdsImage ? width < chosenWidth : width > chosenWidth;
        }
        else
        {
            better = kindRank(candidate, source) < kindRank(chosen, source);
        }
        if (better)
        {
            chosen = candidate;
        }
    }
    return chosen;
}

an::Result<an::OutputIntent> buildExportIntent(const ExportSettings& settings, const an::CodecRegistry& registry,
                                               const an::ImageDescriptor& source, an::CodecFormat container)
{
    if (container != an::CodecFormat::Fits && container != an::CodecFormat::Xisf &&
        container != an::CodecFormat::Tiff && container != an::CodecFormat::Png && container != an::CodecFormat::Jpeg)
    {
        return refusal(an::ErrorCode::Unsupported, "the product writes FITS, XISF, TIFF, PNG and JPEG files only");
    }
    if (source.isMosaiced() && !containerHoldsFilterArray(container, source.cfa().pattern()))
    {
        if (an::cfaPatternIsTabulated(source.cfa().pattern()))
        {
            return refusal(an::ErrorCode::Unsupported,
                           "an X-Trans mosaic cannot be written as FITS, PNG or JPEG, none of which can state its "
                           "six-by-six filter array; write it as XISF or TIFF, or demosaic it first");
        }
        return refusal(an::ErrorCode::Unsupported,
                       "a colour filter array mosaic cannot be written as PNG or JPEG, which cannot state its filter "
                       "array; write it as FITS, XISF or TIFF, or demosaic it first");
    }
    const std::optional<an::CodecCapabilities> capabilities = registry.capabilities(container);
    if (!capabilities.has_value() || capabilities->encodeSamples == 0u)
    {
        return refusal(an::ErrorCode::Unsupported, "no adapter writes this container");
    }
    const std::optional<an::SampleType> written = exportSampleType(source.sampleType(), capabilities->encodeSamples);
    if (!written.has_value())
    {
        return refusal(an::ErrorCode::Unsupported, "the container holds none of the representations the engine writes");
    }
    an::OutputIntentRequest request{.sampleType = *written};
    request.preserveSourceCards = true;
    switch (container)
    {
    case an::CodecFormat::Png:
        request.compression = an::OutputCompression::Deflate;
        request.compressionLevel = settings.pngCompressionLevel;
        request.preserveSourceCards = settings.embedExif;
        break;
    case an::CodecFormat::Jpeg:
        request.compression = an::OutputCompression::Jpeg;
        request.compressionLevel = settings.jpegQuality;
        request.preserveSourceCards = settings.embedExif;
        break;
    case an::CodecFormat::Tiff:
        request.compression = settings.tiffCompression;
        if (request.compression == an::OutputCompression::Jpeg && *written != an::SampleType::UInt8)
        {
            // The JPEG transform is defined for eight bits; a wider image is written with LZW instead, and the intent
            // states that it is.
            LOG_INFO("Export: a TIFF of {} samples cannot use JPEG compression; it is written with LZW",
                     an::sampleTypeName(*written));
            request.compression = an::OutputCompression::Lzw;
        }
        request.preserveSourceCards = settings.embedExif;
        break;
    case an::CodecFormat::Xisf:
        // The pixel data in the form the settings choose; byte shuffling is a step of a compression, so a file
        // written without one states none.
        request.compression = settings.xisfCompression;
        if (request.compression != an::OutputCompression::None)
        {
            request.byteShuffle = settings.xisfByteShuffle;
        }
        request.checksum = settings.xisfChecksum;
        break;
    default:
        request.compression = an::OutputCompression::None;
        break;
    }
    return an::OutputIntent::create(request);
}

} // namespace an::ui
