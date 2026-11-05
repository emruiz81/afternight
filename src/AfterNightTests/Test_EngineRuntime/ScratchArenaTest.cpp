// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Runtime/ScratchArena.h.
//
// Scratch is the memory an operation needs while it runs and nobody needs afterwards. Allocated per
// call, per tile, per frame, from the general allocator, it makes a stacking run spend measurable
// time in the allocator doing nothing a caller asked for.
//
// The rules an arena is built to make true are all about *when*, not about how fast:
//
//  - a block, once taken, is kept and reused for the life of the arena, so a loop that needs the
//    same scratch on every tile takes it from the allocator once;
//  - everything is released when the arena is destroyed, however the execution ended - which is
//    what makes "an application holding open images and running nothing holds no scratch" a
//    property of the design rather than a promise; and
//  - what is held counts against the configured budget while it is held, idle or not, because a
//    budget that ignored pooled memory would be a budget that measured the wrong thing.
//
// The counters exist for the same reason the resource's do: a peak scratch figure is what a
// benchmark reports, and an arena that reused nothing would look identical without them.

#include <AfterNightEngine/Runtime/ScratchArena.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <utility>

namespace
{

/// An engine context with a stated memory budget.
///
/// @param budgetBytes what the context may have outstanding at once.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint64_t budgetBytes = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    options.limits = an::defaultEngineLimits();
    options.limits.memoryBudgetBytes = budgetBytes;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// An arena over a context's memory resource.
///
/// @param engine the context whose resource backs it.
/// @param blockBytes how much the arena takes at a time.
/// @return the arena, which is valid or the case has already failed.
[[nodiscard]] an::ScratchArena makeArena(const an::EngineContext& engine,
                                         std::uint64_t blockBytes = an::kDefaultScratchBlockBytes)
{
    an::Result<an::ScratchArena> created = an::ScratchArena::create(engine, blockBytes);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::ScratchArena{};
}

} // namespace

TEST(ScratchArenaTest, ADefaultArenaHoldsNothingAndAllocatesNothing)
{
    an::ScratchArena empty;
    EXPECT_FALSE(empty.valid());

    const an::Result<void*> refused = empty.allocate(64u);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::NotInitialized);
}

TEST(ScratchArenaTest, AnAllocationComesBackAtTheAlignmentItAskedFor)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.valid());

    for (const std::uint64_t alignment : {std::uint64_t{8u}, std::uint64_t{64u}, std::uint64_t{512u}})
    {
        const an::Result<void*> block = arena.allocate(300u, alignment);
        ASSERT_TRUE(block.hasValue()) << "alignment " << alignment;
        EXPECT_TRUE(an::isPointerAligned(*block, alignment)) << "alignment " << alignment;
        std::memset(*block, 0, 300u);
    }
}

TEST(ScratchArenaTest, TwoAllocationsDoNotOverlap)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.valid());

    const an::Result<void*> first = arena.allocate(1000u, 64u);
    const an::Result<void*> second = arena.allocate(1000u, 64u);
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());

    std::memset(*first, 0x11, 1000u);
    std::memset(*second, 0x22, 1000u);
    EXPECT_EQ(*static_cast<const unsigned char*>(*first), 0x11);
    EXPECT_EQ(*(static_cast<const unsigned char*>(*first) + 999u), 0x11);
}

TEST(ScratchArenaTest, AnAlignmentThatIsNotAPowerOfTwoIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.valid());

    const an::Result<void*> refused = arena.allocate(64u, 24u);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(ScratchArenaTest, AnEmptyRequestIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.valid());

    const an::Result<void*> refused = arena.allocate(0u);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(ScratchArenaTest, ManySmallRequestsComeOutOfOneBlock)
{
    // The whole reason an arena exists rather than a call to the allocator per tile.
    constexpr std::uint64_t kBlock = 64u * 1024u;
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine, kBlock);
    ASSERT_TRUE(arena.valid());

    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t before = resource->counters().totalAllocations;

    for (std::uint32_t index = 0u; index < 512u; ++index)
    {
        const an::Result<void*> block = arena.allocate(64u, 64u);
        ASSERT_TRUE(block.hasValue());
    }

    EXPECT_EQ(resource->counters().totalAllocations - before, 1u);
    EXPECT_EQ(arena.counters().totalAllocations, 512u);
}

TEST(ScratchArenaTest, ARequestLargerThanABlockStillSucceeds)
{
    constexpr std::uint64_t kBlock = 4096u;
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine, kBlock);
    ASSERT_TRUE(arena.valid());

    const an::Result<void*> block = arena.allocate(kBlock * 10u, 64u);
    ASSERT_TRUE(block.hasValue());
    std::memset(*block, 0x5A, static_cast<std::size_t>(kBlock * 10u));
    EXPECT_TRUE(an::isPointerAligned(*block, 64u));
}

TEST(ScratchArenaTest, ResettingReusesTheMemoryRatherThanReturningIt)
{
    constexpr std::uint64_t kBlock = 64u * 1024u;
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine, kBlock);
    ASSERT_TRUE(arena.valid());

    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::Result<void*> first = arena.allocate(1024u, 64u);
    ASSERT_TRUE(first.hasValue());
    const std::uint64_t afterFirst = resource->counters().totalAllocations;
    const std::uint64_t heldBytes = resource->counters().liveBytes;

    arena.reset();
    EXPECT_EQ(arena.counters().liveBytes, 0u);

    // Pooled, not returned: the resource still holds exactly what it held, which is what makes the
    // second round free and what makes the memory still count against the budget.
    EXPECT_EQ(resource->counters().liveBytes, heldBytes);

    const an::Result<void*> second = arena.allocate(1024u, 64u);
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(*second, *first);
    EXPECT_EQ(resource->counters().totalAllocations, afterFirst);
}

TEST(ScratchArenaTest, TheHighWaterMarkSurvivesAReset)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.valid());

    ASSERT_TRUE(arena.allocate(4096u, 64u).hasValue());
    ASSERT_TRUE(arena.allocate(4096u, 64u).hasValue());
    const std::uint64_t peak = arena.counters().peakBytes;
    EXPECT_GE(peak, 8192u);

    arena.reset();
    EXPECT_EQ(arena.counters().liveBytes, 0u);
    EXPECT_EQ(arena.counters().peakBytes, peak);
}

TEST(ScratchArenaTest, EverythingGoesBackWhenTheArenaDoes)
{
    // The rule from the memory design: scratch is released in full when the execution that owns it
    // ends, however it ends. The arena's destructor is what "however it ends" means.
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    ASSERT_EQ(resource->counters().liveBytes, 0u);

    {
        an::ScratchArena arena = makeArena(engine);
        ASSERT_TRUE(arena.valid());
        ASSERT_TRUE(arena.allocate(100000u, 64u).hasValue());
        EXPECT_GT(resource->counters().liveBytes, 0u);
    }

    EXPECT_EQ(resource->counters().liveBytes, 0u);
    EXPECT_EQ(resource->counters().liveAllocations, 0u);
}

TEST(ScratchArenaTest, ReleasingByHandGivesEverythingBackAndLeavesTheArenaUsable)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.valid());
    ASSERT_TRUE(arena.allocate(100000u, 64u).hasValue());

    arena.releaseBlocks();
    EXPECT_EQ(resource->counters().liveBytes, 0u);
    EXPECT_EQ(arena.counters().liveBytes, 0u);
    EXPECT_TRUE(arena.valid());

    ASSERT_TRUE(arena.allocate(64u, 64u).hasValue());
    EXPECT_GT(resource->counters().liveBytes, 0u);
}

TEST(ScratchArenaTest, AMarkerRewindsToWhereItWasTaken)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.valid());

    const an::Result<void*> kept = arena.allocate(1024u, 64u);
    ASSERT_TRUE(kept.hasValue());

    const an::ScratchMarker marker = arena.mark();
    const an::Result<void*> temporary = arena.allocate(1024u, 64u);
    ASSERT_TRUE(temporary.hasValue());
    const std::uint64_t deep = arena.counters().liveBytes;

    arena.rewind(marker);
    EXPECT_LT(arena.counters().liveBytes, deep);

    // The same address comes back, which is what makes a nested stage's scratch free after the
    // first pass through it.
    const an::Result<void*> again = arena.allocate(1024u, 64u);
    ASSERT_TRUE(again.hasValue());
    EXPECT_EQ(*again, *temporary);
}

TEST(ScratchArenaTest, ARewindAcrossSeveralBlocksKeepsThemForReuse)
{
    constexpr std::uint64_t kBlock = 4096u;
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine, kBlock);
    ASSERT_TRUE(arena.valid());

    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::ScratchMarker marker = arena.mark();
    for (std::uint32_t index = 0u; index < 16u; ++index)
    {
        ASSERT_TRUE(arena.allocate(kBlock / 2u, 64u).hasValue());
    }
    const std::uint64_t blocksTaken = resource->counters().totalAllocations;
    ASSERT_GT(blocksTaken, 1u);

    arena.rewind(marker);
    EXPECT_EQ(arena.counters().liveBytes, 0u);

    for (std::uint32_t index = 0u; index < 16u; ++index)
    {
        ASSERT_TRUE(arena.allocate(kBlock / 2u, 64u).hasValue());
    }
    EXPECT_EQ(resource->counters().totalAllocations, blocksTaken);
}

TEST(ScratchArenaTest, AStaleMarkerRewindsToTheBeginning)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.allocate(512u, 64u).hasValue());
    const an::ScratchMarker stale = arena.mark();
    ASSERT_TRUE(arena.allocate(512u, 64u).hasValue());

    arena.reset();
    ASSERT_TRUE(arena.allocate(256u, 64u).hasValue());
    arena.rewind(stale);

    EXPECT_EQ(arena.counters().liveBytes, 0u);
    EXPECT_EQ(arena.counters().liveAllocations, 0u);
}

TEST(ScratchArenaTest, AMarkerFromAnotherArenaRewindsToTheBeginning)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena first = makeArena(engine);
    an::ScratchArena second = makeArena(engine);

    ASSERT_TRUE(first.allocate(512u, 64u).hasValue());
    const an::ScratchMarker foreign = first.mark();
    ASSERT_TRUE(second.allocate(1024u, 64u).hasValue());

    second.rewind(foreign);
    EXPECT_EQ(second.counters().liveBytes, 0u);
    EXPECT_EQ(second.counters().liveAllocations, 0u);
}

TEST(ScratchArenaTest, ATamperedMarkerRewindsToTheBeginning)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.allocate(512u, 64u).hasValue());
    an::ScratchMarker marker = arena.mark();
    ASSERT_TRUE(arena.allocate(512u, 64u).hasValue());
    marker.liveBytes = 1u;

    arena.rewind(marker);
    EXPECT_EQ(arena.counters().liveBytes, 0u);
    EXPECT_EQ(arena.counters().liveAllocations, 0u);
}

TEST(ScratchArenaTest, ADeniedBlockIsReportedRatherThanReturningNothingUsable)
{
    // Denied at the arena's own allocation boundary, which is one of the boundaries the adversarial
    // test list names. The budget is what makes it reachable without exhausting the machine.
    constexpr std::uint64_t kBudget = 128u * 1024u;
    an::EngineContext engine = makeEngineContext(kBudget);
    an::ScratchArena arena = makeArena(engine, 64u * 1024u);
    ASSERT_TRUE(arena.valid());

    ASSERT_TRUE(arena.allocate(1024u, 64u).hasValue());

    const an::Result<void*> refused = arena.allocate(kBudget * 4u, 64u);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(arena.counters().deniedAllocations, 1u);

    // And the arena is still usable afterwards: a refusal is an answer, not a broken object.
    EXPECT_TRUE(arena.allocate(1024u, 64u).hasValue());
}

TEST(ScratchArenaTest, MovingAnArenaMovesWhatItHolds)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    an::ScratchArena original = makeArena(engine);
    ASSERT_TRUE(original.valid());
    ASSERT_TRUE(original.allocate(4096u, 64u).hasValue());
    const std::uint64_t held = resource->counters().liveBytes;

    an::ScratchArena moved{std::move(original)};
    EXPECT_TRUE(moved.valid());
    EXPECT_FALSE(original.valid());
    EXPECT_EQ(resource->counters().liveBytes, held);
    EXPECT_EQ(moved.counters().liveBytes, 4096u);
    EXPECT_EQ(original.counters().liveBytes, 0u);
}

TEST(ScratchArenaTest, AnExecutionContextCarriesTheArenaItWasGiven)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);
    ASSERT_TRUE(arena.valid());

    an::ExecutionOptions options;
    options.scratch = &arena;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    ASSERT_EQ(execution->scratch(), &arena);

    // And an algorithm reaches its persistent allocator through the same object rather than
    // through a second parameter or a global.
    EXPECT_EQ(execution->memoryResource(), engine.memoryResource());

    const an::Result<void*> block = execution->scratch()->allocate(256u);
    ASSERT_TRUE(block.hasValue());
}

TEST(ScratchArenaTest, AnExecutionContextWithNoScratchSaysSoRatherThanInventingOne)
{
    an::EngineContext engine = makeEngineContext();

    const an::ExecutionOptions options;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    EXPECT_EQ(execution->scratch(), nullptr);

    const an::ExecutionContext empty;
    EXPECT_EQ(empty.memoryResource(), nullptr);
}

TEST(ScratchArenaTest, AContextCannotShutDownWhileItsScratchArenaCanStillAllocate)
{
    an::EngineContext engine = makeEngineContext();
    an::ScratchArena arena = makeArena(engine);

    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
    EXPECT_TRUE(engine.valid());

    arena = an::ScratchArena{};
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(ScratchArenaTest, AnExecutionRejectsScratchOwnedByAnotherEngineContext)
{
    an::EngineContext first = makeEngineContext();
    an::EngineContext second = makeEngineContext();
    an::ScratchArena arena = makeArena(first);

    an::ExecutionOptions options;
    options.scratch = &arena;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(second, options);

    ASSERT_FALSE(execution.hasValue());
    EXPECT_EQ(execution.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(second.quiescent());
}
