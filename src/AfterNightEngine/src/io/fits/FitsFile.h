// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FITS adapter's private OpenFITS boundary: the open reader, the selected image's header, the
// translation of what the library throws, and the read and write steps the adapter composes. The metadata step reaches
// no library and is declared in `io/fits/FitsMetadata.h`.
//
// OpenFITS's types appear in this header and the FITS translation units only. Every call into the library is made
// where what it throws is caught: the library's own exceptions are translated by their class and code, never by their
// text (`fitsLibraryFailure`), and anything else - an allocation refusal among them - through `kFitsBoundary`. A path
// is always the name of a file: the library parses no extended file name unless it is asked to, and this adapter
// never asks.

#pragma once

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>

#include <openfits/error.h>
#include <openfits/reader.h>
#include <openfits/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace an
{
/// Where every failure this adapter translates says it came from.
inline constexpr ExternalBoundary kFitsBoundary{
    .library = "OpenFITS", .operation = "FITS codec", .domain = ErrorDomain::Io};

/// What a library call was doing when it failed, which decides the codec reason its failure carries.
enum class FitsStage : std::uint8_t
{
    Open, ///< Opening the file and reading its headers.
    Read, ///< Reading the pixel data of an image.
    Write, ///< Writing a file to the pending path.
};

/// An open reader.
///
/// The reader owns the file for its whole life and reads it by position, from any number of threads at once: its
/// const member functions may run concurrently, which is what lets a decode read its strips on several workers.
struct FitsHandle
{
    std::unique_ptr<openfits::reader> reader; ///< The open file.

    /// The decoded tiles of a compressed image whose tiles span several rows, kept between the strips and the bands
    /// of rows a decode reads, so each tile is decoded once; null for every other image, whose tiles are a row each
    /// and are never met twice.
    std::unique_ptr<openfits::tile_cache> tiles;
};

/// The selected image's description, as read from its header, and the facts a decode needs from it.
struct FitsHeader
{
    CodecImageInfo info; ///< The description, layout, image count, metadata and probe disposition.
    std::size_t unit = 0u; ///< The selected image's header and data unit, counted from 0 for the primary one.
    std::uint32_t axes = 2u; ///< How many axes the image's geometry names; a region names as many.
    int bitpix = 16; ///< The stored BITPIX.
    double scale = 1.0; ///< BSCALE, or 1 when absent.
    double zero = 0.0; ///< BZERO, or 0 when absent.
    bool compressed = false; ///< Whether the image is tile-compressed.
    bool tilesSpanRows = false; ///< Whether a compressed image's tiles are more than one row high.
};

/// Builds a refusal this adapter decided the meaning of.
/// @param code the engine classification this adapter chose.
/// @param reason the codec-specific refinement.
/// @param operation the entry point being performed.
/// @param path the file the refusal is about.
/// @param message what to tell a person.
/// @return a failure in ErrorDomain::Io carrying the path and the reason.
Status fitsFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                   std::string_view message);

/// Translates what the library threw, by the exception's class and code.
///
/// An `io_error` that failed to open or find a file is `ErrorCode::ExternalFailure` - with
/// `CodecFailure::DestinationUnavailable` when writing - and one that ran out of data or failed to read is
/// `ErrorCode::InvalidData` while reading, and `CodecFailure::ShortWrite` when writing; `invalid_data_error` and
/// `integrity_error` are `ErrorCode::InvalidData`; `unsupported_error` is `ErrorCode::Unsupported` with
/// `CodecFailure::UnsupportedRepresentation`; `limit_error` is `ErrorCode::LimitExceeded`; `validation_error` is
/// `ErrorCode::InvalidArgument` naming the keyword; `usage_error` is `ErrorCode::Internal`, a precondition this
/// adapter should have checked; and `cancelled_error` is `ErrorCode::Cancelled`, which a caller holding the execution
/// replaces with the execution's own status. Opening reports `CodecFailure::MetadataUnreadable` and reading
/// `CodecFailure::PayloadUnreadable` where the class names no other reason.
/// @param error what the library threw.
/// @param stage what the call was doing.
/// @param operation the entry point being performed.
/// @param path the file the failure is about.
/// @return a codec failure whose cause carries the library, its code and its message.
Status fitsLibraryFailure(const openfits::error& error, FitsStage stage, std::string_view operation,
                          std::string_view path);

/// Opens a FITS file for reading, as the name of a file.
///
/// The open reads the primary header and no data. Every allocation of the library - a decoded tile, the content of
/// a gzip-compressed file - is bounded by the engine's image limit, and the units it walks by the card bound a
/// header keeps; nothing is inflated into a temporary file.
/// @param path literal UTF-8 disk path.
/// @param limits the engine limits whose image size bounds the library's allocations.
/// @return the handle, or the library's refusal through `fitsLibraryFailure`.
Result<FitsHandle> openFits(std::string_view path, const EngineLimits& limits);

/// Opens a FITS file as `openFits` does, with one record of a header read as blanks.
///
/// The record is a card the library cannot take and the codec does not need: read as blanks it is a card with no
/// keyword, which the standard allows anywhere in a header, and everything else of the file is read as it is.
/// @param path literal UTF-8 disk path.
/// @param limits the engine limits whose image size bounds the library's allocations.
/// @param record the file offset of the 80-byte record.
/// @return the handle, or the library's refusal through `fitsLibraryFailure`.
Result<FitsHandle> openFitsWithoutRecord(std::string_view path, const EngineLimits& limits, std::uint64_t record);

/// Selects the requested image and reads its header into a description, without reading a pixel.
///
/// An integer image whose `BLANK` card states no integer is one the library cannot describe, and one whose samples
/// the codec reads all the same, since it reads no sample as undefined: the file is then opened again with that
/// card read as blanks, and `file` holds the new reader. A compressed image whose tiles span several rows is given
/// the cache its decodes keep those tiles in.
/// @param file the open file; its reader is replaced when the file had to be opened again.
/// @param engine the engine context whose limits bound the geometry.
/// @param execution the operation; cancellation is checked per header-data unit and per card.
/// @param request the image index, and whether to read metadata and validate the payload.
/// @param integer64 the BITPIX=64 policy that decides the decoded representation.
/// @param enumerateImages whether to count every image, which a probe reports; a decode after its
///        probe passes false and stops at the selected image.
/// @return the header, or the failures `createFitsCodec` lists for a read.
Result<FitsHeader> readFitsHeader(FitsHandle& file, const EngineContext& engine, const ExecutionContext& execution,
                                  const CodecReadRequest& request, FitsInteger64Policy integer64,
                                  bool enumerateImages = true);

/// Opens a file, reads the selected image's header without validating the payload, and decodes it.
/// @param engine the engine context that owns the frame.
/// @param execution the operation.
/// @param request the source selection.
/// @param region the rectangle to decode, or null for the whole image.
/// @param integer64 the BITPIX=64 policy.
/// @return the frame and its metadata, or a typed failure.
Result<CodecDecodeResult> readFits(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, const CodecRegion* region,
                                   FitsInteger64Policy integer64);

/// Decodes the image a header already describes from its open file, in strips of at most 4 MiB, reporting progress
/// per strip.
/// @param engine the engine context that owns the frame.
/// @param execution the operation; cancellation is checked before every strip.
/// @param request the source selection.
/// @param region the rectangle to decode, or null for the whole image.
/// @param integer64 the BITPIX=64 policy.
/// @param file the open file; borrowed.
/// @param header the selected image's header, consumed.
/// @param tiles the handle's tile cache, or null.
/// @return the frame and its metadata; `ErrorCode::OutOfRange` for a region outside the image or a 64-bit code the
///         policy refuses; `ErrorCode::InvalidData` with `CodecFailure::PayloadUnreadable` for pixels the library
///         cannot read; or the engine context's memory refusal.
Result<CodecDecodeResult> readPreparedFits(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion* region,
                                           FitsInteger64Policy integer64, const openfits::reader& file,
                                           FitsHeader header, openfits::tile_cache* tiles = nullptr);

/// Decodes a checked region of the selected image into memory laid out as `layout`.
///
/// The one pixel reader behind every decode. The region is read strip by strip, each strip a box of whole rows of the
/// region that lands where `layout` puts it; a large uncompressed payload, read by an operation granted enough
/// workers, is read on them, each strip by the worker that takes it. Memory the library cannot write samples into -
/// not aligned for them - is read a row at a time through one row of memory `engine` owns.
/// @param engine the engine context that owns that row.
/// @param execution the operation, checked for cancellation between strips.
/// @param request the source selection, for failures.
/// @param area a nonempty region inside the image, already checked.
/// @param integer64 how 64-bit codes are admitted.
/// @param file the open file.
/// @param header the selected image's header.
/// @param samples where the region's first sample goes.
/// @param layout the region's layout in that memory.
/// @param tiles the handle's tile cache, or null: where the strips of a compressed image whose tiles span several
///        rows find the tiles an earlier strip decoded.
/// @return success, or the typed failure of the first strip that could not be read or admitted.
Status readFitsPixels(const EngineContext& engine, const ExecutionContext& execution, const CodecReadRequest& request,
                      const CodecRegion& area, FitsInteger64Policy integer64, const openfits::reader& file,
                      const FitsHeader& header, std::byte* samples, const ImageLayout& layout,
                      openfits::tile_cache* tiles = nullptr);

/// Writes a frame and its cards to the request's pending path.
/// @param engine the engine context that owns the staging.
/// @param execution the operation; cancellation is checked before the file is created and as it is written.
/// @param request the pending path and the output intent.
/// @param frame the planar samples, borrowed.
/// @param metadata the source facts whose cards and acquisition record the intent may carry forward.
/// @return the bytes written, or the failures `createFitsCodec` lists for an encode.
Result<CodecEncodeReport> writeFits(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata);

/// The library's numeric type for a raw sample representation.
/// @param format the representation.
/// @return the type, or nothing for a representation FITS cannot store.
std::optional<openfits::numeric_type> fitsNumericType(RawSampleFormat format) noexcept;
} // namespace an
