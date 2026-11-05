// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack source row, compiled once per instruction set.
//
// The stack source row is isolated from the shared SIMD table so its per-instruction-set object
// cost remains attributable: eight stored representations widened, calibrated, transformed and
// matched in one pass is a larger body than any single kernel family carries. The row is the one
// text shared with the scalar algorithm - the local-normalization row body it composes included -
// compiled here once per target, so the compiled row stores the scalar authority's bits by
// construction; the differential tests prove it over every representation and every stage rather
// than assume it.

#include "backends/cpu/StackSourceCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/StackSourceCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <AfterNightEngine/Image/SampleConversion.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace stack_source_simd
{
namespace HWY_NAMESPACE
{

// The local-normalization row body the source path composes, and the source row itself, both
// compiled verbatim for this target.
#include "kernels/reference/LocalNormalizationRow.inl"
#include "kernels/reference/StackSourceRow.inl"

/// The tile representation one stored representation is widened to.
template <typename SourceT>
using StackSourceDestinationOf = std::conditional_t<std::is_same_v<SourceT, double>, double, float>;

/// The source row of one stored representation, named by that representation alone so the
/// per-target chooser can spell it.
///
/// @tparam SourceT the stored representation.
/// @param source the row's first stored sample.
/// @param destination where the row goes.
/// @param pixelCount how many pixels the row holds.
/// @param parameters the rule, the conversion, the stages and their inputs.
/// @return what the row did.
template <typename SourceT>
[[nodiscard]] StackSourceRunOutcome
stackSourceRowOf(const SourceT* source, StackSourceDestinationOf<SourceT>* destination, std::uint64_t pixelCount,
                 const StackSourceRunParameters& parameters) noexcept
{
    return stackSourceRunOf<SourceT, StackSourceDestinationOf<SourceT>>(source, destination, pixelCount, parameters);
}

} // namespace HWY_NAMESPACE
} // namespace stack_source_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace stack_source_simd
{
namespace
{

constexpr StackSourceCpuRuns kAvx2Runs{.uint8 = HWY_CHOOSE_AVX2(stackSourceRowOf<std::uint8_t>),
                                       .int8 = HWY_CHOOSE_AVX2(stackSourceRowOf<std::int8_t>),
                                       .uint16 = HWY_CHOOSE_AVX2(stackSourceRowOf<std::uint16_t>),
                                       .int16 = HWY_CHOOSE_AVX2(stackSourceRowOf<std::int16_t>),
                                       .uint32 = HWY_CHOOSE_AVX2(stackSourceRowOf<std::uint32_t>),
                                       .int32 = HWY_CHOOSE_AVX2(stackSourceRowOf<std::int32_t>),
                                       .float32 = HWY_CHOOSE_AVX2(stackSourceRowOf<float>),
                                       .float64 = HWY_CHOOSE_AVX2(stackSourceRowOf<double>)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr StackSourceCpuRuns kAvx3Runs{.uint8 = HWY_CHOOSE_AVX3(stackSourceRowOf<std::uint8_t>),
                                       .int8 = HWY_CHOOSE_AVX3(stackSourceRowOf<std::int8_t>),
                                       .uint16 = HWY_CHOOSE_AVX3(stackSourceRowOf<std::uint16_t>),
                                       .int16 = HWY_CHOOSE_AVX3(stackSourceRowOf<std::int16_t>),
                                       .uint32 = HWY_CHOOSE_AVX3(stackSourceRowOf<std::uint32_t>),
                                       .int32 = HWY_CHOOSE_AVX3(stackSourceRowOf<std::int32_t>),
                                       .float32 = HWY_CHOOSE_AVX3(stackSourceRowOf<float>),
                                       .float64 = HWY_CHOOSE_AVX3(stackSourceRowOf<double>)};
#endif

} // namespace
} // namespace stack_source_simd

const StackSourceCpuRuns* simdStackSourceRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &stack_source_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &stack_source_simd::kAvx3Runs;
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
