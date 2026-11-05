// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - resident memory, measured rather than assumed.
//
// The pipeline's peak memory is a stated ceiling, because a 16-bit frame is meant to stay 16-bit.
// That ceiling needs a number, and the operating system will only tell you the resident size right now.
//
// Neither platform offers a per-region peak that can be reset between scenarios - Windows keeps a
// process peak since start that no documented call clears - so the peak for one scenario is sampled:
// a low-frequency thread reads the resident size while the measured body runs and keeps the largest
// it saw. That is honest about one limitation, which is why the interval travels with the run: a
// spike shorter than the interval can be missed entirely, so a sampled peak is a lower bound on the
// true one, never an upper.
//
// Engine-neutral by construction. It reads this process, not the engine, so it stays in the shared layer.

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace an::bench
{

/// How often the sampler reads the resident size, in milliseconds.
///
/// Small enough that a frame-scale allocation cannot hide between two samples, large enough that the
/// sampler is not a participant in the measurement it is taking. Recorded in the run manifest so a
/// peak can be read with its own resolution in view, exactly as the CPU clock's is.
inline constexpr std::uint32_t kResidentSampleIntervalMs = 2;

/// This process's resident set - working set on Windows, VmRSS on Linux - in bytes.
///
/// Returns 0 when the platform declines to answer, which a caller must treat as "unknown" and not
/// as "nothing resident".
[[nodiscard]] std::uint64_t currentResidentBytes();

/// The largest resident size seen between `start()` and `stop()`.
///
/// One sampler is reused across a scenario's repetitions; `start()` clears the previous peak so a
/// restarted sampler never reports a number from work that already finished.
class ResidentMemorySampler final
{
public:
    explicit ResidentMemorySampler(std::uint32_t intervalMs = kResidentSampleIntervalMs);
    ~ResidentMemorySampler();

    ResidentMemorySampler(const ResidentMemorySampler&) = delete;
    ResidentMemorySampler& operator=(const ResidentMemorySampler&) = delete;

    /// Begin sampling. Clears any previous peak. Sampling an already-running sampler does nothing.
    void start();

    /// Stop sampling and take a final reading, so a body that finishes between two samples still
    /// contributes the size it reached.
    void stop();

    /// The peak observed during the last sampled interval, in bytes.
    [[nodiscard]] std::uint64_t peakBytes() const;

    /// The interval this sampler reads at, in milliseconds.
    [[nodiscard]] std::uint32_t intervalMs() const;

private:
    std::uint32_t m_intervalMs;
    std::atomic<std::uint64_t> m_peakBytes{0};
    std::atomic<bool> m_running{false};
    std::thread m_worker;
};

} // namespace an::bench
