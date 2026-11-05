// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the linear presentation of narrow codes, compiled once per instruction set.
//
// The row is isolated from the shared SIMD table so its per-instruction-set object cost remains attributable to the
// presentation. A row presents every n-th code of a run as a byte. At a stride of one to four a vector of codes is
// read in one load or one interleaved load - which reads a whole group of codes and keeps the first of each - and
// becomes bytes through the scalar authority's three sixteen-bit steps; every other stride, and the codes past the
// last whole vector, run the scalar authority's own text.

#include "backends/cpu/PresentationCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/PresentationCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <algorithm>
#include <cstdint>
#include <type_traits>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace presentation_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

// The scalar authority, compiled here for the codes a vector does not cover.
#include "kernels/reference/PresentationRow.inl"

/// Writes the bytes one vector of codes presents as.
///
/// @tparam TagT the vector tag of the codes.
/// @param tag the tag.
/// @param codes the codes, in output order.
/// @param destination where their bytes go.
template <typename TagT> void storePresentedBytes(TagT tag, hn::Vec<TagT> codes, std::uint8_t* destination) noexcept
{
    using CodeT = hn::TFromD<TagT>;
    if constexpr (std::is_same_v<CodeT, std::uint8_t>)
    {
        hn::StoreU(codes, tag, destination);
    }
    else
    {
        const hn::Rebind<std::uint8_t, TagT> bytes;
        const auto high = hn::MulHigh(codes, hn::Set(tag, static_cast<std::uint16_t>(65281u)));
        const auto presented = hn::ShiftRight<8>(hn::Add(high, hn::Set(tag, static_cast<std::uint16_t>(128u))));
        hn::StoreU(hn::DemoteTo(bytes, presented), bytes, destination);
    }
}

/// Presents every `step`-th of a run's codes as bytes.
///
/// A vector at output position `x` reads the codes from `x * step` to `(x + lanes) * step`, so the vector loop stops
/// where that would pass the run's `sourceSamples` codes, and the scalar authority writes the rest.
///
/// @tparam CodeT `std::uint8_t` or `std::uint16_t`.
/// @param source the run's first code.
/// @param sourceSamples how many codes the run holds from `source` on.
/// @param step codes between two presented ones.
/// @param destination the row's first byte.
/// @param count how many bytes the row writes.
template <typename CodeT>
void presentationCodeRowOf(const void* source, std::uint64_t sourceSamples, std::uint64_t step,
                           std::uint8_t* destination, std::uint64_t count) noexcept
{
    const CodeT* const in = static_cast<const CodeT*>(source);
    const hn::ScalableTag<CodeT> tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const std::uint64_t limit = (std::min)(count, sourceSamples / step);
    std::uint64_t x = 0u;
    switch (step)
    {
    case 1u:
        for (; x + lanes <= limit; x += lanes)
        {
            storePresentedBytes(tag, hn::LoadU(tag, in + x), destination + x);
        }
        break;
    case 2u:
        for (; x + lanes <= limit; x += lanes)
        {
            hn::Vec<decltype(tag)> first;
            hn::Vec<decltype(tag)> second;
            hn::LoadInterleaved2(tag, in + x * 2u, first, second);
            storePresentedBytes(tag, first, destination + x);
        }
        break;
    case 3u:
        for (; x + lanes <= limit; x += lanes)
        {
            hn::Vec<decltype(tag)> first;
            hn::Vec<decltype(tag)> second;
            hn::Vec<decltype(tag)> third;
            hn::LoadInterleaved3(tag, in + x * 3u, first, second, third);
            storePresentedBytes(tag, first, destination + x);
        }
        break;
    case 4u:
        for (; x + lanes <= limit; x += lanes)
        {
            hn::Vec<decltype(tag)> first;
            hn::Vec<decltype(tag)> second;
            hn::Vec<decltype(tag)> third;
            hn::Vec<decltype(tag)> fourth;
            hn::LoadInterleaved4(tag, in + x * 4u, first, second, third, fourth);
            storePresentedBytes(tag, first, destination + x);
        }
        break;
    default:
        break;
    }
    presentCodeRow(in, step, destination, x, count);
}

} // namespace HWY_NAMESPACE
} // namespace presentation_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace presentation_simd
{
namespace
{

constexpr PresentationCpuRuns kAvx2Runs{.uint8 = HWY_CHOOSE_AVX2(presentationCodeRowOf<std::uint8_t>),
                                        .uint16 = HWY_CHOOSE_AVX2(presentationCodeRowOf<std::uint16_t>)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr PresentationCpuRuns kAvx3Runs{.uint8 = HWY_CHOOSE_AVX3(presentationCodeRowOf<std::uint8_t>),
                                        .uint16 = HWY_CHOOSE_AVX3(presentationCodeRowOf<std::uint16_t>)};
#endif

} // namespace
} // namespace presentation_simd

const PresentationCpuRuns* simdPresentationRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &presentation_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &presentation_simd::kAvx3Runs;
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
