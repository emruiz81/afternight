// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a few long-lived stages of one operation, run side by side on plain host
// threads.
//
// Some operations are a pipeline rather than a partition: a reader, the processing and a writer,
// each waiting on a queue another fills. A stage that waits cannot run inside the operation's task
// arena, because a coordinator blocked on a full queue would occupy a worker the other stages need,
// and with the last worker taken the pipeline stops. So the coordinators here are ordinary threads
// outside the arena, and every nontrivial piece of work a coordinator does enters the arena through
// `Runtime/TaskScheduler.h`.
//
// Each stage runs under an execution of its own that shares the operation's thread grant, identity
// and report ordering, and observes one shared stop source: a stop request on the operation, or the
// first stage to fail, stops them all. Every stage is joined before the call returns, on every path.

#pragma once

#include <AfterNightEngine/Core/Status.h>

#include <cstdint>

namespace an
{
class ExecutionContext;

/// The stages of one operation, run by `runCooperativeTasks`.
class CooperativeTaskBody
{
public:
    /// Virtual, so a body held by base reference is destroyed as what it is.
    virtual ~CooperativeTaskBody() = default;

    /// Runs one stage to completion.
    ///
    /// Called once per stage, concurrently with the other stages; stage zero runs on the thread that
    /// called `runCooperativeTasks` and every other stage on a thread started for it.
    ///
    /// @param execution the stage's own execution. It shares the operation's thread grant and
    ///        reports, and is stopped when the operation is stopped or another stage fails. Only
    ///        stage zero's borrows the operation's scratch arena.
    /// @param stage which stage this is, from zero to one less than the stage count.
    /// @return success, or the failure that stops every other stage. An exception is translated at
    ///         the boundary rather than propagated.
    virtual Status run(const ExecutionContext& execution, std::uint32_t stage) = 0;
};

/// Runs up to three stages of one operation side by side and joins them all.
///
/// The first failure is the one returned, and it requests the stop every other stage's execution
/// observes; a stop request on `execution` does the same. Every thread that was started is joined
/// before the call returns, including when starting one of them fails.
///
/// Must be called from an ordinary host thread, never from inside a scheduler callback: the calling
/// thread runs stage zero and then blocks until every other stage has finished.
///
/// @param execution the operation the stages belong to. Only read.
/// @param count how many stages to run, from one to three.
/// @param body the stages; its `run` is called once per stage, concurrently.
/// @return success when every stage succeeded; otherwise `ErrorCode::NotInitialized` when
///         `execution` holds nothing, `ErrorCode::InvalidArgument` when `count` is zero or above
///         three, the operation's own `Cancelled` or `DeadlineExceeded` when it was already stopped,
///         or the first stage's failure - an exception a stage threw, or a thread that could not be
///         started, translated as `callAcrossBoundary` translates it.
Status runCooperativeTasks(const ExecutionContext& execution, std::uint32_t count, CooperativeTaskBody& body);
} // namespace an
