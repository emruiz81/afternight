// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A checked reader of frames from a sequence container, and the interface a format implements
/// behind it.
///
/// A format's factory opens and validates its container, then hands an open `FrameSourceBackend` to
/// `FrameSource::create`. The handle does the checking every format would otherwise repeat: it keeps
/// the position, refuses a request the descriptor does not advertise before the backend sees it, runs
/// each backend call inside the execution's admitted task arena, and holds every frame returned to the
/// descriptor, the engine context's limits and the ownership rules. A frame owns samples charged to
/// the engine context or carries a mapping's or decoder's lifetime token, and it stays valid through
/// later reads, seeks and close.
///
/// **State.** A refusal made before the backend is called leaves the position and state as they were.
/// Once a backend call has started, a failure, an invalid result or a cancellation observed after it
/// marks the session `SequenceState::Failed`, because the decoder may have moved; only `close` is then
/// useful. End of stream is `SequenceState::EndOfStream`: repeated reads return it again without
/// calling the backend, and a seek leaves it.
///
/// **Threads.** Every call, inspection, move and destruction of one handle must be serialized by the
/// caller; different handles may be used concurrently. A handle holds a lease on its engine context,
/// reserving no thread, until it is closed, and keeps no reference to the context object itself.
///
/// **Failures `read` and `seek` share.** `ErrorCode::InvalidState` with `SequenceFailure::Closed` for a
/// closed or moved-from handle, or with `SequenceFailure::TerminalState` for a failed session;
/// `ErrorCode::NotInitialized` for an execution that is not valid; `ErrorCode::IncompatibleArguments`
/// for an execution of another engine context; the execution's `ErrorCode::Cancelled` or
/// `ErrorCode::DeadlineExceeded`, checked before and after the backend runs. When the backend fails,
/// the handle returns a failure with the same code and `SequenceFailure::CallbackFailure` whose cause
/// is the backend's failure. An exception is returned as `ErrorCode::AllocationFailed` for
/// `std::bad_alloc` and `ErrorCode::ExternalFailure` for anything else.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/IO/SequenceFrame.h>

#include <memory>

namespace an
{
class EngineContext;
class ExecutionContext;

/// The interface a sequence format implements behind a `FrameSource`.
///
/// A backend's state is its own, never global. It allocates samples and caches from the engine context
/// it is given, checks the execution's cancellation during long work, and keeps its library's headers
/// and handles private. The `FrameSource` serializes the calls and runs each inside the execution's
/// admitted task arena. A frame the backend returned stays valid and unchanged whatever the backend
/// does next - read, seek, close or destruction - so a backend never reuses a returned frame's storage.
class AFTERNIGHTENGINE_INTERFACE FrameSourceBackend
{
public:
    /// Releases every remaining resource without throwing and without invalidating a returned frame.
    virtual ~FrameSourceBackend() = default;
    /// Reads the frame at the presentation index the handle asks for.
    /// @param engine the engine context that owns the samples and any cache.
    /// @param execution the operation, valid for this call only.
    /// @param index the next presentation index, chosen by the handle.
    /// @return the frame, end of stream as a result with no frame, or a typed failure. The frame owns
    ///         its samples or carries a lifetime token, and its metadata carries no output intent.
    [[nodiscard]] virtual Result<SequenceReadResult> read(const EngineContext& engine,
                                                          const ExecutionContext& execution, std::uint64_t index) = 0;
    /// Resolves a seek and positions the next read; decodes no frame for the caller.
    /// @param execution the operation and its sinks.
    /// @param request an index or time seek, already checked against what the descriptor advertises.
    /// @return the position the next read returns, or a typed refusal. The default implementation
    ///         returns `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedSeek`.
    [[nodiscard]] virtual AFTERNIGHTENGINE_API Result<SequencePosition> seek(const ExecutionContext& execution,
                                                                             const SequenceSeekRequest& request);
    /// Releases the decoder's resources, even after a failure; does no cancellable work.
    /// @return the cleanup status. The destructor must still release everything when this fails or
    ///         throws.
    [[nodiscard]] virtual Status close() = 0;
};

/// A move-only, checked reader of one sequence; the file comment states its state and thread rules.
class FrameSource
{
public:
    /// Creates a closed handle.
    AFTERNIGHTENGINE_API FrameSource() noexcept;
    /// Closes the source and releases its resources; a cleanup failure is seen only through an
    /// explicit `close`.
    AFTERNIGHTENGINE_API ~FrameSource();
    /// Transfers the session, leaving `other` closed.
    /// @param other the source to take the session from.
    AFTERNIGHTENGINE_API FrameSource(FrameSource&& other) noexcept;
    /// Closes this source, then transfers the session, leaving `other` closed.
    /// @param other the source to take the session from.
    /// @return this handle.
    AFTERNIGHTENGINE_API FrameSource& operator=(FrameSource&& other) noexcept;
    /// Not copyable; an open decoder has one owner.
    FrameSource(const FrameSource&) = delete;
    /// Not copyable; an open decoder has one owner.
    FrameSource& operator=(const FrameSource&) = delete;
    /// Adopts an open backend and copies its description; performs no I/O.
    /// @param engine the engine context that owns the session and every frame it allocates; it is
    ///        leased, and no reference to the object is kept.
    /// @param descriptor what the container is and what the backend can do; copied.
    /// @param backend the open backend, owned exclusively from here and destroyed on failure too;
    ///        must not be null.
    /// @return the open source at index zero; `ErrorCode::InvalidArgument` for a null backend, or with
    ///         `SequenceFailure::InvalidDescriptor` for a descriptor `validateSequenceDescriptor`
    ///         refuses; `ErrorCode::NotInitialized` for an engine context that is not valid;
    ///         `ErrorCode::LimitExceeded` with `SequenceFailure::DescriptorMismatch` for a fixed frame
    ///         beyond the engine context's limits; or `ErrorCode::AllocationFailed` when the session
    ///         cannot be allocated.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<FrameSource> create(const EngineContext& engine,
                                                                         const SequenceDescriptor& descriptor,
                                                                         std::unique_ptr<FrameSourceBackend> backend);
    /// Reads the next frame and advances by exactly one.
    ///
    /// At end of stream the read returns end of stream again without calling the backend, though it
    /// still checks the execution. A failure returns no partial frame and does not advance.
    ///
    /// @param engine the engine context the source was created in; the object may have moved since.
    /// @param execution an execution of that engine context.
    /// @return the frame, or end of stream as a result with no frame; a failure `read` and `seek`
    ///         share; `ErrorCode::IncompatibleArguments` also for an engine context that is not the
    ///         source's, or with `SequenceFailure::DescriptorMismatch` for a frame that is not valid or
    ///         differs from the fixed frame, or with `SequenceFailure::ForeignAllocation` for samples
    ///         charged to another engine context; `ErrorCode::OutOfRange` with
    ///         `SequenceFailure::InvalidPosition` for a frame whose index, timestamp or duration breaks
    ///         the descriptor's timeline, or past the last representable index;
    ///         `ErrorCode::InvalidData` with `SequenceFailure::UnexpectedEnd` when the backend ends
    ///         before a stated frame count or right after a seek that resolved to a timestamp, with
    ///         `SequenceFailure::InvalidPosition` for a first frame after such a seek whose timestamp
    ///         is not that one, or with `SequenceFailure::InvalidMetadata` for metadata carrying an
    ///         output intent; `ErrorCode::LimitExceeded` with `SequenceFailure::DescriptorMismatch` for
    ///         a frame beyond the engine context's limits; or `ErrorCode::InvalidState` with
    ///         `SequenceFailure::UnsafeLifetime` for samples borrowed without a lifetime token.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<SequenceReadResult> read(const EngineContext& engine,
                                                                       const ExecutionContext& execution);
    /// Moves the next read to another position, which also leaves end of stream.
    /// @param execution an execution of the source's engine context.
    /// @param request an exact index, or a time to find the frame at or after, or at or before.
    /// @return the position the next read returns; a failure `read` and `seek` share;
    ///         `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedSeek` for a kind of seek the
    ///         descriptor does not advertise; `ErrorCode::OutOfRange` with
    ///         `SequenceFailure::InvalidPosition` for an index past the frame count, a negative time or
    ///         one at or past the timeline's end, or a request that sets the other kind's field;
    ///         `ErrorCode::InvalidArgument` with `SequenceFailure::InvalidPosition` for a kind outside the
    ///         enumeration; or `ErrorCode::InvalidData` with `SequenceFailure::InvalidPosition` for a
    ///         position the backend resolved that does not answer the request. Every refusal but the
    ///         last is made before the backend is called.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<SequencePosition> seek(const ExecutionContext& execution,
                                                                     const SequenceSeekRequest& request);
    /// Releases the backend and the lease, whatever the cancellation state and even when cleanup
    /// fails. Idempotent.
    /// @return the cleanup failure on the first close, success otherwise; the state is `Closed` either
    ///         way.
    [[nodiscard]] AFTERNIGHTENGINE_API Status close() noexcept;
    /// The session's lifecycle state, read without I/O.
    /// @return the current state; `Closed` for a closed or moved-from handle.
    [[nodiscard]] AFTERNIGHTENGINE_API SequenceState state() const noexcept;
    /// The copied descriptor, borrowed until the handle is closed, moved from or destroyed.
    /// @return the descriptor, or null for a closed handle.
    [[nodiscard]] AFTERNIGHTENGINE_API const SequenceDescriptor* descriptor() const noexcept;
    /// The index the next read returns; a failure leaves it at the last successful position.
    /// @return the next index, or zero for a closed handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t nextIndex() const noexcept;

    /// The engine context that owns the session and its frames, read without I/O.
    /// @return the owning engine context's identity, or zero for a closed handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t contextId() const noexcept;

private:
    /// Decoder, immutable description and context lease, defined privately.
    struct Impl;
    /// Exclusive session ownership.
    std::unique_ptr<Impl> m_impl;
};
} // namespace an
