// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - running an operation's cooperating stages on host threads and joining them.
//
// One stop source is shared by every stage and forwarded from the operation's own stop token, so a
// caller's stop request and the first stage failure reach every stage the same way. The first
// failure is recorded under a mutex and the rest are dropped: a later failure is usually a stage
// noticing that the first one stopped it. Stage zero runs on the calling thread, so a pipeline of
// one stage starts no thread at all, and the threads for the others are `std::jthread`s outside the
// operation's task arena, which is what keeps a blocked coordinator from occupying a worker.

#include "runtime/CooperativeTasks.h"

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "runtime/EngineContextState.h"

#include <array>
#include <mutex>
#include <stop_token>
#include <thread>

namespace an
{
Status runCooperativeTasks(const ExecutionContext& execution, std::uint32_t count, CooperativeTaskBody& body)
{
    if (!execution.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (count == 0u || count > 3u)
        return Status::failure(ErrorCode::InvalidArgument);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;

    std::stop_source stop;
    std::stop_callback forward(execution.stopToken(), [&]() noexcept { stop.request_stop(); });
    std::mutex mutex;
    Status first;
    auto fail = [&](Status status) {
        if (status.failed())
        {
            {
                const std::lock_guard lock(mutex);
                if (first.ok())
                    first = std::move(status);
            }
            stop.request_stop();
        }
    };
    auto run = [&](std::uint32_t stage) noexcept {
        auto linked = EngineContextState::linkedExecution(execution, stop.get_token(), stage == 0u);
        Status status;
        auto boundary = callAcrossBoundary({"scheduler", "cooperative stage", ErrorDomain::Runtime},
                                           [&] { status = body.run(linked, stage); });
        fail(boundary.failed() ? std::move(boundary) : std::move(status));
    };

    // Allocation/thread-start failure stops already-started coordinators before joining them.
    std::array<std::jthread, 2u> workers;
    auto started = callAcrossBoundary({"scheduler", "coordinator start", ErrorDomain::Runtime}, [&] {
        for (std::uint32_t stage = 1u; stage < count; ++stage)
            workers[stage - 1u] = std::jthread([&, stage] { run(stage); });
    });
    if (started.failed())
        fail(std::move(started));
    else
        run(0u);
    for (auto& worker : workers)
        if (worker.joinable())
            worker.join();
    return first;
}
} // namespace an
