// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading a FITS image's header into a description and metadata.
//
// Images are counted in file order, skipping tables and empty primary units, and selected by
// zero-based index. The decoded representation follows BITPIX and the canonical unsigned and signed
// offsets; any other scaling decodes to Float64. Cards are read with CONTINUE long strings
// reassembled and re-quoted, so a string that looks numeric stays a string. An extension inherits
// the primary header's cards it does not state itself, less the structural ones, and every HISTORY
// and COMMENT record. Validating the payload checks the stored checksums and that the unit ends
// inside the file, which proves a tile-compressed image's structure but not its tiles.

#include "io/fits/FitsFile.h"

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace an
{
namespace
{
Result<double> number(fitsfile* file, const char* key, double fallback, std::string_view path)
{
    double value = fallback;
    int status = 0;
    fits_read_key(file, TDOUBLE, key, &value, nullptr, &status);
    if (status != 0 && status != KEY_NO_EXIST)
        return fitsFailure(status, "read FITS scaling", path);
    if (!std::isfinite(value))
        return Status::failure(ErrorCode::InvalidData);
    return status == KEY_NO_EXIST ? fallback : value;
}

std::string stringKey(fitsfile* file, const char* key)
{
    char value[FLEN_VALUE]{};
    int status = 0;
    fits_read_key(file, TSTRING, key, value, nullptr, &status);
    return status == 0 ? value : std::string{};
}

Result<std::vector<CodecCard>> readCards(fitsfile* file, const ExecutionContext& execution, std::string_view path)
{
    int status = 0, count = 0, more = 0;
    fits_get_hdrspace(file, &count, &more, &status);
    if (status != 0)
        return fitsFailure(status, "read FITS header", path);
    if (count < 0 || static_cast<std::size_t>(count) > kMaxCodecCards)
        return Status::failure(ErrorCode::LimitExceeded);
    std::vector<CodecCard> cards;
    cards.reserve(static_cast<std::size_t>(count));
    bool continuedString = false;
    for (int i = 1; i <= count; ++i)
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        char key[FLEN_KEYWORD]{}, value[FLEN_VALUE]{}, comment[FLEN_COMMENT]{};
        fits_read_keyn(file, i, key, value, comment, &status);
        if (status != 0)
            return fitsFailure(status, "read FITS card", path);
        if (*key == '\0')
            continue;
        if (continuedString && std::string_view(key) == "CONTINUE")
            continue;
        continuedString = false;
        std::string text(value);
        if (!text.empty() && text.front() == '\'')
        {
            // CFITSIO may use CONTINUE even for a short identity when its comment is long.
            // The header card bound above limits its allocation; the engine text bound below
            // still applies to the assembled value. Preserve lexical quoting for numeric strings.
            char* expanded = nullptr;
            fits_movabs_key(file, i, &status);
            fits_read_key_longstr(file, key, &expanded, nullptr, &status);
            struct Release
            {
                char* value;
                ~Release()
                {
                    int ignored = 0;
                    fits_free_memory(value, &ignored);
                }
            } release{expanded};
            if (status != 0)
                return fitsFailure(status, "read FITS continued string", path);
            if (std::strlen(expanded) > kMaxCodecCardTextBytes - 2u)
                return Status::failure(ErrorCode::LimitExceeded);
            text = "'";
            for (const char* p = expanded; *p != '\0'; ++p)
            {
                text += *p;
                if (*p == '\'')
                    text += '\'';
            }
            text += '\'';
            continuedString = true;
        }
        auto card = CodecCard::create(CodecNamespace::Fits, key, std::move(text), comment);
        if (!card.hasValue())
            return card.status();
        cards.push_back(std::move(*card));
    }
    return cards;
}

RawSampleFormat representation(int bitpix, double scale, double zero, FitsInteger64Policy integer64)
{
    if (bitpix == LONGLONG_IMG)
        return {8u, integer64 == FitsInteger64Policy::PreserveRaw ? RawSampleClass::SignedInteger
                                                                  : RawSampleClass::FloatingPoint};
    if (scale == 1.0)
    {
        if (bitpix == BYTE_IMG && zero == -128.0)
            return {1u, RawSampleClass::SignedInteger};
        if (bitpix == SHORT_IMG && zero == 32768.0)
            return {2u, RawSampleClass::UnsignedInteger};
        if (bitpix == LONG_IMG && zero == 2147483648.0)
            return {4u, RawSampleClass::UnsignedInteger};
        if (zero == 0.0)
        {
            if (bitpix == BYTE_IMG)
                return {1u, RawSampleClass::UnsignedInteger};
            if (bitpix == SHORT_IMG)
                return {2u, RawSampleClass::SignedInteger};
            if (bitpix == LONG_IMG)
                return {4u, RawSampleClass::SignedInteger};
            if (bitpix == FLOAT_IMG)
                return {4u, RawSampleClass::FloatingPoint};
        }
    }
    return {8u, RawSampleClass::FloatingPoint};
}
} // namespace

Result<FitsHeader> readFitsHeader(fitsfile* file, const EngineContext& engine, const ExecutionContext& execution,
                                  const CodecReadRequest& request, FitsInteger64Policy integer64, bool enumerateImages)
{
    int status = 0, hduCount = static_cast<int>(kMaxCodecCards);
    // Only a public probe needs the total count. Decode's facade already performed that probe;
    // locate its requested image without walking to EOF and back a second time.
    if (enumerateImages)
        fits_get_num_hdus(file, &hduCount, &status);
    if (status != 0)
        return fitsFailure(status, "count FITS images", request.path);
    if (hduCount > static_cast<int>(kMaxCodecCards))
        return Status::failure(ErrorCode::LimitExceeded);
    int selected = 0;
    std::uint64_t imageCount = 0u;
    for (int hdu = 1; hdu <= hduCount; ++hdu)
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        int type = 0, axes = 0;
        fits_movabs_hdu(file, hdu, &type, &status);
        if (!enumerateImages && status == END_OF_FILE)
        {
            status = 0;
            break;
        }
        const bool compressed = fits_is_compressed_image(file, &status) != 0;
        if (type == IMAGE_HDU || compressed)
        {
            fits_get_img_dim(file, &axes, &status);
            if (axes > 0)
            {
                if (imageCount == request.imageIndex)
                    selected = hdu;
                ++imageCount;
            }
        }
        if (status != 0)
            return fitsFailure(status, "select FITS image", request.path);
        if (!enumerateImages && selected != 0)
            break;
    }
    if (selected == 0)
        return Status::failure(ErrorCode::OutOfRange);
    int type = 0, axisCount = 0, bitpix = 0;
    std::array<LONGLONG, 9> axes{};
    fits_movabs_hdu(file, selected, &type, &status);
    fits_get_img_paramll(file, static_cast<int>(axes.size()), &bitpix, &axisCount, axes.data(), &status);
    const bool compressed = fits_is_compressed_image(file, &status) != 0;
    if (status != 0)
        return fitsFailure(status, "describe FITS image", request.path);
    if (axisCount < 1 || axisCount > static_cast<int>(axes.size()))
        return Status::failure(ErrorCode::Unsupported);
    for (int axis = 3; axis < axisCount; ++axis)
        if (axes[axis] != 1)
            return Status::failure(ErrorCode::Unsupported);
    const auto width = axes[0], height = axisCount >= 2 ? axes[1] : 1, channels = axisCount >= 3 ? axes[2] : 1;
    if (width <= 0 || height <= 0 || channels <= 0 || width > engine.limits().maxImageWidth ||
        height > engine.limits().maxImageHeight || channels > engine.limits().maxChannelCount)
        return Status::failure(ErrorCode::LimitExceeded);
    if (bitpix != BYTE_IMG && bitpix != SHORT_IMG && bitpix != LONG_IMG && bitpix != LONGLONG_IMG &&
        bitpix != FLOAT_IMG && bitpix != DOUBLE_IMG)
        return Status::failure(ErrorCode::Unsupported);
    auto scale = number(file, "BSCALE", 1.0, request.path), zero = number(file, "BZERO", 0.0, request.path);
    if (!scale.hasValue())
        return scale.status();
    if (!zero.hasValue())
        return zero.status();
    if (*scale == 0.0)
        return Status::failure(ErrorCode::InvalidData);
    const auto format = representation(bitpix, *scale, *zero, integer64);
    const auto checkedExtent =
        ImageExtent::create(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                            static_cast<std::uint32_t>(channels), engine.limits());
    if (!checkedExtent)
        return Status::failure(ErrorCode::LimitExceeded);
    const auto extent = *checkedExtent;
    const auto layout = ImageLayout::create({extent, format.sizeBytes, 1u, extent.channelCount(), 1u}, engine.limits());
    if (!layout)
        return Status::failure(ErrorCode::LimitExceeded);
    CfaDescriptor cfa{};
    const auto bayer = stringKey(file, "BAYERPAT");
    if (channels == 1)
    {
        for (const auto pattern : {CfaPattern::Rggb, CfaPattern::Bggr, CfaPattern::Gbrg, CfaPattern::Grbg})
            if (bayer == (pattern == CfaPattern::Rggb   ? "RGGB"
                          : pattern == CfaPattern::Bggr ? "BGGR"
                          : pattern == CfaPattern::Gbrg ? "GBRG"
                                                        : "GRBG"))
                cfa = *CfaDescriptor::create(pattern, 0u, 0u);
    }
    const auto descriptor = RawFrameDescriptor::create(
        {.extent = extent,
         .sampleFormat = format,
         .byteOrder = ByteOrder::Native,
         .channelSchema = channels == 1   ? ChannelSchema::Mono
                          : channels == 3 ? ChannelSchema::Rgb
                                          : ChannelSchema::Named,
         .colorSpace = ColorSpace::Unspecified,
         .numericDomain = format.sampleClass == RawSampleClass::FloatingPoint ? NumericDomain::PhysicalScaled
                                                                              : NumericDomain::RawSensorCode,
         .cfa = cfa,
         .coordinateOrigin =
             stringKey(file, "ROWORDER") == "BOTTOM-UP" ? CoordinateOrigin::BottomLeft : CoordinateOrigin::TopLeft,
         .pixelAspectRatio = 1.0});
    if (!descriptor)
        return Status::failure(ErrorCode::InvalidData);
    ImageMetadata metadata;
    if (request.readMetadata)
    {
        auto cards = readCards(file, execution, request.path);
        if (!cards.hasValue())
            return cards.status();
        if (selected != 1)
        {
            fits_movabs_hdu(file, 1, nullptr, &status);
            auto primary = readCards(file, execution, request.path);
            fits_movabs_hdu(file, selected, nullptr, &status);
            if (status != 0)
                return fitsFailure(status, "read primary FITS metadata", request.path);
            if (!primary.hasValue())
                return primary.status();
            for (auto& card : *primary)
            {
                if (isStructuralFitsKeyword(card.keyword()))
                    continue;
                const bool repeated = card.keyword() == "HISTORY" || card.keyword() == "COMMENT";
                const bool overridden = std::any_of(cards->begin(), cards->end(), [&](const CodecCard& chosen) {
                    return chosen.keyword() == card.keyword();
                });
                if (repeated || !overridden)
                    cards->push_back(std::move(card));
            }
        }
        const RawSampleFormat stored{static_cast<std::uint8_t>(std::abs(bitpix) / 8),
                                     bitpix < 0    ? RawSampleClass::FloatingPoint
                                     : bitpix == 8 ? RawSampleClass::UnsignedInteger
                                                   : RawSampleClass::SignedInteger};
        auto codec = CodecMetadata::create({stored, bitpix == 8 ? ByteOrder::Native : ByteOrder::Big, *scale, *zero},
                                           std::move(*cards));
        if (!codec.hasValue())
            return codec.status();
        auto projected = fitsMetadata(std::move(*codec), extent, request.path, integer64, bitpix == LONGLONG_IMG);
        if (!projected.hasValue())
            return projected.status();
        metadata = std::move(*projected);
    }
    else
    {
        const RawSampleFormat stored{static_cast<std::uint8_t>(std::abs(bitpix) / 8),
                                     bitpix < 0    ? RawSampleClass::FloatingPoint
                                     : bitpix == 8 ? RawSampleClass::UnsignedInteger
                                                   : RawSampleClass::SignedInteger};
        auto codec =
            CodecMetadata::create({stored, bitpix == 8 ? ByteOrder::Native : ByteOrder::Big, *scale, *zero}, {});
        if (!codec.hasValue())
            return codec.status();
        metadata = ImageMetadata::create({}, {}, {}, {}, std::move(*codec), std::nullopt);
    }
    auto disposition = CodecProbeDisposition::DecodeRequired;
    if (request.validatePayload)
    {
        LONGLONG end = 0;
        fits_get_hduaddrll(file, nullptr, nullptr, &end, &status);
        std::error_code error;
        const auto size =
            std::filesystem::file_size(std::filesystem::path(std::u8string_view(
                                           reinterpret_cast<const char8_t*>(request.path.data()), request.path.size())),
                                       error);
        int dataChecksum = 0, headerChecksum = 0;
        fits_verify_chksum(file, &dataChecksum, &headerChecksum, &status);
        if (status != 0 || error || end < 0 || static_cast<std::uint64_t>(end) > size || dataChecksum < 0 ||
            headerChecksum < 0)
            return codecFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, CodecFormat::Fits,
                                "validate FITS payload", request.path);
        if (!compressed)
            disposition = CodecProbeDisposition::PayloadValidatedWithoutDecode;
    }
    return FitsHeader{{*descriptor, *layout, imageCount, std::move(metadata), disposition},
                      selected,
                      bitpix,
                      fitsDatatype(format),
                      *scale,
                      *zero,
                      compressed};
}
} // namespace an
