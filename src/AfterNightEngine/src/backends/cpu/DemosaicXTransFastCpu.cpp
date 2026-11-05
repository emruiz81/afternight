// AfterNightEngine - the compiled fast X-Trans estimate, one per instruction set.
//
// The estimate is the port in `kernels/reference/DemosaicKernelsXTransFast.cpp`, derived from
// RawTherapee's fast X-Trans demosaic as librtprocess carries it (`demosaic/xtransfast.cc`, which names
// no copyright holder), distributed under the GNU General Public License, version 3 or any later
// version; this file is part of that derived work and is distributed under the same licence. What is
// here is AfterNight's: the lane-wise spelling of the reference's passes in its order of evaluation. The
// provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// Every lane has a different position in the periodic filter table. The table lookup stays scalar
// and produces masks; all nine weighted sample accumulations and all three divisions are vector
// arithmetic. Contributions are added in the reference's row-major order, so the result equals the
// reference's bit for bit. A target wider than the mask buffer runs the reference instead.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "backends/cpu/DemosaicXTransFastCpu.h"

#include <array>
#include <cstddef>
#include <cstdint>

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/DemosaicXTransFastCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace demosaic_xtrans_fast_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// Which rung of the ladder this compilation of the file belongs to.
///
/// @return the instruction set this body was compiled for.
[[nodiscard]] InstructionSet isaOfCompiledXTransFast() noexcept
{
#if HWY_TARGET == HWY_AVX3
    return InstructionSet::Avx512Skylake;
#else
    return InstructionSet::Avx2;
#endif
}

/// Estimates one tile, lane by lane: each colour is the weighted mean of the site's eight neighbours
/// of that colour, the site's own colour is its measured sample, and so is any colour with no
/// neighbour in the window.
///
/// @param tile the region to write and the picture to read it from.
/// @param scratch unused, except by the reference when the target is wider than the mask buffer.
/// @param scratchFloats as `scratch`.
void xtransFastRun(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    static_cast<void>(scratch);
    static_cast<void>(scratchFloats);
    constexpr std::array<float, 9> kWeights = {0.25f, 0.5f, 0.25f, 0.5f, 0.0f, 0.5f, 0.25f, 0.5f, 0.25f};
    constexpr std::uint32_t kMaskCapacity = 64u;
    const CfaDescriptor& cfa = *tile.cfa;
    const hn::ScalableTag<float> lanes;
    const hn::RebindToUnsigned<decltype(lanes)> unsignedLanes;
    const std::uint32_t width = static_cast<std::uint32_t>(hn::Lanes(lanes));
    if (width > kMaskCapacity)
    {
        demosaicXTransFastReference(tile, scratch, scratchFloats);
        return;
    }
    const auto zero = hn::Zero(lanes);
    const auto redCode = hn::Set(unsignedLanes, static_cast<std::uint32_t>(CfaColor::Red));
    const auto greenCode = hn::Set(unsignedLanes, static_cast<std::uint32_t>(CfaColor::Green));
    const auto blueCode = hn::Set(unsignedLanes, static_cast<std::uint32_t>(CfaColor::Blue));
    alignas(64) std::uint32_t colorCodes[kMaskCapacity]{};

    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t centreRow = static_cast<std::uint64_t>(y) * tile.sourceStride;
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        std::uint32_t x = tile.xBegin;
        for (; x + width <= tile.xEnd; x += width)
        {
            auto redSum = zero;
            auto greenSum = zero;
            auto blueSum = zero;
            auto redWeight = zero;
            auto greenWeight = zero;
            auto blueWeight = zero;
            for (std::uint32_t row = 0u; row < 3u; ++row)
            {
                const std::uint32_t neighbourY = y + row - 1u;
                const std::uint64_t sourceRow = static_cast<std::uint64_t>(neighbourY) * tile.sourceStride;
                for (std::uint32_t column = 0u; column < 3u; ++column)
                {
                    const std::uint32_t neighbourX = x + column - 1u;
                    for (std::uint32_t lane = 0u; lane < width; ++lane)
                    {
                        colorCodes[lane] = static_cast<std::uint32_t>(cfa.colorAt(neighbourX + lane, neighbourY));
                    }
                    const auto codes = hn::LoadU(unsignedLanes, colorCodes);
                    const auto onRed = hn::RebindMask(lanes, hn::Eq(codes, redCode));
                    const auto onGreen = hn::RebindMask(lanes, hn::Eq(codes, greenCode));
                    const auto onBlue = hn::RebindMask(lanes, hn::Eq(codes, blueCode));
                    const auto share = hn::Set(lanes, kWeights[static_cast<std::size_t>(row) * 3u + column]);
                    const auto contribution = hn::Mul(hn::LoadU(lanes, tile.source + sourceRow + neighbourX), share);
                    redSum = hn::Add(redSum, hn::IfThenElse(onRed, contribution, zero));
                    greenSum = hn::Add(greenSum, hn::IfThenElse(onGreen, contribution, zero));
                    blueSum = hn::Add(blueSum, hn::IfThenElse(onBlue, contribution, zero));
                    redWeight = hn::Add(redWeight, hn::IfThenElse(onRed, share, zero));
                    greenWeight = hn::Add(greenWeight, hn::IfThenElse(onGreen, share, zero));
                    blueWeight = hn::Add(blueWeight, hn::IfThenElse(onBlue, share, zero));
                }
            }

            for (std::uint32_t lane = 0u; lane < width; ++lane)
            {
                colorCodes[lane] = static_cast<std::uint32_t>(cfa.colorAt(x + lane, y));
            }
            const auto ownCodes = hn::LoadU(unsignedLanes, colorCodes);
            const auto measured = hn::LoadU(lanes, tile.source + centreRow + x);
            const auto red =
                hn::IfThenElse(hn::RebindMask(lanes, hn::Eq(ownCodes, redCode)), measured,
                               hn::IfThenElse(hn::Le(redWeight, zero), measured, hn::Div(redSum, redWeight)));
            const auto green =
                hn::IfThenElse(hn::RebindMask(lanes, hn::Eq(ownCodes, greenCode)), measured,
                               hn::IfThenElse(hn::Le(greenWeight, zero), measured, hn::Div(greenSum, greenWeight)));
            const auto blue =
                hn::IfThenElse(hn::RebindMask(lanes, hn::Eq(ownCodes, blueCode)), measured,
                               hn::IfThenElse(hn::Le(blueWeight, zero), measured, hn::Div(blueSum, blueWeight)));
            hn::StoreU(red, lanes, tile.red + destinationRow + x);
            hn::StoreU(green, lanes, tile.green + destinationRow + x);
            hn::StoreU(blue, lanes, tile.blue + destinationRow + x);
        }

        for (; x < tile.xEnd; ++x)
        {
            std::array<float, 3> sum{};
            std::array<float, 3> weight{};
            for (std::uint32_t row = 0u; row < 3u; ++row)
            {
                const std::uint32_t neighbourY = y + row - 1u;
                const std::uint64_t sourceRow = static_cast<std::uint64_t>(neighbourY) * tile.sourceStride;
                for (std::uint32_t column = 0u; column < 3u; ++column)
                {
                    const std::uint32_t neighbourX = x + column - 1u;
                    const std::size_t color = static_cast<std::size_t>(cfa.colorAt(neighbourX, neighbourY));
                    const float share = kWeights[static_cast<std::size_t>(row) * 3u + column];
                    sum[color] += tile.source[sourceRow + neighbourX] * share;
                    weight[color] += share;
                }
            }
            const float measured = tile.source[centreRow + x];
            const std::size_t own = static_cast<std::size_t>(cfa.colorAt(x, y));
            std::array<float, 3> estimate{};
            for (std::size_t color = 0u; color < 3u; ++color)
            {
                estimate[color] = (color == own || weight[color] <= 0.0f) ? measured : sum[color] / weight[color];
            }
            tile.red[destinationRow + x] = estimate[0];
            tile.green[destinationRow + x] = estimate[1];
            tile.blue[destinationRow + x] = estimate[2];
        }
    }
}

} // namespace HWY_NAMESPACE
} // namespace demosaic_xtrans_fast_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace demosaic_xtrans_fast_simd
{
namespace
{

constexpr DemosaicRunFn kXTransFastAvx2 = HWY_CHOOSE_AVX2(xtransFastRun);
constexpr InstructionSet (*kXTransFastAvx2Isa)() noexcept = HWY_CHOOSE_AVX2(isaOfCompiledXTransFast);

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr DemosaicRunFn kXTransFastAvx3 = HWY_CHOOSE_AVX3(xtransFastRun);
constexpr InstructionSet (*kXTransFastAvx3Isa)() noexcept = HWY_CHOOSE_AVX3(isaOfCompiledXTransFast);
#endif

} // namespace
} // namespace demosaic_xtrans_fast_simd

DemosaicRunFn simdXTransFastRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_xtrans_fast_simd::kXTransFastAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_xtrans_fast_simd::kXTransFastAvx3;
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

InstructionSet simdXTransFastIsa(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_xtrans_fast_simd::kXTransFastAvx2Isa();
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_xtrans_fast_simd::kXTransFastAvx3Isa();
#else
        return InstructionSet::Automatic;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return InstructionSet::Automatic;
    }
    return InstructionSet::Automatic;
}

} // namespace an

#endif // HWY_ONCE
