// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the global-normalization row, compiled once per instruction set.
//
// The global-normalization row is isolated from the shared SIMD table so its per-instruction-set
// object cost remains attributable. Each admitted sample is one Float64 multiply-add narrowed to
// the resident type; the ordinary expression is compiled under each target without permitting an
// implicit contraction that would move the scalar authority's rounding point.

#include "backends/cpu/NormalizationCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/NormalizationCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <cmath>
#include <cstdint>
#include <limits>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace normalization_simd
{
namespace HWY_NAMESPACE
{

/// Writes one contiguous row of normalization while retaining the scalar authority's rounding point.
///
/// A sample the rule rejects is copied through unchanged. Under the zero-is-no-data rule, a result
/// that lands on exactly zero is written as the smallest normal value with the sign of the scaled
/// sample instead, so a transformed sample never becomes no-data, and the collision is counted.
///
/// @tparam SampleT the resident floating representation.
/// @param source the samples to transform.
/// @param destination where transformed or preserved samples are written.
/// @param pixelCount how many pixels the row contains.
/// @param parameters the channel, validity, and affine choices.
/// @return how many samples were transformed and preserved, and how many results were moved off
///         zero.
template <typename SampleT>
[[nodiscard]] NormalizationRunOutcome normalizationRunOf(const SampleT* source, SampleT* destination,
                                                         std::uint64_t pixelCount,
                                                         const NormalizationRunParameters& parameters) noexcept
{
    NormalizationRunOutcome outcome{};
    const bool zeroRule = parameters.rule == ValidityRule::ZeroIsNoData;
    const bool finiteRule = parameters.rule == ValidityRule::FiniteOnly;
    for (std::uint64_t pixel = 0u; pixel < pixelCount; ++pixel)
    {
        for (std::uint32_t channel = 0u; channel < parameters.channelCount; ++channel)
        {
            const std::uint64_t sample = pixel * parameters.channelCount + channel;
            const SampleT value = source[sample];
            const bool admitted = finiteRule ? (value >= -(std::numeric_limits<SampleT>::max)() &&
                                                value <= (std::numeric_limits<SampleT>::max)())
                                             : (!zeroRule || value != static_cast<SampleT>(0));
            if (!admitted)
            {
                destination[sample] = value;
                ++outcome.samplesPreserved;
                continue;
            }
            const double scaled = static_cast<double>(value) * parameters.scale[channel];
            SampleT written =
                static_cast<SampleT>(parameters.multiplicative ? scaled : scaled + parameters.offset[channel]);
            if (zeroRule && written == static_cast<SampleT>(0))
            {
                written = std::copysign((std::numeric_limits<SampleT>::min)(), static_cast<SampleT>(scaled));
                ++outcome.noDataCollisions;
            }
            destination[sample] = written;
            ++outcome.samplesTransformed;
        }
    }
    return outcome;
}

} // namespace HWY_NAMESPACE
} // namespace normalization_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace normalization_simd
{
namespace
{

constexpr NormalizationCpuRuns kAvx2Runs{.float32 = HWY_CHOOSE_AVX2(normalizationRunOf<float>),
                                         .float64 = HWY_CHOOSE_AVX2(normalizationRunOf<double>)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr NormalizationCpuRuns kAvx3Runs{.float32 = HWY_CHOOSE_AVX3(normalizationRunOf<float>),
                                         .float64 = HWY_CHOOSE_AVX3(normalizationRunOf<double>)};
#endif

} // namespace
} // namespace normalization_simd

const NormalizationCpuRuns* simdNormalizationRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &normalization_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &normalization_simd::kAvx3Runs;
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
