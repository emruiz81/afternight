// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ResidentMemory.h"

#include <algorithm>
#include <chrono>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
// Psapi.h must follow Windows.h.
#include <Psapi.h>
#else
#include <fstream>
#include <limits>
#include <string>
#endif

namespace an::bench
{

std::uint64_t currentResidentBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
#else
    // VmRSS from /proc/self/status, which is in kibibytes. statm's second field would do as well,
    // but status names its units and does not require knowing the page size.
    std::ifstream status("/proc/self/status");
    std::string label;
    while (status >> label)
    {
        if (label == "VmRSS:")
        {
            std::uint64_t kibibytes = 0;
            if (status >> kibibytes)
            {
                return kibibytes * 1024u;
            }
            return 0;
        }
        status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return 0;
#endif
}

ResidentMemorySampler::ResidentMemorySampler(std::uint32_t intervalMs)
    : m_intervalMs((std::max)(intervalMs, std::uint32_t{1}))
{
}

ResidentMemorySampler::~ResidentMemorySampler()
{
    stop();
}

void ResidentMemorySampler::start()
{
    if (m_running.load(std::memory_order_acquire))
    {
        return;
    }
    // Cleared here rather than in stop(), so the peak survives for a caller to read afterwards and
    // a restart cannot report work that has already finished.
    m_peakBytes.store(currentResidentBytes(), std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_worker = std::thread([this] {
        while (m_running.load(std::memory_order_acquire))
        {
            const std::uint64_t resident = currentResidentBytes();
            std::uint64_t peak = m_peakBytes.load(std::memory_order_relaxed);
            while (resident > peak && !m_peakBytes.compare_exchange_weak(peak, resident, std::memory_order_relaxed))
            {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
        }
    });
}

void ResidentMemorySampler::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    // A body that finished between two samples would otherwise contribute nothing at all.
    const std::uint64_t resident = currentResidentBytes();
    std::uint64_t peak = m_peakBytes.load(std::memory_order_relaxed);
    while (resident > peak && !m_peakBytes.compare_exchange_weak(peak, resident, std::memory_order_relaxed))
    {
    }
}

std::uint64_t ResidentMemorySampler::peakBytes() const
{
    return m_peakBytes.load(std::memory_order_acquire);
}

std::uint32_t ResidentMemorySampler::intervalMs() const
{
    return m_intervalMs;
}

} // namespace an::bench
