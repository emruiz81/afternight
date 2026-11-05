// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the local-normalization row, compiled once per instruction set.
//
// The local-normalization row is isolated from the shared SIMD table so its per-instruction-set
// object cost remains attributable. Its cell polynomial, per-position transform, and row loop are
// still the one text shared with the scalar algorithm; only the object that owns each compiled copy
// changes.

#include "backends/cpu/LocalNormalizationCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/LocalNormalizationCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <cmath>
#include <cstdint>
#include <limits>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace local_normalization_simd
{
namespace HWY_NAMESPACE
{

// The scalar authority's row body, compiled verbatim for this target.
#include "kernels/reference/LocalNormalizationRow.inl"

} // namespace HWY_NAMESPACE
} // namespace local_normalization_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace local_normalization_simd
{
namespace
{

constexpr LocalNormalizationCpuRuns kAvx2Runs{.float32 = HWY_CHOOSE_AVX2(localNormalizationRunOf<float>),
                                              .float64 = HWY_CHOOSE_AVX2(localNormalizationRunOf<double>)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr LocalNormalizationCpuRuns kAvx3Runs{.float32 = HWY_CHOOSE_AVX3(localNormalizationRunOf<float>),
                                              .float64 = HWY_CHOOSE_AVX3(localNormalizationRunOf<double>)};
#endif

} // namespace
} // namespace local_normalization_simd

const LocalNormalizationCpuRuns* simdLocalNormalizationRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &local_normalization_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &local_normalization_simd::kAvx3Runs;
#else
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return nullptr;
    }
    return nullptr;
}

} // namespace an

#endif // HWY_ONCE
