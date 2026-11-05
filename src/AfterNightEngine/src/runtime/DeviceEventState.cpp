// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the record behind a device event: its outcome, its timing, its waiters and
// its continuations, all under one mutex.
//
// Every mutation after the outcome is set is refused, so a backend that reports twice, or reports
// a transfer after completing, cannot change what a waiter already read. `finish` takes the
// continuations and the dependencies out under the lock, wakes the waiters, and then tells the
// continuations with no lock held, as the lock order in the header requires. Timing is derived
// from stored time points when it is asked for, so a phase the work has not left is measured up
// to the moment of the call. `reaches` walks only unfinished dependencies, with a visited list
// because two pieces of work may share one.

#include "runtime/DeviceEventState.h"

#include <algorithm>
#include <utility>

namespace an
{

namespace
{

/// The nanoseconds from one point to a later one, and zero for a pair out of order.
[[nodiscard]] std::uint64_t nanosecondsBetween(DeviceEventState::Clock::time_point from,
                                               DeviceEventState::Clock::time_point to) noexcept
{
    if (to <= from)
    {
        return 0u;
    }
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count());
}

} // namespace

DeviceEventState::DeviceEventState(std::uint64_t id, std::uint64_t deviceId,
                                   std::vector<std::shared_ptr<DeviceEventState>> dependencies) noexcept
    : m_id(id), m_deviceId(deviceId), m_accepted(Clock::now()), m_dependencies(std::move(dependencies))
{
}

std::optional<Status> DeviceEventState::outcome() const noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    return m_outcome;
}

Status DeviceEventState::wait(const std::stop_token& stopToken,
                              std::optional<Clock::time_point> deadline) const noexcept
{
    std::unique_lock<std::mutex> lock(m_mutex);
    const auto finished = [this] { return m_outcome.has_value(); };
    if (deadline.has_value())
    {
        static_cast<void>(m_finished.wait_until(lock, stopToken, *deadline, finished));
    }
    else
    {
        static_cast<void>(m_finished.wait(lock, stopToken, finished));
    }

    if (m_outcome.has_value())
    {
        return *m_outcome;
    }
    // A person deciding to stop is the more informative of the two, as at every checkpoint.
    if (stopToken.stop_requested())
    {
        return Status::failure(ErrorCode::Cancelled);
    }
    return Status::failure(ErrorCode::DeadlineExceeded);
}

DeviceWorkTiming DeviceEventState::timing() const noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    const Clock::time_point now = m_ended.value_or(Clock::now());

    DeviceWorkTiming timing;
    timing.waitingNanoseconds = nanosecondsBetween(m_accepted, m_queued.value_or(now));
    if (m_queued.has_value())
    {
        timing.queuedNanoseconds = nanosecondsBetween(*m_queued, m_started.value_or(now));
    }
    if (m_started.has_value())
    {
        timing.runningNanoseconds = nanosecondsBetween(*m_started, now);
    }
    timing.transferNanoseconds = m_transferNanoseconds;
    timing.kernelNanoseconds = m_kernelNanoseconds;
    timing.transfersRecorded = m_transfersRecorded;
    timing.kernelRecorded = m_kernelRecorded;
    return timing;
}

void DeviceEventState::markQueued() noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    if (m_outcome.has_value() || m_queued.has_value())
    {
        return;
    }
    m_queued = Clock::now();
    m_phase.store(DeviceWorkPhase::Queued, std::memory_order_release);
}

void DeviceEventState::markRunning() noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    if (m_outcome.has_value() || m_started.has_value())
    {
        return;
    }
    const Clock::time_point now = Clock::now();
    if (!m_queued.has_value())
    {
        m_queued = now;
    }
    m_started = now;
    m_phase.store(DeviceWorkPhase::Running, std::memory_order_release);
}

void DeviceEventState::recordTransfer(std::uint64_t nanoseconds, std::uint32_t declared) noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    if (m_outcome.has_value() || m_transfersRecorded >= declared)
    {
        return;
    }
    ++m_transfersRecorded;
    m_transferNanoseconds += nanoseconds;
}

void DeviceEventState::recordKernel(std::uint64_t nanoseconds) noexcept
{
    const std::lock_guard<std::mutex> held(m_mutex);
    if (m_outcome.has_value() || m_kernelRecorded)
    {
        return;
    }
    m_kernelRecorded = true;
    m_kernelNanoseconds = nanoseconds;
}

bool DeviceEventState::finish(Status outcome) noexcept
{
    std::vector<std::weak_ptr<DeviceEventContinuation>> continuations;
    std::vector<std::shared_ptr<DeviceEventState>> dependencies;
    {
        const std::lock_guard<std::mutex> held(m_mutex);
        if (m_outcome.has_value())
        {
            return false;
        }
        const Clock::time_point now = Clock::now();
        // Work a backend took and completed without saying it started is taken to have started when it
        // finished, so its queued time is not counted as running time.
        if (m_queued.has_value() && !m_started.has_value())
        {
            m_started = now;
        }
        m_ended = now;
        m_outcome = std::move(outcome);
        continuations.swap(m_continuations);
        dependencies.swap(m_dependencies);
        m_phase.store(DeviceWorkPhase::Finished, std::memory_order_release);
    }
    m_finished.notify_all();

    for (const std::weak_ptr<DeviceEventContinuation>& registered : continuations)
    {
        if (const std::shared_ptr<DeviceEventContinuation> continuation = registered.lock())
        {
            continuation->onEventFinished(*this);
        }
    }
    return true;
}

bool DeviceEventState::addContinuation(const std::weak_ptr<DeviceEventContinuation>& continuation)
{
    const std::lock_guard<std::mutex> held(m_mutex);
    if (m_outcome.has_value())
    {
        return false;
    }
    m_continuations.push_back(continuation);
    return true;
}

bool DeviceEventState::reaches(const DeviceEventState* target) const
{
    if (this == target)
    {
        return true;
    }

    std::vector<std::shared_ptr<DeviceEventState>> unvisited = pendingDependencies();
    std::vector<const DeviceEventState*> visited;
    while (!unvisited.empty())
    {
        const std::shared_ptr<DeviceEventState> current = std::move(unvisited.back());
        unvisited.pop_back();
        if (current.get() == target)
        {
            return true;
        }
        if (std::find(visited.begin(), visited.end(), current.get()) != visited.end())
        {
            continue;
        }
        visited.push_back(current.get());
        for (std::shared_ptr<DeviceEventState>& next : current->pendingDependencies())
        {
            unvisited.push_back(std::move(next));
        }
    }
    return false;
}

std::vector<std::shared_ptr<DeviceEventState>> DeviceEventState::pendingDependencies() const
{
    const std::lock_guard<std::mutex> held(m_mutex);
    if (m_outcome.has_value())
    {
        return {};
    }
    return m_dependencies;
}

} // namespace an
