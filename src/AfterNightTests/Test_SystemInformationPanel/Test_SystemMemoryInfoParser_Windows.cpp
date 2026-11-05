// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "controls/SystemMemoryInfoParser.h"

namespace
{
using an::ui::system_memory::MemorySnapshot;

TEST(SystemMemoryInfoParserWindows, BuildSnapshotComputesExpectedUsage)
{
    constexpr std::uint64_t total = 64ULL * 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t available = 48ULL * 1024ULL * 1024ULL * 1024ULL;

    const MemorySnapshot snapshot = an::ui::system_memory::buildWindowsSnapshot(total, available);
    EXPECT_EQ(snapshot.totalBytes, static_cast<std::int64_t>(total));
    EXPECT_EQ(snapshot.availableBytes, static_cast<std::int64_t>(available));
    EXPECT_EQ(an::ui::system_memory::calculateUsagePercent(snapshot), 25);
}

TEST(SystemMemoryInfoParserWindows, BuildSnapshotClampsAvailableWhenGreaterThanTotal)
{
    constexpr std::uint64_t total = 16ULL * 1024ULL;
    constexpr std::uint64_t available = 32ULL * 1024ULL;

    const MemorySnapshot snapshot = an::ui::system_memory::buildWindowsSnapshot(total, available);
    EXPECT_EQ(snapshot.totalBytes, 16LL * 1024LL);
    EXPECT_EQ(snapshot.availableBytes, 16LL * 1024LL);
    EXPECT_EQ(an::ui::system_memory::calculateUsagePercent(snapshot), 0);
}

TEST(SystemMemoryInfoParserWindows, UsagePercentHandlesZeroTotal)
{
    MemorySnapshot snapshot;
    snapshot.totalBytes = 0;
    snapshot.availableBytes = 12345;
    EXPECT_EQ(an::ui::system_memory::calculateUsagePercent(snapshot), 0);
}
} // namespace
