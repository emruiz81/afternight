// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The object that owns what a library would otherwise keep in globals.
///
/// A static facade gives initialisation, shutdown, logging, settings and thread limits a hidden
/// lifetime, lets a helper set a process-wide parallelism limit from a literal, and connects the
/// user's thread budget to none of it. An EngineContext is the answer to all of that at once. It is
/// an ordinary object with an ordinary lifetime, a caller may hold two of them at the same time, and
/// nothing in this engine requires one to exist.
///
/// Three rules shape the interface, and each of them costs a caller something.
///
/// **The maximum concurrency is fixed when the context is built.** There is no setter, because a
/// setter is what lets one call site widen a number the user chose. Changing it means quiescing the
/// outstanding work, shutting the context down and creating a replacement - which is what
/// `createReplacement` does and what the task arenas its threads run in require: an arena's
/// concurrency is fixed once it starts, and terminating one while work is in it is not safe.
///
/// **The context is move-only and its state is opaque.** Move-only because a copy would be two
/// owners of one set of services, and the second destructor would release what the first still
/// uses. Opaque because the services it owns are implementation types - a thread budget whose
/// grants are oneTBB task arenas, a budgeted memory resource - and a published header that named
/// them would put an external library into the public surface.
///
/// **Work in flight is counted, and a context that has any refuses to shut down.** That is what
/// makes "quiesce, then replace" a checkable instruction rather than a hopeful one. The counting is
/// a lease: an operation holds one for as long as it is running, and while any lease is outstanding
/// the context is not quiescent. A lease also carries that work's share of the machine, because a
/// share and a hold have one lifetime and two things to release is one thing to forget.
///
/// What a context owns is the threads, the memory and, when a caller supplies one, a codec
/// registry. The threads are a budget rather than a pool: an operation is granted part of it and
/// runs in a task arena of exactly that width, so a nested `tbb::parallel_for` - including one
/// inside an algorithm this engine adopted rather than wrote - inherits the operation's share
/// without being told about it, and the grants outstanding in one context never add up to more
/// than its maximum. `Runtime/TaskScheduler.h` is how a kernel reaches them, and nothing in this
/// engine may cap parallelism process-wide instead. The memory is one budgeted allocator, so the
/// configured budget bounds what this context has outstanding and nothing else;
/// `Runtime/MemoryResource.h` is its interface and `Runtime/ScratchArena.h` is the operation-scoped
/// half. The dispatch table is not owned here at all - what implementation a kernel runs on is a
/// property of the build and the host rather than of a context, so it lives behind
/// `Runtime/BackendDispatch.h` as an immutable constant.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineCapabilities.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace an
{

/// The services an engine context owns, declared here and defined only inside the engine.
///
/// Named in this header so that a context and a lease can hold a pointer to one; it is never
/// defined in the published surface. A caller can do nothing with the name, which is the point:
/// what a context owns - its thread budget, its budgeted memory resource, its codec registry - is
/// implementation, and the budget is spent through oneTBB types that must not cross into a public
/// header.
class EngineContextState;

/// The threads one operation was granted, declared here and defined only inside the engine.
///
/// Named in this header so that a lease can hold a pointer to one. It is a task arena sized to what
/// the engine context's budget granted, and a task arena is a library type that must not cross into
/// a public header - which is the whole reason a caller sees a name and nothing else.
class OperationArena;

/// Where an engine context's persistent memory comes from. Defined in `Runtime/MemoryResource.h`.
///
/// Named here rather than included, because a context only hands out a pointer to one: a caller
/// that holds a context and never allocates should not pay for the allocator's vocabulary. Include
/// that header to use the resource this context returns.
class MemoryResource;

/// One operation's context-bound scratch allocator. Defined in `Runtime/ScratchArena.h`.
class ScratchArena;

/// The immutable codec configuration a context may retain. Defined in `IO/CodecRegistry.h`.
///
/// Named here rather than included, because the runtime layer never includes the IO layer: a
/// context only holds the registry a caller built and hands it back.
class CodecRegistry;

/// The maximum concurrency that asks for the host's own.
///
/// Stated as a request rather than as a number, because the number is not knowable until the
/// engine asks the machine. A context never reports this value back: `maxConcurrency()` is what
/// construction resolved it to, which is at least one.
inline constexpr std::uint32_t kUseHostConcurrency = 0u;

/// The largest maximum concurrency a context may be configured with.
///
/// A filter against nonsense rather than a policy: four thousand is an order of magnitude above the
/// largest machine this engine expects to run on and far below the values a wrong field or an
/// unchecked calculation produces. A host that genuinely reports more than this is clamped to it
/// rather than refused, because a caller that asked for the host's concurrency asked a question,
/// not for a number this engine should then reject.
inline constexpr std::uint32_t kMaxConfigurableConcurrency = 4096u;

/// Everything a caller states when building an engine context.
///
/// A plain aggregate with a default for every member, so a caller names only what it is changing:
///
/// @code
/// an::EngineContextOptions options;
/// options.maxConcurrency = 4u;
/// an::Result<an::EngineContext> context = an::EngineContext::create(options);
/// @endcode
///
/// Every member is immutable once the context is built. Changing one means building a replacement.
struct EngineContextOptions
{
    /// The most threads the engine may use at once, or `kUseHostConcurrency` for the host's own.
    ///
    /// A ceiling, never a demand: the engine routinely uses fewer, because a small tile or a
    /// bandwidth-bound pass does not scale to it. Must be no greater than
    /// `kMaxConfigurableConcurrency`, or construction fails with `ErrorCode::LimitExceeded`.
    std::uint32_t maxConcurrency = kUseHostConcurrency;

    /// The configured maxima and memory budget this context works within.
    ///
    /// Each maximum, and the memory budget, must be nonzero - a limit of zero admits nothing at
    /// all, which is a misconfiguration rather than a strict policy, and construction refuses it
    /// with `ErrorCode::InvalidArgument` naming the field.
    EngineLimits limits = defaultEngineLimits();

    /// The codecs operations in this context may read and write files with, or null for none.
    ///
    /// Built through `IO/CodecRegistry.h` before the context is created, and shared: the context
    /// keeps its own reference for its whole life. With none, every read or write that needs a
    /// codec in this context is refused with `ErrorCode::Unsupported`.
    std::shared_ptr<const CodecRegistry> codecRegistry{};

    /// Where this context's bytes ultimately come from, or null for the platform allocator.
    ///
    /// Borrowed, never owned: it must outlive the context and every lease the context hands out.
    /// The context still owns its own resource, still enforces `limits.memoryBudgetBytes`, and still
    /// reports its own counters - what changes is only where the admitted bytes are taken from, and
    /// a refusal from here is returned to the caller exactly as it was given.
    ///
    /// This exists because `Runtime/MemoryResource.h` states that a caller may implement a resource
    /// in order to refuse an allocation at a chosen boundary, and a resource with no way into a
    /// context is a sentence rather than a seam. Nothing in the engine reaches for it, so an ordinary
    /// caller leaves it null and a fault campaign does not.
    MemoryResource* memoryResource = nullptr;
};

/// A statement that one piece of work is outstanding in an engine context, and how much of the
/// machine it claimed.
///
/// An operation holds a lease for as long as it is running. While any lease exists the context is
/// not quiescent and refuses to shut down, which is what makes destroying a context and building a
/// replacement a safe instruction: the caller is told when there is still something running rather
/// than finding out afterwards.
///
/// The share of the machine is here rather than in a second object because it has exactly this
/// lifetime: work that has finished is neither outstanding nor holding threads, and two things to
/// release is one thing to forget. A lease taken without asking for threads holds none, which is
/// the right answer for a caller that only wants the context kept open.
///
/// Move-only, because a copy would be a second hold nobody asked for. Releasing is idempotent: a
/// lease released by hand and then again by its destructor counts once, for the share as well as
/// for the hold.
///
/// A lease keeps the context's state alive. Destroying the context handle while a lease is
/// outstanding is a defect in the caller, and the engine's answer to it is a late release rather
/// than a use-after-free - the state and its services survive until the last lease goes.
class EngineContextLease
{
public:
    /// A lease that holds nothing.
    ///
    /// The state a lease is left in by a move and by `release()`, and a usable default for a
    /// member that is assigned later. `valid()` is false and releasing it does nothing.
    EngineContextLease() noexcept = default;

    /// Releases the hold, if this lease still has one.
    AFTERNIGHTENGINE_API ~EngineContextLease();

    /// Takes over another lease's hold and its share, leaving it holding nothing.
    ///
    /// Allocation-free, and neither the count nor the budget changes: one hold moved, not a second
    /// one taken.
    ///
    /// @param other the lease to take over. Left holding nothing.
    EngineContextLease(EngineContextLease&& other) noexcept = default;

    /// Releases this lease's hold and share, and takes over another's.
    ///
    /// The release happens first, so the count and the budget go down by the lease that was
    /// overwritten and up by nothing. Self-assignment is a no-op and keeps the hold.
    ///
    /// Allocation-free.
    ///
    /// @param other the lease to take over. Left holding nothing.
    /// @return this lease.
    AFTERNIGHTENGINE_API EngineContextLease& operator=(EngineContextLease&& other) noexcept;

    /// Copying is deleted: a lease is one hold, and copying it would raise the count without a
    /// call site asking for it.
    EngineContextLease(const EngineContextLease&) = delete;

    /// Copying is deleted. See the copy constructor.
    EngineContextLease& operator=(const EngineContextLease&) = delete;

    /// Whether this lease still holds a context.
    ///
    /// Thread-safe with respect to other leases, and allocation-free.
    ///
    /// @return false for a default-constructed lease, one that has been moved from, and one that
    ///         has been released.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// Which context this lease holds.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the context's identity, or `0` when this lease holds nothing. No live context has
    ///         an identity of zero.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t contextId() const noexcept;

    /// How many threads this lease's work may run at once.
    ///
    /// What the engine context's budget had to give when the lease was taken, which is at most what
    /// was asked for. A lease that asked for nothing reports zero. A lease that asks for threads
    /// when the budget is exhausted is refused rather than admitted outside the accounting.
    ///
    /// Fixed for the life of the lease. Threads freed by other work finishing do not widen it,
    /// because an operation that grew halfway through would be an operation whose partition
    /// depended on when it was asked.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the granted width, or `0` for a lease that holds nothing or asked for nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t admittedConcurrency() const noexcept;

    /// Gives up the hold and the share now rather than at the end of the scope.
    ///
    /// For the operation that finishes before its lease goes out of scope. Calling it on a lease
    /// that holds nothing does nothing, so releasing by hand and then again by destructor counts
    /// once.
    ///
    /// Thread-safe with respect to other leases, and allocation-free.
    AFTERNIGHTENGINE_API void release() noexcept;

private:
    friend class EngineContext;

    /// The state is what the handles point to, so it is also what knows how to be reached from one.
    /// See this header's declaration of `EngineContextState`.
    friend class EngineContextState;

    /// The grant is reached from the lease that holds it, for the same reason. See this header's
    /// declaration of `OperationArena`.
    friend class OperationArena;

    /// Takes a hold and a grant that the context has already made.
    ///
    /// @param state the context's state, already incremented by `EngineContext::acquireLease`.
    /// @param arena the threads that call granted, or null when none were asked for.
    EngineContextLease(std::shared_ptr<EngineContextState> state, std::shared_ptr<OperationArena> arena) noexcept
        : m_state(std::move(state)), m_arena(std::move(arena))
    {
    }

    /// The context this lease holds, or null. Shared rather than borrowed, so that the state
    /// outlives a handle destroyed while this lease is still outstanding.
    std::shared_ptr<EngineContextState> m_state;

    /// The threads this lease's work was granted, or null when it asked for none. Releasing it is
    /// what returns them to the context's budget.
    std::shared_ptr<OperationArena> m_arena;
};

/// One engine's owned services, configuration and lifetime.
///
/// Built through `create`, which validates what it is given and reports why it refused. Move-only.
/// Nothing in this engine requires a context to exist, and nothing holds one on a caller's behalf:
/// there is no instance accessor, no lazy initialisation, and no process-wide default. A caller
/// that wants two, with different settings, has two.
///
/// The configuration accessors are immutable and safe to read from several threads at once. The
/// two operations that change what the handle owns - `shutdown()` and the `createReplacement` that
/// uses it - are not: like any other object, one context must not be modified by one thread while
/// another reads it.
class EngineContext
{
public:
    /// A context that holds nothing.
    ///
    /// The state a context is left in by a move and by `shutdown()`, and a usable default for a
    /// member that is assigned later. It owns no services and reports zeroes; `valid()` is how to
    /// ask.
    EngineContext() noexcept = default;

    /// Releases the services this context owns.
    ///
    /// Destroying a context that is not quiescent is a defect in the caller: use `shutdown()`,
    /// which reports it, or let the outstanding leases go first. The engine's answer to the defect
    /// is a late release rather than a use-after-free - the state survives under the outstanding
    /// leases and its services are released when the last of them goes.
    ~EngineContext() = default;

    /// Takes over another context's services, leaving it holding nothing.
    ///
    /// Allocation-free. The identity, concurrency and limits move with the services; nothing is
    /// re-resolved, because nothing about a context is resolved twice.
    ///
    /// @param other the context to take over. Left holding nothing.
    EngineContext(EngineContext&& other) noexcept = default;

    /// Releases this context's services and takes over another's.
    ///
    /// The context being overwritten is released here, under the same rule as the destructor: a
    /// caller that overwrites a context which is not quiescent has a defect, and what it gets is a
    /// late release rather than a dangling one.
    ///
    /// Allocation-free.
    ///
    /// @param other the context to take over. Left holding nothing.
    /// @return this context.
    EngineContext& operator=(EngineContext&& other) noexcept = default;

    /// Copying is deleted: a copy would be two owners of one set of services, and the second
    /// destructor would release what the first still uses.
    EngineContext(const EngineContext&) = delete;

    /// Copying is deleted. See the copy constructor.
    EngineContext& operator=(const EngineContext&) = delete;

    /// Builds a context, or reports why it cannot.
    ///
    /// The options are validated before anything is acquired, in a fixed order: the concurrency
    /// setting first, then each configured limit in the order `EngineLimits` declares them. The
    /// order is fixed so that the same options always meet the same refusal, and the first field
    /// named is the first one to repair.
    ///
    /// Thread-safe: contexts share nothing, so two threads may build one at the same time.
    /// Allocates the context's state.
    ///
    /// @param options what the caller is configuring. Copied; nothing in it is retained by
    ///        reference.
    /// @return the context, or a failure. `ErrorCode::LimitExceeded` when `maxConcurrency` is
    ///         above `kMaxConfigurableConcurrency`, carrying the requested value and the limit as
    ///         typed context; `ErrorCode::InvalidArgument` when a configured maximum or the memory
    ///         budget is zero, naming the field; `ErrorCode::AllocationFailed` when the state cannot
    ///         be allocated.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<EngineContext> create(const EngineContextOptions& options);

    /// Shuts a context down and builds its replacement.
    ///
    /// The only supported way to change a setting that is fixed at construction. The replacement is
    /// a different context: it shares no arena, no cache and no accounting with the one it
    /// replaces, because nothing is reinitialised in place.
    ///
    /// The order is deliberate and is what a caller has to plan around. The options are validated
    /// first, so a configuration mistake costs nothing. The previous context is then shut down,
    /// which fails if it is not quiescent and leaves it exactly as it was. Only then is the
    /// replacement built - so the machine is never held by two contexts at once, and a failure at
    /// that last step leaves the caller with no context rather than with the old one.
    ///
    /// Not thread-safe with respect to `previous`: it is being modified, so nothing else may be
    /// using it at the same time.
    ///
    /// @param previous the context to replace. Left holding nothing on success, and untouched when
    ///        this fails with anything other than an allocation failure.
    /// @param options what the replacement is configured with.
    /// @return the replacement, or a failure. `ErrorCode::Busy` when `previous` still has work
    ///         outstanding, carrying that refusal as its cause; otherwise whatever `create`
    ///         reports for these options.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<EngineContext>
    createReplacement(EngineContext& previous, const EngineContextOptions& options);

    /// Whether this context owns anything.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return false for a default-constructed context, one that has been moved from, and one that
    ///         has been shut down.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// This context's identity, for telling one from another in a diagnostic or a report.
    ///
    /// Unique among every context this process creates, and never reused: it is drawn from a
    /// counter, so a context destroyed and another created do not share one. It says nothing about
    /// the context beyond which one it is.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the identity, or `0` when this context holds nothing. No live context is zero.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t id() const noexcept;

    /// The most threads this context may use at once.
    ///
    /// What construction resolved, so it is a number rather than a request: never
    /// `kUseHostConcurrency`, never above `kMaxConfigurableConcurrency`, and never changed
    /// afterwards. A ceiling rather than a demand - the engine uses fewer whenever fewer are worth
    /// using.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the resolved maximum, or `0` when this context holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t maxConcurrency() const noexcept;

    /// The configured maxima and memory budget this context works within.
    ///
    /// Returned by value because it is a small immutable record and a caller usually reads several
    /// of its fields.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the limits construction was given, or a value with every field zero when this
    ///         context holds nothing - limits that admit nothing, which no live context reports.
    [[nodiscard]] AFTERNIGHTENGINE_API EngineLimits limits() const noexcept;

    /// What the loaded build offers.
    ///
    /// The same values `engineCapabilities()` reports, for the caller that already holds a context
    /// and should not have to know a free function exists. They describe the build and the host
    /// rather than this context, so a context that holds nothing still answers.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the version, binary-interface generation, host concurrency, and optional-module
    ///         presence of the binary that is running.
    [[nodiscard]] AFTERNIGHTENGINE_API EngineCapabilities capabilities() const noexcept;

    /// Where this context's persistent memory comes from.
    ///
    /// One resource per context, so the configured memory budget is enforced against what this
    /// context has outstanding and against nothing else, and two contexts account separately. Its
    /// counters are what a benchmark reports peak bytes from.
    ///
    /// Owned by the context and released with it, so a caller must not hold the pointer past the
    /// context - and must not still be holding memory from it either, which is a lifetime problem
    /// of the caller's own making rather than something the lease count can report.
    ///
    /// Thread-safe and allocation-free; the resource itself is safe to use from several threads.
    ///
    /// @return the resource, or null when this context holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API MemoryResource* memoryResource() const noexcept;

    /// The codec configuration this context was built with.
    ///
    /// Never a default made up here: a context built without one answers null.
    ///
    /// Thread-safe and allocation-free; the registry itself is immutable.
    ///
    /// @return the registry, valid while this context or any of its leases lives; or null when the
    ///         context was built without one or holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const CodecRegistry* codecRegistry() const noexcept;

    /// Takes a lease, so that the work about to run is counted as outstanding and given its share
    /// of the machine.
    ///
    /// The share is granted from the context's own budget: what is asked for when that much is
    /// free, and what is left when it is not. When nothing is left the lease is refused rather than
    /// admitted outside the accounting. Asking for more than this context permits narrows rather
    /// than refuses, because this is the engine dividing what it has rather than a caller stating a
    /// policy - `ExecutionContext::create` is where a request that would exceed the user's setting
    /// is refused.
    ///
    /// Thread-safe: several threads may hold leases on one context at the same time, and taking
    /// one is safe while another thread is reading this context.
    ///
    /// A lease that asks for threads allocates once, for the arena those threads run in; a lease
    /// that asks for none allocates nothing. Constructing that arena reserves no thread - oneTBB
    /// defers that to the arena's first use - so an operation that never runs anything in parallel
    /// costs one small object.
    ///
    /// @param requestedConcurrency how many threads the work would use. Zero asks for none, which
    ///        is what a caller that only wants the context kept open passes.
    /// @return the lease, or `ErrorCode::NotInitialized` when this context holds nothing,
    ///         `ErrorCode::Busy` when no requested thread remains, or
    ///         `ErrorCode::AllocationFailed` when the threads it asked for cannot be held.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<EngineContextLease>
    acquireLease(std::uint32_t requestedConcurrency = 0u) const;

    /// How many leases are outstanding.
    ///
    /// A diagnostic number rather than a decision: by the time a caller reads it another thread
    /// may have taken or released one. `quiescent()` has the same caveat, and `shutdown()` is the
    /// call that decides without a gap between asking and acting.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the number of leases outstanding, or `0` when this context holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t outstandingLeases() const noexcept;

    /// How many of this context's threads its outstanding work has claimed between it.
    ///
    /// Never more than `maxConcurrency()`. Work that arrives when nothing is left is refused, so
    /// every accepted execution is represented in this total.
    ///
    /// A diagnostic number rather than a decision, on the same terms as `outstandingLeases()`:
    /// another thread may take or release a grant immediately afterwards.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the granted total, or `0` when this context holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t admittedConcurrency() const noexcept;

    /// How many of this context's threads nothing has claimed.
    ///
    /// What the next lease would be granted if it asked for everything, and zero for a context whose
    /// work has claimed all of it.
    ///
    /// A diagnostic number rather than a decision, on the same terms as `outstandingLeases()`.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the unclaimed threads, or `0` when this context holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t availableConcurrency() const noexcept;

    /// Whether anything is outstanding in this context.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return true when no lease is outstanding, which a context holding nothing also reports.
    [[nodiscard]] bool quiescent() const noexcept
    {
        return outstandingLeases() == 0u;
    }

    /// Releases this context's services, and says whether it could.
    ///
    /// The checked half of destruction: a context with work outstanding refuses rather than
    /// pulling the ground out from under it. On success this context holds nothing afterwards, as
    /// though it had been moved from.
    ///
    /// Idempotent: shutting down a context that holds nothing succeeds, so cleanup that shuts one
    /// down and then lets a scope guard do it again is correct rather than an error to report.
    ///
    /// Not thread-safe with respect to this context, which is being modified. Allocates only the
    /// failure it reports.
    ///
    /// @return success, or `ErrorCode::Busy` when leases are still outstanding - in which case
    ///         nothing has been released and the context is exactly as it was.
    [[nodiscard]] AFTERNIGHTENGINE_API Status shutdown();

private:
    friend class ScratchArena;

    /// Takes ownership of a state the factory has already built and validated.
    ///
    /// @param state the context's services. Never null on this path.
    explicit EngineContext(std::shared_ptr<EngineContextState> state) noexcept : m_state(std::move(state)) {}

    /// What this context owns, or null when it owns nothing. Shared rather than unique so that an
    /// outstanding lease keeps the services alive past a handle destroyed too early.
    std::shared_ptr<EngineContextState> m_state;
};

} // namespace an
