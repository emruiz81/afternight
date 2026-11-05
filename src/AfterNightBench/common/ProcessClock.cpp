// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ProcessClock.h"

#include <chrono>
#include <limits>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <ctime>
#endif

namespace an::bench
{

double processCpuMilliseconds()
{
#if defined(_WIN32)
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto ticks = [](const FILETIME& value) {
        ULARGE_INTEGER combined{};
        combined.LowPart = value.dwLowDateTime;
        combined.HighPart = value.dwHighDateTime;
        return combined.QuadPart;
    };
    // Kernel and user together: a stage that spends its time inside the file system is spending
    // this process's processor time just as much as one that spends it in arithmetic.
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
#else
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(value.tv_sec) * 1000.0 + static_cast<double>(value.tv_nsec) / 1000000.0;
#endif
}

void waitForWarmWorkersToPark()
{
    // The measured hosts park oneTBB's active wait in a little over 100 ms. Keep a fixed margin so
    // the clock boundary is identical across runs rather than load-dependent.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

} // namespace an::bench
