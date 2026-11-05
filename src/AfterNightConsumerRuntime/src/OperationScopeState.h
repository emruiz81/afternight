// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an operation scope owns, in the order it is released. Private to the library: the session builds it when it
/// admits an operation, and the scope only moves the pointer to it.

#pragma once

#include "SessionCore.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ScratchArena.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>

namespace an::consumer
{

/// Requests a stop on the scope's own stop source when the caller's token is stopped.
struct ForwardStop
{
    std::stop_source* target = nullptr;

    void operator()() const noexcept
    {
        target->request_stop();
    }
};

/// A scope's state: a plain record whose release order is `end`'s.
///
/// The members are declared in the order they are acquired, so that destruction would release them in reverse even
/// without `end` - but `end` states the order rather than leaving it to declaration order, because the admission slot
/// must go last and is released by a call rather than by a destructor.
class OperationScope::State
{
public:
    State(std::shared_ptr<detail::SessionCore> admittedBy, OperationPurpose admittedPurpose,
          detail::AdmissionPool admittedPool, std::uint32_t admittedThreads)
        : core(std::move(admittedBy)), purpose(admittedPurpose), pool(admittedPool), granted(admittedThreads)
    {
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

    ~State()
    {
        end();
    }

    /// Releases the execution - returning its threads to the engine - then the scratch arena it borrowed, then the
    /// link to the caller's stop token, and last the admission slot, which wakes whatever waits for it.
    void end() noexcept
    {
        execution = an::ExecutionContext{};
        scratch.reset();
        link.reset();
        if (core != nullptr)
        {
            core->releaseOperation(pool, granted);
            core.reset();
        }
        granted = 0u;
    }

    /// The session whose admission slot this holds; null once ended.
    std::shared_ptr<detail::SessionCore> core;

    /// Why the operation runs.
    OperationPurpose purpose = OperationPurpose::Apply;

    /// The share it was admitted from.
    detail::AdmissionPool pool = detail::AdmissionPool::Other;

    /// The threads admission granted.
    std::uint32_t granted = 0u;

    /// The operation's own stop state, which the execution observes.
    std::stop_source stopSource;

    /// Forwards the caller's stop token to `stopSource`.
    std::optional<std::stop_callback<ForwardStop>> link;

    /// The scratch arena the execution borrows, if the request asked for one.
    std::optional<an::ScratchArena> scratch;

    /// The execution, holding the engine lease and the threads.
    an::ExecutionContext execution;
};

} // namespace an::consumer
