// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One admitted operation as Controls runs it: the consumer runtime's operation scope, with sinks that carry what the
/// engine reports to the GUI thread, and the one terminal report its observer receives.
///
/// The engine reports progress and diagnostics on whatever thread it is using, through sinks that must not throw,
/// with strings it lends for the call only. A dialog wants them on the GUI thread, after the call has returned, and
/// not at all once the dialog is gone. An execution scope is the adapter between the two:
///
/// - **Every payload is owned before the sink returns.** A progress report's stage and a diagnostic's operation and
///   message are copied into the scope's own records; nothing the engine lent is read after the call.
/// - **Delivery is bounded.** Progress is coalesced: only the newest report not yet delivered is kept. Diagnostics
///   wait in a queue of a stated capacity, and one that does not fit is dropped and counted. At most one delivery is
///   queued to the GUI thread at a time, however fast the engine reports.
/// - **The terminal report comes once, last, from the worker's result.** `finish` ends the operation - its threads
///   go back to the session before anyone is told - and states the result; a scope destroyed without one reports
///   that it ended without a result. Whatever was still queued is delivered before it.
/// - **A destroyed receiver receives nothing.** The observer names its receiver through a guarded reference, and a
///   delivery that finds it gone is discarded and counted rather than run.
/// - **Lost telemetry never fails the work.** A copy that cannot be made, a queue that is full and a delivery that
///   cannot be queued are counted, and the operation goes on.
///
/// Admission is the session's: a scope is admitted with a purpose - preview, apply, batch, workflow stage or
/// background - and the session decides how many of its threads it is granted. `admit` waits and is for a worker
/// thread; `tryAdmit` never waits.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QObject>
#include <QPointer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace an::ui
{

class ImageReads;

/// How many diagnostics a scope keeps waiting for delivery by default.
inline constexpr std::size_t kDefaultDiagnosticCapacity = 256u;

/// One progress report, owned.
struct ExecutionProgress
{
    /// The operation it is about.
    std::uint64_t operationId = 0u;

    /// Its position among the operation's callbacks, shared with diagnostics.
    std::uint64_t sequence = 0u;

    /// The units of work finished.
    std::uint64_t completed = 0u;

    /// The units of work expected; zero when not yet known.
    std::uint64_t total = 0u;

    /// What the operation is doing, for a person; never parsed.
    std::string stage;
};

/// One diagnostic, owned.
struct ExecutionDiagnostic
{
    /// How much it matters.
    an::DiagnosticSeverity severity = an::DiagnosticSeverity::Info;

    /// Which part of the engine spoke.
    an::ErrorDomain domain = an::ErrorDomain::Unspecified;

    /// The operation it came from.
    std::uint64_t operationId = 0u;

    /// Its position among the operation's callbacks, shared with progress.
    std::uint64_t sequence = 0u;

    /// The engine operation speaking.
    std::string operation;

    /// A sentence for a person; never parsed.
    std::string message;
};

/// What became of an execution's reports.
struct ExecutionTelemetry
{
    /// Progress reports the engine made.
    std::uint64_t progressReceived = 0u;

    /// Progress reports replaced by a newer one before they were delivered. Coalescing, not loss.
    std::uint64_t progressCoalesced = 0u;

    /// Progress reports that could not be copied or queued.
    std::uint64_t progressDropped = 0u;

    /// Progress reports handed to the observer.
    std::uint64_t progressDelivered = 0u;

    /// Diagnostics the engine reported.
    std::uint64_t diagnosticsReceived = 0u;

    /// Diagnostics dropped because the queue was full or a copy could not be made.
    std::uint64_t diagnosticsDropped = 0u;

    /// Diagnostics handed to the observer.
    std::uint64_t diagnosticsDelivered = 0u;

    /// Reports - progress, diagnostics and the terminal one - discarded because the receiver had been destroyed or
    /// no delivery could be queued.
    std::uint64_t discarded = 0u;

    /// Deliveries queued to the GUI thread. One is queued only when none is waiting, so a burst of reports costs one.
    std::uint64_t deliveriesQueued = 0u;
};

/// The terminal report.
struct ExecutionOutcome
{
    /// The operation it ends.
    std::uint64_t operationId = 0u;

    /// Why the operation ran.
    an::consumer::OperationPurpose purpose = an::consumer::OperationPurpose::Apply;

    /// What the worker said the operation came to; `ErrorCode::InvalidState` for a scope that ended without saying.
    an::Status status;

    /// What became of the reports, as of the terminal report.
    ExecutionTelemetry telemetry{};
};

/// Where an execution's reports go. Every callback runs on the GUI thread, and only while `receiver` lives.
///
/// Built on the GUI thread, then handed with the request to whatever admits the execution. A callback left empty is
/// not called.
struct ExecutionObserver
{
    /// The object the reports are for - a dialog, a control. Guarded: a delivery that finds it destroyed is discarded.
    QPointer<QObject> receiver;

    /// Receives the newest progress not yet delivered.
    std::function<void(const ExecutionProgress&)> progress;

    /// Receives each diagnostic that was kept, in the order the engine reported them.
    std::function<void(const ExecutionDiagnostic&)> diagnostic;

    /// Receives the terminal report, once, after everything else.
    std::function<void(const ExecutionOutcome&)> finished;
};

/// What an execution asks of its session.
struct ExecutionRequest
{
    /// Why it runs.
    an::consumer::OperationPurpose purpose = an::consumer::OperationPurpose::Apply;

    /// The most threads it can use; zero asks for as many as its purpose may have.
    std::uint32_t maxConcurrency = 0u;

    /// Stops the request while it is queued and the operation once it runs - a commit ticket's, usually.
    std::stop_token stopToken{};

    /// When the request stops waiting, and when the operation's checkpoints report it late.
    std::optional<std::chrono::steady_clock::time_point> deadline{};

    /// The block size of a scratch arena for the operation; zero gives it none.
    std::uint64_t scratchBlockBytes = 0u;

    /// How many diagnostics may wait for delivery; one that does not fit is dropped and counted.
    std::size_t diagnosticCapacity = kDefaultDiagnosticCapacity;

    /// The least severe diagnostic also written to the application's log; nothing is logged when absent.
    std::optional<an::DiagnosticSeverity> logFrom = an::DiagnosticSeverity::Warning;

    /// The images the run leased before it asked to be admitted, which its body reads through `readBeside`; null when
    /// it named none. Borrowed: the run keeps them for as long as the operation lives.
    const ImageReads* reads = nullptr;
};

/// One admitted operation with GUI sinks.
///
/// Move-only, and moving keeps everything the engine borrows where it is. One thread owns a scope; the sinks it gives
/// the engine are called from any of the operation's threads, and the observer's callbacks run on the GUI thread.
class ExecutionScope
{
public:
    /// A scope that holds nothing.
    AFTERNIGHTCONTROLS_API ExecutionScope() noexcept;

    /// Ends the operation and, when `finish` was never called, delivers the terminal report saying so.
    AFTERNIGHTCONTROLS_API ~ExecutionScope();

    /// Takes over another scope.
    ///
    /// @param other the scope to take over.
    AFTERNIGHTCONTROLS_API ExecutionScope(ExecutionScope&& other) noexcept;

    /// Ends this scope as its destructor would and takes over another.
    ///
    /// @param other the scope to take over.
    /// @return this scope.
    AFTERNIGHTCONTROLS_API ExecutionScope& operator=(ExecutionScope&& other) noexcept;

    ExecutionScope(const ExecutionScope&) = delete;
    ExecutionScope& operator=(const ExecutionScope&) = delete;

    /// Admits an execution, waiting in the session's queue until its purpose has a thread free. For a worker thread.
    ///
    /// @param session the process's session, from `IEngineSessionProvider`.
    /// @param request what the execution asks for.
    /// @param observer where its reports go.
    /// @return the scope, or what `EngineSession::admit` refuses. Nothing is delivered for a refused admission.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<ExecutionScope>
    admit(const an::consumer::EngineSession& session, const ExecutionRequest& request, ExecutionObserver observer);

    /// Admits an execution only if its purpose has a thread free now. Never blocks.
    ///
    /// @param session the process's session.
    /// @param request what the execution asks for.
    /// @param observer where its reports go.
    /// @return the scope, or what `EngineSession::tryAdmit` refuses.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<ExecutionScope>
    tryAdmit(const an::consumer::EngineSession& session, const ExecutionRequest& request, ExecutionObserver observer);

    /// Whether this scope holds an execution that has not finished.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool valid() const noexcept;

    /// Why the execution runs.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::consumer::OperationPurpose purpose() const noexcept;

    /// How many threads admission granted; zero for a scope that holds nothing.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint32_t grantedConcurrency() const noexcept;

    /// The engine's identity for the operation; zero for a scope that holds nothing.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t operationId() const noexcept;

    /// The session that admitted the operation, which an asset the operation makes belongs to. Calling it on a scope
    /// that holds nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const an::consumer::EngineSession& session() const noexcept;

    /// The engine context the operation runs in: the session's. Calling it on a scope that holds nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const an::EngineContext& engine() const noexcept;

    /// The execution the engine runs the operation under. Calling it on a scope that holds nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const an::ExecutionContext& execution() const noexcept;

    /// Asks the operation to stop at its next checkpoint.
    AFTERNIGHTCONTROLS_API void requestStop() noexcept;

    /// Whether the operation has been asked to stop, through this scope or through the request's token.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool stopRequested() const noexcept;

    /// What has become of the reports so far. A diagnostic: another thread may change it before the caller reads it.
    [[nodiscard]] AFTERNIGHTCONTROLS_API ExecutionTelemetry telemetry() const;

    /// The images the run leased before it was admitted, as its request stated them; null when it named none.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const ImageReads* reads() const noexcept;

    /// Ends the operation - releasing its threads, scratch and admission slot - and delivers the terminal report with
    /// `status`, after everything still queued. Finishing twice finishes once.
    ///
    /// @param status what the operation came to.
    AFTERNIGHTCONTROLS_API void finish(an::Status status) noexcept;

private:
    class State;

    explicit ExecutionScope(std::unique_ptr<State> state) noexcept;

    [[nodiscard]] static an::Result<ExecutionScope> admitWith(const an::consumer::EngineSession& session,
                                                              const ExecutionRequest& request,
                                                              ExecutionObserver observer, bool wait);

    std::unique_ptr<State> m_state;
};

} // namespace an::ui
