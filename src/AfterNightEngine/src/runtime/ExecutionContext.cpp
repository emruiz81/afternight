// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/ScratchArena.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "runtime/EngineContextState.h"
#include "runtime/ProgressRange.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

// Starting an operation, and the three things it can say while it runs.
//
// The order of `create` is the part worth stating here rather than only in the header. Policy and
// borrowed-resource validation happen before admission, so a configuration error is never hidden
// by a busy context and never takes a lease. Admission then keeps the engine state alive while the
// operation-local telemetry is built; any later refusal releases that lease with the local.
//
// The report helpers are here rather than inline in the header because the sinks are named
// there and defined here. That is the whole reason the header only forward-declares them: a header
// that receives an execution context should not have to carry the reporting vocabulary in order to
// compile, and every header that later includes this one inherits whatever this one includes.

namespace an
{

/// The synchronization and counters that belong to one operation rather than to its public handle.
///
/// Every member after the guard is read and written only under it, which is what gives progress,
/// diagnostics and traces from one operation a single order and a counter snapshot that never
/// describes half of a callback.
class OperationTelemetryState final
{
public:
    /// Starts the operation's clock and names the operation its counters describe.
    ///
    /// @param operationId the operation's identity.
    explicit OperationTelemetryState(std::uint64_t operationId) noexcept : m_startedAt(std::chrono::steady_clock::now())
    {
        m_counters.operationId = operationId;
    }

private:
    friend class ExecutionContext;
    friend class ProgressRange;

    /// The gate every callback, counter snapshot and progress-range change of the operation takes.
    std::mutex m_callbackGuard;

    /// When the operation started; trace coordinates are measured from it.
    std::chrono::steady_clock::time_point m_startedAt;

    /// The delivered and discarded counts, and the last sequence assigned.
    OperationCounters m_counters{};

    /// The completed count of the last delivered progress report.
    std::uint64_t m_lastCompleted = 0u;

    /// The last trace coordinate reported, which no later one goes below.
    std::uint64_t m_lastElapsedNanoseconds = 0u;

    /// Whether any progress report has been delivered, so the first one is never discarded.
    bool m_hasProgress = false;

    /// The innermost installed progress share, or null when reports go out unmapped.
    ProgressRange* m_progressRange = nullptr;
};

ProgressRange::ProgressRange(const ExecutionContext& execution, const std::uint64_t begin, const std::uint64_t end,
                             const std::uint64_t total) noexcept
    : m_telemetry(execution.m_telemetry), m_begin(static_cast<long double>(begin)),
      m_end(static_cast<long double>(end)), m_total(total)
{
    if (total == 0u || begin > end || end > total)
    {
        m_telemetry.reset();
        return;
    }
    if (m_telemetry == nullptr)
    {
        return;
    }
    const std::lock_guard<std::mutex> held(m_telemetry->m_callbackGuard);
    m_previous = m_telemetry->m_progressRange;
    if (m_previous != nullptr)
    {
        const auto width = m_previous->m_end - m_previous->m_begin;
        m_begin = m_previous->m_begin + width * static_cast<long double>(begin) / static_cast<long double>(total);
        m_end = m_previous->m_begin + width * static_cast<long double>(end) / static_cast<long double>(total);
        m_total = m_previous->m_total;
    }
    m_telemetry->m_progressRange = this;
}

ProgressRange::~ProgressRange()
{
    if (m_telemetry != nullptr)
    {
        const std::lock_guard<std::mutex> held(m_telemetry->m_callbackGuard);
        m_telemetry->m_progressRange = m_previous;
    }
}

namespace
{

/// Raise a callback counter without ever letting it wrap backwards.
///
/// @param value the counter to raise, saturated at its largest value.
void incrementSaturated(std::uint64_t& value) noexcept
{
    if (value != (std::numeric_limits<std::uint64_t>::max)())
    {
        ++value;
    }
}

/// The next callback sequence in an operation's common order.
///
/// @param sequence the last sequence assigned, updated in place.
/// @return the updated sequence, saturated rather than wrapped.
[[nodiscard]] std::uint64_t nextSequence(std::uint64_t& sequence) noexcept
{
    incrementSaturated(sequence);
    return sequence;
}

/// A monotonic profiling coordinate for a trace event.
///
/// The steady clock is specified not to move backwards, and the explicit comparison retains that
/// promise even on a platform clock defect or a duration conversion that rounds two reads together.
///
/// @param startedAt when the operation-local state was created.
/// @param previous the last coordinate reported, updated in place.
/// @return elapsed nanoseconds, never below `previous`.
[[nodiscard]] std::uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point startedAt,
                                               std::uint64_t& previous) noexcept
{
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startedAt).count();
    std::uint64_t current = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0u;
    if (current < previous)
    {
        current = previous;
    }
    previous = current;
    return current;
}

/// The identity for the next operation this process starts.
///
/// A counter rather than an address, so an identity is never reused by a later operation - which is
/// what a progress display correlating reports to operations depends on. It owns nothing: when
/// every execution context is gone, what is left is a number.
///
/// @return an identity no other operation has held, counting from one.
[[nodiscard]] std::uint64_t nextOperationId() noexcept
{
    static std::atomic<std::uint64_t> counter{0u};
    return counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
}

/// Whether this build contains a backend at all.
///
/// Only the build-level question, which is the only one that can be answered before the dispatch
/// table exists. Whether a host can run what the build contains is that table's question.
///
/// @param backend the backend a caller asked for.
/// @return true when an operation could be served by it in this binary.
[[nodiscard]] bool backendIsInThisBuild(ComputeBackend backend) noexcept
{
    switch (backend)
    {
    case ComputeBackend::Automatic:
    case ComputeBackend::Reference:
    case ComputeBackend::Cpu:
        return true;
    case ComputeBackend::Gpu:
        return engineCapabilities().gpuBackend;
    }
    return false;
}

/// Whether an operation can actually be made to run on an instruction set here.
///
/// Two questions rather than one, and both have to hold. A host that cannot run AVX-512 and a build
/// that was not compiled with it are different problems, and an operation that could be forced onto
/// a set this build lacks would silently run the scalar code while claiming otherwise - which is
/// exactly the failure a conformance run exists to catch and would then be blind to.
///
/// @param instructionSet the set a caller asked for.
/// @return true when this host supports it and this build contains an implementation for it.
[[nodiscard]] bool instructionSetCanBeForced(InstructionSet instructionSet) noexcept
{
    return supportsInstructionSet(cpuCapabilities(), instructionSet) && instructionSetIsCompiledIn(instructionSet);
}

} // namespace

Result<ExecutionContext> ExecutionContext::create(const EngineContext& engine, const ExecutionOptions& options)
{
    if (!engine.valid())
    {
        Result<EngineContextLease> absent = engine.acquireLease();
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "an operation cannot be started in an engine context that "
                                                       "holds nothing",
                                            .operation = "ExecutionContext::create",
                                            .domain = ErrorDomain::Runtime},
                               absent.status());
    }

    const std::uint32_t permitted = engine.maxConcurrency();
    const std::uint32_t requested =
        options.maxConcurrency == kUseContextConcurrency ? permitted : options.maxConcurrency;

    if (options.maxConcurrency != kUseContextConcurrency && options.maxConcurrency > permitted)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "an operation may not use more of the machine than its engine context permits",
                         .operation = "ExecutionContext::create",
                         .domain = ErrorDomain::Runtime,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, options.maxConcurrency),
                                     StatusContextField::ofCount(ContextKey::Limit, permitted)}});
    }

    if (options.scratch != nullptr && options.scratch->contextId() != engine.id())
    {
        return Status::failure(
            ErrorCode::InvalidArgument,
            StatusDetail{.message = "an operation's scratch arena must belong to the same engine context",
                         .operation = "ExecutionContext::create",
                         .domain = ErrorDomain::Runtime,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "scratch")}});
    }

    ComputeBackend backend = options.backend;
    if (!backendIsInThisBuild(backend))
    {
        if (options.backendFallback == BackendFallback::Forbidden)
        {
            return Status::failure(
                ErrorCode::UnsupportedBackend,
                StatusDetail{.message = "the requested backend is not in this build and no fallback is permitted",
                             .operation = "ExecutionContext::create",
                             .domain = ErrorDomain::Runtime,
                             .context = {StatusContextField::ofText(ContextKey::Parameter,
                                                                    computeBackendName(options.backend))}});
        }

        // Falling back is not a claim about which backend will run: it says this operation is no
        // longer constrained, and the dispatch table chooses as it would have anyway.
        backend = ComputeBackend::Automatic;
    }

    InstructionSet instructionSet =
        backend == ComputeBackend::Reference ? InstructionSet::Scalar : options.instructionSet;
    if (backend != ComputeBackend::Reference && instructionSet != InstructionSet::Automatic &&
        !instructionSetCanBeForced(instructionSet))
    {
        if (options.backendFallback == BackendFallback::Forbidden)
        {
            return Status::failure(
                ErrorCode::UnsupportedBackend,
                StatusDetail{.message = "the requested instruction set cannot be forced on this host and build, and "
                                        "no fallback is permitted",
                             .operation = "ExecutionContext::create",
                             .domain = ErrorDomain::Runtime,
                             .context = {StatusContextField::ofText(ContextKey::Parameter,
                                                                    instructionSetName(options.instructionSet))}});
        }

        // Narrowed to unconstrained rather than to the next set down, on the same reasoning as the
        // backend request: what will run is the dispatch table's claim to make, not this one's.
        instructionSet = InstructionSet::Automatic;
    }

    // A fixed single-worker arena also fixes reporting-only folds that accumulate per worker.
    // Decision-feeding reductions retain their fixed partition under either policy.
    const std::uint32_t admitted = options.reductionDeterminism == ReductionDeterminism::All ? 1u : requested;
    Result<EngineContextLease> lease = engine.acquireLease(admitted);
    if (!lease.hasValue())
    {
        // Busy and allocation failures are already expressed in the admission boundary's exact
        // vocabulary. In particular, an arena allocation failure must never be rewritten as an
        // uninitialised engine context.
        return lease.status();
    }

    const std::uint64_t operationId = nextOperationId();
    try
    {
        return ExecutionContext{
            std::move(*lease), options,        std::make_shared<OperationTelemetryState>(operationId),
            operationId,       requested,      backend,
            instructionSet,    engine.limits()};
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

MemoryResource* ExecutionContext::memoryResource() const noexcept
{
    // Reached through the lease this operation already holds, so an algorithm needs no second
    // parameter and the engine context cannot have gone away underneath the pointer.
    EngineContextState* const state = EngineContextState::of(*this);
    return state != nullptr ? &state->memory() : nullptr;
}

OperationCounters ExecutionContext::operationCounters() const noexcept
{
    if (m_telemetry == nullptr)
    {
        return OperationCounters{};
    }

    const std::lock_guard<std::mutex> held(m_telemetry->m_callbackGuard);
    return m_telemetry->m_counters;
}

void ExecutionContext::reportProgress(std::uint64_t completed, std::uint64_t total, const char* stage) const noexcept
{
    if (m_progressSink == nullptr || m_telemetry == nullptr)
    {
        return;
    }

    const std::lock_guard<std::mutex> held(m_telemetry->m_callbackGuard);
    if (const auto* range = m_telemetry->m_progressRange; range != nullptr)
    {
        const long double fraction =
            total == 0u ? 0.0L
                        : (std::min)(1.0L, static_cast<long double>(completed) / static_cast<long double>(total));
        const long double mapped = range->m_begin + (range->m_end - range->m_begin) * fraction;
        completed = mapped >= static_cast<long double>((std::numeric_limits<std::uint64_t>::max)())
                        ? (std::numeric_limits<std::uint64_t>::max)()
                        : static_cast<std::uint64_t>(mapped);
        total = range->m_total;
    }
    if (m_telemetry->m_hasProgress && completed < m_telemetry->m_lastCompleted)
    {
        incrementSaturated(m_telemetry->m_counters.discardedProgressReports);
        return;
    }

    m_telemetry->m_hasProgress = true;
    m_telemetry->m_lastCompleted = completed;
    const std::uint64_t sequence = nextSequence(m_telemetry->m_counters.lastSequence);
    incrementSaturated(m_telemetry->m_counters.progressReports);

    const ProgressReport report{.operationId = m_operationId,
                                .sequence = sequence,
                                .completed = completed,
                                .total = total,
                                .stage = stage != nullptr ? stage : ""};
    m_progressSink->onProgress(report);
}

void ExecutionContext::reportDiagnostic(DiagnosticSeverity severity, ErrorDomain domain, const char* operation,
                                        const char* message) const noexcept
{
    if (m_diagnosticSink == nullptr || m_telemetry == nullptr)
    {
        return;
    }

    const std::lock_guard<std::mutex> held(m_telemetry->m_callbackGuard);
    const std::uint64_t sequence = nextSequence(m_telemetry->m_counters.lastSequence);
    incrementSaturated(m_telemetry->m_counters.diagnosticEvents);

    const DiagnosticEvent event{.severity = severity,
                                .domain = domain,
                                .operationId = m_operationId,
                                .sequence = sequence,
                                .operation = operation != nullptr ? operation : "",
                                .message = message != nullptr ? message : ""};
    m_diagnosticSink->onDiagnostic(event);
}

void ExecutionContext::reportTrace(TraceEventKind kind, const char* category, const char* name, double value,
                                   const char* unit) const noexcept
{
    if (m_traceSink == nullptr || m_telemetry == nullptr)
    {
        return;
    }

    const std::lock_guard<std::mutex> held(m_telemetry->m_callbackGuard);
    const std::uint64_t sequence = nextSequence(m_telemetry->m_counters.lastSequence);
    incrementSaturated(m_telemetry->m_counters.traceEvents);

    const TraceEvent event{.kind = kind,
                           .operationId = m_operationId,
                           .sequence = sequence,
                           .elapsedNanoseconds =
                               elapsedNanoseconds(m_telemetry->m_startedAt, m_telemetry->m_lastElapsedNanoseconds),
                           .category = category != nullptr ? category : "",
                           .name = name != nullptr ? name : "",
                           .value = kind == TraceEventKind::Counter ? value : 0.0,
                           .unit = kind == TraceEventKind::Counter && unit != nullptr ? unit : ""};
    m_traceSink->onTrace(event);
}

} // namespace an
