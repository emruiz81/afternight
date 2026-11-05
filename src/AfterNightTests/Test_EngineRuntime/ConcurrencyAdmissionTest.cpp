// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the admission an engine context grants a piece of work.
//
// A lease already said that one piece of work is outstanding. These cases are about the second
// thing it says: how much of the machine that work claimed. The two are one object because they
// have one lifetime - work that has finished is neither outstanding nor holding a share - and
// because separating them would mean two things to remember to release instead of one.
//
// Every execution needs at least one admitted worker. Work that arrives after the budget is empty
// is refused with `Busy`; otherwise a one-worker arena would run outside the accounting and the sum
// of active execution widths could exceed the context's configured maximum.
//
// The budget is the engine context's, so the cases below are mostly about arithmetic that has to
// come out right when several holders arrive at once. What the admitted number then does to a
// running loop is measured in TaskSchedulerTest, next door.

#include <AfterNightEngine/Runtime/EngineContext.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// An engine context with a stated ceiling, for the cases that divide it up.
///
/// @param maxConcurrency the ceiling to configure.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint32_t maxConcurrency)
{
    an::EngineContextOptions options;
    options.maxConcurrency = maxConcurrency;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

} // namespace

TEST(ConcurrencyAdmissionTest, ALeaseThatAsksForNothingHoldsNoShare)
{
    // A plain lease: a caller that only wants the context kept open does not take part in the
    // division of the machine.
    an::EngineContext engine = makeEngineContext(4u);

    const an::Result<an::EngineContextLease> lease = engine.acquireLease();
    ASSERT_TRUE(lease.hasValue());
    EXPECT_EQ(lease->admittedConcurrency(), 0u);
    EXPECT_EQ(engine.admittedConcurrency(), 0u);
    EXPECT_EQ(engine.availableConcurrency(), 4u);
}

TEST(ConcurrencyAdmissionTest, AskingForLessThanIsFreeGrantsExactlyThat)
{
    an::EngineContext engine = makeEngineContext(4u);

    const an::Result<an::EngineContextLease> lease = engine.acquireLease(2u);
    ASSERT_TRUE(lease.hasValue());
    EXPECT_EQ(lease->admittedConcurrency(), 2u);
    EXPECT_EQ(engine.admittedConcurrency(), 2u);
    EXPECT_EQ(engine.availableConcurrency(), 2u);
}

TEST(ConcurrencyAdmissionTest, AskingForMoreThanIsFreeGrantsWhatIsLeft)
{
    an::EngineContext engine = makeEngineContext(4u);

    const an::Result<an::EngineContextLease> first = engine.acquireLease(3u);
    ASSERT_TRUE(first.hasValue());
    const an::Result<an::EngineContextLease> second = engine.acquireLease(3u);
    ASSERT_TRUE(second.hasValue());

    EXPECT_EQ(first->admittedConcurrency(), 3u);
    EXPECT_EQ(second->admittedConcurrency(), 1u);
    EXPECT_EQ(engine.admittedConcurrency(), 4u);
    EXPECT_EQ(engine.availableConcurrency(), 0u);
}

TEST(ConcurrencyAdmissionTest, WorkThatArrivesAtAFullContextIsRefused)
{
    an::EngineContext engine = makeEngineContext(2u);

    const an::Result<an::EngineContextLease> all = engine.acquireLease(2u);
    ASSERT_TRUE(all.hasValue());
    const an::Result<an::EngineContextLease> late = engine.acquireLease(2u);
    ASSERT_FALSE(late.hasValue());

    EXPECT_EQ(all->admittedConcurrency(), 2u);
    EXPECT_EQ(late.status().code(), an::ErrorCode::Busy);
    EXPECT_EQ(engine.admittedConcurrency(), 2u);
    EXPECT_EQ(engine.availableConcurrency(), 0u);
    EXPECT_EQ(engine.outstandingLeases(), 1u);
}

TEST(ConcurrencyAdmissionTest, ReleasingGivesTheShareBack)
{
    an::EngineContext engine = makeEngineContext(4u);

    {
        an::Result<an::EngineContextLease> held = engine.acquireLease(4u);
        ASSERT_TRUE(held.hasValue());
        EXPECT_EQ(engine.availableConcurrency(), 0u);
        held->release();
        EXPECT_EQ(engine.availableConcurrency(), 4u);
        EXPECT_EQ(held->admittedConcurrency(), 0u);

        // Releasing by hand and then again through the destructor counts once, for the share as
        // well as for the outstanding count.
    }

    EXPECT_EQ(engine.availableConcurrency(), 4u);
    EXPECT_EQ(engine.outstandingLeases(), 0u);
}

TEST(ConcurrencyAdmissionTest, TheScopeOfALeaseIsTheScopeOfItsShare)
{
    an::EngineContext engine = makeEngineContext(4u);

    {
        const an::Result<an::EngineContextLease> held = engine.acquireLease(3u);
        ASSERT_TRUE(held.hasValue());
        EXPECT_EQ(engine.availableConcurrency(), 1u);
    }

    EXPECT_EQ(engine.availableConcurrency(), 4u);
}

TEST(ConcurrencyAdmissionTest, MovingALeaseMovesItsShareRatherThanTakingASecond)
{
    an::EngineContext engine = makeEngineContext(4u);

    an::Result<an::EngineContextLease> original = engine.acquireLease(3u);
    ASSERT_TRUE(original.hasValue());

    const an::EngineContextLease moved{std::move(*original)};

    EXPECT_EQ(moved.admittedConcurrency(), 3u);
    EXPECT_EQ(original->admittedConcurrency(), 0u);
    EXPECT_EQ(engine.admittedConcurrency(), 3u);
    EXPECT_EQ(engine.outstandingLeases(), 1u);
}

TEST(ConcurrencyAdmissionTest, MoveAssignmentReleasesTheShareItOverwrites)
{
    an::EngineContext engine = makeEngineContext(4u);

    an::Result<an::EngineContextLease> overwritten = engine.acquireLease(3u);
    ASSERT_TRUE(overwritten.hasValue());
    an::Result<an::EngineContextLease> taken = engine.acquireLease(1u);
    ASSERT_TRUE(taken.hasValue());
    EXPECT_EQ(engine.admittedConcurrency(), 4u);

    *overwritten = std::move(*taken);

    EXPECT_EQ(overwritten->admittedConcurrency(), 1u);
    EXPECT_EQ(engine.admittedConcurrency(), 1u);
    EXPECT_EQ(engine.outstandingLeases(), 1u);
}

TEST(ConcurrencyAdmissionTest, ALeaseThatHoldsNothingReportsNoShare)
{
    const an::EngineContextLease empty;
    EXPECT_EQ(empty.admittedConcurrency(), 0u);
    EXPECT_EQ(empty.contextId(), 0u);
}

TEST(ConcurrencyAdmissionTest, AContextThatHoldsNothingReportsNoBudget)
{
    const an::EngineContext empty;
    EXPECT_EQ(empty.admittedConcurrency(), 0u);
    EXPECT_EQ(empty.availableConcurrency(), 0u);
}

TEST(ConcurrencyAdmissionTest, AskingForMoreThanTheContextPermitsIsNarrowedRatherThanRefused)
{
    // Asking a *lease* for too much is not the same mistake as asking an *execution context* for
    // too much. An execution context is a policy a caller stated and is refused when it exceeds the
    // user's setting; a lease is the engine dividing what it has, so it narrows.
    an::EngineContext engine = makeEngineContext(2u);

    const an::Result<an::EngineContextLease> lease = engine.acquireLease(64u);
    ASSERT_TRUE(lease.hasValue());
    EXPECT_EQ(lease->admittedConcurrency(), 2u);
}

TEST(ConcurrencyAdmissionTest, TheBudgetIsPerContext)
{
    an::EngineContext first = makeEngineContext(2u);
    an::EngineContext second = makeEngineContext(2u);

    const an::Result<an::EngineContextLease> exhausting = first.acquireLease(2u);
    ASSERT_TRUE(exhausting.hasValue());

    EXPECT_EQ(first.availableConcurrency(), 0u);
    EXPECT_EQ(second.availableConcurrency(), 2u);

    const an::Result<an::EngineContextLease> unaffected = second.acquireLease(2u);
    ASSERT_TRUE(unaffected.hasValue());
    EXPECT_EQ(unaffected->admittedConcurrency(), 2u);
}

TEST(ConcurrencyAdmissionTest, ContendedAcquisitionNeverGrantsMoreThanTheBudgetHolds)
{
    // The arithmetic under contention. Every thread asks for one, so the sum of what is granted may
    // not exceed the ceiling while they all hold at once - and the count has to come back to zero
    // afterwards, which is what catches a release that fires twice.
    constexpr std::uint32_t kCeiling = 8u;
    constexpr std::uint32_t kThreads = 16u;

    an::EngineContext engine = makeEngineContext(kCeiling);

    std::atomic<std::uint32_t> ready{0u};
    std::atomic<std::uint32_t> granted{0u};
    std::atomic<std::uint32_t> refused{0u};
    std::atomic<bool> release{false};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (std::uint32_t index = 0u; index < kThreads; ++index)
    {
        threads.emplace_back([&]() {
            an::Result<an::EngineContextLease> lease = engine.acquireLease(1u);
            if (lease.hasValue())
            {
                granted.fetch_add(lease->admittedConcurrency(), std::memory_order_acq_rel);
            }
            else
            {
                EXPECT_EQ(lease.status().code(), an::ErrorCode::Busy);
                refused.fetch_add(1u, std::memory_order_acq_rel);
            }
            ready.fetch_add(1u, std::memory_order_acq_rel);
            while (!release.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < kThreads)
    {
        std::this_thread::yield();
    }

    // Eight requests receive a worker and the rest are refused. No accepted execution runs outside
    // the context's accounting.
    EXPECT_EQ(engine.admittedConcurrency(), kCeiling);
    EXPECT_EQ(engine.availableConcurrency(), 0u);
    EXPECT_EQ(granted.load(std::memory_order_acquire), kCeiling);
    EXPECT_EQ(refused.load(std::memory_order_acquire), kThreads - kCeiling);

    release.store(true, std::memory_order_release);
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(engine.admittedConcurrency(), 0u);
    EXPECT_EQ(engine.availableConcurrency(), kCeiling);
    EXPECT_EQ(engine.outstandingLeases(), 0u);
}

TEST(ConcurrencyAdmissionTest, AnExecutionContextTakesItsShareWhenItStarts)
{
    an::EngineContext engine = makeEngineContext(4u);

    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    EXPECT_EQ(execution->maxConcurrency(), 2u);
    EXPECT_EQ(execution->admittedConcurrency(), 2u);
    EXPECT_EQ(engine.admittedConcurrency(), 2u);
}

TEST(ConcurrencyAdmissionTest, AnOperationsCeilingAndItsShareAreDifferentNumbers)
{
    // The distinction the accessors exist to make. What a caller asked for is a policy and does not
    // change while other work comes and goes; what it was admitted is the engine's division of the
    // machine at the moment the operation started.
    an::EngineContext engine = makeEngineContext(4u);
    ASSERT_EQ(engine.availableConcurrency(), 4u);

    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    const an::Result<an::ExecutionContext> first = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(first.hasValue());
    options.maxConcurrency = 4u;
    const an::Result<an::ExecutionContext> second = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(second.hasValue());

    EXPECT_EQ(first->maxConcurrency(), 3u);
    EXPECT_EQ(second->maxConcurrency(), 4u);
    EXPECT_EQ(first->admittedConcurrency(), 3u);
    EXPECT_EQ(second->admittedConcurrency(), 1u);
}

TEST(ConcurrencyAdmissionTest, AnExecutionContextGivesItsShareBackWhenItEnds)
{
    an::EngineContext engine = makeEngineContext(4u);

    {
        an::ExecutionOptions options;
        const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue());
        EXPECT_EQ(engine.availableConcurrency(), 0u);
    }

    EXPECT_EQ(engine.availableConcurrency(), 4u);
}

TEST(ConcurrencyAdmissionTest, AMovedExecutionContextCarriesItsShareWithIt)
{
    an::EngineContext engine = makeEngineContext(4u);

    an::ExecutionOptions options;
    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(created.hasValue());

    const an::ExecutionContext moved{std::move(*created)};

    EXPECT_EQ(moved.admittedConcurrency(), 4u);
    EXPECT_EQ(created->admittedConcurrency(), 0u);
    EXPECT_EQ(engine.admittedConcurrency(), 4u);
}

TEST(ConcurrencyAdmissionTest, AnExhaustedBudgetRefusesAnotherOperation)
{
    an::EngineContext engine = makeEngineContext(1u);

    an::ExecutionOptions options;
    const an::Result<an::ExecutionContext> first = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(first.hasValue());
    const an::Result<an::ExecutionContext> second = an::ExecutionContext::create(engine, options);
    ASSERT_FALSE(second.hasValue());

    EXPECT_EQ(second.status().code(), an::ErrorCode::Busy);
    EXPECT_EQ(engine.outstandingLeases(), 1u);
}
