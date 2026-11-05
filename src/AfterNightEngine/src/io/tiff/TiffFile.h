// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>

#include <tiffio.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <fstream>
#endif

// AfterNightEngine - the TIFF boundary, and the two process-wide things it does not touch.
//
// This library reports a failure by calling a handler and returning a code, which is the civilised
// half of what the three raster libraries do - no long jump, and nothing to unwind. What it has
// instead is two pieces of process state: a pair of handlers installed once for the whole
// program, and a file interface that opens paths in the platform's narrow
// encoding. Both are avoided here. A handler belongs to the open container, so two operations in
// one process cannot take each other's diagnostics, and the container is a file this adapter opens
// itself, so a literal UTF-8 path reaches the file system unchanged on both platforms.
//
// On Windows that file is a native handle rather than a stream: the library moves a whole strip per
// call, and MSVC's file stream splits every read longer than 4,095 bytes into calls of that length,
// each a system call, which made reading a strip cost ten times what one transfer does. Elsewhere it
// is a stream, whose library hands a long read to the operating system whole.

namespace an
{
class EngineContext;
class ExecutionContext;

/// Where every failure this adapter translates says it came from.
inline constexpr ExternalBoundary kTiffBoundary{"libtiff", "TIFF codec", ErrorDomain::Io};

/// The longest message this adapter keeps from the library.
inline constexpr std::size_t kTiffMessageBytes = 512u;

/// One open container: the library's handle, the file under it, and what its handlers said.
///
/// The handlers are per-handle, so what they record belongs to this operation and to no other.
struct TiffFile
{
    /// Holds nothing open.
    TiffFile() = default;

    /// Closes the file; the library's handle, when there is one, is closed first by `TiffFileDeleter`.
    ~TiffFile();

    TiffFile(const TiffFile&) = delete;
    TiffFile& operator=(const TiffFile&) = delete;

    TIFF* tiff = nullptr; ///< The library's handle.
#if defined(_WIN32)
    void* handle = nullptr; ///< The container's native file handle, or null while none is open.
#else
    std::fstream stream; ///< The container, opened from a literal UTF-8 path.
#endif
    const ExecutionContext* execution = nullptr; ///< Whose diagnostic sink a warning reaches.
    char message[kTiffMessageBytes]{}; ///< The first refusal the handler recorded.
    std::uint32_t warnings = 0u; ///< How many warnings the container produced.
    bool writing = false; ///< Whether the file was opened for writing as well as reading.
    bool failed = false; ///< Whether a write or a seek through the file failed.
};

/// Closes a container's library handle before the file under it.
struct TiffFileDeleter
{
    /// @param file the container to close and release; null is a no-op.
    void operator()(TiffFile* file) const noexcept;
};

/// An open container, closed when the last owner releases it.
using TiffHandle = std::unique_ptr<TiffFile, TiffFileDeleter>;

/// What one image of a container states about itself, before any of it is decoded.
struct TiffImageFacts
{
    std::uint32_t width = 0u; ///< Columns.
    std::uint32_t height = 0u; ///< Rows.
    std::uint16_t bitsPerSample = 0u; ///< Bits in one sample.
    std::uint16_t samplesPerPixel = 0u; ///< Samples in one pixel.
    std::uint16_t sampleFormat = SAMPLEFORMAT_UINT; ///< What kind of value a sample holds.
    std::uint16_t photometric = 0u; ///< What the samples mean.
    std::uint16_t planarConfig = PLANARCONFIG_CONTIG; ///< Interleaved (`CONTIG`) or separate planes.
    std::uint16_t orientation = ORIENTATION_TOPLEFT; ///< Where the first row and column are.
    std::uint16_t compression = COMPRESSION_NONE; ///< How the payload is compressed.
    std::uint16_t extraSamples = 0u; ///< How many samples past the photometric ones there are.
    std::uint16_t firstExtraSample = EXTRASAMPLE_UNSPECIFIED; ///< What the first of them is.
    bool associatedAlpha = false; ///< Whether any extra channel claims premultiplied colour samples.
    bool tiled = false; ///< Whether the payload is stored in tiles rather than strips.
    bool profile = false; ///< Whether the image carries an embedded colour profile.
};

/// Keeps the first refusal the library reports for one container; later ones are dropped.
/// @param tiff the library's handle; unused, because `user` already names the container.
/// @param user the container the handler belongs to.
/// @param module the library routine that failed; unused.
/// @param format what the library wants to say.
/// @param arguments its arguments.
/// @return one, so the process-wide handler this engine never installs is not consulted.
int tiffErrorHandler(TIFF* tiff, void* user, const char* module, const char* format, va_list arguments);

/// Counts a warning and reports it through the operation's own diagnostic sink.
/// @param tiff the library's handle; unused, because `user` already names the container.
/// @param user the container the handler belongs to.
/// @param module the library routine that warned; unused.
/// @param format what the library wants to say.
/// @param arguments its arguments.
/// @return one, for the reason the error handler returns it.
int tiffWarningHandler(TIFF* tiff, void* user, const char* module, const char* format, va_list arguments);

/// Reads the next bytes of a container out of its file, as one transfer where the platform allows.
/// @param handle the container.
/// @param data where the bytes go.
/// @param size how many are wanted.
/// @return how many were read.
tmsize_t tiffReadProc(thandle_t handle, void* data, tmsize_t size);

/// Writes the next bytes of a container into its file.
/// @param handle the container.
/// @param data the bytes.
/// @param size how many there are.
/// @return how many were written; zero is a failed write.
tmsize_t tiffWriteProc(thandle_t handle, void* data, tmsize_t size);

/// Moves a file's one position, which the library reads and writes through alike.
/// @param handle the container.
/// @param offset where to move to.
/// @param whence what the offset is measured from.
/// @return the new position, or the largest value when the seek failed.
toff_t tiffSeekProc(thandle_t handle, toff_t offset, int whence);

/// Leaves a file to its owner, which closes it after the library's handle.
/// @return zero, which the library reads as success.
int tiffCloseProc(thandle_t handle);

/// How long a container is.
/// @param handle the container.
/// @return its size in bytes.
toff_t tiffSizeProc(thandle_t handle);

/// Refuses to map a container into memory.
/// @return zero, which the library reads as "read it instead".
int tiffMapProc(thandle_t handle, void** base, toff_t* size);

/// Unmaps nothing, because nothing was mapped.
void tiffUnmapProc(thandle_t handle, void* base, toff_t size);

/// Opens the file under a container: an existing one for reading, or one created empty, or emptied,
/// for reading and writing, shared with other readers and writers as a C library stream is.
/// @param file the container whose file this is; `writing` records which.
/// @param path literal UTF-8 disk path.
/// @param writing whether to create the file for writing rather than open it for reading.
/// @return whether the file is open.
[[nodiscard]] bool openTiffFile(TiffFile& file, std::string_view path, bool writing);

/// Reads a whole file of a stated length into memory, as one transfer where the platform allows.
/// @param path literal UTF-8 disk path.
/// @param bytes where the file goes, `size` bytes of it.
/// @param size how long the file is.
/// @return whether the file opened and exactly `size` bytes were read.
[[nodiscard]] bool readTiffContainer(std::string_view path, std::byte* bytes, std::uint64_t size);

/// Hands everything written so far to the operating system and says whether every write reached it.
/// @param file an open container being written.
/// @return false when a write, a seek or this hand-over failed.
[[nodiscard]] bool flushTiffFile(TiffFile& file);

/// Translates a refusal this adapter decided the meaning of.
/// @param code the engine classification this adapter chose.
/// @param reason the codec-specific refinement.
/// @param operation the entry point being performed.
/// @param path the file the refusal is about.
/// @param message what to tell a person.
/// @return a failure in ErrorDomain::Io carrying the path and the reason.
Status tiffFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                   std::string_view message);

/// Opens a container for reading, with handlers and allocation bounds of its own.
///
/// The library's largest single allocation is held to the context's largest image in bytes.
/// @param engine context supplying the allocation bound the library is held to.
/// @param execution the operation whose diagnostic sink a warning reaches.
/// @param path literal UTF-8 disk path.
/// @return the open container; `ErrorCode::ExternalFailure` when the file cannot be opened;
///         `ErrorCode::AllocationFailed` when the library's open options cannot be allocated; or
///         `ErrorCode::InvalidData` with `CodecFailure::MetadataUnreadable` when the library does not
///         accept the file as a TIFF container.
Result<TiffHandle> openTiffRead(const EngineContext& engine, const ExecutionContext& execution, std::string_view path);

/// Selects one image of a container and reads what it states about itself.
/// @param file the open container.
/// @param imageIndex which image, counted from zero in the container's directory order.
/// @param path the file a refusal is about.
/// @return the facts; `ErrorCode::OutOfRange` when the container has no image at that index; or
///         `ErrorCode::InvalidData` when the image states no geometry or no photometric
///         interpretation.
Result<TiffImageFacts> readTiffFacts(TiffFile& file, std::uint32_t imageIndex, std::string_view path);

/// Describes the selected image without materializing its payload.
///
/// A JPEG-compressed luma-and-chroma image is described as red, green and blue, and the handle is
/// told to convert it so the decode obtains exactly that. A bottom-left or bottom-right orientation
/// is a bottom-up description; every other orientation is reported only through its card.
/// @param file the open container.
/// @param engine context supplying the geometry limits.
/// @param execution the operation whose cancellation is checked.
/// @param request the source selection, including whether metadata is wanted.
/// @return the checked description, which always says the payload is validated only by decoding
///         it; `ErrorCode::Unsupported` with `CodecFailure::UnsupportedRepresentation` for a
///         photometric interpretation other than black-is-zero grey, red-green-blue or
///         JPEG-compressed luma and chroma, a sample representation this engine does not carry, or
///         associated alpha; `ErrorCode::LimitExceeded` for a geometry over the limits;
///         `ErrorCode::InvalidData` for a description the frame model refuses; or what selecting
///         the image or reading its metadata reported.
Result<CodecImageInfo> describeTiff(TiffFile& file, const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecReadRequest& request);

/// Decodes the complete selected image through a container already described.
///
/// Strips are read a row at a time straight into the frame; tiles are read through one counted tile
/// buffer and clipped where they overhang the image. Cancellation is checked before every row or
/// tile.
/// @param engine owner of the decoded allocation.
/// @param execution the operation policy and sinks.
/// @param request the source selection.
/// @param file the open container.
/// @param info the description `describeTiff` produced, consumed by this call.
/// @return the complete frame and its source metadata; the cancellation's own failure; the
///         budget's refusal of the frame or the tile buffer; or `ErrorCode::InvalidData` with
///         `CodecFailure::PayloadUnreadable` when a tile's size is not stated or a row or tile does
///         not decode.
Result<CodecDecodeResult> readTiff(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, TiffFile& file, CodecImageInfo info);

/// Serializes prepared samples into the request's private pending path.
///
/// Every refusal of the frame or the intent is decided before the pending file is opened.
/// @param engine owner of the encoder workspace.
/// @param execution the operation policy and sinks.
/// @param request the pending target and explicit intent.
/// @param frame the complete described samples.
/// @param metadata the immutable source facts.
/// @return the finished file's size; `ErrorCode::Unsupported` for a representation this container
///         does not spell, a channel schema other than grey, colour or named, the lossy compression
///         on anything but eight-bit unsigned samples, a mosaic of more than one channel, an intent
///         stating a scale, or an intent card that is an artifact identity or not Exif; the
///         budget's refusal of the row workspace or of the buffer the metadata pass reads the file
///         into; `ErrorCode::LimitExceeded` when that file is larger than one image of the context
///         may be; the cancellation's own failure; `ErrorCode::ExternalFailure` when the output
///         cannot be created, reopened or measured, or a row, the directory or the metadata falls
///         short; or what the metadata adapter reported while embedding the Exif.
Result<CodecEncodeReport> writeTiff(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata);

/// Opens a container for writing over a stream this adapter owns.
/// @param engine context supplying the allocation bound the library is held to.
/// @param execution the operation whose diagnostic sink a warning reaches.
/// @param path the pending output path.
/// @param bigTiff whether the container is written in the form that addresses more than four
///        gibibytes.
/// @param byteOrder which byte order the file states, as the library spells it: `'l'`, `'b'` or
///        zero for the host's.
/// @return the open container; `ErrorCode::ExternalFailure` with
///         `CodecFailure::DestinationUnavailable` when the path cannot be opened or the library
///         cannot create the container on it; or `ErrorCode::AllocationFailed` when the library's
///         open options cannot be allocated.
Result<TiffHandle> openTiffWrite(const EngineContext& engine, const ExecutionContext& execution, std::string_view path,
                                 bool bigTiff, char byteOrder);

/// Reads the Exif a container states, through the metadata adapter.
///
/// The container addresses its own tags by absolute offset, so the metadata adapter is handed the
/// whole file rather than pieces that cannot be proved to reach every one of them. On Windows the
/// file is mapped read-only, by a handle that refuses in-place writes while the tags are read, and
/// its pages belong to the operating system's cache rather than to the context's budget; elsewhere
/// it is read once into counted memory. Either is released before this returns, so a decode's peak
/// is the larger of the metadata and the samples rather than their sum.
///
/// @param engine the context whose limits bound the file, and whose budget pays for the buffer
///        where one is used.
/// @param execution the operation whose cancellation is checked before the read.
/// @param path literal UTF-8 disk path.
/// @return the cards, empty for an empty file; the cancellation's own failure;
///         `ErrorCode::LimitExceeded` when the file is larger than one image of the context may be;
///         `ErrorCode::ExternalFailure` when it cannot be measured, opened or mapped at its size;
///         outside Windows, the budget's refusal or `ErrorCode::InvalidData` for a short read; or
///         what the metadata adapter reported.
Result<std::vector<CodecCard>> readTiffCards(const EngineContext& engine, const ExecutionContext& execution,
                                             std::string_view path);

/// Builds the typed metadata a decoded container states.
/// @param cards the preserved cards.
/// @param facts what the selected image states about itself.
/// @param sampleFormat the representation the file states.
/// @param fileByteOrder the byte order the file stores its samples in.
/// @param path the source path recorded as lineage.
/// @return the aggregate, or whatever building one of its parts reported.
Result<ImageMetadata> tiffMetadata(std::vector<CodecCard> cards, const TiffImageFacts& facts,
                                   const RawSampleFormat& sampleFormat, ByteOrder fileByteOrder, std::string_view path);

} // namespace an
