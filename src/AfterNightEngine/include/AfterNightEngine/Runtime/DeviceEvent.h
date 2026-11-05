// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Work a device has accepted and has not necessarily finished.
///
/// A device finishes work after the call that submitted it has returned. That is what makes a device
/// worth having, and it is the property an interface designed around synchronous calls cannot express
/// later without a redesign. An event is that deferred completion as a value: a caller holds one for
/// each piece of work, passes it to later work that has to wait for it, asks it what happened, and
/// waits on it when a synchronous answer is what the caller owes its own caller.
///
/// Three rules shape it.
///
/// **It reports a `Status`, the vocabulary every operation reports in.** Success, a failure with its
/// typed detail, `Cancelled`, or `DeadlineExceeded`. A dependency that did not succeed is the cause of
/// the failure of the work that needed it, so a report shows the stage that went wrong without every
/// later stage rediscovering it.
///
/// **Waiting is not cancelling.** A wait that the waiter's own execution stops returns `Cancelled` and
/// leaves the work alone; stopping the work is what the stop token the work was submitted with is for.
/// A synchronous operation that waits internally can therefore give its caller a prompt answer without
/// leaving a device half-way through a transfer nobody accounted for.
///
/// **Its timing separates the kinds of time a planner has to tell apart.** Waiting for dependencies,
/// queued on the device and running - and within running, what the backend spent in transfers and
/// what it spent in the kernel, because a device result slower than the host is only explicable when
/// transfer time is not hidden inside kernel time.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, because a wait only borrows one for the duration of the call.
class ExecutionContext;

/// How far a piece of accepted work has got.
///
/// The phases only move forwards. Work refused before it was accepted has no event at all.
enum class DeviceWorkPhase : std::uint8_t
{
    /// Accepted, and waiting for one or more of its dependencies to finish.
    Waiting,

    /// Every dependency succeeded and the backend has the work, which it has not started.
    Queued,

    /// The backend has started the work.
    Running,

    /// The work has an outcome, which never changes afterwards.
    Finished
};

/// The phase's own name, for a diagnostic or a report.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param phase the phase to name. A value outside the enumeration is named `"UnknownDeviceWorkPhase"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* deviceWorkPhaseName(DeviceWorkPhase phase) noexcept;

/// Where a piece of work's time went, in nanoseconds on the steady clock.
///
/// The three phase durations partition the time from acceptance to the outcome: work that never
/// reached a phase spent nothing in it. The transfer and kernel figures are what the backend recorded
/// while running. A backend that measures correctly keeps their sum within `runningNanoseconds`,
/// give or take the disagreement between its clock and the engine's; the engine records the figures
/// as given and does not check them.
struct DeviceWorkTiming
{
    /// From acceptance until the work was handed to the backend, or until its outcome when it never
    /// was.
    std::uint64_t waitingNanoseconds = 0u;

    /// From hand-off until the backend started the work.
    std::uint64_t queuedNanoseconds = 0u;

    /// From the start until the outcome.
    std::uint64_t runningNanoseconds = 0u;

    /// What the backend recorded spending in the work's transfers.
    std::uint64_t transferNanoseconds = 0u;

    /// What the backend recorded spending in the work's kernel.
    std::uint64_t kernelNanoseconds = 0u;

    /// How many of the work's declared transfers the backend recorded as performed.
    std::uint32_t transfersRecorded = 0u;

    /// Whether the backend recorded the work's kernel as run.
    bool kernelRecorded = false;
};

/// The engine's record of one piece of accepted work, defined inside the engine.
///
/// An event holds a pointer to one. A caller can do nothing with the name; the outcome, the timing and
/// the waiters live there.
class DeviceEventState;

/// One piece of accepted work, observed.
///
/// Returned by `Device::submit`, and passed to later submissions as a dependency. Copyable: every copy
/// observes the same work, and a copy costs one shared reference. An event owns no device memory and
/// no backend resource, so holding one past the work's outcome - or past its device's shutdown - is
/// harmless.
///
/// Every member is safe to call from several threads at once.
class DeviceEvent
{
public:
    /// An event that observes nothing. `valid()` is false.
    DeviceEvent() noexcept = default;

    /// Whether this event observes a piece of work.
    ///
    /// @return false for a default-constructed event.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// This event's identity, unique among the events its device creates and never reused.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the identity, or `0` for an event that observes nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t id() const noexcept;

    /// The identity of the device the work was submitted to.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the device's identity, or `0` for an event that observes nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t deviceId() const noexcept;

    /// How far the work has got.
    ///
    /// A diagnostic answer rather than a decision: another thread may move the work on immediately
    /// afterwards. `outcome()` and `wait()` are the calls that decide.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the phase, or `DeviceWorkPhase::Finished` for an event that observes nothing, because
    ///         there is nothing left to wait for.
    [[nodiscard]] AFTERNIGHTENGINE_API DeviceWorkPhase phase() const noexcept;

    /// What happened to the work, if it has finished.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return no value while the work has not finished; otherwise its outcome, which never changes.
    ///         An event that observes nothing answers `ErrorCode::NotInitialized`.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<Status> outcome() const noexcept;

    /// Waits until the work has finished.
    ///
    /// Blocks the calling thread with no time limit. It must not be called from a thread the device
    /// needs in order to finish the work - from inside a backend, for instance - because nothing would
    /// then finish it.
    ///
    /// Thread-safe. Allocation-free.
    ///
    /// @return the work's outcome, or `ErrorCode::NotInitialized` for an event that observes nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API Status wait() const noexcept;

    /// Waits until the work has finished or the waiting operation is stopped.
    ///
    /// The wait observes `execution`'s stop token and deadline and nothing else. Stopping the wait does
    /// not stop the work; the work observes the stop token it was submitted with, which is a different
    /// operation's unless the caller passed the same one. After a stopped wait, `outcome()` says
    /// whether the work had finished anyway.
    ///
    /// Thread-safe. Allocation-free. The same rule about backend threads applies as for `wait()`.
    ///
    /// @param execution the operation that is waiting. Only read; not retained.
    /// @return the work's outcome; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when
    ///         `execution` was stopped first, with a stop request reported in preference to an elapsed
    ///         deadline; or `ErrorCode::NotInitialized` when this event or `execution` holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API Status wait(const ExecutionContext& execution) const noexcept;

    /// Where the work's time went so far.
    ///
    /// Complete once the work has finished; before that, the phases it has not left are measured up to
    /// the moment of the call.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the timing, or all zeroes for an event that observes nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API DeviceWorkTiming timing() const noexcept;

private:
    /// The work record is what makes an event, so it is also what may build one and look inside one.
    friend class DeviceEventState;

    /// Observes a record the device has already built.
    ///
    /// @param state the record. Never null on this path.
    explicit DeviceEvent(std::shared_ptr<DeviceEventState> state) noexcept : m_state(std::move(state)) {}

    /// The work's record, or null.
    std::shared_ptr<DeviceEventState> m_state;
};

} // namespace an
