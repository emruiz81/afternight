// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The overlap of a stage's frames: the read-ahead hands items over in order from one producer or several and never
/// runs further ahead than it was allowed, the write-behind consumes everything it is handed within its capacity, and
/// the run's width is divided so that the overlapped operations together never use more threads than the run may.

#include "processes/ImageIntegration/workflow/stages/StageFramePipeline.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

using stage_utils::pipelineWidths;
using stage_utils::ReadAhead;
using stage_utils::WriteBehind;

/// A short, index-dependent pause, so that producers finish out of order.
void pauseFor(std::size_t index)
{
    std::this_thread::sleep_for(std::chrono::microseconds(((index * 7919u) % 5u) * 300u));
}

TEST(StageFramePipelineTest, OneProducerHandsItemsOverInOrderAndReadsOneAhead)
{
    std::atomic<std::size_t> taken{0u};
    std::atomic<std::size_t> furthestAhead{0u};
    ReadAhead<std::size_t> reader(40u, 1u, [&](std::size_t index, std::stop_token) {
        const std::size_t ahead = index - taken.load();
        std::size_t seen = furthestAhead.load();
        while (ahead > seen && !furthestAhead.compare_exchange_weak(seen, ahead))
        {
        }
        pauseFor(index);
        return index * 10u + 1u;
    });
    for (std::size_t index = 0u; index < 40u; ++index)
    {
        EXPECT_EQ(reader.take(), index * 10u + 1u);
        taken.store(index + 1u);
    }
    EXPECT_EQ(reader.take(), 0u) << "a take past the list is a default item";
    EXPECT_LE(furthestAhead.load(), 1u);
}

TEST(StageFramePipelineTest, SeveralProducersHandItemsOverInOrderWithinTheirLookahead)
{
    constexpr std::size_t kProducers = 3u;
    constexpr std::size_t kDepth = 2u;
    std::atomic<std::size_t> taken{0u};
    std::atomic<std::size_t> running{0u};
    std::atomic<std::size_t> mostRunning{0u};
    std::atomic<std::size_t> furthestAhead{0u};
    std::mutex claimed;
    std::set<std::size_t> produced;
    ReadAhead<std::size_t> reader(60u, kDepth, kProducers, [&](std::size_t index, std::stop_token) {
        {
            const std::lock_guard lock(claimed);
            EXPECT_TRUE(produced.insert(index).second) << "index " << index << " produced twice";
        }
        const std::size_t now = ++running;
        std::size_t most = mostRunning.load();
        while (now > most && !mostRunning.compare_exchange_weak(most, now))
        {
        }
        const std::size_t ahead = index - taken.load();
        std::size_t seen = furthestAhead.load();
        while (ahead > seen && !furthestAhead.compare_exchange_weak(seen, ahead))
        {
        }
        pauseFor(index);
        --running;
        return index + 100u;
    });
    for (std::size_t index = 0u; index < 60u; ++index)
    {
        EXPECT_EQ(reader.take(), index + 100u);
        taken.store(index + 1u);
    }
    EXPECT_EQ(produced.size(), 60u);
    EXPECT_LE(mostRunning.load(), kProducers);
    EXPECT_GT(mostRunning.load(), 1u) << "the producers never overlapped";
    EXPECT_LE(furthestAhead.load(), kDepth + kProducers - 1u);
}

TEST(StageFramePipelineTest, ConsumersTakingByIndexEachGetTheirOwnItem)
{
    // A stage that consumes on several threads - each item claimed by the index it knows - receives the item of that
    // index, whichever thread asks first.
    ReadAhead<std::size_t> reader(30u, 1u, [](std::size_t index, std::stop_token) {
        pauseFor(index);
        return index * 3u;
    });
    std::vector<std::size_t> received(30u, 0u);
    std::atomic<std::size_t> next{0u};
    std::vector<std::thread> consumers;
    for (int consumer = 0; consumer < 3; ++consumer)
    {
        consumers.emplace_back([&] {
            for (;;)
            {
                const std::size_t index = next++;
                if (index >= 30u)
                {
                    return;
                }
                received[index] = reader.take(index);
            }
        });
    }
    for (std::thread& consumer : consumers)
    {
        consumer.join();
    }
    for (std::size_t index = 0u; index < 30u; ++index)
    {
        EXPECT_EQ(received[index], index * 3u) << index;
    }
}

TEST(StageFramePipelineTest, WhatAProducerThrowsReachesTheTakeOfItsItem)
{
    ReadAhead<int> reader(10u, 1u, [](std::size_t index, std::stop_token) {
        if (index == 3u)
        {
            throw std::runtime_error("the fourth item cannot be read");
        }
        return static_cast<int>(index);
    });
    EXPECT_EQ(reader.take(), 0);
    EXPECT_EQ(reader.take(), 1);
    EXPECT_EQ(reader.take(), 2);
    EXPECT_THROW((void)reader.take(), std::runtime_error);
}

TEST(StageFramePipelineTest, DestroyingAReadAheadEarlyFinishesTheItemInHandAndStartsNoOther)
{
    std::atomic<std::size_t> started{0u};
    std::atomic<std::size_t> finished{0u};
    {
        ReadAhead<std::size_t> reader(100u, 1u, 2u, [&](std::size_t index, std::stop_token) {
            ++started;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++finished;
            return index;
        });
        EXPECT_EQ(reader.take(), 0u);
        EXPECT_EQ(reader.take(), 1u);
    }
    EXPECT_EQ(started.load(), finished.load());
    EXPECT_LE(started.load(), 2u + 2u);
}

TEST(StageFramePipelineTest, TheWriteBehindConsumesEverythingItIsHandedWithinItsCapacity)
{
    constexpr std::size_t kWriters = 2u;
    constexpr std::size_t kCapacity = 3u;
    std::atomic<std::size_t> held{0u};
    std::atomic<std::size_t> mostHeld{0u};
    std::atomic<std::size_t> writing{0u};
    std::atomic<std::size_t> mostWriting{0u};
    std::vector<int> written(25u, 0);
    {
        WriteBehind<std::size_t> writer(kWriters, kCapacity, [&](std::size_t&& index) {
            const std::size_t now = ++writing;
            std::size_t most = mostWriting.load();
            while (now > most && !mostWriting.compare_exchange_weak(most, now))
            {
            }
            pauseFor(index);
            ++written[index];
            --writing;
            --held;
        });
        for (std::size_t index = 0u; index < 25u; ++index)
        {
            // Counted before it is handed over, so the writer's count down always follows it.
            const std::size_t now = ++held;
            std::size_t most = mostHeld.load();
            while (now > most && !mostHeld.compare_exchange_weak(most, now))
            {
            }
            writer.submit(std::size_t{index});
            if (index == 12u)
            {
                writer.drain();
                EXPECT_EQ(held.load(), 0u) << "drain returned with writes outstanding";
            }
        }
        // Destroyed without a drain: it consumes what it holds before its writers stop.
    }
    EXPECT_EQ(std::count(written.begin(), written.end(), 1), 25);
    EXPECT_LE(mostWriting.load(), kWriters);
    // One more than the capacity: the item counted while `submit` waits for room.
    EXPECT_LE(mostHeld.load(), kCapacity + 1u);
}

TEST(StageFramePipelineTest, TheRunsWidthIsDividedWithoutExceedingIt)
{
    for (const std::uint32_t narrow : {0u, 1u, 2u, 3u})
    {
        const stage_utils::PipelineWidths widths = pipelineWidths(narrow, true);
        EXPECT_FALSE(widths.overlapped) << narrow;
        EXPECT_EQ(widths.compute, (std::max)(1u, narrow));
    }
    for (const std::uint32_t width : {4u, 8u, 12u, 16u, 32u, 64u})
    {
        for (const bool writes : {false, true})
        {
            const stage_utils::PipelineWidths widths = pipelineWidths(width, writes);
            ASSERT_TRUE(widths.overlapped) << width;
            EXPECT_GE(widths.reader, 1u);
            EXPECT_LE(widths.reader, 4u);
            EXPECT_GE(widths.compute, 1u);
            EXPECT_EQ(widths.writers, writes ? (width >= 16u ? 2u : 1u) : 0u);
            EXPECT_EQ(widths.reader + widths.compute + widths.writers, width) << width << (writes ? " writing" : "");
        }
    }
    const stage_utils::PipelineWidths full = pipelineWidths(32u, true);
    EXPECT_EQ(full.reader, 4u);
    EXPECT_EQ(full.writers, 2u);
    EXPECT_EQ(full.compute, 26u);
}

} // namespace
