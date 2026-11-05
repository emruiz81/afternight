// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the tile executor and its worker-private scratch partition.

#include <AfterNightEngine/Runtime/ParallelExecutor.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <limits>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext executorEngine(std::uint32_t concurrency)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext executorExecution(const an::EngineContext& engine, std::uint32_t concurrency,
                                                     std::stop_token stopToken = {})
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

} // namespace

TEST(ParallelExecutorTest, OddImageIsCoveredExactlyOnceByStableRowMajorTiles)
{
    an::EngineContext engine = executorEngine(4u);
    an::ExecutionContext execution = executorExecution(engine, 4u);
    const an::Result<an::ParallelTilePlan> planned = an::planParallelTiles(
        execution, an::ParallelTileRequest{.width = 13u, .height = 7u, .tileWidth = 5u, .tileHeight = 3u});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->columns, 3u);
    EXPECT_EQ(planned->rows, 3u);
    EXPECT_EQ(planned->tileCount, 9u);

    std::vector<std::atomic<std::uint32_t>> visits(13u * 7u);
    const an::Status ran = an::executeParallelTiles(execution, *planned, {}, [&](const an::ParallelTile& tile) {
        EXPECT_EQ(tile.index, static_cast<std::uint64_t>(tile.row) * planned->columns + tile.column);
        EXPECT_LT(tile.xBegin, tile.xEnd);
        EXPECT_LT(tile.yBegin, tile.yEnd);
        for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
        {
            for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
            {
                ++visits[static_cast<std::size_t>(y) * 13u + x];
            }
        }
        return an::Status::success();
    });
    ASSERT_TRUE(ran.ok()) << ran.message();
    EXPECT_TRUE(std::ranges::all_of(visits, [](const auto& count) { return count.load() == 1u; }));
}

TEST(ParallelExecutorTest, WorkerSlotsOwnAlignedDisjointScratchForTheWholeRegion)
{
    an::EngineContext engine = executorEngine(4u);
    an::ExecutionContext execution = executorExecution(engine, 4u);
    const an::Result<an::ParallelTilePlan> planned = an::planParallelTiles(
        execution,
        an::ParallelTileRequest{.width = 64u,
                                .height = 32u,
                                .tileWidth = 4u,
                                .tileHeight = 4u,
                                .pixelCost = {.bytesPerItem = 1024u * 1024u, .operationsPerItem = 1024u * 1024u},
                                .scratchBytesPerWorker = 17u,
                                .scratchAlignment = alignof(std::uint64_t)});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    ASSERT_GE(planned->plannedWorkers, 2u);
    std::vector<std::uint64_t> storage((planned->requiredScratchBytes + sizeof(std::uint64_t) - 1u) /
                                       sizeof(std::uint64_t));
    std::span<std::byte> scratch{reinterpret_cast<std::byte*>(storage.data()), planned->requiredScratchBytes};

    std::mutex guard;
    std::vector<const std::byte*> addresses(planned->plannedWorkers, nullptr);
    const an::Status ran = an::executeParallelTiles(execution, *planned, scratch, [&](const an::ParallelTile& tile) {
        EXPECT_LT(tile.workerSlot, planned->plannedWorkers);
        EXPECT_EQ(tile.scratch.size(), 17u);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(tile.scratch.data()) % alignof(std::uint64_t), 0u);
        const std::lock_guard<std::mutex> held(guard);
        const std::byte*& recorded = addresses[tile.workerSlot];
        if (recorded == nullptr)
        {
            recorded = tile.scratch.data();
        }
        EXPECT_EQ(recorded, tile.scratch.data());
        return an::Status::success();
    });
    ASSERT_TRUE(ran.ok()) << ran.message();
    std::erase(addresses, nullptr);
    std::ranges::sort(addresses);
    EXPECT_EQ(std::ranges::unique(addresses).begin(), addresses.end());
}

TEST(ParallelExecutorTest, InvalidPlanShortScratchAndPreCancellationRunNoBody)
{
    an::EngineContext engine = executorEngine(4u);
    an::ExecutionContext execution = executorExecution(engine, 2u);
    EXPECT_EQ(
        an::planParallelTiles(execution, an::ParallelTileRequest{.width = 8u, .height = 8u, .tileWidth = 0u}).code(),
        an::ErrorCode::InvalidArgument);

    const an::Result<an::ParallelTilePlan> planned = an::planParallelTiles(
        execution, an::ParallelTileRequest{
                       .width = 8u, .height = 8u, .tileWidth = 4u, .tileHeight = 4u, .scratchBytesPerWorker = 32u});
    ASSERT_TRUE(planned.hasValue());
    std::vector<std::byte> shortScratch(planned->requiredScratchBytes - 1u);
    std::atomic<std::uint32_t> calls{0u};
    EXPECT_EQ(an::executeParallelTiles(execution, *planned, shortScratch,
                                       [&](const an::ParallelTile&) {
                                           ++calls;
                                           return an::Status::success();
                                       })
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(calls.load(), 0u);

    EXPECT_EQ(an::planParallelTiles(
                  execution,
                  an::ParallelTileRequest{.width = 2u,
                                          .height = 1u,
                                          .tileWidth = 2u,
                                          .tileHeight = 1u,
                                          .pixelCost = {.bytesPerItem = (std::numeric_limits<std::uint64_t>::max)(),
                                                        .operationsPerItem = 1u}})
                  .code(),
              an::ErrorCode::LimitExceeded);

    an::ParallelTilePlan malformed = *planned;
    malformed.scratchBytesPerWorker = (std::numeric_limits<std::uint64_t>::max)();
    EXPECT_EQ(an::executeParallelTiles(execution, malformed, {},
                                       [&](const an::ParallelTile&) {
                                           ++calls;
                                           return an::Status::success();
                                       })
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(calls.load(), 0u);

    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext cancelled = executorExecution(engine, 2u, stop.get_token());
    const an::Result<an::ParallelTilePlan> cancelledPlan = an::planParallelTiles(
        cancelled, an::ParallelTileRequest{.width = 8u, .height = 8u, .tileWidth = 4u, .tileHeight = 4u});
    ASSERT_TRUE(cancelledPlan.hasValue());
    EXPECT_EQ(an::executeParallelTiles(cancelled, *cancelledPlan, {},
                                       [&](const an::ParallelTile&) {
                                           ++calls;
                                           return an::Status::success();
                                       })
                  .code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(calls.load(), 0u);
}

TEST(ParallelExecutorTest, EmptyGeometryNeedsNoWorkersOrScratch)
{
    an::EngineContext engine = executorEngine(2u);
    an::ExecutionContext execution = executorExecution(engine, 2u);
    const an::Result<an::ParallelTilePlan> planned = an::planParallelTiles(
        execution, an::ParallelTileRequest{.width = 0u, .height = 8u, .scratchBytesPerWorker = 64u});
    ASSERT_TRUE(planned.hasValue()) << planned.status().message();
    EXPECT_EQ(planned->tileCount, 0u);
    EXPECT_EQ(planned->plannedWorkers, 0u);
    EXPECT_EQ(planned->scratchBytesPerWorker, 0u);
    EXPECT_EQ(planned->requiredScratchBytes, 0u);
    std::atomic<std::uint32_t> calls{0u};
    const an::Status ran = an::executeParallelTiles(execution, *planned, {}, [&](const an::ParallelTile&) {
        ++calls;
        return an::Status::success();
    });
    EXPECT_TRUE(ran.ok()) << ran.message();
    EXPECT_EQ(calls.load(), 0u);

    an::ParallelTilePlan malformedEmpty = *planned;
    malformedEmpty.scratchBytesPerWorker = 1u;
    malformedEmpty.scratchStrideBytes = malformedEmpty.scratchAlignment;
    EXPECT_EQ(an::executeParallelTiles(execution, malformedEmpty, {},
                                       [&](const an::ParallelTile&) {
                                           ++calls;
                                           return an::Status::success();
                                       })
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(calls.load(), 0u);
}

TEST(ParallelExecutorTest, NestedRegionsRemainInsideTheOuterOperationArena)
{
    an::EngineContext engine = executorEngine(2u);
    an::ExecutionContext execution = executorExecution(engine, 2u);
    const an::Result<an::ParallelTilePlan> outer = an::planParallelTiles(
        execution,
        an::ParallelTileRequest{.width = 4u,
                                .height = 1u,
                                .tileWidth = 1u,
                                .tileHeight = 1u,
                                .pixelCost = {.bytesPerItem = 1024u * 1024u, .operationsPerItem = 1024u * 1024u}});
    const an::Result<an::ParallelTilePlan> inner = an::planParallelTiles(
        execution,
        an::ParallelTileRequest{.width = 4u,
                                .height = 1u,
                                .tileWidth = 1u,
                                .tileHeight = 1u,
                                .pixelCost = {.bytesPerItem = 1024u * 1024u, .operationsPerItem = 1024u * 1024u}});
    ASSERT_TRUE(outer.hasValue());
    ASSERT_TRUE(inner.hasValue());
    std::atomic<std::uint32_t> running{0u};
    std::atomic<std::uint32_t> peak{0u};
    const an::Status ran = an::executeParallelTiles(execution, *outer, {}, [&](const an::ParallelTile&) {
        return an::executeParallelTiles(execution, *inner, {}, [&](const an::ParallelTile&) {
            const std::uint32_t now = running.fetch_add(1u) + 1u;
            std::uint32_t observed = peak.load();
            while (now > observed && !peak.compare_exchange_weak(observed, now))
            {
            }
            running.fetch_sub(1u);
            return an::Status::success();
        });
    });
    ASSERT_TRUE(ran.ok()) << ran.message();
    EXPECT_LE(peak.load(), 2u);
}
