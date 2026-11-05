// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what an engine context owns, behind the handle that owns it.
//
// The public header names this type and never defines it, which is what makes a context's contents
// opaque without a hand-written indirection: the handle holds a pointer to one of these, and the
// published surface therefore says nothing about the thread budget, the budgeted memory resource and
// the codec registry that live here, nor about the oneTBB arenas the budget is spent through.
//
// Held by `std::shared_ptr` rather than owned uniquely by the handle, for one reason: a lease is a
// second reference to it. A caller that destroys a context while an operation is still running has
// a defect, and the two possible answers to that defect are a use-after-free or a late release.
// Sharing the state chooses the second one.
//
// Three counters are mutable, and they are atomics; the memory resource synchronises itself; and
// everything else is immutable once constructed, which is why nothing here is locked: the
// configuration is written once by the factory and read by anyone afterwards.
//
// One of those counters is the thread budget, and what it is *not* is worth stating: there is no
// task arena here. The threads a context owns are lent out as grants, one arena per operation, and
// that arena lives in `runtime/OperationArena.h` beside the lease that holds it - because a nested
// `tbb::parallel_for` inherits the concurrency of the arena it runs in, and an operation narrowed to
// two threads has to be narrow for the ported algorithms it calls as well as for the loops this
// engine writes. A single context-wide arena would make every operation as wide as the context.
//
// The budget is also the reason the maximum concurrency is fixed at construction. oneTBB fixes an
// arena's concurrency when the arena starts, and reinitialising one that has work in it is not
// safe; the engine's answer is to build a replacement context rather than reconfigure this one.
// Nothing anywhere in this engine constructs a `tbb::global_control`, which resizes the pool for the
// whole process; a per-operation arena drawn from a per-context budget is the local answer to the
// same question.

#pragma once

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "memory/BudgetedMemoryResource.h"

#include <atomic>
#include <cstdint>
#include <limits>

namespace an
{

/// The services, configuration and identity behind one engine context.
class EngineContextState
{
public:
    /// Builds the state of a context whose options have already been validated.
    ///
    /// @param id this context's identity, from `nextId()`.
    /// @param maxConcurrency the resolved maximum, which is at least one.
    /// @param limits the configured maxima and memory budget, copied.
    /// @param codecs the codec registry the caller configured, retained for the context's life; null
    ///        for none. Held by pointer, so this layer needs no IO header.
    /// @param upstream the caller's resource the context's own resource takes its admitted bytes
    ///        from, or null for the platform allocator. Borrowed; it must outlive this state.
    EngineContextState(std::uint64_t id, std::uint32_t maxConcurrency, const EngineLimits& limits,
                       std::shared_ptr<const CodecRegistry> codecs, MemoryResource* upstream) noexcept
        : m_id(id), m_maxConcurrency(maxConcurrency), m_limits(limits), m_memory(limits, upstream),
          m_codecs(std::move(codecs))
    {
    }

    /// Releases what this context owns.
    ///
    /// It is safe to run because a context that still had work in it refused to shut down, and
    /// because every operation's arena is owned by that operation's lease rather than by this:
    /// `EngineContext::shutdown` enforces the first, and the grants enforce the second.
    ~EngineContextState() = default;

    /// Neither copied nor moved: the handle and its leases refer to one of these, and a second
    /// copy would be a second set of services claiming to be the same context.
    EngineContextState(const EngineContextState&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    EngineContextState& operator=(const EngineContextState&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    EngineContextState(EngineContextState&&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    EngineContextState& operator=(EngineContextState&&) = delete;

    /// This context's identity. Immutable, so reading it needs no synchronisation.
    ///
    /// @return the identity this state was built with, which is never zero.
    [[nodiscard]] std::uint64_t id() const noexcept
    {
        return m_id;
    }

    /// The resolved maximum concurrency. Immutable, which is the whole point of it.
    ///
    /// @return the maximum this state was built with.
    [[nodiscard]] std::uint32_t maxConcurrency() const noexcept
    {
        return m_maxConcurrency;
    }

    /// The configured maxima and memory budget. Immutable.
    ///
    /// @return a reference to the limits this state was built with, valid while the state is.
    [[nodiscard]] const EngineLimits& limits() const noexcept
    {
        return m_limits;
    }

    /// Where this context's persistent memory comes from.
    ///
    /// One per context, so the configured budget bounds what this context has outstanding and
    /// nothing else. Safe to use from several threads, which one resource serving a whole context
    /// has to be.
    ///
    /// @return the resource, valid while this state is.
    [[nodiscard]] MemoryResource& memory() noexcept
    {
        return m_memory;
    }

    /// The codec registry this context was built with. Immutable.
    ///
    /// @return the registry, valid while this state is; or null when the context was built without
    ///         one.
    [[nodiscard]] const CodecRegistry* codecRegistry() const noexcept
    {
        return m_codecs.get();
    }

    /// Counts one more piece of work as outstanding.
    ///
    /// Acquire-release, so that whatever the holder does under its lease is ordered against the
    /// shutdown that later observes the count fall to zero.
    void acquireLease() noexcept
    {
        m_outstandingLeases.fetch_add(1u, std::memory_order_acq_rel);
    }

    /// Counts one piece of work as finished. Called exactly once per acquired lease.
    void releaseLease() noexcept
    {
        m_outstandingLeases.fetch_sub(1u, std::memory_order_acq_rel);
    }

    /// How many leases are outstanding.
    ///
    /// @return the current count, which another thread may change immediately afterwards.
    [[nodiscard]] std::uint64_t outstandingLeases() const noexcept
    {
        return m_outstandingLeases.load(std::memory_order_acquire);
    }

    /// Grants a piece of work its share of this context's threads.
    ///
    /// What is asked for when that much is unclaimed, and what is left when it is not. Zero tells
    /// the caller to refuse the operation; otherwise a one-worker arena would execute outside this
    /// accounting and the active widths could exceed the configured maximum.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param requested how many threads the work would use. Zero grants zero.
    /// @return the granted tokens, which never take the outstanding total past the maximum.
    [[nodiscard]] std::uint32_t admitConcurrency(std::uint32_t requested) noexcept
    {
        if (requested == 0u)
        {
            return 0u;
        }

        const std::uint64_t capacity = m_maxConcurrency;
        std::uint64_t current = m_admittedConcurrency.load(std::memory_order_acquire);
        for (;;)
        {
            const std::uint64_t unclaimed = capacity > current ? capacity - current : 0u;
            const std::uint64_t granted = requested < unclaimed ? requested : unclaimed;
            if (granted == 0u)
            {
                return 0u;
            }

            if (m_admittedConcurrency.compare_exchange_weak(current, current + granted, std::memory_order_acq_rel,
                                                            std::memory_order_acquire))
            {
                return static_cast<std::uint32_t>(granted);
            }
        }
    }

    /// Gives a granted share back. Called exactly once per non-zero grant.
    ///
    /// @param granted what `admitConcurrency` returned.
    void releaseConcurrency(std::uint32_t granted) noexcept
    {
        if (granted != 0u)
        {
            m_admittedConcurrency.fetch_sub(granted, std::memory_order_acq_rel);
        }
    }

    /// How many threads the outstanding work was granted between them.
    ///
    /// @return the granted total, which never exceeds this context's maximum. Another thread may
    ///         change it immediately afterwards.
    [[nodiscard]] std::uint32_t admittedConcurrency() const noexcept
    {
        const std::uint64_t admitted = m_admittedConcurrency.load(std::memory_order_acquire);
        constexpr std::uint64_t kCeiling = (std::numeric_limits<std::uint32_t>::max)();
        return static_cast<std::uint32_t>(admitted < kCeiling ? admitted : kCeiling);
    }

    /// How many of this context's threads nothing has claimed.
    ///
    /// @return the unclaimed threads, which saturates at zero rather than going negative.
    [[nodiscard]] std::uint32_t availableConcurrency() const noexcept
    {
        const std::uint64_t admitted = m_admittedConcurrency.load(std::memory_order_acquire);
        const std::uint64_t capacity = m_maxConcurrency;
        return static_cast<std::uint32_t>(capacity > admitted ? capacity - admitted : 0u);
    }

    /// The identity for the next scratch arena owned by this context.
    ///
    /// Per-context rather than process-wide: the context identity and this sequence together name
    /// an arena uniquely, and destroying every context leaves no scratch service or counter behind.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return a non-zero identity not previously issued by this context.
    [[nodiscard]] std::uint64_t nextScratchArenaId() noexcept
    {
        return m_nextScratchArenaId.fetch_add(1u, std::memory_order_relaxed) + 1u;
    }

    /// The state behind one lease, for the engine's own use.
    ///
    /// The state is what both handles point to, so it is also what knows how to be reached from
    /// one. It is a static member of this class rather than a free function so that the friendship
    /// the handles grant names one type instead of a list of entry points.
    ///
    /// @param lease the lease to look inside.
    /// @return the state it holds, or null when it holds nothing.
    [[nodiscard]] static EngineContextState* of(const EngineContextLease& lease) noexcept
    {
        return lease.m_state.get();
    }

    /// The state behind one execution context, for the engine's own use.
    ///
    /// @param execution the operation to look inside.
    /// @return the state its lease holds, or null when it holds nothing.
    [[nodiscard]] static EngineContextState* of(const ExecutionContext& execution) noexcept
    {
        return of(execution.m_lease);
    }

    /// Keeps an execution's engine context open for a result that outlives the execution, without
    /// claiming any threads.
    ///
    /// @param execution the execution whose context is retained. Must hold something: the state is
    ///        not checked for null.
    /// @return a lease of its own, holding no threads, which the caller releases with the storage
    ///         of the result it keeps.
    [[nodiscard]] static EngineContextLease retain(const ExecutionContext& execution) noexcept
    {
        auto state = execution.m_lease.m_state;
        state->acquireLease();
        return EngineContextLease(std::move(state), {});
    }

    /// An execution for one cooperating stage of a running operation.
    ///
    /// It is the same operation - the same identity, thread grant, sinks, report ordering, deadline
    /// and backend policy - under a stop token of the caller's choosing, which the caller forwards
    /// from the operation's own. It takes a lease of its own on the context without claiming more
    /// threads. At most one stage execution may borrow the operation's scratch arena, because an
    /// arena is not thread-safe, and every stage execution must be gone before the operation returns.
    ///
    /// @param execution the running operation. Must hold something: the state is not checked for
    ///        null.
    /// @param token the stop token the stage observes.
    /// @param borrowScratch whether this stage borrows the operation's scratch arena.
    /// @return the stage's execution.
    [[nodiscard]] static ExecutionContext linkedExecution(const ExecutionContext& execution, std::stop_token token,
                                                          bool borrowScratch) noexcept
    {
        auto state = execution.m_lease.m_state;
        state->acquireLease();
        EngineContextLease lease(std::move(state), execution.m_lease.m_arena);
        ExecutionOptions options;
        options.stopToken = token;
        options.deadline = execution.m_deadline;
        options.progressSink = execution.m_progressSink;
        options.diagnosticSink = execution.m_diagnosticSink;
        options.traceSink = execution.m_traceSink;
        options.scratch = borrowScratch ? execution.m_scratch : nullptr;
        options.backend = execution.m_requestedBackend;
        options.instructionSet = execution.m_requestedInstructionSet;
        options.backendFallback = execution.m_backendFallback;
        options.reductionDeterminism = execution.m_reductionDeterminism;
        return ExecutionContext(std::move(lease), options, execution.m_telemetry, execution.m_operationId,
                                execution.m_maxConcurrency, execution.m_backend, execution.m_instructionSet,
                                execution.m_limits);
    }

    /// The identity for the next context this process builds.
    ///
    /// A counter rather than an address, so that an identity is never reused by a later context -
    /// which is what a log correlating work to a context depends on. It is the one datum in this
    /// engine that outlives every context, and it owns nothing: destroying every context leaves a
    /// number behind and no service, no thread and no allocation.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return an identity no other context has held, counting from one.
    [[nodiscard]] static std::uint64_t nextId() noexcept
    {
        static std::atomic<std::uint64_t> counter{0u};
        return counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
    }

private:
    /// This context's identity, unique in the process and never reused.
    std::uint64_t m_id;

    /// The resolved maximum concurrency, at least one and no more than the configurable ceiling.
    std::uint32_t m_maxConcurrency;

    /// The configured maxima and memory budget this context works within.
    EngineLimits m_limits;

    /// Where this context's persistent memory comes from, within the budget in the limits above.
    /// Takes its bytes from the caller's resource when one was supplied, and from the platform
    /// otherwise; either way the budget and the counters are this context's.
    BudgetedMemoryResource m_memory;

    /// Immutable adapter configuration; codecs never own a lease back to this context.
    std::shared_ptr<const CodecRegistry> m_codecs;

    /// How many leases are outstanding.
    std::atomic<std::uint64_t> m_outstandingLeases{0u};

    /// How many threads those leases were granted between them, which never exceeds the maximum
    /// above. Sixty-four bits so the compare-exchange that maintains it needs no narrowing.
    std::atomic<std::uint64_t> m_admittedConcurrency{0u};

    /// The next identity issued to a scratch arena owned by this context.
    std::atomic<std::uint64_t> m_nextScratchArenaId{0u};
};

} // namespace an
