// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a running operation says how far it has got.
///
/// An interface a caller implements, never a callback the engine holds a GUI object in. It reports
/// progress and nothing else - cancellation belongs to the execution context - and it is deliberately
/// the smallest thing that can serve a progress bar.
///
/// Three properties are the contract, and each of them exists because of what a sink really is - a
/// caller's object, reached from inside the engine, while the engine is busy.
///
/// `onProgress` is `noexcept`. A sink that throws would unwind through a kernel, so the interface
/// refuses the possibility rather than documenting it; the compiler enforces that on the
/// implementation.
///
/// Every report names the operation it came from. One sink can therefore serve several operations at
/// once - which is the ordinary case for a workflow with one progress display - without a separate
/// sink per operation and without the display having to guess which one moved.
///
/// The strings in a report do not outlive the call. They are pointers into whatever the engine was
/// doing at the time, so a sink that keeps one copies it.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstdint>

namespace an
{

/// One coarse progress report from a running operation.
///
/// Coarse on purpose: a report is made at documented checkpoints - a frame, a stage, a tile group -
/// rather than per pixel, because a sink is a caller's code and reaching it is not free.
///
/// Every member carries a default, so a caller building one - in a test, in a report - names only
/// what it is setting.
struct ProgressReport
{
    /// Which operation this report is about. See `ExecutionContext::operationId`. Never zero for a
    /// report the engine made.
    std::uint64_t operationId = 0u;

    /// This report's position among every callback delivered for the operation.
    ///
    /// Starts at one, is shared with diagnostics and traces, and never decreases. See
    /// `OperationCounters::lastSequence`.
    std::uint64_t sequence = 0u;

    /// How many units of work are finished. The unit is the operation's own and is named by
    /// `stage`; the pair is only meaningful together.
    std::uint64_t completed = 0u;

    /// How many units the operation expects to finish. May change between reports when the
    /// operation learns more - a total of zero means it does not yet know.
    std::uint64_t total = 0u;

    /// What the operation is doing, for a person reading a progress display. Never null, possibly
    /// empty, and never parsed.
    ///
    /// Valid only for the duration of the call: it points into the reporting operation, which is
    /// free to move on afterwards. A sink that keeps it copies it.
    const char* stage = "";
};

/// Where an operation's progress is delivered.
///
/// Implemented by the caller and given to an execution context, which holds a pointer to it and
/// never owns it - so the sink must outlive every execution context it was given to.
///
/// Reports may arrive on any thread the operation is using. An execution context serializes all of
/// one operation's progress, diagnostic, and trace callbacks and discards progress whose completed
/// count would move backwards. The same sink may still serve different operations concurrently, so
/// an implementation must be safe for that use.
class ProgressSink
{
public:
    /// Virtual, so that a sink held by base pointer is destroyed as what it is.
    virtual ~ProgressSink() = default;

    /// Called at a documented checkpoint of a running operation.
    ///
    /// `noexcept`, and meant: an exception here would unwind out of a kernel, so an implementation
    /// handles its own failures. It should also be quick - the operation is waiting - and must not
    /// call back into the execution context that is reporting.
    ///
    /// @param report what the operation has finished. Valid only for the duration of the call,
    ///        including the string it carries.
    virtual void onProgress(const ProgressReport& report) noexcept = 0;

protected:
    /// Constructible only by a derived class: this is an interface, not a value.
    ProgressSink() = default;

    /// Protected rather than public, so that a sink is never copied through a base pointer and
    /// sliced down to its interface. A derived sink that wants to be copyable still can be.
    ///
    /// @param other the sink to copy from.
    ProgressSink(const ProgressSink& other) = default;

    /// Protected for the same reason as the copy constructor.
    ///
    /// @param other the sink to copy from.
    /// @return this sink.
    ProgressSink& operator=(const ProgressSink& other) = default;
};

} // namespace an
