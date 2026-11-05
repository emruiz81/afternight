// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which implementation an operation runs on, and how it says which one it
/// ran.
///
/// The engine has more than one implementation of the same arithmetic - a scalar reference and a
/// compiled CPU backend at one of several instruction sets - and a conformance run is the reason
/// all of them exist at once. That run compares an optimised kernel
/// against the reference, and it is worth nothing unless two things hold: each implementation can be
/// asked for by name, and the engine afterwards says which one it actually used.
///
/// So the selection here is a lookup in a function table rather than a calculation. The tables are
/// constants and the feature detection they are chosen by is computed once and never changes, which
/// is what makes forcing a choice free of a race against the initialisation it forces. And the
/// reported instruction set is read back out of the selected table's own entry rather than
/// recomputed alongside it - so a table wired to the wrong implementation is a failing selection
/// test rather than a wrong pixel found much later.
///
/// The tables themselves are private to the CPU backend. The scalar table points at the reference
/// kernels and each vector table at kernels compiled for its own instruction set, so an operation
/// forced to `ComputeBackend::Reference` runs genuinely different code from one left automatic.
/// Which vector sets a build contains is read from the build rather than stated: a Microsoft build
/// has the AVX2 rung and no higher, and a Clang build has the AVX-512 rung as well.
///
/// A GPU is not reached through these tables. No build contains a GPU dispatch table, so an
/// operation constrained to `ComputeBackend::Gpu` is refused; a device is reached through
/// `Algorithms/Kernel/Device.h` instead.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, because every entry point below only borrows one for the
/// duration of a call. A caller reaching these functions holds an execution context and has
/// therefore already included the header that defines it.
class ExecutionContext;

/// Which implementation an operation is going to run on.
///
/// The pair a benchmark manifest records beside the timing and a conformance run records beside the
/// output, because "faster than the reference" and "the same as the reference" are both claims about
/// two named implementations rather than about one engine.
///
/// Every member carries a default, so a caller building one - in a report, in a test - names only
/// what it is setting.
struct BackendSelection
{
    /// Which of the engine's implementations. Never `ComputeBackend::Automatic`: this is an answer
    /// rather than a request.
    ComputeBackend backend = ComputeBackend::Reference;

    /// Which instruction set inside it. Never `InstructionSet::Automatic`, for the same reason.
    /// `ComputeBackend::Reference` always reports `InstructionSet::Scalar`.
    InstructionSet instructionSet = InstructionSet::Scalar;

    /// Two selections are equal when they name the same backend and the same instruction set.
    ///
    /// @param left one selection.
    /// @param right the other.
    /// @return true when they are the same answer.
    [[nodiscard]] friend constexpr bool operator==(const BackendSelection& left,
                                                   const BackendSelection& right) noexcept = default;
};

/// Which implementation this operation will run on, and a note to its diagnostic sink saying so.
///
/// Resolves the operation's requested backend and instruction set against what this build contains
/// and what this host supports. A request that cannot be served has already been answered when the
/// execution context was built - refused there, or narrowed to unconstrained - so what reaches this
/// function is either a constraint that can be honoured or no constraint at all.
///
/// The answer is read out of the selected dispatch table's own entry, so what it reports is what
/// would run rather than what the selection intended.
///
/// It reports the selection to the operation's diagnostic sink at `DiagnosticSeverity::Debug`, so a
/// run that behaved unexpectedly can be asked what it dispatched to without being re-run under a
/// debugger. An operation given no sink says nothing.
///
/// Thread-safe with respect to `execution`, which is only read, and safe to call from several
/// threads at once: the table it reads is immutable once built. Allocation-free apart from a
/// failure it has to describe.
///
/// @param execution the operation. Must hold something.
/// @return the selection, or `ErrorCode::NotInitialized` when `execution` holds nothing, or
///         `ErrorCode::UnsupportedBackend` when the operation is constrained to a backend this
///         build has no dispatch table for.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BackendSelection> selectBackend(const ExecutionContext& execution);

/// The instruction set the code this operation would dispatch to was compiled for.
///
/// Asked of the selected table's entry rather than of the selection, which is the difference
/// between observing and asserting: a table whose entries point at the wrong implementation reports
/// the wrong set here, and nothing else in the engine would notice until an output was wrong.
///
/// Thread-safe and allocation-free.
///
/// @param execution the operation. One that holds nothing reports `InstructionSet::Automatic`,
///        because it would dispatch to nothing at all.
/// @return the instruction set the dispatched code declares.
[[nodiscard]] AFTERNIGHTENGINE_API InstructionSet
instructionSetOfDispatchedCode(const ExecutionContext& execution) noexcept;

/// Whether this build contains an implementation compiled for an instruction set.
///
/// The build question beside `supportsInstructionSet`'s host question. Both have to be true before
/// an operation can be made to run on a set, and they fail for different reasons: a host that
/// cannot run AVX-512 and a build that was not compiled with it need different answers from
/// whoever is trying to force it.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param instructionSet the set to test.
/// @return true when this binary has a dispatch table for it.
[[nodiscard]] AFTERNIGHTENGINE_API bool instructionSetIsCompiledIn(InstructionSet instructionSet) noexcept;

} // namespace an
