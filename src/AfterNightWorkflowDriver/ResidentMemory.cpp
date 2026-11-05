// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ResidentMemory.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>
#else
#include <fstream>
#include <sstream>
#endif

#include <algorithm>
#include <utility>

namespace an::measurement
{
namespace
{

#if !defined(_WIN32)
// The value of one `Key:   123 kB` line of /proc/self/status, in bytes.
std::uint64_t statusKilobytes(const std::string& text, const std::string& key)
{
    const std::size_t at = text.find(key + ":");
    if (at == std::string::npos)
    {
        return 0;
    }
    std::istringstream line(text.substr(at + key.size() + 1));
    std::uint64_t kilobytes = 0;
    line >> kilobytes;
    return kilobytes * 1024u;
}
#endif

} // namespace

ResidentMemoryReading readResidentMemory()
{
    ResidentMemoryReading reading;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    {
        reading.residentBytes = counters.WorkingSetSize;
        reading.highWaterBytes = counters.PeakWorkingSetSize;
        reading.valid = true;
    }
#else
    std::ifstream status("/proc/self/status");
    if (status)
    {
        std::ostringstream text;
        text << status.rdbuf();
        reading.residentBytes = statusKilobytes(text.str(), "VmRSS");
        reading.highWaterBytes = statusKilobytes(text.str(), "VmHWM");
        reading.valid = reading.residentBytes > 0;
    }
#endif
    return reading;
}

bool resetResidentHighWater()
{
#if defined(_WIN32)
    return false;
#else
    std::ofstream clearRefs("/proc/self/clear_refs");
    if (!clearRefs)
    {
        return false;
    }
    clearRefs << "5";
    return static_cast<bool>(clearRefs.flush());
#endif
}

std::string residentMemoryMetric()
{
#if defined(_WIN32)
    return "WorkingSetSize, PeakWorkingSetSize";
#else
    return "VmRSS, VmHWM";
#endif
}

ResidentMemorySampler::ResidentMemorySampler(const std::chrono::milliseconds interval) : m_interval(interval)
{
    m_summary.intervalMs = static_cast<double>(interval.count());
}

ResidentMemorySampler::~ResidentMemorySampler()
{
    if (m_thread.joinable())
    {
        (void)stop();
    }
}

void ResidentMemorySampler::start()
{
    m_started = std::chrono::steady_clock::now();
    sample();
    m_thread = std::thread([this]() { run(); });
}

void ResidentMemorySampler::setPhase(const std::string& phase)
{
    std::lock_guard lock(m_mutex);
    m_phase = phase;
}

ResidentMemorySampler::Summary ResidentMemorySampler::stop()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    sample();
    std::lock_guard lock(m_mutex);
    return m_summary;
}

void ResidentMemorySampler::sample()
{
    const ResidentMemoryReading reading = readResidentMemory();
    if (!reading.valid)
    {
        return;
    }
    const double atMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_started).count();
    std::lock_guard lock(m_mutex);
    ++m_summary.samples;
    if (reading.residentBytes > m_summary.peakBytes)
    {
        m_summary.peakBytes = reading.residentBytes;
        m_summary.peakAtMs = atMs;
        m_summary.peakPhase = m_phase;
    }
    std::uint64_t& phasePeak = m_summary.phasePeaks[m_phase];
    phasePeak = (std::max)(phasePeak, reading.residentBytes);
}

void ResidentMemorySampler::run()
{
    std::unique_lock lock(m_mutex);
    while (!m_stopping)
    {
        m_wake.wait_for(lock, m_interval, [this]() { return m_stopping; });
        if (m_stopping)
        {
            break;
        }
        lock.unlock();
        sample();
        lock.lock();
    }
}

} // namespace an::measurement
