// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - feature detection, and the two things about it that are easy to get wrong.
//
// **Asking the processor is not enough for the wide registers.** AVX and AVX-512 add register state
// that the operating system has to agree to preserve across a context switch, and a processor whose
// feature bit is set under a kernel that has not enabled that state will execute the instruction and
// then lose the register. The check is therefore two-sided: the feature bit, and `XGETBV` reporting
// that the state is enabled. Detection that reads only the feature bit is the classic way to produce
// a binary that works on the test machine and corrupts data somewhere else.
//
// **The ladder has to be a ladder.** A caller uses `highestSupported` directly as a dispatch
// decision and a checker asserts that nothing above it is supported, so each rung is derived from
// the one below it rather than from its own bit alone. A processor reporting AVX2 without AVX would
// then be reported as AVX2-incapable, which is the safe direction. The AVX2 rung also requires FMA,
// which the kernels compiled for that rung assume.
//
// Everything is computed once, into a function-local static. That is the whole of the thread-safe
// initialisation: the standard already requires the initialiser to run exactly once even when
// several threads arrive together, and the value is a constant afterwards, so there is nothing left
// for a forced selection to race against.

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

#include <cstddef>
#include <cstdint>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#define AFTERNIGHTENGINE_CPU_X86 1
#else
#define AFTERNIGHTENGINE_CPU_X86 0
#endif

#if AFTERNIGHTENGINE_CPU_X86
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#include <immintrin.h>
#endif
#endif

namespace an
{
namespace
{

#if AFTERNIGHTENGINE_CPU_X86

/// The four registers one `CPUID` leaf reports.
struct CpuIdResult
{
    /// EAX.
    std::uint32_t eax = 0u;

    /// EBX.
    std::uint32_t ebx = 0u;

    /// ECX.
    std::uint32_t ecx = 0u;

    /// EDX.
    std::uint32_t edx = 0u;
};

/// Reads one `CPUID` leaf and sub-leaf.
///
/// @param leaf the leaf number, in EAX.
/// @param subLeaf the sub-leaf number, in ECX.
/// @return what the four registers held.
[[nodiscard]] CpuIdResult readCpuId(std::uint32_t leaf, std::uint32_t subLeaf) noexcept
{
    CpuIdResult result;
#if defined(_MSC_VER)
    int registers[4] = {0, 0, 0, 0};
    __cpuidex(registers, static_cast<int>(leaf), static_cast<int>(subLeaf));
    result.eax = static_cast<std::uint32_t>(registers[0]);
    result.ebx = static_cast<std::uint32_t>(registers[1]);
    result.ecx = static_cast<std::uint32_t>(registers[2]);
    result.edx = static_cast<std::uint32_t>(registers[3]);
#else
    __cpuid_count(leaf, subLeaf, result.eax, result.ebx, result.ecx, result.edx);
#endif
    return result;
}

/// Whether the operating system preserves the register state a set of `XCR0` bits describes.
///
/// @param mask the `XCR0` bits that must all be set.
/// @return true when the processor supports `XGETBV` and every named bit is enabled.
[[nodiscard]] bool extendedStateEnabled(std::uint64_t mask) noexcept
{
    constexpr std::uint32_t kOsXsaveBit = 1u << 27u;
    const CpuIdResult features = readCpuId(1u, 0u);
    if ((features.ecx & kOsXsaveBit) == 0u)
    {
        return false;
    }

#if defined(_MSC_VER)
    const std::uint64_t enabled = _xgetbv(0);
#else
    std::uint32_t low = 0u;
    std::uint32_t high = 0u;
    __asm__ __volatile__("xgetbv" : "=a"(low), "=d"(high) : "c"(0u));
    const std::uint64_t enabled = (static_cast<std::uint64_t>(high) << 32u) | low;
#endif
    return (enabled & mask) == mask;
}

/// The largest cache line this host reports, in bytes.
///
/// Read from the deterministic cache-parameter leaf, which reports a line size per cache level. The
/// largest is the interesting one, because it is the granularity two threads can falsely share at.
///
/// @return the line size, or zero when the leaf is not available.
[[nodiscard]] std::uint32_t readCacheLineBytes() noexcept
{
    const CpuIdResult highest = readCpuId(0u, 0u);
    if (highest.eax < 4u)
    {
        return 0u;
    }

    std::uint32_t largest = 0u;
    for (std::uint32_t level = 0u; level < 8u; ++level)
    {
        const CpuIdResult cache = readCpuId(4u, level);
        const std::uint32_t type = cache.eax & 0x1Fu;
        if (type == 0u)
        {
            break;
        }

        const std::uint32_t lineBytes = (cache.ebx & 0xFFFu) + 1u;
        if (lineBytes > largest)
        {
            largest = lineBytes;
        }
    }
    return largest;
}

#endif // AFTERNIGHTENGINE_CPU_X86

/// Asks the processor what it supports, once.
///
/// @return the capabilities of the host this process is running on.
[[nodiscard]] CpuCapabilities detectCapabilities() noexcept
{
    CpuCapabilities capabilities;

#if AFTERNIGHTENGINE_CPU_X86
    constexpr std::uint64_t kSseState = 0x2u;
    constexpr std::uint64_t kAvxState = 0x4u;
    constexpr std::uint64_t kOpmaskState = 0x20u;
    constexpr std::uint64_t kZmmHigh256State = 0x40u;
    constexpr std::uint64_t kHighZmmState = 0x80u;

    const CpuIdResult features = readCpuId(1u, 0u);
    capabilities.sse2 = (features.edx & (1u << 26u)) != 0u;
    capabilities.sse42 = capabilities.sse2 && (features.ecx & (1u << 20u)) != 0u;

    const bool avxState = extendedStateEnabled(kSseState | kAvxState);
    capabilities.avx = capabilities.sse42 && avxState && (features.ecx & (1u << 28u)) != 0u;
    capabilities.fma = capabilities.avx && (features.ecx & (1u << 12u)) != 0u;

    const CpuIdResult extended = readCpuId(7u, 0u);
    capabilities.avx2 = capabilities.avx && capabilities.fma && (extended.ebx & (1u << 5u)) != 0u;

    const bool avx512State =
        extendedStateEnabled(kSseState | kAvxState | kOpmaskState | kZmmHigh256State | kHighZmmState);
    capabilities.avx512f = capabilities.avx2 && avx512State && (extended.ebx & (1u << 16u)) != 0u;
    capabilities.avx512dq = capabilities.avx512f && (extended.ebx & (1u << 17u)) != 0u;
    capabilities.avx512bw = capabilities.avx512f && (extended.ebx & (1u << 30u)) != 0u;
    capabilities.avx512vl = capabilities.avx512f && (extended.ebx & (1u << 31u)) != 0u;

    const std::uint32_t lineBytes = readCacheLineBytes();
    if (lineBytes != 0u)
    {
        capabilities.cacheLineBytes = lineBytes;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    // Advanced SIMD is part of the AArch64 baseline, so there is nothing to ask.
    capabilities.neon = true;
#endif

    // The ladder, derived rather than read, so that nothing above `highestSupported` is ever
    // reported as supported.
    if (capabilities.avx512f && capabilities.avx512bw && capabilities.avx512dq && capabilities.avx512vl)
    {
        capabilities.highestSupported = InstructionSet::Avx512Skylake;
    }
    else if (capabilities.avx2)
    {
        capabilities.highestSupported = InstructionSet::Avx2;
    }
    else if (capabilities.avx)
    {
        capabilities.highestSupported = InstructionSet::Avx;
    }
    else if (capabilities.sse42)
    {
        capabilities.highestSupported = InstructionSet::Sse42;
    }
    else if (capabilities.sse2)
    {
        capabilities.highestSupported = InstructionSet::Sse2;
    }
    else if (capabilities.neon)
    {
        capabilities.highestSupported = InstructionSet::Neon;
    }
    else
    {
        capabilities.highestSupported = InstructionSet::Scalar;
    }

    return capabilities;
}

} // namespace

CpuCapabilities cpuCapabilities() noexcept
{
    // The whole of the thread-safe initialisation. The standard guarantees the initialiser runs
    // exactly once however many threads arrive together, and the value is immutable afterwards, so
    // a forced backend selection has nothing to race against.
    static const CpuCapabilities kDetected = detectCapabilities();
    return kDetected;
}

bool supportsInstructionSet(const CpuCapabilities& capabilities, InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Automatic:
        return false;
    case InstructionSet::Scalar:
        return true;
    case InstructionSet::Sse2:
        return capabilities.sse2;
    case InstructionSet::Sse42:
        return capabilities.sse42;
    case InstructionSet::Avx:
        return capabilities.avx;
    case InstructionSet::Avx2:
        return capabilities.avx2 && capabilities.fma;
    case InstructionSet::Avx512Skylake:
        return capabilities.avx512f && capabilities.avx512bw && capabilities.avx512dq && capabilities.avx512vl;
    case InstructionSet::Neon:
        return capabilities.neon;
    }
    return false;
}

const char* instructionSetName(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Automatic:
        return "Automatic";
    case InstructionSet::Scalar:
        return "Scalar";
    case InstructionSet::Sse2:
        return "Sse2";
    case InstructionSet::Sse42:
        return "Sse42";
    case InstructionSet::Avx:
        return "Avx";
    case InstructionSet::Avx2:
        return "Avx2";
    case InstructionSet::Avx512Skylake:
        return "Avx512Skylake";
    case InstructionSet::Neon:
        return "Neon";
    }
    return "UnknownInstructionSet";
}

} // namespace an
