// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a running operation says something that is not a failure.
///
/// A returned `Status` is how an operation reports that it could not do what it was asked. This is
/// the other half: the things worth saying while it can - a frame with no exposure time, a header
/// field that had to be guessed, a backend that was not available and was fallen back from. None of
/// those stops the operation, and none of them should be written to a stream the engine chose.
///
/// So the engine writes nothing anywhere. It hands the event to a sink the caller supplied, and a
/// build with no sink says nothing at all: logging is never a global with a file behind it.
///
/// The vocabulary is shared with the failure model on purpose. An event carries an `ErrorDomain`,
/// the same enumeration a `Status` carries, so a sink routing by subsystem uses one table for both
/// and a diagnostic about the image model cannot be filed differently from a failure in it.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <cstdint>

namespace an
{

/// How much an event matters, for a sink deciding what to keep.
///
/// Ordered from least to most serious, and the order is part of the contract: a sink filtering at a
/// level keeps everything at or above it, so a comparison on the underlying value is meaningful.
enum class DiagnosticSeverity : std::uint8_t
{
    /// Step-by-step detail, useful when reproducing a specific run and noise otherwise.
    Trace,

    /// Detail a developer wants while investigating, and nobody wants during a normal run.
    Debug,

    /// Something a person would want in a record of what the run did.
    Info,

    /// The operation is continuing and something about the result is worth knowing - a value that
    /// had to be assumed, a preference that could not be met.
    Warning,

    /// Something went wrong. An operation that is also giving up returns a `Status` saying so; this
    /// is for the failure that is worth reporting while the operation carries on regardless.
    Error
};

/// One thing an operation had to say while it was running.
///
/// A plain aggregate with a default for every member, built at the point the engine reports and
/// read inside the sink call. Nothing in it is owned by the sink.
struct DiagnosticEvent
{
    /// How much this matters.
    DiagnosticSeverity severity = DiagnosticSeverity::Info;

    /// Which part of the engine is speaking, in the same vocabulary a failure uses.
    ErrorDomain domain = ErrorDomain::Unspecified;

    /// Which operation this came from. See `ExecutionContext::operationId`. Never zero for an event
    /// the engine reported, so one sink can serve several operations and tell them apart.
    std::uint64_t operationId = 0u;

    /// This event's position among every callback delivered for the operation.
    ///
    /// Starts at one, is shared with progress and traces, and never decreases. See
    /// `OperationCounters::lastSequence`.
    std::uint64_t sequence = 0u;

    /// The engine operation speaking, spelled as it is in the source. Never null, possibly empty.
    ///
    /// Valid only for the duration of the sink call. A sink that keeps it copies it.
    const char* operation = "";

    /// A sentence for a person. Never null, possibly empty, free to change between builds, and
    /// never parsed - anything a caller acts on is the severity, the domain, or a returned failure.
    ///
    /// Valid only for the duration of the sink call. A sink that keeps it copies it.
    const char* message = "";
};

/// Where an operation's diagnostics are delivered.
///
/// Implemented by the caller and given to an execution context, which holds a pointer to it and
/// never owns it - so the sink must outlive every execution context it was given to.
///
/// Events may arrive on any thread the operation is using. An execution context serializes all of
/// one operation's diagnostic, progress, and trace callbacks. The same sink may still serve
/// different operations concurrently, so an implementation must be safe for that use.
class DiagnosticSink
{
public:
    /// Virtual, so that a sink held by base pointer is destroyed as what it is.
    virtual ~DiagnosticSink() = default;

    /// Called when a running operation has something to say.
    ///
    /// `noexcept`, and meant: an exception here would unwind out of whatever the engine was doing,
    /// so an implementation handles its own failures. It should be quick, and must not call back
    /// into the execution context that is reporting.
    ///
    /// @param event what happened. Valid only for the duration of the call, including the strings
    ///        it carries.
    virtual void onDiagnostic(const DiagnosticEvent& event) noexcept = 0;

protected:
    /// Constructible only by a derived class: this is an interface, not a value.
    DiagnosticSink() = default;

    /// Protected rather than public, so that a sink is never copied through a base pointer and
    /// sliced down to its interface. A derived sink that wants to be copyable still can be.
    ///
    /// @param other the sink to copy from.
    DiagnosticSink(const DiagnosticSink& other) = default;

    /// Protected for the same reason as the copy constructor.
    ///
    /// @param other the sink to copy from.
    /// @return this sink.
    DiagnosticSink& operator=(const DiagnosticSink& other) = default;
};

/// The name of a severity, for a sink writing a line somebody will read.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param severity the severity to name. A value from outside the enumeration is named
///        `"UnknownDiagnosticSeverity"` rather than read off the end of a table.
/// @return a never-null pointer to a string literal compiled into the library, which outlives every
///         caller and is never freed or mutated.
[[nodiscard]] AFTERNIGHTENGINE_API const char* diagnosticSeverityName(DiagnosticSeverity severity) noexcept;

} // namespace an
