// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image-list manifest, and the sink that writes a list.
///
/// An image list is a sequence whose frames are separate image files. Its manifest - an `.anseq` XML document -
/// names them in order beside the list's creation time, its first frame's geometry and its frame rate. A list's
/// frames are written into a directory named after the manifest, the manifest is written last, and a manifest is
/// read back into the paths its image list decodes. This is that document and that layout, with the reading and
/// the writing made explicit.
///
/// **The bytes are fixed.** A manifest is written in one fixed XML formatting, and its attributes are read by one
/// fixed set of rules - a number stops at the first character that is not part of it, an attribute's tabs and line
/// breaks become spaces, and the named entities and numeric character references are decoded - so a manifest reads
/// the same whoever wrote it. No XML library is linked, because the grammar is small enough to state, and it
/// bounds nesting at a depth no manifest needs.
///
/// **A manifest is published, never overwritten in place.** Writing one goes through an `IO/SequenceOutput.h`
/// output, so a reader never finds half of one, and the sink publishes each frame through
/// `IO/OutputTransaction.h` before the manifest that names it exists.
///
/// **The list is the caller's.** A manifest names paths and states nothing a frame does not state for itself
/// when it is decoded. `resolveImageListFramePath` turns each spelled path into one `createImageListSource`
/// opens, and editing a list is editing `ImageListManifest::framePaths` before it is written.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/FrameSink.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

class EngineContext;
class ExecutionContext;

/// The extension a manifest is written with, dot included. A manifest's own name without it is the list's
/// stem, which names the list's frames directory and its unnamed frames.
inline constexpr std::string_view kImageListManifestExtension = ".anseq";

/// The manifest version this engine reads and writes.
inline constexpr std::uint32_t kImageListManifestVersion = 1u;

/// The most frames one manifest lists, which is the most `createImageListSource` opens.
inline constexpr std::uint64_t kMaxImageListManifestFrames = 1048576u;

/// The largest manifest document this engine reads, in bytes.
inline constexpr std::uint64_t kMaxImageListManifestBytes = 134217728u;

/// What an image-list manifest states.
///
/// A plain aggregate. The geometry describes the list's first frame for a reader of the manifest; nothing in
/// this engine reads it back as a fact about the frames, which state their own when they are decoded.
struct ImageListManifest
{
    /// When the list was created, in seconds since the Unix epoch: the `timestamp` attribute.
    std::uint64_t timestampSeconds = 0u;

    /// The first frame's width: the `width` attribute.
    std::uint64_t width = 0u;

    /// The first frame's height: the `height` attribute.
    std::uint64_t height = 0u;

    /// The first frame's channel count: the `channels` attribute.
    std::uint64_t channels = 0u;

    /// Whether the first frame is a mosaic: the `isCFA` attribute.
    bool mosaiced = false;

    /// The frame rate in frames per second: the `fps` attribute, written with four significant digits.
    double framesPerSecond = 0.0;

    /// The frames in order, each spelled as the manifest spells it: relative to the manifest's directory, or
    /// absolute.
    std::vector<std::string> framePaths{};

    /// Whether two manifests state the same list.
    ///
    /// @param left one manifest.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend bool operator==(const ImageListManifest& left, const ImageListManifest& right) = default;
};

/// Reads a manifest from disk.
///
/// The document is read with a small XML grammar: it may begin with a UTF-8 byte-order mark,
/// its first NUL byte ends it, and it holds elements, attributes, comments, processing instructions, character
/// data, CDATA sections and a top-level document type declaration, which is skipped. Elements nest at most 4,096
/// deep. The first top-level element named `sequence` is the manifest. A `version` attribute, when present, must
/// read as 1. The other attributes read as follows, and as zero or false when absent:
/// `timestamp`, `width`, `height` and `channels` as unsigned integers - leading whitespace skipped, a `0x` prefix
/// hexadecimal, a negative value zero and a value beyond 64 bits the largest one; `fps` as the C library's
/// `strtod` reads it in the C locale; and `isCFA` as true when its first character is `1`, `t`, `T`, `y` or `Y`.
/// Each `frame` child of the manifest, in document order, must carry a `path` that decodes to nonempty UTF-8
/// without NUL; an element nested more deeply is not a frame.
///
/// The file is read into memory from the engine context in bounded chunks, with cancellation checked between
/// them, and released before return.
///
/// @param engine the context whose memory holds the document while it is read.
/// @param execution an operation of that context.
/// @param path the manifest's UTF-8 path; borrowed for this call.
/// @return the manifest; `ErrorCode::NotInitialized` for an empty context; `ErrorCode::IncompatibleArguments`
///         for an execution of another context; `ErrorCode::InvalidArgument` naming `path` when it is empty,
///         not UTF-8, holds a NUL or exceeds the path bound; `ErrorCode::ExternalFailure` with
///         `SequenceFailure::IoFailure` carrying the operating system's code and the path;
///         `ErrorCode::LimitExceeded` with `SequenceFailure::UnsupportedEncoding` naming `path` for a file
///         larger than `kMaxImageListManifestBytes` or `frame` for more than `kMaxImageListManifestFrames`
///         frames; `ErrorCode::InvalidData` with `SequenceFailure::MalformedContainer` naming `document` for
///         text that is not a well-formed document of that grammar, `sequence` when it holds no manifest, or
///         `path` with the zero-based frame as `ContextKey::Requested` for a frame whose path is absent, empty,
///         not UTF-8, holds a NUL or exceeds the path bound; `ErrorCode::InvalidData` with
///         `SequenceFailure::TruncatedContainer` for a file that shrank while it was read;
///         `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding` naming `version`; a memory-budget
///         refusal; or the execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageListManifest>
readImageListManifest(const EngineContext& engine, const ExecutionContext& execution, std::string_view path);

/// Publishes a manifest at a destination, whole or not at all.
///
/// The document is an XML declaration and the `sequence` element with its attributes in the order `version`,
/// `timestamp`, `width`, `height`, `channels`, `isCFA` and `fps` - the rate as `printf`'s `%.4g` spells it -
/// holding one tab-indented, self-closed `frame` element per path; each line ends in a line feed, a list with
/// no frame is one self-closed element, and attribute text escapes `&`, `<`, `"` and every control character.
/// It is written through a bounded buffer into the pending payload of an `IO/SequenceOutput.h` output over the
/// request, which is then committed. Nothing is reserved for a manifest this function refuses.
///
/// @param engine the context the output is reserved for.
/// @param execution an operation of that context; its cancellation is checked between buffers.
/// @param request where the manifest is published and how; borrowed for this call.
/// @param manifest the list to write; borrowed for this call.
/// @return the committed output's report; `ErrorCode::InvalidArgument` naming `framePaths` with the zero-based
///         frame as `ContextKey::Requested` for a path that is empty, not UTF-8, holds a NUL or exceeds the path
///         bound, or naming `framesPerSecond` for a rate that is not finite; `ErrorCode::LimitExceeded` naming
///         `framePaths` for more than `kMaxImageListManifestFrames` paths; what `SequenceOutput::begin`,
///         `SequenceOutput::claimWriter` and `SequenceOutput::commit` refuse; `ErrorCode::ExternalFailure` with
///         `SequenceFailure::IoFailure` carrying the operating system's code; a memory-budget refusal; or the
///         execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SequenceOutputReport>
writeImageListManifest(const EngineContext& engine, const ExecutionContext& execution,
                       const SequenceOutputRequest& request, const ImageListManifest& manifest);

/// Where one of a manifest's frames is.
///
/// A frame path that is absolute on this platform is the answer. A path with only a root name or only a root
/// directory is refused rather than being joined into a different spelling. Any unrooted path is appended to the
/// manifest path's directory - everything up to and including its last separator of either kind - so a manifest
/// named without a directory leaves the frame path as it is spelled. Dot components are not normalized: `..` may
/// deliberately name a frame outside the manifest's directory. Nothing is opened.
///
/// Thread-safe; allocates the returned path.
///
/// @param manifestPath the manifest's UTF-8 path.
/// @param framePath the frame's path as the manifest spells it.
/// @return the UTF-8 path; `ErrorCode::InvalidArgument` naming `manifestPath` or `framePath` when it is empty,
///         not UTF-8, holds a NUL, exceeds the path bound or has only a root name or root directory; or
///         `ErrorCode::LimitExceeded` naming `framePath` when the joined path exceeds the path bound.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::string> resolveImageListFramePath(std::string_view manifestPath,
                                                                                 std::string_view framePath);

/// The directory a list's own frames are written to.
///
/// The manifest path without its `.anseq` extension, followed by `-frames`. Nothing is created.
///
/// Thread-safe; allocates the returned path.
///
/// @param manifestPath the manifest's UTF-8 path, whose last component must end in `.anseq`, compared without
///        regard to ASCII case, after a nonempty stem.
/// @return the UTF-8 directory path; `ErrorCode::InvalidArgument` naming `manifestPath` when it is malformed or
///         names no manifest; or `ErrorCode::LimitExceeded` naming `manifestPath` when the directory's path
///         exceeds the path bound.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::string> imageListFramesDirectory(std::string_view manifestPath);

/// Removes an image list from disk.
///
/// The frames directory, with everything below it, when it is a directory - a symbolic link to one is removed
/// and its target is not - and then the manifest when it exists. A list with neither is removed already, and
/// nothing else is touched: frames a manifest names outside its frames directory stay where they are.
///
/// Thread-safe for distinct lists.
///
/// @param manifestPath the manifest's UTF-8 path, naming a manifest as `imageListFramesDirectory` requires.
/// @return success; what `imageListFramesDirectory` refuses; or `ErrorCode::ExternalFailure` with
///         `SequenceFailure::IoFailure` carrying the operating system's code and the path it refused.
[[nodiscard]] AFTERNIGHTENGINE_API Status removeImageListFromDisk(std::string_view manifestPath);

/// What an image-list sink writes and how.
///
/// Borrowed by `createImageListSink`, which copies it, names included.
struct ImageListSinkRequest
{
    /// The container each frame is written as. FITS is the default.
    CodecFormat frameFormat = CodecFormat::Fits;

    /// The intent each frame is written under, in the representation that frame already has: the writer converts
    /// nothing, so a frame the intent would convert is refused unwritten as `ErrorCode::IncompatibleArguments`, which
    /// fails the sink. Absent writes each frame through the representation-preserving encoding
    /// `IO/OutputTransaction.h` describes, which carries no cards.
    std::optional<OutputIntent> frameIntent{};

    /// Whether each frame is flushed to the device before it is published.
    OutputDurability frameDurability = OutputDurability::FlushBeforePublish;

    /// When the list was created, in seconds since the Unix epoch. The caller states it, because the clock
    /// is the host's.
    std::uint64_t timestampSeconds = 0u;

    /// Each frame's file name, without directory or extension, in append order. A frame past the end of the
    /// span - every frame, when it is empty - is named after the list and its zero-based index,
    /// `<stem>-<index>`. A repeated name replaces the earlier frame's file, and the manifest names it twice.
    std::span<const std::string> frameNames{};
};

/// Opens a sink that writes frames as image files and an image-list manifest over an output.
///
/// The output's destination must name a manifest, as `imageListFramesDirectory` requires. Appending frame `i`
/// publishes it through `writeImage` at `<stem>-frames/<name><extension>` beside the destination - the first
/// extension `codecExtensions` lists for the frame format - replacing a file of that name and creating the
/// frames directory when it does not exist; an append reports no container bytes, because the manifest is the
/// container and nothing is written to it before the sink finishes. Finishing writes the manifest into the
/// output's pending payload - the first frame's geometry, the request's time, the descriptor's nominal frame
/// rate or zero, and each frame's path relative to the destination's directory - and completes the claim,
/// after which the caller commits the output. Closing or destroying an unfinished sink removes the frames it
/// published, and the frames directory when it created it and nothing else is in it, and returns the payload
/// to the output unpublished. After a successful finish the frames are the caller's:
/// `removeImageListFromDisk` removes a list whose output was aborted instead of committed.
///
/// @param engine the context the output was reserved for, whose codecs write the frames.
/// @param execution an operation of that context.
/// @param output a valid, uncommitted output of the same context with no writer; claimed until the sink
///        finishes or closes.
/// @param descriptor the list's description: `SequenceFormat::ImageList`, with no seek advertised and no
///        timestamps required; copied.
/// @param request what to write and how; copied.
/// @return an open sink; `ErrorCode::InvalidArgument` naming `frameFormat` for a value outside its enumeration,
///         `CodecFormat::Unknown` or a format with no extension, `frameDurability` for a value outside its
///         enumeration, `destinationPath` for an output whose destination names no manifest, `frameNames` with
///         the zero-based name as `ContextKey::Requested` for a name that is empty, not UTF-8, holds a NUL or
///         a separator or is `.` or `..`, or, with `SequenceFailure::InvalidDescriptor`, the descriptor field at
///         fault; `ErrorCode::Unsupported` naming `frameFormat` when no codec in the context encodes it, or with
///         `SequenceFailure::UnsupportedEncoding` naming `timestampsRequired`; `ErrorCode::LimitExceeded` naming
///         `frameCount` for more frames than one manifest lists; `ErrorCode::InvalidState` for an output that
///         holds nothing or already has a writer; `ErrorCode::IncompatibleArguments` for an output or execution
///         of another context; `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure` when the payload
///         cannot be opened; a memory-budget refusal; or the execution's cancellation. An append refuses a
///         frame whose path would exceed the path bound with `ErrorCode::LimitExceeded` naming `frameNames`,
///         and otherwise fails with what `writeImage` or the frames directory's creation refuses.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSink>
createImageListSink(const EngineContext& engine, const ExecutionContext& execution, SequenceOutput& output,
                    const SequenceDescriptor& descriptor, const ImageListSinkRequest& request);

} // namespace an
