// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the record behind a device event: one outcome, its waiters, and what waits
// for it.
//
// An event is observed from any thread and finished from whichever thread the backend completes work
// on, so this record is where the synchronisation lives. One mutex guards the outcome, the timing and
// the continuations; the phase is also an atomic, so a caller asking how far work has got never waits
// for a lock the finishing thread holds.
//
// A finished event keeps nothing of anything else. Its continuations and its dependencies are handed
// over when it finishes - the first to be told, the second so that a long chain of finished work does
// not keep every earlier event alive through the one at its end.
//
// Lock order: a device's lock may be held while one event's lock is taken, and never the other way
// round; an event's lock is never held while a continuation runs.

#pragma once

#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <vector>

namespace an
{

class DeviceEventState;

/// Something that waits for an event to finish and is told once when it does.
class DeviceEventContinuation
{
public:
    /// Virtual, so a continuation held by base pointer is destroyed as what it is.
    virtual ~DeviceEventContinuation() = default;

    /// Called once, on the thread that finished the event, after its outcome is visible to waiters and
    /// with no lock of the event held.
    ///
    /// @param event the event that finished.
    virtual void onEventFinished(const DeviceEventState& event) noexcept = 0;

protected:
    /// Constructible only by a derived class.
    DeviceEventContinuation() = default;

    /// Protected so a continuation is never copied through a base pointer.
    DeviceEventContinuation(const DeviceEventContinuation&) = default;

    /// Protected for the same reason as the copy constructor.
    DeviceEventContinuation& operator=(const DeviceEventContinuation&) = default;
};

/// One piece of accepted work's outcome, timing, waiters and continuations.
class DeviceEventState
{
public:
    /// The clock every duration here is measured on.
    using Clock = std::chrono::steady_clock;

    /// Records work accepted now.
    ///
    /// @param id the event's identity on its device.
    /// @param deviceId the device the work was submitted to.
    /// @param dependencies the events the work waits for, kept until it finishes.
    DeviceEventState(std::uint64_t id, std::uint64_t deviceId,
                     std::vector<std::shared_ptr<DeviceEventState>> dependencies) noexcept;

    DeviceEventState(const DeviceEventState&) = delete;
    DeviceEventState& operator=(const DeviceEventState&) = delete;
    DeviceEventState(DeviceEventState&&) = delete;
    DeviceEventState& operator=(DeviceEventState&&) = delete;
    ~DeviceEventState() = default;

    /// @return the event's identity on its device.
    [[nodiscard]] std::uint64_t id() const noexcept
    {
        return m_id;
    }

    /// @return the device the work was submitted to.
    [[nodiscard]] std::uint64_t deviceId() const noexcept
    {
        return m_deviceId;
    }

    /// @return how far the work has got, read without taking the lock.
    [[nodiscard]] DeviceWorkPhase phase() const noexcept
    {
        return m_phase.load(std::memory_order_acquire);
    }

    /// @return the outcome, or no value while the work has not finished.
    [[nodiscard]] std::optional<Status> outcome() const noexcept;

    /// Waits for the outcome, a stop request, or a deadline.
    ///
    /// @param stopToken what stops the wait; a token with no stop state never does.
    /// @param deadline when the wait gives up, or no value for never.
    /// @return the outcome, or `Cancelled` or `DeadlineExceeded` for the wait itself.
    [[nodiscard]] Status wait(const std::stop_token& stopToken,
                              std::optional<Clock::time_point> deadline) const noexcept;

    /// @return where the work's time went so far.
    [[nodiscard]] DeviceWorkTiming timing() const noexcept;

    /// Records that the work was handed to its backend.
    void markQueued() noexcept;

    /// Records that the backend started the work. The first call counts.
    void markRunning() noexcept;

    /// Records one performed transfer, up to the number the work declared.
    ///
    /// @param nanoseconds what it took.
    /// @param declared how many transfers the work declared.
    void recordTransfer(std::uint64_t nanoseconds, std::uint32_t declared) noexcept;

    /// Records that the kernel ran. The first call counts.
    ///
    /// @param nanoseconds what it took.
    void recordKernel(std::uint64_t nanoseconds) noexcept;

    /// Gives the event its outcome, wakes its waiters and tells its continuations.
    ///
    /// @param outcome the work's outcome.
    /// @return false, changing nothing, when the event already had one.
    bool finish(Status outcome) noexcept;

    /// Registers a continuation, unless the event has already finished.
    ///
    /// @param continuation what to tell. Held weakly.
    /// @return true when registered; false, registering nothing, when the event has already finished.
    [[nodiscard]] bool addContinuation(const std::weak_ptr<DeviceEventContinuation>& continuation);

    /// Whether this event is `target`, or depends on it through events that have not finished.
    ///
    /// @param target the event to look for.
    /// @return true when waiting for this event also waits for `target`.
    [[nodiscard]] bool reaches(const DeviceEventState* target) const;

    /// Builds the public handle over a record.
    ///
    /// @param state the record.
    /// @return an event observing it.
    [[nodiscard]] static DeviceEvent makeHandle(std::shared_ptr<DeviceEventState> state) noexcept
    {
        return DeviceEvent(std::move(state));
    }

    /// The record behind a public handle.
    ///
    /// @param event the handle.
    /// @return its record, which is null for an event that observes nothing.
    [[nodiscard]] static const std::shared_ptr<DeviceEventState>& share(const DeviceEvent& event) noexcept
    {
        return event.m_state;
    }

private:
    /// @return the dependencies still held, or none once the event has finished.
    [[nodiscard]] std::vector<std::shared_ptr<DeviceEventState>> pendingDependencies() const;

    /// The event's identity on its device; immutable.
    std::uint64_t m_id;

    /// The device the work was submitted to; immutable.
    std::uint64_t m_deviceId;

    /// How far the work has got. Written under `m_mutex`, and read without it by `phase()`.
    std::atomic<DeviceWorkPhase> m_phase{DeviceWorkPhase::Waiting};

    /// Guards every member below.
    mutable std::mutex m_mutex;

    /// Signalled once, when the outcome is set; `condition_variable_any` so a wait can observe a
    /// stop token.
    mutable std::condition_variable_any m_finished;

    /// The outcome, set once and never changed.
    std::optional<Status> m_outcome;

    /// When the work was accepted.
    Clock::time_point m_accepted;

    /// When the work was handed to the backend.
    std::optional<Clock::time_point> m_queued;

    /// When the backend started the work.
    std::optional<Clock::time_point> m_started;

    /// When the outcome was set.
    std::optional<Clock::time_point> m_ended;

    /// The sum of what the backend recorded for the work's transfers.
    std::uint64_t m_transferNanoseconds = 0u;

    /// What the backend recorded for the work's kernel.
    std::uint64_t m_kernelNanoseconds = 0u;

    /// How many transfers were recorded, never more than the work declared.
    std::uint32_t m_transfersRecorded = 0u;

    /// Whether the kernel was recorded as run.
    bool m_kernelRecorded = false;

    /// Told once when the work finishes, and emptied then.
    std::vector<std::weak_ptr<DeviceEventContinuation>> m_continuations;

    /// The work this work waits for, released when it finishes.
    std::vector<std::shared_ptr<DeviceEventState>> m_dependencies;
};

} // namespace an
