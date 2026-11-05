// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Streaming a sequence from a source through an optional per-frame operation into an optional sink,
/// with bounded read-ahead and write-behind.
///
/// `streamFrames` reads frames in presentation order, hands each to a `FrameStreamProcessor`, and
/// appends what it returns to a `FrameSink`. With more than one admitted thread, reading and writing
/// run beside the processing, each through a queue whose depth and bytes are bounded before the run
/// starts; a slot is reserved before a frame is decoded, so a slow consumer can never let an extra,
/// unaccounted frame in. Every stage has finished and joined before the call returns, whatever
/// happened, and nothing is ever published: completing the private output is the most a run does, and
/// committing it through `IO/SequenceOutput.h` stays the caller's step.
///
/// A failure is reported with the stage and the frame index it happened at, as the typed context
/// fields `ContextKey::StreamStage` and `ContextKey::FrameIndex` beside the original failure as its
/// cause, and the counters are settled even then.

#pragma once

#include <AfterNightEngine/IO/FrameSink.h>
#include <AfterNightEngine/IO/FrameSource.h>

#include <cstdint>
#include <limits>

namespace an
{
/// The stage a stream's failure happened in, carried as a typed context field.
enum class FrameStreamStage : std::uint8_t
{
    Read, ///< A source read, or reserving a place for a frame in the input queue.
    Process, ///< The processor, or the check of what it returned.
    Write, ///< A sink append, or reserving a place for a frame in the output queue.
    Finish ///< Completing the sink's private container.
};

/// The bounds of one streaming run; a depth of zero runs that stage inline, on the processing thread.
///
/// A depth counts the frames waiting in a queue and the one frame being read or written in the
/// background. The processor holds at most one more input, plus whatever output and workspace it
/// allocates itself. A frame's bytes are its buffer's whole capacity, padding included, and are checked
/// once the frame has been read or processed: the allocations a codec or the processor makes to
/// produce it are bounded by their own contracts and the engine context's memory budget, not by a
/// reservation here.
struct FrameStreamOptions
{
    std::uint32_t readAheadDepth = 1u; ///< Frames read ahead of the processor, at most 4096; zero reads inline.
    std::uint32_t writeBehindDepth = 1u; ///< Frames written behind it, at most 4096; zero writes inline.
    std::uint64_t maxFrameBytes = 256u * 1024u * 1024u; ///< The largest frame admitted, in or out; positive.
    /// The input queue's byte budget; at least `maxFrameBytes` when reading ahead.
    std::uint64_t readAheadBytes = 256u * 1024u * 1024u;
    std::uint64_t writeBehindBytes =
        256u * 1024u * 1024u; ///< The output queue's byte budget; at least `maxFrameBytes` when writing behind.
    std::uint64_t frameLimit =
        (std::numeric_limits<std::uint64_t>::max)(); ///< The most frames to read; zero reads none.
    bool finishSink = true; ///< Whether to complete the sink's private output at the end; never publishes it.
};

/// One queue's measurements, settled even when the stream fails.
struct FrameQueueCounters
{
    std::uint64_t peakFrames = 0u; ///< The most frames queued or in the background at once, reserved included.
    std::uint64_t peakReservedBytes = 0u; ///< The most bytes reserved at once; never above the queue's budget.
    std::uint64_t waits = 0u; ///< Hand-overs that had to wait, a wait a cancellation ended included.
    std::uint64_t waitNanoseconds = 0u; ///< The total of those waits, on the steady clock.
    std::uint64_t longestWaitNanoseconds = 0u; ///< The longest single wait.
};

/// One run's counts, each taken only once its stage succeeded; there is no per-frame list.
///
/// The stages overlap, so their times do not add up to the run's wall time. Every count saturates.
struct FrameStreamCounters
{
    FrameQueueCounters readAhead{}; ///< The input queue's measurements; zero when reading inline.
    FrameQueueCounters writeBehind{}; ///< The output queue's measurements; zero when writing inline.
    std::uint64_t framesRead = 0u; ///< Complete frames read, any read ahead and then discarded included.
    std::uint64_t framesProcessed = 0u; ///< Frames the processor returned, or passed through without one.
    std::uint64_t framesWritten = 0u; ///< Successful sink appends; finishing is not counted.
    std::uint64_t bytesRead = 0u; ///< The buffer capacities of the frames read, padding included.
    std::uint64_t bytesWritten = 0u; ///< The buffer capacities the sink accepted, not the bytes it encoded.
    std::uint64_t readNanoseconds = 0u; ///< Time spent reading the source.
    std::uint64_t processNanoseconds = 0u; ///< Time spent processing frames.
    std::uint64_t writeNanoseconds = 0u; ///< Time spent appending to and finishing the sink.
};

/// A caller's per-frame operation, called once per frame, one frame at a time, in presentation order.
///
/// It composes the engine's operations - calibrating, detecting, warping, converting, collecting typed
/// results - and applies the metadata and validity transitions those operations require itself. It
/// receives each frame as its own and returns a complete frame of its own with the same position.
class FrameStreamProcessor
{
public:
    /// Releases the caller's processor state.
    virtual ~FrameStreamProcessor() = default;
    /// Processes one frame inside the execution's admitted task arena; an exception is returned as a
    /// failure.
    /// @param engine the engine context that owns the frames, borrowed for the call.
    /// @param execution the run's execution: its cancellation, policy, sinks and optional scratch.
    /// @param frame the input, whose ownership passes to the processor.
    /// @return the output - samples owned by the engine context, the input's position unchanged - or a
    ///         typed failure.
    [[nodiscard]] virtual Result<SequenceFrame> process(const EngineContext& engine, const ExecutionContext& execution,
                                                        SequenceFrame frame) = 0;
};

/// Streams frames from a source, through an optional processor, into an optional sink.
///
/// **Threads.** With one admitted thread, or with both depths zero, everything runs inline on the
/// calling thread. Otherwise reading and writing each run on a coordinator thread the call starts and
/// joins; a queue wait happens there, outside the task arena, while codec and processor work enters
/// the arena and cannot widen the operation's grant.
/// Only the processor borrows the execution's scratch. Every stage shares the execution's stop,
/// deadline and sinks, and a backend that makes an uninterruptible foreign call delays the join until
/// it returns. Call this from a thread outside any scheduler callback - so a queue wait holds no arena
/// slot - and never from inside a processor. The source, the sink and the processor are used
/// exclusively until the call returns; different runs may proceed concurrently.
///
/// **Memory.** The queues and every queued frame take their memory from the engine context. A frame
/// whose samples the engine context does not own - mapped, decoder-owned or borrowed - is copied into
/// its memory as soon as it is read, without conversion, so everything queued is counted. A frame the
/// processor returns must already own memory of this engine context.
///
/// **Afterwards.** A successful run leaves the source positioned just after the last frame read, so a
/// caller can seek or run another bounded batch; nothing is read past `frameLimit`. A failure once work
/// has begun cancels the work in flight, closes the source and closes the sink, which discards its
/// output; a refusal before that leaves both untouched. The first failure observed is the one
/// returned, and a cleanup failure never replaces it.
///
/// @param engine the engine context that owns the source, the sink and every frame.
/// @param execution an execution of that engine context, borrowed until every stage has joined.
/// @param source an open source at the position to start from.
/// @param sink the private output to append to, or null to drop each frame once it is processed.
/// @param processor the per-frame operation, or null to pass frames through unchanged.
/// @param options the bounds, the frame limit and whether to finish the sink.
/// @param counters reset on entry and settled on every return; read them only after the call.
/// @return success; `ErrorCode::NotInitialized` for an engine context or execution that is not valid;
///         `ErrorCode::IncompatibleArguments` for an execution, source or sink of another engine
///         context; `ErrorCode::InvalidState` for a source that is closed or failed, or a sink that is
///         not open; `ErrorCode::InvalidArgument` for a zero `maxFrameBytes`, a depth above 4096, or a
///         queue budget below `maxFrameBytes`; the execution's cancellation; the engine context's
///         memory refusal for the queues; or, with the stage and frame index, the failure of a stage -
///         the source's, the processor's or the sink's own, `ErrorCode::MemoryBudgetExceeded` for a
///         frame larger than `maxFrameBytes`, or `ErrorCode::IncompatibleArguments` for a processed
///         frame that is not valid, moved position or does not own memory of this engine context.
[[nodiscard]] AFTERNIGHTENGINE_API Status streamFrames(const EngineContext& engine, const ExecutionContext& execution,
                                                       FrameSource& source, FrameSink* sink,
                                                       FrameStreamProcessor* processor,
                                                       const FrameStreamOptions& options,
                                                       FrameStreamCounters& counters);
} // namespace an
