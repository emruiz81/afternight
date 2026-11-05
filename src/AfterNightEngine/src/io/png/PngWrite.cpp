// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - encoding a PNG container into a pending path.
//
// Every refusal is decided before the pending file is opened. The cards are the acquisition facts,
// then the source's writable cards when the intent preserves them, then the intent's, with the
// engine's `Software` entry last - the only card a write with no intent states. The Exif fields among
// them become one eXIf chunk and every other card a text entry: uncompressed Latin text when its
// value is ASCII, international text otherwise. The colour declaration follows the metadata: the
// standard colour space for an sRGB transfer, a gamma of one for a linear frame, or the file gamma of
// a pure power law. The payload is deflated at the intent's level, 6 when it states none, or stored
// when the intent asks for no compression.
// Sixteen-bit samples are swapped to the file's big-endian order as they are written, and a planar
// frame is interleaved one row at a time through a row of counted workspace.

#include "io/png/PngFile.h"
#include "io/registry/PayloadForm.h"

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "interop/exiv2/ExifCards.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>

namespace an
{
namespace
{

/// The keyword the engine that wrote the file states itself under.
constexpr std::string_view kSoftwareKeyword = "Software";

/// The longest keyword this container's text entries hold.
constexpr std::size_t kMaxTextKeywordBytes = 79u;

/// The deflate level a write states when the caller states none.
constexpr int kDefaultCompressionLevel = 6;

/// Keeps what the library said about a refusal, then leaves the frame it was called from.
///
/// @param png the library's writer.
/// @param text what it said.
void pngWriteError(png_structp png, png_const_charp text)
{
    auto* const session = static_cast<PngWriteSession*>(png_get_error_ptr(png));
    if (session != nullptr && text != nullptr)
    {
        const auto length = (std::min)(std::strlen(text), kPngMessageBytes - 1u);
        std::memcpy(session->message, text, length);
        session->message[length] = '\0';
    }
    png_longjmp(png, 1);
}

/// Swallows what the library would otherwise write to the process's error stream.
void pngWriteWarning(png_structp, png_const_charp) {}

/// Pushes the next bytes of the container into the stream.
///
/// A failed write is recorded rather than thrown: the library finishes, the bytes it produces after
/// the failure go nowhere, and the caller reports a short write once the encode has unwound.
///
/// @param png the library's writer.
/// @param data the bytes.
/// @param size how many there are.
void pngWriteBytes(png_structp png, png_bytep data, png_size_t size)
{
    auto* const session = static_cast<PngWriteSession*>(png_get_io_ptr(png));
    session->stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!session->stream)
    {
        session->failed = true;
    }
}

/// Flushes whatever the stream still holds.
///
/// @param png the library's writer.
void pngFlushBytes(png_structp png)
{
    auto* const session = static_cast<PngWriteSession*>(png_get_io_ptr(png));
    session->stream.flush();
}

/// One row of engine memory, released however the encode ends.
class RowWorkspace
{
public:
    /// Takes nothing.
    RowWorkspace() = default;

    /// Releases the row.
    ~RowWorkspace()
    {
        if (m_bytes != nullptr && m_resource != nullptr)
        {
            m_resource->deallocate(m_bytes, m_size, kAlignment);
        }
    }

    RowWorkspace(const RowWorkspace&) = delete;
    RowWorkspace& operator=(const RowWorkspace&) = delete;
    RowWorkspace(RowWorkspace&&) = delete;
    RowWorkspace& operator=(RowWorkspace&&) = delete;

    /// Takes one row's worth of counted memory from the engine context.
    ///
    /// @param engine the context whose budget pays for it.
    /// @param size how many bytes one row needs.
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

    /// The row.
    ///
    /// @return the first byte, or null when nothing was acquired.
    [[nodiscard]] std::byte* bytes() const noexcept
    {
        return static_cast<std::byte*>(m_bytes);
    }

private:
    /// What a sample of any width may be read from.
    static constexpr std::uint64_t kAlignment = 64u;

    /// Who to give the memory back to.
    MemoryResource* m_resource = nullptr;

    /// The row.
    void* m_bytes = nullptr;

    /// How long it is.
    std::uint64_t m_size = 0u;
};

/// Where one row of samples comes from, and what runs between rows.
struct RowSource
{
    const ExecutionContext* execution = nullptr; ///< Whose cancellation is checked.
    const std::byte* bytes = nullptr; ///< The frame's first byte.
    std::byte* workspace = nullptr; ///< Where a planar row is interleaved, or null.
    std::uint64_t rowStrideBytes = 0u; ///< The distance between two rows of one plane.
    std::uint64_t planeStrideBytes = 0u; ///< The distance between two planes.
    std::uint32_t width = 0u; ///< Columns.
    std::uint32_t height = 0u; ///< Rows.
    std::uint32_t channels = 0u; ///< How many samples one pixel has.
    std::uint32_t elementSizeBytes = 1u; ///< How wide one sample is.
    bool planar = false; ///< Whether the channels sit in separate planes.
};

/// Supplies one row of interleaved samples to the library.
///
/// @param user the row source.
/// @param index which row.
/// @return the row, or null when the caller has asked the operation to stop.
png_bytep pngRowOf(void* user, std::uint32_t index)
{
    auto* const source = static_cast<RowSource*>(user);
    if (source->execution->checkCancellation().failed())
    {
        return nullptr;
    }
    const std::byte* row = source->bytes + index * source->rowStrideBytes;
    if (source->planar)
    {
        for (std::uint32_t channel = 0u; channel < source->channels; ++channel)
        {
            const std::byte* const plane = row + channel * source->planeStrideBytes;
            for (std::uint32_t x = 0u; x < source->width; ++x)
            {
                std::memcpy(source->workspace +
                                (static_cast<std::size_t>(x) * source->channels + channel) * source->elementSizeBytes,
                            plane + static_cast<std::size_t>(x) * source->elementSizeBytes, source->elementSizeBytes);
            }
        }
        row = source->workspace;
    }
    source->execution->reportProgress(index + 1u, source->height, "encode PNG");
    return reinterpret_cast<png_bytep>(const_cast<std::byte*>(row));
}

/// Whether a keyword is one this container's text entries can hold.
///
/// The format states what a keyword may be - one to seventy-nine printable characters of the
/// Latin-1 repertoire, with no leading, trailing or repeated space - and a card that breaks the
/// rule is refused rather than written into a chunk no reader would accept.
///
/// @param keyword the card's keyword.
/// @return true when the container can hold it.
[[nodiscard]] bool isTextKeyword(std::string_view keyword) noexcept
{
    if (keyword.empty() || keyword.size() > kMaxTextKeywordBytes || keyword.front() == ' ' || keyword.back() == ' ')
    {
        return false;
    }
    bool previousSpace = false;
    for (const char character : keyword)
    {
        const auto value = static_cast<unsigned char>(character);
        const bool printable = (value >= 32u && value <= 126u) || (value >= 161u && value <= 255u);
        if (!printable || (value == 32u && previousSpace))
        {
            return false;
        }
        previousSpace = value == 32u;
    }
    return true;
}

/// The cards the encoder writes, in the order it writes them.
///
/// The acquisition facts this engine holds first, then the source's own writable cards when the
/// intent preserves them, then the intent's. A raw pass-through states only the engine that wrote
/// the file, which is the one fact about the write rather than about the image.
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
    if (request.intent != nullptr)
    {
        auto projected = acquisitionCards({.acquisition = metadata.acquisition().toRequest()}, CodecNamespace::Png);
        if (!projected.hasValue())
        {
            return projected.status();
        }
        cards = std::move(*projected);
        if (request.intent->preserveSourceCards())
        {
            for (const CodecCard& card : metadata.codec().cards())
            {
                if (cardDisposition(CodecFormat::Png, card) == CardDisposition::Written)
                {
                    append(card);
                }
            }
        }
        for (const CodecCard& card : request.intent->cards())
        {
            if (!isExifKeyword(card.keyword()) && !isTextKeyword(card.keyword()))
            {
                return codecFailure(
                    ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Png,
                    "encode PNG cards", request.pendingPath,
                    Status::failure(ErrorCode::Unsupported,
                                    {.message = "this container's text entries state what a keyword may be, and "
                                                "this one is not one of them",
                                     .operation = "encode PNG cards",
                                     .domain = ErrorDomain::Io,
                                     .context = {StatusContextField::ofText(ContextKey::Parameter, card.keyword())}}));
            }
            append(card);
        }
    }
    auto software = CodecCard::create(CodecNamespace::Png, std::string(kSoftwareKeyword),
                                      std::string(kEngineVersionString), "tEXt");
    if (!software.hasValue())
    {
        return software.status();
    }
    append(*software);
    return cards;
}

} // namespace

void PngWriteSessionDeleter::operator()(PngWriteSession* session) const noexcept
{
    if (session == nullptr)
    {
        return;
    }
    if (session->png != nullptr)
    {
        png_destroy_write_struct(&session->png, session->info != nullptr ? &session->info : nullptr);
    }
    delete session;
}

bool pngWriteImage(PngWriteSession* session, const PngEncodeSource* source) noexcept
{
    if (setjmp(png_jmpbuf(session->png)) != 0)
    {
        return false;
    }
    png_set_write_fn(session->png, session, &pngWriteBytes, &pngFlushBytes);
    png_set_compression_level(session->png, source->compressionLevel);
    png_set_IHDR(session->png, session->info, source->width, source->height, source->bitDepth, source->colorType,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (source->standardColorSpace)
    {
        png_set_sRGB_gAMA_and_cHRM(session->png, session->info, PNG_sRGB_INTENT_PERCEPTUAL);
    }
    else if (source->declaresGamma)
    {
        png_set_gAMA_fixed(session->png, session->info,
                           static_cast<png_fixed_point>(std::lround(source->fileGamma * 100000.0)));
    }
    if (source->textCount > 0)
    {
        png_set_text(session->png, session->info, source->text, source->textCount);
    }
    if (source->exif != nullptr && source->exifBytes > 0u)
    {
        png_set_eXIf_1(session->png, session->info, static_cast<png_uint_32>(source->exifBytes),
                       reinterpret_cast<png_bytep>(const_cast<std::byte*>(source->exif)));
    }
    png_write_info(session->png, session->info);
    if (source->swapBytes)
    {
        png_set_swap(session->png);
    }
    for (std::uint32_t y = 0u; y < source->height; ++y)
    {
        png_bytep row = source->row(source->user, y);
        if (row == nullptr)
        {
            session->stopped = true;
            return false;
        }
        png_write_row(session->png, row);
    }
    png_write_end(session->png, session->info);
    return true;
}

Result<CodecEncodeReport> writePng(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecEncodeRequest& request, const RawFrame& frame,
                                   const ImageMetadata& metadata)
{
    const auto descriptor = *frame.descriptor();
    const auto layout = *frame.layout();
    const auto& extent = descriptor.extent();
    const auto channels = extent.channelCount();
    const auto format = descriptor.sampleFormat();
    const auto schema = descriptor.channelSchema();
    const bool describable = (channels == 1u && schema == ChannelSchema::Mono) ||
                             (channels == 2u && schema == ChannelSchema::Named) ||
                             (channels == 3u && (schema == ChannelSchema::Rgb || schema == ChannelSchema::Named)) ||
                             (channels == 4u && (schema == ChannelSchema::Rgba || schema == ChannelSchema::Named));
    const bool representable =
        format.sampleClass == RawSampleClass::UnsignedInteger && (format.sizeBytes == 1u || format.sizeBytes == 2u);
    const auto compression = request.intent != nullptr ? request.intent->compression() : OutputCompression::Deflate;
    // Every refusal is decided before the pending file is opened. This container holds unsigned
    // eight- and sixteen-bit samples, states no filter array and no scale, stores its sixteen-bit
    // samples in one byte order, and its rows always run from the top.
    if (!representable || !describable || descriptor.cfa().isMosaiced() || statesPayloadForm(request.intent) ||
        descriptor.coordinateOrigin() != CoordinateOrigin::TopLeft ||
        (compression != OutputCompression::None && compression != OutputCompression::Deflate) ||
        (request.intent != nullptr &&
         (request.intent->scaling() != OutputScaling::None || request.intent->byteOrder() == ByteOrder::Little)))
    {
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Png,
                            "encode PNG options", request.pendingPath);
    }
    auto cards = outputCards(request, metadata);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    auto exif = writeExifBlock(*cards);
    if (!exif.hasValue())
    {
        return exif.status();
    }
    // The text entries are built here, where the strings that back them live for the whole write:
    // the library keeps the pointers it is given until the header has been written.
    std::vector<std::string> keywords;
    std::vector<std::string> values;
    for (const CodecCard& card : *cards)
    {
        if (isExifKeyword(card.keyword()))
        {
            continue;
        }
        if (card.comment() == kExifContinuedComment && !keywords.empty() && keywords.back() == card.keyword())
        {
            values.back() += card.value();
            continue;
        }
        keywords.push_back(card.keyword());
        values.push_back(card.value());
    }
    std::vector<png_text> entries;
    entries.reserve(keywords.size());
    for (std::size_t index = 0u; index < keywords.size(); ++index)
    {
        const bool ascii = std::all_of(values[index].begin(), values[index].end(),
                                       [](char character) { return static_cast<unsigned char>(character) < 0x80u; });
        png_text entry{};
        entry.compression = ascii ? PNG_TEXT_COMPRESSION_NONE : PNG_ITXT_COMPRESSION_NONE;
        entry.key = const_cast<char*>(keywords[index].c_str());
        entry.text = const_cast<char*>(values[index].c_str());
        entry.text_length = ascii ? values[index].size() : 0u;
        entry.itxt_length = ascii ? 0u : values[index].size();
        entries.push_back(entry);
    }
    const auto& semantics = metadata.semantics();
    const bool standard = semantics.transferFunction() == TransferFunction::Srgb;
    bool declaresGamma = false;
    double fileGamma = 1.0;
    if (semantics.transferFunction() == TransferFunction::Linear ||
        (semantics.transferFunction() == TransferFunction::Unspecified &&
         descriptor.numericDomain() == NumericDomain::NormalizedLinear))
    {
        declaresGamma = true;
    }
    else if (semantics.transferFunction() == TransferFunction::PureGamma && semantics.gammaExponent().has_value() &&
             *semantics.gammaExponent() > 0.0)
    {
        declaresGamma = true;
        fileGamma = 1.0 / *semantics.gammaExponent();
    }
    if (declaresGamma && (fileGamma <= 0.00001 || fileGamma > 21474.0))
    {
        declaresGamma = false;
    }
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    const bool planar = layout.planeCount() > 1u;
    RowWorkspace workspace;
    if (planar)
    {
        if (const auto acquired =
                workspace.acquire(engine, static_cast<std::uint64_t>(extent.width()) * channels * format.sizeBytes);
            acquired.failed())
        {
            return acquired;
        }
    }
    RowSource source{
        &execution,     frame.bytes().data(), workspace.bytes(), layout.rowStrideBytes(), layout.planeStrideBytes(),
        extent.width(), extent.height(),      channels,          format.sizeBytes,        planar};
    auto opened = evaluateAcrossBoundary(kPngBoundary, [&]() -> PngWriteHandle {
        PngWriteHandle session{new PngWriteSession()};
        session->stream.open(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(request.pendingPath.data()),
                                                     request.pendingPath.size())),
            std::ios::binary | std::ios::trunc);
        return session;
    });
    if (!opened.hasValue())
    {
        return codecFailure(opened.status().code(), CodecFailure::DestinationUnavailable, CodecFormat::Png,
                            "create PNG pending output", request.pendingPath, opened.status());
    }
    PngWriteHandle session = std::move(*opened);
    if (!session->stream.is_open())
    {
        return pngFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable, "create PNG pending output",
                          request.pendingPath, "the pending output path could not be opened for writing");
    }
    session->png = png_create_write_struct(PNG_LIBPNG_VER_STRING, session.get(), &pngWriteError, &pngWriteWarning);
    if (session->png == nullptr)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    session->info = png_create_info_struct(session->png);
    if (session->info == nullptr)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    const auto resolved = descriptor.byteOrder() == ByteOrder::Native ? nativeByteOrder() : descriptor.byteOrder();
    const int colorType = channels == 1u   ? PNG_COLOR_TYPE_GRAY
                          : channels == 2u ? PNG_COLOR_TYPE_GRAY_ALPHA
                          : channels == 3u ? PNG_COLOR_TYPE_RGB
                                           : PNG_COLOR_TYPE_RGB_ALPHA;
    const PngEncodeSource encode{extent.width(),
                                 extent.height(),
                                 static_cast<int>(format.sizeBytes * 8u),
                                 colorType,
                                 compression == OutputCompression::None
                                     ? 0
                                     : static_cast<int>(request.intent != nullptr
                                                            ? request.intent->compressionLevel().value_or(
                                                                  static_cast<std::uint32_t>(kDefaultCompressionLevel))
                                                            : static_cast<std::uint32_t>(kDefaultCompressionLevel)),
                                 format.sizeBytes == 2u && resolved != ByteOrder::Big,
                                 standard,
                                 declaresGamma,
                                 fileGamma,
                                 entries.empty() ? nullptr : entries.data(),
                                 static_cast<int>(entries.size()),
                                 exif->empty() ? nullptr : exif->data(),
                                 exif->size(),
                                 &source,
                                 &pngRowOf};
    const bool written = pngWriteImage(session.get(), &encode);
    if (session->stopped)
    {
        const auto stopped = execution.checkCancellation();
        return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
    }
    if (!written)
    {
        return pngFailure(ErrorCode::ExternalFailure, CodecFailure::CallbackFailure, "encode PNG image",
                          request.pendingPath, session->message);
    }
    session->stream.close();
    if (session->failed || session->stream.fail())
    {
        return pngFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "write PNG output", request.pendingPath,
                          "the pending PNG payload could not be written in full");
    }
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(request.pendingPath.data()),
                                                 request.pendingPath.size())),
        sizeError);
    if (sizeError)
    {
        return Status::failure(ErrorCode::ExternalFailure);
    }
    return CodecEncodeReport{static_cast<std::uint64_t>(size)};
}

} // namespace an
