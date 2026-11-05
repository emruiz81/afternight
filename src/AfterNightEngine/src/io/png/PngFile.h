// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/PngCodec.h>

#include <png.h>

#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// AfterNightEngine - the PNG boundary, and the shape a long jump forces it into.
//
// This library reports a failure the way the JPEG one does, by calling a handler that must not
// return, so the same discipline applies: every function that calls into the library holds nothing
// but plain data, the session lives on the heap in a frame no jump crosses, and the work needing
// C++ objects happens in callbacks whose frames are complete before anything can jump out of them.
//
// What this container adds is that its colour declaration is part of its header rather than of its
// metadata. An sRGB chunk, a gamma and an embedded profile decide what the samples mean, so they
// are read where the geometry is read and written where the geometry is written - which is what
// keeps a probe and a decode describing one image.

namespace an
{
class EngineContext;
class ExecutionContext;

/// Where every failure this adapter translates says it came from.
inline constexpr ExternalBoundary kPngBoundary{"libpng", "PNG codec", ErrorDomain::Io};

/// The longest message this adapter keeps from the library.
inline constexpr std::size_t kPngMessageBytes = 256u;

/// The keyword an embedded colour profile's presence is preserved under.
inline constexpr std::string_view kPngIccKeyword = "iCCP";

/// The keyword a declared gamma is preserved under.
inline constexpr std::string_view kPngGammaKeyword = "gAMA";

/// The keyword a declared standard colour space is preserved under.
inline constexpr std::string_view kPngSrgbKeyword = "sRGB";

/// The keyword the significant bits a file declares are preserved under.
inline constexpr std::string_view kPngSignificantBitsKeyword = "sBIT";

/// The keyword a sub-byte source bit depth is preserved under.
inline constexpr std::string_view kPngBitDepthKeyword = "BitDepth";

/// The keyword the colour type a file states is preserved under.
inline constexpr std::string_view kPngColorTypeKeyword = "ColorType";

/// The text keyword a writer that packs an Exif block into a text entry uses.
inline constexpr std::string_view kPngRawExifKeyword = "Raw profile type exif";

/// The other spelling of that same packing.
inline constexpr std::string_view kPngRawApp1Keyword = "Raw profile type APP1";

/// What a container declares about the light its samples stand for.
struct PngColorDeclaration
{
    bool profile = false; ///< Whether an embedded colour profile decides it.
    bool standard = false; ///< Whether the container declares the standard colour space.
    bool gamma = false; ///< Whether the container declares a gamma.
    double fileGamma = 1.0; ///< The gamma it declares, as the file states it.
};

/// One open source: the library's reader, the stream behind it, and what its header states.
struct PngReadSession
{
    png_structp png = nullptr; ///< The library's reader.
    png_infop info = nullptr; ///< What the header states.
    png_infop endInfo = nullptr; ///< What the chunks after the payload state.
    std::ifstream stream; ///< The container, opened from a literal UTF-8 path.
    char message[kPngMessageBytes]{}; ///< What the library said about a refusal.
    std::uint32_t width = 0u; ///< Columns.
    std::uint32_t height = 0u; ///< Rows.
    int bitDepth = 0; ///< Bits per sample after the header's own expansions.
    int colorType = 0; ///< The colour type after those expansions.
    int channels = 0; ///< Samples per pixel after them.
    int sourceBitDepth = 0; ///< Bits per sample as the file states them.
    int sourceColorType = 0; ///< The colour type as the file states it.
    int passes = 1; ///< How many passes an interlaced payload is read in.
    bool truncated = false; ///< Whether the payload ended before the container said it would.
    bool stopped = false; ///< Whether the caller's cancellation ended the decode.
};

/// Releases a session's reader before the session itself.
struct PngReadSessionDeleter
{
    /// @param session the session to close and release; null is a no-op.
    void operator()(PngReadSession* session) const noexcept;
};

/// An open source, closed when the last owner releases it.
using PngReadHandle = std::unique_ptr<PngReadSession, PngReadSessionDeleter>;

/// Where the decoded rows go, and what runs between them.
struct PngDecodeTarget
{
    std::byte* bytes = nullptr; ///< The first row's first byte.
    std::uint64_t rowStrideBytes = 0u; ///< The distance between two rows.
    void* user = nullptr; ///< What `step` is called with.
    /// Runs after each row, with the rows read so far counted across every pass of an interlaced
    /// payload; false stops the decode without finishing it.
    bool (*step)(void* user, std::uint64_t completedRows) = nullptr;
};

/// What one encode is handed: the geometry, the declarations, and where each row comes from.
struct PngEncodeSource
{
    std::uint32_t width = 0u; ///< Columns.
    std::uint32_t height = 0u; ///< Rows.
    int bitDepth = 8; ///< Eight or sixteen.
    int colorType = 0; ///< The colour type, as the library numbers them.
    int compressionLevel = 6; ///< The deflate level, zero to nine.
    bool swapBytes = false; ///< Whether sixteen-bit samples are swapped on the way out.
    bool standardColorSpace = false; ///< Whether to declare the standard colour space.
    bool declaresGamma = false; ///< Whether to declare a gamma.
    double fileGamma = 1.0; ///< The gamma to declare, as a file states one.
    png_const_textp text = nullptr; ///< The text entries to write.
    int textCount = 0; ///< How many there are.
    const std::byte* exif = nullptr; ///< The Exif block to write, or null.
    std::size_t exifBytes = 0u; ///< How long it is.
    void* user = nullptr; ///< What `row` is called with.
    /// Supplies one row of samples; null stops the encode without finishing it.
    png_bytep (*row)(void* user, std::uint32_t index) = nullptr;
};

/// One pending output: the library's writer, the stream it fills, and what a refusal said.
struct PngWriteSession
{
    png_structp png = nullptr; ///< The library's writer.
    png_infop info = nullptr; ///< What its header will state.
    std::ofstream stream; ///< The pending payload.
    char message[kPngMessageBytes]{}; ///< What the library said about a refusal.
    bool failed = false; ///< Whether a write to the stream failed.
    bool stopped = false; ///< Whether the caller's cancellation ended the encode.
};

/// Releases a session's writer before the session itself.
struct PngWriteSessionDeleter
{
    /// @param session the session to close and release; null is a no-op.
    void operator()(PngWriteSession* session) const noexcept;
};

/// A pending output, closed when the last owner releases it.
using PngWriteHandle = std::unique_ptr<PngWriteSession, PngWriteSessionDeleter>;

/// Translates a refusal this adapter decided the meaning of.
/// @param code the engine classification this adapter chose.
/// @param reason the codec-specific refinement.
/// @param operation the entry point being performed.
/// @param path the file the refusal is about.
/// @param message what to tell a person.
/// @return a failure in ErrorDomain::Io carrying the path and the reason.
Status pngFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                  std::string_view message);

/// Opens a container and reads its header, without decoding a pixel.
/// @param engine context supplying the geometry limits the reader is bounded by.
/// @param path literal UTF-8 disk path.
/// @return the open session; `ErrorCode::ExternalFailure` when the file cannot be opened;
///         `ErrorCode::AllocationFailed` when the library cannot create its structures; or
///         `ErrorCode::InvalidData` with `CodecFailure::MetadataUnreadable` when the library
///         refuses the header - which includes a width or height over the context's limits, since
///         the reader is bounded by them before the header is read.
Result<PngReadHandle> openPng(const EngineContext& engine, std::string_view path);

/// Describes the image an open session's header states.
/// @param session the open session.
/// @param engine context supplying the geometry limits.
/// @param request the source selection, including whether metadata is wanted.
/// @return the checked description, which always says the payload is validated only by decoding
///         it; `ErrorCode::Unsupported` with `CodecFailure::UnsupportedRepresentation` for a depth
///         other than eight or sixteen bits or a channel count outside one to four after the
///         header's expansions; `ErrorCode::LimitExceeded` for a geometry over the limits;
///         `ErrorCode::InvalidData` for a description the frame model refuses; or what reading the
///         chunks or building the metadata reported.
Result<CodecImageInfo> describePng(PngReadSession& session, const EngineContext& engine,
                                   const CodecReadRequest& request);

/// Decodes the complete image through a session whose header has already been described.
///
/// When metadata is wanted, the cards are read again once the payload has been passed, so the text
/// a writer placed after the image data is included - which a probe cannot reach.
/// @param engine owner of the decoded allocation.
/// @param execution the operation policy and sinks.
/// @param request the source selection.
/// @param session the open session.
/// @param info the description `describePng` produced, consumed by this call.
/// @return the complete frame and its source metadata; the cancellation's own failure when the
///         caller stops the decode; the budget's refusal of the frame; `ErrorCode::InvalidData`
///         with `CodecFailure::PayloadUnreadable` when the payload is truncated or refused; or what
///         reading the trailing chunks reported.
Result<CodecDecodeResult> readPng(const EngineContext& engine, const ExecutionContext& execution,
                                  const CodecReadRequest& request, PngReadSession& session, CodecImageInfo info);

/// Reads every card the container's chunks state.
/// @param session the open session.
/// @param includeEnd whether the chunks after the payload are included.
/// @return the cards, or a typed limit or metadata failure.
Result<std::vector<CodecCard>> pngCards(const PngReadSession& session, bool includeEnd);

/// Builds the typed metadata a decoded container states.
/// @param cards the preserved cards.
/// @param declaration what the container declares about its colour.
/// @param sampleFormat the representation the file states.
/// @param path the source path recorded as lineage.
/// @return the aggregate, or whatever building one of its parts reported.
Result<ImageMetadata> pngMetadata(std::vector<CodecCard> cards, const PngColorDeclaration& declaration,
                                  const RawSampleFormat& sampleFormat, std::string_view path);

/// What a container's colour declaration says the samples are.
/// @param declaration what the container declares.
/// @param channels how many channels the image has.
/// @return the colour basis, the numeric domain and the transfer function, in that order.
std::tuple<ColorSpace, NumericDomain, TransferFunction> pngColorMeaning(const PngColorDeclaration& declaration,
                                                                        std::uint32_t channels) noexcept;

/// Reads what the container declares about the light its samples stand for.
/// @param session the open session.
/// @return the declaration.
PngColorDeclaration pngColorDeclaration(const PngReadSession& session) noexcept;

/// Serializes prepared samples into the request's private pending path.
///
/// Every refusal of the frame or the intent is decided before the pending file is opened.
/// @param engine owner of the encoder workspace.
/// @param execution the operation policy and sinks.
/// @param request the pending target and explicit intent.
/// @param frame the complete described samples.
/// @param metadata the immutable source facts.
/// @return the finished file's size; `ErrorCode::Unsupported` for samples other than unsigned eight
///         or sixteen bits, a channel schema no colour type states, a mosaic, a bottom-up frame, a
///         compression other than none or deflate, an intent stating a scale or a little-endian
///         file, or an intent card that is neither Exif nor a valid text keyword; the budget's
///         refusal of a planar frame's row workspace; the cancellation's own failure;
///         `ErrorCode::AllocationFailed` when the library cannot create its structures; or
///         `ErrorCode::ExternalFailure` when the output cannot be opened, the library refuses the
///         encode, or a write falls short.
Result<CodecEncodeReport> writePng(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecEncodeRequest& request, const RawFrame& frame,
                                   const ImageMetadata& metadata);

/// Reads the container's header and states its transformations. Plain data only: a refusal jumps
/// out of this frame.
/// @param session the session to open the header of.
/// @param maxWidth the widest image this context admits.
/// @param maxHeight the tallest image this context admits.
/// @return whether the header was read.
bool pngReadHeader(PngReadSession* session, std::uint32_t maxWidth, std::uint32_t maxHeight) noexcept;

/// Decodes every row into the target. Plain data only: a refusal jumps out of this frame.
/// @param session the session whose header has been read.
/// @param target where the rows go and what runs between them.
/// @return whether every row was decoded.
bool pngDecodeRows(PngReadSession* session, const PngDecodeTarget* target) noexcept;

/// Writes the complete container. Plain data only: a refusal jumps out of this frame.
/// @param session the session holding the pending payload.
/// @param source the geometry, declarations, text and rows.
/// @return whether the container was written.
bool pngWriteImage(PngWriteSession* session, const PngEncodeSource* source) noexcept;

} // namespace an
