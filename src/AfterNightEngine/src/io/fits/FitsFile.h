// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FITS adapter's private CFITSIO boundary: the handle, the selected image's
// header, and the read, write and metadata steps the adapter composes.
//
// CFITSIO's types appear in this header and the FITS translation units only. A handle is opened as a
// literal disk file, so the library's extended filename syntax is never interpreted, and it is closed
// on every return. A library status is translated here, by its number alone: CFITSIO's message stack
// is shared by every thread, so this boundary never drains it.

#pragma once

#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>

#include <fitsio.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace an
{
class FitsPayloadFile;

/// Closes a CFITSIO handle, ignoring the close status; for the owning `FitsHandle`.
struct FitsCloser
{
    /// Closes `file` when it is not null.
    /// @param file the handle to close.
    void operator()(fitsfile* file) const noexcept;
};
/// An open CFITSIO file, closed when the handle is destroyed.
using FitsHandle = std::unique_ptr<fitsfile, FitsCloser>;

/// The selected image's description, as read from its header, and the facts a decode needs from it.
struct FitsHeader
{
    CodecImageInfo info; ///< The description, layout, image count, metadata and probe disposition.
    int hdu; ///< The selected image's one-based header-data unit.
    int bitpix; ///< The stored BITPIX.
    int datatype; ///< The CFITSIO data type the decoded samples are read as.
    double scale; ///< BSCALE, or 1 when absent.
    double zero; ///< BZERO, or 0 when absent.
    bool compressed; ///< Whether the image is tile-compressed.
};

/// A CFITSIO status as an engine failure: `ErrorCode::AllocationFailed` for a memory refusal and
/// `ErrorCode::ExternalFailure` otherwise, with the library's code, the path and a codec reason.
/// @param status the nonzero CFITSIO status.
/// @param operation the step that failed, for a person reading the failure.
/// @param path the file's path.
/// @param writing whether the step was writing, which makes the reason `CodecFailure::ShortWrite`
///        rather than `CodecFailure::MetadataUnreadable`.
/// @return the failure.
Status fitsFailure(int status, const char* operation, std::string_view path, bool writing = false);
/// Opens a FITS file read-only as a literal disk path.
/// @param path the file's UTF-8 path.
/// @return the handle; `ErrorCode::LimitExceeded` for a path longer than CFITSIO accepts; or the
///         library's refusal through `fitsFailure`.
Result<FitsHandle> openFits(std::string_view path);
/// Selects the requested image and reads its header into a description, without reading a pixel.
/// @param file the open file.
/// @param engine the engine context whose limits bound the geometry.
/// @param execution the operation; cancellation is checked per header-data unit and per card.
/// @param request the image index, and whether to read metadata and validate the payload.
/// @param integer64 the BITPIX=64 policy that decides the decoded representation.
/// @param enumerateImages whether to count every image, which a probe reports; a decode after its
///        probe passes false and stops at the selected image.
/// @return the header, or the failures `createFitsCodec` lists for a read.
Result<FitsHeader> readFitsHeader(fitsfile* file, const EngineContext& engine, const ExecutionContext& execution,
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
/// Decodes the image a header already describes from its open file, in strips of at most 4 MiB,
/// reporting progress per strip.
/// @param engine the engine context that owns the frame.
/// @param execution the operation; cancellation is checked before every strip.
/// @param request the source selection.
/// @param region the rectangle to decode, or null for the whole image.
/// @param integer64 the BITPIX=64 policy.
/// @param file the open file, on the selected image's header-data unit where `readFitsHeader` leaves
///        it; borrowed.
/// @param header the selected image's header, consumed.
/// @return the frame and its metadata; `ErrorCode::OutOfRange` for a region outside the image or a
///         64-bit code the policy refuses; `ErrorCode::InvalidData` with
///         `CodecFailure::PayloadUnreadable` for pixels CFITSIO cannot read; or the engine context's
///         memory refusal.
Result<CodecDecodeResult> readPreparedFits(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion* region,
                                           FitsInteger64Policy integer64, fitsfile* file, FitsHeader header);
/// Decodes a checked region of the selected image into memory laid out as `layout`.
///
/// The one pixel reader behind every decode: whole rows of an image the library would only copy or
/// flip come straight from the payload through `payloadFile`, which is opened here the first time it
/// is needed and kept open for the caller's next region; everything else goes through the library.
/// @param execution the operation, checked for cancellation between strips.
/// @param request the source selection, for failures.
/// @param area a nonempty region inside the image, already checked.
/// @param integer64 how 64-bit codes are admitted.
/// @param file the open library handle on the selected image.
/// @param header the selected image's header.
/// @param samples where the region's first sample goes.
/// @param layout the region's layout in that memory.
/// @param payloadFile the payload handle, open or not.
/// @return success, or the typed failure of the first strip that could not be read or admitted.
Status readFitsPixels(const ExecutionContext& execution, const CodecReadRequest& request, const CodecRegion& area,
                      FitsInteger64Policy integer64, fitsfile* file, const FitsHeader& header, std::byte* samples,
                      const ImageLayout& layout, FitsPayloadFile& payloadFile);
/// Writes a frame and its cards to the request's pending path.
/// @param engine the engine context that owns the staging and row workspace.
/// @param execution the operation; cancellation is checked before the file is created and per strip.
/// @param request the pending path and the output intent.
/// @param frame the planar samples, borrowed.
/// @param metadata the source facts whose cards and acquisition record the intent may carry forward.
/// @return the bytes written, or the failures `createFitsCodec` lists for an encode.
Result<CodecEncodeReport> writeFits(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata);
/// Builds a FITS image's metadata from its cards: the acquisition projection, a supported WCS
/// solution - cropped when a region is given - and the file's provenance.
/// @param codec the cards and source encoding, consumed.
/// @param extent the whole image's extent, which the WCS solution is read against.
/// @param path the file's path, recorded in provenance when it fits.
/// @param integer64 the BITPIX=64 policy.
/// @param integerImage whether the image is BITPIX=64, whose lossy decode provenance records.
/// @param crop the decoded region, or null for the whole image.
/// @return the metadata, or the failure building it reported.
Result<ImageMetadata> fitsMetadata(CodecMetadata codec, const ImageExtent& extent, std::string_view path,
                                   FitsInteger64Policy integer64, bool integerImage, const CodecRegion* crop = nullptr);
/// A UTF-8 path in the spelling CFITSIO opens: backslashes become forward slashes on Windows.
/// @param path the path.
/// @return the converted copy.
std::string fitsPath(std::string_view path);
/// The CFITSIO data type code for a raw sample representation.
/// @param format the representation.
/// @return the `T...` code, or zero for a representation FITS cannot store.
int fitsDatatype(RawSampleFormat format) noexcept;
/// The CFITSIO image type for a raw sample representation, unsigned types using their offset forms.
/// @param format the representation.
/// @return the `..._IMG` code, or zero for a representation FITS cannot store.
int fitsImageType(RawSampleFormat format) noexcept;

/// How an uncompressed image's stored samples become its decoded representation, when that is a
/// byte swap and at most a flip of the sign bit.
struct FitsPayloadTransform
{
    std::uint32_t sampleBytes = 0u; ///< Bytes in one stored sample: 1, 2, 4 or 8.
    bool flipSign = false; ///< Whether the sign bit flips: the unsigned zero-point convention.
};
/// The transform that decodes the selected image straight from its payload.
///
/// Every integer width but 64 bits at unit scale with the signed or unsigned zero point, and both
/// floating widths at unit scale and zero offset: the representations the library itself only copies
/// or flips.
/// @param header the selected image's header.
/// @return the transform, or nothing when the image needs the library's conversion: tile-compressed,
///         64-bit integers, a scale other than one, or another zero point.
std::optional<FitsPayloadTransform> fitsPayloadTransform(const FitsHeader& header) noexcept;
/// Converts stored big-endian samples in place into their decoded representation.
/// @param bytes the samples, as read.
/// @param samples how many.
/// @param transform what each one needs.
void convertFitsPayload(std::byte* bytes, std::uint64_t samples, FitsPayloadTransform transform) noexcept;
} // namespace an
