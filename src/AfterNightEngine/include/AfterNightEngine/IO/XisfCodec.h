// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The XISF adapter: OpenXISF behind the codec interface.
///
/// `createXisfCodec` builds an adapter to register in a `CodecRegistry`. It is reentrant: each call owns
/// its reader and library state, and nothing is created or kept on the caller's behalf - no registry,
/// engine context or global default. It probes, decodes whole images and native regions, decodes bands
/// of whole rows into caller memory for `ImageRowReader`, and encodes.
///
/// **What a read returns.** The arrangement the file declares, planar or interleaved, never reorganized
/// into the other, in native byte order whichever order the file stores. Unsigned 8-, 16- and 32-bit and
/// both float widths are resident types, unsigned 64-bit samples are carried as exact codes that are not,
/// and the two complex representations are refused by name rather than read as something else, as is an
/// image of more than two axes. Grey is mono, RGB is RGB or RGBA, CIE L*a*b* is Lab, and any other
/// combination is named channels; a mono image's 2x2 Bayer or 6x6 RGB filter array becomes a typed
/// mosaic. Samples are described as normalized unless a float image declares a range other than 0 to 1,
/// when they are physically scaled. Every codec and checksum algorithm the format defines is read, and a
/// block's checksum is verified before its data are used unless the adapter was created not to. The
/// structural facts the file states outside its properties - colour space, image type, sample range,
/// filter array, pedestal, orientation, identifiers, RGB working space, display function and resolution -
/// travel as XISF cards beside the properties, the FITS keywords and the unit's own metadata, and the
/// acquisition facts and astrometric solution are read from the FITS keywords, the solution from the
/// image's AstrometricSolution properties when the keywords state none. A read without metadata reads
/// the header alone: no ICC profile, thumbnail or property held in a data block is loaded.
///
/// **Regions and bands.** A region is read natively, the library reading or decompressing the samples of
/// the region alone straight into the frame: an uncompressed block run by run, a compressed one subblock
/// by subblock, those that hold bytes of the region. A filter array's phase moves with the region's
/// offset, and a solution is cropped to it. A block compressed as one subblock is decoded whole for any
/// region, and a block with a checksum is read whole and verified first; both are still read correctly,
/// and `supportsBoundedXisfRegionDecode` tells a caller which files they are. A whole image, a region of
/// whole rows or a band of rows of 32 MiB or more, read by an operation granted eight or more workers, is
/// read strip by strip on those workers, every strip through the one reader, when its block can be read
/// in parts.
///
/// **What a write stores.** Prepared planar or interleaved samples in a representation XISF spells,
/// written only to the request's pending path; `writeImage` supplies atomic publication. The pixel data
/// are written in the form the intent states: uncompressed, or compressed with zlib (`Deflate`), LZ4,
/// LZ4-HC or Zstandard at the codec's own default level or the one stated (zlib's level 0 compresses
/// nothing, so its data are written uncompressed); their bytes shuffled - by
/// default whenever a sample has more than one byte, which one byte never needs - in subblocks of the
/// size stated, 4 MiB by default, which a region read decodes on their own; and with the checksum
/// stated, none by default. Every unsupported representation or option is refused before the pending
/// file is opened. The adapter also writes a reserved image, which `OutputTransaction::reserve` asks
/// for: the unit with zeros where the samples go, in an uncompressed block - so an intent that states a
/// compression is refused - whose rows `OutputTransaction::writeRows` then fills in place, its checksum
/// computed over the rows written at the commit. An encode that asks for `OutputHeaderProlog::Blank` writes the
/// unit as specified and then, in the pending file and before it is closed, overwrites everything of the header
/// before its root element - the XML declaration and the comment - with spaces: the header keeps its length and
/// every block its place, and the unit reads as the one written as specified does.
///
/// **Memory outside the budget.** The library keeps a model of the header beside its reader, bounded by
/// `kMaxXisfHeaderBytes` and its own element and object limits, and a read with metadata loads the image's
/// ancillary data within the engine's image limit; what a decode allocates besides the frame is what
/// `xisfDecodeLibraryBytes` answers, for a whole image or a region.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace an
{
class ImageCodec;
class EngineContext;
class ExecutionContext;
struct CodecRegion;

/// The largest serialized XML header accepted before the library parses it: 64 MiB, whatever the
/// pixels. It bounds the parser's input, not its total workspace and not the engine's counted memory.
inline constexpr std::uint64_t kMaxXisfHeaderBytes = 64u * 1024u * 1024u;

/// How an XISF adapter reads.
struct XisfCodecOptions
{
    /// Whether the data of a block with a checksum are verified against it before they are used, as the
    /// specification requires. A checksum covers the whole block, so a region of such a block is read by
    /// reading all of it and verifying it first. False is the caller's explicit choice to read such a block
    /// as one without a checksum, which nothing verifies then: regions of a unit it verified once, for
    /// example. A block written in the header is verified when the unit is opened, either way.
    bool verifyChecksums = true;
};

/// Creates the XISF adapter, verifying every checksum it reads.
///
/// An unreadable container or invalid header carries `CodecFailure::MetadataUnreadable`, and so does
/// one whose every image the library had to leave out - a geometry that describes no pixels, a data
/// block of another size than its geometry gives. Description refusals retain their own
/// classification: `ErrorCode::OutOfRange` for an image index past the last image;
/// `ErrorCode::Unsupported` with `CodecFailure::UnsupportedRepresentation` for a complex image or one
/// of more than two axes; and `ErrorCode::LimitExceeded` for geometry, card count or property text
/// beyond the supported limits. Cancellation and allocation failures also retain their detail.
/// An image whose data block the library cannot use is still described, and its decode fails - whole,
/// by region or by band: with `ErrorCode::InvalidData` and `CodecFailure::PayloadUnreadable` when the
/// block lies past the end of the file, fails its checksum or does not decompress to its size, or when
/// the file has since become shorter than the samples a read reaches; with `ErrorCode::Unsupported` and
/// `CodecFailure::UnsupportedRepresentation` for a codec or checksum algorithm the library does not
/// implement; and with `ErrorCode::LimitExceeded` when its stored or decompressed data exceed the
/// engine context's `maxImageBytes`.
///
/// An encode fails with `ErrorCode::Unsupported` and `CodecFailure::UnsupportedRepresentation` for a
/// representation XISF cannot spell - any signed integer, or a float other than 4 or 8 bytes - a
/// compression of another container (FITS's tiles, LZW, the JPEG transform), a semi-planar
/// arrangement, a bottom-left origin, any output scaling or a big-endian output byte order;
/// `ErrorCode::InvalidArgument` for a card holding a control character, which the XML cannot carry, a FITS keyword
/// whose name is longer than eight characters or whose text is not printable ASCII, or a property name that is not an
/// XISF identifier; `ErrorCode::LimitExceeded` for more cards than one image preserves; `ErrorCode::InvalidState` when
/// something other than a regular file stands at the pending path; `ErrorCode::ExternalFailure` when the
/// pending path cannot be inspected or measured, or with `CodecFailure::DestinationUnavailable` or
/// `CodecFailure::ShortWrite` when it cannot be opened or written in full; `ErrorCode::InvalidArgument`
/// with `CodecFailure::InvalidMetadata` when the library refuses the image model it was handed, naming
/// the element; or the execution's cancellation.
///
/// @return the adapter, or `ErrorCode::AllocationFailed` when it cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::shared_ptr<const ImageCodec>> createXisfCodec();

/// Creates the XISF adapter with the options stated, refusing and failing as the adapter with the default
/// options does.
///
/// @param options how the adapter reads.
/// @return the adapter, or `ErrorCode::AllocationFailed` when it cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::shared_ptr<const ImageCodec>>
createXisfCodec(const XisfCodecOptions& options);

/// An upper bound on the memory the library allocates outside the engine context's budget to decode one
/// image whole into the frame the engine allocates for it, through an adapter with the default options.
///
/// The same answer as the overload that takes the options, given the default options.
///
/// @param engine the engine context whose limits apply.
/// @param execution a valid execution of that engine context.
/// @param path the XISF file's UTF-8 path, taken literally.
/// @param imageIndex the zero-based image in the container.
/// @return what the overload that takes the options returns.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint64_t> xisfDecodeLibraryBytes(const EngineContext& engine,
                                                                                const ExecutionContext& execution,
                                                                                std::string_view path,
                                                                                std::uint32_t imageIndex = 0u);

/// An upper bound on the memory the library allocates outside the engine context's budget to decode one
/// image whole into the frame the engine allocates for it, through an adapter with the options stated, in
/// this execution.
///
/// Read in one call, an uncompressed block is read straight into the frame, and verified there when it has
/// a checksum, which makes the answer zero. A compressed block needs its stored bytes, which are read whole
/// and verified before they are decompressed, and a byte-shuffled one besides them a buffer of its
/// decompressed size that it is unshuffled from; a block written in the header is held by the reader from
/// the open on, so it counts its stored bytes besides. A decode the execution reads strip by strip on its
/// workers - see the file comment - takes, on each worker, what a region of the block takes, as
/// `xisfRegionDecodeLibraryBytes` answers it. Two things are not counted: the model of the header the
/// reader keeps, which whatever the pixels is bounded by `kMaxXisfHeaderBytes` and the library's own
/// limits, and a codec's decoder state, a fixed few hundred KiB at most for each subblock decoded at once,
/// whatever the image. This call reads the header alone.
///
/// @param engine the engine context whose limits apply.
/// @param execution a valid execution of that engine context, whose granted workers decide whether a
///        decode is read on them.
/// @param path the XISF file's UTF-8 path, taken literally.
/// @param imageIndex the zero-based image in the container.
/// @param options the options of the adapter the decode goes through.
/// @return the bound in bytes; `ErrorCode::NotInitialized` for an engine context or execution that is
///         not valid; `ErrorCode::IncompatibleArguments` for an execution of another engine context; the
///         execution's cancellation; `ErrorCode::InvalidArgument` for a path that is not a valid boundary
///         path; the failure a decode of the image returns for a block the library cannot use; or a
///         failure a read's header would return.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint64_t>
xisfDecodeLibraryBytes(const EngineContext& engine, const ExecutionContext& execution, std::string_view path,
                       std::uint32_t imageIndex, const XisfCodecOptions& options);

/// An upper bound on the memory the library allocates outside the engine context's budget to decode one
/// region of an image into the frame the engine allocates for it, or to read the same rows as a band into
/// caller memory through `ImageRowReader`, through an adapter with the options stated, in this execution.
///
/// A region of an uncompressed block is read run by run straight into the frame; runs close together are
/// read in one read through a staging buffer of at most 4 MiB, which a region of whole rows never needs. A
/// region of a compressed block is decoded one subblock at a time, those that hold bytes of the region, each
/// read from its stored bytes, so it takes a subblock and its stored bytes whatever the region's size - a
/// subblock's stored bytes counted as at most its data's, as every writer that divides a block into
/// subblocks stores one that does not shrink as it is. A block compressed as one subblock is that one
/// subblock. A block with a checksum that is verified is read whole first, so it takes its stored bytes, and
/// a block written in the header is held by the reader from the open on. A region the execution reads strip
/// by strip on its workers takes that on each worker. A region that is the whole image is read as the whole
/// image is. What `xisfDecodeLibraryBytes` leaves out is left out here too. This call reads the header alone.
///
/// @param engine the engine context whose limits apply.
/// @param execution a valid execution of that engine context, whose granted workers decide whether a
///        region is read on them.
/// @param path the XISF file's UTF-8 path, taken literally.
/// @param region the rectangle, inside the image, in the source's row order.
/// @param imageIndex the zero-based image in the container.
/// @param options the options of the adapter the decode goes through.
/// @return the bound in bytes; `ErrorCode::OutOfRange` for a region that is empty or reaches past the
///         image; or what `xisfDecodeLibraryBytes` returns otherwise.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint64_t>
xisfRegionDecodeLibraryBytes(const EngineContext& engine, const ExecutionContext& execution, std::string_view path,
                             const CodecRegion& region, std::uint32_t imageIndex = 0u,
                             const XisfCodecOptions& options = {});

/// Whether a region read of a file's first image needs no more memory beyond the returned frame than a
/// few subblocks or a staging buffer, whatever the image's size.
///
/// Reads the first image's structure without reading its cards or decoding a pixel. An uncompressed block
/// and one compressed in more than one subblock answer true. A block compressed as one subblock, a block
/// with a checksum that the options verify, and a block written in the header answer false: a region read
/// is still available and correct, but the library decodes, reads or holds the whole block for it,
/// outside the engine's budget, so a caller that needs a strict bound on a region read's memory refuses
/// that file. `xisfRegionDecodeLibraryBytes` gives the bound itself.
///
/// @param engine the engine context whose limits bound the image's geometry.
/// @param execution a valid execution of that engine context.
/// @param path the XISF file's UTF-8 path, taken literally; nonempty, at most 131072 bytes, no NUL.
/// @param options the options of the adapter the region is read through.
/// @return true for a block read in parts, false for one read whole; `ErrorCode::NotInitialized` for an
///         engine context or execution that is not valid; `ErrorCode::IncompatibleArguments` for an
///         execution of another engine context; the execution's cancellation; `ErrorCode::InvalidArgument`
///         for a path that is not a valid boundary path; the failure a decode of the image returns for a
///         block the library cannot use; or a failure a read's header would return.
[[nodiscard]] AFTERNIGHTENGINE_API Result<bool> supportsBoundedXisfRegionDecode(const EngineContext& engine,
                                                                                const ExecutionContext& execution,
                                                                                std::string_view path,
                                                                                const XisfCodecOptions& options = {});
} // namespace an
