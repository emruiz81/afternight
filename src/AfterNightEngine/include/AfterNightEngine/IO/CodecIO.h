// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The codec facade: probe, decode, region decode and encode through an engine context's registry.
///
/// Every call selects one adapter - the request's stated format, or the one its path's extension
/// names - and never falls back to another. Before the adapter runs, the facade checks the engine
/// context, the execution and the path, and that the adapter advertises the operation; afterwards it
/// holds what came back to the engine context's limits and the adapter's capability record. The
/// adapter runs inside the execution's admitted task arena, and whatever it throws becomes a returned
/// failure. Every call is synchronous and borrows its arguments for the call only.
///
/// Nothing converts. A decode returns the container's own representation, byte order, arrangement and
/// orientation, and an encode writes the frame it is given; conversion, repacking and orientation are
/// operations a caller composes. An encode writes a private pending file and never publishes it -
/// `IO/OutputTransaction.h` owns the atomic commit.
///
/// **A registry per read.** An adapter's configuration is fixed when it is built - a camera RAW
/// adapter's full-colour white balance among it - so a caller that reads with more than one
/// configuration names the registry for each read instead of building another engine context. The
/// probe, decode and region overloads that take a `CodecRegistry` select the adapter from it and from
/// nothing else, and are otherwise the calls without one: the engine context's limits and memory, the
/// execution's admission, arena and stop, the same checks, and the same failures.
///
/// **Failures every call shares.** `ErrorCode::NotInitialized` for an engine context or execution
/// that is not valid; `ErrorCode::IncompatibleArguments` for an execution of another engine context;
/// the execution's `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`, checked before the path
/// is examined and again before and after the adapter runs; `ErrorCode::InvalidArgument` for a path
/// that is empty, longer than 131072 bytes, not UTF-8 or holding a NUL, and with
/// `CodecFailure::UnknownFormat` for a format outside the enumeration; `ErrorCode::Unsupported` with
/// `CodecFailure::UnknownFormat` when no format is stated and the extension names none, or with
/// `CodecFailure::Unavailable` when the engine context has no adapter for the format. When the
/// adapter itself fails, the facade returns a failure with the same code and
/// `CodecFailure::CallbackFailure` whose cause is the adapter's failure, carrying the adapter's own
/// reason. An exception is returned as `ErrorCode::AllocationFailed` for `std::bad_alloc` and
/// `ErrorCode::ExternalFailure` for anything else, wrapped the same way when the adapter threw it.

#pragma once

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecRequests.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace an
{
class CodecRegistry;
class EngineContext;
class ExecutionContext;

/// Reads the selected image's description and metadata through its adapter, without decoding pixels.
///
/// A recognised extension alone does not make a file readable; only the adapter's probe does. What it
/// returns is checked: its disposition must be one the request allowed, the container must hold an
/// image at `imageIndex`, the layout must agree with the descriptor, and the geometry must fit the
/// engine context's limits and the adapter's capability record.
///
/// @param engine an initialized engine context; its registry selects the adapter.
/// @param execution a valid execution of that engine context.
/// @param request the source, its stated format or extension hint, and the image to select.
/// @return the source's description; a failure every call shares; `ErrorCode::InvalidData` with
///         `CodecFailure::DescriptorMismatch` for a disposition the request did not allow or a layout
///         that disagrees with the descriptor, with `CodecFailure::InvalidMetadata` for source
///         metadata carrying an output intent, or with no reason for a container that reports no
///         image; `ErrorCode::OutOfRange` for an `imageIndex` past the last image;
///         `ErrorCode::LimitExceeded` for a width, height, channel count, byte size or stride beyond
///         the engine context's limits; or `ErrorCode::Unsupported` with
///         `CodecFailure::UnsupportedRepresentation` for a representation, arrangement or channel
///         count the adapter does not advertise.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecImageInfo>
probeImage(const EngineContext& engine, const ExecutionContext& execution, const CodecReadRequest& request);

/// Decodes the selected image in full, after reading its header through the adapter.
///
/// The header is read first and held to every check `probeImage` makes, so the engine context's limits
/// are enforced before any pixel storage is allocated; the payload is validated by the decode itself,
/// not twice. The frame must then match that description exactly, layout included, and its samples
/// must be charged to this engine context or kept alive by a lifetime token. No narrowing, byte swap,
/// orientation change or output intent is applied. A failed or cancelled decode releases its frame
/// before returning, so a partial frame is never returned.
///
/// @param engine an initialized engine context; it owns the decoded samples.
/// @param execution a valid execution of that engine context.
/// @param request the source selection.
/// @return the frame and its source metadata; a failure every call shares; any failure `probeImage`
///         returns for the header; `ErrorCode::Unsupported` with `CodecFailure::UnsupportedOperation`
///         when the adapter has no decoder; `ErrorCode::InvalidData` with
///         `CodecFailure::DescriptorMismatch` for a frame that differs from its header, with
///         `CodecFailure::UnsafeLifetime` for samples borrowed without a lifetime token, or with
///         `CodecFailure::InvalidMetadata` for metadata carrying an output intent; or
///         `ErrorCode::IncompatibleArguments` with `CodecFailure::ForeignAllocation` for samples charged
///         to another engine context.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecDecodeResult>
decodeImage(const EngineContext& engine, const ExecutionContext& execution, const CodecReadRequest& request);

/// Decodes one rectangle of the selected image through the adapter's native region reader.
///
/// An adapter with no region reader is refused before the header is read, and nothing decodes the
/// whole image and crops it instead. The region is checked against the header's extent without
/// overflow. The frame keeps every channel, the numeric meaning, byte order and origin, and a filter
/// array's phase moves by the region's offset; it must match that cropped description, and its layout
/// may carry padding.
///
/// @param engine an initialized engine context; it owns the decoded samples.
/// @param execution a valid execution of that engine context.
/// @param request the source selection.
/// @param region a nonempty rectangle inside the image, in the source's row order.
/// @return the region's frame and source metadata; every failure `decodeImage` returns, where
///         `ErrorCode::Unsupported` with `CodecFailure::UnsupportedOperation` also covers an adapter
///         with no region reader; or `ErrorCode::OutOfRange` for an empty region or one that reaches
///         past the image.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecDecodeResult> decodeImageRegion(const EngineContext& engine,
                                                                               const ExecutionContext& execution,
                                                                               const CodecReadRequest& request,
                                                                               const CodecRegion& region);

/// Reads the selected image's description through the adapter `registry` holds for its format.
///
/// `probeImage` in every other respect. The registry replaces the engine context's for this call and is
/// never merged with it: a format it has no adapter for is refused before the path is examined by any
/// adapter, even when the engine context's registry has one. The registry is borrowed for the call and
/// may be released as soon as it returns. It brings no memory, threads or limits of its own - the
/// engine context and the execution supply all three - so a caller holding several registries, one per
/// configuration, still has one budget.
///
/// @param engine an initialized engine context; its limits bind the description.
/// @param execution a valid execution of that engine context; the adapter runs in its arena.
/// @param registry the adapters to select from, borrowed for the call.
/// @param request the source, its stated format or extension hint, and the image to select.
/// @return every answer `probeImage` gives, where `ErrorCode::Unsupported` with
///         `CodecFailure::Unavailable` means that `registry` has no adapter for the format.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecImageInfo> probeImage(const EngineContext& engine,
                                                                     const ExecutionContext& execution,
                                                                     const CodecRegistry& registry,
                                                                     const CodecReadRequest& request);

/// Decodes the selected image in full through the adapter `registry` holds for its format.
///
/// `decodeImage` in every other respect, with the registry chosen as `probeImage` with a registry
/// chooses it: the samples are charged to the engine context, the adapter runs in the execution's arena
/// and observes its stop, and a decoded frame does not depend on the registry, which may be released
/// while the frame is still held.
///
/// @param engine an initialized engine context; it owns the decoded samples.
/// @param execution a valid execution of that engine context.
/// @param registry the adapters to select from, borrowed for the call.
/// @param request the source selection.
/// @return every answer `decodeImage` gives, where `ErrorCode::Unsupported` with
///         `CodecFailure::Unavailable` means that `registry` has no adapter for the format.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecDecodeResult> decodeImage(const EngineContext& engine,
                                                                         const ExecutionContext& execution,
                                                                         const CodecRegistry& registry,
                                                                         const CodecReadRequest& request);

/// Decodes one rectangle of the selected image through the adapter `registry` holds for its format.
///
/// `decodeImageRegion` in every other respect, with the registry chosen as `probeImage` with a registry
/// chooses it.
///
/// @param engine an initialized engine context; it owns the decoded samples.
/// @param execution a valid execution of that engine context.
/// @param registry the adapters to select from, borrowed for the call.
/// @param request the source selection.
/// @param region a nonempty rectangle inside the image, in the source's row order.
/// @return every answer `decodeImageRegion` gives, where `ErrorCode::Unsupported` with
///         `CodecFailure::Unavailable` means that `registry` has no adapter for the format.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecDecodeResult>
decodeImageRegion(const EngineContext& engine, const ExecutionContext& execution, const CodecRegistry& registry,
                  const CodecReadRequest& request, const CodecRegion& region);

/// One image held open for decoding it a band of whole rows at a time into memory the caller holds.
///
/// A stack that streams its frames tile by tile reads each frame a band at a time, many times over.
/// Opened once, a reader checks the header once and keeps the source open, so a band costs neither a
/// reopen nor another header read, and the operating system sees one reader moving forward through
/// the file, which its read-ahead answers. A band is decoded into the caller's memory - a stack's own
/// tile - so it is neither allocated nor copied on its way there. An adapter that cannot decode rows
/// into caller memory is read through `decodeImageRegion` and the band copied, so the answer is the
/// same either way.
///
/// Only an adapter with a region reader opens one, and the samples are the adapter's own: the
/// representation, byte order and arrangement `decodeImageRegion` would return, rows of exactly the
/// width and nothing between them. Move-only. One reader serves one thread at a time; readers of the
/// same file are independent. The engine context must outlive the reader.
class ImageRowReader
{
public:
    /// A reader that holds no image.
    AFTERNIGHTENGINE_API ImageRowReader() noexcept;

    /// Closes the image.
    AFTERNIGHTENGINE_API ~ImageRowReader();

    /// Takes the other reader's image, leaving it holding none.
    /// @param other the reader to take from.
    AFTERNIGHTENGINE_API ImageRowReader(ImageRowReader&& other) noexcept;

    /// Closes this reader's image and takes the other's.
    /// @param other the reader to take from.
    /// @return this reader.
    AFTERNIGHTENGINE_API ImageRowReader& operator=(ImageRowReader&& other) noexcept;

    /// Copying is deleted: a reader holds an open source.
    ImageRowReader(const ImageRowReader&) = delete;

    /// Copying is deleted. See the copy constructor.
    ImageRowReader& operator=(const ImageRowReader&) = delete;

    /// Opens the selected image and reads its header, held to every check `probeImage` makes.
    ///
    /// @param engine an initialized engine context; it owns the reader's workspaces and must outlive
    ///        the reader.
    /// @param execution a valid execution of that engine context.
    /// @param request the source selection; its path is copied.
    /// @return the open reader; a failure every call shares; any failure `probeImage` returns for the
    ///         header; or `ErrorCode::Unsupported` with `CodecFailure::UnsupportedOperation` when the
    ///         adapter has no region reader.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ImageRowReader>
    open(const EngineContext& engine, const ExecutionContext& execution, const CodecReadRequest& request);

    /// Opens the selected image through the adapter `registry` holds for its format.
    ///
    /// `open` in every other respect, with the registry chosen as `probeImage` with a registry chooses
    /// it: the header and every band are read through that adapter and through nothing of the engine
    /// context's.
    ///
    /// @param engine an initialized engine context; it owns the reader's workspaces and must outlive
    ///        the reader.
    /// @param execution a valid execution of that engine context.
    /// @param registry the adapters to select from, borrowed for the reader's life: it must outlive the
    ///        reader.
    /// @param request the source selection; its path is copied.
    /// @return every answer `open` gives, where `ErrorCode::Unsupported` with
    ///         `CodecFailure::Unavailable` means that `registry` has no adapter for the format.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ImageRowReader> open(const EngineContext& engine,
                                                                          const ExecutionContext& execution,
                                                                          const CodecRegistry& registry,
                                                                          const CodecReadRequest& request);

    /// Whether this reader holds an open image.
    /// @return true after a successful `open`, until the image is moved away.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// The open image's description, as `probeImage` reports it.
    /// @return the description, borrowed for the reader's life, or null for a reader that holds none.
    [[nodiscard]] AFTERNIGHTENGINE_API const CodecImageInfo* info() const noexcept;

    /// Where `readRows` puts a band of `rows` rows: every channel, rows of exactly the width, planes
    /// one after another for a planar adapter and pixels together for an interleaved one.
    /// @param rows how many rows, at least one and at most the image's height.
    /// @return the layout; `ErrorCode::NotInitialized` for a reader that holds no image; or
    ///         `ErrorCode::OutOfRange` for a count outside the image.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayout> rowsLayout(std::uint32_t rows) const;

    /// How many rows the smallest piece of the image that a band read decodes whole spans.
    ///
    /// Consecutive bands of at least this many rows decode each stored byte at most about twice; narrower bands
    /// decode a piece again for every band that reaches it, so a caller that streams a frame band by band sizes its
    /// bands to this where its memory allows (`StackExecutionRequest::sourceGranularityRows`). One for a payload read
    /// as it is stored and for an adapter that decodes no rows into caller memory; for an XISF block compressed in
    /// subblocks, the rows a subblock holds of one stretch of the block, each byte of a sample in a stretch of its own
    /// when the bytes are shuffled; and the image's height where every band decodes or verifies the whole payload.
    ///
    /// @return at least one and at most the image's height; zero for a reader that holds no image.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t decodeGranularityRows() const noexcept;

    /// Decodes `rows` whole rows from `firstRow` into `destination`, laid out as `rowsLayout(rows)`.
    ///
    /// @param execution a valid execution of the reader's engine context.
    /// @param firstRow the first row, in the source's row order.
    /// @param rows how many rows, at least one.
    /// @param destination at least `rowsLayout(rows)->totalBytes()` bytes; what it held is replaced, and
    ///        after a failure it holds nothing to rely on.
    /// @return the layout written; `ErrorCode::NotInitialized` for a reader that holds no image or an
    ///         execution that is not valid; `ErrorCode::IncompatibleArguments` for an execution of another
    ///         engine context or a destination shorter than the band; `ErrorCode::OutOfRange` for rows
    ///         outside the image; the execution's `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`;
    ///         or the adapter's failure, as `decodeImageRegion` returns it.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayout> readRows(const ExecutionContext& execution,
                                                                    std::uint32_t firstRow, std::uint32_t rows,
                                                                    std::span<std::byte> destination);

private:
    /// Opens the selected image through `registry`, or through the engine context's when it is null,
    /// inside the caller's exception boundary.
    ///
    /// @param engine the reader's engine context.
    /// @param execution a valid execution of that engine context.
    /// @param registry the adapters to select from, or null for the engine context's.
    /// @param request the source selection.
    /// @return the open reader, or what `open` refuses.
    [[nodiscard]] static Result<ImageRowReader> openThrough(const EngineContext& engine,
                                                            const ExecutionContext& execution,
                                                            const CodecRegistry* registry,
                                                            const CodecReadRequest& request);

    /// What an open reader keeps.
    struct State;
    /// The open image, or null.
    std::unique_ptr<State> m_state;
};

/// Encodes a frame into the request's private pending path, without publishing it.
///
/// The adapter runs only after the frame has passed every check: it is valid, it fits the engine
/// context's limits, its representation, arrangement and channel count are ones the adapter encodes,
/// and a stated output intent already describes its sample type and byte order. Nothing is
/// converted. A failed encode may leave partial bytes at the pending path; the output transaction is
/// what commits a pending file atomically and removes what a failure left.
///
/// @param engine an initialized engine context; it owns the encoder's workspaces.
/// @param execution a valid execution of that engine context.
/// @param request the pending path, the format, and the explicit output intent; the source metadata's
///        own intent is never used.
/// @param frame the complete samples, borrowed for the call and not modified.
/// @param metadata the source facts to write beside them.
/// @return the number of bytes written, which commits nothing; a failure every call shares, the path
///         being `pendingPath`; `ErrorCode::Unsupported` with `CodecFailure::UnsupportedOperation` when
///         the adapter has no encoder; `ErrorCode::InvalidArgument` for a frame that is not valid;
///         `ErrorCode::InvalidData` with `CodecFailure::DescriptorMismatch` for a layout that disagrees
///         with the frame's descriptor; `ErrorCode::LimitExceeded` for a frame beyond the engine
///         context's limits; `ErrorCode::Unsupported` with `CodecFailure::UnsupportedRepresentation`
///         for a representation, arrangement or channel count the adapter does not encode, or for a
///         blank header prolog asked of a container other than XISF; `ErrorCode::InvalidArgument` for a
///         header prolog outside its enumeration; or
///         `ErrorCode::IncompatibleArguments` with `CodecFailure::UnsupportedRepresentation` for an
///         intent whose sample type or byte order differs from the frame's, or that asks for
///         `OutputScaling::NormalizeFullRange`, a data-dependent normalization the caller applies
///         before encoding.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecEncodeReport>
encodeImage(const EngineContext& engine, const ExecutionContext& execution, const CodecEncodeRequest& request,
            const RawFrame& frame, const ImageMetadata& metadata);
} // namespace an
