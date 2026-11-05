// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A checked writer of frames into a sequence container's private output, and the interface a format
/// implements behind it.
///
/// A format's factory checks the output intent, reserves the pending output and opens its container,
/// then hands an open `FrameSinkBackend` to `FrameSink::create`. The handle checks every frame before
/// the backend sees it - the next index, a timeline that does not go backwards, the fixed frame and the
/// engine context's limits - runs each backend call inside the execution's admitted task arena, and
/// holds the counts the backend reports to exactly one more frame per append. Nothing converts,
/// demosaics, normalizes or inherits an output intent: the caller prepares each frame's representation
/// and meaning before appending it.
///
/// **What a sink writes is private.** Finishing completes the container at its pending path; it
/// publishes nothing and claims no durability. The caller commits the output through its own
/// transaction afterwards. Closing or destroying an unfinished sink aborts the output, and destruction
/// never finishes one.
///
/// **State and threads.** A refusal made before the backend is called leaves the counts and state as
/// they were; once a backend call has started, a failure, an invalid report or a cancellation observed
/// after it marks the session `SequenceState::Failed`, and only `close` is then useful. Every call,
/// inspection, move and destruction of one handle must be serialized by the caller; different handles
/// are independent. A handle holds a lease on its engine context, reserving no thread, until it is
/// closed.

#pragma once

#include <AfterNightEngine/IO/SequenceFrame.h>

#include <memory>

namespace an
{
class EngineContext;
class ExecutionContext;

/// The interface a sequence format implements behind a `FrameSink`.
///
/// A backend writes only to a private output its factory reserved, never to the artifact's
/// destination. It allocates its workspace from the engine context it is given, and the `FrameSink`
/// serializes the calls and runs each inside the execution's admitted task arena. A failed write may
/// leave private bytes behind; `close` and the destructor abort and remove an incomplete output and
/// never throw. A frame is borrowed only until `append` returns.
class FrameSinkBackend
{
public:
    /// Aborts an unfinished output and releases resources without throwing; never publishes.
    virtual ~FrameSinkBackend() = default;
    /// Writes one complete frame to the private output before returning.
    /// @param engine the engine context that owns the encoder's workspaces.
    /// @param execution the operation, borrowed for this call.
    /// @param frame the samples, metadata and position, borrowed for this call and not modified.
    /// @return the cumulative counts, or a typed failure, which may leave partial private bytes.
    [[nodiscard]] virtual Result<SequenceWriteResult>
    append(const EngineContext& engine, const ExecutionContext& execution, const SequenceFrame& frame) = 0;
    /// Completes and closes the private container, reporting any error the encoder delayed; never
    /// publishes.
    /// @param execution the operation; its cancellation is observed only before the point after which
    ///        completion cannot be undone.
    /// @return the final cumulative counts, or a failure after which the output needs cleanup. A
    ///         success is returned as a success even when a stop arrives during it.
    [[nodiscard]] virtual Result<SequenceWriteResult> finish(const ExecutionContext& execution) = 0;
    /// Aborts an unfinished output and releases resources, even after a failure; not cancellable.
    /// @return the cleanup status. The destructor must still release everything when this fails or
    ///         throws.
    [[nodiscard]] virtual Status close() = 0;
};

/// A move-only, checked writer of one sequence; the file comment states its state and thread rules.
class FrameSink
{
public:
    /// Creates a closed handle.
    AFTERNIGHTENGINE_API FrameSink() noexcept;
    /// Closes the sink, aborting an unfinished output; a cleanup failure is seen only through an
    /// explicit `close`.
    AFTERNIGHTENGINE_API ~FrameSink();
    /// Transfers the session, leaving `other` closed.
    /// @param other the sink to take the session from.
    AFTERNIGHTENGINE_API FrameSink(FrameSink&& other) noexcept;
    /// Closes this sink, then transfers the session, leaving `other` closed.
    /// @param other the sink to take the session from.
    /// @return this handle.
    AFTERNIGHTENGINE_API FrameSink& operator=(FrameSink&& other) noexcept;
    /// Not copyable; an open writer has one owner.
    FrameSink(const FrameSink&) = delete;
    /// Not copyable; an open writer has one owner.
    FrameSink& operator=(const FrameSink&) = delete;
    /// Adopts an open backend and copies its description; starts no I/O and no thread.
    /// @param engine the engine context that owns the session and its workspace; it is leased, and no
    ///        reference to the object is kept.
    /// @param descriptor what the container will hold; it advertises no seek.
    /// @param backend the open backend, owned exclusively from here and destroyed on failure too;
    ///        must not be null.
    /// @return the open sink with nothing written; `ErrorCode::InvalidArgument` for a null backend, or
    ///         with `SequenceFailure::InvalidDescriptor` for a descriptor `validateSequenceDescriptor`
    ///         refuses or one that advertises a seek; `ErrorCode::NotInitialized` for an engine context
    ///         that is not valid; `ErrorCode::LimitExceeded` with `SequenceFailure::DescriptorMismatch`
    ///         for a fixed frame beyond the engine context's limits; or `ErrorCode::AllocationFailed`
    ///         when the session cannot be allocated.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<FrameSink> create(const EngineContext& engine,
                                                                       const SequenceDescriptor& descriptor,
                                                                       std::unique_ptr<FrameSinkBackend> backend);
    /// Appends the next frame: its index is the number written so far, it matches the fixed frame, and
    /// its timestamp does not go backwards.
    /// @param engine the engine context the sink was created in.
    /// @param execution an execution of that engine context.
    /// @param frame the frame, borrowed until the call returns; its samples may themselves be borrowed.
    /// @return the cumulative counts, advanced by exactly one frame; `ErrorCode::InvalidState` with
    ///         `SequenceFailure::Closed` for a closed handle, or with `SequenceFailure::TerminalState`
    ///         for a failed or finished sink; `ErrorCode::NotInitialized` for an execution that is not
    ///         valid; `ErrorCode::IncompatibleArguments` for an execution or engine context other than
    ///         the sink's, or with `SequenceFailure::DescriptorMismatch` for a frame that is not valid
    ///         or differs from the fixed frame; the execution's cancellation, checked before and after
    ///         the backend runs; `ErrorCode::OutOfRange` with `SequenceFailure::InvalidPosition` for a
    ///         wrong index, a missing required timestamp or one that breaks the timeline;
    ///         `ErrorCode::LimitExceeded` with `SequenceFailure::DescriptorMismatch` for a frame beyond
    ///         the engine context's limits; the backend's failure with the same code and
    ///         `SequenceFailure::CallbackFailure`; or `ErrorCode::InvalidData` with
    ///         `SequenceFailure::InvalidReport` for counts the backend reports that are not one frame
    ///         more, or bytes that shrink. A failure never advances the counts.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<SequenceWriteResult>
    append(const EngineContext& engine, const ExecutionContext& execution, const SequenceFrame& frame);
    /// Completes the private output; a stated frame count must already have been written.
    ///
    /// Finishing a finished sink returns the same counts again. A completion that succeeds is returned
    /// as a success even when a stop arrives while it runs.
    ///
    /// @param execution an execution of the sink's engine context; its cancellation is checked before
    ///        an unfinished output is completed.
    /// @return the final counts; `ErrorCode::InvalidState` with `SequenceFailure::Closed` for a closed
    ///         handle, with `SequenceFailure::TerminalState` for a failed sink, or with
    ///         `SequenceFailure::UnexpectedEnd` - the sink stays open - when fewer frames than the
    ///         stated count were written; `ErrorCode::IncompatibleArguments` for an execution that is
    ///         not valid or belongs to another engine context; the execution's cancellation; the
    ///         backend's failure with the same code and `SequenceFailure::CallbackFailure`; or
    ///         `ErrorCode::InvalidData` with `SequenceFailure::InvalidReport` for final counts that
    ///         disagree with what was appended.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<SequenceWriteResult> finish(const ExecutionContext& execution);
    /// Releases the backend and the lease, aborting an unfinished output even after a failure and
    /// whatever the cancellation state. Idempotent.
    /// @return the cleanup failure on the first close, success otherwise; the state is `Closed` either
    ///         way.
    [[nodiscard]] AFTERNIGHTENGINE_API Status close() noexcept;
    /// The session's lifecycle state, read without I/O.
    /// @return the current state; `Closed` for a closed or moved-from handle.
    [[nodiscard]] AFTERNIGHTENGINE_API SequenceState state() const noexcept;
    /// The copied descriptor, borrowed until the handle is closed, moved from or destroyed.
    /// @return the descriptor, or null for a closed handle.
    [[nodiscard]] AFTERNIGHTENGINE_API const SequenceDescriptor* descriptor() const noexcept;
    /// The counts of successful appends and finish; a partly written frame is never counted.
    /// @return the cumulative private progress, or zeros for a closed handle.
    [[nodiscard]] AFTERNIGHTENGINE_API SequenceWriteResult progress() const noexcept;

    /// The engine context that owns the session, read without I/O.
    /// @return the owning engine context's identity, or zero for a closed handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t contextId() const noexcept;

private:
    /// Private session state and resource ownership.
    struct Impl;
    /// Exclusive session ownership.
    std::unique_ptr<Impl> m_impl;
};
} // namespace an
