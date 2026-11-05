// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A versioned snapshot of one image: its exact samples, its complete typed metadata, its validity mask and the
/// artifact identity its caller gave it, written whole or not at all.
///
/// An undo history keeps the state an image had before an edit, and a state is more than samples: what the image
/// means, where it points, what its source said and which of its samples count. This container holds all of it
/// in one file, so a history step outlives the session that took it. It is separate from the developer dump of
/// `IO/DumpCodec.h` on purpose: that format stores samples and a description and refuses anything else, and stays
/// exactly as it is.
///
/// **The format is versioned and checked rather than trusted.** A 256-byte little-endian header states a magic, a
/// format version, every section's offset, length and checksum, the frame's description and packed layout, and a
/// checksum of itself; the metadata, the samples and the validity mask follow it with nothing between them. A
/// reader recomputes every length from the validated geometry, requires the file's size to be exactly the header's
/// total, checks every checksum, and refuses a version it does not read before it reads anything else. Memory for
/// the metadata, the samples and the mask comes from the engine context and is refused there like any other
/// allocation, so a damaged length cannot make a read allocate what the file does not hold.
///
/// **A write publishes a whole snapshot or none.** It is written to a private pending sibling - the one
/// `IO/OutputTransaction.h` writes, ending in `kPendingOutputSuffix`, so `sweepPendingOutputs` recovers what a
/// crash left - its size is proved, it is flushed when asked, and it is renamed over the destination. A failure
/// or a stop at any point removes the pending file and leaves the destination as it was.
///
/// **The identity is the caller's.** A snapshot records the `ArtifactIdentity` it was written for and gives it
/// back on every read, so a caller can tell the snapshot it asked for from another one at the same path; the
/// engine never derives it, and a document's names, paths and timeline stay in the caller's own records.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace an
{
class EngineContext;
class ExecutionContext;

/// The snapshot format version this build writes. It reads every version from 1 to this one: version 2 states three
/// more facts of an output intent than version 1 - its byte shuffling, compression block size and checksum - and an
/// intent read from a version 1 file states none of them.
inline constexpr std::uint32_t kImageSnapshotFormatVersion = 2u;

/// The largest metadata section a snapshot holds, in bytes.
///
/// Far beyond any real image's metadata - a header of a few hundred cards and a lineage of a few dozen entries
/// encodes in kilobytes - and far below what the metadata model's own bounds would admit, so a write refuses
/// metadata past it rather than a read allocating it.
inline constexpr std::uint64_t kMaxImageSnapshotMetadataBytes = 64u * 1024u * 1024u;

/// Where and how a snapshot is written.
struct ImageSnapshotWriteRequest
{
    /// The destination, as UTF-8; nonempty, without NUL and within the engine's path bound.
    std::string_view destinationPath;

    /// The identity the snapshot is written for and gives back on every read. Required; borrowed for the call.
    const ArtifactIdentity* identity = nullptr;

    /// Whether the pending file is flushed to stable storage before it is published.
    OutputDurability durability = OutputDurability::FlushBeforePublish;

    /// What happens when the destination already exists.
    OutputReplacePolicy replacePolicy = OutputReplacePolicy::ReplaceExisting;
};

/// What a published snapshot write did.
struct ImageSnapshotWriteReport
{
    /// The published file's size in bytes.
    std::uint64_t bytesWritten = 0u;

    /// Whether the file was flushed to stable storage before it was published.
    bool flushed = false;

    /// Whether the destination existed and was replaced.
    bool replacedExisting = false;
};

/// What a snapshot file states about itself, read without its samples.
struct ImageSnapshotInfo
{
    /// The format version the file was written in.
    std::uint32_t formatVersion;

    /// The frame's description, its byte order the host's.
    RawFrameDescriptor descriptor;

    /// The packed layout the samples are stored and read back in.
    ImageLayout layout;

    /// How many bit-planes the stored validity mask has: zero for none, one for a mask every channel shares, or
    /// the frame's channel count.
    std::uint32_t validityPlanes;

    /// The file's size in bytes.
    std::uint64_t fileBytes;

    /// The identity the snapshot was written for.
    ArtifactIdentity identity;
};

/// Everything a snapshot holds, read back.
struct ImageSnapshotContents
{
    /// The samples, bit for bit as written, in engine memory of the reading context, packed, in the host's byte
    /// order and adoptable by `Image::adopt` without a copy.
    RawFrame frame;

    /// The metadata, equal to what was written.
    ImageMetadata metadata;

    /// The validity mask, when one was written; its pad bits are zero.
    std::optional<ValidityMask> validity;

    /// The identity the snapshot was written for.
    ArtifactIdentity identity;
};

/// Writes an image's snapshot and publishes it at the destination.
///
/// The frame is one of the eight resident sample types, little-endian or in the host's order, in any layout; its
/// row and plane padding is not stored. Every member of the metadata is stored, the output intent's presence
/// included. A validity mask is stored when one is given, and one must be given when the metadata's validity rule
/// is `ValidityRule::ExplicitMask`; it covers the frame's width and height with one bit-plane or one per channel.
/// The pending file's size is checked against what was written before it is flushed and published; transfers are
/// at most 4 MiB, with a cancellation check before each, and the execution's cancellation is also checked before
/// the publishing rename and between its retries.
///
/// @param engine the context whose memory stages the write and whose lease is held for its length.
/// @param execution the operation the write belongs to; its engine context must be `engine`.
/// @param request where and how to write.
/// @param frame the samples and their description; only read.
/// @param metadata the metadata to store; only read.
/// @param validity the validity mask to store, or null when there is none; only read.
/// @return what was written; `ErrorCode::NotInitialized` for an engine or execution that holds nothing, or a frame
///         or mask that does; `ErrorCode::IncompatibleArguments` for an execution of another context, a mask that
///         does not cover the frame, or a missing mask the validity rule needs; `ErrorCode::InvalidArgument` for a
///         destination that is not a bounded UTF-8 path, a null identity, an unknown durability or replace policy,
///         or a frame in big-endian order; `ErrorCode::Unsupported` for a frame whose samples are not one of the
///         eight resident types; `ErrorCode::LimitExceeded` for metadata beyond `kMaxImageSnapshotMetadataBytes`
///         or a pending path beyond the path bound; `ErrorCode::InvalidState` with `CodecFailure::ExistingDestination`
///         when the destination exists and the request refuses to replace it; `ErrorCode::Busy` with
///         `CodecFailure::PendingConflict` when the pending sibling exists; `ErrorCode::ExternalFailure` with
///         `CodecFailure::DestinationUnavailable`, `ShortWrite`, `FlushFailure` or `PublishFailure` for the
///         operating system's refusals; the memory resource's refusal; or the execution's cancellation. On every
///         failure nothing is published and no pending file remains.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageSnapshotWriteReport>
writeImageSnapshot(const EngineContext& engine, const ExecutionContext& execution,
                   const ImageSnapshotWriteRequest& request, const RawFrame& frame, const ImageMetadata& metadata,
                   const ValidityMask* validity = nullptr);

/// Reads what a snapshot states about itself: its version, description, layout, mask planes and identity.
///
/// Reads and checks the header and the metadata section, checksums included, and never the samples, so a file
/// whose samples are damaged still probes; `readImageSnapshot` is what proves them.
///
/// @param engine the context whose limits the description is checked against and whose memory holds the
///        metadata section while it is checked.
/// @param execution the operation the read belongs to; its engine context must be `engine`.
/// @param path the snapshot, as UTF-8.
/// @return the information; or the refusal `readImageSnapshot` documents for the header and the metadata.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageSnapshotInfo>
probeImageSnapshot(const EngineContext& engine, const ExecutionContext& execution, std::string_view path);

/// Reads a snapshot whole.
///
/// The header is read and checked first: its magic, its version, its own checksum, every offset and length
/// against the geometry it states, its reserved bytes and the file's exact size. Then the metadata section is
/// read, checksummed and decoded, every object rebuilt through its own `create` so a value the metadata model
/// refuses is refused here too; then the samples, in transfers of at most 4 MiB with a cancellation check before
/// each, into memory taken from the engine context, and their checksum; then the mask, whose pad bits must be
/// zero. Nothing is returned until all of it has passed.
///
/// @param engine the context whose memory holds what is read and whose limits the description is checked against.
/// @param execution the operation the read belongs to; its engine context must be `engine`.
/// @param path the snapshot, as UTF-8.
/// @return the contents; `ErrorCode::NotInitialized` for an engine or execution that holds nothing;
///         `ErrorCode::IncompatibleArguments` for an execution of another context; `ErrorCode::InvalidArgument`
///         for a path that is not bounded UTF-8; `ErrorCode::ExternalFailure` with `CodecFailure::MetadataUnreadable`
///         when the file cannot be opened; `ErrorCode::Unsupported` with `CodecFailure::MetadataUnreadable` for a
///         snapshot of a version this build does not read; `ErrorCode::InvalidData` with
///         `CodecFailure::MetadataUnreadable` for a file that is not a snapshot, a damaged header, a metadata
///         section that fails its checksum or does not decode, or a file whose size is not the header's total, and
///         with `CodecFailure::PayloadUnreadable` for samples or a mask that fail their checksum, a mask with a pad
///         bit set, or a read that comes up short; `ErrorCode::LimitExceeded` for a geometry beyond the context's
///         limits; the memory resource's refusal; or the execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageSnapshotContents>
readImageSnapshot(const EngineContext& engine, const ExecutionContext& execution, std::string_view path);

} // namespace an
