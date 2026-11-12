// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading a FITS image's header into a description and metadata.
//
// Images are counted in file order as the library finds them - the primary array, IMAGE extensions and tile-compressed
// images, each with at least one axis and no axis of length zero, so tables and empty primary units are skipped - and
// selected by zero-based index. The decoded representation follows BITPIX and the canonical unsigned and signed
// offsets; any other scaling decodes to Float64. A tile-compressed image is described by the header its decompression
// restores, as the image it holds. Cards are read from the header's records by the one reading
// `io/fits/FitsCardRecords.h` holds, with CONTINUE long strings joined and re-quoted, so a string that looks numeric
// stays a string. An extension inherits the primary header's cards it does not state itself, less the structural ones,
// and every HISTORY and COMMENT record. Validating the payload checks that the unit ends inside the file and, where
// its header states them, its checksums, which proves a tile-compressed image's structure but not its tiles.

#include "io/fits/FitsCardRecords.h"
#include "io/fits/FitsFile.h"
#include "io/fits/FitsMetadata.h"

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <openfits/checksum.h>
#include <openfits/hdu.h>
#include <openfits/header.h>
#include <openfits/image.h>
#include <openfits/io.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an
{
namespace
{
/// The bytes of a FITS block, which every unit's data fill to a whole number of.
constexpr std::uint64_t kFitsBlockBytes = 2880u;

/// The most axes an image may name; those after the third must each have one element.
constexpr std::size_t kMaxFitsAxes = 9u;

/// The text of a keyword's value, or empty when the header states none or none that reads as text.
std::string stringKey(const openfits::header& cards, std::string_view keyword)
{
    try
    {
        return cards.find<std::string>(keyword).value_or(std::string{});
    }
    catch (const openfits::error&)
    {
        return {};
    }
}

/// The cards of a header, as the codec reads them from its records.
Result<std::vector<CodecCard>> cardsOf(const openfits::header& header, const ExecutionContext& execution)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    // Every record before the blank ones that end a header counts, continuations and commentary included, as a
    // header's records always have against this bound.
    if (header.size() - header.free_cards() > kMaxCodecCards)
        return Status::failure(ErrorCode::LimitExceeded);
    // The records the file holds, each card read from one kept as it was read; a card the library restored for a
    // compressed image is formatted as its writer would write it.
    return readFitsRecordCards(header.to_string());
}

/// The decoded representation of a stored type under its scaling.
RawSampleFormat representation(int bitpix, double scale, double zero, FitsInteger64Policy integer64)
{
    if (bitpix == 64)
        return {8u, integer64 == FitsInteger64Policy::PreserveRaw ? RawSampleClass::SignedInteger
                                                                  : RawSampleClass::FloatingPoint};
    if (scale == 1.0)
    {
        if (bitpix == 8 && zero == -128.0)
            return {1u, RawSampleClass::SignedInteger};
        if (bitpix == 16 && zero == 32768.0)
            return {2u, RawSampleClass::UnsignedInteger};
        if (bitpix == 32 && zero == 2147483648.0)
            return {4u, RawSampleClass::UnsignedInteger};
        if (zero == 0.0)
        {
            if (bitpix == 8)
                return {1u, RawSampleClass::UnsignedInteger};
            if (bitpix == 16)
                return {2u, RawSampleClass::SignedInteger};
            if (bitpix == 32)
                return {4u, RawSampleClass::SignedInteger};
            if (bitpix == -32)
                return {4u, RawSampleClass::FloatingPoint};
        }
    }
    return {8u, RawSampleClass::FloatingPoint};
}

/// The unit of the image at `imageIndex`, counting images from the first, and how many images the file holds - all of
/// them when `enumerateImages`, or those up to the selected one otherwise.
struct Selection
{
    std::optional<std::size_t> unit;
    std::uint64_t imageCount = 0u;
};

/// Walks the file's images as far as the request needs.
Result<Selection> selectImage(const openfits::reader& file, const ExecutionContext& execution, std::uint64_t imageIndex,
                              bool enumerateImages)
{
    Selection selection;
    // Only a public probe needs the total count. A decode's facade already performed that probe, so its requested
    // image is located without walking to the end of the file.
    if (enumerateImages && file.hdu_count() > kMaxCodecCards)
        return Status::failure(ErrorCode::LimitExceeded);
    std::optional<std::size_t> next = file.first_image(0u);
    while (next.has_value())
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (selection.imageCount == imageIndex)
            selection.unit = *next;
        ++selection.imageCount;
        if (!enumerateImages && selection.unit.has_value())
            break;
        try
        {
            next = file.first_image(*next + 1u);
        }
        catch (const openfits::cancelled_error&)
        {
            throw;
        }
        catch (const openfits::error&)
        {
            // A unit the library cannot walk past - a header that gives no size for its data - ends the file's
            // images: the ones before it are the file's images, and stay readable.
            next.reset();
        }
    }
    return selection;
}

/// Where the record of a unit's `BLANK` card begins in the file, when that card is what stops the library describing
/// the unit's image: it is there, its value is not the integer the standard asks of an integer image, and the file
/// holds it at the offset the header gives - which the content of a gzip-compressed file does not.
///
/// @param file the open file.
/// @param unit the unit the library could not describe.
/// @param path the file's path, to read the record's own bytes.
/// @return the record's offset, or nothing when `BLANK` is not the problem or the record is not where it should be.
std::optional<std::uint64_t> malformedBlankRecord(const openfits::reader& file, std::size_t unit, std::string_view path)
{
    constexpr std::string_view kKeyword = "BLANK   ";
    try
    {
        const openfits::hdu_info& described = file.hdu(unit);
        const openfits::header& cards = described.header();
        const std::optional<std::size_t> card = cards.index_of("BLANK");
        if (!card.has_value())
            return std::nullopt;
        try
        {
            (void)cards.find<std::int64_t>("BLANK");
            return std::nullopt;
        }
        catch (const openfits::invalid_data_error&)
        {
            // The card states no integer: it is the one to leave out.
        }
        // A header read from a file holds a card for each of its records, in their order.
        const std::uint64_t record = described.header_offset() + 80u * static_cast<std::uint64_t>(*card);
        const openfits::file_source bytes(path);
        std::array<std::byte, 8u> keyword{};
        if (record + keyword.size() > bytes.size())
            return std::nullopt;
        bytes.read(record, keyword);
        if (std::memcmp(keyword.data(), kKeyword.data(), keyword.size()) != 0)
            return std::nullopt;
        return record;
    }
    catch (const openfits::error&)
    {
        return std::nullopt;
    }
}

/// Whether the selected unit's data end inside the file and match the checksums its header states.
///
/// The fill of the data's last block counts as the data's: a unit cut inside it was cut short. A unit whose header
/// states neither `DATASUM` nor `CHECKSUM` asserts no sum and is not read; one that states either is read whole, as
/// verifying it takes, with the operation's stop checked as it is read. A unit the library cannot read is not whole;
/// a stop passes through as the library's cancellation.
bool payloadIsWhole(const openfits::reader& file, std::size_t unit, const ExecutionContext& execution)
{
    try
    {
        const openfits::hdu_info& info = file.hdu(unit);
        const std::optional<std::uint64_t> dataBytes = info.data_size();
        if (!dataBytes.has_value())
            return false;
        const std::uint64_t filled = (*dataBytes + kFitsBlockBytes - 1u) / kFitsBlockBytes * kFitsBlockBytes;
        if (info.data_offset() > file.size() || filled > file.size() - info.data_offset())
            return false;
        if (!info.header().contains("DATASUM") && !info.header().contains("CHECKSUM"))
            return true;
        const openfits::checksum_report sums =
            file.verify(unit, {.progress = [&execution](std::uint64_t, std::uint64_t) {
                            return !execution.checkCancellation().failed();
                        }});
        return sums.datasum != openfits::checksum_state::invalid && sums.checksum != openfits::checksum_state::invalid;
    }
    catch (const openfits::cancelled_error&)
    {
        throw;
    }
    catch (const openfits::error&)
    {
        return false;
    }
}

/// Reads the selected image's header, inside the library's boundary.
///
/// @param malformedBlank set to where the selected unit's `BLANK` record begins when that card is what stopped the
///        library describing the image, so the caller may open the file again without it; left as it is otherwise.
Result<FitsHeader> describe(const openfits::reader& file, const EngineContext& engine,
                            const ExecutionContext& execution, const CodecReadRequest& request,
                            FitsInteger64Policy integer64, bool enumerateImages,
                            std::optional<std::uint64_t>& malformedBlank)
{
    const auto selection = selectImage(file, execution, request.imageIndex, enumerateImages);
    if (!selection.hasValue())
        return selection.status();
    if (!selection->unit.has_value())
        return Status::failure(ErrorCode::OutOfRange);
    const std::size_t selected = *selection->unit;
    // A header the file ends in before its END record describes no image: the library reads what is there as if it
    // were complete, and says so among the unit's diagnostics.
    for (const openfits::diagnostic& problem : file.hdu(selected).diagnostics())
        if (problem.code == openfits::errc::missing_end)
            return Status::failure(ErrorCode::InvalidData,
                                   StatusDetail{.message = "the FITS header has no END record before the file ends",
                                                .operation = "read FITS header",
                                                .domain = ErrorDomain::Io});
    openfits::image_info image;
    try
    {
        image = file.image(selected);
    }
    catch (const openfits::invalid_data_error&)
    {
        malformedBlank = malformedBlankRecord(file, selected, request.path);
        throw;
    }
    const std::vector<std::uint64_t>& axes = image.geometry.axes;
    if (axes.empty() || axes.size() > kMaxFitsAxes || !image.bitpix.has_value())
        return Status::failure(ErrorCode::Unsupported);
    for (std::size_t axis = 3u; axis < axes.size(); ++axis)
        if (axes[axis] != 1u)
            return Status::failure(ErrorCode::Unsupported);
    const std::uint64_t width = axes[0];
    const std::uint64_t height = axes.size() >= 2u ? axes[1] : 1u;
    const std::uint64_t channels = axes.size() >= 3u ? axes[2] : 1u;
    if (width == 0u || height == 0u || channels == 0u || width > engine.limits().maxImageWidth ||
        height > engine.limits().maxImageHeight || channels > engine.limits().maxChannelCount)
        return Status::failure(ErrorCode::LimitExceeded);
    const int bitpix = static_cast<int>(*image.bitpix);
    const double scale = image.scaling.scale;
    const double zero = image.scaling.zero;
    if (!std::isfinite(scale) || !std::isfinite(zero) || scale == 0.0)
        return Status::failure(ErrorCode::InvalidData);
    const auto format = representation(bitpix, scale, zero, integer64);
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
    if (channels == 1u)
    {
        const auto bayer = stringKey(image.header, "BAYERPAT");
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
         .channelSchema = channels == 1u   ? ChannelSchema::Mono
                          : channels == 3u ? ChannelSchema::Rgb
                                           : ChannelSchema::Named,
         .colorSpace = ColorSpace::Unspecified,
         .numericDomain = format.sampleClass == RawSampleClass::FloatingPoint ? NumericDomain::PhysicalScaled
                                                                              : NumericDomain::RawSensorCode,
         .cfa = cfa,
         .coordinateOrigin = stringKey(image.header, "ROWORDER") == "BOTTOM-UP" ? CoordinateOrigin::BottomLeft
                                                                                : CoordinateOrigin::TopLeft,
         .pixelAspectRatio = 1.0});
    if (!descriptor)
        return Status::failure(ErrorCode::InvalidData);
    const RawSampleFormat stored{static_cast<std::uint8_t>(std::abs(bitpix) / 8),
                                 bitpix < 0    ? RawSampleClass::FloatingPoint
                                 : bitpix == 8 ? RawSampleClass::UnsignedInteger
                                               : RawSampleClass::SignedInteger};
    const SourceEncoding encoding{stored, bitpix == 8 ? ByteOrder::Native : ByteOrder::Big, scale, zero};
    ImageMetadata metadata;
    if (request.readMetadata)
    {
        auto cards = cardsOf(image.header, execution);
        if (!cards.hasValue())
            return cards.status();
        if (selected != 0u)
        {
            auto primary = cardsOf(file.hdu(0u).header(), execution);
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
        auto codec = CodecMetadata::create(encoding, std::move(*cards));
        if (!codec.hasValue())
            return codec.status();
        auto projected = fitsMetadata(std::move(*codec), extent, request.path, integer64, bitpix == 64);
        if (!projected.hasValue())
            return projected.status();
        metadata = std::move(*projected);
    }
    else
    {
        auto codec = CodecMetadata::create(encoding, {});
        if (!codec.hasValue())
            return codec.status();
        metadata = ImageMetadata::create({}, {}, {}, {}, std::move(*codec), std::nullopt);
    }
    const bool compressed = file.hdu(selected).kind() == openfits::hdu_kind::compressed_image;
    // A tile is one row of the image unless the unit states a taller one along its second axis or a deeper one along
    // its third.
    bool tilesSpanRows = false;
    if (compressed)
    {
        const openfits::header& unitCards = file.hdu(selected).header();
        for (const char* const keyword : {"ZTILE2", "ZTILE3"})
        {
            try
            {
                tilesSpanRows = tilesSpanRows || unitCards.find<std::int64_t>(keyword).value_or(1) > 1;
            }
            catch (const openfits::invalid_data_error&)
            {
                // A tile size that is no integer is the library's to refuse when the image is read.
            }
        }
    }
    auto disposition = CodecProbeDisposition::DecodeRequired;
    if (request.validatePayload)
    {
        if (!payloadIsWhole(file, selected, execution))
            return codecFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, CodecFormat::Fits,
                                "validate FITS payload", request.path);
        if (!compressed)
            disposition = CodecProbeDisposition::PayloadValidatedWithoutDecode;
    }
    return FitsHeader{.info = {*descriptor, *layout, selection->imageCount, std::move(metadata), disposition},
                      .unit = selected,
                      .axes = static_cast<std::uint32_t>(axes.size()),
                      .bitpix = bitpix,
                      .scale = scale,
                      .zero = zero,
                      .compressed = compressed,
                      .tilesSpanRows = tilesSpanRows};
}

/// The most decoded tiles a handle keeps between the strips of one image, in bytes, under the engine's own bound
/// on an image: enough for the whole-image tile of any frame the engine reads.
constexpr std::uint64_t kFitsTileCacheBytes = std::uint64_t{1u} << 31u;
} // namespace

Result<FitsHeader> readFitsHeader(FitsHandle& file, const EngineContext& engine, const ExecutionContext& execution,
                                  const CodecReadRequest& request, FitsInteger64Policy integer64, bool enumerateImages)
{
    std::optional<std::uint64_t> malformedBlank;
    const auto describeFile = [&]() {
        return evaluateAcrossBoundary(kFitsBoundary, [&]() -> Result<FitsHeader> {
            try
            {
                return describe(*file.reader, engine, execution, request, integer64, enumerateImages, malformedBlank);
            }
            catch (const openfits::cancelled_error&)
            {
                auto stopped = execution.checkCancellation();
                return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
            }
            catch (const openfits::error& error)
            {
                return fitsLibraryFailure(error, FitsStage::Open, "read FITS header", request.path);
            }
        });
    };
    auto described = describeFile();
    if (described.hasValue() && !described->hasValue() && malformedBlank.has_value())
    {
        // The image's BLANK card states no integer. The codec reads no sample as undefined, so the card is not
        // needed: the file is opened again with that record read as blanks, and described from there.
        auto reopened = openFitsWithoutRecord(request.path, engine.limits(), *malformedBlank);
        if (reopened.hasValue())
        {
            file = std::move(*reopened);
            malformedBlank.reset();
            described = describeFile();
        }
    }
    if (!described.hasValue())
        return codecFailure(described.status().code(), CodecFailure::MetadataUnreadable, CodecFormat::Fits,
                            "read FITS header", request.path, described.status());
    if (!described->hasValue())
        return described->status();
    if ((*described)->tilesSpanRows && file.tiles == nullptr)
    {
        // What the library decodes stays outside the engine's budget and under its bound on one image, as the open
        // set it: the tiles kept are no more than that.
        const Status kept = callAcrossBoundary(kFitsBoundary, [&] {
            file.tiles = std::make_unique<openfits::tile_cache>(
                (std::min)(kFitsTileCacheBytes, static_cast<std::uint64_t>(engine.limits().maxImageBytes)));
        });
        // A cache that could not be had costs a decode its saving, and nothing else.
        if (kept.failed())
            file.tiles.reset();
    }
    return std::move(**described);
}
} // namespace an
