// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// EngineBenchCommon - what the machine was permitted and configured to do during a run.
//
// The run protocol lists affinity, power plan, frequency policy and background load among the
// fields a publishable run must carry, and each of them can move a measurement without changing a
// line of code: a process confined to half the cores, a laptop that dropped to a power-saving
// governor, a clock that drooped under sustained load, an indexer that woke up mid-run. Read from
// the operating system rather than asserted by the operator, because the operator's belief about
// the machine is exactly what these fields exist to check.

#pragma once

#include "Results.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace an::bench
{

/// The machine's policy toward this process, as the operating system reports it.
struct HostPolicy
{
    /// Logical processors this process is permitted to run on.
    std::uint32_t affinityCores = 0;

    /// Which ones, as lowercase hexadecimal with the low bit standing for processor 0.
    ///
    /// Covers the first 64 processors. `affinityCores` is counted over the whole set, so on a
    /// larger machine the count stays right while the mask describes the part that fits.
    /// Empty when the platform will not report it.
    std::string affinityMask;

    /// The power policy in force. Windows names the active scheme; Linux states the governor, the
    /// energy-performance preference and whether turbo is enabled. Empty when unavailable.
    std::string powerPlan;
};

/// Read the policy currently in force. Cheap; safe to call once per run.
[[nodiscard]] HostPolicy readHostPolicy();

/// Mean current clock across the machine's logical processors, in megahertz.
///
/// Zero when the platform will not report it, which is a different fact from a clock of zero and is
/// recorded as such: a sample that could not be taken is not a sample of nothing.
[[nodiscard]] double currentCpuMegahertz();

/// The share of the machine's processor capacity that something other than this process consumed.
///
/// `systemBusyMs` is processor time the whole machine spent out of idle, `processCpuMs` is this
/// process's own share of it, and the pair is divided by the capacity the run had available. The
/// benchmark's own work is subtracted because the field answers "what else was the machine doing";
/// left in, every run would report itself and the field would say nothing.
///
/// Clamped to [0, 1]. The two clocks are read a moment apart and at different resolutions, so their
/// difference can land just outside the range it describes, and a fraction above one would be
/// published as a fact.
[[nodiscard]] double backgroundLoadFraction(double systemBusyMs, double processCpuMs, double wallMs,
                                            std::uint32_t logicalCores);

/// Samples the processor clock and the machine's background load across a run.
///
/// A single reading cannot show a clock drooping under sustained load, which the run protocol calls
/// the most common source of invented results on the laptop host, so the clock is sampled
/// throughout and reported as a range. The default interval is one second, which is what the
/// protocol executed by hand and slow enough that the sampler is not a participant in the
/// measurement. The thread is deliberately not pinned: reserving a core to isolate it would change
/// the machine the benchmark is measuring, and the perturbation this protocol actually recorded
/// came from a per-second process spawn rather than from an in-process read.
class HostActivitySampler
{
public:
    explicit HostActivitySampler(std::chrono::milliseconds interval = std::chrono::seconds(1));
    ~HostActivitySampler();

    HostActivitySampler(const HostActivitySampler&) = delete;
    HostActivitySampler& operator=(const HostActivitySampler&) = delete;

    /// Begin sampling, taking one reading immediately so even a short run has endpoints.
    void start();

    /// Take a final reading and stop. Safe to call when never started.
    void stop();

    /// The clock across the sampled region. `samples` is zero when nothing was sampled.
    [[nodiscard]] SampledRange frequencyMhz() const;

    /// The share of the machine consumed by other processes across the sampled region.
    [[nodiscard]] double backgroundLoad() const;

private:
    std::chrono::milliseconds m_interval;
    std::atomic<bool> m_running{false};
    std::thread m_worker;
    /// Wakes the worker between readings, so stopping never waits out an interval.
    std::mutex m_wakeMutex;
    std::condition_variable m_wake;
    mutable std::mutex m_mutex;
    SampledRange m_frequency;
    double m_systemBusyStartMs = 0.0;
    double m_processCpuStartMs = 0.0;
    double m_backgroundLoad = 0.0;
    std::chrono::steady_clock::time_point m_startedAt;

    void sampleFrequency();
};

} // namespace an::bench
