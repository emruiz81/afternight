// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - opening a JPEG container, describing it, and decoding its pixels.
//
// The file is opened natively - a Windows handle or a POSIX descriptor - so a literal UTF-8 path
// reaches the file system unchanged, and on Windows the handle refuses in-place writes while the
// session reads. The library pulls from it through the session's own buffer. When metadata is wanted,
// the comment, Exif and colour-profile markers are counted and sized by a scan of the raw file
// before the library is asked to keep any of them, so a file with more than the card model holds is
// refused before the library allocates for it.
//
// A description is eight-bit grey or red-green-blue, interleaved and top-down, nonlinearly encoded
// with no stated colour basis. A decode of a grey image whose sides are whole blocks goes straight
// into the frame through the library's raw-data interface; everything else is decoded by scanline.
// Both run in groups bounded by `kJpegRowBatch` rows and `kJpegScanlineChunkBytes`, with
// cancellation checked and progress reported between groups, and the library's working memory is
// held to what remains of the engine context's budget. A payload that ends early is refused rather
// than completed with invented rows.

#include "io/jpeg/JpegFile.h"

#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "interop/exiv2/ExifCards.h"

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOSERVICE
#define NOSERVICE
#endif
#ifndef NOMCX
#define NOMCX
#endif
#ifndef NOIME
#define NOIME
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace an
{
namespace
{

/// What an Exif marker states before the block itself.
constexpr std::string_view kExifMarkerPrefix{"Exif\0\0", 6u};

/// What a colour-profile marker states before its chunk, including the chunk's two counters.
constexpr std::string_view kIccMarkerPrefix{"ICC_PROFILE\0", 12u};

/// The keyword a container's own comment marker is preserved under.
constexpr std::string_view kCommentKeyword = "Comment";

/// The keyword the presence of an embedded colour profile is preserved under.
constexpr std::string_view kIccKeyword = "ICC_PROFILE";

/// The aggregate payload retained by the three metadata marker classes, in the codec model's terms.
constexpr std::uint64_t kMaxSavedMarkerBytes = kMaxCodecCards * kMaxCodecCardTextBytes;

/// What the scan of the retained metadata markers found.
enum class MarkerScanResult
{
    WithinLimit, ///< Within the card count and bytes, or the scan could not tell and the library decides.
    ExceedsLimit ///< More markers or more payload than the codec's card model holds.
};

/// Fills a scan buffer from the already-open native source.
[[nodiscard]] std::size_t readNative(JpegReadSession& session, unsigned char* bytes, std::size_t size) noexcept
{
#ifdef _WIN32
    DWORD received = 0u;
    if (!ReadFile(reinterpret_cast<HANDLE>(session.file), bytes, static_cast<DWORD>(size), &received, nullptr))
        return 0u;
#else
    ssize_t received;
    do
        received = ::read(static_cast<int>(session.file), bytes, size);
    while (received < 0 && errno == EINTR);
    if (received <= 0)
        return 0u;
#endif
    return static_cast<std::size_t>(received);
}

/// Moves through a marker payload without bringing it into memory.
[[nodiscard]] bool skipNative(JpegReadSession& session, std::uint64_t bytes) noexcept
{
#ifdef _WIN32
    LARGE_INTEGER distance{};
    distance.QuadPart = static_cast<LONGLONG>(bytes);
    return SetFilePointerEx(reinterpret_cast<HANDLE>(session.file), distance, nullptr, FILE_CURRENT) != 0;
#else
    return ::lseek(static_cast<int>(session.file), static_cast<off_t>(bytes), SEEK_CUR) != static_cast<off_t>(-1);
#endif
}

/// Returns the native source to its first byte after a bounded marker scan.
void rewindNative(JpegReadSession& session) noexcept
{
#ifdef _WIN32
    LARGE_INTEGER start{};
    static_cast<void>(SetFilePointerEx(reinterpret_cast<HANDLE>(session.file), start, nullptr, FILE_BEGIN));
#else
    static_cast<void>(::lseek(static_cast<int>(session.file), 0, SEEK_SET));
#endif
}

/// A buffered, allocation-free walk over the source while libjpeg has not started reading it.
class MarkerReader
{
public:
    explicit MarkerReader(JpegReadSession& session) noexcept : m_session(session) {}

    [[nodiscard]] bool next(unsigned char& byte) noexcept
    {
        if (m_position == m_size)
        {
            m_size = readNative(m_session, m_session.buffer, kJpegBufferBytes);
            m_position = 0u;
            if (m_size == 0u)
                return false;
        }
        byte = m_session.buffer[m_position++];
        return true;
    }

    [[nodiscard]] bool skip(std::uint64_t bytes) noexcept
    {
        const std::size_t buffered = m_size - m_position;
        if (bytes <= buffered)
        {
            m_position += static_cast<std::size_t>(bytes);
            return true;
        }
        bytes -= buffered;
        m_position = m_size = 0u;
        return skipNative(m_session, bytes);
    }

private:
    JpegReadSession& m_session;
    std::size_t m_position = 0u;
    std::size_t m_size = 0u;
};

/// Counts the marker payloads libjpeg would retain, before asking it to allocate any of them.
[[nodiscard]] MarkerScanResult scanSavedMarkers(JpegReadSession& session) noexcept
{
    const auto finish = [&session](MarkerScanResult result) {
        rewindNative(session);
        return result;
    };
    MarkerReader reader(session);
    unsigned char first = 0u, second = 0u;
    if (!reader.next(first) || !reader.next(second) || first != 0xffu || second != 0xd8u)
        return finish(MarkerScanResult::WithinLimit);
    std::uint64_t count = 0u;
    std::uint64_t bytes = 0u;
    for (;;)
    {
        unsigned char prefix = 0u;
        do
            if (!reader.next(prefix))
                return finish(MarkerScanResult::WithinLimit);
        while (prefix != 0xffu);
        unsigned char marker = 0xffu;
        while (marker == 0xffu)
            if (!reader.next(marker))
                return finish(MarkerScanResult::WithinLimit);
        if (marker == 0xd9u)
            return finish(MarkerScanResult::WithinLimit);
        if (marker == 0x00u)
            continue;
        if (marker == 0xd8u || marker == 0x01u || (marker >= 0xd0u && marker <= 0xd7u))
            continue;
        unsigned char lengthHigh = 0u, lengthLow = 0u;
        if (!reader.next(lengthHigh) || !reader.next(lengthLow))
            return finish(MarkerScanResult::WithinLimit);
        const std::uint16_t length = static_cast<std::uint16_t>((static_cast<std::uint16_t>(lengthHigh) << 8u) |
                                                                static_cast<std::uint16_t>(lengthLow));
        if (length < 2u)
            return finish(MarkerScanResult::WithinLimit);
        const std::uint64_t payload = length - 2u;
        if (marker == JPEG_COM || marker == JPEG_APP0 + 1 || marker == JPEG_APP0 + 2)
        {
            if (count >= kMaxCodecCards || payload > kMaxSavedMarkerBytes - bytes)
                return finish(MarkerScanResult::ExceedsLimit);
            ++count;
            bytes += payload;
        }
        if (!reader.skip(payload))
            return finish(MarkerScanResult::WithinLimit);
    }
}

/// Reports a refusal by leaving the frame the library was called from.
///
/// @param info the library's own state, whose client data is the session.
void jpegReadErrorExit(j_common_ptr info)
{
    auto* const session = static_cast<JpegReadSession*>(info->client_data);
    // The session is attached before any call that can fail. A handler that returned would let the
    // library continue in a state it has already declared unusable, so there is nothing else to do.
    if (session == nullptr)
    {
        std::abort();
    }
    (*info->err->format_message)(info, session->message);
    std::longjmp(session->escape, 1);
}

/// Swallows what the library would otherwise write to the process's error stream.
void jpegSilentMessage(j_common_ptr) {}

/// Starts a read; the stream is already open and positioned.
void jpegInitSource(j_decompress_ptr) {}

/// Fills the library's buffer from the stream, or ends the container where it actually ends.
///
/// @param info the library's own state.
/// @return always true: this source never suspends, because the stream blocks.
boolean jpegFillInput(j_decompress_ptr info)
{
    auto* const session = static_cast<JpegReadSession*>(info->client_data);
    auto* const buffer = session->buffer;
#ifdef _WIN32
    DWORD read = 0u;
    if (!ReadFile(reinterpret_cast<HANDLE>(session->file), buffer, kJpegBufferBytes, &read, nullptr))
        read = 0u;
#else
    ssize_t read;
    do
        read = ::read(static_cast<int>(session->file), buffer, kJpegBufferBytes);
    while (read < 0 && errno == EINTR);
#endif
    if (read <= 0)
    {
        // The payload ends before the container said it would. An end-of-image marker lets the
        // library finish rather than read past its buffer, and the flag is what turns the result
        // into a refusal instead of an image whose last rows are invented.
        session->truncated = true;
        buffer[0] = 0xffu;
        buffer[1] = 0xd9u;
        session->source.next_input_byte = buffer;
        session->source.bytes_in_buffer = 2u;
        return TRUE;
    }
    session->source.next_input_byte = buffer;
    session->source.bytes_in_buffer = static_cast<std::size_t>(read);
    return TRUE;
}

/// Skips over a marker the library does not want.
///
/// @param info the library's own state.
/// @param count how many bytes to skip; a count of zero or less is the library's own no-op.
void jpegSkipInput(j_decompress_ptr info, long count)
{
    if (count <= 0)
    {
        return;
    }
    auto* const session = static_cast<JpegReadSession*>(info->client_data);
    auto remaining = static_cast<std::size_t>(count);
    while (remaining > session->source.bytes_in_buffer)
    {
        remaining -= session->source.bytes_in_buffer;
        session->source.bytes_in_buffer = 0u;
        static_cast<void>(jpegFillInput(info));
        if (session->truncated)
        {
            return;
        }
    }
    session->source.next_input_byte += remaining;
    session->source.bytes_in_buffer -= remaining;
}

/// Ends a read; the stream is closed with the session.
void jpegTermSource(j_decompress_ptr) {}

/// What runs between two decoded rows.
struct RowProgress
{
    const ExecutionContext* execution = nullptr; ///< Whose cancellation is checked.
    std::uint32_t height = 0u; ///< How many rows the image has.
};

/// Checks cancellation and reports progress between rows.
///
/// @param user the progress record.
/// @param rows how many rows are decoded.
/// @return false when the caller has asked the operation to stop.
bool jpegRowStep(void* user, std::uint32_t rows)
{
    auto* const progress = static_cast<RowProgress*>(user);
    if (progress->execution->checkCancellation().failed())
    {
        return false;
    }
    progress->execution->reportProgress(rows, progress->height, "decode JPEG");
    return !progress->execution->checkCancellation().failed();
}

/// Reads the text a comment marker carries, without the padding some writers leave on it.
///
/// @param bytes the marker's payload.
/// @return the text.
[[nodiscard]] std::string markerText(std::span<const std::byte> bytes)
{
    std::size_t size = bytes.size();
    while (size > 0u && std::to_integer<unsigned char>(bytes[size - 1u]) == 0u)
    {
        --size;
    }
    return std::string(reinterpret_cast<const char*>(bytes.data()), size);
}

/// Reads every card the container's markers state.
///
/// An Exif marker identical to the adapter's own software stamp takes the stamp's prepared cards
/// rather than being decoded again. A colour profile is preserved as its length alone.
///
/// @param session the open session, whose header has been read.
/// @param software the adapter's software stamp and the cards it decodes to.
/// @return the cards, or a typed limit or metadata failure.
[[nodiscard]] Result<std::vector<CodecCard>> jpegCards(const JpegReadSession& session,
                                                       const JpegSoftwareMetadata& software)
{
    std::vector<CodecCard> cards;
    std::uint64_t profileBytes = 0u;
    std::size_t markerCount = 0u;
    for (const jpeg_saved_marker_ptr* marker = &session.info.marker_list; *marker != nullptr; marker = &(*marker)->next)
    {
        if (++markerCount > kMaxCodecCards)
            return Status::failure(ErrorCode::LimitExceeded);
        const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>((*marker)->data),
                                               (*marker)->data_length};
        const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        if ((*marker)->marker == JPEG_COM)
        {
            if (const auto added = appendContinuedCard(cards, CodecNamespace::Jpeg, std::string(kCommentKeyword),
                                                       markerText(bytes), "comment marker the file carries");
                added.failed())
            {
                return added;
            }
            continue;
        }
        if ((*marker)->marker == JPEG_APP0 + 1 && text.starts_with(kExifMarkerPrefix))
        {
            auto exif = std::equal(bytes.begin(), bytes.end(), software.marker.begin(), software.marker.end())
                            ? Result<std::vector<CodecCard>>(software.cards)
                            : readExifBlock(CodecNamespace::Jpeg, bytes.subspan(kExifMarkerPrefix.size()));
            if (!exif.hasValue())
            {
                return exif.status();
            }
            for (CodecCard& card : *exif)
            {
                if (cards.size() >= kMaxCodecCards)
                {
                    return Status::failure(ErrorCode::LimitExceeded);
                }
                cards.push_back(std::move(card));
            }
            continue;
        }
        if ((*marker)->marker == JPEG_APP0 + 2 && text.starts_with(kIccMarkerPrefix) &&
            bytes.size() > kIccMarkerPrefix.size() + 2u)
        {
            // The profile is split across markers, each carrying its own sequence and count after
            // the identifier. What is preserved is that the container has one and how long it is:
            // this engine states no colour management, so carrying the profile's bytes would be
            // carrying something nothing here can honour.
            profileBytes += bytes.size() - kIccMarkerPrefix.size() - 2u;
        }
    }
    if (profileBytes != 0u)
    {
        if (const auto added =
                appendContinuedCard(cards, CodecNamespace::Jpeg, std::string(kIccKeyword), std::to_string(profileBytes),
                                    "bytes of embedded colour profile, preserved as a fact");
            added.failed())
        {
            return added;
        }
    }
    return cards;
}

/// Builds the typed metadata a decoded container states.
///
/// @param codec the preserved cards and source encoding.
/// @param path the source path recorded as lineage.
/// @return the aggregate, or whatever building one of its parts reported.
[[nodiscard]] Result<ImageMetadata> jpegMetadata(CodecMetadata codec, std::string_view path)
{
    const auto projection = projectAcquisition(codec);
    auto acquisition = AcquisitionMetadata::create(projection.acquisition);
    if (!acquisition.hasValue())
    {
        return acquisition.status();
    }
    auto provenance = Provenance::create(
        ProvenanceSourceKind::File,
        path.size() <= kMaxProvenanceTextBytes ? std::string(path) : "JPEG source path exceeds lineage text bound");
    if (!provenance.hasValue())
    {
        return provenance.status();
    }
    // A JPEG container declares no transfer function of its own: the samples are nonlinearly
    // encoded, which the description says, and which encoding is not stated anywhere in the file.
    return ImageMetadata::create(std::move(*acquisition), {}, {}, std::move(*provenance), std::move(codec),
                                 std::nullopt);
}

} // namespace

void JpegReadSessionDeleter::operator()(JpegReadSession* session) const noexcept
{
    if (session == nullptr)
    {
        return;
    }
    if (session->created)
    {
        jpeg_destroy_decompress(&session->info);
    }
    if (session->file != -1)
    {
#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(session->file));
#else
        ::close(static_cast<int>(session->file));
#endif
    }
    delete session;
}

Status jpegFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                   std::string_view message)
{
    return codecFailure(
        code, reason, CodecFormat::Jpeg, operation, path,
        Status::failure(
            code, {.message = std::string(message), .operation = std::string(operation), .domain = ErrorDomain::Io}));
}

bool jpegReadHeader(JpegReadSession* session, bool saveMetadata) noexcept
{
    if (setjmp(session->escape) != 0)
    {
        return false;
    }
    jpeg_create_decompress(&session->info);
    session->created = true;
    session->info.src = &session->source;
    if (saveMetadata)
    {
        jpeg_save_markers(&session->info, JPEG_COM, 0xffffu);
        jpeg_save_markers(&session->info, JPEG_APP0 + 1, 0xffffu);
        jpeg_save_markers(&session->info, JPEG_APP0 + 2, 0xffffu);
    }
    static_cast<void>(jpeg_read_header(&session->info, TRUE));
    jpeg_calc_output_dimensions(&session->info);
    return true;
}

bool jpegDecodeRows(JpegReadSession* session, const JpegDecodeTarget* target) noexcept
{
    if (setjmp(session->escape) != 0)
    {
        return false;
    }
    // An unscaled, unsubsampled grey plane already has the library's raw component layout.
    // Complete DCT blocks can be decoded straight into it, avoiding the intermediate strip
    // and postprocessor copy. Odd extents, colour, and lossless (one-sample units) use scanlines.
    const bool directBlocks = session->info.num_components == 1 && session->info.jpeg_color_space == JCS_GRAYSCALE &&
                              session->info.out_color_space == JCS_GRAYSCALE &&
                              session->info.comp_info[0].h_samp_factor == 1 &&
                              session->info.comp_info[0].v_samp_factor == 1 &&
#if JPEG_LIB_VERSION >= 70
                              session->info.comp_info[0].DCT_h_scaled_size == DCTSIZE &&
                              session->info.comp_info[0].DCT_v_scaled_size == DCTSIZE &&
#else
                              session->info.comp_info[0].DCT_scaled_size == DCTSIZE &&
#endif
                              session->info.output_width % DCTSIZE == 0u && session->info.output_height % DCTSIZE == 0u;
    session->info.raw_data_out = directBlocks;
    static_cast<void>(jpeg_start_decompress(&session->info));
    session->started = true;
    while (session->info.output_scanline < session->info.output_height)
    {
        // Feed a bounded group of final destination rows to the library. Its upsampler can
        // finish several rows per call, with cancellation still checked between small groups.
        JSAMPROW rows[kJpegRowBatch];
        const auto remaining = session->info.output_height - session->info.output_scanline;
        const auto bounded =
            static_cast<JDIMENSION>((std::max)(std::uint64_t{1u}, kJpegScanlineChunkBytes / target->rowStrideBytes));
        auto count = (std::min)({remaining, kJpegRowBatch, bounded});
        if (directBlocks)
            count -= count % DCTSIZE;
        for (JDIMENSION i = 0u; i < count; ++i)
            rows[i] = reinterpret_cast<JSAMPROW>(target->bytes +
                                                 (session->info.output_scanline + i) * target->rowStrideBytes);
        JDIMENSION received = 0u;
        while (received < count)
        {
            JSAMPARRAY plane = rows + received;
            const auto got = directBlocks ? jpeg_read_raw_data(&session->info, &plane, count - received)
                                          : jpeg_read_scanlines(&session->info, plane, count - received);
            if (got == 0u)
                return false;
            received += got;
        }
        if (target->step != nullptr && !target->step(target->user, session->info.output_scanline))
        {
            session->stopped = true;
            return false;
        }
    }
    static_cast<void>(jpeg_finish_decompress(&session->info));
    return true;
}

Result<JpegReadHandle> openJpeg(std::string_view path, bool readMetadata)
{
    auto opened = evaluateAcrossBoundary(kJpegBoundary, [&]() -> JpegReadHandle {
        JpegReadHandle session{new JpegReadSession};
        const std::filesystem::path source(
            std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
#ifdef _WIN32
        // Pull bytes directly into the library's input buffer. Keep this source stable against
        // in-place writes while permitting another transaction to replace its directory entry.
        session->file = reinterpret_cast<std::intptr_t>(CreateFileW(source.c_str(), GENERIC_READ,
                                                                    FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                                                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
#else
        session->file = ::open(source.c_str(), O_RDONLY | O_CLOEXEC);
#endif
        return session;
    });
    if (!opened.hasValue())
    {
        return codecFailure(opened.status().code(), CodecFailure::MetadataUnreadable, CodecFormat::Jpeg, "open JPEG",
                            path, opened.status());
    }
    JpegReadHandle session = std::move(*opened);
    if (session->file == -1)
    {
        return jpegFailure(ErrorCode::ExternalFailure, CodecFailure::MetadataUnreadable, "open JPEG", path,
                           "the file could not be opened for reading");
    }
    if (readMetadata && scanSavedMarkers(*session) == MarkerScanResult::ExceedsLimit)
    {
        return jpegFailure(ErrorCode::LimitExceeded, CodecFailure::MetadataUnreadable, "read JPEG header", path,
                           "the retained JPEG metadata markers exceed the count or byte limit");
    }
    session->info.err = jpeg_std_error(&session->error);
    session->error.error_exit = &jpegReadErrorExit;
    session->error.output_message = &jpegSilentMessage;
    session->info.client_data = session.get();
    session->source.init_source = &jpegInitSource;
    session->source.fill_input_buffer = &jpegFillInput;
    session->source.skip_input_data = &jpegSkipInput;
    session->source.resync_to_restart = jpeg_resync_to_restart;
    session->source.term_source = &jpegTermSource;
    session->source.bytes_in_buffer = 0u;
    session->source.next_input_byte = nullptr;
    if (!jpegReadHeader(session.get(), readMetadata))
    {
        return jpegFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, "read JPEG header", path,
                           session->message);
    }
    if (session->truncated)
    {
        return jpegFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, "read JPEG header", path,
                           "the container ends before its header does");
    }
    return session;
}

Result<CodecImageInfo> describeJpeg(JpegReadSession& session, const EngineContext& engine,
                                    const CodecReadRequest& request, const JpegSoftwareMetadata& software)
{
    if (session.info.data_precision != 8)
    {
        return jpegFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, "describe JPEG image",
                           request.path, "this container states a sample precision other than eight bits");
    }
    if (session.info.out_color_space != JCS_GRAYSCALE && session.info.out_color_space != JCS_RGB)
    {
        return jpegFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, "describe JPEG image",
                           request.path,
                           "this container states components this engine's image model does not describe");
    }
    const auto limits = engine.limits();
    const auto channels = static_cast<std::uint32_t>(session.info.out_color_components);
    const auto extent = ImageExtent::create(static_cast<std::uint32_t>(session.info.output_width),
                                            static_cast<std::uint32_t>(session.info.output_height), channels, limits);
    if (!extent)
    {
        return Status::failure(ErrorCode::LimitExceeded);
    }
    const auto descriptor =
        RawFrameDescriptor::create({.extent = *extent,
                                    .sampleFormat = {1u, RawSampleClass::UnsignedInteger},
                                    .byteOrder = ByteOrder::Native,
                                    .channelSchema = channels == 1u ? ChannelSchema::Mono : ChannelSchema::Rgb,
                                    .colorSpace = ColorSpace::Unspecified,
                                    .numericDomain = NumericDomain::EncodedNonlinear,
                                    .cfa = {},
                                    .coordinateOrigin = CoordinateOrigin::TopLeft,
                                    .pixelAspectRatio = 1.0});
    if (!descriptor)
    {
        return Status::failure(ErrorCode::InvalidData);
    }
    const auto layout = ImageLayout::create({.extent = *extent,
                                             .elementSizeBytes = 1u,
                                             .samplesPerPixel = channels,
                                             .planeCount = 1u,
                                             .rowAlignmentBytes = 1u},
                                            limits);
    if (!layout)
    {
        return Status::failure(ErrorCode::LimitExceeded);
    }
    std::vector<CodecCard> cards;
    if (request.readMetadata)
    {
        auto read = jpegCards(session, software);
        if (!read.hasValue())
        {
            return read.status();
        }
        cards = std::move(*read);
    }
    auto codec =
        CodecMetadata::create(SourceEncoding{.sampleFormat = RawSampleFormat{1u, RawSampleClass::UnsignedInteger},
                                             .byteOrder = ByteOrder::Native},
                              std::move(cards));
    if (!codec.hasValue())
    {
        return codec.status();
    }
    ImageMetadata metadata;
    if (request.readMetadata)
    {
        auto projected = jpegMetadata(std::move(*codec), request.path);
        if (!projected.hasValue())
        {
            return projected.status();
        }
        metadata = std::move(*projected);
    }
    else
    {
        metadata = ImageMetadata::create({}, {}, {}, {}, std::move(*codec), std::nullopt);
    }
    // The library validates the payload only by decoding it, so a probe says what it actually
    // established rather than claiming a check it did not make.
    return CodecImageInfo{*descriptor, *layout, 1u, std::move(metadata), CodecProbeDisposition::DecodeRequired};
}

Result<CodecDecodeResult> readJpeg(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, JpegReadSession& session, CodecImageInfo info)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    auto buffer = SampleBuffer::createOwned(engine, info.decodedLayout, StorageInitialization::Uninitialized);
    if (!buffer.hasValue())
    {
        return buffer.status();
    }
    auto frame = RawFrame::create(info.descriptor, info.decodedLayout, std::move(*buffer));
    if (!frame.hasValue())
    {
        return frame.status();
    }
    auto bytes = *frame->mutableBytes();
    const auto limits = engine.limits();
    const std::uint64_t liveBytes = engine.memoryResource()->counters().liveBytes;
    const std::uint64_t availableBytes =
        liveBytes < limits.memoryBudgetBytes ? limits.memoryBudgetBytes - liveBytes : 0u;
    const std::uint64_t libraryLimit =
        (std::min)(availableBytes, static_cast<std::uint64_t>((std::numeric_limits<long>::max)()));
    // Zero means unlimited to libjpeg-turbo. One instead means that a progressive image which needs
    // a virtual whole-image array is refused when the caller's budget has no room left for it.
    session.info.mem->max_memory_to_use = static_cast<long>((std::max)(libraryLimit, std::uint64_t{1u}));
    RowProgress progress{&execution, info.descriptor.extent().height()};
    const JpegDecodeTarget target{bytes.data(), info.decodedLayout.rowStrideBytes(), &progress, &jpegRowStep};
    const bool decoded = jpegDecodeRows(&session, &target);
    if (session.stopped)
    {
        const auto stopped = execution.checkCancellation();
        return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
    }
    if (session.truncated)
    {
        return jpegFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, "decode JPEG pixels", request.path,
                           "the payload ends before the geometry the header declares");
    }
    if (!decoded)
    {
        return jpegFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, "decode JPEG pixels", request.path,
                           session.message);
    }
    return CodecDecodeResult{std::move(*frame), std::move(info.metadata)};
}

} // namespace an
