// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ResidentMemory.h
 * @brief The process's resident memory as the operating system reports it, and a sampler placing its peak in time.
 *
 * The consumer measurement protocol compares the product's resident peak across one region, so the peak has to come
 * from the operating system rather than from any allocator: Qt, Python, the codecs and the engine all count. The
 * system's high-water mark is authoritative. On Linux it can be reset at the start of the region, so it is the
 * region's own peak; on Windows it is the process's lifetime peak, and the sampler's maximum over the region is the
 * region's. The sampler reads the resident set every interval on a thread of its own and records which phase of the
 * workflow each peak fell in.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace an::measurement
{

/// One reading of the process's resident memory.
struct ResidentMemoryReading
{
    /// The resident set now: `WorkingSetSize` on Windows, `VmRSS` on Linux.
    std::uint64_t residentBytes = 0;

    /// The system's high-water mark of the resident set: `PeakWorkingSetSize` on Windows - the process's lifetime - and
    /// `VmHWM` on Linux, since the last reset.
    std::uint64_t highWaterBytes = 0;

    /// Whether the system answered.
    bool valid = false;
};

/// Reads the process's resident memory from the operating system.
[[nodiscard]] ResidentMemoryReading readResidentMemory();

/// Resets the system's high-water mark to the current resident set, where the system allows it: Linux, by writing 5
/// to `/proc/self/clear_refs`. Returns whether it did.
bool resetResidentHighWater();

/// The metric names this host reads, for a result to state.
[[nodiscard]] std::string residentMemoryMetric();

/// A thread that reads the resident set at a fixed interval and keeps the peak and where it fell.
class ResidentMemorySampler
{
public:
    struct Summary
    {
        std::uint64_t samples = 0;
        std::uint64_t peakBytes = 0;
        double peakAtMs = 0.0;
        std::string peakPhase;
        /// The largest reading in each phase the sampler was told of.
        std::map<std::string, std::uint64_t> phasePeaks;
        double intervalMs = 0.0;
    };

    explicit ResidentMemorySampler(std::chrono::milliseconds interval);
    ~ResidentMemorySampler();

    ResidentMemorySampler(const ResidentMemorySampler&) = delete;
    ResidentMemorySampler& operator=(const ResidentMemorySampler&) = delete;

    /// Takes a first reading and starts the thread; the region's clock starts here.
    void start();

    /// Names the phase later readings belong to. Thread-safe.
    void setPhase(const std::string& phase);

    /// Takes a last reading, stops the thread and returns what it saw.
    Summary stop();

private:
    void sample();
    void run();

    std::chrono::milliseconds m_interval;
    std::chrono::steady_clock::time_point m_started;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stopping = false;
    std::string m_phase = "start";
    Summary m_summary;
    std::thread m_thread;
};

} // namespace an::measurement
