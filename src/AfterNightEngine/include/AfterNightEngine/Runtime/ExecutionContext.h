// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One operation's policy, its permission to run, and its way to stop.
///
/// An engine context is what the component owns. An execution context is what one operation is
/// allowed to do with it, and every non-trivial algorithm receives one. It carries no state of its
/// own beyond the answers: how it may be cancelled, how much of the machine it may use, which
/// implementation it should prefer, where it may say something, and where its scratch memory comes
/// from.
///
/// Four decisions shape it.
///
/// **Cancellation is a `std::stop_token`, not a pointer to a bool.** A raw `bool*` carried across
/// threads is a data race and a lifetime nobody can reason about, in one member. A stop
/// token is a shared handle whose stop state outlives every observer and whose reads and writes are
/// defined under concurrency, so passing one to a nested operation is correct rather than hopeful.
///
/// **It holds a lease on its engine context.** That is what makes "cannot outlive its EngineContext"
/// a fact rather than a rule: while an execution context exists the engine context is not quiescent
/// and refuses to shut down, and nobody had to remember to say so.
///
/// **It may narrow and may not widen.** An operation asking for fewer threads than the user's
/// setting is legitimate - a preview should not take the machine while a batch runs. An operation
/// asking for more is asking the engine to exceed a setting the user chose, and is refused.
///
/// **It reports through interfaces the caller supplied, or says nothing.** No stream, no global log,
/// no GUI callback. A build given no sink is silent, and the helpers here mean an algorithm reports
/// unconditionally instead of writing the same null check everywhere.
///
/// It is cheap to pass by reference, allocates nothing after construction, and every accessor is
/// safe to read from several threads at once - which is the ordinary case, because the threads
/// running one operation share one of these. One opaque operation-local state serializes callbacks,
/// keeps progress monotonic and holds counters without putting a mutex in this published header.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>

namespace an
{

/// Where an operation's progress is delivered. Defined in `Runtime/ProgressSink.h`.
///
/// Named here rather than included, because an execution context only holds a pointer to one: a
/// header that receives an execution context should not have to carry the reporting vocabulary in
/// order to compile. Include that header to implement a sink or to name one.
class ProgressSink;

/// Where an operation's diagnostics are delivered. Defined in `Runtime/DiagnosticSink.h`.
///
/// Named here rather than included, for the same reason as `ProgressSink`.
class DiagnosticSink;

/// Where an operation's trace events are delivered. Defined in `Runtime/TraceSink.h`.
///
/// Named here rather than included, for the same reason as the other sinks.
class TraceSink;

/// How much a diagnostic matters. Defined in `Runtime/DiagnosticSink.h`.
///
/// Declared here with its underlying type so that the reporting helper below can take one by value
/// without this header pulling in the whole diagnostic vocabulary. A caller naming a severity
/// includes that header, which defines the same enumeration.
enum class DiagnosticSeverity : std::uint8_t;

/// What a trace event represents. Defined in `Runtime/TraceSink.h`.
///
/// Declared here with its underlying type so the reporting helper can take one by value without
/// pulling the trace vocabulary into every algorithm header.
enum class TraceEventKind : std::uint8_t;

/// Stable counts for callbacks accepted by an operation. Defined in `Runtime/TraceSink.h`.
///
/// Include that header to inspect the value returned by `ExecutionContext::operationCounters`.
struct OperationCounters;

/// The callback gate, monotonic-progress state, and counters for one operation.
///
/// Defined only inside the engine so its mutex and clock bookkeeping do not enter a public header.
/// An execution context owns it through a shared pointer so that the context remains movable and
/// so that the engine's own per-stage executions of one operation share one order; it is never
/// shared between two operations.
class OperationTelemetryState;

/// Where the engine context's persistent memory comes from. Defined in `Runtime/MemoryResource.h`.
///
/// Named here rather than included, for the same reason as the sinks: an execution context hands
/// out a pointer to one and a header that receives an execution context should not have to carry
/// the allocator's vocabulary to compile.
class MemoryResource;

/// An operation's scratch memory. Defined in `Runtime/ScratchArena.h`.
///
/// An execution context holds a pointer to one and never owns it: the arena belongs to whatever set
/// the execution up, is released in full when that owner destroys it however the execution ended,
/// and must outlive every execution context it was given to. The type itself is not part of this
/// header, because what it offers is that header's to state.
class ScratchArena;

/// The concurrency request that means "as much as the engine context permits".
///
/// A request rather than a number, like the engine context's own host-concurrency request: what an
/// execution context reports afterwards is what it resolved to, which is at least one.
inline constexpr std::uint32_t kUseContextConcurrency = 0u;

/// Everything a caller states when starting one operation.
///
/// A plain aggregate with a default for every member, so a caller names only what it is changing:
///
/// @code
/// an::ExecutionOptions options;
/// options.stopToken = source.get_token();
/// options.maxConcurrency = 2u;
/// an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
/// @endcode
///
/// The defaults are the ordinary case: no way to stop, no deadline, no sinks, no scratch, the
/// engine context's full concurrency, and the engine's own choice of backend.
struct ExecutionOptions
{
    /// How the caller may stop this operation.
    ///
    /// A default-constructed token has no stop state, which is what an operation nobody can
    /// interrupt is given. A token shared with other executions stops all of them together, which
    /// is how one user action stops a whole workflow.
    std::stop_token stopToken{};

    /// When this operation's time budget runs out, as an absolute point on the steady clock.
    ///
    /// A caller with a duration budget passes `std::chrono::steady_clock::now() + budget`; a caller
    /// running several operations under one overall budget passes the same point to each. Absent
    /// means no deadline. A point already in the past is a legitimate request and is exceeded
    /// immediately.
    std::optional<std::chrono::steady_clock::time_point> deadline{};

    /// Where progress is delivered, or null for an operation that reports none.
    ///
    /// Borrowed, never owned: it must outlive the execution context.
    ProgressSink* progressSink = nullptr;

    /// Where diagnostics are delivered, or null for an operation that says nothing.
    ///
    /// Borrowed, never owned: it must outlive the execution context.
    DiagnosticSink* diagnosticSink = nullptr;

    /// Where machine-readable trace events are delivered, or null for an untraced operation.
    ///
    /// Borrowed, never owned: it must outlive the execution context.
    TraceSink* traceSink = nullptr;

    /// The scratch memory this operation may use, or null when it was given none.
    ///
    /// Borrowed, never owned: it must outlive the execution context.
    ScratchArena* scratch = nullptr;

    /// The most threads this operation may use, or `kUseContextConcurrency` for everything the
    /// engine context permits.
    ///
    /// May be lower than the engine context's maximum and may not be higher: construction fails
    /// with `ErrorCode::LimitExceeded` for a request that would widen it.
    std::uint32_t maxConcurrency = kUseContextConcurrency;

    /// Which implementation this operation should use.
    ComputeBackend backend = ComputeBackend::Automatic;

    /// Which instruction set inside the CPU backend this operation should use.
    ///
    /// `InstructionSet::Automatic` lets the dispatch table choose the best one this host can run
    /// and this build contains, which is the ordinary case. Naming one is what a conformance run
    /// does: construction fails with `ErrorCode::UnsupportedBackend` when the named set cannot be
    /// forced here and no fallback is permitted, and narrows to `Automatic` when one is.
    ///
    /// Ignored when `backend` is `ComputeBackend::Reference`, which is scalar by definition.
    InstructionSet instructionSet = InstructionSet::Automatic;

    /// What to do when the requested backend cannot serve this operation.
    BackendFallback backendFallback = BackendFallback::Allowed;

    /// Which of this operation's reductions must be thread-count independent.
    ReductionDeterminism reductionDeterminism = ReductionDeterminism::DecisionFeeding;
};

/// One operation's policy and control.
///
/// Built through `create`, which validates what it is given against the engine context it will run
/// in. Move-only, because it holds a lease and a copy would be a second hold nobody asked for.
///
/// Every accessor is immutable and safe to call from several threads at once, which is the ordinary
/// case: the threads running one operation share one execution context by reference. What is not
/// safe, as for any other object, is destroying or moving one while another thread is using it.
class ExecutionContext
{
public:
    /// An execution context that holds nothing.
    ///
    /// The state one is left in by a move, and a usable default for a member assigned later. It
    /// holds no lease, reports nothing, and is never cancelled; `valid()` is how to ask.
    ExecutionContext() noexcept = default;

    /// Releases the lease on the engine context, which is what lets that context be shut down.
    ~ExecutionContext() = default;

    /// Takes over another execution's lease and policy, leaving it holding nothing.
    ///
    /// Allocation-free. The lease moves rather than being taken again, so the engine context's
    /// count is unchanged.
    ///
    /// @param other the execution context to take over. Left exactly as a default-constructed one:
    ///        no lease, no policy, no sinks, and no way to be cancelled.
    ExecutionContext(ExecutionContext&& other) noexcept
    {
        takeFrom(other);
    }

    /// Releases this execution's lease and takes over another's.
    ///
    /// Allocation-free.
    ///
    /// @param other the execution context to take over. Left exactly as a default-constructed one.
    /// @return this execution context.
    ExecutionContext& operator=(ExecutionContext&& other) noexcept
    {
        if (this != &other)
        {
            takeFrom(other);
        }
        return *this;
    }

    /// Copying is deleted: a copy would hold the engine context open a second time without any
    /// call site asking for it.
    ExecutionContext(const ExecutionContext&) = delete;

    /// Copying is deleted. See the copy constructor.
    ExecutionContext& operator=(const ExecutionContext&) = delete;

    /// Starts one operation in an engine context, or reports why it cannot.
    ///
    /// Validates the policy and any scratch binding before taking a lease, so an invalid request is
    /// not hidden by a busy context. An execution context that exists holds a lease, and a refused
    /// execution leaves no lease behind.
    ///
    /// Thread-safe: several threads may start operations in one engine context at the same time.
    /// Allocates one small operation-local telemetry state in addition to the scheduler grant.
    ///
    /// @param engine the engine context this operation runs in. Must outlive nothing - the lease
    ///        keeps its services alive - but it must be an engine context that holds something.
    /// @param options what the caller is asking for. Copied; the sinks and scratch it names are
    ///        borrowed and must outlive the returned execution context.
    /// @return the execution context, or a failure. `ErrorCode::NotInitialized` when `engine` holds
    ///         nothing, carrying the lease refusal as its cause; `ErrorCode::InvalidArgument` when
    ///         `scratch` belongs to another context; `ErrorCode::LimitExceeded` when
    ///         `maxConcurrency` is above the engine context's, carrying the request and the limit
    ///         as typed context; `ErrorCode::Busy` when the concurrency budget is exhausted;
    ///         `ErrorCode::UnsupportedBackend` when no fallback is permitted and either the
    ///         requested backend is not in this build or the requested instruction set cannot be
    ///         forced on this host and build; `ErrorCode::AllocationFailed` when admission or the
    ///         operation-local callback state cannot be allocated.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ExecutionContext> create(const EngineContext& engine,
                                                                              const ExecutionOptions& options);

    /// Whether this execution context holds a lease on an engine context.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return false for a default-constructed one and for one that has been moved from.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_lease.valid();
    }

    /// This operation's identity, for correlating its progress, diagnostics and traces.
    ///
    /// Unique among every operation this process starts, and never reused. It is what lets one sink
    /// serve several operations at once.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the identity, or `0` when this execution context holds nothing.
    [[nodiscard]] std::uint64_t operationId() const noexcept
    {
        return m_operationId;
    }

    /// Which engine context this operation runs in.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the engine context's identity, or `0` when this execution context holds nothing.
    [[nodiscard]] std::uint64_t engineContextId() const noexcept
    {
        return m_lease.contextId();
    }

    /// The most threads this operation may use at once.
    ///
    /// What construction resolved, so it is a number rather than a request: never
    /// `kUseContextConcurrency`, and never above the engine context's own maximum. A ceiling rather
    /// than a demand.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the resolved maximum, or `0` when this execution context holds nothing.
    [[nodiscard]] std::uint32_t maxConcurrency() const noexcept
    {
        return m_maxConcurrency;
    }

    /// The most threads this operation will actually be given.
    ///
    /// A different number from `maxConcurrency()`, and the difference is what separates a policy
    /// from a division. The ceiling is what the caller stated and never moves; this is what the
    /// engine context's budget had to give when this operation started, which depends on what else
    /// was running then. It is at most the ceiling and at least one, and it is what the scheduler
    /// uses.
    ///
    /// Fixed for the life of the operation. Threads freed by other work finishing do not widen it:
    /// an operation that grew halfway through would be an operation whose partition depended on
    /// when it was asked.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the granted share, or `0` when this execution context holds nothing.
    [[nodiscard]] std::uint32_t admittedConcurrency() const noexcept
    {
        return m_lease.admittedConcurrency();
    }

    /// The configured maxima and memory budget this operation works within.
    ///
    /// Copied from the engine context, where they are immutable, so an algorithm validating its own
    /// geometry reaches them through the context it was handed rather than through a second
    /// parameter or a global.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the limits, or a value with every field zero when this execution context holds
    ///         nothing.
    [[nodiscard]] EngineLimits limits() const noexcept
    {
        return m_limits;
    }

    /// The backend the caller asked for.
    ///
    /// Kept beside the resolved one so that a caller can see its preference was not met, and so a
    /// benchmark manifest can record what was asked as well as what ran.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the requested backend.
    [[nodiscard]] ComputeBackend requestedBackend() const noexcept
    {
        return m_requestedBackend;
    }

    /// The backend this operation is constrained to.
    ///
    /// `ComputeBackend::Automatic` means unconstrained - the dispatch table chooses - which is also
    /// what a request this build cannot serve becomes when fallback is permitted. Anything else is
    /// a backend the operation must use.
    ///
    /// This resolves only what the build can answer: whether the requested backend exists in this
    /// binary at all. Whether a host can actually run it is the dispatch table's question.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the backend constraint, or `ComputeBackend::Automatic` when there is none.
    [[nodiscard]] ComputeBackend backend() const noexcept
    {
        return m_backend;
    }

    /// The instruction set the caller asked for.
    ///
    /// Kept beside the resolved one for the same reason `requestedBackend()` is: a benchmark
    /// manifest records what was asked as well as what ran, and a caller can see that its
    /// preference was not met.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the requested instruction set.
    [[nodiscard]] InstructionSet requestedInstructionSet() const noexcept
    {
        return m_requestedInstructionSet;
    }

    /// The instruction set this operation is constrained to.
    ///
    /// `InstructionSet::Automatic` means unconstrained - the dispatch table chooses - which is also
    /// what a request this host or this build cannot serve becomes when fallback is permitted.
    /// Anything else is a set the operation must run on.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the constraint, or `InstructionSet::Automatic` when there is none.
    [[nodiscard]] InstructionSet instructionSet() const noexcept
    {
        return m_instructionSet;
    }

    /// What happens when the requested backend cannot serve this operation.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the fallback policy the caller stated.
    [[nodiscard]] BackendFallback backendFallback() const noexcept
    {
        return m_backendFallback;
    }

    /// Which of this operation's reductions must be thread-count independent.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the rule the caller stated.
    [[nodiscard]] ReductionDeterminism reductionDeterminism() const noexcept
    {
        return m_reductionDeterminism;
    }

    /// The token this operation is stopped through.
    ///
    /// Handed out so that an operation starting a nested one passes it on rather than inventing a
    /// second stop state. Copying a token is cheap and the copy observes the same stop request.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the token, which has no stop state at all when the caller gave none.
    [[nodiscard]] std::stop_token stopToken() const noexcept
    {
        return m_stopToken;
    }

    /// When this operation's time budget runs out.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the deadline, or an empty optional when the caller set none.
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> deadline() const noexcept
    {
        return m_deadline;
    }

    /// Whether the caller has asked this operation to stop.
    ///
    /// Thread-safe and allocation-free; safe to call while another thread requests the stop.
    ///
    /// @return true once a stop has been requested through the token this was given.
    [[nodiscard]] bool stopRequested() const noexcept
    {
        return m_stopToken.stop_requested();
    }

    /// Whether this operation's time budget has run out.
    ///
    /// Reads the steady clock, so two calls a moment apart may answer differently.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return true when a deadline was set and is now in the past; false when none was set.
    [[nodiscard]] bool deadlineExceeded() const noexcept
    {
        return m_deadline.has_value() && std::chrono::steady_clock::now() >= *m_deadline;
    }

    /// What an operation calls at a cancellation checkpoint.
    ///
    /// The one call that answers both questions and tells them apart: a caller that pressed stop
    /// and a caller whose time budget elapsed want different words back. A stop request is reported
    /// in preference to an elapsed deadline when both are true, because a person deciding to stop
    /// is the more informative of the two.
    ///
    /// Allocation-free, which is why it is worth calling often: the failure it returns carries only
    /// its code. The operation that gives up turns that into a described failure, because it is the
    /// one that knows what it was doing.
    ///
    /// Thread-safe: safe to call from every thread running the operation, while another thread
    /// requests the stop.
    ///
    /// @return success, `ErrorCode::Cancelled` when a stop has been requested, or
    ///         `ErrorCode::DeadlineExceeded` when the time budget has run out.
    [[nodiscard]] Status checkCancellation() const noexcept
    {
        if (stopRequested())
        {
            return Status::failure(ErrorCode::Cancelled);
        }
        if (deadlineExceeded())
        {
            return Status::failure(ErrorCode::DeadlineExceeded);
        }
        return Status::success();
    }

    /// Where this operation's progress goes.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the sink the caller supplied, or null. Borrowed, never owned.
    [[nodiscard]] ProgressSink* progressSink() const noexcept
    {
        return m_progressSink;
    }

    /// Where this operation's diagnostics go.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the sink the caller supplied, or null. Borrowed, never owned.
    [[nodiscard]] DiagnosticSink* diagnosticSink() const noexcept
    {
        return m_diagnosticSink;
    }

    /// Where this operation's traces go.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the sink the caller supplied, or null. Borrowed, never owned.
    [[nodiscard]] TraceSink* traceSink() const noexcept
    {
        return m_traceSink;
    }

    /// This operation's scratch memory.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the arena the caller supplied, or null when it supplied none. Borrowed, never owned.
    [[nodiscard]] ScratchArena* scratch() const noexcept
    {
        return m_scratch;
    }

    /// Where this operation's persistent memory comes from.
    ///
    /// Its engine context's resource, reached through the lease this execution already holds, so an
    /// algorithm that needs a buffer outliving one tile asks the object it was handed rather than
    /// taking a second parameter or reaching for a global. Scratch is the other question and is
    /// `scratch()` above.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the engine context's resource, or null when this execution context holds nothing.
    ///         Borrowed, never owned.
    [[nodiscard]] AFTERNIGHTENGINE_API MemoryResource* memoryResource() const noexcept;

    /// A stable snapshot of this operation's delivered callbacks and discarded regressions.
    ///
    /// The snapshot is taken under the same operation-local gate as callback delivery, so it never
    /// describes half of a callback. Counts only increase and saturate rather than wrap. Calling it
    /// from inside one of this operation's sink callbacks is forbidden, as is every callback into
    /// the reporting execution context, because that would wait on the gate already held.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the counters, carrying this operation's identity; all-zero for a context that holds
    ///         nothing. The complete type is declared in `Runtime/TraceSink.h`.
    [[nodiscard]] AFTERNIGHTENGINE_API OperationCounters operationCounters() const noexcept;

    /// Reports progress, if anybody is listening.
    ///
    /// Does nothing when no progress sink was supplied, which is why an algorithm calls this
    /// unconditionally instead of writing the same null check at every checkpoint. The report
    /// carries this operation's identity.
    ///
    /// Called at documented coarse checkpoints - a frame, a stage, a tile group - never per pixel.
    ///
    /// Thread-safe and allocation-free. Calls for one operation are serialized with its diagnostic
    /// and trace callbacks. A report whose completed count is below the last accepted one is
    /// discarded and counted, so a sink never observes progress move backwards. Equal completed
    /// counts are delivered because a stage or total may have changed. While the engine runs a
    /// nested operation on this execution, that operation's reports are first mapped into the share
    /// of this operation's total it was given, and the comparison is made on that scale.
    ///
    /// @param completed how many units of work are finished.
    /// @param total how many units the operation expects to finish; zero when it does not know.
    /// @param stage what the operation is doing, for a person. Must be null-terminated and remain
    ///        valid for the duration of this call; a null pointer is reported as an empty string.
    AFTERNIGHTENGINE_API void reportProgress(std::uint64_t completed, std::uint64_t total,
                                             const char* stage = "") const noexcept;

    /// Reports something worth saying, if anybody is listening.
    ///
    /// Does nothing when no diagnostic sink was supplied. The event carries this operation's
    /// identity. For something that is worth saying while the operation continues - an operation
    /// that is giving up returns a `Status` instead.
    ///
    /// Thread-safe and allocation-free. Calls for one operation are serialized with its progress
    /// and trace callbacks and receive a sequence from that common order.
    ///
    /// @param severity how much this matters.
    /// @param domain which part of the engine is speaking.
    /// @param operation the engine operation speaking, spelled as it is in the source. Must remain
    ///        valid for the duration of this call; null is reported as an empty string.
    /// @param message a sentence for a person, never parsed. Must remain valid for the duration of
    ///        this call; null is reported as an empty string.
    AFTERNIGHTENGINE_API void reportDiagnostic(DiagnosticSeverity severity, ErrorDomain domain, const char* operation,
                                               const char* message) const noexcept;

    /// Reports a machine-readable trace event, if anybody is listening.
    ///
    /// Does nothing when no trace sink was supplied. The execution context supplies the operation
    /// identity, sequence, and elapsed steady-clock time; an algorithm supplies stable category,
    /// name, value, and unit data. Span matching is the reporting algorithm's responsibility.
    ///
    /// Thread-safe and allocation-free. Calls for one operation are serialized with its progress
    /// and diagnostic callbacks and receive a sequence from that common order.
    ///
    /// @param kind what the event represents.
    /// @param category stable subsystem or activity family. Must remain valid for the duration of
    ///        this call; null is reported as an empty string.
    /// @param name stable event or counter name. Must remain valid for the duration of this call;
    ///        null is reported as an empty string.
    /// @param value current numeric value for a counter; ignored for other kinds, which report zero.
    /// @param unit stable unit for a counter value. Must remain valid for the duration of this call;
    ///        null is reported as an empty string, and other kinds report it empty.
    AFTERNIGHTENGINE_API void reportTrace(TraceEventKind kind, const char* category, const char* name,
                                          double value = 0.0, const char* unit = "") const noexcept;

private:
    /// The engine's own way to reach the services this operation runs on, through the lease it
    /// holds. See the declarations of `EngineContextState` and `OperationArena` in
    /// `Runtime/EngineContext.h`.
    friend class EngineContextState;

    /// The engine's scoped mapping of nested progress, without a second operation or thread grant.
    friend class ProgressRange;

    /// The engine's own way to reach the threads this operation was granted, for the same reason.
    friend class OperationArena;

    /// Takes ownership of a lease the factory has already acquired, with the policy it validated.
    ///
    /// @param lease the hold on the engine context, moved in.
    /// @param options what the caller asked for, with the sinks and scratch it named.
    /// @param telemetry the operation-local callback gate and counters, moved in.
    /// @param operationId this operation's identity.
    /// @param maxConcurrency the resolved maximum, at least one.
    /// @param backend the resolved backend constraint.
    /// @param instructionSet the resolved instruction-set constraint.
    /// @param limits the engine context's configured limits.
    ExecutionContext(EngineContextLease lease, const ExecutionOptions& options,
                     std::shared_ptr<OperationTelemetryState> telemetry, std::uint64_t operationId,
                     std::uint32_t maxConcurrency, ComputeBackend backend, InstructionSet instructionSet,
                     const EngineLimits& limits) noexcept
        : m_lease(std::move(lease)), m_stopToken(options.stopToken), m_deadline(options.deadline),
          m_progressSink(options.progressSink), m_diagnosticSink(options.diagnosticSink),
          m_traceSink(options.traceSink), m_telemetry(std::move(telemetry)), m_scratch(options.scratch),
          m_limits(limits), m_operationId(operationId), m_maxConcurrency(maxConcurrency),
          m_requestedBackend(options.backend), m_backend(backend), m_requestedInstructionSet(options.instructionSet),
          m_instructionSet(instructionSet), m_backendFallback(options.backendFallback),
          m_reductionDeterminism(options.reductionDeterminism)
    {
    }

    /// Takes everything from another execution context and leaves it empty.
    ///
    /// Written out rather than defaulted, because a defaulted move leaves the source's scalars
    /// exactly where they were: the lease would move and the operation identity, the concurrency
    /// and the policy would be readable through a context that no longer holds anything. Every
    /// member is therefore exchanged for the value a default-constructed one carries, so "moved
    /// from" and "holds nothing" are the same state rather than two states that mostly agree.
    ///
    /// @param other the execution context to take from. Left as a default-constructed one.
    void takeFrom(ExecutionContext& other) noexcept
    {
        m_lease = std::move(other.m_lease);
        m_stopToken = std::exchange(other.m_stopToken, std::stop_token{});
        m_deadline = std::exchange(other.m_deadline, std::nullopt);
        m_progressSink = std::exchange(other.m_progressSink, nullptr);
        m_diagnosticSink = std::exchange(other.m_diagnosticSink, nullptr);
        m_traceSink = std::exchange(other.m_traceSink, nullptr);
        m_telemetry = std::move(other.m_telemetry);
        m_scratch = std::exchange(other.m_scratch, nullptr);
        m_limits = std::exchange(other.m_limits, EngineLimits{});
        m_operationId = std::exchange(other.m_operationId, 0u);
        m_maxConcurrency = std::exchange(other.m_maxConcurrency, 0u);
        m_requestedBackend = std::exchange(other.m_requestedBackend, ComputeBackend::Automatic);
        m_backend = std::exchange(other.m_backend, ComputeBackend::Automatic);
        m_requestedInstructionSet = std::exchange(other.m_requestedInstructionSet, InstructionSet::Automatic);
        m_instructionSet = std::exchange(other.m_instructionSet, InstructionSet::Automatic);
        m_backendFallback = std::exchange(other.m_backendFallback, BackendFallback::Allowed);
        m_reductionDeterminism = std::exchange(other.m_reductionDeterminism, ReductionDeterminism::DecisionFeeding);
    }

    /// The hold on the engine context, which is what stops it being shut down while this exists.
    EngineContextLease m_lease;

    /// How this operation is stopped. A handle with no stop state when it cannot be.
    std::stop_token m_stopToken;

    /// When this operation's time budget runs out, if it has one.
    std::optional<std::chrono::steady_clock::time_point> m_deadline;

    /// Where progress goes, or null. Borrowed.
    ProgressSink* m_progressSink = nullptr;

    /// Where diagnostics go, or null. Borrowed.
    DiagnosticSink* m_diagnosticSink = nullptr;

    /// Where traces go, or null. Borrowed.
    TraceSink* m_traceSink = nullptr;

    /// This operation's callback gate, monotonic-progress state, and stable counters.
    std::shared_ptr<OperationTelemetryState> m_telemetry;

    /// This operation's scratch memory, or null. Borrowed.
    ScratchArena* m_scratch = nullptr;

    /// The engine context's configured maxima and memory budget, copied because they are immutable.
    EngineLimits m_limits{};

    /// This operation's identity, unique in the process and never reused.
    std::uint64_t m_operationId = 0u;

    /// The resolved maximum concurrency, at least one and no more than the engine context's.
    std::uint32_t m_maxConcurrency = 0u;

    /// The backend the caller asked for.
    ComputeBackend m_requestedBackend = ComputeBackend::Automatic;

    /// The backend this operation is constrained to, or `Automatic` for unconstrained.
    ComputeBackend m_backend = ComputeBackend::Automatic;

    /// The instruction set the caller asked for.
    InstructionSet m_requestedInstructionSet = InstructionSet::Automatic;

    /// The instruction set this operation is constrained to, or `Automatic` for unconstrained.
    InstructionSet m_instructionSet = InstructionSet::Automatic;

    /// What the caller wanted done about an unavailable backend.
    BackendFallback m_backendFallback = BackendFallback::Allowed;

    /// Which reductions must be thread-count independent.
    ReductionDeterminism m_reductionDeterminism = ReductionDeterminism::DecisionFeeding;
};

} // namespace an
