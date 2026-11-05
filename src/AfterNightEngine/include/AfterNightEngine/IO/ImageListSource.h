// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A frame source over an ordered list of still images, read through the codec facade.
///
/// The list is the caller's: each entry is a `CodecReadRequest`, with its own format, image index,
/// metadata and payload choices, and nothing here parses a manifest, demosaics, converts, infers
/// timing or works in the background - `IO/ImageListManifest.h` turns a manifest into such a list. A
/// load mode decides when frames are decoded and whether they are kept: never kept, all decoded up
/// front, or kept in a least-recently-used cache under a byte budget of its own, enforced beside the
/// engine context's memory budget.

#pragma once

#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FrameSource.h>

#include <span>

namespace an
{
/// When an image list decodes its frames and whether it keeps them; every mode keeps each frame's
/// representation as the codec returned it.
enum class ImageListLoadMode : std::uint8_t
{
    Lazy, ///< Nothing is decoded at creation, and every read decodes again; nothing is kept.
    /// The whole list is decoded at creation, and creation fails when it does not fit the cache budget.
    Eager,
    /// A read the cache misses decodes and keeps the frame, evicting the least recently used ones; a
    /// frame larger than the whole budget passes through without being kept.
    Cached
};

/// How an image-list source loads and what it holds its frames to; copied into the source.
struct ImageListSourceOptions
{
    ImageListLoadMode mode = ImageListLoadMode::Lazy; ///< The load mode; a value outside the enumeration is refused.
    std::uint64_t cacheBudgetBytes =
        0u; ///< The bytes kept frames may hold, padding included: zero for `Lazy`, positive otherwise.
    std::optional<RawFrameDescriptor>
        fixedFrame; ///< The description every frame must have, or absent to allow frames that differ.
};

/// Opens an ordered list of still images as a source that can seek to any index.
///
/// Every path is made absolute and copied at creation, without checking that the file exists; a path
/// that repeats is a separate entry. In `Lazy` and `Cached` modes a file's problems appear at its first
/// read that the cache does not answer; `Eager` mode reads every file at creation and fails as a whole.
/// A read that fails for one entry returns a failure with the same code naming `frameIndex`, with the
/// entry's index as `ContextKey::Requested` and its path, and the codec's or the budget's failure as
/// its cause.
///
/// The entries, their path text and the cache's index are one allocation from the engine context, in
/// addition to the cache budget; kept frames are allocated from it too. A read the cache answers hands
/// out the kept samples behind a decoder-owned lifetime token, with no copy, and that frame stays valid
/// after an eviction or the source's close. An evicted frame a caller still holds stops counting against
/// the cache budget but not against the engine context's, so frames a caller keeps, or a codec's
/// workspace, can still meet an engine-budget refusal. A kept frame is the file as it was when it was
/// read; a read the cache misses sees the file as it is now. Each read reports `image-list` trace events
/// named `cache-hit`, `cache-miss`, `cache-eviction`, `cache-bytes` and, when a frame a codec owns is
/// copied to be kept, `cache-copy-bytes`.
///
/// @param engine the engine context that owns the registry, the session and every decoded or kept
///        frame.
/// @param execution an execution of that engine context, borrowed for the call; a cancellation during
///        an eager load releases what was loaded.
/// @param images the ordered selections, copied before the call returns; an empty list is a valid
///        source of no frames.
/// @param options the load mode, the cache budget and the fixed frame; copied.
/// @return the open source, never a partial one; `ErrorCode::NotInitialized` for an engine context or
///         execution that is not valid; `ErrorCode::IncompatibleArguments` for an execution of another
///         engine context; `ErrorCode::InvalidArgument` for a mode outside the enumeration or a cache
///         budget that does not match the mode; `ErrorCode::LimitExceeded` for more than 1,048,576
///         entries, or with `SequenceFailure::DescriptorMismatch` for a fixed frame beyond the engine
///         context's limits; the execution's cancellation; for one entry, and naming it,
///         `ErrorCode::InvalidArgument` for a path that is not a valid boundary path or a format outside
///         the enumeration, `ErrorCode::ExternalFailure` when the path cannot be made absolute, or
///         `ErrorCode::LimitExceeded` when the absolute path exceeds the path bound or the paths
///         together exceed 64 MiB; the engine context's memory refusal for the entry table; or, in
///         `Eager` mode, any failure a read returns - `ErrorCode::MemoryBudgetExceeded` when the list
///         does not fit the cache budget, `ErrorCode::IncompatibleArguments` with
///         `SequenceFailure::DescriptorMismatch` for a frame that differs from the fixed frame, or what
///         the codec facade returns.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSource> createImageListSource(const EngineContext& engine,
                                                                             const ExecutionContext& execution,
                                                                             std::span<const CodecReadRequest> images,
                                                                             const ImageListSourceOptions& options);
} // namespace an
