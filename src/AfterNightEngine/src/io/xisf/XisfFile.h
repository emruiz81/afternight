// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the XISF adapter's private OpenXISF boundary: the open reader, the selected image's header, the
// translation of what the library throws, and the read, write and metadata steps the adapter composes.
//
// OpenXISF's types appear in this header and the XISF translation units only. Every call into the library is made
// where what it throws is caught: the library's own exceptions are translated by their class and code, never by their
// text (`xisfLibraryFailure`), and anything else - an allocation refusal among them - through `kXisfBoundary`.

#pragma once

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>

#include "io/registry/ReservedCodecWrite.h"

#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/property.h>
#include <openxisf/reader.h>
#include <openxisf/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an
{
/// Where every failure this adapter translates says it came from.
inline constexpr ExternalBoundary kXisfBoundary{
    .library = "OpenXISF", .operation = "XISF codec", .domain = ErrorDomain::Io};

/// The keyword of the XISF card stating the colour space the file declares; like the keywords below, a
/// structural fact the file states outside its properties, which the encoder consumes and restates.
inline constexpr std::string_view kXisfColorSpaceKeyword = "ColorSpace";
/// The keyword of the XISF card stating the image type - light, dark, flat, bias and so on.
inline constexpr std::string_view kXisfImageTypeKeyword = "ImageType";
/// The keyword of the XISF card stating the sample range, as `low:high` in shortest exact decimals.
inline constexpr std::string_view kXisfBoundsKeyword = "Bounds";
/// The keyword of the XISF card stating the filter array, as `WxH:letters`, with `;name` when the file names it.
inline constexpr std::string_view kXisfFilterArrayKeyword = "ColorFilterArray";
/// The keyword of the XISF card stating the pedestal added to every sample, as a shortest exact decimal.
inline constexpr std::string_view kXisfOffsetKeyword = "Offset";
/// The keyword of the XISF card stating how the image is turned to be shown, as the attribute's own text.
inline constexpr std::string_view kXisfOrientationKeyword = "Orientation";
/// The keyword of the XISF card stating the image's identifier within its unit.
inline constexpr std::string_view kXisfIdKeyword = "Id";
/// The keyword of the XISF card stating the image's UUID, which a derived output never inherits.
inline constexpr std::string_view kXisfUuidKeyword = "Uuid";
/// The keyword of the XISF card stating the RGB working space, as `gamma;x1,x2,x3;y1,y2,y3;Y1,Y2,Y3;name`.
inline constexpr std::string_view kXisfWorkingSpaceKeyword = "RGBWorkingSpace";
/// The keyword of the XISF card stating the display function, as its five quadruples and its name.
inline constexpr std::string_view kXisfDisplayFunctionKeyword = "DisplayFunction";
/// The keyword of the XISF card stating the display resolution, as `horizontal;vertical;unit`.
inline constexpr std::string_view kXisfResolutionKeyword = "Resolution";
/// The namespace of the unit's own metadata - creator, creation time, codecs - which every writer states for itself.
inline constexpr std::string_view kXisfUnitMetadataPrefix = "XISF:";
/// The comment of a world-coordinate card this adapter translated from the image's AstrometricSolution. An XISF write
/// leaves such a card to the solution's own properties while they are written with it, so a unit read and written
/// again states its solution as its writer did, once.
inline constexpr std::string_view kXisfTranslatedSolutionComment = "XISF AstrometricSolution";

/// What a library call was doing when it failed, which decides the codec reason its failure carries.
enum class XisfStage : std::uint8_t
{
    Open, ///< Opening the unit and reading its header.
    Read, ///< Reading the pixel data of an image.
    Write, ///< Writing a unit to the pending file.
};

/// An open reader.
///
/// The reader owns the file for its whole life and reads it by position, from any number of threads at once.
struct XisfHandle
{
    std::unique_ptr<openxisf::reader> reader; ///< The open unit.
};

/// The selected image's checked description beside the library facts a decode still needs.
struct XisfHeader
{
    CodecImageInfo info; ///< Descriptor, decoded layout, image count and projected metadata.
    /// The arrangement the file declares, never a preference.
    openxisf::pixel_storage storage = openxisf::pixel_storage::planar;
    openxisf::sample_format sampleFormat = openxisf::sample_format::uint16; ///< The representation the file declares.
    /// How many axes the image's geometry names: one for a single row, two otherwise. A region names as many.
    std::uint32_t axes = 2u;
    /// How the selected image's pixel data are stored, when its data block is available.
    std::optional<openxisf::block_info> block;
    /// Why the selected image's data block is unavailable - an unsupported codec, a block past the end of the file,
    /// data that fail their checksum - which its decode returns; a success when the block is available. The rest of
    /// the image is still described, as the library still lists it.
    Status blockFailure;
};

/// Builds a refusal this adapter decided the meaning of.
/// @param code the engine classification this adapter chose.
/// @param reason the codec-specific refinement.
/// @param operation the entry point being performed.
/// @param path the file the refusal is about.
/// @param message what to tell a person.
/// @return a failure in ErrorDomain::Io carrying the path and the reason.
Status xisfFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                   std::string_view message);

/// Translates what the library threw, by the exception's class and code.
///
/// An `io_error` that failed to open or find a file is `ErrorCode::ExternalFailure` - with
/// `CodecFailure::DestinationUnavailable` when writing - and one that ran out of data or failed to read or write is
/// `ErrorCode::InvalidData` reading and `CodecFailure::ShortWrite` writing; `invalid_data_error` and `integrity_error`
/// are `ErrorCode::InvalidData`; `unsupported_error` is `ErrorCode::Unsupported` with
/// `CodecFailure::UnsupportedRepresentation`; `limit_error` is `ErrorCode::LimitExceeded`; `validation_error` is
/// `ErrorCode::InvalidArgument` naming the element; `usage_error` is `ErrorCode::Internal`, a precondition this
/// adapter should have checked; and `cancelled_error` is `ErrorCode::Cancelled`, which a caller holding the execution
/// replaces with the execution's own status. Opening reports `CodecFailure::MetadataUnreadable` and reading
/// `CodecFailure::PayloadUnreadable` where the class names no other reason.
/// @param error what the library threw.
/// @param stage what the call was doing.
/// @param operation the entry point being performed.
/// @param path the file the failure is about.
/// @return a codec failure whose cause carries the library, its code and its message.
Status xisfLibraryFailure(const openxisf::error& error, XisfStage stage, std::string_view operation,
                          std::string_view path);

/// Opens a unit for reading and reads its header.
///
/// Every allocation of the library is bounded by the engine's image limit, and the header by `kMaxXisfHeaderBytes`.
/// A header-only open loads no ancillary data - no ICC profile, no thumbnail pixels, no property value held in a data
/// block - which is what a read without metadata asks for; a full open bounds what it loads to the image limit.
/// @param path literal UTF-8 disk path.
/// @param limits the engine limits whose image size bounds the library's allocations.
/// @param headerOnly whether to leave the ancillary data unloaded.
/// @return the reader, or a typed failure carrying `CodecFailure::MetadataUnreadable` or a limit.
Result<XisfHandle> openXisf(std::string_view path, const EngineLimits& limits, bool headerOnly);

/// Describes the selected image without reading its pixel data.
/// @param reader an open unit.
/// @param engine context supplying the geometry limits.
/// @param execution the operation whose cancellation is checked before the image is described and
///        between its cards.
/// @param request the source selection, including whether metadata is wanted.
/// @return the checked header; the execution's cancellation, when it is already stopped; or a typed
///         format, limit or cancellation failure carrying `CodecFailure::MetadataUnreadable` above its
///         cause.
Result<XisfHeader> readXisfHeader(const openxisf::reader& reader, const EngineContext& engine,
                                  const ExecutionContext& execution, const CodecReadRequest& request);

/// Opens, describes and decodes one image, or one region of it, in a single call.
/// @param engine owner of the decoded allocation.
/// @param execution the operation policy and sinks.
/// @param request the source selection.
/// @param region the rectangle to decode, or null for the whole image.
/// @param options how the adapter reads.
/// @return the frame and its source metadata, or a typed failure.
Result<CodecDecodeResult> readXisf(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, const CodecRegion* region,
                                   const XisfCodecOptions& options);

/// Decodes the whole image, or one region of it, through a reader and header the facade has already checked.
///
/// A region keeps the source's meaning: a filter array's phase moves by the region's offset, and with metadata the
/// solution is cropped to it.
/// @param engine owner of the decoded allocation.
/// @param execution the operation policy and sinks.
/// @param request the source selection.
/// @param region the rectangle to decode, or null for the whole image.
/// @param options how the adapter reads.
/// @param source the open unit the header was read from.
/// @param header the checked description, consumed by this call.
/// @return the frame and its source metadata; `ErrorCode::OutOfRange` for a region that is empty or reaches past the
///         image; or a typed failure.
Result<CodecDecodeResult> readPreparedXisf(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion* region,
                                           const XisfCodecOptions& options, XisfHandle& source, XisfHeader header);

/// Reads the samples of an area of the image into memory laid out for them: rows of exactly the area's width, planes
/// one after the other in the file's own arrangement.
///
/// A whole image is read in one call of the library. A region is read natively, a planar one a channel at a time. An
/// area of whole rows of a block the library reads in parts - uncompressed, or compressed in more than one subblock,
/// and not verified whole - of 32 MiB or more, read by an operation granted eight or more workers, is cut into strips
/// read on those workers through the one reader. Progress counts the area's rows, every plane's.
/// @param execution the operation the read belongs to.
/// @param request the source selection.
/// @param area the rectangle to read, already checked against the image.
/// @param header the image's checked description.
/// @param reader the open unit the header was read from.
/// @param options how the adapter reads.
/// @param samples where the first sample goes.
/// @param layout where each sample goes.
/// @return success; the block's own failure when it is unavailable; the operation's stop; or the library's failure,
///         translated, after which the samples hold nothing to rely on.
Status readXisfPixels(const ExecutionContext& execution, const CodecReadRequest& request, const CodecRegion& area,
                      const XisfHeader& header, const openxisf::reader& reader, const XisfCodecOptions& options,
                      std::byte* samples, const ImageLayout& layout);

/// Whether a region of a block decodes, reads and holds only what covers it: an uncompressed block, or one compressed
/// in more than one subblock, stored apart from the header and not read whole to have its checksum verified.
/// @param block how the image's pixel data are stored.
/// @param options how the adapter reads.
/// @return true when a region read of the block is bounded by a few subblocks or a staging buffer.
bool xisfBlockReadInParts(const openxisf::block_info& block, const XisfCodecOptions& options) noexcept;

/// How many rows the smallest piece of the image a band read decodes whole spans: one for an uncompressed block, read
/// run by run; for one compressed in subblocks, the rows a subblock holds of one stretch of the block - a stretch for
/// each byte of a sample when the bytes are shuffled, the whole block otherwise - so that consecutive bands of that
/// many rows decode each subblock at most twice; and the image's height for a block a band read decodes or verifies
/// whole.
/// @param header the image's checked description, with its block's facts.
/// @param options how the adapter reads.
/// @return at least one and at most the image's height; one for an image whose block is unavailable.
std::uint32_t xisfDecodeGranularityRows(const XisfHeader& header, const XisfCodecOptions& options) noexcept;

/// An upper bound on what the library allocates besides the destination to read an area of the image, by the plan
/// `readXisfPixels` follows for it in this execution.
/// @param execution the operation the read would belong to.
/// @param header the image's checked description, with its block's facts.
/// @param area the rectangle, already checked against the image.
/// @param options how the adapter reads.
/// @return the bound in bytes, saturating; zero for an image whose block is unavailable, which no read reaches.
std::uint64_t xisfReadLibraryBytes(const ExecutionContext& execution, const XisfHeader& header, const CodecRegion& area,
                                   const XisfCodecOptions& options);

/// The metadata of a region of an image whose metadata were read whole: the solution, and the world-coordinate cards
/// beside it, cropped to the region, the image's AstrometricSolution properties going with the cards they describe.
///
/// The distortion the whole image's solution stated stays stated: cropping keeps the sky's mapping, whatever part of
/// it the cards hold.
/// @param metadata the whole image's metadata.
/// @param extent the whole image's extent, which the solution is evaluated against.
/// @param crop the region, inside the image.
/// @return the region's metadata, or whatever cropping the solution or rewriting its cards reported.
Result<ImageMetadata> croppedXisfMetadata(const ImageMetadata& metadata, const ImageExtent& extent,
                                          const CodecRegion& crop);

/// Serializes prepared samples into the request's private pending path.
/// @param engine owner of the encoder workspace.
/// @param execution the operation policy and sinks.
/// @param request the pending target and explicit intent.
/// @param frame the complete described samples.
/// @param metadata the immutable source facts.
/// @return the encoded byte count, or a typed failure.
Result<CodecEncodeReport> writeXisf(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecEncodeRequest& request, const RawFrame& frame,
                                    const ImageMetadata& metadata);

/// Writes a unit into the request's private pending path with its one image reserved - zeros where the samples go, in
/// a block written uncompressed - and opens it in the library's editor, which writes the rows handed to it in place.
/// @param engine the engine whose limits bound the editor's open.
/// @param execution the operation policy and sinks.
/// @param request the pending target and explicit intent, which states no compression.
/// @param descriptor what the image is, its byte order the host's.
/// @param arrangement how its samples are stored, planar or interleaved.
/// @param metadata the immutable source facts.
/// @return the room the rows go into, or a typed failure.
Result<std::unique_ptr<CodecReservedRows>> reserveXisf(const EngineContext& engine, const ExecutionContext& execution,
                                                       const CodecEncodeRequest& request,
                                                       const RawFrameDescriptor& descriptor,
                                                       StorageArrangement arrangement, const ImageMetadata& metadata);

/// Projects the preserved cards of a decoded image into the typed metadata aggregate.
///
/// The FITS keywords state the solution when they describe one. When they describe none, the image's
/// AstrometricSolution is translated into the world-coordinate cards a plate solve adds when the engine's model holds
/// its first layer - a gnomonic projection of ICRS directions, with the celestial pole where the engine's projection
/// puts it - and the cards it gives describe a solution with the rest; each carries `kXisfTranslatedSolutionComment`. A
/// solution stated in more layers than its first is summarised as having a distortion, which the cards do not carry.
/// @param codec the preserved cards and source encoding.
/// @param extent the decoded geometry the astrometric bridge is evaluated against.
/// @param path the source path recorded as lineage.
/// @param properties the image's properties, read for its AstrometricSolution.
/// @return the aggregate, or whatever building one of its parts reported.
Result<ImageMetadata> xisfMetadata(CodecMetadata codec, const ImageExtent& extent, std::string_view path,
                                   const openxisf::property_list& properties);

/// Reads every card the selected image carries: the structural facts it states outside its properties, its FITS
/// keywords, its properties, and the unit's own metadata.
/// @param reader the open unit, whose metadata follows the image's own cards.
/// @param image the described image.
/// @param execution the operation whose cancellation is checked between cards.
/// @param path the file the cards came from, for a refusal.
/// @return the cards in file order, or a typed limit or cancellation failure.
Result<std::vector<CodecCard>> readXisfCards(const openxisf::reader& reader, const openxisf::image_info& image,
                                             const ExecutionContext& execution, std::string_view path);

/// The text a property's card carries, when it fits the bound.
///
/// A string is its own text, a TimePoint ISO 8601 in UTC, and every number - a scalar, the parts of a complex number,
/// the elements of a vector or matrix - its shortest exact decimal, the elements separated by commas and the rows of a
/// matrix by semicolons; a Float128 element is written as its nearest double. The text of a long value is never built
/// past the bound: one of more elements than the bound could hold the separators of is refused before an element is
/// written, and any other stops at the first element past it.
/// @param value the property's value.
/// @param bound the most bytes the text may take.
/// @return the text, or nothing when it would be longer than the bound.
std::optional<std::string> xisfPropertyText(const openxisf::property_value& value, std::size_t bound);

/// The comment a property's card carries: the property's type as the specification names it, a matrix's dimensions as
/// `rows`x`columns` after it, and the property's own comment after a semicolon when it has one, as in
/// `F64Matrix 1000x2` or `String; the observer`.
/// @param property the property.
/// @return the comment.
std::string xisfPropertyComment(const openxisf::property& property);

/// The property a card states, the inverse of `xisfPropertyText` and `xisfPropertyComment`.
///
/// A comment that names no XISF type, or text that is not a value of the type it names, gives a String of the text,
/// with the comment kept as the property's own.
/// @param id the property's identifier, the card's keyword.
/// @param text the card's value, its continuation cards joined.
/// @param comment the card's comment.
/// @return the property.
openxisf::property xisfPropertyOf(std::string id, std::string_view text, std::string_view comment);

/// The engine representation one XISF sample format is.
/// @param format the library's declared representation.
/// @return the boundary format, or nothing for the two complex representations.
std::optional<RawSampleFormat> xisfSampleFormat(openxisf::sample_format format) noexcept;

/// The XISF sample format one engine representation is written as.
/// @param format the boundary format.
/// @return the library's representation, or nothing for one XISF cannot spell.
std::optional<openxisf::sample_format> xisfSampleFormatOf(const RawSampleFormat& format) noexcept;

/// The shortest decimal text that reads back as the same binary64 value.
/// @param value the number.
/// @return its text, or nothing written for a value that has none.
std::string xisfNumberText(double value);

/// The `ColorFilterArray` card's text for a filter array.
/// @param filters the array the file declares.
/// @return `WxH:letters`, with `;name` when the array is named.
std::string xisfFilterArrayText(const openxisf::color_filter_array& filters);

/// The filter array a `ColorFilterArray` card states.
/// @param text the card's value, as `xisfFilterArrayText` writes it.
/// @return the block width, height, letters and name, or nothing when the text is not one.
std::optional<openxisf::color_filter_array> parseXisfFilterArray(std::string_view text);

/// The `RGBWorkingSpace` card's text for a working space.
/// @param space the working space the file declares.
/// @return `gamma;x1,x2,x3;y1,y2,y3;Y1,Y2,Y3;name`, the gamma being `sRGB` for the sRGB function.
std::string xisfWorkingSpaceText(const openxisf::rgb_working_space& space);

/// The working space a `RGBWorkingSpace` card states.
/// @param text the card's value, as `xisfWorkingSpaceText` writes it.
/// @return the working space, or nothing when the text is not one.
std::optional<openxisf::rgb_working_space> parseXisfWorkingSpace(std::string_view text);

/// The `DisplayFunction` card's text for a display function.
/// @param function the display function the file declares.
/// @return the midtones, shadows, highlights, shadows expansion and highlights expansion, four numbers each, then the
///         name, separated by semicolons.
std::string xisfDisplayFunctionText(const openxisf::display_function& function);

/// The display function a `DisplayFunction` card states.
/// @param text the card's value, as `xisfDisplayFunctionText` writes it.
/// @return the display function, or nothing when the text is not one.
std::optional<openxisf::display_function> parseXisfDisplayFunction(std::string_view text);

/// The `Resolution` card's text for a resolution.
/// @param resolution the resolution the file declares.
/// @return `horizontal;vertical;unit`, the unit being `inch` or `cm`.
std::string xisfResolutionText(const openxisf::resolution& resolution);

/// The resolution a `Resolution` card states.
/// @param text the card's value, as `xisfResolutionText` writes it.
/// @return the resolution, or nothing when the text is not one.
std::optional<openxisf::resolution> parseXisfResolution(std::string_view text);
} // namespace an
