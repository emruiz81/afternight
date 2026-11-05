// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "runtime/OperationArena.h"
#include "runtime/TaskPartition.h"

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/partitioner.h>
#include <oneapi/tbb/task_arena.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

// How a loop actually runs, and why it runs that way.
//
// **Workers walk a partition; the partition does not become threads.** The obvious implementation
// hands oneTBB a range and a grain size and lets it decide how many tasks to make, but then the
// only thing bounding how many run at once is the arena, and every operation in a context would
// have the whole context. So the shape here is the other one: exactly as many tasks as the
// operation was admitted threads, each looping on "take the next chunk that is free". The bound is
// then structural - a task is a thread's worth of work and there are only so many of them - and the
// chunks are still finer than the tasks, so the workers balance against each other.
//
// **The arena is the operation's, and it is entered rather than created here.** It was built when
// the operation started, with the width its engine context's budget granted, and everything the
// operation runs goes through it - which is what makes a nested `tbb::parallel_for` inside an
// adopted algorithm inherit the same width without that algorithm being told anything.
// `task_arena::execute` from a thread already inside that arena runs the functor where it is, so a
// nested region needs no detection: there is nothing to detect, because there is nothing extra to
// take.
//
// **A failure is reported by range order rather than by arrival order.** Which task fails first is
// a fact about timing; which item is wrong is a fact about the data, and a report should name the
// second. The ledger below keeps the lowest failing range, and a worker that claims a chunk past a
// recorded failure stops - which is safe precisely because chunks are claimed in increasing order,
// so every chunk that could still beat that failure has already been claimed by somebody.

namespace an
{
namespace
{

/// The boundary the scheduler's own calls into oneTBB are translated at.
///
/// The library can throw - out of memory while starting workers, or a scheduler that has been
/// disabled - and none of that may travel out of a kernel untranslated.
constexpr ExternalBoundary kSchedulerBoundary{
    .library = "onetbb", .operation = "an::parallelFor", .domain = ErrorDomain::Runtime};

/// Returns one temporary allocation to the engine context on every reduction exit path.
class ScopedResourceAllocation
{
public:
    /// Records an allocation already made by a resource.
    ///
    /// @param resource the resource that made it. Never null.
    /// @param pointer the allocation. Never null.
    /// @param bytes its requested byte count.
    /// @param alignment its requested alignment.
    ScopedResourceAllocation(MemoryResource& resource, void* pointer, std::uint64_t bytes,
                             std::uint64_t alignment) noexcept
        : m_resource(&resource), m_pointer(pointer), m_bytes(bytes), m_alignment(alignment)
    {
    }

    /// Returns the allocation.
    ~ScopedResourceAllocation()
    {
        m_resource->deallocate(m_pointer, m_bytes, m_alignment);
    }

    ScopedResourceAllocation(const ScopedResourceAllocation&) = delete;
    ScopedResourceAllocation& operator=(const ScopedResourceAllocation&) = delete;

private:
    /// The resource that owns the allocation.
    MemoryResource* m_resource;

    /// The allocation to return.
    void* m_pointer;

    /// Its requested byte count.
    std::uint64_t m_bytes;

    /// Its requested alignment.
    std::uint64_t m_alignment;
};

/// The value the failure ledger holds when nothing has failed.
///
/// Every real range begins below it, so a comparison against it needs no separate flag.
constexpr std::uint64_t kNoFailure = (std::numeric_limits<std::uint64_t>::max)();

/// Divides and rounds up, without the overflow the usual `(a + b - 1) / b` has.
///
/// @param value the numerator.
/// @param divisor the denominator, which must not be zero.
/// @return the quotient rounded up.
[[nodiscard]] std::uint64_t ceilDivide(std::uint64_t value, std::uint64_t divisor) noexcept
{
    return value / divisor + (value % divisor != 0u ? 1u : 0u);
}

/// How many items a task must hold before handing it to another thread pays for itself.
///
/// The two measures of the same work are combined by taking the smaller count, because a task that
/// satisfies either of them is worth running: a streaming pass reaches the byte figure long before
/// the arithmetic figure, and a transcendental one does the opposite.
///
/// @param cost what one item costs. A zero in either field means the caller does not know it, and
///        the other field decides alone; both zero is the cheapest possible item.
/// @return the item count, which is at least one.
[[nodiscard]] std::uint64_t itemsForMinimumWork(const TaskCost& cost) noexcept
{
    const std::uint64_t byBytes =
        cost.bytesPerItem == 0u ? kNoFailure : ceilDivide(kMinimumTaskBytes, cost.bytesPerItem);
    const std::uint64_t byOperations =
        cost.operationsPerItem == 0u ? kNoFailure : ceilDivide(kMinimumTaskOperations, cost.operationsPerItem);

    std::uint64_t items = byBytes < byOperations ? byBytes : byOperations;
    if (items == kNoFailure)
    {
        // Neither field was stated. The item is then as cheap as an item can be, which is the
        // assumption that produces the largest task rather than the smallest.
        items = kMinimumTaskBytes > kMinimumTaskOperations ? kMinimumTaskBytes : kMinimumTaskOperations;
    }
    return items != 0u ? items : 1u;
}

/// The items chunk `index` of an even split of `itemCount` into `chunkCount` chunks covers.
///
/// An even split rather than a fixed stride, so the remainder is spread one item per chunk instead
/// of landing entirely on the last one - which is what stops the final chunk of a badly divided
/// loop being the one everything waits for.
///
/// @param index which chunk, below `chunkCount`.
/// @param itemCount how many items the loop covers, at least `chunkCount`.
/// @param chunkCount how many chunks, at least one.
/// @return the chunk's range, which is never empty.
[[nodiscard]] TaskRange chunkRange(std::uint64_t index, std::uint64_t itemCount, std::uint64_t chunkCount) noexcept
{
    const std::uint64_t base = itemCount / chunkCount;
    const std::uint64_t remainder = itemCount % chunkCount;
    const std::uint64_t extra = index < remainder ? index : remainder;
    const std::uint64_t begin = index * base + extra;
    return TaskRange{.begin = begin, .end = begin + base + (index < remainder ? 1u : 0u)};
}

/// The failure a parallel region reports, chosen by range rather than by arrival.
///
/// The atomic is the cheap half: every worker reads it once per chunk to decide whether anything it
/// could still do would matter. The mutex is the cold half, taken only by a worker that actually
/// failed, because a `Status` carrying detail is not something an atomic can hold.
class FailureLedger
{
public:
    /// Whether a chunk starting here is already beaten by a recorded failure.
    ///
    /// Safe to answer with a stale value: a worker that runs a chunk it could have skipped does
    /// unnecessary work and changes no answer, because the ledger keeps the minimum either way.
    ///
    /// @param begin the first item of the chunk about to run.
    /// @return true when a failure at a strictly earlier item has already been recorded.
    [[nodiscard]] bool beaten(std::uint64_t begin) const noexcept
    {
        return begin > m_lowestFailure.load(std::memory_order_acquire);
    }

    /// Records one failure, keeping whichever covers the earliest item.
    ///
    /// @param begin the first item of the chunk that failed.
    /// @param status what it reported. Must be a failure.
    void record(std::uint64_t begin, Status status)
    {
        {
            const std::lock_guard<std::mutex> held(m_guard);
            if (m_begin == kNoFailure || begin < m_begin)
            {
                m_begin = begin;
                m_status = std::move(status);
            }
        }

        std::uint64_t observed = m_lowestFailure.load(std::memory_order_acquire);
        while (begin < observed && !m_lowestFailure.compare_exchange_weak(observed, begin, std::memory_order_acq_rel,
                                                                          std::memory_order_acquire))
        {
        }
    }

    /// What the region reports.
    ///
    /// Called after every worker has finished, so no lock is needed and none is taken.
    ///
    /// @return the earliest recorded failure, or success when nothing failed.
    [[nodiscard]] Status outcome() && noexcept
    {
        return std::move(m_status);
    }

private:
    /// Guards the recorded failure, which is too large to be an atomic.
    std::mutex m_guard;

    /// The first item of the earliest failure recorded so far, or `kNoFailure`. Duplicated outside
    /// the mutex so that the common path - no failure at all - reads one atomic per chunk.
    std::atomic<std::uint64_t> m_lowestFailure{kNoFailure};

    /// The first item of the recorded failure, under the mutex.
    std::uint64_t m_begin = kNoFailure;

    /// The recorded failure itself, under the mutex.
    Status m_status;
};

/// What one chunk of a parallel region does.
///
/// A second erased callable rather than a template, so that the loop below is compiled once for
/// both entry points: the difference between a loop and a reduction is entirely in what this does
/// with the chunk index.
class ChunkRunner
{
public:
    /// Borrows a callable that takes a chunk index and its range.
    ///
    /// @param callable what to run for one chunk. Borrowed, and must outlive this runner.
    template <typename CallableT>
    explicit ChunkRunner(CallableT& callable) noexcept : m_callable(&callable), m_invoke(&invokeAs<CallableT>)
    {
    }

    /// Runs one chunk.
    ///
    /// @param index which chunk.
    /// @param workerSlot which stable worker task owns the chunk.
    /// @param range the items it covers.
    /// @return whatever the callable reported.
    [[nodiscard]] Status operator()(std::uint64_t index, std::uint32_t workerSlot, TaskRange range) const
    {
        return m_invoke(m_callable, index, workerSlot, range);
    }

private:
    /// Calls one particular callable type through the erased pointer.
    ///
    /// @param callable the address the constructor recorded.
    /// @param index which chunk.
    /// @param workerSlot which stable worker task owns the chunk.
    /// @param range the items it covers.
    /// @return whatever the callable reported.
    template <typename CallableT>
    [[nodiscard]] static Status invokeAs(void* callable, std::uint64_t index, std::uint32_t workerSlot, TaskRange range)
    {
        return (*static_cast<CallableT*>(callable))(index, workerSlot, range);
    }

    /// The callable this runner borrows. Never null.
    void* m_callable;

    /// How to call it. Never null.
    Status (*m_invoke)(void*, std::uint64_t, std::uint32_t, TaskRange);
};

/// The refusal an entry point reports for an execution context that holds nothing.
///
/// @param operation the entry point refusing, for the failure's own record.
/// @return an `ErrorCode::NotInitialized` failure.
[[nodiscard]] Status refuseEmptyExecution(const char* operation)
{
    return Status::failure(ErrorCode::NotInitialized,
                           StatusDetail{.message = "a parallel region needs an operation that holds an engine context",
                                        .operation = operation,
                                        .domain = ErrorDomain::Runtime});
}

/// Runs `chunkCount` chunks on `workers` threads of this operation's engine context.
///
/// The one place a parallel region exists in this engine. Both entry points reduce to it, so the
/// admission, the arena, the cancellation checkpoints and the failure ordering are decided once.
///
/// @param execution the operation, which must hold something.
/// @param threads the arena it was granted.
/// @param itemCount how many items the region covers.
/// @param chunkCount how many chunks to split them into, at least one and at most `itemCount`.
/// @param workers how many chunks may be in flight at once, at least one.
/// @param runChunk what to do with one chunk.
/// @return success, the earliest failure in range order, or the translation of anything oneTBB
///         threw.
[[nodiscard]] Status runRegion(const ExecutionContext& execution, OperationArena& threads, std::uint64_t itemCount,
                               std::uint64_t chunkCount, std::uint32_t workers, const ChunkRunner& runChunk)
{
    std::atomic<std::uint64_t> nextChunk{0u};
    FailureLedger ledger;

    const auto walk = [&](std::uint32_t workerSlot) {
        for (;;)
        {
            const std::uint64_t index = nextChunk.fetch_add(1u, std::memory_order_relaxed);
            if (index >= chunkCount)
            {
                return;
            }

            const TaskRange range = chunkRange(index, itemCount, chunkCount);
            if (ledger.beaten(range.begin))
            {
                // Chunks are claimed in increasing order, so everything that could still report an
                // earlier failure has already been claimed. Stopping here cannot change the answer.
                return;
            }

            Status stopped = execution.checkCancellation();
            if (stopped.failed())
            {
                ledger.record(range.begin, std::move(stopped));
                return;
            }

            Status result = runChunk(index, workerSlot, range);
            if (result.failed())
            {
                ledger.record(range.begin, std::move(result));
                return;
            }
        }
    };

    const Status thrown = callAcrossBoundary(kSchedulerBoundary, [&] {
        threads.arena().execute([&] {
            if (workers <= 1u)
            {
                // One worker is this thread, and a task for it would be a hand-off to itself.
                walk(0u);
                return;
            }

            // A simple partitioner with a grain of one, so the workers really are separate tasks:
            // the automatic partitioner is free to fuse them, which would silently run an operation
            // admitted four threads on fewer.
            tbb::parallel_for(
                tbb::blocked_range<std::uint32_t>(0u, workers, 1u),
                [&](const tbb::blocked_range<std::uint32_t>& slots) {
                    for (std::uint32_t slot = slots.begin(); slot != slots.end(); ++slot)
                    {
                        walk(slot);
                    }
                },
                tbb::simple_partitioner{});
        });
    });

    Status outcome = std::move(ledger).outcome();
    return outcome.failed() ? std::move(outcome) : thrown;
}

} // namespace

std::uint64_t chooseGrainSize(std::uint64_t itemCount, const TaskCost& cost, std::uint32_t concurrency) noexcept
{
    return chooseGrainSizeInChunks(itemCount, cost, concurrency, kChunksPerWorker);
}

std::uint64_t chooseGrainSizeInChunks(std::uint64_t itemCount, const TaskCost& cost, std::uint32_t concurrency,
                                      std::uint32_t chunksPerWorker) noexcept
{
    if (itemCount == 0u)
    {
        return 1u;
    }

    const std::uint64_t workers = concurrency != 0u ? concurrency : 1u;
    const std::uint64_t targetTasks = workers * (chunksPerWorker != 0u ? chunksPerWorker : 1u);
    const std::uint64_t byBalance = ceilDivide(itemCount, targetTasks);
    const std::uint64_t byOverhead = itemsForMinimumWork(cost);

    const std::uint64_t grain = byBalance > byOverhead ? byBalance : byOverhead;
    return grain < itemCount ? grain : itemCount;
}

std::uint64_t deterministicChunkCount(std::uint64_t itemCount, const TaskCost& cost) noexcept
{
    if (itemCount == 0u)
    {
        return 0u;
    }

    // No concurrency term anywhere in this function, which is the whole contract: the partition a
    // reduction is merged over is a function of the work and of nothing else.
    const std::uint64_t chunks = ceilDivide(itemCount, itemsForMinimumWork(cost));
    return chunks < kMaxDeterministicChunks ? chunks : kMaxDeterministicChunks;
}

std::uint32_t plannedWorkerCount(const ExecutionContext& execution, std::uint64_t itemCount,
                                 const TaskCost& cost) noexcept
{
    if (itemCount == 0u || OperationArena::of(execution) == nullptr)
    {
        return 0u;
    }

    const std::uint32_t admitted = execution.admittedConcurrency() != 0u ? execution.admittedConcurrency() : 1u;
    const std::uint64_t chunkCount = ceilDivide(itemCount, chooseGrainSize(itemCount, cost, admitted));
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(admitted) < chunkCount ? admitted : chunkCount);
}

Status parallelFor(const ExecutionContext& execution, std::uint64_t itemCount, const TaskCost& cost, TaskBody body)
{
    return parallelForInChunks(execution, itemCount, cost, kChunksPerWorker, body);
}

Status parallelForInChunks(const ExecutionContext& execution, std::uint64_t itemCount, const TaskCost& cost,
                           std::uint32_t chunksPerWorker, TaskBody body)
{
    OperationArena* const threads = OperationArena::of(execution);
    if (threads == nullptr)
    {
        return refuseEmptyExecution("an::parallelFor");
    }

    if (itemCount == 0u)
    {
        return Status::success();
    }

    const Status stopped = execution.checkCancellation();
    if (stopped.failed())
    {
        // Answered before anything is claimed or partitioned, so an operation stopped before it
        // started runs no body at all rather than one per worker.
        return stopped;
    }

    const std::uint32_t workers = execution.admittedConcurrency() != 0u ? execution.admittedConcurrency() : 1u;
    const std::uint64_t grain = chooseGrainSizeInChunks(itemCount, cost, workers, chunksPerWorker);
    const std::uint64_t chunkCount = ceilDivide(itemCount, grain);

    auto runChunk = [&](std::uint64_t, std::uint32_t, TaskRange range) { return body(range); };
    const ChunkRunner runner{runChunk};

    const std::uint64_t cappedWorkers =
        static_cast<std::uint64_t>(workers) < chunkCount ? static_cast<std::uint64_t>(workers) : chunkCount;
    return runRegion(execution, *threads, itemCount, chunkCount, static_cast<std::uint32_t>(cappedWorkers), runner);
}

Status parallelForWorkers(const ExecutionContext& execution, std::uint64_t itemCount, const TaskCost& cost,
                          WorkerTaskBody body)
{
    OperationArena* const threads = OperationArena::of(execution);
    if (threads == nullptr)
    {
        return refuseEmptyExecution("an::parallelForWorkers");
    }

    if (itemCount == 0u)
    {
        return Status::success();
    }

    const Status stopped = execution.checkCancellation();
    if (stopped.failed())
    {
        return stopped;
    }

    const std::uint32_t workers = execution.admittedConcurrency() != 0u ? execution.admittedConcurrency() : 1u;
    const std::uint64_t grain = chooseGrainSize(itemCount, cost, workers);
    const std::uint64_t chunkCount = ceilDivide(itemCount, grain);
    const std::uint64_t cappedWorkers =
        static_cast<std::uint64_t>(workers) < chunkCount ? static_cast<std::uint64_t>(workers) : chunkCount;

    auto runChunk = [&](std::uint64_t, std::uint32_t workerSlot, TaskRange range) {
        return body(WorkerTask{.range = range, .workerSlot = workerSlot});
    };
    const ChunkRunner runner{runChunk};
    return runRegion(execution, *threads, itemCount, chunkCount, static_cast<std::uint32_t>(cappedWorkers), runner);
}

Result<double> parallelReduce(const ExecutionContext& execution, std::uint64_t itemCount, const TaskCost& cost,
                              double identity, ReductionBody body, ReductionMerge merge)
{
    OperationArena* const threads = OperationArena::of(execution);
    if (threads == nullptr)
    {
        return refuseEmptyExecution("an::parallelReduce");
    }

    if (merge == nullptr)
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = "a reduction cannot combine its partial values without a merge",
                                            .operation = "an::parallelReduce",
                                            .domain = ErrorDomain::Runtime,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "merge")}});
    }

    if (itemCount == 0u)
    {
        return identity;
    }

    const Status stopped = execution.checkCancellation();
    if (stopped.failed())
    {
        return stopped;
    }

    const std::uint64_t chunkCount = deterministicChunkCount(itemCount, cost);

    // The one allocation a reduction makes, and the reason the chunk count is bounded. It comes
    // from the operation's context so it participates in the configured memory budget and its
    // counters, and it is made before any work starts.
    MemoryResource* const resource = execution.memoryResource();
    if (resource == nullptr)
    {
        return refuseEmptyExecution("an::parallelReduce");
    }
    constexpr std::uint64_t kPartialAlignment = alignof(double);
    const std::uint64_t partialBytes = chunkCount * sizeof(double);
    Result<void*> allocated = resource->allocate(partialBytes, kPartialAlignment);
    if (!allocated.hasValue())
    {
        return allocated.status();
    }
    const ScopedResourceAllocation release{*resource, *allocated, partialBytes, kPartialAlignment};
    double* const partials = static_cast<double*>(*allocated);
    std::fill_n(partials, static_cast<std::size_t>(chunkCount), identity);

    auto runChunk = [&](std::uint64_t index, std::uint32_t, TaskRange range) {
        // The body accumulates through a local of the worker's own rather than through its slot
        // of the partial array. The neighbouring slots belong to chunks other workers are running
        // at the same time, and a body that stores on every item - which is every body under a
        // compiler without type-based alias analysis - would otherwise fight them for one cache
        // line. Each chunk is claimed once, so the slot is written exactly once, when it is done.
        double& slot = partials[static_cast<std::size_t>(index)];
        double partial = slot;
        const Status outcome = body(range, partial);
        slot = partial;
        return outcome;
    };
    const ChunkRunner runner{runChunk};

    const std::uint32_t admitted = execution.admittedConcurrency() != 0u ? execution.admittedConcurrency() : 1u;
    const std::uint64_t cappedWorkers =
        static_cast<std::uint64_t>(admitted) < chunkCount ? static_cast<std::uint64_t>(admitted) : chunkCount;
    const Status ran =
        runRegion(execution, *threads, itemCount, chunkCount, static_cast<std::uint32_t>(cappedWorkers), runner);
    if (ran.failed())
    {
        return ran;
    }

    // A fixed pairwise tree over the chunk indices. Nothing in it reads how many workers there
    // were, which is what makes the answer identical across thread counts, and its depth is the
    // logarithm of the chunk count rather than the count itself, which is what makes its error
    // bound better than a running total's.
    std::uint64_t remaining = chunkCount;
    while (remaining > 1u)
    {
        const std::uint64_t pairs = remaining / 2u;
        for (std::uint64_t pair = 0u; pair < pairs; ++pair)
        {
            partials[static_cast<std::size_t>(pair)] = merge(partials[static_cast<std::size_t>(2u * pair)],
                                                             partials[static_cast<std::size_t>(2u * pair + 1u)]);
        }
        if (remaining % 2u != 0u)
        {
            partials[static_cast<std::size_t>(pairs)] = partials[static_cast<std::size_t>(remaining - 1u)];
        }
        remaining = pairs + remaining % 2u;
    }

    return partials[0];
}

} // namespace an
