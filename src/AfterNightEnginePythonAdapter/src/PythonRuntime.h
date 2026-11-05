// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEnginePythonAdapter - the interpreter this adapter owns, and how a call enters it.
//
// This is the implementation side of `PythonRuntimeAdapter`: the state a handle points at, the
// process slot that makes "one interpreter per process" a checkable rule rather than a hope, and the
// two calls that cross into Python. Python's own header arrives through "PythonHeader.h" first,
// before any standard header, for the reason that file gives.
//
// What lives here rather than in the public header is everything a consumer must not need: a thread
// state, a lock, a lease, and the counters. What lives in the public header is a handle and a
// forward declaration, which is what lets a caller compile against this adapter without a Python
// toolchain.

#pragma once

#include "PythonHeader.h"

#include "PythonProcessState.h"

#include <AfterNightEnginePythonAdapter/PythonAdapterCapabilities.h>
#include <AfterNightEnginePythonAdapter/PythonBufferBridge.h>
#include <AfterNightEnginePythonAdapter/PythonRuntimeAdapter.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace an
{

class ExecutionContext;

/// A failure the adapter recognised itself, rather than one to translate out of the interpreter.
///
/// A body that has already worked out what went wrong - a module that is not there, an attribute that
/// is not callable, a value returned where none was expected - fills this in, and the call reports it
/// in the adapter's own words. The Python error is then cleared rather than translated, and the
/// exception counter is left alone: it counts what this adapter could not name.
struct RecognisedFailure
{
    /// Which of this adapter's rules the body decided on, or `None` to translate Python's error.
    PythonAdapterReason reason = PythonAdapterReason::None;

    /// The sentence for a person, moved into the failure.
    std::string message;
};

/// The interpreter one adapter started, the engine context it holds open, and the calls in it.
///
/// Reference-counted by the handles and the calls that are running, so an adapter destroyed while
/// another thread is inside a call leaves the interpreter alive until that call returns. Finalising
/// happens in the destructor, which is what makes that a late release rather than a use-after-free.
///
/// Nothing about this type is copyable or movable: the interpreter it owns is process-wide, and the
/// slot it holds is released by its destructor.
class PythonInterpreterState
{
public:
    /// Starts an interpreter, or reports why it cannot.
    ///
    /// The order is the contract, and it is the order the public header documents: the build fact
    /// first, then the options the adapter can judge alone, then the process slot, then a foreign
    /// interpreter, then the engine context's lease, and only then the runtime itself. The slot is
    /// taken only when the runtime has started and a refused start releases the lease, so it leaves
    /// no slot and no lease behind.
    ///
    /// @param engine the engine context the adapter belongs to.
    /// @param options what the caller configured.
    /// @return the state, or the failure to report.
    [[nodiscard]] static Result<std::shared_ptr<PythonInterpreterState>> start(const EngineContext& engine,
                                                                               const PythonRuntimeOptions& options);

    /// Finalises the interpreter and releases the process slot and the lease.
    ~PythonInterpreterState();

    PythonInterpreterState(const PythonInterpreterState&) = delete;
    PythonInterpreterState& operator=(const PythonInterpreterState&) = delete;
    PythonInterpreterState(PythonInterpreterState&&) = delete;
    PythonInterpreterState& operator=(PythonInterpreterState&&) = delete;

    /// Runs source in the interpreter's `__main__` module. See `PythonRuntimeAdapter::execute`.
    [[nodiscard]] Status execute(const ExecutionContext& execution, std::string_view source);

    /// Calls a function over two borrowed buffers. See `PythonRuntimeAdapter::callImageFunction`.
    [[nodiscard]] Status callImageFunction(const ExecutionContext& execution, const PythonImageCall& call);

    /// The runtime version the loaded library reports.
    [[nodiscard]] PythonRuntimeVersion runtimeVersion() const noexcept
    {
        return m_runtimeVersion;
    }

    /// Which engine context this state holds open.
    [[nodiscard]] std::uint64_t engineContextId() const noexcept
    {
        return m_lease.contextId();
    }

    /// How many calls are in progress right now, inside the interpreter or waiting for its lock.
    [[nodiscard]] std::uint64_t callsInFlight() const noexcept
    {
        return m_callsInFlight.load(std::memory_order_acquire);
    }

    /// What this state has done since it started.
    [[nodiscard]] PythonCallCounters counters() const noexcept;

private:
    /// Takes the lease and the policy a successful start has already resolved.
    PythonInterpreterState(EngineContextLease lease, PythonCancellationPolicy cancellation,
                           PythonRuntimeVersion runtimeVersion) noexcept;

    /// Starts the interpreter on its lifetime-owning thread and waits for its answer.
    [[nodiscard]] Status initialize(const PythonRuntimeOptions& options);

    /// Runs one body inside the interpreter with the lock held, the counters moved and cancellation
    /// applied as this state's policy states it.
    ///
    /// Every call into Python goes through here, so there is one place where the boundary checks, the
    /// lock, the trace hook and the error translation happen, rather than one per entry point. A body
    /// that recognised its own failure fills in the optional `RecognisedFailure`, and the call then
    /// reports that instead of whatever the interpreter happens to be holding.
    template <typename BodyT>
    [[nodiscard]] Status withInterpreter(const ExecutionContext& execution, const char* operation, BodyT&& body,
                                         RecognisedFailure* recognised = nullptr);

    /// The hold on the engine context, which is what stops it being shut down under the interpreter.
    EngineContextLease m_lease;

    /// When a cancellation request reaches the interpreter.
    PythonCancellationPolicy m_cancellation = PythonCancellationPolicy::AtBoundary;

    /// The version the loaded runtime reports, resolved once at startup.
    PythonRuntimeVersion m_runtimeVersion{};

    /// The thread state the interpreter-owning thread left saved while calls may enter.
    void* m_savedThreadState = nullptr;

    /// The thread that initializes and finalizes CPython, which requires both on one OS thread.
    std::thread m_ownerThread;

    /// Coordinates the synchronous startup answer and the eventual finalization request.
    std::mutex m_ownerMutex;
    std::condition_variable m_ownerChanged;
    Status m_startupStatus;
    bool m_startupComplete = false;
    bool m_stopOwner = false;

    /// Whether this state was published into the process-wide adapter seat.
    bool m_ownsSeat = false;

    /// How many calls are in progress, counted before the interpreter lock is taken, so a shutdown
    /// can refuse.
    std::atomic<std::uint64_t> m_callsInFlight{0u};

    /// The counters, each moved once per call that reaches its case.
    std::atomic<std::uint64_t> m_calls{0u};
    std::atomic<std::uint64_t> m_sharedBytes{0u};
    std::atomic<std::uint64_t> m_bridgeRefusals{0u};
    std::atomic<std::uint64_t> m_cancellations{0u};
    std::atomic<std::uint64_t> m_pythonExceptions{0u};
};

/// A failure in this adapter's own vocabulary.
///
/// One place that attaches the typed reason field, so no call site spells the key and the kind
/// together and gets one of them wrong.
///
/// @param code what kind of failure it is.
/// @param reason which of this adapter's rules refused.
/// @param operation the adapter operation that failed, spelled as it is in the source.
/// @param message a sentence for a person.
/// @return the failure, in `ErrorDomain::Interop`, carrying the reason as typed context.
[[nodiscard]] Status pythonAdapterFailure(ErrorCode code, PythonAdapterReason reason, const char* operation,
                                          std::string message);

} // namespace an
