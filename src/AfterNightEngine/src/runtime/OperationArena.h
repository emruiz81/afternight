// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the threads one operation was admitted, and the arena it runs them in.
//
// The engine context owns a budget of threads; this is what one operation was granted out of it,
// and it is a `tbb::task_arena` rather than a number for a reason that decides how much adopted
// code has to be rewritten.
//
// **A nested `tbb::parallel_for` inherits the concurrency of the arena it runs in.** Code that calls
// oneTBB directly - an algorithm this engine adopts rather than writes, or a library that
// parallelises with oneTBB inside - therefore respects the user's setting once its call site runs
// inside this arena, with no change to that code. That is the whole reason the grant is an arena and
// not an integer the kernels would each have to be taught about. A number would work for the loops
// this engine writes itself, which all reach the arena through `Runtime/TaskScheduler.h`, and would
// silently miss every one it adopts.
//
// The arena's concurrency is the grant, so an operation narrowed to two threads is narrow even in a
// context that is otherwise idle, and the sum of the grants outstanding in one context never
// exceeds that context's maximum. An operation that arrives when the budget is exhausted is
// refused before an arena is built, so every arena's width is part of that sum.
//
// Constructing an arena costs no threads. oneTBB fixes an arena's parameters at construction and
// defers reserving anything until the arena is first used, so an operation that turns out to run
// nothing in parallel has cost one small allocation and nothing else.

#pragma once

#include <oneapi/tbb/task_arena.h>

#include <cstdint>
#include <memory>
#include <utility>

#include "runtime/EngineContextState.h"

namespace an
{

/// The threads one operation was granted, and the arena that spends them.
///
/// Owned by the lease that acquired it, so its lifetime is the operation's: the tokens go back to
/// the engine context's budget when the last holder lets go. It keeps its engine context's state
/// alive for the same reason a lease does - a context handle destroyed while an operation is still
/// running is a defect whose answer here is a late release rather than a dangling one.
class OperationArena
{
public:
    /// Takes ownership of a grant the context's budget has already made.
    ///
    /// @param context the engine context this operation belongs to. Never null on this path.
    /// @param tokens how many threads the budget granted, which is at least one.
    OperationArena(std::shared_ptr<EngineContextState> context, std::uint32_t tokens)
        : m_context(std::move(context)), m_tokens(tokens), m_arena(static_cast<int>(tokens), 1u)
    {
    }

    /// Gives the granted threads back to the engine context's budget.
    ~OperationArena()
    {
        if (m_context != nullptr)
        {
            m_context->releaseConcurrency(m_tokens);
        }
    }

    /// Neither copied nor moved: the lease holds one of these by shared pointer, and a copy would
    /// be a second set of threads claiming to be the same grant.
    OperationArena(const OperationArena&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    OperationArena& operator=(const OperationArena&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    OperationArena(OperationArena&&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    OperationArena& operator=(OperationArena&&) = delete;

    /// The threads this operation runs on.
    ///
    /// Work submitted through it, and any nested `tbb::parallel_for` inside that work, is bounded by
    /// this arena's concurrency. Submitting from several threads at once is oneTBB's own contract.
    ///
    /// @return the arena, valid while this grant is.
    [[nodiscard]] tbb::task_arena& arena() noexcept
    {
        return m_arena;
    }

    /// How many threads this operation may run at once.
    ///
    /// The grant, which is never zero because a request made at an exhausted budget is refused.
    ///
    /// @return the arena's concurrency.
    [[nodiscard]] std::uint32_t concurrency() const noexcept
    {
        return m_tokens;
    }

    /// How many threads the budget actually gave up for this operation.
    ///
    /// It is what `concurrency()` returns and what the release gives back.
    ///
    /// @return the granted tokens.
    [[nodiscard]] std::uint32_t tokens() const noexcept
    {
        return m_tokens;
    }

    /// The grant behind one lease, for the engine's own use.
    ///
    /// @param lease the lease to look inside.
    /// @return the grant it holds, or null when it holds none.
    [[nodiscard]] static OperationArena* of(const EngineContextLease& lease) noexcept
    {
        return lease.m_arena.get();
    }

    /// The grant behind one execution context, for the engine's own use.
    ///
    /// @param execution the operation to look inside.
    /// @return the grant its lease holds, or null when it holds none.
    [[nodiscard]] static OperationArena* of(const ExecutionContext& execution) noexcept
    {
        return of(execution.m_lease);
    }

private:
    /// The engine context whose budget this grant came from. Shared, so the budget outlives a
    /// handle destroyed while this operation is still running.
    std::shared_ptr<EngineContextState> m_context;

    /// How many threads the budget gave up, which is what the destructor returns.
    std::uint32_t m_tokens;

    /// The threads themselves. Constructed with the grant and never reinitialised.
    tbb::task_arena m_arena;
};

} // namespace an
