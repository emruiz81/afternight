// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One admitted operation: its execution, its scratch arena and its stop state, owned in the order that outlives
/// every callback the execution borrows.
///
/// A consumer does not build an `ExecutionContext` itself. It asks its session to admit an operation with a purpose,
/// and the session decides how many of its threads the operation is granted - all of them, part of them, or none yet,
/// in which case the request waits in a bounded queue. What comes back is an `OperationScope`: the execution the
/// engine runs under, and the admission slot that granted it.
///
/// **The order of release is the point of the type.** The execution holds an engine lease and borrows the scratch
/// arena and the stop token; the admission slot is what the next queued operation is waiting for. So ending a scope
/// releases the execution first - returning its threads to the engine - then the scratch arena, then the link to the
/// caller's stop token, and only then the admission slot. An operation the session admits next therefore always finds
/// the engine's threads free, and nothing the execution borrowed is gone while it can still be reached. Ending a scope
/// is also what a workflow does between stages: an execution holds its share of the threads for its whole life, so a
/// stage that keeps its scope one statement too long is why the next one waits.
///
/// Nested work shares the scope: an operation that composes others passes `execution()` down rather than asking for
/// another admission, which at width one would wait for itself.
#pragma once

#include <AfterNightConsumerRuntime/ConsumerRuntimeExport.h>

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/ScratchArena.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>

namespace an
{
class DiagnosticSink;
class ProgressSink;
class TraceSink;
} // namespace an

namespace an::consumer
{

namespace detail
{
class SessionCore;
} // namespace detail

/// Why an operation runs, which decides how admission treats it.
enum class OperationPurpose : std::uint8_t
{
    /// A preview a dialog shows while its parameters change. Is sure of the preview reservation, and borrows every free
    /// thread but one while no other work waits.
    Preview,

    /// The operation a person applied to an image.
    Apply,

    /// One item of work over many images.
    Batch,

    /// One stage of a workflow.
    WorkflowStage,

    /// Work nobody is waiting for. Admitted on one thread, and only when one is free: it never waits in the queue.
    Background
};

/// A purpose's name, for messages and records.
///
/// @param purpose the purpose.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const char* operationPurposeName(OperationPurpose purpose) noexcept;

/// How many of a session's threads are reserved for previews while a preview is admitted or a preview dialog is
/// open: none at width one, where all work is serialized; one up to width four; a quarter of the width above it.
/// Every open preview shares the reservation rather than claiming one of its own. It is what previews are sure of:
/// while no other work waits, a preview borrows every free thread but one besides.
///
/// @param width the session's maximum concurrency.
/// @return the reserved threads.
[[nodiscard]] constexpr std::uint32_t previewReservationWidth(std::uint32_t width) noexcept
{
    if (width <= 1u)
    {
        return 0u;
    }
    if (width <= 4u)
    {
        return 1u;
    }
    return width / 4u;
}

/// What an operation asks of its session.
///
/// The sinks are borrowed: they must outlive the scope the request is admitted into. Everything else is copied.
struct OperationRequest
{
    /// Why it runs.
    OperationPurpose purpose = OperationPurpose::Apply;

    /// The most threads it can use; zero asks for as many as its purpose may have. A request wider than that is
    /// narrowed rather than refused: dividing the threads is the session's decision, not the caller's.
    std::uint32_t maxConcurrency = 0u;

    /// Stops the request while it is queued, and the operation once it runs.
    std::stop_token stopToken{};

    /// When the request stops waiting, and when the operation's checkpoints report it late.
    std::optional<std::chrono::steady_clock::time_point> deadline{};

    /// Receives the operation's progress. Borrowed.
    an::ProgressSink* progressSink = nullptr;

    /// Receives the operation's diagnostics. Borrowed.
    an::DiagnosticSink* diagnosticSink = nullptr;

    /// Receives the operation's trace events. Borrowed.
    an::TraceSink* traceSink = nullptr;

    /// The block size of a scratch arena for the operation; zero gives it none.
    std::uint64_t scratchBlockBytes = 0u;

    /// The backend the operation prefers.
    an::ComputeBackend backend = an::ComputeBackend::Automatic;

    /// The instruction set the operation prefers.
    an::InstructionSet instructionSet = an::InstructionSet::Automatic;

    /// Whether a preference the host or build cannot meet falls back or is refused.
    an::BackendFallback backendFallback = an::BackendFallback::Allowed;

    /// How reductions the operation runs combine their partial results.
    an::ReductionDeterminism reductionDeterminism = an::ReductionDeterminism::DecisionFeeding;
};

/// One admitted operation.
///
/// Move-only, and moving keeps everything the execution borrows where it is: the state lives in one allocation that
/// moves as a pointer. Not thread-safe as an object - one thread owns a scope - but `execution()` is the engine's
/// execution context, which the engine's own threads read concurrently as it documents.
class OperationScope
{
public:
    /// A scope that holds nothing.
    AFTERNIGHTCONSUMERRUNTIME_API OperationScope() noexcept;

    /// Ends the operation: the execution, then the scratch arena, then the stop link, then the admission slot.
    AFTERNIGHTCONSUMERRUNTIME_API ~OperationScope();

    /// Takes over another scope, leaving it holding nothing.
    ///
    /// @param other the scope to take over.
    AFTERNIGHTCONSUMERRUNTIME_API OperationScope(OperationScope&& other) noexcept;

    /// Ends this scope's operation and takes over another's.
    ///
    /// @param other the scope to take over.
    /// @return this scope.
    AFTERNIGHTCONSUMERRUNTIME_API OperationScope& operator=(OperationScope&& other) noexcept;

    OperationScope(const OperationScope&) = delete;
    OperationScope& operator=(const OperationScope&) = delete;

    /// Whether this scope holds an admitted operation.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool valid() const noexcept;

    /// Why the operation runs.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API OperationPurpose purpose() const noexcept;

    /// How many threads admission granted; at least one for a valid scope, zero otherwise. Fixed for the scope's life:
    /// threads freed after it was admitted are not added to it.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint32_t grantedConcurrency() const noexcept;

    /// The engine context the operation runs in, valid while this scope is: the session refuses to replace a context
    /// while a scope of it is outstanding. Calling it on a scope that holds nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const an::EngineContext& engine() const noexcept;

    /// The execution the engine runs the operation under. Calling it on a scope that holds nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const an::ExecutionContext& execution() const noexcept;

    /// The operation's scratch arena, or null when the request asked for none. The execution borrows the same one.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::ScratchArena* scratch() const noexcept;

    /// The engine's identity for the operation, or zero for a scope that holds nothing.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint64_t operationId() const noexcept;

    /// Asks the operation to stop at its next checkpoint. The caller's own stop token is not touched.
    AFTERNIGHTCONSUMERRUNTIME_API void requestStop() noexcept;

    /// Whether the operation has been asked to stop, through this scope or through the caller's token.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool stopRequested() const noexcept;

    /// Ends the operation now, in the same order as destruction. Ending twice ends it once.
    AFTERNIGHTCONSUMERRUNTIME_API void end() noexcept;

private:
    friend class detail::SessionCore;

    /// The state a scope owns: the engine's own types and the session's admission slot, in one allocation.
    class State;

    explicit OperationScope(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> m_state;
};

} // namespace an::consumer
