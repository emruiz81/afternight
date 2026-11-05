// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - encoding a JPEG container into a pending path or a transaction's output.
//
// Every refusal is decided before the output is opened: the frame must be eight-bit unsigned grey or
// three-channel colour, top-down, unmosaiced and at most 65,535 pixels a side, and an intent must
// state the JPEG compression and no scale. The cards are the acquisition facts, then the source's
// writable cards when the intent preserves them, then the intent's, with the engine's software stamp
// last, and a write with no intent states none; they become at most one Exif marker and one comment
// marker, each within what one marker holds. The quality is the intent's compression level, or 95.
// A grey frame whose sides are whole blocks is handed to the encoder as raw blocks and anything else
// as scanlines, and a planar frame is interleaved one row at a time through a row of counted
// workspace. Bytes are counted as the output accepts them; a failed write is recorded and reported
// once the encode has unwound.

#include "io/jpeg/JpegFile.h"

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "interop/exiv2/ExifCards.h"
#include "io/registry/PayloadForm.h"
#include "io/registry/PreparedCodecWrite.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <utility>

namespace an
{
namespace
{

/// The keyword a comment marker is written from.
constexpr std::string_view kCommentKeyword = "Comment";

/// What an Exif marker states before the block itself.
constexpr std::string_view kExifMarkerPrefix{"Exif\0\0", 6u};

/// The largest payload one marker holds: the 65,535 bytes its length field can state, less that
/// field's own two. An Exif marker's identifier is part of the payload and counts against it.
constexpr std::size_t kMaxMarkerBytes = 65533u;

/// The quality this container is written at when the caller states none.
constexpr int kDefaultQuality = 95;

/// The largest extent this container states.
constexpr std::uint32_t kMaxJpegExtent = 65535u;

/// Reports a refusal by leaving the frame the library was called from.
///
/// @param info the library's own state, whose client data is the session.
void jpegWriteErrorExit(j_common_ptr info)
{
    auto* const session = static_cast<JpegWriteSession*>(info->client_data);
    if (session == nullptr)
    {
        std::abort();
    }
    (*info->err->format_message)(info, session->message);
    std::longjmp(session->escape, 1);
}

/// Swallows what the library would otherwise write to the process's error stream.
void jpegSilentWriteMessage(j_common_ptr) {}

/// Points the library at the buffer this session fills.
///
/// @param info the library's own state.
void jpegInitDestination(j_compress_ptr info)
{
    auto* const session = static_cast<JpegWriteSession*>(info->client_data);
    session->destination.next_output_byte = session->buffer;
    session->destination.free_in_buffer = kJpegBufferBytes;
}

/// Writes a full buffer to the stream and starts the next one.
///
/// A failed write is recorded rather than thrown: the library is told to continue, the bytes it
/// produces are discarded, and the caller reports a short write once the encode has unwound.
///
/// @param info the library's own state.
/// @return always true: this destination never suspends.
boolean jpegEmptyBuffer(j_compress_ptr info)
{
    auto* const session = static_cast<JpegWriteSession*>(info->client_data);
    if (session->output != nullptr)
    {
        if (!session->output->write({reinterpret_cast<const std::byte*>(session->buffer), kJpegBufferBytes}))
            session->failed = true;
    }
    else
    {
        session->stream->write(reinterpret_cast<const char*>(session->buffer),
                               static_cast<std::streamsize>(kJpegBufferBytes));
        if (!*session->stream)
            session->failed = true;
    }
    session->bytesWritten += kJpegBufferBytes;
    session->destination.next_output_byte = session->buffer;
    session->destination.free_in_buffer = kJpegBufferBytes;
    return TRUE;
}

/// Writes whatever the last buffer still holds.
///
/// @param info the library's own state.
void jpegTermDestination(j_compress_ptr info)
{
    auto* const session = static_cast<JpegWriteSession*>(info->client_data);
    const auto pending = kJpegBufferBytes - session->destination.free_in_buffer;
    if (pending > 0u)
    {
        if (session->output != nullptr)
        {
            if (!session->output->write({reinterpret_cast<const std::byte*>(session->buffer), pending}))
                session->failed = true;
        }
        else
            session->stream->write(reinterpret_cast<const char*>(session->buffer),
                                   static_cast<std::streamsize>(pending));
        session->bytesWritten += pending;
    }
    if (session->output == nullptr)
    {
        session->stream->flush();
        if (!*session->stream)
            session->failed = true;
    }
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
    bool planar = false; ///< Whether the channels sit in separate planes.
};

/// Supplies a bounded group of interleaved rows to the library.
///
/// @param user the row source.
/// @param index the first row.
/// @param count how many rows; one when the input needs planar repacking.
/// @param rows the pointers the library receives.
/// @return false when the caller has asked the operation to stop.
bool jpegRowsOf(void* user, std::uint32_t index, std::uint32_t count, JSAMPROW* rows)
{
    auto* const source = static_cast<RowSource*>(user);
    if (source->execution->checkCancellation().failed())
    {
        return false;
    }
    const std::byte* row = source->bytes + index * source->rowStrideBytes;
    if (source->planar)
    {
        for (std::uint32_t channel = 0u; channel < source->channels; ++channel)
        {
            const std::byte* const plane = row + channel * source->planeStrideBytes;
            for (std::uint32_t x = 0u; x < source->width; ++x)
            {
                source->workspace[x * source->channels + channel] = plane[x];
            }
        }
        row = source->workspace;
    }
    for (std::uint32_t offset = 0u; offset < count; ++offset)
        rows[offset] = reinterpret_cast<JSAMPROW>(const_cast<std::byte*>(row + offset * source->rowStrideBytes));
    source->execution->reportProgress(index + count, source->height, "encode JPEG");
    return !source->execution->checkCancellation().failed();
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

/// The cards the encoder writes, in the order it writes them.
///
/// The acquisition facts this engine holds first, then the source's own writable cards when the
/// intent preserves them, then the intent's, so a stated card replaces a preserved one of the same
/// name. A raw pass-through states nothing: it has no intent to state it in.
///
/// @param request the pending target and explicit intent.
/// @param metadata the immutable source facts.
/// @return the cards, or a typed failure naming a card this container cannot carry.
[[nodiscard]] Result<std::vector<CodecCard>> outputCards(const CodecEncodeRequest& request,
                                                         const ImageMetadata& metadata)
{
    std::vector<CodecCard> cards;
    if (request.intent == nullptr)
    {
        return cards;
    }
    auto projected = acquisitionCards({.acquisition = metadata.acquisition().toRequest()}, CodecNamespace::Jpeg);
    if (!projected.hasValue())
    {
        return projected.status();
    }
    cards = std::move(*projected);
    const auto append = [&cards](const CodecCard& card) {
        if (card.comment() != kExifContinuedComment)
        {
            std::erase_if(cards, [&card](const CodecCard& existing) { return existing.keyword() == card.keyword(); });
        }
        cards.push_back(card);
    };
    if (request.intent->preserveSourceCards())
    {
        for (const CodecCard& card : metadata.codec().cards())
        {
            if (cardDisposition(CodecFormat::Jpeg, card) == CardDisposition::Written)
            {
                append(card);
            }
        }
    }
    for (const CodecCard& card : request.intent->cards())
    {
        if (isArtifactIdentityKeyword(card.keyword()))
        {
            return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Jpeg,
                                "encode JPEG cards", request.pendingPath,
                                Status::failure(ErrorCode::Unsupported,
                                                {.message = "a JPEG container has no keyword-addressed text a stamp "
                                                            "could be read back from, so an artifact identity is "
                                                            "refused rather than written where nothing would find it",
                                                 .operation = "encode JPEG cards",
                                                 .domain = ErrorDomain::Io}));
        }
        if (!isExifKeyword(card.keyword()) && card.keyword() != kCommentKeyword)
        {
            return codecFailure(
                ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Jpeg, "encode JPEG cards",
                request.pendingPath,
                Status::failure(ErrorCode::Unsupported,
                                {.message = "this container carries Exif fields and comment markers; "
                                            "a card of another vocabulary has nowhere to go in it",
                                 .operation = "encode JPEG cards",
                                 .domain = ErrorDomain::Io,
                                 .context = {StatusContextField::ofText(ContextKey::Parameter, card.keyword())}}));
        }
        append(card);
    }
    // The engine that wrote the file says so, whatever the source said. The rest of the acquisition
    // record is the source's; this one field is a fact about this write.
    auto software =
        CodecCard::create(CodecNamespace::Jpeg, "Exif.Image.Software", std::string(kEngineVersionString), "Ascii");
    if (!software.hasValue())
    {
        return software.status();
    }
    append(*software);
    return cards;
}

} // namespace

Result<JpegSoftwareMetadata> prepareJpegSoftwareMetadata()
{
    auto card =
        CodecCard::create(CodecNamespace::Jpeg, "Exif.Image.Software", std::string(kEngineVersionString), "Ascii");
    if (!card.hasValue())
        return card.status();
    auto bytes = writeExifBlock(std::span<const CodecCard>(&*card, 1u));
    if (!bytes.hasValue())
        return bytes.status();
    auto decoded = readExifBlock(CodecNamespace::Jpeg, *bytes);
    if (!decoded.hasValue())
        return decoded.status();
    JpegSoftwareMetadata software;
    software.marker.resize(kExifMarkerPrefix.size() + bytes->size());
    std::memcpy(software.marker.data(), kExifMarkerPrefix.data(), kExifMarkerPrefix.size());
    std::memcpy(software.marker.data() + kExifMarkerPrefix.size(), bytes->data(), bytes->size());
    software.cards = std::move(*decoded);
    return software;
}

void JpegWriteSessionDeleter::operator()(JpegWriteSession* session) const noexcept
{
    if (session == nullptr)
    {
        return;
    }
    if (session->created)
    {
        jpeg_destroy_compress(&session->info);
    }
    delete session;
}

bool jpegWriteImage(JpegWriteSession* session, const JpegEncodeSource* source) noexcept
{
    if (setjmp(session->escape) != 0)
    {
        return false;
    }
    jpeg_create_compress(&session->info);
    session->created = true;
    session->info.dest = &session->destination;
    session->info.image_width = source->width;
    session->info.image_height = source->height;
    session->info.input_components = static_cast<int>(source->channels);
    session->info.in_color_space = source->channels == 1u ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&session->info);
    jpeg_set_quality(&session->info, source->quality, TRUE);
    // A complete-block grey plane needs neither colour conversion nor downsampling. Supply
    // its existing rows directly to the same DCT/entropy encoder; incomplete blocks and RGB
    // retain the library's scanline preprocessing and edge extension.
    const bool directBlocks = source->channels == 1u && source->width % DCTSIZE == 0u && source->height % DCTSIZE == 0u;
    session->info.raw_data_in = directBlocks;
    jpeg_start_compress(&session->info, TRUE);
    for (std::size_t index = 0u; index < source->markerCount; ++index)
    {
        const JpegMarker& marker = source->markers[index];
        jpeg_write_marker(&session->info, marker.code, reinterpret_cast<const JOCTET*>(marker.bytes),
                          static_cast<unsigned int>(marker.size));
    }
    while (session->info.next_scanline < session->info.image_height)
    {
        auto count = (std::min)(source->rowBatch, session->info.image_height - session->info.next_scanline);
        if (directBlocks)
            count -= count % DCTSIZE;
        JSAMPROW rows[kJpegRowBatch];
        if (!source->rows(source->user, session->info.next_scanline, count, rows))
        {
            session->stopped = true;
            return false;
        }
        JDIMENSION accepted = 0u;
        while (accepted < count)
        {
            JSAMPARRAY plane = rows + accepted;
            const auto got = directBlocks ? jpeg_write_raw_data(&session->info, &plane, count - accepted)
                                          : jpeg_write_scanlines(&session->info, plane, count - accepted);
            if (got == 0u)
                return false;
            accepted += got;
        }
    }
    jpeg_finish_compress(&session->info);
    return true;
}

Result<CodecEncodeReport> writeJpeg(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata, const JpegSoftwareMetadata& software,
                                    CodecOutput* output)
{
    const auto descriptor = *frame.descriptor();
    const auto layout = *frame.layout();
    const auto& extent = descriptor.extent();
    const auto channels = extent.channelCount();
    const auto schema = descriptor.channelSchema();
    const bool describable = channels == 1u
                                 ? schema == ChannelSchema::Mono
                                 : channels == 3u && (schema == ChannelSchema::Rgb || schema == ChannelSchema::Named);
    // Every refusal is decided before the pending file is opened. A JPEG payload is always
    // JPEG-compressed, its rows always run from the top, and it states neither a filter array nor a
    // scale, so an intent asking for any of those describes a file this container cannot hold.
    if (descriptor.sampleFormat().sizeBytes != 1u ||
        descriptor.sampleFormat().sampleClass != RawSampleClass::UnsignedInteger || !describable ||
        descriptor.cfa().isMosaiced() || descriptor.coordinateOrigin() != CoordinateOrigin::TopLeft ||
        extent.width() > kMaxJpegExtent || extent.height() > kMaxJpegExtent || statesPayloadForm(request.intent) ||
        (request.intent != nullptr && (request.intent->scaling() != OutputScaling::None ||
                                       request.intent->compression() != OutputCompression::Jpeg)))
    {
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Jpeg,
                            "encode JPEG options", request.pendingPath);
    }
    auto cards = outputCards(request, metadata);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    // The version stamp is the same for every write by this immutable adapter. Other Exif
    // fields still use the library; comment markers do not change the Exif block at all.
    const bool softwareOnly =
        request.intent != nullptr && std::all_of(cards->begin(), cards->end(), [](const CodecCard& card) {
            return !isExifKeyword(card.keyword()) || card.keyword() == "Exif.Image.Software";
        });
    std::vector<std::byte> encodedExif;
    std::span<const std::byte> exifMarker;
    if (softwareOnly)
        exifMarker = software.marker;
    else
    {
        auto exif = writeExifBlock(*cards);
        if (!exif.hasValue())
            return exif.status();
        if (!exif->empty())
        {
            encodedExif.resize(kExifMarkerPrefix.size() + exif->size());
            std::memcpy(encodedExif.data(), kExifMarkerPrefix.data(), kExifMarkerPrefix.size());
            std::memcpy(encodedExif.data() + kExifMarkerPrefix.size(), exif->data(), exif->size());
            exifMarker = encodedExif;
        }
    }
    const auto comment = joinField(*cards, kCommentKeyword);
    if (exifMarker.size() > kMaxMarkerBytes || (comment.has_value() && comment->size() > kMaxMarkerBytes))
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            {.message = "the metadata this write states is longer than one JPEG marker holds",
             .operation = "encode JPEG cards",
             .domain = ErrorDomain::Io,
             .context = {StatusContextField::ofCount(
                             ContextKey::Requested,
                             (std::max)(exifMarker.size(), comment.has_value() ? comment->size() : std::size_t{0u})),
                         StatusContextField::ofCount(ContextKey::Limit, kMaxMarkerBytes)}});
    }
    std::vector<JpegMarker> markers;
    if (!exifMarker.empty())
    {
        markers.push_back(JpegMarker{JPEG_APP0 + 1, exifMarker.data(), exifMarker.size()});
    }
    if (comment.has_value())
    {
        markers.push_back(JpegMarker{JPEG_COM, reinterpret_cast<const std::byte*>(comment->data()), comment->size()});
    }
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    const bool planar = layout.planeCount() > 1u;
    RowWorkspace workspace;
    if (planar)
    {
        if (const auto acquired = workspace.acquire(engine, static_cast<std::uint64_t>(extent.width()) * channels);
            acquired.failed())
        {
            return acquired;
        }
    }
    RowSource source{&execution,
                     frame.bytes().data(),
                     workspace.bytes(),
                     layout.rowStrideBytes(),
                     layout.planeStrideBytes(),
                     extent.width(),
                     extent.height(),
                     channels,
                     planar};
    auto opened = evaluateAcrossBoundary(kJpegBoundary, [&]() -> JpegWriteHandle {
        JpegWriteHandle session{new JpegWriteSession};
        session->output = output;
        if (output == nullptr)
            session->stream.emplace(
                std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(request.pendingPath.data()),
                                                         request.pendingPath.size())),
                std::ios::binary | std::ios::trunc);
        return session;
    });
    if (!opened.hasValue())
    {
        return codecFailure(opened.status().code(), CodecFailure::DestinationUnavailable, CodecFormat::Jpeg,
                            "create JPEG pending output", request.pendingPath, opened.status());
    }
    JpegWriteHandle session = std::move(*opened);
    if (output == nullptr && !session->stream->is_open())
    {
        return jpegFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable,
                           "create JPEG pending output", request.pendingPath,
                           "the pending output path could not be opened for writing");
    }
    session->info.err = jpeg_std_error(&session->error);
    session->error.error_exit = &jpegWriteErrorExit;
    session->error.output_message = &jpegSilentWriteMessage;
    session->info.client_data = session.get();
    session->destination.init_destination = &jpegInitDestination;
    session->destination.empty_output_buffer = &jpegEmptyBuffer;
    session->destination.term_destination = &jpegTermDestination;
    const JpegEncodeSource encode{
        extent.width(),
        extent.height(),
        channels,
        request.intent != nullptr
            ? static_cast<int>(request.intent->compressionLevel().value_or(static_cast<std::uint32_t>(kDefaultQuality)))
            : kDefaultQuality,
        markers.empty() ? nullptr : markers.data(),
        markers.size(),
        &source,
        planar ? 1u
               : static_cast<std::uint32_t>(
                     (std::min)(std::uint64_t{kJpegRowBatch},
                                (std::max)(std::uint64_t{1u},
                                           kJpegScanlineChunkBytes /
                                               (static_cast<std::uint64_t>(extent.width()) * channels)))),
        &jpegRowsOf};
    const bool written = jpegWriteImage(session.get(), &encode);
    if (session->stopped)
    {
        const auto stopped = execution.checkCancellation();
        return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
    }
    if (!written)
    {
        return jpegFailure(ErrorCode::ExternalFailure, CodecFailure::CallbackFailure, "encode JPEG image",
                           request.pendingPath, session->message);
    }
    if (output == nullptr)
        session->stream->close();
    if (session->failed || (output == nullptr && session->stream->fail()))
    {
        return jpegFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, "write JPEG output",
                           request.pendingPath, "the pending JPEG payload could not be written in full");
    }
    // Output counts every accepted byte; the transaction independently checks its file's size and close.
    return CodecEncodeReport{session->bytesWritten};
}

} // namespace an
