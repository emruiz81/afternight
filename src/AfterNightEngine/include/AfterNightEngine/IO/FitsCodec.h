// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The FITS adapter: CFITSIO behind the codec interface.
///
/// `createFitsCodec` builds an adapter to register in a `CodecRegistry`. It probes, decodes whole
/// images and native regions, and encodes. CFITSIO's types stay inside the adapter's translation
/// units; each call opens and closes its own disk-file handle, so the adapter is reentrant, and it sets
/// no process-wide library control. A path is taken literally: CFITSIO's extended filename syntax is
/// never interpreted.
///
/// **What a read returns.** Samples are planar in native byte order, one plane per channel. The
/// representation follows the stored BITPIX and the canonical FITS offsets - BZERO -128 makes eight
/// bits signed, 32768 and 2147483648 make sixteen and thirty-two bits unsigned - and any other scaling
/// is applied once, into Float64, without normalization. A floating-point image is described as
/// physically scaled and an integer one as sensor codes. One channel is mono, a filter array when
/// BAYERPAT names a 2x2 pattern; three channels are RGB; any other count is named channels. ROWORDER
/// BOTTOM-UP gives a bottom-left origin. Metadata carries the header's cards, those an extension
/// inherits from the primary header included; the acquisition facts projected from them; an
/// astrometric solution when the WCS cards describe one the engine supports, cropped for a region; and
/// the stored representation and scaling as the source encoding, kept even when metadata is not read.
///
/// **What a write stores.** Planar samples in any of the eight integer representations or either
/// float, padding and a foreign byte order included, with the scaling, compression and cards the output
/// intent states; the header space for the scaling cards is reserved before any pixel is written, so
/// the payload is never moved.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <memory>
#include <string_view>

namespace an
{
class ImageCodec;
class EngineContext;
class ExecutionContext;

/// How a BITPIX=64 image reaches a caller; the set of resident sample types does not change.
enum class FitsInteger64Policy : unsigned char
{
    /// The default: a stored code whose magnitude exceeds 2^53 is refused with `ErrorCode::OutOfRange`
    /// before it is converted to Float64 and scaled.
    CheckedFloat64,
    /// Every code is converted to Float64, rounding outside that range; the decode's provenance records
    /// that the loss was accepted, even when metadata is not read.
    AllowLossyFloat64,
    /// The exact signed stored codes are returned as a 64-bit integer frame that is not a resident
    /// sample type, with the scaling kept in the source encoding.
    PreserveRaw
};

/// Creates the FITS adapter.
///
/// A read through it fails with `ErrorCode::OutOfRange` for an image index past the file's last image
/// or, under `FitsInteger64Policy::CheckedFloat64`, a 64-bit code beyond 2^53;
/// `ErrorCode::Unsupported` for a BITPIX CFITSIO cannot describe or an image with more than three
/// non-singleton axes; `ErrorCode::LimitExceeded` for geometry beyond the engine context's limits, too
/// many header cards or images, or a path longer than CFITSIO accepts; `ErrorCode::InvalidData` for a
/// BSCALE of zero or a scaling that is not finite, or with `CodecFailure::PayloadUnreadable` for a
/// payload that fails its checksum, extends past the file or cannot be decoded; and a CFITSIO
/// failure as `ErrorCode::ExternalFailure`, or `ErrorCode::AllocationFailed` for its memory refusal,
/// with `CodecFailure::MetadataUnreadable` and the library's code. A probe that validates the payload
/// reports `CodecProbeDisposition::PayloadValidatedWithoutDecode` for an uncompressed image and
/// `DecodeRequired` for a tile-compressed one.
///
/// An encode writes only the request's pending path, where an empty file the output transaction
/// reserved may already stand; publishing it atomically is `writeImage`'s job. Options, cards and
/// sample values are refused before the pending file is opened. It fails with
/// `ErrorCode::Unsupported` with `CodecFailure::UnsupportedRepresentation` for a compression other
/// than Rice, Gzip or PLIO, byte shuffling, a compression block size or a checksum stated beside it, which
/// are XISF's, a 64-bit integer image with any compression, a float image with Rice or
/// PLIO, an unsigned 32-bit image with PLIO, `OutputScaling::NormalizeFullRange`, a little-endian
/// output byte order, a filter array that is not 2x2, or a pending path longer than CFITSIO accepts;
/// `ErrorCode::OutOfRange` for a scale that is zero or not finite, an offset that is not finite, or a
/// PLIO sample outside 0 to 2^24 - 1; `ErrorCode::InvalidArgument` for a card whose keyword is longer
/// than 60 bytes or whose text is not printable ASCII; `ErrorCode::LimitExceeded` for more cards than
/// one header holds or a numeric card longer than one record; `ErrorCode::InvalidState` when something
/// other than an empty regular file stands at the pending path; `ErrorCode::ExternalFailure` when the
/// pending path cannot be inspected or the finished file measured; and a CFITSIO failure as for a
/// read, with `CodecFailure::ShortWrite`.
///
/// @param integer64 the BITPIX=64 policy for every read through this adapter.
/// @return the adapter; `ErrorCode::InvalidArgument` for a policy outside the enumeration;
///         `ErrorCode::Unsupported` when the linked CFITSIO is not a reentrant build; or
///         `ErrorCode::AllocationFailed` when the adapter cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::shared_ptr<const ImageCodec>>
createFitsCodec(FitsInteger64Policy integer64 = FitsInteger64Policy::CheckedFloat64);

/// Whether a region read of a file's first image needs no memory beyond the returned frame.
///
/// Reads the first image's structure without reading its cards or decoding a pixel. A tile-compressed
/// image answers false: a region read is still available, but CFITSIO allocates the decompressed
/// tiles itself, outside the engine's budget, so a caller that needs a strict bound on a region read's
/// memory refuses that file.
///
/// @param engine the engine context whose limits bound the image's geometry.
/// @param execution a valid execution of that engine context.
/// @param path the FITS file's UTF-8 path, taken literally; nonempty, at most 131072 bytes, no NUL.
/// @return true for an ordinary image, false for a tile-compressed one; `ErrorCode::NotInitialized`
///         for an engine context or execution that is not valid; `ErrorCode::IncompatibleArguments`
///         for an execution of another engine context; the execution's cancellation;
///         `ErrorCode::InvalidArgument` for a path that is not a valid boundary path; or a failure a
///         read's header would return.
[[nodiscard]] AFTERNIGHTENGINE_API Result<bool>
supportsBoundedFitsRegionDecode(const EngineContext& engine, const ExecutionContext& execution, std::string_view path);
} // namespace an
