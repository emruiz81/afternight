// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The choices one operation may make about how it is computed.
///
/// Deliberately three small enumerations rather than a settings object, and deliberately in a header
/// of their own: the dispatch table that acts on a backend choice has no business carrying the whole
/// execution context to read one enumerator.
///
/// The set is short because most of what a caller might expect to find here has been decided once,
/// for the whole engine, and is not a per-operation choice at all. There is no strict/reproducible/
/// fast mode selector: this engine has **one** numeric contract, and a mode matrix would mean three
/// sets of golden outputs, three conformance runs and defect reports whose reproduction depends on a
/// flag. What is left below is the part that genuinely varies between one run and the next.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstddef>
#include <cstdint>

namespace an
{

/// Which of the engine's compute implementations an operation should use.
///
/// These are the implementations the engine is physically built from, not instruction sets:
/// selecting AVX2 against AVX-512 happens inside the CPU backend and is a different question with a
/// different mechanism.
enum class ComputeBackend : std::uint8_t
{
    /// Let the engine choose. The ordinary case, and the only one that lets a kernel pick the
    /// fastest implementation it has for the work in front of it.
    Automatic,

    /// The scalar reference kernels. Always present, never the fastest, and the oracle a
    /// conformance run compares an optimised kernel against - which is why it is selectable rather
    /// than being test-only scaffolding.
    Reference,

    /// The compiled CPU backend, with whatever instruction set the dispatch table selects for this
    /// host. Always present.
    Cpu,

    /// Reserved implicit GPU dispatch. Always unavailable in this engine; GPU work uses an
    /// explicitly supplied device through `Algorithms/Kernel/Device.h`.
    ///
    /// No build of this repository contains it today, so a request for it is refused with
    /// `ErrorCode::UnsupportedBackend` when fallback is forbidden and runs unconstrained when it is
    /// allowed. The GPU work that exists is a separately compiled device backend reached through
    /// `Algorithms/Kernel/Device.h`, which this enumerator does not select.
    ///
    /// Deliberately the last enumerator; `kComputeBackendCount` is derived from it.
    Gpu
};

/// What happens when the requested backend cannot serve an operation.
enum class BackendFallback : std::uint8_t
{
    /// Use another backend rather than failing. The request is a preference, and an operation that
    /// cannot be served by the named backend still runs.
    Allowed,

    /// Fail rather than use another backend. What a conformance run needs: a comparison against the
    /// reference backend is worthless if the engine quietly ran the same code twice.
    Forbidden
};

/// Which of an operation's reductions must give the same answer whatever the thread count.
///
/// The engine's numeric contract makes this guarantee unconditionally for any reduction whose
/// result feeds a decision - a rejection threshold, a normalisation factor, anything that changes
/// which data is used or how it is scaled - because otherwise a stacking result would depend on the
/// core count of the machine that produced it. Reductions that only get reported cost the same
/// guarantee for no benefit, so by default they may be computed the faster way.
///
/// `All` admits exactly one worker for the operation, fixing reporting-only folds as well as
/// decision-feeding reductions regardless of the requested concurrency. It can cost throughput;
/// the default retains parallel execution. This controls thread-count dependence, not differences
/// between instruction sets.
enum class ReductionDeterminism : std::uint8_t
{
    /// The default and the approved baseline: reductions that feed a decision are thread-count
    /// independent, and reporting-only reductions may reassociate within their recorded bound.
    DecisionFeeding,

    /// Every reduction is thread-count independent, at whatever it costs. For a conformance run
    /// that compares reported statistics across thread counts, and for measuring what the stronger
    /// guarantee would cost if it were extended to everything.
    All
};

/// How many backends `ComputeBackend` holds.
///
/// Derived from the last enumerator rather than written out, so it cannot drift from the
/// enumeration while `Gpu` stays last. It exists so that a check over the whole set - a name for
/// every value, a table sized to them - can be written without its author restating the
/// enumeration, which is what makes a value added without a name fail on every compiler rather
/// than only on the ones that report an unhandled switch case.
inline constexpr std::size_t kComputeBackendCount = static_cast<std::size_t>(ComputeBackend::Gpu) + 1u;

/// The name of a backend, for a diagnostic, a report, or a benchmark manifest.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param backend the backend to name. A value from outside the enumeration is named
///        `"UnknownComputeBackend"` rather than read off the end of a table.
/// @return a never-null pointer to a string literal compiled into the library, which outlives every
///         caller and is never freed or mutated.
[[nodiscard]] AFTERNIGHTENGINE_API const char* computeBackendName(ComputeBackend backend) noexcept;

/// The name of a fallback policy. See `computeBackendName` for the guarantees.
///
/// @param fallback the policy to name. A value from outside the enumeration is named
///        `"UnknownBackendFallback"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* backendFallbackName(BackendFallback fallback) noexcept;

/// The name of a reduction rule. See `computeBackendName` for the guarantees.
///
/// @param determinism the rule to name. A value from outside the enumeration is named
///        `"UnknownReductionDeterminism"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* reductionDeterminismName(ReductionDeterminism determinism) noexcept;

} // namespace an
