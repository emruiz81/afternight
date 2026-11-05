// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Operation traces and stable callback counters.
///
/// Diagnostics are sentences about a run. Traces are its machine-readable shape: named points,
/// spans and counters that a profiler can correlate without parsing a log message. The engine still
/// chooses no recorder and writes no global trace; a caller supplies this interface to one execution
/// or receives nothing.
///
/// The sequence and elapsed time are assigned by the execution context, not by an algorithm. That
/// gives progress, diagnostics and traces from one operation a single order even when its workers
/// report concurrently. The counter snapshot uses the same gate, so it never describes half of a
/// callback.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstdint>

namespace an
{

/// What a trace event represents.
enum class TraceEventKind : std::uint8_t
{
    /// A named point in the operation with no duration of its own.
    Instant,

    /// The beginning of a named span. A matching end uses the same category and name.
    SpanBegin,

    /// The end of a named span. Span nesting is a convention of the reporting algorithm and is not
    /// repaired by the sink boundary.
    SpanEnd,

    /// An instantaneous numeric counter value. `TraceEvent::value` and `TraceEvent::unit` are
    /// meaningful for this kind.
    Counter
};

/// One machine-readable event from a running operation.
///
/// Every string is borrowed and valid only during `TraceSink::onTrace`. Category and name are
/// stable identifiers chosen by the reporting operation, not prose for a person; a sink may group
/// or aggregate them after copying them. Sequence numbers are shared with progress and diagnostics.
struct TraceEvent
{
    /// What this event represents.
    TraceEventKind kind = TraceEventKind::Instant;

    /// Which operation emitted it. See `ExecutionContext::operationId`. Never zero for an event the
    /// engine reports.
    std::uint64_t operationId = 0u;

    /// This event's position among every callback delivered for the operation.
    ///
    /// Starts at one, never decreases, and saturates at the largest `std::uint64_t` value rather
    /// than wrapping. Different operations have independent sequences.
    std::uint64_t sequence = 0u;

    /// Nanoseconds since the execution context was created, measured on the steady clock.
    ///
    /// Monotonic within one operation and saturated rather than wrapped. This is a profiling
    /// coordinate, not wall-clock time and not comparable between operations.
    std::uint64_t elapsedNanoseconds = 0u;

    /// Stable subsystem or activity family. Never null, possibly empty.
    ///
    /// Valid only for the duration of the sink call. A sink that keeps it copies it.
    const char* category = "";

    /// Stable event or counter name. Never null, possibly empty.
    ///
    /// Valid only for the duration of the sink call. A sink that keeps it copies it.
    const char* name = "";

    /// The counter's current value for `TraceEventKind::Counter`; zero for kinds that carry no
    /// numeric value.
    double value = 0.0;

    /// The counter value's stable unit, such as `"bytes"` or `"frames"`; empty when not applicable.
    ///
    /// Valid only for the duration of the sink call. A sink that keeps it copies it.
    const char* unit = "";
};

/// Stable counts for callbacks accepted by one execution context.
///
/// Counts never decrease and saturate rather than wrap. Progress reports whose completed count
/// would move backwards are counted separately and are not delivered. Calls made for a sink that
/// was not supplied are no-ops and therefore do not appear here.
struct OperationCounters
{
    /// Which operation these counters describe, or zero for an execution context that holds
    /// nothing.
    std::uint64_t operationId = 0u;

    /// Monotonic progress reports delivered to the progress sink.
    std::uint64_t progressReports = 0u;

    /// Progress reports discarded because their completed count was below the last accepted one.
    std::uint64_t discardedProgressReports = 0u;

    /// Diagnostic events delivered to the diagnostic sink.
    std::uint64_t diagnosticEvents = 0u;

    /// Trace events delivered to the trace sink.
    std::uint64_t traceEvents = 0u;

    /// Sequence assigned to the last delivered callback, or zero before any callback.
    ///
    /// Equal to the saturated sum of the three delivered-event counts unless that sum itself has
    /// saturated.
    std::uint64_t lastSequence = 0u;
};

/// Where an operation's trace events are delivered.
///
/// Implemented by the caller and borrowed by an execution context. Callbacks from one operation
/// are serialized with that operation's progress and diagnostics; the same sink may still receive
/// callbacks from different operations concurrently and must be safe for that use.
class TraceSink
{
public:
    /// Virtual, so that a sink held by base pointer is destroyed as what it is.
    virtual ~TraceSink() = default;

    /// Called for one trace point, span boundary, or counter.
    ///
    /// `noexcept`, and meant: a callback may not unwind through a kernel. It should be quick and
    /// must not call back into the execution context that is reporting.
    ///
    /// @param event the event. Valid only for the duration of the call, including its strings.
    virtual void onTrace(const TraceEvent& event) noexcept = 0;

protected:
    /// Constructible only by a derived class: this is an interface, not a value.
    TraceSink() = default;

    /// Protected so copying through a base pointer cannot slice a derived sink.
    ///
    /// @param other the sink to copy from.
    TraceSink(const TraceSink& other) = default;

    /// Protected for the same reason as the copy constructor.
    ///
    /// @param other the sink to copy from.
    /// @return this sink.
    TraceSink& operator=(const TraceSink& other) = default;
};

/// The stable spelling of a trace event kind.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param kind the kind to name. A value outside the enumeration is named
///        `"UnknownTraceEventKind"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* traceEventKindName(TraceEventKind kind) noexcept;

} // namespace an
