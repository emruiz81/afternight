// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the processor this build is running on can actually do.
///
/// `EngineCapabilities` answers what the *build* offers and deliberately stops there; this is the
/// finer question it hands on, and it belongs here because exactly one thing acts on it: the CPU
/// dispatch table. Answering it in two places is how two answers start to disagree.
///
/// **The answer is immutable and is computed once.** What a processor supports does not change while
/// a process runs, so it is derived on first use and is a constant afterwards. That is not only an
/// efficiency: a conformance run compares an optimised kernel against a reference, and a detection
/// that could give two answers in one run would leave the comparison depending on which one won.
///
/// **The instruction sets are a ladder and a `switch` never runs off the end of one.** A processor
/// that has AVX2 has SSE2, and the highest supported set is therefore usable directly as the default
/// choice. `Automatic` is a request rather than a capability, and no host ever reports it.
///
/// **Nothing here is a claim that the build contains a kernel for a set.** This says what the machine
/// could run. Whether this binary was compiled with an implementation for it is
/// `Runtime/BackendDispatch.h`'s question, and it is the one an operation's forced choice is
/// resolved against.
///
/// The header is kept tiny on purpose - one sibling include and two standard headers - because
/// `Runtime/ExecutionContext.h` carries it so that an operation's forced instruction set can travel
/// with the operation, and everything that receives an execution context inherits whatever that
/// header includes.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstddef>
#include <cstdint>

namespace an
{

/// Which family of vector instructions a kernel was compiled for.
///
/// A coarse ladder rather than a feature bitmap: what a dispatch table needs is a small number of
/// implementations to choose between, and a variant per feature combination is a combinatorial
/// build nobody can test. The features themselves are in `CpuCapabilities`, for the kernel that has
/// a reason to ask about one.
enum class InstructionSet : std::uint8_t
{
    /// Let the dispatch table choose. The ordinary case, and a request rather than a capability: no
    /// host reports it and no kernel is compiled for it.
    ///
    /// Deliberately zero, so a default-initialised choice is "unconstrained" rather than a claim.
    Automatic = 0,

    /// No vector instructions at all. Always available, always the slowest, and the oracle a
    /// conformance run compares an optimised kernel against.
    Scalar,

    /// SSE2. The x86-64 baseline, so every x86-64 host has it.
    Sse2,

    /// SSE4.2, which is where the integer and blend instructions a pixel kernel wants begin.
    Sse42,

    /// AVX. 256-bit floating-point vectors, without the integer instructions AVX2 adds.
    Avx,

    /// AVX2 with FMA. The packaged floor of this product's builds.
    Avx2,

    /// The AVX-512 subset a server or workstation part from Skylake-SP onwards provides: F, BW, DQ
    /// and VL together. Named for the subset rather than for "AVX-512", because the parts that have
    /// some of it and not the rest are exactly what an unqualified name gets wrong.
    Avx512Skylake,

    /// AArch64 Advanced SIMD. Present on every AArch64 host, and absent from every x86-64 one.
    ///
    /// Deliberately the last enumerator; `kInstructionSetCount` is derived from it.
    Neon
};

/// How many values `InstructionSet` holds, counting `Automatic`.
///
/// Derived from the last enumerator rather than written out, so it cannot drift from the
/// enumeration while `Neon` stays last. It exists so that a table sized to the set, or a check over
/// all of them, can be written without its author restating the enumeration.
inline constexpr std::size_t kInstructionSetCount = static_cast<std::size_t>(InstructionSet::Neon) + 1u;

/// What the processor this process is running on supports.
///
/// Immutable for the life of the process. Every member carries a default, so a caller building one
/// - in a test, in a report of another machine - names only what it is setting; the defaults
/// describe a machine with no vector instructions at all, which is the conservative starting point
/// rather than a plausible one.
///
/// Detection derives each x86 flag from the rung below it as well as from its own feature bit, so
/// a processor that reports a feature without the rungs below it - AVX2 without AVX, say - is
/// reported without it too. That is the safe direction, and it is what lets `highestSupported` be
/// used directly as a dispatch decision.
struct CpuCapabilities
{
    /// The highest set on the ladder this host supports, which is what an unconstrained operation
    /// resolves to. Never `Automatic`.
    InstructionSet highestSupported = InstructionSet::Scalar;

    /// SSE2 is present. True on every x86-64 host and false on every other architecture.
    bool sse2 = false;

    /// SSE4.2 is present.
    bool sse42 = false;

    /// AVX is present, and the operating system saves the wider registers on a context switch.
    /// Both halves matter: a processor with AVX under a kernel that does not preserve its state
    /// cannot use it, and the detection tests for both.
    bool avx = false;

    /// AVX2 is present, together with the FMA it is packaged with on this ladder.
    bool avx2 = false;

    /// Fused multiply-add is present. Separate from `avx2` because they are separate feature bits,
    /// and because an accumulation's error bound depends on which of the two a kernel used.
    bool fma = false;

    /// AVX-512 Foundation is present, and the operating system saves the mask and wide registers.
    bool avx512f = false;

    /// AVX-512 Byte and Word instructions are present.
    bool avx512bw = false;

    /// AVX-512 Doubleword and Quadword instructions are present.
    bool avx512dq = false;

    /// AVX-512 Vector Length extensions are present, which is what lets an AVX-512 kernel operate
    /// on 128- and 256-bit vectors.
    bool avx512vl = false;

    /// AArch64 Advanced SIMD is present.
    bool neon = false;

    /// How many bytes a cache line holds on this host: the largest line any cache level reports,
    /// because that is the granularity two threads can falsely share at.
    ///
    /// Reported so that a tiling decision can use the machine's own figure rather than the compiled
    /// constant in `Core/Alignment.h`, which is what buffers are aligned to. Never zero: a host that
    /// cannot be asked is reported as the usual sixty-four rather than as an answer nobody can
    /// divide by.
    std::uint32_t cacheLineBytes = 64u;
};

/// What the processor this process is running on supports.
///
/// Thread-safe, reentrant, and allocation-free; it cannot fail. The first call derives the answer
/// and every later one returns the same bytes, so two threads racing on the first call still see
/// one processor.
///
/// @return the capabilities, which are the same for the life of the process.
[[nodiscard]] AFTERNIGHTENGINE_API CpuCapabilities cpuCapabilities() noexcept;

/// Whether a host with these capabilities can run code compiled for an instruction set.
///
/// `Scalar` is always true and `Automatic` is always false: the first is a set every machine has,
/// and the second is a request no machine answers.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param capabilities what the host supports.
/// @param instructionSet the set to test. A value from outside the enumeration is not supported.
/// @return true when code compiled for that set would run here.
[[nodiscard]] AFTERNIGHTENGINE_API bool supportsInstructionSet(const CpuCapabilities& capabilities,
                                                               InstructionSet instructionSet) noexcept;

/// The name of an instruction set, for a diagnostic, a report, or a benchmark manifest.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param instructionSet the set to name. A value from outside the enumeration is named
///        `"UnknownInstructionSet"` rather than read off the end of a table.
/// @return a never-null pointer to a string literal compiled into the library, which outlives every
///         caller and is never freed or mutated.
[[nodiscard]] AFTERNIGHTENGINE_API const char* instructionSetName(InstructionSet instructionSet) noexcept;

} // namespace an
