// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Runtime/TaskScheduler.h.
//
// The scheduler is where a policy object stops being a policy object: an execution context says how
// much of the machine an operation may use, and this is the thing that has to make that true. So
// most of these cases measure rather than assert a return value - they run a parallel region and
// record how many bodies were ever in flight at once, which is the only honest way to ask whether a
// narrowing was respected.
//
// A peak-concurrency measurement can only ever under-report: two bodies that genuinely overlapped
// may still be counted one at a time if the scheduler happened to serialise them. Every case is
// therefore written so that under-reporting cannot make it pass - the assertions are upper bounds,
// and the one case that needs a lower bound proves it by making each body wait for the others.
//
// The grain and chunk policies are pure functions and are tested as such next to the loops that use
// them, because the difference between them is the whole reason there are two: a parallel loop's
// partition may follow the machine, and a reduction's may not.

#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// An engine context every case in this file starts from.
///
/// @param maxConcurrency the ceiling to configure. Four is enough to tell a narrowing from a
///        widening on any machine this suite runs on.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint32_t maxConcurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = maxConcurrency;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// One operation in an engine context.
///
/// @param engine the context to start it in.
/// @param maxConcurrency the operation's own ceiling, or `kUseContextConcurrency` for all of it.
/// @param stopToken how the operation may be stopped.
/// @return the execution context, which is valid or the case has already failed.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 std::uint32_t maxConcurrency = an::kUseContextConcurrency,
                                                 std::stop_token stopToken = std::stop_token{})
{
    an::ExecutionOptions options;
    options.maxConcurrency = maxConcurrency;
    options.stopToken = std::move(stopToken);

    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::ExecutionContext{};
}

/// How many bodies were ever running at the same time.
///
/// Shared between the cases that measure narrowing. `enter` and `leave` bracket one body; `peak` is
/// the largest count `enter` ever observed. It can only under-report, never over-report, which is
/// what makes an upper-bound assertion on it meaningful.
class ConcurrencyProbe
{
public:
    /// Counts one body as running, and records a new peak if this is one.
    void enter() noexcept
    {
        const std::uint32_t running = m_running.fetch_add(1u, std::memory_order_acq_rel) + 1u;
        std::uint32_t observed = m_peak.load(std::memory_order_acquire);
        while (running > observed && !m_peak.compare_exchange_weak(observed, running, std::memory_order_acq_rel))
        {
        }
    }

    /// Counts one body as finished.
    void leave() noexcept
    {
        m_running.fetch_sub(1u, std::memory_order_acq_rel);
    }

    /// The largest number of bodies ever seen running at once.
    ///
    /// @return the peak, which is at least one once anything has run.
    [[nodiscard]] std::uint32_t peak() const noexcept
    {
        return m_peak.load(std::memory_order_acquire);
    }

private:
    /// How many bodies are running right now.
    std::atomic<std::uint32_t> m_running{0u};

    /// The largest value `m_running` ever reached.
    std::atomic<std::uint32_t> m_peak{0u};
};

/// The cost of an item cheap enough that the grain policy never fuses a test's items into one task.
///
/// A megabyte per item reaches the minimum task size in one item, so a partition follows the item
/// count and the concurrency rather than the work, which is what a scheduling test wants to vary.
constexpr an::TaskCost kOneItemPerTask{.bytesPerItem = 1024u * 1024u, .operationsPerItem = 1024u * 1024u};

} // namespace

// ---------------------------------------------------------------------------------------------
// The grain policy: what a parallel loop's partition follows.
// ---------------------------------------------------------------------------------------------

TEST(TaskSchedulerGrainTest, GrainIsNeverZeroAndNeverExceedsTheItemCount)
{
    EXPECT_EQ(an::chooseGrainSize(0u, an::TaskCost{}, 4u), 1u);
    EXPECT_EQ(an::chooseGrainSize(1u, an::TaskCost{}, 4u), 1u);
    EXPECT_LE(an::chooseGrainSize(10u, an::TaskCost{}, 4u), 10u);
    EXPECT_GE(an::chooseGrainSize(1000000u, kOneItemPerTask, 4u), 1u);
}

TEST(TaskSchedulerGrainTest, ACheaperItemGetsALargerGrainWhileTheOverheadFloorIsWhatDecides)
{
    constexpr an::TaskCost kCheap{.bytesPerItem = 1u, .operationsPerItem = 1u};
    constexpr an::TaskCost kExpensive{.bytesPerItem = 4096u, .operationsPerItem = 4096u};

    // A hundred thousand items over four threads: the balance term asks for a few thousand per
    // task, which is above what the expensive item needs to be worth scheduling and well below
    // what the cheap one needs.
    EXPECT_GT(an::chooseGrainSize(100000u, kCheap, 4u), an::chooseGrainSize(100000u, kExpensive, 4u));

    // And the regime either side of it, which is the reason this case names the one it is in: with
    // enough items the balance term is above both floors and decides alone, so the cost stops
    // mattering. A grain policy that still separated them there would be one that had stopped
    // giving every thread work to do.
    EXPECT_EQ(an::chooseGrainSize(100000000u, kCheap, 4u), an::chooseGrainSize(100000000u, kExpensive, 4u));
}

TEST(TaskSchedulerGrainTest, MoreThreadsGetASmallerGrainUntilTheOverheadFloorBinds)
{
    constexpr an::TaskCost kCheap{.bytesPerItem = 1u, .operationsPerItem = 1u};

    // Enough items that the balance term dominates at both thread counts.
    const std::uint64_t items = 1000000000u;
    EXPECT_LT(an::chooseGrainSize(items, kCheap, 8u), an::chooseGrainSize(items, kCheap, 2u));

    // Few enough items that the floor dominates, so the two answers agree.
    EXPECT_EQ(an::chooseGrainSize(64u, kCheap, 8u), an::chooseGrainSize(64u, kCheap, 2u));
}

TEST(TaskSchedulerGrainTest, AZeroCostIsTreatedAsTheCheapestItemRatherThanAsADivisionByZero)
{
    constexpr an::TaskCost kNothing{.bytesPerItem = 0u, .operationsPerItem = 0u};

    const std::uint64_t grain = an::chooseGrainSize(1000000u, kNothing, 4u);
    EXPECT_GE(grain, 1u);
    EXPECT_LE(grain, 1000000u);
}

TEST(TaskSchedulerGrainTest, TheDeterministicChunkCountDoesNotFollowTheMachine)
{
    constexpr an::TaskCost kCheap{.bytesPerItem = 1u, .operationsPerItem = 1u};

    // The signature is the assertion: there is no concurrency to pass. This case states the
    // consequence - the same item count and cost give one answer, whatever is running.
    EXPECT_EQ(an::deterministicChunkCount(1000000u, kCheap), an::deterministicChunkCount(1000000u, kCheap));
    EXPECT_EQ(an::deterministicChunkCount(0u, kCheap), 0u);
    EXPECT_EQ(an::deterministicChunkCount(1u, kCheap), 1u);
}

TEST(TaskSchedulerGrainTest, TheDeterministicChunkCountIsBounded)
{
    constexpr an::TaskCost kCheap{.bytesPerItem = 1u, .operationsPerItem = 1u};

    EXPECT_LE(an::deterministicChunkCount(1000000000000u, kCheap), an::kMaxDeterministicChunks);
    EXPECT_LE(an::deterministicChunkCount(7u, kOneItemPerTask), 7u);
}

// ---------------------------------------------------------------------------------------------
// parallelFor: coverage, failure, cancellation.
// ---------------------------------------------------------------------------------------------

TEST(ParallelForTest, EveryItemIsVisitedExactlyOnce)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint64_t kItems = 10000u;
    std::vector<std::atomic<std::uint32_t>> visits(kItems);

    const an::Status status = an::parallelFor(execution, kItems, an::TaskCost{}, [&](an::TaskRange range) {
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            visits[static_cast<std::size_t>(index)].fetch_add(1u, std::memory_order_relaxed);
        }
        return an::Status::success();
    });

    ASSERT_TRUE(status.ok());
    for (std::uint64_t index = 0u; index < kItems; ++index)
    {
        EXPECT_EQ(visits[static_cast<std::size_t>(index)].load(std::memory_order_relaxed), 1u)
            << "item " << index << " was not visited exactly once";
    }
}

TEST(ParallelForTest, TheRangesArePartOfOnePartitionRatherThanOverlappingViews)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint64_t kItems = 5000u;
    std::mutex guard;
    std::vector<an::TaskRange> ranges;

    const an::Status status = an::parallelFor(execution, kItems, an::TaskCost{}, [&](an::TaskRange range) {
        const std::lock_guard<std::mutex> held(guard);
        ranges.push_back(range);
        return an::Status::success();
    });

    ASSERT_TRUE(status.ok());
    ASSERT_FALSE(ranges.empty());

    std::sort(ranges.begin(), ranges.end(),
              [](const an::TaskRange& left, const an::TaskRange& right) { return left.begin < right.begin; });

    EXPECT_EQ(ranges.front().begin, 0u);
    EXPECT_EQ(ranges.back().end, kItems);
    for (std::size_t index = 1u; index < ranges.size(); ++index)
    {
        EXPECT_EQ(ranges[index - 1u].end, ranges[index].begin) << "the partition has a gap or an overlap";
        EXPECT_FALSE(ranges[index].empty()) << "an empty range was handed to a body";
    }
}

TEST(ParallelForTest, NoItemsMeansTheBodyIsNeverCalled)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    std::atomic<std::uint32_t> calls{0u};
    const an::Status status = an::parallelFor(execution, 0u, an::TaskCost{}, [&](an::TaskRange) {
        calls.fetch_add(1u, std::memory_order_relaxed);
        return an::Status::success();
    });

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(calls.load(std::memory_order_relaxed), 0u);
}

TEST(ParallelForTest, AnExecutionThatHoldsNothingIsRefused)
{
    const an::ExecutionContext execution;

    std::atomic<std::uint32_t> calls{0u};
    const an::Status status = an::parallelFor(execution, 10u, an::TaskCost{}, [&](an::TaskRange) {
        calls.fetch_add(1u, std::memory_order_relaxed);
        return an::Status::success();
    });

    ASSERT_TRUE(status.failed());
    EXPECT_EQ(status.code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(calls.load(std::memory_order_relaxed), 0u);
}

TEST(ParallelForTest, ABodyFailureIsReported)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Status status = an::parallelFor(execution, 1000u, an::TaskCost{}, [&](an::TaskRange) {
        return an::Status::failure(an::ErrorCode::InvalidData);
    });

    ASSERT_TRUE(status.failed());
    EXPECT_EQ(status.code(), an::ErrorCode::InvalidData);
}

TEST(ParallelForTest, TheLowestFailingRangeIsTheOneReported)
{
    // Two different failures, and the earliest range must win however late it arrives. The first
    // chunk is deliberately made the *last* to fail, so a scheduler that reported whichever failure
    // reached it first would report the other one - which is what makes this case about ordering
    // rather than about a coincidence of scheduling.
    if (an::engineCapabilities().hostConcurrency < 2u)
    {
        GTEST_SKIP() << "this host reports a single thread, so nothing can fail while the first chunk waits";
    }

    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint64_t kItems = 1000u;
    std::atomic<bool> laterFailed{false};

    const an::Status status = an::parallelFor(execution, kItems, kOneItemPerTask, [&](an::TaskRange range) {
        if (range.begin != 0u)
        {
            laterFailed.store(true, std::memory_order_release);
            return an::Status::failure(an::ErrorCode::OutOfRange);
        }

        // Bounded, so a machine that cannot in fact run two chunks at once reports rather than
        // hangs: the assertion below then fails on the code, which is the honest outcome.
        for (int spin = 0; spin < 2000000 && !laterFailed.load(std::memory_order_acquire); ++spin)
        {
            std::this_thread::yield();
        }

        // The flag is raised inside the later body, which then still has to return through the
        // scheduler before its failure is recorded. Yielding well past that is what makes this
        // case about which range wins rather than about which thread was quicker.
        for (int settle = 0; settle < 20000; ++settle)
        {
            std::this_thread::yield();
        }
        return an::Status::failure(an::ErrorCode::InvalidData);
    });

    ASSERT_TRUE(status.failed());
    EXPECT_TRUE(laterFailed.load(std::memory_order_acquire)) << "no later chunk failed, so nothing was ordered";
    EXPECT_EQ(status.code(), an::ErrorCode::InvalidData);
}

TEST(ParallelForTest, AStopRequestedBeforeTheLoopStartsRunsNothing)
{
    an::EngineContext engine = makeEngineContext();
    std::stop_source source;
    const an::ExecutionContext execution = makeExecution(engine, an::kUseContextConcurrency, source.get_token());
    source.request_stop();

    std::atomic<std::uint32_t> calls{0u};
    const an::Status status = an::parallelFor(execution, 1000u, an::TaskCost{}, [&](an::TaskRange) {
        calls.fetch_add(1u, std::memory_order_relaxed);
        return an::Status::success();
    });

    ASSERT_TRUE(status.failed());
    EXPECT_EQ(status.code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(calls.load(std::memory_order_relaxed), 0u);
}

TEST(ParallelForTest, AStopRequestedDuringTheLoopEndsIt)
{
    an::EngineContext engine = makeEngineContext();
    std::stop_source source;
    const an::ExecutionContext execution = makeExecution(engine, an::kUseContextConcurrency, source.get_token());

    std::atomic<std::uint64_t> visited{0u};
    const an::Status status = an::parallelFor(execution, 1000000u, kOneItemPerTask, [&](an::TaskRange range) {
        if (visited.fetch_add(range.size(), std::memory_order_relaxed) > 16u)
        {
            source.request_stop();
        }
        return an::Status::success();
    });

    ASSERT_TRUE(status.failed());
    EXPECT_EQ(status.code(), an::ErrorCode::Cancelled);
    EXPECT_LT(visited.load(std::memory_order_relaxed), 1000000u) << "the stop request did not end the loop early";
}

// ---------------------------------------------------------------------------------------------
// parallelFor: how much of the machine one operation gets.
// ---------------------------------------------------------------------------------------------

TEST(ParallelForConcurrencyTest, AnOperationNarrowedToOneThreadRunsOneBodyAtATime)
{
    an::EngineContext engine = makeEngineContext(4u);
    const an::ExecutionContext execution = makeExecution(engine, 1u);
    ASSERT_EQ(execution.admittedConcurrency(), 1u);

    ConcurrencyProbe probe;
    const an::Status status = an::parallelFor(execution, 64u, kOneItemPerTask, [&](an::TaskRange) {
        probe.enter();
        std::this_thread::yield();
        probe.leave();
        return an::Status::success();
    });

    ASSERT_TRUE(status.ok());
    EXPECT_EQ(probe.peak(), 1u);
}

TEST(ParallelForConcurrencyTest, AnOperationNeverRunsMoreBodiesAtOnceThanItWasAdmitted)
{
    an::EngineContext engine = makeEngineContext(4u);
    const an::ExecutionContext execution = makeExecution(engine, 2u);
    ASSERT_EQ(execution.admittedConcurrency(), 2u);

    ConcurrencyProbe probe;
    const an::Status status = an::parallelFor(execution, 256u, kOneItemPerTask, [&](an::TaskRange) {
        probe.enter();
        std::this_thread::yield();
        probe.leave();
        return an::Status::success();
    });

    ASSERT_TRUE(status.ok());
    EXPECT_LE(probe.peak(), 2u);
}

TEST(ParallelForConcurrencyTest, AnOperationAdmittedLessThanItsCeilingRunsAtTheAdmittedWidth)
{
    // The case that separates the ceiling from the share. This operation's policy permits four
    // threads and the budget had one left to lend, so a loop that followed the ceiling would run
    // four bodies at once in a context that has already promised the rest elsewhere.
    an::EngineContext engine = makeEngineContext(4u);

    const an::ExecutionContext first = makeExecution(engine, 3u);
    ASSERT_EQ(first.admittedConcurrency(), 3u);

    const an::ExecutionContext second = makeExecution(engine);
    ASSERT_EQ(second.maxConcurrency(), 4u);
    ASSERT_EQ(second.admittedConcurrency(), 1u);

    // Each body waits for a second one to arrive rather than merely yielding. A body that only
    // yields can be finished before another thread has started, so a peak of one would also be what
    // a broken narrowing looks like on a quick machine; a body that waits is still there to be
    // counted. The wait is bounded, so correct narrowing costs this case a few milliseconds instead
    // of hanging.
    ConcurrencyProbe probe;
    const an::Status status = an::parallelFor(second, 8u, kOneItemPerTask, [&](an::TaskRange) {
        probe.enter();
        for (int spin = 0; spin < 20000 && probe.peak() < 2u; ++spin)
        {
            std::this_thread::yield();
        }
        probe.leave();
        return an::Status::success();
    });

    ASSERT_TRUE(status.ok());
    EXPECT_EQ(probe.peak(), 1u) << "the loop followed the operation's ceiling rather than its share";
}

TEST(ParallelForConcurrencyTest, TwoOverlappingOperationsNeverExceedTheContextTogether)
{
    // The aggregate guarantee. Each operation asks for half the context and gets it, so the two of
    // them together may use the whole machine the context was configured for and no more - which is
    // the property an admission budget exists to give and a per-operation arena alone would not.
    an::EngineContext engine = makeEngineContext(4u);

    ConcurrencyProbe shared;
    std::atomic<std::uint32_t> arrived{0u};

    const auto run = [&]() {
        const an::ExecutionContext execution = makeExecution(engine, 2u);
        EXPECT_EQ(execution.admittedConcurrency(), 2u);
        return an::parallelFor(execution, 512u, kOneItemPerTask, [&](an::TaskRange) {
            shared.enter();
            arrived.fetch_add(1u, std::memory_order_relaxed);
            std::this_thread::yield();
            shared.leave();
            return an::Status::success();
        });
    };

    an::Status second = an::Status::success();
    std::thread other([&]() { second = run(); });
    const an::Status first = run();
    other.join();

    EXPECT_TRUE(first.ok());
    EXPECT_TRUE(second.ok());
    EXPECT_GT(arrived.load(std::memory_order_relaxed), 0u);
    EXPECT_LE(shared.peak(), 4u) << "two operations in one context exceeded its maximum concurrency together";
}

TEST(ParallelForConcurrencyTest, TwoEngineContextsDoNotShareEachOthersThreads)
{
    // Each context has its own maximum and its own arena, so exhausting one says nothing about the
    // other. Measured per context rather than in aggregate, because the aggregate of two contexts is
    // deliberately allowed to reach the sum of their maxima.
    an::EngineContext first = makeEngineContext(1u);
    an::EngineContext second = makeEngineContext(2u);

    ConcurrencyProbe firstProbe;
    ConcurrencyProbe secondProbe;

    const auto run = [](const an::EngineContext& engine, ConcurrencyProbe& probe) {
        an::ExecutionOptions options;
        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        EXPECT_TRUE(execution.hasValue());
        if (!execution.hasValue())
        {
            return an::Status::failure(an::ErrorCode::Internal);
        }
        return an::parallelFor(*execution, 256u, kOneItemPerTask, [&](an::TaskRange) {
            probe.enter();
            std::this_thread::yield();
            probe.leave();
            return an::Status::success();
        });
    };

    an::Status secondStatus = an::Status::success();
    std::thread other([&]() { secondStatus = run(second, secondProbe); });
    const an::Status firstStatus = run(first, firstProbe);
    other.join();

    EXPECT_TRUE(firstStatus.ok());
    EXPECT_TRUE(secondStatus.ok());
    EXPECT_EQ(firstProbe.peak(), 1u);
    EXPECT_LE(secondProbe.peak(), 2u);
}

TEST(ParallelForConcurrencyTest, AnOperationAdmittedSeveralThreadsCanActuallyUseThem)
{
    // The one lower-bound case in the file, and the reason the others are safe as upper bounds: a
    // peak that only ever under-reports would let a scheduler that silently serialised everything
    // pass every case above. Each body waits for the others to arrive, so the peak cannot be one
    // unless the loop really did run on one thread - and the wait is bounded, so a machine that
    // genuinely cannot provide two threads reports rather than hangs.
    const std::uint32_t hosts = an::engineCapabilities().hostConcurrency;
    if (hosts < 2u)
    {
        GTEST_SKIP() << "this host reports a single thread, so there is no second one to observe";
    }

    an::EngineContext engine = makeEngineContext(2u);
    const an::ExecutionContext execution = makeExecution(engine);
    ASSERT_EQ(execution.admittedConcurrency(), 2u);

    std::atomic<std::uint32_t> arrived{0u};
    ConcurrencyProbe probe;

    const an::Status status = an::parallelFor(execution, 2u, kOneItemPerTask, [&](an::TaskRange) {
        probe.enter();
        arrived.fetch_add(1u, std::memory_order_acq_rel);
        for (int spin = 0; spin < 200000 && arrived.load(std::memory_order_acquire) < 2u; ++spin)
        {
            std::this_thread::yield();
        }
        probe.leave();
        return an::Status::success();
    });

    ASSERT_TRUE(status.ok());
    EXPECT_EQ(probe.peak(), 2u) << "an operation admitted two threads ran its bodies one at a time";
}

TEST(ParallelForConcurrencyTest, ANestedLoopStaysInsideTheContextsMaximum)
{
    an::EngineContext engine = makeEngineContext(2u);
    const an::ExecutionContext execution = makeExecution(engine);

    ConcurrencyProbe inner;
    std::atomic<std::uint64_t> innerItems{0u};

    const an::Status status = an::parallelFor(execution, 8u, kOneItemPerTask, [&](an::TaskRange) {
        return an::parallelFor(execution, 8u, kOneItemPerTask, [&](an::TaskRange innerRange) {
            inner.enter();
            innerItems.fetch_add(innerRange.size(), std::memory_order_relaxed);
            std::this_thread::yield();
            inner.leave();
            return an::Status::success();
        });
    });

    ASSERT_TRUE(status.ok());
    EXPECT_EQ(innerItems.load(std::memory_order_relaxed), 64u);
    EXPECT_LE(inner.peak(), 2u) << "a nested loop oversubscribed its engine context";
}

// ---------------------------------------------------------------------------------------------
// parallelReduce: the same answer whatever the machine.
// ---------------------------------------------------------------------------------------------

namespace
{

/// Adds two partial values. A plain function rather than a lambda, because a merge has no state and
/// the scheduler's parameter says so.
[[nodiscard]] double addPartials(double left, double right) noexcept
{
    return left + right;
}

/// Sums a deliberately awkward series over one range.
///
/// The terms differ in magnitude by many orders, so any change to the order in which partial sums
/// are combined changes the last bits of the answer. That is what makes the thread-count case below
/// a real test rather than a restatement of associativity over well-conditioned numbers.
///
/// @param range the items to sum.
/// @param partial the accumulator for this range, already at the identity.
/// @return success; this body cannot fail.
[[nodiscard]] an::Status sumAwkwardSeries(an::TaskRange range, double& partial)
{
    for (std::uint64_t index = range.begin; index < range.end; ++index)
    {
        partial += 1.0 / static_cast<double>(index + 1u);
    }
    return an::Status::success();
}

} // namespace

TEST(ParallelReduceTest, ASumIsCorrect)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Result<double> total = an::parallelReduce(
        execution, 100u, an::TaskCost{}, 0.0,
        [](an::TaskRange range, double& partial) {
            for (std::uint64_t index = range.begin; index < range.end; ++index)
            {
                partial += static_cast<double>(index);
            }
            return an::Status::success();
        },
        &addPartials);

    ASSERT_TRUE(total.hasValue());
    EXPECT_DOUBLE_EQ(*total, 4950.0);
}

TEST(ParallelReduceTest, NoItemsGivesTheIdentity)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Result<double> total = an::parallelReduce(
        execution, 0u, an::TaskCost{}, 7.5,
        [](an::TaskRange, double&) { return an::Status::failure(an::ErrorCode::Internal); }, &addPartials);

    ASSERT_TRUE(total.hasValue());
    EXPECT_DOUBLE_EQ(*total, 7.5);
}

TEST(ParallelReduceTest, PartialStorageUsesTheEngineContextsMemoryBudget)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 1u;
    engineOptions.limits = an::defaultEngineLimits();
    engineOptions.limits.memoryBudgetBytes = 64u;
    an::Result<an::EngineContext> created = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(created.hasValue());
    an::EngineContext engine = std::move(*created);
    const an::ExecutionContext execution = makeExecution(engine, 1u);

    const an::Result<double> total = an::parallelReduce(
        execution, 128u, kOneItemPerTask, 0.0,
        [](an::TaskRange range, double& partial) {
            partial += static_cast<double>(range.end - range.begin);
            return an::Status::success();
        },
        &addPartials);

    ASSERT_FALSE(total.hasValue());
    EXPECT_EQ(total.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    ASSERT_NE(execution.memoryResource(), nullptr);
    EXPECT_EQ(execution.memoryResource()->counters().deniedAllocations, 1u);
}

TEST(ParallelReduceTest, TheAnswerIsTheSameBitForBitAtEveryThreadCount)
{
    // The guarantee the whole reduction skeleton exists for. The partition is derived from the work
    // and never from the machine, and the partials are merged in a fixed tree, so the only thing
    // that changes between these runs is how many threads walked the same partition.
    constexpr std::uint64_t kItems = 300000u;
    constexpr an::TaskCost kCheap{.bytesPerItem = 8u, .operationsPerItem = 4u};

    an::EngineContext engine = makeEngineContext(8u);

    const auto reduceWith = [&](std::uint32_t threads) {
        const an::ExecutionContext execution = makeExecution(engine, threads);
        const an::Result<double> total =
            an::parallelReduce(execution, kItems, kCheap, 0.0, &sumAwkwardSeries, &addPartials);
        EXPECT_TRUE(total.hasValue());
        return total.hasValue() ? *total : 0.0;
    };

    const double reference = reduceWith(1u);
    for (const std::uint32_t threads : {2u, 3u, 5u, 8u})
    {
        EXPECT_EQ(reduceWith(threads), reference) << "the reduction changed answer at " << threads << " threads";
    }
}

TEST(ParallelReduceTest, TheAnswerDoesNotDependOnHowBusyTheContextIs)
{
    // The same guarantee seen from the other side. A second operation running at the same time
    // changes what the admission budget grants, so an answer that followed the admitted share
    // rather than the work would move here and nowhere else.
    constexpr std::uint64_t kItems = 200000u;
    constexpr an::TaskCost kCheap{.bytesPerItem = 8u, .operationsPerItem = 4u};

    an::EngineContext engine = makeEngineContext(4u);

    double aloneValue = 0.0;
    {
        const an::ExecutionContext quiet = makeExecution(engine);
        const an::Result<double> alone =
            an::parallelReduce(quiet, kItems, kCheap, 0.0, &sumAwkwardSeries, &addPartials);
        ASSERT_TRUE(alone.hasValue());
        aloneValue = *alone;
    }

    std::atomic<bool> keepBusy{true};
    std::atomic<bool> otherReady{false};
    std::thread busy([&]() {
        const an::ExecutionContext other = makeExecution(engine, 2u);
        otherReady.store(true, std::memory_order_release);
        while (keepBusy.load(std::memory_order_acquire))
        {
            const an::Status status =
                an::parallelFor(other, 4096u, an::TaskCost{}, [](an::TaskRange) { return an::Status::success(); });
            EXPECT_TRUE(status.ok());
        }
    });

    while (!otherReady.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    const an::ExecutionContext contended = makeExecution(engine);
    const an::Result<double> shared =
        an::parallelReduce(contended, kItems, kCheap, 0.0, &sumAwkwardSeries, &addPartials);

    keepBusy.store(false, std::memory_order_release);
    busy.join();

    ASSERT_TRUE(shared.hasValue());
    EXPECT_EQ(*shared, aloneValue);
}

TEST(ParallelReduceTest, PartialsHandedToDifferentWorkersNeverShareACacheLine)
{
    // A body that accumulates through its partial on every item stores to that partial on every
    // item under a compiler without type-based alias analysis. If the partials of chunks running
    // at once sat beside each other, those stores would fight over one cache line and the
    // reduction would get slower as threads were added, so the partial a body is handed must be
    // private to the worker running it.
    constexpr std::uint64_t kChunks = 256u;
    constexpr std::uintptr_t kCacheLineBytes = 64u;

    an::EngineContext engine = makeEngineContext(4u);
    const an::ExecutionContext execution = makeExecution(engine, 4u);

    struct Observation
    {
        std::thread::id thread;
        const double* partial;
    };
    std::mutex guard;
    std::vector<Observation> observations;

    const an::Result<double> total = an::parallelReduce(
        execution, kChunks, kOneItemPerTask, 0.0,
        [&](an::TaskRange range, double& partial) {
            // Enough work per chunk that the other workers join in before the chunks run out.
            double spin = 0.0;
            for (std::uint32_t step = 0u; step < 20000u; ++step)
            {
                spin += std::sqrt(static_cast<double>(step + range.begin));
            }
            partial += spin > 0.0 ? 1.0 : 0.0;
            const std::lock_guard<std::mutex> lock(guard);
            observations.push_back({std::this_thread::get_id(), &partial});
            return an::Status::success();
        },
        &addPartials);

    ASSERT_TRUE(total.hasValue());
    EXPECT_DOUBLE_EQ(*total, static_cast<double>(kChunks));
    ASSERT_EQ(observations.size(), kChunks);

    std::set<std::thread::id> threads;
    for (const Observation& observation : observations)
    {
        threads.insert(observation.thread);
    }
    ASSERT_GE(threads.size(), 2u) << "every chunk ran on one thread, so no two partials were ever live at once";

    for (std::size_t first = 0u; first < observations.size(); ++first)
    {
        for (std::size_t second = first + 1u; second < observations.size(); ++second)
        {
            if (observations[first].thread == observations[second].thread)
            {
                continue;
            }
            const std::uintptr_t left = reinterpret_cast<std::uintptr_t>(observations[first].partial);
            const std::uintptr_t right = reinterpret_cast<std::uintptr_t>(observations[second].partial);
            const std::uintptr_t distance = left > right ? left - right : right - left;
            ASSERT_GE(distance, kCacheLineBytes)
                << "chunks " << first << " and " << second << " ran on different threads with partials " << distance
                << " bytes apart";
        }
    }
}

TEST(ParallelReduceTest, ABodyFailureIsReportedAndNoValueIsProduced)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Result<double> total = an::parallelReduce(
        execution, 1000u, an::TaskCost{}, 0.0,
        [](an::TaskRange, double&) { return an::Status::failure(an::ErrorCode::InvalidData); }, &addPartials);

    ASSERT_FALSE(total.hasValue());
    EXPECT_EQ(total.status().code(), an::ErrorCode::InvalidData);
}

TEST(ParallelReduceTest, AStopRequestIsReportedAsACancellation)
{
    an::EngineContext engine = makeEngineContext();
    std::stop_source source;
    const an::ExecutionContext execution = makeExecution(engine, an::kUseContextConcurrency, source.get_token());
    source.request_stop();

    const an::Result<double> total = an::parallelReduce(
        execution, 1000u, an::TaskCost{}, 0.0,
        [](an::TaskRange, double& partial) {
            partial += 1.0;
            return an::Status::success();
        },
        &addPartials);

    ASSERT_FALSE(total.hasValue());
    EXPECT_EQ(total.status().code(), an::ErrorCode::Cancelled);
}

TEST(ParallelReduceTest, AnExecutionThatHoldsNothingIsRefused)
{
    const an::ExecutionContext execution;

    const an::Result<double> total = an::parallelReduce(
        execution, 10u, an::TaskCost{}, 0.0,
        [](an::TaskRange, double& partial) {
            partial += 1.0;
            return an::Status::success();
        },
        &addPartials);

    ASSERT_FALSE(total.hasValue());
    EXPECT_EQ(total.status().code(), an::ErrorCode::NotInitialized);
}

TEST(ParallelReduceTest, AMergeIsRequiredAndAMissingOneIsRefusedRatherThanCalled)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::Result<double> total = an::parallelReduce(
        execution, 10u, an::TaskCost{}, 0.0,
        [](an::TaskRange, double& partial) {
            partial += 1.0;
            return an::Status::success();
        },
        nullptr);

    ASSERT_FALSE(total.hasValue());
    EXPECT_EQ(total.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(ParallelForConcurrencyTest, ThePlannedWorkerCountIsWhatASweepShouldRecord)
{
    // A scaling sweep forces a thread count by building a context with it, and then has to record
    // what actually ran rather than what it asked for. These are the three ways the two differ.
    an::EngineContext fullEngine = makeEngineContext(4u);

    const an::ExecutionContext full = makeExecution(fullEngine);
    EXPECT_EQ(an::plannedWorkerCount(full, 1000000u, an::TaskCost{}), 4u);

    // Fewer chunks than threads: a loop this small cannot use four of anything.
    EXPECT_EQ(an::plannedWorkerCount(full, 2u, kOneItemPerTask), 2u);

    // Nothing to do, and nothing to run it on.
    EXPECT_EQ(an::plannedWorkerCount(full, 0u, an::TaskCost{}), 0u);
    EXPECT_EQ(an::plannedWorkerCount(an::ExecutionContext{}, 1000u, an::TaskCost{}), 0u);

    // Admitted less than it asked for, because a separate context is already busy.
    an::EngineContext engine = makeEngineContext(4u);
    const an::ExecutionContext busy = makeExecution(engine, 3u);
    const an::ExecutionContext late = makeExecution(engine);
    EXPECT_EQ(late.maxConcurrency(), 4u);
    EXPECT_EQ(an::plannedWorkerCount(late, 1000000u, an::TaskCost{}), 1u);
}

TEST(ParallelForConcurrencyTest, ANestedLoopInheritsTheOperationsWidthWithoutBeingTold)
{
    // The property the whole grant-is-an-arena design exists for, seen from the inside. The inner
    // loop is a separate parallel region that says nothing about concurrency; it is narrow because
    // of where it runs, not because anybody passed it a number. That is what lets an adopted
    // algorithm's own `tbb::parallel_for` respect the user's setting without being edited.
    an::EngineContext engine = makeEngineContext(8u);
    const an::ExecutionContext execution = makeExecution(engine, 2u);
    ASSERT_EQ(execution.admittedConcurrency(), 2u);

    ConcurrencyProbe inner;
    std::atomic<std::uint64_t> innerItems{0u};

    const an::Status status = an::parallelFor(execution, 4u, kOneItemPerTask, [&](an::TaskRange) {
        return an::parallelFor(execution, 32u, kOneItemPerTask, [&](an::TaskRange innerRange) {
            inner.enter();
            for (int spin = 0; spin < 20000 && inner.peak() < 3u; ++spin)
            {
                std::this_thread::yield();
            }
            innerItems.fetch_add(innerRange.size(), std::memory_order_relaxed);
            inner.leave();
            return an::Status::success();
        });
    });

    ASSERT_TRUE(status.ok());
    EXPECT_EQ(innerItems.load(std::memory_order_relaxed), 128u);
    EXPECT_LE(inner.peak(), 2u) << "a nested loop ran wider than the operation that contains it";
}
