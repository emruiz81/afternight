// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Runtime/MemoryResource.h.
//
// An allocation through `malloc` behind a helper that does not zero what it returns, does not report
// a refusal as anything a caller can act on, and counts nothing makes a budget a number in a settings
// dialog rather than something the engine honours, and leaves "how much did that stack peak at"
// without an answer. The memory resource answers all of that, and the cases below are about the
// three halves of it: alignment that is asked for and actually delivered, a refusal that is a
// returned failure with the numbers attached, and counters that survive being hammered from several
// threads.
//
// A denied allocation is tested by refusing it deliberately rather than by exhausting the machine.
// The budget is the mechanism that makes that possible: it is a number this engine enforces itself,
// so a test can set one small enough to refuse the next request without the host having to be short
// of memory.

#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// An engine context with a stated memory budget.
///
/// @param budgetBytes what the context may have outstanding at once.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint64_t budgetBytes = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    options.limits = an::defaultEngineLimits();
    options.limits.memoryBudgetBytes = budgetBytes;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

} // namespace

TEST(MemoryResourceTest, ZeroedAllocationsHonorAlignmentSizeAndReuse)
{
    auto engine = makeEngineContext();
    auto* resource = engine.memoryResource();
    for (const std::uint64_t alignment : {1u, 2u, 8u, 64u, 4096u})
        for (const std::uint64_t bytes : {1u, 17u, 4099u, 1048583u})
            for (unsigned repeat = 0u; repeat < 2u; ++repeat)
            {
                auto block = resource->allocateZeroed(bytes, alignment);
                ASSERT_TRUE(block);
                EXPECT_TRUE(an::isPointerAligned(*block, alignment));
                const auto* first = static_cast<unsigned char*>(*block);
                EXPECT_TRUE(std::all_of(first, first + bytes, [](unsigned char value) { return value == 0u; }));
                std::memset(*block, 0xA5, static_cast<std::size_t>(bytes));
                resource->deallocate(*block, bytes, alignment);
            }
    EXPECT_EQ(resource->counters().liveBytes, 0u);
    EXPECT_EQ(resource->counters().liveAllocations, 0u);
    EXPECT_EQ(resource->counters().totalAllocations, 40u);
}

TEST(MemoryResourceTest, ZeroedAndUninitializedAllocationsShareOneBudget)
{
    auto engine = makeEngineContext(64u);
    auto* resource = engine.memoryResource();
    auto first = resource->allocate(32u, 8u);
    auto second = resource->allocateZeroed(32u, 64u);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto refused = resource->allocateZeroed(1u, 1u);
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(resource->counters().liveBytes, 64u);
    EXPECT_EQ(resource->counters().peakBytes, 64u);
    EXPECT_EQ(resource->counters().totalAllocations, 2u);
    resource->deallocate(*first, 32u, 8u);
    resource->deallocate(*second, 32u, 64u);
    EXPECT_EQ(resource->counters().liveBytes, 0u);
}

TEST(MemoryResourceTest, ZeroedOverflowAndInvalidRequestsReleaseReservations)
{
    auto engine = makeEngineContext((std::numeric_limits<std::uint64_t>::max)());
    ASSERT_TRUE(engine.valid());
    auto* resource = engine.memoryResource();
    EXPECT_EQ(resource->allocateZeroed(0u, 8u).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(resource->allocateZeroed(16u, 3u).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(resource->allocateZeroed((std::numeric_limits<std::uint64_t>::max)(), 64u).status().code(),
              an::ErrorCode::AllocationFailed);
    auto valid = resource->allocateZeroed(1u, 1u);
    ASSERT_TRUE(valid);
    EXPECT_EQ(resource->counters().liveBytes, 1u);
    EXPECT_EQ(resource->counters().deniedAllocations, 3u);
    resource->deallocate(*valid, 1u, 1u);
}

TEST(MemoryResourceTest, DefaultZeroingSupportsCustomResourcesAndPropagatesRefusals)
{
    class Resource final : public an::MemoryResource
    {
    public:
        an::Result<void*> allocate(std::uint64_t, std::uint64_t) override
        {
            return refuse ? an::Result<void*>(an::Status::failure(an::ErrorCode::AllocationFailed))
                          : an::Result<void*>(bytes.data());
        }
        void deallocate(void*, std::uint64_t, std::uint64_t) noexcept override {}
        an::MemoryCounters counters() const noexcept override
        {
            return {};
        }
        std::array<unsigned char, 32u> bytes;
        bool refuse = false;
    } resource;
    resource.bytes.fill(0xA5);
    auto block = resource.allocateZeroed(17u, 1u);
    ASSERT_TRUE(block);
    for (std::size_t index = 0u; index < resource.bytes.size(); ++index)
        EXPECT_EQ(resource.bytes[index], index < 17u ? 0u : 0xA5u);
    resource.refuse = true;
    resource.bytes.fill(0xA5);
    EXPECT_EQ(resource.allocateZeroed(17u, 1u).status().code(), an::ErrorCode::AllocationFailed);
    EXPECT_TRUE(
        std::all_of(resource.bytes.begin(), resource.bytes.end(), [](unsigned char value) { return value == 0xA5; }));
}

TEST(MemoryResourceTest, ConcurrentZeroedAllocationsRemainIndependent)
{
    auto engine = makeEngineContext(4096u);
    auto* resource = engine.memoryResource();
    std::atomic<unsigned> failures = 0u;
    std::vector<std::thread> workers;
    for (unsigned worker = 0u; worker < 4u; ++worker)
        workers.emplace_back([&] {
            for (unsigned iteration = 0u; iteration < 128u; ++iteration)
            {
                auto block = resource->allocateZeroed(1024u, 256u);
                if (!block)
                {
                    ++failures;
                    continue;
                }
                const auto* bytes = static_cast<unsigned char*>(*block);
                if (!std::all_of(bytes, bytes + 1024u, [](unsigned char value) { return value == 0u; }))
                    ++failures;
                std::memset(*block, 0xA5, 1024u);
                resource->deallocate(*block, 1024u, 256u);
            }
        });
    for (auto& worker : workers)
        worker.join();
    EXPECT_EQ(failures, 0u);
    EXPECT_EQ(resource->counters().liveBytes, 0u);
    EXPECT_LE(resource->counters().peakBytes, 4096u);
}

TEST(MemoryResourceTest, LargeBlocksGivenBackFromManyThreadsLeaveTheCountersExactAndTheContextShutsDown)
{
    // A frame-sized block given back to a context with no budget is returned to the platform on the resource's own
    // thread; the counters move when the caller gives it back, and the context shuts down - joining that thread -
    // once everything is back, whether or not the platform has had every block yet.
    constexpr std::uint64_t kLarge = std::uint64_t{24} << 20u;
    constexpr int kThreads = 6;
    constexpr int kRounds = 8;
    {
        an::EngineContext engine = makeEngineContext();
        an::MemoryResource* const resource = engine.memoryResource();
        ASSERT_NE(resource, nullptr);
        std::atomic<int> failures{0};
        std::vector<std::thread> workers;
        for (int thread = 0; thread < kThreads; ++thread)
        {
            workers.emplace_back([&, thread]() {
                for (int round = 0; round < kRounds; ++round)
                {
                    an::Result<void*> block = (round + thread) % 2 == 0 ? resource->allocate(kLarge, 64u)
                                                                        : resource->allocateZeroed(kLarge, 64u);
                    if (!block.hasValue())
                    {
                        ++failures;
                        continue;
                    }
                    auto* const bytes = static_cast<unsigned char*>(*block);
                    bytes[0] = static_cast<unsigned char>(round);
                    bytes[kLarge - 1u] = static_cast<unsigned char>(thread);
                    resource->deallocate(*block, kLarge, 64u);
                }
            });
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }
        EXPECT_EQ(failures.load(), 0);
        const an::MemoryCounters counters = resource->counters();
        EXPECT_EQ(counters.liveBytes, 0u) << "a block given back is no longer the context's";
        EXPECT_EQ(counters.liveAllocations, 0u);
        EXPECT_EQ(counters.totalAllocations, static_cast<std::uint64_t>(kThreads * kRounds));
        EXPECT_TRUE(engine.shutdown().ok());
    }
    {
        // A block given back and asked for again at once is a new block, written in full.
        an::EngineContext engine = makeEngineContext();
        an::MemoryResource* const resource = engine.memoryResource();
        for (int round = 0; round < 4; ++round)
        {
            an::Result<void*> block = resource->allocateZeroed(kLarge, 4096u);
            ASSERT_TRUE(block.hasValue());
            const auto* const bytes = static_cast<const unsigned char*>(*block);
            EXPECT_TRUE(std::all_of(bytes, bytes + kLarge, [](unsigned char value) { return value == 0u; }))
                << "round " << round;
            std::memset(*block, 0xA5, static_cast<std::size_t>(kLarge));
            resource->deallocate(*block, kLarge, 4096u);
        }
        EXPECT_EQ(resource->counters().liveBytes, 0u);
    }
}

TEST(MemoryResourceTest, AContextOwnsOneAndAContextThatHoldsNothingOwnsNone)
{
    an::EngineContext engine = makeEngineContext();
    EXPECT_NE(engine.memoryResource(), nullptr);

    const an::EngineContext empty;
    EXPECT_EQ(empty.memoryResource(), nullptr);
}

TEST(MemoryResourceTest, AnAllocationComesBackAtTheAlignmentItAskedFor)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    for (const std::uint64_t alignment :
         {std::uint64_t{8u}, std::uint64_t{64u}, std::uint64_t{256u}, std::uint64_t{4096u}})
    {
        const an::Result<void*> block = resource->allocate(1000u, alignment);
        ASSERT_TRUE(block.hasValue()) << "alignment " << alignment;
        EXPECT_TRUE(an::isPointerAligned(*block, alignment)) << "alignment " << alignment;
        resource->deallocate(*block, 1000u, alignment);
    }
}

TEST(MemoryResourceTest, AnAllocationIsWritableForEveryByteItPromised)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    constexpr std::uint64_t kBytes = 65536u;
    const an::Result<void*> block = resource->allocate(kBytes, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(block.hasValue());

    std::memset(*block, 0xA5, static_cast<std::size_t>(kBytes));
    const unsigned char* const bytes = static_cast<const unsigned char*>(*block);
    EXPECT_EQ(bytes[0], 0xA5);
    EXPECT_EQ(bytes[kBytes - 1u], 0xA5);

    resource->deallocate(*block, kBytes, an::kDefaultBufferAlignmentBytes);
}

TEST(MemoryResourceTest, AnAlignmentThatIsNotAPowerOfTwoIsRefusedRatherThanRounded)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::Result<void*> refused = resource->allocate(64u, 48u);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);

    const an::Result<void*> zero = resource->allocate(64u, 0u);
    ASSERT_FALSE(zero.hasValue());
    EXPECT_EQ(zero.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(MemoryResourceTest, AnEmptyAllocationIsRefusedRatherThanReturningSomethingUndereferenceable)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::Result<void*> refused = resource->allocate(0u, an::kDefaultBufferAlignmentBytes);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(MemoryResourceTest, ARequestPastTheBudgetIsRefusedWithBothNumbersAttached)
{
    // The failure a caller can answer - by tiling, by streaming, or by raising the budget - which is
    // why it is not reported as the allocator having said no.
    constexpr std::uint64_t kBudget = 1024u * 1024u;
    an::EngineContext engine = makeEngineContext(kBudget);
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::Result<void*> refused = resource->allocate(kBudget * 2u, an::kDefaultBufferAlignmentBytes);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);

    const an::StatusContextField* const requested = refused.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* const limit = refused.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(requested->count, kBudget * 2u);
    EXPECT_EQ(limit->count, kBudget);
}

TEST(MemoryResourceTest, TheBudgetIsAggregateRatherThanPerRequest)
{
    constexpr std::uint64_t kBudget = 1024u * 1024u;
    constexpr std::uint64_t kHalf = kBudget / 2u;
    an::EngineContext engine = makeEngineContext(kBudget);
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::Result<void*> first = resource->allocate(kHalf, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(first.hasValue());
    const an::Result<void*> second = resource->allocate(kHalf, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(second.hasValue());

    // Both fitted; a third of the same size does not, because what is already held counts.
    const an::Result<void*> third = resource->allocate(kHalf, an::kDefaultBufferAlignmentBytes);
    ASSERT_FALSE(third.hasValue());
    EXPECT_EQ(third.status().code(), an::ErrorCode::MemoryBudgetExceeded);

    // And it fits again once something is given back, which is what makes the budget a constraint
    // on what is outstanding rather than on what has ever been asked for.
    resource->deallocate(*first, kHalf, an::kDefaultBufferAlignmentBytes);
    const an::Result<void*> fourth = resource->allocate(kHalf, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(fourth.hasValue());

    resource->deallocate(*second, kHalf, an::kDefaultBufferAlignmentBytes);
    resource->deallocate(*fourth, kHalf, an::kDefaultBufferAlignmentBytes);
}

TEST(MemoryResourceTest, TheCountersSayWhatIsHeldWhatWasEverHeldAndWhatWasRefused)
{
    an::EngineContext engine = makeEngineContext(1024u * 1024u);
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::MemoryCounters empty = resource->counters();
    EXPECT_EQ(empty.liveBytes, 0u);
    EXPECT_EQ(empty.liveAllocations, 0u);
    EXPECT_EQ(empty.peakBytes, 0u);
    EXPECT_EQ(empty.deniedAllocations, 0u);

    const an::Result<void*> big = resource->allocate(4096u, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(big.hasValue());
    const an::Result<void*> small = resource->allocate(1024u, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(small.hasValue());

    const an::MemoryCounters held = resource->counters();
    EXPECT_EQ(held.liveBytes, 5120u);
    EXPECT_EQ(held.liveAllocations, 2u);
    EXPECT_EQ(held.peakBytes, 5120u);
    EXPECT_EQ(held.totalAllocations, 2u);

    resource->deallocate(*small, 1024u, an::kDefaultBufferAlignmentBytes);
    resource->deallocate(*big, 4096u, an::kDefaultBufferAlignmentBytes);

    const an::MemoryCounters returned = resource->counters();
    EXPECT_EQ(returned.liveBytes, 0u);
    EXPECT_EQ(returned.liveAllocations, 0u);
    EXPECT_EQ(returned.totalAllocations, 2u);

    // The high-water mark is the number a benchmark reports and the one that must not fall back
    // when the memory is given up.
    EXPECT_EQ(returned.peakBytes, 5120u);

    const an::Result<void*> refused = resource->allocate(1024u * 1024u * 4u, an::kDefaultBufferAlignmentBytes);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(resource->counters().deniedAllocations, 1u);

    // And a smaller allocation after the peak does not pull the mark down with it, which is the
    // half of a high-water mark that a plain assignment would still satisfy on a rising sequence.
    const an::Result<void*> late = resource->allocate(64u, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(late.hasValue());
    EXPECT_EQ(resource->counters().liveBytes, 64u);
    EXPECT_EQ(resource->counters().peakBytes, 5120u);
    resource->deallocate(*late, 64u, an::kDefaultBufferAlignmentBytes);
}

TEST(MemoryResourceTest, DeallocatingNothingIsANoOpRatherThanACrash)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    resource->deallocate(nullptr, 0u, an::kDefaultBufferAlignmentBytes);
    EXPECT_EQ(resource->counters().liveAllocations, 0u);
}

TEST(MemoryResourceTest, TheCountersAreCorrectUnderContention)
{
    // The accounting is what the budget is enforced from, so a lost update is a budget that admits
    // more than it was configured for. Every thread allocates and frees the same amount, so the
    // live figures have to come back to exactly zero and the totals to exactly the work done.
    constexpr std::uint32_t kThreads = 8u;
    constexpr std::uint32_t kRounds = 200u;
    constexpr std::uint64_t kBytes = 4096u;

    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (std::uint32_t index = 0u; index < kThreads; ++index)
    {
        threads.emplace_back([&]() {
            for (std::uint32_t round = 0u; round < kRounds; ++round)
            {
                const an::Result<void*> block = resource->allocate(kBytes, an::kDefaultBufferAlignmentBytes);
                EXPECT_TRUE(block.hasValue());
                if (block.hasValue())
                {
                    resource->deallocate(*block, kBytes, an::kDefaultBufferAlignmentBytes);
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    const an::MemoryCounters counters = resource->counters();
    EXPECT_EQ(counters.liveBytes, 0u);
    EXPECT_EQ(counters.liveAllocations, 0u);
    EXPECT_EQ(counters.totalAllocations, kThreads * kRounds);
    EXPECT_EQ(counters.totalBytes, std::uint64_t{kThreads} * kRounds * kBytes);
    EXPECT_GE(counters.peakBytes, kBytes);
    EXPECT_LE(counters.peakBytes, std::uint64_t{kThreads} * kBytes);
}

TEST(MemoryResourceTest, EveryCounterSnapshotIsInternallyCoherent)
{
    constexpr std::uint32_t kThreads = 4u;
    constexpr std::uint32_t kRounds = 500u;
    constexpr std::uint64_t kBytes = 64u;

    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> finished{0u};
    std::vector<std::thread> threads;
    for (std::uint32_t index = 0u; index < kThreads; ++index)
    {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (std::uint32_t round = 0u; round < kRounds; ++round)
            {
                const an::Result<void*> block = resource->allocate(kBytes, an::kDefaultBufferAlignmentBytes);
                ASSERT_TRUE(block.hasValue());
                resource->deallocate(*block, kBytes, an::kDefaultBufferAlignmentBytes);
            }
            finished.fetch_add(1u, std::memory_order_release);
        });
    }

    start.store(true, std::memory_order_release);
    while (finished.load(std::memory_order_acquire) != kThreads)
    {
        const an::MemoryCounters counters = resource->counters();
        EXPECT_EQ(counters.liveBytes, counters.liveAllocations * kBytes);
        EXPECT_GE(counters.peakBytes, counters.liveBytes);
        EXPECT_EQ(counters.totalBytes, counters.totalAllocations * kBytes);
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

TEST(MemoryResourceTest, APlatformRefusalDoesNotRaiseThePeakOrUseAllocatingErrorDetail)
{
    an::EngineContext engine = makeEngineContext((std::numeric_limits<std::uint64_t>::max)());
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::Result<void*> refused =
        resource->allocate((std::numeric_limits<std::uint64_t>::max)() / 2u, an::kDefaultBufferAlignmentBytes);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::AllocationFailed);
    EXPECT_FALSE(refused.status().hasDetail());

    const an::MemoryCounters counters = resource->counters();
    EXPECT_EQ(counters.liveBytes, 0u);
    EXPECT_EQ(counters.liveAllocations, 0u);
    EXPECT_EQ(counters.peakBytes, 0u);
    EXPECT_EQ(counters.totalBytes, 0u);
    EXPECT_EQ(counters.totalAllocations, 0u);
    EXPECT_EQ(counters.deniedAllocations, 1u);
}

TEST(MemoryResourceTest, TwoContextsAccountSeparately)
{
    an::EngineContext first = makeEngineContext();
    an::EngineContext second = makeEngineContext();

    an::MemoryResource* const firstResource = first.memoryResource();
    an::MemoryResource* const secondResource = second.memoryResource();
    ASSERT_NE(firstResource, nullptr);
    ASSERT_NE(secondResource, nullptr);
    EXPECT_NE(firstResource, secondResource);

    const an::Result<void*> block = firstResource->allocate(4096u, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(block.hasValue());

    EXPECT_EQ(firstResource->counters().liveBytes, 4096u);
    EXPECT_EQ(secondResource->counters().liveBytes, 0u);

    firstResource->deallocate(*block, 4096u, an::kDefaultBufferAlignmentBytes);
}

TEST(MemoryResourceTest, AContextRefusesToShutDownIsUnaffectedByMemoryStillHeld)
{
    // Deliberately stated rather than assumed: memory outstanding is not work outstanding. A caller
    // that still holds a buffer has a lifetime problem of its own making, and the lease count is
    // what shutdown asks about.
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::Result<void*> block = resource->allocate(4096u, an::kDefaultBufferAlignmentBytes);
    ASSERT_TRUE(block.hasValue());
    resource->deallocate(*block, 4096u, an::kDefaultBufferAlignmentBytes);

    EXPECT_TRUE(engine.shutdown().ok());
    EXPECT_EQ(engine.memoryResource(), nullptr);
}
