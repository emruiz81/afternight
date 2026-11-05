// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "controls/SystemMemoryInfoParser.h"

namespace
{
using an::ui::system_memory::MemorySnapshot;

TEST(SystemMemoryInfoParserLinux, LinuxFreeBytesParsesTotalAndAvailable)
{
    constexpr const char* kFreeOutput =
        "               total        used        free      shared  buff/cache   available\n"
        "Mem:        98734592000 20000000000 10000000000 1200000000 58734592000 78734592000\n"
        "Swap:         2147479552           0  2147479552\n";

    const MemorySnapshot snapshot = an::ui::system_memory::parseLinuxFreeBytesOutput(kFreeOutput);
    EXPECT_EQ(snapshot.totalBytes, 98734592000LL);
    EXPECT_EQ(snapshot.availableBytes, 78734592000LL);
    EXPECT_EQ(an::ui::system_memory::calculateUsagePercent(snapshot), 20);
}

TEST(SystemMemoryInfoParserLinux, LinuxMeminfoPrefersMemAvailable)
{
    constexpr const char* kMeminfo = "MemTotal:       96420500 kB\n"
                                     "MemFree:        35617072 kB\n"
                                     "MemAvailable:   83191468 kB\n"
                                     "Buffers:          607328 kB\n"
                                     "Cached:         47354956 kB\n"
                                     "Shmem:           1674540 kB\n"
                                     "SReclaimable:    2259944 kB\n";

    const MemorySnapshot snapshot = an::ui::system_memory::parseLinuxMeminfo(kMeminfo);
    EXPECT_EQ(snapshot.totalBytes, 96420500LL * 1024LL);
    EXPECT_EQ(snapshot.availableBytes, 83191468LL * 1024LL);
    EXPECT_EQ(an::ui::system_memory::calculateUsagePercent(snapshot), 13);
}

TEST(SystemMemoryInfoParserLinux, LinuxMeminfoFallsBackWhenMemAvailableMissing)
{
    constexpr const char* kMeminfoNoAvailable = "MemTotal:       1000 kB\n"
                                                "MemFree:        200 kB\n"
                                                "Buffers:        50 kB\n"
                                                "Cached:         300 kB\n"
                                                "SReclaimable:   80 kB\n"
                                                "Shmem:          30 kB\n";

    const MemorySnapshot snapshot = an::ui::system_memory::parseLinuxMeminfo(kMeminfoNoAvailable);
    EXPECT_EQ(snapshot.totalBytes, 1000LL * 1024LL);
    EXPECT_EQ(snapshot.availableBytes, 600LL * 1024LL);
    EXPECT_EQ(an::ui::system_memory::calculateUsagePercent(snapshot), 40);
}

TEST(SystemMemoryInfoParserLinux, LinuxMeminfoHandlesLeadingWhitespace)
{
    constexpr const char* kMeminfoIndented = "  MemTotal:       8000 kB\n"
                                             "\tMemAvailable:   6000 kB\n";

    const MemorySnapshot snapshot = an::ui::system_memory::parseLinuxMeminfo(kMeminfoIndented);
    EXPECT_EQ(snapshot.totalBytes, 8000LL * 1024LL);
    EXPECT_EQ(snapshot.availableBytes, 6000LL * 1024LL);
    EXPECT_EQ(an::ui::system_memory::calculateUsagePercent(snapshot), 25);
}
} // namespace
