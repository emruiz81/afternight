// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The consumer memory categories: allowances that start at the proposed session defaults, admission before
/// allocation, eviction before refusal, a required buffer admitted with its overrun counted, the snapshot rules - one
/// oversized snapshot fits alone, at most two wait - and a total across every category.

#include "ConsumerRuntimeTestSupport.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace
{

using an::consumer::CategoryAllowance;
using an::consumer::ConsumerMemory;
using an::consumer::kGibibyte;
using an::consumer::kMebibyte;
using an::consumer::kUnlimitedAllowance;
using an::consumer::MemoryAllowances;
using an::consumer::MemoryCategory;
using an::consumer::MemoryReservation;
using an::consumer::ReservationKind;

[[nodiscard]] MemoryAllowances cachesOf(std::uint64_t bytes)
{
    MemoryAllowances allowances;
    allowances.derivedCaches = CategoryAllowance{.bytes = bytes};
    return allowances;
}

} // namespace

TEST(ConsumerMemoryTest, TheDefaultsAreTheProposedSessionDefaults)
{
    const MemoryAllowances defaults;
    EXPECT_EQ(defaults.historyCopies.bytes, kUnlimitedAllowance)
        << "undo copies are bounded by the history's depth, not by bytes";
    EXPECT_EQ(defaults.pendingSnapshots.bytes, kGibibyte);
    EXPECT_EQ(defaults.pendingSnapshots.maxEntries, 2u);
    EXPECT_TRUE(defaults.pendingSnapshots.admitsOneOversized);
    EXPECT_EQ(defaults.derivedCaches.bytes, kGibibyte);
    EXPECT_EQ(defaults.displayBuffers.bytes, 512u * kMebibyte);
    EXPECT_EQ(defaults.totalBytes, kUnlimitedAllowance);
    EXPECT_TRUE(an::consumer::checkMemoryAllowances(defaults).ok());

    // A session starts with them.
    std::optional<an::consumer::EngineSession> session = an::tests::consumer::openSession(1u);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->memory().allowances().displayBuffers.bytes, 512u * kMebibyte);
    EXPECT_EQ(session->memory().totalReservedBytes(), 0u);
}

TEST(ConsumerMemoryTest, ARequestWithinItsAllowanceIsAdmittedAndCounted)
{
    ConsumerMemory memory(cachesOf(1000u));
    an::Result<MemoryReservation> first = memory.reserve(MemoryCategory::DerivedCaches, 600u);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_EQ(first->bytes(), 600u);
    EXPECT_EQ(first->category(), MemoryCategory::DerivedCaches);
    an::Result<MemoryReservation> second = memory.reserve(MemoryCategory::DerivedCaches, 400u);
    ASSERT_TRUE(second.hasValue());

    const an::consumer::MemoryCategoryCounters counters = memory.counters(MemoryCategory::DerivedCaches);
    EXPECT_EQ(counters.reservedBytes, 1000u);
    EXPECT_EQ(counters.entries, 2u);
    EXPECT_EQ(counters.admitted, 2u);
    EXPECT_EQ(counters.peakBytes, 1000u);
    EXPECT_EQ(memory.totalReservedBytes(), 1000u);
    EXPECT_EQ(memory.counters(MemoryCategory::DisplayBuffers).reservedBytes, 0u) << "categories are separate";
}

TEST(ConsumerMemoryTest, ARequestBeyondItsAllowanceIsRefusedWithTheNumbers)
{
    ConsumerMemory memory(cachesOf(1000u));
    an::Result<MemoryReservation> held = memory.reserve(MemoryCategory::DerivedCaches, 700u);
    ASSERT_TRUE(held.hasValue());
    const an::Result<MemoryReservation> refused = memory.reserve(MemoryCategory::DerivedCaches, 400u);
    ASSERT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded);
    ASSERT_NE(refused.status().findContext(an::ContextKey::Requested), nullptr);
    EXPECT_EQ(refused.status().findContext(an::ContextKey::Requested)->count, 400u);
    EXPECT_EQ(refused.status().findContext(an::ContextKey::Available)->count, 300u);
    EXPECT_EQ(refused.status().findContext(an::ContextKey::Limit)->count, 1000u);
    EXPECT_EQ(refused.status().findContext(an::ContextKey::Parameter)->text, "derived caches");
    EXPECT_EQ(memory.counters(MemoryCategory::DerivedCaches).refused, 1u);
    EXPECT_EQ(memory.counters(MemoryCategory::DerivedCaches).reservedBytes, 700u);

    EXPECT_EQ(memory.reserve(MemoryCategory::DerivedCaches, 0u).code(), an::ErrorCode::InvalidArgument);
}

// The category's owner is asked for the shortfall before anything is refused, and the request is admitted against
// what eviction left.
TEST(ConsumerMemoryTest, EvictionPrecedesRefusal)
{
    ConsumerMemory memory(cachesOf(1000u));
    std::vector<MemoryReservation> cache;
    for (int entry = 0; entry < 4; ++entry)
    {
        an::Result<MemoryReservation> held = memory.reserve(MemoryCategory::DerivedCaches, 250u);
        ASSERT_TRUE(held.hasValue());
        cache.push_back(std::move(*held));
    }
    std::uint64_t askedFor = 0u;
    an::Result<an::consumer::EvictorRegistration> registration =
        memory.registerEvictor(MemoryCategory::DerivedCaches, [&cache, &askedFor](std::uint64_t shortfall) {
            askedFor = shortfall;
            std::uint64_t released = 0u;
            while (released < shortfall && !cache.empty())
            {
                released += cache.front().bytes();
                cache.erase(cache.begin());
            }
            return released;
        });
    ASSERT_TRUE(registration.hasValue());

    an::Result<MemoryReservation> fresh = memory.reserve(MemoryCategory::DerivedCaches, 400u);
    ASSERT_TRUE(fresh.hasValue()) << fresh.status().message();
    EXPECT_EQ(askedFor, 400u);
    EXPECT_EQ(cache.size(), 2u);
    const an::consumer::MemoryCategoryCounters counters = memory.counters(MemoryCategory::DerivedCaches);
    EXPECT_EQ(counters.evictionRequests, 1u);
    EXPECT_EQ(counters.evictedBytes, 500u);
    EXPECT_EQ(counters.reservedBytes, 900u);
    EXPECT_EQ(counters.refused, 0u);
}

TEST(ConsumerMemoryTest, WhatEvictionCannotFreeIsStillRefused)
{
    ConsumerMemory memory(cachesOf(1000u));
    an::Result<MemoryReservation> pinned = memory.reserve(MemoryCategory::DerivedCaches, 900u);
    ASSERT_TRUE(pinned.hasValue());
    int calls = 0;
    an::Result<an::consumer::EvictorRegistration> registration =
        memory.registerEvictor(MemoryCategory::DerivedCaches, [&calls](std::uint64_t) -> std::uint64_t {
            ++calls;
            return 0u;
        });
    ASSERT_TRUE(registration.hasValue());
    EXPECT_EQ(memory.reserve(MemoryCategory::DerivedCaches, 200u).code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(memory.counters(MemoryCategory::DerivedCaches).refused, 1u);
}

TEST(ConsumerMemoryTest, ARemovedEvictorIsNotCalled)
{
    ConsumerMemory memory(cachesOf(100u));
    an::Result<MemoryReservation> held = memory.reserve(MemoryCategory::DerivedCaches, 100u);
    ASSERT_TRUE(held.hasValue());
    int calls = 0;
    {
        an::Result<an::consumer::EvictorRegistration> registration =
            memory.registerEvictor(MemoryCategory::DerivedCaches, [&calls](std::uint64_t) -> std::uint64_t {
                ++calls;
                return 0u;
            });
        ASSERT_TRUE(registration.hasValue());
    }
    EXPECT_EQ(memory.reserve(MemoryCategory::DerivedCaches, 1u).code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(memory.registerEvictor(MemoryCategory::DerivedCaches, {}).code(), an::ErrorCode::InvalidArgument);
}

// The buffer a visible view paints now cannot be refused without blanking the view: it is admitted, and what exceeds
// the allowance is counted where a person can see it.
TEST(ConsumerMemoryTest, ARequiredBufferIsAdmittedAndItsOverrunCounted)
{
    MemoryAllowances allowances;
    allowances.displayBuffers = CategoryAllowance{.bytes = 1000u};
    ConsumerMemory memory(allowances);
    an::Result<MemoryReservation> hidden = memory.reserve(MemoryCategory::DisplayBuffers, 800u);
    ASSERT_TRUE(hidden.hasValue());
    EXPECT_EQ(memory.reserve(MemoryCategory::DisplayBuffers, 500u).code(), an::ErrorCode::MemoryBudgetExceeded);

    an::Result<MemoryReservation> visible =
        memory.reserve(MemoryCategory::DisplayBuffers, 500u, ReservationKind::Required);
    ASSERT_TRUE(visible.hasValue());
    an::consumer::MemoryCategoryCounters counters = memory.counters(MemoryCategory::DisplayBuffers);
    EXPECT_EQ(counters.reservedBytes, 1300u);
    EXPECT_EQ(counters.overrunBytes, 300u);
    EXPECT_EQ(counters.overrunAdmissions, 1u);

    visible->release();
    counters = memory.counters(MemoryCategory::DisplayBuffers);
    EXPECT_EQ(counters.reservedBytes, 800u);
    EXPECT_EQ(counters.overrunBytes, 0u);
    EXPECT_EQ(counters.overrunAdmissions, 1u) << "the count of overruns is a history, not a level";
}

// One snapshot must always fit, or every edit of an image larger than the allowance would fail; a second one waits
// for the history writer to drain the first.
TEST(ConsumerMemoryTest, OneSnapshotLargerThanTheAllowanceFitsAlone)
{
    MemoryAllowances allowances;
    allowances.pendingSnapshots.bytes = 1000u;
    ConsumerMemory memory(allowances);
    an::Result<MemoryReservation> large = memory.reserve(MemoryCategory::PendingSnapshots, 5000u);
    ASSERT_TRUE(large.hasValue()) << large.status().message();
    EXPECT_EQ(memory.counters(MemoryCategory::PendingSnapshots).overrunBytes, 0u)
        << "an oversized snapshot admitted alone is within its rule, not an overrun";
    EXPECT_EQ(memory.reserve(MemoryCategory::PendingSnapshots, 10u).code(), an::ErrorCode::MemoryBudgetExceeded);
    large->release();
    EXPECT_TRUE(memory.reserve(MemoryCategory::PendingSnapshots, 10u).hasValue());
}

TEST(ConsumerMemoryTest, AtMostTwoSnapshotsArePending)
{
    ConsumerMemory memory;
    an::Result<MemoryReservation> first = memory.reserve(MemoryCategory::PendingSnapshots, kMebibyte);
    an::Result<MemoryReservation> second = memory.reserve(MemoryCategory::PendingSnapshots, kMebibyte);
    ASSERT_TRUE(first.hasValue() && second.hasValue());
    const an::Result<MemoryReservation> third = memory.reserve(MemoryCategory::PendingSnapshots, kMebibyte);
    ASSERT_EQ(third.code(), an::ErrorCode::LimitExceeded);
    ASSERT_NE(third.status().findContext(an::ContextKey::Limit), nullptr);
    EXPECT_EQ(third.status().findContext(an::ContextKey::Limit)->count, 2u);
    first->release();
    EXPECT_TRUE(memory.reserve(MemoryCategory::PendingSnapshots, kMebibyte).hasValue());
}

TEST(ConsumerMemoryTest, TheTotalBoundsEveryCategoryTogether)
{
    MemoryAllowances allowances;
    allowances.totalBytes = 1000u;
    ConsumerMemory memory(allowances);
    an::Result<MemoryReservation> caches = memory.reserve(MemoryCategory::DerivedCaches, 600u);
    ASSERT_TRUE(caches.hasValue());
    const an::Result<MemoryReservation> display = memory.reserve(MemoryCategory::DisplayBuffers, 500u);
    ASSERT_EQ(display.code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(display.status().findContext(an::ContextKey::Limit)->count, 1000u);
    EXPECT_EQ(display.status().findContext(an::ContextKey::Available)->count, 400u);
    EXPECT_TRUE(memory.reserve(MemoryCategory::DisplayBuffers, 400u).hasValue());
}

TEST(ConsumerMemoryTest, ReleasingReturnsTheBytesOnce)
{
    ConsumerMemory memory(cachesOf(1000u));
    an::Result<MemoryReservation> held = memory.reserve(MemoryCategory::DerivedCaches, 300u);
    ASSERT_TRUE(held.hasValue());
    MemoryReservation moved = std::move(*held);
    EXPECT_FALSE(held->valid());
    EXPECT_EQ(held->bytes(), 0u);
    held->release();
    EXPECT_EQ(memory.counters(MemoryCategory::DerivedCaches).reservedBytes, 300u);
    moved.release();
    moved.release();
    EXPECT_EQ(memory.counters(MemoryCategory::DerivedCaches).reservedBytes, 0u);
    EXPECT_EQ(memory.counters(MemoryCategory::DerivedCaches).entries, 0u);
}

// A reservation keeps the accountant's state, so a buffer may be released after the session that admitted it is gone.
TEST(ConsumerMemoryTest, AReservationMayOutliveItsSession)
{
    std::optional<MemoryReservation> kept;
    {
        std::optional<an::consumer::EngineSession> session = an::tests::consumer::openSession(1u);
        ASSERT_TRUE(session.has_value());
        an::Result<MemoryReservation> held = session->memory().reserve(MemoryCategory::DisplayBuffers, 64u);
        ASSERT_TRUE(held.hasValue());
        kept.emplace(std::move(*held));
    }
    kept->release();
    EXPECT_FALSE(kept->valid());
}

TEST(ConsumerMemoryTest, ZeroAllowancesAreRefusedByName)
{
    MemoryAllowances allowances;
    allowances.displayBuffers.bytes = 0u;
    const an::Status refused = an::consumer::checkMemoryAllowances(allowances);
    ASSERT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(refused.findContext(an::ContextKey::Parameter)->text, "memory.displayBuffers.bytes");
    allowances = MemoryAllowances{};
    allowances.totalBytes = 0u;
    EXPECT_EQ(an::consumer::checkMemoryAllowances(allowances).code(), an::ErrorCode::InvalidArgument);
}

TEST(ConsumerMemoryTest, CategoryNamesAreStable)
{
    EXPECT_STREQ(an::consumer::memoryCategoryName(MemoryCategory::HistoryCopies), "history copies");
    EXPECT_STREQ(an::consumer::memoryCategoryName(MemoryCategory::PendingSnapshots), "pending snapshots");
    EXPECT_STREQ(an::consumer::memoryCategoryName(MemoryCategory::DerivedCaches), "derived caches");
    EXPECT_STREQ(an::consumer::memoryCategoryName(MemoryCategory::DisplayBuffers), "display buffers");
}
