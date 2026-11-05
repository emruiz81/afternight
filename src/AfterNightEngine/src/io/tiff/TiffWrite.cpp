// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - encoding a TIFF container into a pending path.
//
// Every refusal is decided before the pending file is opened. The image is written in the
// arrangement of the frame it is handed - separate planes for a planar frame, contiguous samples for
// an interleaved one - in the intent's byte order or else the frame's, with an orientation that
// keeps a source's mirror or transposition when this write moved no pixel. The payload is written
// uncompressed, with LZW, with deflate or with the lossy JPEG transform, in strips of about a quarter
// of a megabyte, and in the form that addresses more than four gibibytes once the samples pass
// 3,500 MiB. Every row goes through one row of counted workspace, because the library may swap and
// predict a row in place. The Exif is embedded afterwards, by reading the finished file back into
// counted memory and letting the metadata adapter rewrite it - a pass taken only when a card states
// an Exif field other than the software stamp the imaging library has already written.

#include "io/registry/PayloadForm.h"
#include "io/tiff/TiffFile.h"

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "interop/exiv2/ExifCards.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace an
{
namespace
{

/// The keyword the orientation a source file stated is read back from.
constexpr std::string_view kOrientationKeyword = "Exif.Image.Orientation";

/// The keyword the engine that wrote the file states itself under.
constexpr std::string_view kSoftwareKeyword = "Exif.Image.Software";

/// The JPEG quality a write states when the caller states none.
constexpr int kDefaultJpegQuality = 95;

/// The deflate level a write states when the caller states none.
///
/// Negative is this library's own spelling of "the compressor's default".
constexpr int kDefaultZipQuality = -1;

/// The payload above which a container is written in the form that addresses more than four
/// gibibytes.
///
/// Below the format's own four-gibibyte ceiling by enough that the directories, the strip tables
/// and the metadata cannot carry it over.
constexpr std::uint64_t kBigTiffThresholdBytes = 3500ull * 1024ull * 1024ull;

/// The filter colours a filter-array tag states, in the order that tag numbers them.
constexpr std::uint8_t kCfaRed = 0u;
constexpr std::uint8_t kCfaGreen = 1u;
constexpr std::uint8_t kCfaBlue = 2u;

/// The path a literal UTF-8 string names.
///
/// @param path the boundary path.
/// @return the platform path, decoded through `char8_t` rather than the process code page.
[[nodiscard]] std::filesystem::path filePath(std::string_view path)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
}

/// A block of counted engine memory, released however the write ends.
class CountedBuffer
{
public:
    /// Takes nothing.
    CountedBuffer() = default;

    /// Releases the block.
    ~CountedBuffer()
    {
        if (m_bytes != nullptr && m_resource != nullptr)
        {
            m_resource->deallocate(m_bytes, m_size, kAlignment);
        }
    }

    CountedBuffer(const CountedBuffer&) = delete;
    CountedBuffer& operator=(const CountedBuffer&) = delete;
    CountedBuffer(CountedBuffer&&) = delete;
    CountedBuffer& operator=(CountedBuffer&&) = delete;

    /// Takes a block from the engine context's budget.
    ///
    /// @param engine the context whose budget pays for it.
    /// @param size how many bytes are wanted.
    /// @return success, or the budget's refusal.
    [[nodiscard]] Status acquire(const EngineContext& engine, std::uint64_t size)
    {
        MemoryResource* const resource = engine.memoryResource();
        if (resource == nullptr)
        {
            return Status::failure(ErrorCode::NotInitialized);
        }
        auto memory = resource->allocate(size, kAlignment);
        if (!memory.hasValue())
        {
            return memory.status();
        }
        m_resource = resource;
        m_bytes = *memory;
        m_size = size;
        return {};
    }

    /// The block.
    ///
    /// @return its first byte, or null when nothing was acquired.
    [[nodiscard]] std::byte* bytes() const noexcept
    {
        return static_cast<std::byte*>(m_bytes);
    }

    /// How long it is.
    ///
    /// @return the size in bytes.
    [[nodiscard]] std::uint64_t size() const noexcept
    {
        return m_size;
    }

private:
    /// What a sample of any width may be read from.
    static constexpr std::uint64_t kAlignment = 64u;

    /// Who to give the memory back to.
    MemoryResource* m_resource = nullptr;

    /// The block.
    void* m_bytes = nullptr;

    /// How long it is.
    std::uint64_t m_size = 0u;
};

/// What this container calls a representation, or nothing for one it does not spell.
///
/// @param format the frame's representation.
/// @return the sample-format code, or nothing.
[[nodiscard]] std::optional<std::uint16_t> tiffSampleFormatCode(const RawSampleFormat& format) noexcept
{
    if (format.sizeBytes > 8u || !isRawSampleSize(format.sizeBytes))
    {
        return std::nullopt;
    }
    switch (format.sampleClass)
    {
    case RawSampleClass::UnsignedInteger:
        return static_cast<std::uint16_t>(SAMPLEFORMAT_UINT);
    case RawSampleClass::SignedInteger:
        return static_cast<std::uint16_t>(SAMPLEFORMAT_INT);
    case RawSampleClass::FloatingPoint:
        return format.sizeBytes == 1u ? std::nullopt
                                      : std::optional<std::uint16_t>{static_cast<std::uint16_t>(SAMPLEFORMAT_IEEEFP)};
    case RawSampleClass::Opaque:
        break;
    }
    return std::nullopt;
}

/// The complete text of the first field with this keyword, continuations joined.
///
/// @param cards the cards to read.
/// @param keyword the field to find.
/// @return the text, or an empty optional when no card states it.
[[nodiscard]] std::optional<std::string> joinField(std::span<const CodecCard> cards, std::string_view keyword)
{
    std::optional<std::string> text;
    for (const CodecCard& card : cards)
    {
        if (card.keyword() != keyword)
        {
            continue;
        }
        if (!text.has_value())
        {
            text = card.value();
            continue;
        }
        if (card.comment() == kExifContinuedComment)
        {
            *text += card.value();
        }
    }
    return text;
}

/// Which orientation a written image states.
///
/// The row direction is the descriptor's, because that is what the samples actually are. A source
/// that also stated a mirror or a transposition keeps it, since this write did not move a pixel:
/// the tag then says the same thing about the same samples that the source file said.
///
/// @param origin where the frame's first row is.
/// @param stated the orientation a source file stated, or nothing.
/// @return the orientation code to write.
[[nodiscard]] std::uint16_t outputOrientation(CoordinateOrigin origin, const std::optional<std::string>& stated)
{
    const std::uint16_t fallback = origin == CoordinateOrigin::BottomLeft
                                       ? static_cast<std::uint16_t>(ORIENTATION_BOTLEFT)
                                       : static_cast<std::uint16_t>(ORIENTATION_TOPLEFT);
    if (!stated.has_value())
    {
        return fallback;
    }
    int value = 0;
    const char* const first = stated->data();
    const char* const last = first + stated->size();
    if (std::from_chars(first, last, value).ec != std::errc{} || value < 1 || value > 8)
    {
        return fallback;
    }
    const bool topDown = value == ORIENTATION_TOPLEFT || value == ORIENTATION_TOPRIGHT ||
                         value == ORIENTATION_LEFTTOP || value == ORIENTATION_RIGHTTOP ||
                         value == ORIENTATION_RIGHTBOT || value == ORIENTATION_LEFTBOT;
    const bool agrees = origin == CoordinateOrigin::TopLeft ? topDown : !topDown;
    return agrees ? static_cast<std::uint16_t>(value) : fallback;
}

/// The cards the encoder writes, in the order it writes them.
///
/// The acquisition facts this engine holds first, then the source's own writable cards when the
/// intent preserves them, then the intent's, with the software stamp last, so a stated card replaces
/// a preserved one of the same name. A raw pass-through states nothing: it has no intent to state it
/// in.
///
/// @param request the pending target and explicit intent.
/// @param metadata the immutable source facts.
/// @return the cards, or a typed failure naming a card this container cannot carry.
[[nodiscard]] Result<std::vector<CodecCard>> outputCards(const CodecEncodeRequest& request,
                                                         const ImageMetadata& metadata)
{
    std::vector<CodecCard> cards;
    const auto append = [&cards](const CodecCard& card) {
        if (card.comment() != kExifContinuedComment)
        {
            std::erase_if(cards, [&card](const CodecCard& existing) { return existing.keyword() == card.keyword(); });
        }
        cards.push_back(card);
    };
    if (request.intent == nullptr)
    {
        return cards;
    }
    auto projected = acquisitionCards({.acquisition = metadata.acquisition().toRequest()}, CodecNamespace::Tiff);
    if (!projected.hasValue())
    {
        return projected.status();
    }
    cards = std::move(*projected);
    if (request.intent->preserveSourceCards())
    {
        for (const CodecCard& card : metadata.codec().cards())
        {
            if (cardDisposition(CodecFormat::Tiff, card) == CardDisposition::Written)
            {
                append(card);
            }
        }
    }
    for (const CodecCard& card : request.intent->cards())
    {
        if (isArtifactIdentityKeyword(card.keyword()))
        {
            return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Tiff,
                                "encode TIFF cards", request.pendingPath,
                                Status::failure(ErrorCode::Unsupported,
                                                {.message = "a TIFF container has no keyword-addressed text a stamp "
                                                            "could be read back from, so an artifact identity is "
                                                            "refused rather than written where nothing would find it",
                                                 .operation = "encode TIFF cards",
                                                 .domain = ErrorDomain::Io}));
        }
        if (!isExifKeyword(card.keyword()))
        {
            return codecFailure(
                ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Tiff, "encode TIFF cards",
                request.pendingPath,
                Status::failure(ErrorCode::Unsupported,
                                {.message = "this container carries its metadata as tags; a card of "
                                            "another vocabulary has nowhere to go in it",
                                 .operation = "encode TIFF cards",
                                 .domain = ErrorDomain::Io,
                                 .context = {StatusContextField::ofText(ContextKey::Parameter, card.keyword())}}));
        }
        append(card);
    }
    auto software = CodecCard::create(CodecNamespace::Tiff, std::string(kSoftwareKeyword),
                                      std::string(kEngineVersionString), "Ascii");
    if (!software.hasValue())
    {
        return software.status();
    }
    append(*software);
    return cards;
}

/// States the tags a written image carries, other than its metadata.
///
/// @param file the open container.
/// @param descriptor what the frame is.
/// @param layout where its samples are.
/// @param compression the compression the intent states.
/// @param level the compression's level, or nothing for the codec's own default.
/// @param orientation the orientation to state.
/// @return success: nothing here refuses. A tag the library rejects is recorded by the container's
///         error handler, and that message is what a later failed write reports.
[[nodiscard]] Status stateTiffTags(TiffFile& file, const RawFrameDescriptor& descriptor, const ImageLayout& layout,
                                   OutputCompression compression, std::optional<std::uint32_t> level,
                                   std::uint16_t orientation)
{
    const auto& extent = descriptor.extent();
    const auto channels = extent.channelCount();
    const auto format = descriptor.sampleFormat();
    const auto sampleFormat = *tiffSampleFormatCode(format);
    const bool separate = layout.planeCount() > 1u;
    const auto schema = descriptor.channelSchema();
    const bool colour = channels >= 3u;
    const std::uint16_t photometric =
        colour ? static_cast<std::uint16_t>(PHOTOMETRIC_RGB) : static_cast<std::uint16_t>(PHOTOMETRIC_MINISBLACK);
    TIFFSetField(file.tiff, TIFFTAG_IMAGEWIDTH, extent.width());
    TIFFSetField(file.tiff, TIFFTAG_IMAGELENGTH, extent.height());
    TIFFSetField(file.tiff, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(format.sizeBytes * 8u));
    TIFFSetField(file.tiff, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(channels));
    TIFFSetField(file.tiff, TIFFTAG_SAMPLEFORMAT, sampleFormat);
    TIFFSetField(file.tiff, TIFFTAG_PHOTOMETRIC, photometric);
    TIFFSetField(file.tiff, TIFFTAG_PLANARCONFIG,
                 static_cast<std::uint16_t>(separate ? PLANARCONFIG_SEPARATE : PLANARCONFIG_CONTIG));
    TIFFSetField(file.tiff, TIFFTAG_ORIENTATION, orientation);
    TIFFSetField(file.tiff, TIFFTAG_SOFTWARE, kEngineVersionString);
    const std::uint32_t photometricChannels = colour ? 3u : 1u;
    if (channels > photometricChannels)
    {
        std::vector<std::uint16_t> extras(channels - photometricChannels,
                                          static_cast<std::uint16_t>(EXTRASAMPLE_UNSPECIFIED));
        if (schema == ChannelSchema::Rgba)
        {
            extras.front() = static_cast<std::uint16_t>(EXTRASAMPLE_UNASSALPHA);
        }
        TIFFSetField(file.tiff, TIFFTAG_EXTRASAMPLES, static_cast<std::uint16_t>(extras.size()), extras.data());
    }
    if (descriptor.cfa().isMosaiced())
    {
        const auto side = cfaPatternBlockSize(descriptor.cfa().pattern());
        std::vector<std::uint8_t> pattern(static_cast<std::size_t>(side) * side, kCfaGreen);
        for (std::uint32_t y = 0u; y < side; ++y)
        {
            for (std::uint32_t x = 0u; x < side; ++x)
            {
                const auto color = descriptor.cfa().colorAt(x, y);
                pattern[static_cast<std::size_t>(y) * side + x] = color == CfaColor::Red    ? kCfaRed
                                                                  : color == CfaColor::Blue ? kCfaBlue
                                                                                            : kCfaGreen;
            }
        }
        const std::uint16_t block[2] = {static_cast<std::uint16_t>(side), static_cast<std::uint16_t>(side)};
        TIFFSetField(file.tiff, TIFFTAG_CFAREPEATPATTERNDIM, block);
        TIFFSetField(file.tiff, TIFFTAG_CFAPATTERN, static_cast<std::uint16_t>(pattern.size()), pattern.data());
    }
    const std::uint16_t compressionCode =
        compression == OutputCompression::Lzw       ? static_cast<std::uint16_t>(COMPRESSION_LZW)
        : compression == OutputCompression::Deflate ? static_cast<std::uint16_t>(COMPRESSION_ADOBE_DEFLATE)
        : compression == OutputCompression::Jpeg    ? static_cast<std::uint16_t>(COMPRESSION_JPEG)
                                                    : static_cast<std::uint16_t>(COMPRESSION_NONE);
    TIFFSetField(file.tiff, TIFFTAG_COMPRESSION, compressionCode);
    if (compression == OutputCompression::Lzw || compression == OutputCompression::Deflate)
    {
        // The horizontal predictor is stated for eight- and sixteen-bit integer samples, because it
        // is what makes a continuous-tone image compress at all.
        if (format.sampleClass != RawSampleClass::FloatingPoint && (format.sizeBytes == 1u || format.sizeBytes == 2u))
        {
            TIFFSetField(file.tiff, TIFFTAG_PREDICTOR, static_cast<std::uint16_t>(PREDICTOR_HORIZONTAL));
        }
        if (compression == OutputCompression::Deflate)
        {
            TIFFSetField(file.tiff, TIFFTAG_ZIPQUALITY,
                         level.has_value() ? static_cast<int>(*level) : kDefaultZipQuality);
        }
    }
    if (compression == OutputCompression::Jpeg)
    {
        TIFFSetField(file.tiff, TIFFTAG_JPEGQUALITY,
                     level.has_value() ? static_cast<int>(*level) : kDefaultJpegQuality);
    }
    // The strip height is stated rather than left to the imaging library's own default, which aims at
    // eight kilobytes - a figure from an era of smaller memories - and which floors at a single row
    // for any image whose row is wider than that, so a container written here would carry one strip
    // per row and be read back a row at a time. A strip is the unit a partial read has to decode, so
    // it is kept small enough that reading part of an image does not decode all of it, and large
    // enough that reading all of it is not several hundred seeks.
    constexpr std::uint64_t kTargetStripBytes = 256u * 1024u;
    constexpr std::uint64_t kJpegBlockRows = 16u;
    const auto scanlineBytes = static_cast<std::uint64_t>(TIFFScanlineSize64(file.tiff));
    std::uint64_t rowsPerStrip = scanlineBytes == 0u ? extent.height() : kTargetStripBytes / scanlineBytes;
    if (rowsPerStrip == 0u)
    {
        rowsPerStrip = 1u;
    }
    if (compression == OutputCompression::Jpeg)
    {
        // The transform this container defines works in blocks of rows, and the library refuses a
        // strip that is not a whole number of them unless it is the whole image.
        rowsPerStrip =
            rowsPerStrip < kJpegBlockRows ? extent.height() : (rowsPerStrip / kJpegBlockRows) * kJpegBlockRows;
    }
    if (rowsPerStrip > extent.height())
    {
        rowsPerStrip = extent.height();
    }
    TIFFSetField(file.tiff, TIFFTAG_ROWSPERSTRIP, static_cast<std::uint32_t>(rowsPerStrip));
    return {};
}

} // namespace

Result<TiffHandle> openTiffWrite(const EngineContext& engine, const ExecutionContext& execution, std::string_view path,
                                 bool bigTiff, char byteOrder)
{
    auto opened = evaluateAcrossBoundary(kTiffBoundary, [&]() -> TiffHandle {
        TiffHandle file{new TiffFile()};
        file->execution = &execution;
        return openTiffFile(*file, path, true) ? std::move(file) : TiffHandle{};
    });
    if (!opened.hasValue())
    {
        return codecFailure(opened.status().code(), CodecFailure::DestinationUnavailable, CodecFormat::Tiff,
                            "create TIFF pending output", path, opened.status());
    }
    TiffHandle file = std::move(*opened);
    if (file == nullptr)
    {
        return tiffFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable,
                           "create TIFF pending output", path,
                           "the pending output path could not be opened for writing");
    }
    std::string mode = "w";
    if (bigTiff)
    {
        mode += '8';
    }
    if (byteOrder != '\0')
    {
        mode += byteOrder;
    }
    TIFFOpenOptions* const options = TIFFOpenOptionsAlloc();
    if (options == nullptr)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    TIFFOpenOptionsSetErrorHandlerExtR(options, &tiffErrorHandler, file.get());
    TIFFOpenOptionsSetWarningHandlerExtR(options, &tiffWarningHandler, file.get());
    TIFFOpenOptionsSetMaxSingleMemAlloc(options, static_cast<tmsize_t>(engine.limits().maxImageBytes));
    file->tiff = TIFFClientOpenExt("image", mode.c_str(), file.get(), &tiffReadProc, &tiffWriteProc, &tiffSeekProc,
                                   &tiffCloseProc, &tiffSizeProc, &tiffMapProc, &tiffUnmapProc, options);
    TIFFOpenOptionsFree(options);
    if (file->tiff == nullptr)
    {
        return tiffFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable, "create TIFF output", path,
                           file->message[0] != '\0' ? file->message : "the container could not be created");
    }
    return file;
}

Result<CodecEncodeReport> writeTiff(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata)
{
    const auto descriptor = *frame.descriptor();
    const auto layout = *frame.layout();
    const auto& extent = descriptor.extent();
    const auto channels = extent.channelCount();
    const auto format = descriptor.sampleFormat();
    const auto schema = descriptor.channelSchema();
    const auto sampleFormat = tiffSampleFormatCode(format);
    const bool describable = schema == ChannelSchema::Mono || schema == ChannelSchema::Rgb ||
                             schema == ChannelSchema::Rgba || schema == ChannelSchema::Named;
    const auto compression = request.intent != nullptr ? request.intent->compression() : OutputCompression::None;
    const bool compressible = compression == OutputCompression::None || compression == OutputCompression::Lzw ||
                              compression == OutputCompression::Deflate ||
                              (compression == OutputCompression::Jpeg && format.sizeBytes == 1u &&
                               format.sampleClass == RawSampleClass::UnsignedInteger);
    // Every refusal is decided before the pending file is opened. This container states no scale of
    // its own, its lossy transform is defined for eight-bit samples alone, and a schema whose
    // channels are not grey, colour or named has no photometric interpretation here.
    if (!sampleFormat || !describable || !compressible || statesPayloadForm(request.intent) ||
        (descriptor.cfa().isMosaiced() && channels != 1u) ||
        (request.intent != nullptr && request.intent->scaling() != OutputScaling::None))
    {
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Tiff,
                            "encode TIFF options", request.pendingPath);
    }
    auto cards = outputCards(request, metadata);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    const auto orientation = outputOrientation(descriptor.coordinateOrigin(), joinField(*cards, kOrientationKeyword));
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    const auto resolved = descriptor.byteOrder() == ByteOrder::Native ? nativeByteOrder() : descriptor.byteOrder();
    const auto intentOrder = request.intent != nullptr && request.intent->byteOrder() != ByteOrder::Native
                                 ? request.intent->byteOrder()
                                 : resolved;
    const char orderFlag = format.sizeBytes == 1u ? '\0' : (intentOrder == ByteOrder::Big ? 'b' : 'l');
    const bool swap = format.sizeBytes > 1u && resolved != nativeByteOrder();
    const auto rowBytes = static_cast<std::uint64_t>(extent.width()) * layout.samplesPerPixel() * format.sizeBytes;
    CountedBuffer row;
    if (const auto acquired = row.acquire(engine, rowBytes); acquired.failed())
    {
        return acquired;
    }
    auto opened =
        openTiffWrite(engine, execution, request.pendingPath, layout.totalBytes() > kBigTiffThresholdBytes, orderFlag);
    if (!opened.hasValue())
    {
        return opened.status();
    }
    TiffHandle file = std::move(*opened);
    // The body returns a status, which a value-carrying boundary cannot hold, so the boundary wraps
    // a call that returns nothing and the body's own answer comes back beside it.
    const auto body = [&]() -> Status {
        if (const auto stated = stateTiffTags(
                *file, descriptor, layout, compression,
                request.intent != nullptr ? request.intent->compressionLevel() : std::nullopt, orientation);
            stated.failed())
        {
            return stated;
        }
        const auto planes = layout.planeCount();
        const std::uint64_t total = static_cast<std::uint64_t>(extent.height()) * planes;
        std::uint64_t completed = 0u;
        for (std::uint32_t plane = 0u; plane < planes; ++plane)
        {
            for (std::uint32_t y = 0u; y < extent.height(); ++y)
            {
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                {
                    return stopped;
                }
                const std::byte* source =
                    frame.bytes().data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes();
                // LibTIFF is permitted to byte-swap and predictor-transform the scanline it is
                // handed. Both operations happen in place, so even a host-order row needs a working
                // copy: the encoder's source is immutable and may still be live after this call.
                std::memcpy(row.bytes(), source, static_cast<std::size_t>(rowBytes));
                if (swap)
                {
                    // The library writes what the host holds and swaps it if the file's order
                    // differs, so a frame that arrived in the other order is brought to the host's
                    // first. Nothing else about the samples changes.
                    for (std::uint64_t offset = 0u; offset < rowBytes; offset += format.sizeBytes)
                    {
                        std::reverse(row.bytes() + offset, row.bytes() + offset + format.sizeBytes);
                    }
                }
                if (TIFFWriteScanline(file->tiff, row.bytes(), y, static_cast<std::uint16_t>(plane)) != 1)
                {
                    return tiffFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "write TIFF output",
                                       request.pendingPath,
                                       file->message[0] != '\0' ? file->message : "a row could not be written");
                }
                ++completed;
                execution.reportProgress(completed, total, "encode TIFF");
            }
        }
        // The last compressed strip and the image directory are deferred until finalization. Do it
        // while the handle, its error text and the file's failure flag are still observable; closing
        // a handle is too late to turn either failure into the transaction's whole-file refusal.
        if (TIFFWriteDirectory(file->tiff) != 1 || TIFFFlush(file->tiff) != 1 || file->failed)
        {
            return tiffFailure(
                ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "finish TIFF output", request.pendingPath,
                file->message[0] != '\0' ? file->message : "the final strip or directory could not be written");
        }
        if (!flushTiffFile(*file))
        {
            return tiffFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "finish TIFF output",
                               request.pendingPath, "the completed container could not be flushed");
        }
        return {};
    };
    Status outcome;
    if (const auto thrown = callAcrossBoundary(kTiffBoundary, [&] { outcome = body(); }); thrown.failed())
    {
        return thrown;
    }
    if (outcome.failed())
    {
        return outcome;
    }
    // Finalization above leaves closing with no buffered strip or directory still to write.
    file.reset();
    // The metadata pass reads the finished container back and lets the metadata library rewrite it,
    // which costs the file twice over. It is worth that only for a field this adapter cannot state
    // itself: the engine's own name is a tag the imaging library has already written, so a write
    // that states nothing else does not pay for it.
    bool exif = false;
    for (const CodecCard& card : *cards)
    {
        exif = exif || (isExifKeyword(card.keyword()) && card.keyword() != kSoftwareKeyword);
    }
    if (exif)
    {
        std::error_code sizeError;
        const auto containerBytes = std::filesystem::file_size(filePath(request.pendingPath), sizeError);
        if (sizeError)
        {
            return Status::failure(ErrorCode::ExternalFailure);
        }
        if (containerBytes > engine.limits().maxImageBytes)
        {
            return Status::failure(ErrorCode::LimitExceeded);
        }
        CountedBuffer container;
        if (const auto acquired = container.acquire(engine, containerBytes); acquired.failed())
        {
            return acquired;
        }
        if (!readTiffContainer(request.pendingPath, container.bytes(), containerBytes))
        {
            return Status::failure(ErrorCode::ExternalFailure);
        }
        auto rewrite = embedTiffExifCards(
            std::span<std::byte>{container.bytes(), static_cast<std::size_t>(containerBytes)}, *cards);
        if (!rewrite.hasValue())
        {
            return rewrite.status();
        }
        const auto payload = rewrite->rewritten ? std::span<const std::byte>{rewrite->bytes}
                                                : std::span<const std::byte>{container.bytes(),
                                                                             static_cast<std::size_t>(containerBytes)};
        std::ofstream output(filePath(request.pendingPath), std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            return tiffFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable, "write TIFF metadata",
                               request.pendingPath, "the pending output could not be reopened for its metadata");
        }
        output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        output.flush();
        const bool complete = output.good();
        output.close();
        if (!complete || output.fail())
        {
            return tiffFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "write TIFF metadata",
                               request.pendingPath, "the pending TIFF payload could not be written in full");
        }
    }
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(filePath(request.pendingPath), sizeError);
    if (sizeError)
    {
        return Status::failure(ErrorCode::ExternalFailure);
    }
    return CodecEncodeReport{static_cast<std::uint64_t>(size)};
}

} // namespace an
