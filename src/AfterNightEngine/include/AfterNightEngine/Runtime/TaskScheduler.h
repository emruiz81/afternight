// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a loop is spread across the threads an operation was allowed.
///
/// An execution context states how much of the machine one operation may use. This is what makes
/// that statement true, and it is the only way a kernel in this engine starts parallel work: there
/// is no `tbb::parallel_for` written at a call site, because a call site that writes one has no way
/// of knowing what else is running.
///
/// Five decisions shape it.
///
/// **The threads belong to the engine context, not to the helper that wants them.** The current
/// engine caps parallelism by constructing a `tbb::global_control` inside a workflow helper, which
/// is process-wide: one stacking run quietly resizes the pool every other part of the application is
/// using, and two of them disagree. Here an engine context owns a *budget* of threads and lends part
/// of it to each operation, which runs in a task arena of exactly that width. Two contexts share
/// nothing at all, and the grants outstanding in one context never add up to more than its maximum.
///
/// **An operation's grant is an arena rather than a number, and that is what makes it propagate.** A
/// nested `tbb::parallel_for` inherits the concurrency of the arena it runs in, so code this engine
/// adopts rather than writes - code that calls `parallel_for` itself and knows nothing about any
/// setting - respects the user's thread budget the moment its call site runs inside the operation's
/// arena, with no change to that code at all. A number would have worked for the loops below and
/// silently missed all of that.
///
/// **A loop is walked by a fixed number of workers over a finer partition.** The workers are what the
/// operation was granted, so the loop creates no more tasks than there are threads to run them, and
/// the chunks stay finer than the tasks so the workers balance by taking the next one that is free.
/// The arena is what *bounds* the width; the worker count is what stops the loop planning for more
/// than it will get.
///
/// **A parallel loop's partition may follow the machine and a reduction's may not.** `chooseGrainSize`
/// takes a concurrency and `deterministicChunkCount` does not, and that difference is the whole
/// reason both exist: a reduction whose chunk boundaries moved with the thread count would give a
/// stacking run a different answer on a different machine.
///
/// **What a body reports is a `Status`, and the one that is reported back is the earliest.** Not the
/// first to arrive - the first *in the range*, so that a report names the same item run after run.
/// A loop stops as soon as a failure can no longer be beaten by anything still to run.
///
/// The header is deliberately cheap. It names `ExecutionContext` without including it, so a kernel
/// header that composes this one does not inherit the runtime's whole closure; a caller has an
/// execution context already, because it cannot call any of this without one.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <memory>
#include <type_traits>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, because every entry point below only borrows one for the
/// duration of a call. A caller reaching these functions holds an execution context and has
/// therefore already included the header that defines it.
class ExecutionContext;

/// The half-open range of items one call of a body must process.
///
/// Items rather than bytes or pixels: what an item is belongs to the loop, and the scheduler only
/// ever counts them. A range handed to a body is never empty.
struct TaskRange
{
    /// The first item in the range.
    std::uint64_t begin = 0u;

    /// One past the last item in the range.
    std::uint64_t end = 0u;

    /// How many items this range holds.
    ///
    /// Allocation-free and usable in a constant expression.
    ///
    /// @return the count, or zero for a range whose end is not past its beginning.
    [[nodiscard]] constexpr std::uint64_t size() const noexcept
    {
        return end > begin ? end - begin : 0u;
    }

    /// Whether this range holds nothing.
    ///
    /// Allocation-free and usable in a constant expression.
    ///
    /// @return true when the end is not past the beginning.
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return end <= begin;
    }
};

/// What one item of a loop costs, so that a grain size is derived rather than copied.
///
/// The two numbers are two measures of the same work and are both approximate: what they decide is
/// how many items have to be in one task before the task is worth handing to another thread, and
/// that decision is insensitive to a factor of two. A kernel family states its cost once, beside
/// the kernel, rather than each of its call sites carrying a magic grain size.
///
/// Either number may be zero, which says the caller does not know it. Both zero is a caller that
/// knows nothing about the work, and is treated as the cheapest possible item.
struct TaskCost
{
    /// How many bytes one item reads and writes together, counting each byte once.
    std::uint64_t bytesPerItem = 1u;

    /// Roughly how many arithmetic operations one item costs. An order of magnitude is enough.
    std::uint64_t operationsPerItem = 1u;
};

/// The bytes one task must touch before handing it to another thread pays for itself.
///
/// Sixty-four kibibytes is a few tens of microseconds of streaming memory traffic on the hosts this
/// engine targets, against a task hand-off measured in hundreds of nanoseconds, so a task this size
/// spends at most a fraction of a per cent of itself on being scheduled. It is deliberately a round
/// number: the cost curve is flat either side of it, and a value tuned to one host's measurement
/// would be a false precision the next host does not share.
inline constexpr std::uint64_t kMinimumTaskBytes = 64u * 1024u;

/// The arithmetic one task must do before handing it to another thread pays for itself.
///
/// The other half of the same question, for work that touches little memory and computes a lot - a
/// transcendental function over a small buffer reaches this long before it reaches the byte figure.
/// A task satisfying either measure is worth running, which is why the two are combined by taking
/// the smaller item count rather than the larger.
inline constexpr std::uint64_t kMinimumTaskOperations = 64u * 1024u;

/// How many chunks each worker should have to choose from, for load balance.
///
/// One chunk per worker would leave every worker waiting for the slowest, and a chunk per item
/// would spend the run in the scheduler. Four is the usual compromise and is what this engine uses
/// until a kernel measures a reason to differ.
inline constexpr std::uint32_t kChunksPerWorker = 4u;

/// The most chunks a deterministic reduction is ever split into.
///
/// It bounds the partials a reduction holds, and therefore the one allocation `parallelReduce`
/// makes, at eight kibibytes. It also bounds the depth of the merge tree, which is what the error
/// bound of a reduction depends on.
inline constexpr std::uint64_t kMaxDeterministicChunks = 1024u;

/// How many items one task of a parallel loop should take.
///
/// The answer is the larger of two demands: enough items that the task is worth scheduling at all,
/// and few enough that every worker has several to choose from. A loop is free to ignore this and
/// state its own partition, and a loop that does so is stating that it measured something.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param itemCount how many items the loop covers.
/// @param cost what one item costs.
/// @param concurrency how many workers will walk the partition. Zero is read as one.
/// @return the grain, which is at least one, and never more than `itemCount` except for an empty
///         loop, which reports one.
[[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t chooseGrainSize(std::uint64_t itemCount, const TaskCost& cost,
                                                                 std::uint32_t concurrency) noexcept;

/// How many chunks a reduction over these items is split into.
///
/// Deliberately without a concurrency parameter, and that absence is the contract: the partition a
/// reduction is merged over is a function of the work alone, so the same input reduces to the same
/// number on one thread and on sixty-four. A chunk here is larger than a parallel loop's grain,
/// because chunks are also merge-tree leaves and a tree with more leaves is a deeper one.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param itemCount how many items the reduction covers.
/// @param cost what one item costs.
/// @return the number of chunks, which is zero for an empty reduction, at least one otherwise, and
///         never more than `itemCount` or `kMaxDeterministicChunks`.
[[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t deterministicChunkCount(std::uint64_t itemCount,
                                                                         const TaskCost& cost) noexcept;

/// How many threads a loop of this shape will actually run on.
///
/// The number a scaling sweep records, as against the number it asked for. Forcing a thread count
/// is a matter of configuration - an engine context is built with the maximum to sweep at, and an
/// operation may narrow below it - but the answer is not always the request: a loop with fewer
/// chunks than threads uses fewer threads, and an operation that started while the context was busy
/// was admitted less than it asked for. A sweep that recorded the request would report a speedup
/// against a thread count that never ran.
///
/// Thread-safe with respect to `execution`, which is only read. Allocation-free.
///
/// @param execution the operation the loop would belong to.
/// @param itemCount how many items the loop would cover.
/// @param cost what one item costs, for the grain policy.
/// @return the worker count, or `0` for an empty loop or an execution context that holds nothing.
[[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t
plannedWorkerCount(const ExecutionContext& execution, std::uint64_t itemCount, const TaskCost& cost) noexcept;

/// What a parallel loop calls for one range of items.
///
/// A borrowed reference to whatever the caller passed, not a copy of it: constructing one costs two
/// pointers and no allocation, which is what lets a loop take a capturing lambda without the
/// scheduler's signature admitting a heap allocation on a path a kernel runs.
///
/// It borrows, so it must not outlive what it was built from. Passing a lambda directly as an
/// argument is safe and is the expected form - the temporary lives until the call it was written in
/// has returned. Storing one in a member is not.
class TaskBody
{
public:
    /// Borrows a callable that takes a `TaskRange` and returns a `Status`.
    ///
    /// @param callable what the loop should call. Borrowed, and must outlive this body.
    template <typename CallableT>
        requires(!std::is_same_v<std::remove_cvref_t<CallableT>, TaskBody>)
    TaskBody(CallableT&& callable) noexcept
        : m_callable(const_cast<void*>(static_cast<const void*>(std::addressof(callable)))),
          m_invoke(&invokeAs<std::remove_reference_t<CallableT>>)
    {
    }

    /// Runs the borrowed callable over one range.
    ///
    /// @param range the items to process. Never empty.
    /// @return whatever the callable reported.
    [[nodiscard]] Status operator()(TaskRange range) const
    {
        return m_invoke(m_callable, range);
    }

private:
    using Invoke = Status (*)(void*, TaskRange);

    /// Calls one particular callable type through the erased pointer.
    ///
    /// @param callable the address the constructor recorded.
    /// @param range the items to process.
    /// @return whatever the callable reported.
    template <typename CallableT> [[nodiscard]] static Status invokeAs(void* callable, TaskRange range)
    {
        return (*static_cast<CallableT*>(callable))(range);
    }

    /// The callable this body borrows. Never null.
    void* m_callable;

    /// How to call it. Never null.
    Invoke m_invoke;
};

/// One range assigned to one stable worker slot of a parallel region.
///
/// Slots are dense in `[0, plannedWorkerCount)` and remain owned by the same scheduler task for the
/// duration of one `parallelForWorkers` call. They identify worker-private scratch, not an operating
/// system thread: a later region may run the same slot on another thread. The range is never empty.
struct WorkerTask
{
    /// Items this call of the body must process.
    TaskRange range{};

    /// Dense worker-private index for this region.
    std::uint32_t workerSlot = 0u;
};

/// What a worker-aware parallel loop calls for one range of items.
///
/// This is the scratch-partitioning form of `TaskBody`. It borrows the callable and adds a stable
/// slot without exposing oneTBB or a thread identifier in the public interface.
class WorkerTaskBody
{
public:
    /// Borrows a callable that takes a `WorkerTask` and returns a `Status`.
    ///
    /// @param callable what the loop should call. Borrowed, and must outlive this body.
    template <typename CallableT>
        requires(!std::is_same_v<std::remove_cvref_t<CallableT>, WorkerTaskBody>)
    WorkerTaskBody(CallableT&& callable) noexcept
        : m_callable(const_cast<void*>(static_cast<const void*>(std::addressof(callable)))),
          m_invoke(&invokeAs<std::remove_reference_t<CallableT>>)
    {
    }

    /// Runs the borrowed callable for one worker-owned range.
    ///
    /// @param task the non-empty range and its dense worker slot.
    /// @return whatever the callable reported.
    [[nodiscard]] Status operator()(WorkerTask task) const
    {
        return m_invoke(m_callable, task);
    }

private:
    using Invoke = Status (*)(void*, WorkerTask);

    /// Calls one particular callable type through the erased pointer.
    ///
    /// @param callable the address the constructor recorded.
    /// @param task the range and worker slot to pass.
    /// @return whatever the callable reported.
    template <typename CallableT> [[nodiscard]] static Status invokeAs(void* callable, WorkerTask task)
    {
        return (*static_cast<CallableT*>(callable))(task);
    }

    /// The callable this body borrows. Never null.
    void* m_callable;

    /// How to call it. Never null.
    Invoke m_invoke;
};

/// What a reduction calls for one chunk of items.
///
/// The same borrowed reference as `TaskBody`, for a callable that accumulates into a partial value
/// rather than returning one. Accumulating into a reference is what lets a chunk's body be the same
/// loop a serial implementation would write.
class ReductionBody
{
public:
    /// Borrows a callable that takes a `TaskRange` and a partial value, and returns a `Status`.
    ///
    /// @param callable what the reduction should call. Borrowed, and must outlive this body.
    template <typename CallableT>
        requires(!std::is_same_v<std::remove_cvref_t<CallableT>, ReductionBody>)
    ReductionBody(CallableT&& callable) noexcept
        : m_callable(const_cast<void*>(static_cast<const void*>(std::addressof(callable)))),
          m_invoke(&invokeAs<std::remove_reference_t<CallableT>>)
    {
    }

    /// Runs the borrowed callable over one chunk.
    ///
    /// @param range the items to process. Never empty.
    /// @param partial the accumulator for this chunk, which arrives holding the identity.
    /// @return whatever the callable reported.
    [[nodiscard]] Status operator()(TaskRange range, double& partial) const
    {
        return m_invoke(m_callable, range, partial);
    }

private:
    using Invoke = Status (*)(void*, TaskRange, double&);

    /// Calls one particular callable type through the erased pointer.
    ///
    /// @param callable the address the constructor recorded.
    /// @param range the items to process.
    /// @param partial the accumulator for this chunk.
    /// @return whatever the callable reported.
    template <typename CallableT> [[nodiscard]] static Status invokeAs(void* callable, TaskRange range, double& partial)
    {
        return (*static_cast<CallableT*>(callable))(range, partial);
    }

    /// The callable this body borrows. Never null.
    void* m_callable;

    /// How to call it. Never null.
    Invoke m_invoke;
};

/// How two partial values of a reduction are combined.
///
/// A plain function pointer rather than an erased callable, because a merge has no state to
/// capture: summing, taking a minimum and taking a maximum are all functions of their two
/// arguments. It is called on one thread, after every chunk has finished, in an order fixed by the
/// chunk indices - so a merge that is not associative still gives a reproducible answer, and one
/// that is gives the answer a serial run would produce to within its own error bound.
///
/// It must be `noexcept`: it runs inside the reduction's merge loop, where there is nothing
/// sensible to do with an exception.
using ReductionMerge = double (*)(double, double) noexcept;

/// Runs a loop over `itemCount` items on the threads this operation was admitted.
///
/// The items are split into chunks by `chooseGrainSize`, and the chunks are walked by as many
/// workers as `ExecutionContext::admittedConcurrency` reports - so a narrowed operation is narrow
/// even in a context that is otherwise idle, and two operations in one context share that context's
/// threads rather than each taking them all. A body may itself call this: a nested loop runs in the
/// same arena and cannot oversubscribe it.
///
/// Cancellation is checked before the loop starts and once per chunk, so a loop stops within one
/// chunk of the request rather than at the end of the frame.
///
/// A body reports a failure by returning it rather than by throwing. An exception that escapes a
/// body is caught at the scheduler's boundary with oneTBB and, unless a range failed first, is
/// returned as `ErrorCode::AllocationFailed` for `std::bad_alloc` and `ErrorCode::ExternalFailure`
/// otherwise.
///
/// Thread-safe with respect to `execution`, which is only read. Allocation-free unless a body
/// allocates or a failure has to be described.
///
/// @param execution the operation this loop belongs to. Must hold something.
/// @param itemCount how many items to cover. Zero is a loop that succeeds without calling the body.
/// @param cost what one item costs, for the grain policy.
/// @param body what to run for each range of items. Called on several threads at once, so it must
///        be safe to call concurrently on disjoint ranges.
/// @return success, or the first failure in range order: `ErrorCode::NotInitialized` when
///         `execution` holds nothing, `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when
///         the operation was stopped, `ErrorCode::AllocationFailed` or `ErrorCode::ExternalFailure`
///         when the scheduler itself could not start or a body threw, and otherwise whatever a
///         body reported.
[[nodiscard]] AFTERNIGHTENGINE_API Status parallelFor(const ExecutionContext& execution, std::uint64_t itemCount,
                                                      const TaskCost& cost, TaskBody body);

/// Runs a loop whose body can address worker-private state by a stable dense slot.
///
/// Partitioning, admission, nested-arena behavior, cancellation checkpoints, allocation behavior,
/// and failure ordering are identical to `parallelFor`. The additional slot is stable only for
/// this call and ranges from zero to one below `plannedWorkerCount(execution, itemCount, cost)`.
/// Concurrent body calls never carry the same slot.
///
/// Thread-safe with respect to `execution`, which is only read. Allocation-free unless a body
/// allocates or a failure has to be described.
///
/// @param execution the operation this loop belongs to. Must hold something.
/// @param itemCount how many items to cover. Zero succeeds without calling the body.
/// @param cost what one item costs, for the grain policy.
/// @param body what to run for each worker-owned range. Calls may overlap for distinct slots.
/// @return success, or the same execution, cancellation, allocation, or body failures as
///         `parallelFor`, with body failures chosen in range order.
[[nodiscard]] AFTERNIGHTENGINE_API Status parallelForWorkers(const ExecutionContext& execution, std::uint64_t itemCount,
                                                             const TaskCost& cost, WorkerTaskBody body);

/// Reduces `itemCount` items to one value, giving the same answer whatever the thread count.
///
/// The items are split by `deterministicChunkCount`, which does not know how many threads there
/// are; each chunk is accumulated into its own partial starting from `identity`; and the partials
/// are merged in a fixed pairwise tree over the chunk indices. Nothing in that depends on which
/// worker took which chunk or on how many workers there were, which is why the answer is bitwise
/// identical across thread counts for a merge that is a pure function of its arguments.
///
/// The merge tree is also why this is not simply a running total: pairwise summation over
/// `kMaxDeterministicChunks` leaves has an error bound that grows with the logarithm of the chunk
/// count rather than with the count itself.
///
/// Thread-safe with respect to `execution`, which is only read. Makes one allocation, for the
/// partials.
///
/// The partial a body accumulates through is private to the worker running that chunk and is
/// copied into the partial array once, when the chunk is done. A body may therefore store to it on
/// every item without two workers ever contending for one cache line, which a compiler without
/// type-based alias analysis makes every body do.
///
/// @param execution the operation this reduction belongs to. Must hold something.
/// @param itemCount how many items to cover. Zero produces `identity` without calling the body.
/// @param cost what one item costs, for the chunk policy.
/// @param identity the value each chunk's partial starts from, and the answer for an empty
///        reduction. Must be an identity of `merge`, or the result depends on the chunk count.
/// @param body what to run for each chunk. Called on several threads at once, each with its own
///        private partial, so it must be safe to call concurrently on disjoint ranges.
/// @param merge how two partials are combined. Must not be null.
/// @return the reduced value, or a failure: `ErrorCode::NotInitialized` when `execution` holds
///         nothing, `ErrorCode::InvalidArgument` when `merge` is null, `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped,
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when the partials
///         cannot be held, the scheduler failures `parallelFor` describes, and otherwise the first
///         failure in chunk order that a body reported.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> parallelReduce(const ExecutionContext& execution,
                                                                 std::uint64_t itemCount, const TaskCost& cost,
                                                                 double identity, ReductionBody body,
                                                                 ReductionMerge merge);

} // namespace an
