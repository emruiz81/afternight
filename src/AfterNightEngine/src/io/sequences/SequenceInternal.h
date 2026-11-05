// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the checks and the calling discipline every sequence handle shares.
//
// `FrameSource` and `FrameSink` are checked handles over a backend that a format adapter implements.
// Every entry point runs inside `guarded`, so no exception crosses the engine's boundary. Every argument
// is checked here before a backend sees it, and every backend call runs through `call`, as one task in
// the execution's arena. A backend's answer is then checked again - its position, its frame, the place a
// seek landed - so a defective backend is caught at the boundary rather than trusted.

#pragma once

#include <AfterNightEngine/IO/SequenceFrame.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <limits>
#include <type_traits>
#include <utility>

namespace an
{
/// Shared preflight and callback machinery for the two checked sequence handles.
namespace sequence_detail
{
/// Whether an execution may drive a handle of an engine context.
///
/// @param contextId the engine context the handle belongs to.
/// @param execution the operation making the call.
/// @return success; `ErrorCode::NotInitialized` for an execution that holds nothing;
///         `ErrorCode::IncompatibleArguments` for an execution of another context; or the execution's
///         own cancellation.
Status checkContext(std::uint64_t contextId, const ExecutionContext& execution) noexcept;

/// Whether a descriptor describes a sequence this engine context can hold.
///
/// @return success; `ErrorCode::NotInitialized` for an engine context that holds nothing; whatever
///         `validateSequenceDescriptor` refuses; or `ErrorCode::LimitExceeded` with
///         `SequenceFailure::DescriptorMismatch` for a fixed frame beyond the engine's limits.
Status checkDescription(const EngineContext& engine, const SequenceDescriptor& descriptor) noexcept;

/// Whether a frame's position may follow the frames before it, for a frame read or a frame appended.
///
/// Refused as `ErrorCode::OutOfRange` with `SequenceFailure::InvalidPosition` when the index is not
/// `expectedIndex`, is the largest index, or reaches the frame count; when required timestamps are
/// missing, or a timestamp or duration is stated without a time base; when the presentation time is
/// negative, before `previousTicks` or at or past the sequence's duration; or when a duration is not
/// positive, has no presentation time, or runs past the sequence's duration.
///
/// @param descriptor the sequence.
/// @param position the frame's position.
/// @param expectedIndex the index the next frame must have.
/// @param previousTicks the previous frame's presentation time, if it stated one.
Status checkPosition(const SequenceDescriptor& descriptor, const SequencePosition& position,
                     std::uint64_t expectedIndex, std::optional<std::int64_t> previousTicks) noexcept;

/// Whether a frame matches its sequence and, for a frame a source read, may be handed to the caller.
///
/// An invalid frame, or one whose description differs from the fixed frame, is
/// `ErrorCode::IncompatibleArguments` with `SequenceFailure::DescriptorMismatch`, and one beyond the
/// engine's limits is `ErrorCode::LimitExceeded` with the same reason. A frame a source read is also
/// refused when its samples are borrowed (`ErrorCode::InvalidState`, `SequenceFailure::UnsafeLifetime`),
/// when they are another context's engine memory (`ErrorCode::IncompatibleArguments`,
/// `SequenceFailure::ForeignAllocation`), or when its metadata carries an output intent
/// (`ErrorCode::InvalidData`, `SequenceFailure::InvalidMetadata`).
///
/// @param engine the engine context whose limits apply.
/// @param descriptor the sequence.
/// @param frame the frame.
/// @param source true for a frame a source read, false for one a sink is given.
Status checkFrame(const EngineContext& engine, const SequenceDescriptor& descriptor, const SequenceFrame& frame,
                  bool source) noexcept;

/// Whether a seek request is one the sequence declares and names a place in it.
///
/// A kind of seek the descriptor does not declare is `ErrorCode::Unsupported` with
/// `SequenceFailure::UnsupportedSeek`. An index past the frame count (or the largest index when no count
/// is stated), a time on an index seek, an index on a time seek, or a negative time or one at or past the
/// duration is `ErrorCode::OutOfRange` with `SequenceFailure::InvalidPosition`, and a kind outside the
/// enumeration is `ErrorCode::InvalidArgument`. An index equal to the frame count is the end, and is
/// admitted.
Status checkSeek(const SequenceDescriptor& descriptor, const SequenceSeekRequest& request) noexcept;

/// Whether the place a backend's seek landed answers the request.
///
/// Refused as `ErrorCode::InvalidData` with `SequenceFailure::InvalidPosition` when the position
/// carries a duration, lies past the end, states a time without a time base or outside the sequence's
/// duration, is another index than an index seek asked for, or - for a time seek - states no time, is
/// the end, or lies on the wrong side of the requested time.
Status checkSeekResult(const SequenceDescriptor& descriptor, const SequenceSeekRequest& request,
                       const SequencePosition& position) noexcept;

/// Runs a public entry point's body so that no exception leaves it.
///
/// An exception thrown while the handle is being set up or by anything the body calls becomes a
/// returned failure through the engine's exception boundary: `std::bad_alloc` as
/// `ErrorCode::AllocationFailed`, anything else as `ErrorCode::ExternalFailure`.
template <typename CallableT> auto guarded(CallableT&& body) -> std::invoke_result_t<CallableT>
{
    if constexpr (std::is_same_v<std::invoke_result_t<CallableT>, Status>)
    {
        Status result;
        auto called = callAcrossBoundary({"sequence", "session", ErrorDomain::Io}, [&] { result = body(); });
        return called.failed() ? called : result;
    }
    else
    {
        auto result = evaluateAcrossBoundary({"sequence", "session", ErrorDomain::Io}, std::forward<CallableT>(body));
        if (!result.hasValue())
            return result.status();
        return std::move(*result);
    }
}

/// Runs one backend callback as a single task on the execution's granted threads, so the callback and any
/// library work nested in it run inside the operation's arena.
///
/// `entered` is set once the callback starts, which tells the caller whether a refusal left the backend
/// untouched. A backend's failure, or the scheduler's, is returned with its own code and
/// `SequenceFailure::CallbackFailure`, the original failure being its cause. A stop that arrives while
/// the callback runs discards what it produced - unless `completionWins` and the callback succeeded,
/// which is how a sink that has finished writing its private output keeps that result.
template <typename CallableT>
auto call(const ExecutionContext& execution, bool& entered, bool completionWins, CallableT&& body)
    -> std::invoke_result_t<CallableT>
{
    std::optional<std::invoke_result_t<CallableT>> result;
    const auto scheduled = parallelFor(execution, 1u, {0u, 1u}, [&](TaskRange) -> Status {
        entered = true;
        auto evaluated =
            evaluateAcrossBoundary({"sequence", "backend", ErrorDomain::Io}, std::forward<CallableT>(body));
        if (!evaluated.hasValue())
            return evaluated.status();
        result.emplace(std::move(*evaluated));
        return result->status();
    });
    if (completionWins && result && result->hasValue())
        return std::move(*result);
    if (scheduled.failed())
        return sequenceFailure(scheduled.code(), SequenceFailure::CallbackFailure, scheduled);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (!result)
        return sequenceFailure(ErrorCode::Internal, SequenceFailure::CallbackFailure);
    return std::move(*result);
}
} // namespace sequence_detail
} // namespace an
