// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <jpeglib.h>

#include <csetjmp>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// AfterNightEngine - the JPEG boundary, and the shape a long jump forces it into.
//
// This library reports a failure by calling a handler that must not return, which in C means
// `longjmp`. A jump like that skips the destructors of everything between it and its `setjmp`, so
// every function here that calls into the library holds nothing but plain data: the session lives
// on the heap and is owned by a frame the jump never crosses, and the work that needs C++ objects -
// preparing a row, checking cancellation, reporting progress - happens in functions the library
// calls back into, whose own frames are complete before anything can jump out of them.

namespace an
{
class EngineContext;
class ExecutionContext;
class CodecOutput;

/// Where every failure this adapter translates says it came from.
inline constexpr ExternalBoundary kJpegBoundary{"libjpeg-turbo", "JPEG codec", ErrorDomain::Io};

/// The bytes one read from or write to the file moves, 12 KiB. The buffer lives inside the session,
/// and this size keeps a session out of Windows' larger heap-allocation classes.
inline constexpr std::size_t kJpegBufferBytes = 12288u;
/// The most rows one scanline or raw-block call is handed; `kJpegScanlineChunkBytes` bounds it too.
inline constexpr std::uint32_t kJpegRowBatch = 64u;
/// The most sample bytes one scanline or raw-block call is handed, one MiB, so a wide image is
/// decoded and encoded in fewer rows per call; one row is always allowed.
inline constexpr std::uint64_t kJpegScanlineChunkBytes = 1048576u;

/// Immutable library-produced encoding of the writer's software stamp, owned by one adapter.
/// The decoded cards are also supplied by the library, so recognition is an exact byte match.
struct JpegSoftwareMetadata
{
    std::vector<std::byte> marker; ///< Complete Exif marker payload, including its identifier.
    std::vector<CodecCard> cards; ///< The cards obtained by decoding exactly those bytes.
};

/// Prepares the invariant stamp once, without retaining any process-wide engine state.
Result<JpegSoftwareMetadata> prepareJpegSoftwareMetadata();

/// One open source: the library's decoder, its native file, and the escape a failure takes.
///
/// Heap-allocated and owned by a frame no long jump crosses, so its members and source handle
/// are released in the ordinary way.
struct JpegReadSession
{
    jpeg_decompress_struct info{}; ///< The library's decoder state.
    jpeg_source_mgr source{}; ///< The callbacks that pull from the stream below.
    jpeg_error_mgr error{}; ///< The handler that reports a refusal by jumping.
    std::jmp_buf escape{}; ///< Where a refusal lands.
    std::intptr_t file = -1; ///< Native read-only source, closed by the session deleter.
    char message[JMSG_LENGTH_MAX]{}; ///< What the library said about a refusal.
    unsigned char buffer[kJpegBufferBytes]; ///< Uninitialized scratch; a native read defines the valid prefix.
    bool created = false; ///< Whether the decoder needs destroying.
    bool started = false; ///< Whether decompression began.
    bool truncated = false; ///< Whether the payload ended before the container said it would.
    bool stopped = false; ///< Whether the caller's cancellation ended the decode.
};

/// Releases a session's decoder before the session itself.
struct JpegReadSessionDeleter
{
    /// @param session the session to close and release; null is a no-op.
    void operator()(JpegReadSession* session) const noexcept;
};

/// An open source, closed when the last owner releases it.
using JpegReadHandle = std::unique_ptr<JpegReadSession, JpegReadSessionDeleter>;

/// Where one decoded row goes, and what runs between rows.
struct JpegDecodeTarget
{
    std::byte* bytes = nullptr; ///< The first row's first byte.
    std::uint64_t rowStrideBytes = 0u; ///< The distance between two rows.
    void* user = nullptr; ///< What `step` is called with.
    /// Runs after each bounded group of rows; false stops the decode without finishing it.
    bool (*step)(void* user, std::uint32_t completedRows) = nullptr;
};

/// One marker a written container carries, complete but for its own length field.
struct JpegMarker
{
    int code = 0; ///< The marker code, as the library numbers them.
    const std::byte* bytes = nullptr; ///< The marker's payload.
    std::size_t size = 0u; ///< How many bytes of it there are.
};

/// What one encode is handed: the geometry, the options, and where each row comes from.
struct JpegEncodeSource
{
    std::uint32_t width = 0u; ///< Columns.
    std::uint32_t height = 0u; ///< Rows.
    std::uint32_t channels = 0u; ///< One for grey, three for red-green-blue.
    int quality = 95; ///< The library's quality scale, one to a hundred.
    const JpegMarker* markers = nullptr; ///< The markers to write, in order.
    std::size_t markerCount = 0u; ///< How many there are.
    void* user = nullptr; ///< What `rows` is called with.
    /// The most rows one call to `rows` supplies: one when a planar frame is interleaved through its
    /// one-row workspace, otherwise at most `kJpegRowBatch` rows and `kJpegScanlineChunkBytes` bytes.
    std::uint32_t rowBatch = 1u;
    /// Supplies rows of interleaved samples; false stops the encode without finishing it.
    bool (*rows)(void* user, std::uint32_t index, std::uint32_t count, JSAMPROW* rows) = nullptr;
};

/// One pending output: the library's encoder, the stream it fills, and the escape a failure takes.
struct JpegWriteSession
{
    jpeg_compress_struct info{}; ///< The library's encoder state.
    jpeg_destination_mgr destination{}; ///< The callbacks that push into the stream below.
    jpeg_error_mgr error{}; ///< The handler that reports a refusal by jumping.
    std::jmp_buf escape{}; ///< Where a refusal lands.
    std::optional<std::ofstream> stream; ///< Constructed only for a direct path-based write.
    CodecOutput* output = nullptr; ///< Borrowed transaction output, or null for the stream.
    char message[JMSG_LENGTH_MAX]{}; ///< What the library said about a refusal.
    unsigned char buffer[kJpegBufferBytes]; ///< Uninitialized scratch; libjpeg fills every byte passed to the output.
    std::uint64_t bytesWritten = 0u; ///< Bytes accepted by the stream, verified by close and the output transaction.
    bool created = false; ///< Whether the encoder needs destroying.
    bool failed = false; ///< Whether a write to the stream failed.
    bool stopped = false; ///< Whether the caller's cancellation ended the encode.
};

/// Releases a session's encoder before the session itself.
struct JpegWriteSessionDeleter
{
    /// @param session the session to close and release; null is a no-op.
    void operator()(JpegWriteSession* session) const noexcept;
};

/// A pending output, closed when the last owner releases it.
using JpegWriteHandle = std::unique_ptr<JpegWriteSession, JpegWriteSessionDeleter>;

/// Translates a refusal this adapter decided the meaning of.
/// @param code the engine classification this adapter chose.
/// @param reason the codec-specific refinement.
/// @param operation the entry point being performed.
/// @param path the file the refusal is about.
/// @param message what to tell a person.
/// @return a failure in ErrorDomain::Io carrying the path and the reason.
Status jpegFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                   std::string_view message);

/// Opens a container and reads its header, without decoding a pixel.
///
/// When metadata is wanted, the comment, Exif and colour-profile markers are counted by a scan of
/// the raw file before the library is asked to keep any, so an oversized set is refused unallocated.
/// @param path literal UTF-8 disk path.
/// @param readMetadata whether metadata markers should be retained and decoded.
/// @return the open session, or a failure with `CodecFailure::MetadataUnreadable`:
///         `ErrorCode::ExternalFailure` when the file cannot be opened, `ErrorCode::LimitExceeded`
///         when the retained markers exceed the codec model's card count or bytes, and
///         `ErrorCode::InvalidData` when the library refuses the header or the file ends inside it.
Result<JpegReadHandle> openJpeg(std::string_view path, bool readMetadata);

/// Describes the image an open session's header states.
/// @param session the open session.
/// @param engine context supplying the geometry limits.
/// @param request the source selection, including whether metadata is wanted.
/// @param software the adapter's immutable software stamp encoding.
/// @return the checked description, which always says the payload is validated only by decoding
///         it; `ErrorCode::Unsupported` with `CodecFailure::UnsupportedRepresentation` for a
///         precision other than eight bits or components other than grey or red-green-blue;
///         `ErrorCode::LimitExceeded` for a geometry or a card count over the limits;
///         `ErrorCode::InvalidData` for a description the frame model refuses; or what reading the
///         Exif or building the metadata reported.
Result<CodecImageInfo> describeJpeg(JpegReadSession& session, const EngineContext& engine,
                                    const CodecReadRequest& request, const JpegSoftwareMetadata& software);

/// Decodes the complete image through a session whose header has already been described.
///
/// The library's own working memory is held to what remains of the engine context's budget.
/// @param engine owner of the decoded allocation.
/// @param execution the operation policy and sinks.
/// @param request the source selection.
/// @param session the open session.
/// @param info the description `describeJpeg` produced, consumed by this call.
/// @return the complete frame and its source metadata; the cancellation's own failure when the
///         caller stops the decode; the budget's refusal of the frame; or `ErrorCode::InvalidData`
///         with `CodecFailure::PayloadUnreadable` when the payload is truncated or refused.
Result<CodecDecodeResult> readJpeg(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, JpegReadSession& session, CodecImageInfo info);

/// Serializes prepared samples into the request's pending path, or into `output` when one is given.
///
/// Every refusal of the frame or the intent is decided before the output is opened.
/// @param engine owner of the encoder workspace.
/// @param execution the operation policy and sinks.
/// @param request the pending target and explicit intent.
/// @param frame the complete described samples.
/// @param metadata the immutable source facts.
/// @param software the adapter's immutable software stamp encoding.
/// @param output optional transaction-owned output; the transaction closes it before publication.
/// @return the count of bytes the output accepted; `ErrorCode::Unsupported` for a frame other
///         than top-down, unmosaiced eight-bit grey or three-channel colour at most 65,535 pixels a
///         side, for an intent that does not state the JPEG compression or states a scale, and for
///         a card that is an artifact identity or neither Exif nor the comment;
///         `ErrorCode::LimitExceeded` when the Exif block or the comment exceeds one marker; the
///         budget's refusal of a planar frame's row workspace; the cancellation's own failure; or
///         `ErrorCode::ExternalFailure` when the output cannot be opened, the library refuses the
///         encode, or a write falls short.
Result<CodecEncodeReport> writeJpeg(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata, const JpegSoftwareMetadata& software,
                                    CodecOutput* output);

/// Reads the container's header. Plain data only: a refusal jumps out of this frame.
/// @param session the session to open the header of.
/// @param saveMetadata whether the bounded metadata marker types should be retained.
/// @return whether the header was read.
bool jpegReadHeader(JpegReadSession* session, bool saveMetadata) noexcept;

/// Decodes every row into the target. Plain data only: a refusal jumps out of this frame.
/// @param session the session whose header has been read.
/// @param target where the rows go and what runs between them.
/// @return whether every row was decoded.
bool jpegDecodeRows(JpegReadSession* session, const JpegDecodeTarget* target) noexcept;

/// Writes the complete container. Plain data only: a refusal jumps out of this frame.
/// @param session the session holding the pending payload.
/// @param source the geometry, options, markers and rows.
/// @return whether the container was written.
bool jpegWriteImage(JpegWriteSession* session, const JpegEncodeSource* source) noexcept;

} // namespace an
