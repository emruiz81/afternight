// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A bounded cache of delivered frames in front of an index-seekable source.
///
/// A source decodes what it is asked for and keeps nothing, so a caller that returns to a frame pays the decoder
/// again. This puts a cache in front of any source whose frames can be sought by index, with a prefetch: a read
/// the cache misses can read a stated number of the frames that follow into the cache while the source is
/// positioned among them, which is where a video decoder reads cheapest.
///
/// **A cached frame is the frame the source delivered, shared rather than copied.** A read the cache answers
/// returns the retained samples behind a decoder-owned lifetime token, so a frame a caller holds stays valid
/// after the cache evicts it, is cleared or closes - the rule `IO/ImageListSource.h` applies to its own cache.
///
/// **The cache is bounded twice and controlled from outside.** A frame count and a byte count, either of which
/// may be left open; a caller changes both, clears the cache and reads its
/// counters through a control that shares the cache with the source and may be used from another thread.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/FrameSource.h>

#include <cstdint>
#include <memory>

namespace an
{

class EngineContext;
class ExecutionContext;
class FrameCacheState;

/// How a frame cache is bounded and how far a read it misses reads ahead.
///
/// A plain aggregate; every value is valid.
struct FrameCacheOptions
{
    /// The most frames kept; zero keeps none.
    std::uint64_t maxFrames = 10u;

    /// The most sample bytes kept, counting each retained frame's whole buffer; zero bounds the cache by frame
    /// count alone.
    std::uint64_t maxBytes = 0u;

    /// How many of the frames after one the cache misses are read into unused cache capacity before that read
    /// returns; zero reads only the frame asked for. Read-ahead never evicts a retained frame.
    std::uint32_t readAhead = 0u;
};

/// What a frame cache has done and what it holds.
struct FrameCacheCounters
{
    /// Reads the cache answered since it was created.
    std::uint64_t hits = 0u;

    /// Reads the source answered since the cache was created.
    std::uint64_t misses = 0u;

    /// Frames successfully retained ahead of a request since it was created.
    std::uint64_t framesReadAhead = 0u;

    /// Frames dropped to stay within the bounds since the cache was created; clearing drops without counting.
    std::uint64_t evictions = 0u;

    /// Frames the cache holds now.
    std::uint64_t frames = 0u;

    /// Sample bytes the cache holds now.
    std::uint64_t bytes = 0u;
};

/// A caller's hold on a frame cache's bounds and contents.
///
/// Copyable: every copy controls the same cache, whose state lives as long as its source or any control. Every
/// call is thread-safe, against other calls and against the source's reads. A change applies to the next read; a
/// read in flight finishes under the bounds it began with, and then keeps no more than the changed bounds admit.
class FrameCacheControl
{
public:
    /// A control that controls nothing.
    AFTERNIGHTENGINE_API FrameCacheControl() noexcept;

    /// Releases this hold on the cache.
    AFTERNIGHTENGINE_API ~FrameCacheControl();

    /// Another hold on the same cache.
    ///
    /// @param other the control to copy.
    AFTERNIGHTENGINE_API FrameCacheControl(const FrameCacheControl& other) noexcept;

    /// Makes this a hold on another control's cache.
    ///
    /// @param other the control to copy.
    /// @return this control.
    AFTERNIGHTENGINE_API FrameCacheControl& operator=(const FrameCacheControl& other) noexcept;

    /// Takes over another control's hold, leaving it controlling nothing.
    ///
    /// @param other the control to take over.
    AFTERNIGHTENGINE_API FrameCacheControl(FrameCacheControl&& other) noexcept;

    /// Releases this hold and takes over another's, leaving it controlling nothing.
    ///
    /// @param other the control to take over.
    /// @return this control.
    AFTERNIGHTENGINE_API FrameCacheControl& operator=(FrameCacheControl&& other) noexcept;

    /// Drops every frame the cache holds. Frames a caller already holds stay valid. Does nothing for a control
    /// that controls nothing.
    AFTERNIGHTENGINE_API void clear() noexcept;

    /// Changes the bounds, dropping the least recently used frames they no longer admit.
    ///
    /// @param maxFrames the most frames kept; zero keeps none.
    /// @param maxBytes the most sample bytes kept; zero bounds the cache by frame count alone.
    AFTERNIGHTENGINE_API void resize(std::uint64_t maxFrames, std::uint64_t maxBytes) noexcept;

    /// Changes how far a read the cache misses reads ahead.
    ///
    /// @param frames how many of the following frames are read into the cache; zero reads none.
    AFTERNIGHTENGINE_API void setReadAhead(std::uint32_t frames) noexcept;

    /// The bounds and read-ahead as they stand.
    ///
    /// @return them; the defaults for a control that controls nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API FrameCacheOptions options() const noexcept;

    /// What the cache has done and holds.
    ///
    /// @return the counters; zeros for a control that controls nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API FrameCacheCounters counters() const noexcept;

    /// Whether this control controls a cache.
    ///
    /// @return true for the control a cached source was created with and its copies; false otherwise.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

private:
    friend class FrameCacheAccess;

    /// A hold on a cache `createCachedFrameSource` built.
    ///
    /// @param state the cache.
    explicit FrameCacheControl(std::shared_ptr<FrameCacheState> state) noexcept;

    /// The cache, or null.
    std::shared_ptr<FrameCacheState> m_state;
};

/// A source behind a frame cache, and the cache's control.
struct CachedFrameSource
{
    /// The wrapped source's frames, read through the cache, under the wrapped source's descriptor.
    FrameSource source;

    /// The cache's control.
    FrameCacheControl control;
};

/// Puts a frame cache in front of a source.
///
/// The source is consumed; it must be open, seekable by index and of a stated length, and the cached source
/// states its descriptor. A read of frame `i` the cache holds returns it and makes it the most recently used. A
/// read the cache misses positions the source at `i` unless it is there already, reads the frame, keeps it when
/// the bounds admit it - evicting the least recently used frames to make room - and then reads the frames that
/// follow into unused cache capacity, up to the read-ahead count, stopping at the bound, the end of the sequence
/// or a frame the cache already holds. Read-ahead never evicts an existing frame. A frame larger than the byte
/// bound is returned and never kept. A time seek is resolved by the source. A failed read, of the frame asked for
/// or of one read ahead, fails the cached source as a failed read fails any source, and frames already handed out
/// stay valid. Each read reports `frame-cache` trace counters named `hit`, `miss`, `read-ahead`, `eviction` and
/// `bytes`.
///
/// @param engine the context the source belongs to.
/// @param execution an operation of that context.
/// @param source an open, index-seekable source of a stated length; consumed, and closed on every refusal.
/// @param options the cache's initial bounds and read-ahead; copied.
/// @return the cached source and its control; `ErrorCode::NotInitialized` for an empty context or an execution
///         that is not valid; `ErrorCode::IncompatibleArguments` naming `source` for a source of another context,
///         or for an execution of another context; the execution's cancellation; `ErrorCode::InvalidState`
///         naming `source` for a source that is closed or failed; `ErrorCode::InvalidArgument` with
///         `SequenceFailure::UnsupportedSeek` naming `seekByIndex` for a source that cannot seek by index, or
///         with `SequenceFailure::InvalidDescriptor` naming `frameCount` for one that states no length; or
///         `ErrorCode::AllocationFailed` when the cache or the session cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CachedFrameSource> createCachedFrameSource(const EngineContext& engine,
                                                                                     const ExecutionContext& execution,
                                                                                     FrameSource source,
                                                                                     const FrameCacheOptions& options);

} // namespace an
