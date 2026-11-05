// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - running a caller's callback inside an operation's task arena, with its
// exceptions translated.
//
// Some engine entry points hand control to code the engine did not write - a codec session, a
// per-frame processor - and that code may throw, or may start oneTBB work of its own. Two helpers
// cover the two halves. `guarded` is only the exception boundary. `call` also runs the callback as
// a one-item `parallelFor`, which checks cancellation first and enters the operation's arena on the
// calling thread, so any parallel work the callback starts is bounded by the operation's grant
// rather than by the whole machine.

#pragma once

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <optional>
#include <type_traits>
#include <utility>

/// Helpers the engine's entry points use to run a caller's callback safely; not a public surface.
namespace an::scheduled_detail
{
/// Runs a callback and translates anything it throws.
///
/// An exception becomes a failure reported for the library `"engine"` in `ErrorDomain::Runtime`:
/// `ErrorCode::AllocationFailed` for `std::bad_alloc` and `ErrorCode::ExternalFailure` otherwise.
///
/// @tparam CallableT invocable with no arguments, returning `Status` or a `Result`.
/// @param body the callback, invoked exactly once on the calling thread.
/// @return what the callback returned, or the translated exception.
template <typename CallableT> auto guarded(CallableT&& body) -> std::invoke_result_t<CallableT>
{
    if constexpr (std::is_same_v<std::invoke_result_t<CallableT>, Status>)
    {
        Status result;
        auto boundary = callAcrossBoundary({"engine", "callback", ErrorDomain::Runtime}, [&] { result = body(); });
        return boundary.failed() ? boundary : result;
    }
    else
    {
        auto result =
            evaluateAcrossBoundary({"engine", "callback", ErrorDomain::Runtime}, std::forward<CallableT>(body));
        if (!result)
            return result.status();
        return std::move(*result);
    }
}

/// Runs a callback once inside the operation's task arena, after a cancellation checkpoint.
///
/// The callback runs on the calling thread through `guarded`. A stop that arrives while it runs is
/// observed afterwards, and by default wins over what the callback produced.
///
/// @tparam CallableT invocable with no arguments, returning a `Result`.
/// @param execution the operation the callback belongs to.
/// @param entered set to true when the callback started, which tells a caller whether a side effect
///        may have happened even though the call failed. Left as it was otherwise.
/// @param completionWins whether a value the callback produced is returned even when the operation
///        was stopped while it ran.
/// @param body the callback, invoked at most once.
/// @return the callback's value; its failure; `parallelFor`'s failure when the callback never ran,
///         such as `ErrorCode::Cancelled`; the operation's `Cancelled` or `DeadlineExceeded` when it
///         was stopped while the callback ran and `completionWins` is false; or
///         `ErrorCode::Internal` when the region succeeded without running the callback.
template <typename CallableT>
auto call(const ExecutionContext& execution, bool& entered, bool completionWins, CallableT&& body)
    -> std::invoke_result_t<CallableT>
{
    std::optional<std::invoke_result_t<CallableT>> result;
    const auto scheduled = parallelFor(execution, 1u, {0u, 1u}, [&](TaskRange) -> Status {
        entered = true;
        result.emplace(guarded(std::forward<CallableT>(body)));
        return result->status();
    });
    if (completionWins && result && result->hasValue())
        return std::move(*result);
    if (scheduled.failed())
        return scheduled;
    if (auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (!result)
        return Status::failure(ErrorCode::Internal);
    return std::move(*result);
}
} // namespace an::scheduled_detail
