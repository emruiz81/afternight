// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// EngineBenchCommon - the process CPU clock.
//
// Engine-neutral, so the runner and every scenario inside it read the same clock the same way.
// It is deliberately not a thread clock: what a measurement needs to know is how much processor
// time the work cost across every thread it used, which is what turns a wall time into a statement
// about parallelism rather than a number that could mean anything.

#pragma once

namespace an::bench
{

/// Processor time consumed by this process so far, in milliseconds, across all of its threads.
///
/// Both supported hosts report kernel and user time together and include worker threads, so the
/// difference between two samples is the processor time spent between them.
///
/// Returns a quiet NaN when the platform refuses to answer, rather than zero: a zero would be
/// indistinguishable from work that took no measurable time, and the run protocol needs a run that
/// could not read the clock to say so instead of reporting a plausible number.
///
/// The clock is coarse. It advances in 15.625 ms steps on the Windows benchmark host, which the run
/// manifest records, so a region shorter than several of those ticks measures quantization rather
/// than work.
[[nodiscard]] double processCpuMilliseconds();

/// Let workers awakened by a benchmark warm-up park before the measured process-clock interval.
///
/// oneTBB and libraries built over it briefly spin before sleeping. That work belongs to the
/// warm-up; if the next interval starts immediately, its process CPU can exceed the runner's
/// stated width even though every measured task honored that width. The runner calls this exact
/// boundary, and the wait itself is outside every reported clock.
void waitForWarmWorkersToPark();

} // namespace an::bench
