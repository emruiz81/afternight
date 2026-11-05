// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The physical-memory query.

#include <AfterNightPlatform/SystemMemory.h>

#include <gtest/gtest.h>

namespace
{

TEST(SystemMemoryTest, TheMachineReportsSomeMemoryAndNoMoreAvailableThanInstalled)
{
    const auto memory = an::platform::querySystemMemory();

    EXPECT_GT(memory.totalPhysicalBytes, 0u);
    EXPECT_GT(memory.availablePhysicalBytes, 0u);
    EXPECT_LE(memory.availablePhysicalBytes, memory.totalPhysicalBytes);
}

} // namespace
