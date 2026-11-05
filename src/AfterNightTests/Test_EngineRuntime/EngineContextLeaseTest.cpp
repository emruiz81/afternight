// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the lease half of AfterNightEngine/Runtime/EngineContext.h.
//
// A lease is how the engine knows whether a context is quiescent, and quiescence is what makes
// "destroy it and build a replacement" a safe instruction rather than a hopeful one. So the cases
// here are about counting and about lifetime, and they are deliberately picky about both: a lease
// that is released twice must count once, a lease that is moved must not count twice, and a lease
// that outlives the handle that produced it must keep the context's state alive rather than
// pointing at freed memory.
//
// The last of those is the case that matters most and asserts least. What it really proves is
// proved by the sanitizer job running the same suite: reading through a lease after the handle is
// gone is either a use-after-free or it is nothing, and only an instrumented run can tell.

#include <AfterNightEngine/Runtime/EngineContext.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

/// A context every case in this file can start from.
[[nodiscard]] an::EngineContext makeContext()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

TEST(EngineContextLeaseTest, IsMoveOnly)
{
    // Copying a lease would be the count going up without a call site asking for it, which is the
    // one way a quiescence check can silently start reporting the wrong answer.
    static_assert(!std::is_copy_constructible_v<an::EngineContextLease>);
    static_assert(!std::is_copy_assignable_v<an::EngineContextLease>);
    static_assert(std::is_nothrow_move_constructible_v<an::EngineContextLease>);
    static_assert(std::is_nothrow_move_assignable_v<an::EngineContextLease>);
    static_assert(std::is_nothrow_default_constructible_v<an::EngineContextLease>);
}

TEST(EngineContextLeaseTest, ADefaultConstructedLeaseHoldsNothingAndReleasesNothing)
{
    an::EngineContextLease lease;
    EXPECT_FALSE(lease.valid());
    EXPECT_EQ(lease.contextId(), 0u);

    lease.release();
    EXPECT_FALSE(lease.valid());
    EXPECT_EQ(lease.contextId(), 0u);
}

TEST(EngineContextLeaseTest, HoldingOneMakesTheContextNonQuiescentAndNamesIt)
{
    const an::EngineContext context = makeContext();
    ASSERT_TRUE(context.valid());
    ASSERT_TRUE(context.quiescent());

    const an::Result<an::EngineContextLease> lease = context.acquireLease();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();

    EXPECT_TRUE(lease->valid());
    EXPECT_EQ(lease->contextId(), context.id());
    EXPECT_EQ(context.outstandingLeases(), 1u);
    EXPECT_FALSE(context.quiescent());
}

TEST(EngineContextLeaseTest, LeavingScopeReleasesTheHold)
{
    const an::EngineContext context = makeContext();
    ASSERT_TRUE(context.valid());

    {
        const an::Result<an::EngineContextLease> lease = context.acquireLease();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        EXPECT_EQ(context.outstandingLeases(), 1u);
    }

    EXPECT_EQ(context.outstandingLeases(), 0u);
    EXPECT_TRUE(context.quiescent());
}

TEST(EngineContextLeaseTest, ReleasingEarlyIsTheSameAsLeavingScope)
{
    const an::EngineContext context = makeContext();
    ASSERT_TRUE(context.valid());

    an::Result<an::EngineContextLease> lease = context.acquireLease();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();

    lease->release();
    EXPECT_FALSE(lease->valid());
    EXPECT_EQ(lease->contextId(), 0u);
    EXPECT_TRUE(context.quiescent());

    // Released twice by hand, and once more by the destructor when this scope ends. A lease that
    // decremented on each of those would take the count below zero, which on an unsigned counter
    // is a context that can never be shut down again.
    lease->release();
    EXPECT_TRUE(context.quiescent());
}

TEST(EngineContextLeaseTest, SeveralLeasesAreCountedSeparately)
{
    const an::EngineContext context = makeContext();
    ASSERT_TRUE(context.valid());

    std::vector<an::EngineContextLease> held;
    for (int index = 0; index < 8; ++index)
    {
        an::Result<an::EngineContextLease> lease = context.acquireLease();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        held.push_back(std::move(*lease));
    }

    // Eight at once, on a context whose maximum concurrency is four. A lease is a statement that
    // work is outstanding, not a thread reservation, so the two numbers are unrelated.
    EXPECT_EQ(context.outstandingLeases(), 8u);
    EXPECT_EQ(context.maxConcurrency(), 4u);

    held.clear();
    EXPECT_TRUE(context.quiescent());
}

TEST(EngineContextLeaseTest, MovingALeaseTransfersTheHoldWithoutCountingItTwice)
{
    const an::EngineContext context = makeContext();
    ASSERT_TRUE(context.valid());

    an::Result<an::EngineContextLease> acquired = context.acquireLease();
    ASSERT_TRUE(acquired.hasValue()) << acquired.status().message();

    an::EngineContextLease source = std::move(*acquired);
    const std::uint64_t id = source.contextId();
    ASSERT_TRUE(source.valid());
    EXPECT_EQ(context.outstandingLeases(), 1u);

    an::EngineContextLease target = std::move(source);
    EXPECT_TRUE(target.valid());
    EXPECT_EQ(target.contextId(), id);
    EXPECT_FALSE(source.valid());
    EXPECT_EQ(context.outstandingLeases(), 1u);

    target.release();
    EXPECT_TRUE(context.quiescent());
}

TEST(EngineContextLeaseTest, MoveAssignmentReleasesTheLeaseItReplaces)
{
    const an::EngineContext context = makeContext();
    ASSERT_TRUE(context.valid());

    an::Result<an::EngineContextLease> first = context.acquireLease();
    an::Result<an::EngineContextLease> second = context.acquireLease();
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    an::EngineContextLease held = std::move(*first);
    EXPECT_EQ(context.outstandingLeases(), 2u);

    // The lease being overwritten is released here rather than leaked, so the count goes down by
    // one and not by none.
    held = std::move(*second);
    EXPECT_TRUE(held.valid());
    EXPECT_EQ(context.outstandingLeases(), 1u);

    held.release();
    EXPECT_TRUE(context.quiescent());
}

TEST(EngineContextLeaseTest, ALeaseOutlivesTheHandleThatProducedItWithoutDangling)
{
    an::EngineContextLease lease;
    std::uint64_t id = 0u;

    {
        const an::EngineContext context = makeContext();
        ASSERT_TRUE(context.valid());
        id = context.id();

        an::Result<an::EngineContextLease> acquired = context.acquireLease();
        ASSERT_TRUE(acquired.hasValue()) << acquired.status().message();
        lease = std::move(*acquired);
    }

    // Destroying a context that still has work outstanding is a defect in the caller. What the
    // engine guarantees is that it is a late release rather than a use-after-free: the state stays
    // alive under the lease, so the outstanding work reads what it was given and the services go
    // when the last lease does.
    EXPECT_TRUE(lease.valid());
    EXPECT_EQ(lease.contextId(), id);

    lease.release();
    EXPECT_FALSE(lease.valid());
}

TEST(EngineContextLeaseTest, LeasesTakenFromSeveralThreadsAreCountedExactly)
{
    const an::EngineContext context = makeContext();
    ASSERT_TRUE(context.valid());

    constexpr int kThreadCount = 8;
    constexpr int kCyclesPerThread = 2000;

    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    for (int worker = 0; worker < kThreadCount; ++worker)
    {
        workers.emplace_back([&context] {
            for (int cycle = 0; cycle < kCyclesPerThread; ++cycle)
            {
                an::Result<an::EngineContextLease> lease = context.acquireLease();
                if (!lease.hasValue() || !lease->valid())
                {
                    ADD_FAILURE() << "a lease was refused by a live context";
                    return;
                }
            }
        });
    }

    for (std::thread& worker : workers)
    {
        worker.join();
    }

    // Sixteen thousand acquisitions and releases across eight threads, and the count is back where
    // it started. A counter that was not atomic loses increments here rather than failing loudly,
    // which is why the number of cycles is large enough for that loss to be near-certain.
    EXPECT_EQ(context.outstandingLeases(), 0u);
    EXPECT_TRUE(context.quiescent());
}

} // namespace
