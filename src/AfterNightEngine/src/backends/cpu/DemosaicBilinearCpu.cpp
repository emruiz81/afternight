// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the bilinear estimate, compiled once per instruction set.
//
// A lane-wise transcription of the four cases the scalar reference is written as, in the same
// arithmetic and the same order, so the two agree bit for bit rather than to a tolerance. The whole
// of the vectorisation is that both branches of each case are computed for every lane and the row's
// column parity selects between them - which costs two extra multiplies per pixel and removes the
// per-pixel branch that would keep a compiled loop at one pixel at a time.

#include "backends/cpu/DemosaicBilinearCpu.h"

#include <cstdint>

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/DemosaicBilinearCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace demosaic_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// Which rung of the ladder this compilation of the file belongs to.
///
/// @return the instruction set this body was compiled for.
[[nodiscard]] InstructionSet isaOfCompiledBilinear() noexcept
{
#if HWY_TARGET == HWY_AVX3
    return InstructionSet::Avx512Skylake;
#else
    return InstructionSet::Avx2;
#endif
}

/// Estimates one tile, lane by lane.
///
/// @param tile the region to write and the picture to read it from.
/// @param scratch unused.
/// @param scratchFloats unused.
void bilinearRun(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    static_cast<void>(scratch);
    static_cast<void>(scratchFloats);
    const hn::ScalableTag<float> lanes;
    const hn::RebindToUnsigned<decltype(lanes)> indices;
    const std::size_t width = hn::Lanes(lanes);
    const auto quarter = hn::Set(lanes, 0.25f);
    const auto half = hn::Set(lanes, 0.5f);
    const auto one = hn::Set(indices, 1u);
    const auto zero = hn::Zero(indices);
    const CfaDescriptor& cfa = *tile.cfa;

    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        const std::uint64_t centreRow = static_cast<std::uint64_t>(y) * tile.sourceStride;
        const std::uint64_t aboveRow = centreRow - tile.sourceStride;
        const std::uint64_t belowRow = centreRow + tile.sourceStride;
        const bool greenAtBegin = cfa.colorAt(tile.xBegin, y) == CfaColor::Green;
        const std::uint32_t colourColumn = greenAtBegin ? tile.xBegin + 1u : tile.xBegin;
        const bool rowIsRed = cfa.colorAt(colourColumn, y) == CfaColor::Red;
        float* const rowColour = rowIsRed ? tile.red : tile.blue;
        float* const otherColour = rowIsRed ? tile.blue : tile.red;

        std::uint32_t x = tile.xBegin;
        for (; x + width <= tile.xEnd; x += static_cast<std::uint32_t>(width))
        {
            const auto centre = hn::LoadU(lanes, tile.source + centreRow + x);
            const auto above = hn::LoadU(lanes, tile.source + aboveRow + x);
            const auto below = hn::LoadU(lanes, tile.source + belowRow + x);
            const auto left = hn::LoadU(lanes, tile.source + centreRow + x - 1u);
            const auto right = hn::LoadU(lanes, tile.source + centreRow + x + 1u);
            const auto orthogonal = hn::Mul(hn::Add(hn::Add(above, below), hn::Add(left, right)), quarter);
            const auto diagonal = hn::Mul(hn::Add(hn::Add(hn::LoadU(lanes, tile.source + aboveRow + x - 1u),
                                                          hn::LoadU(lanes, tile.source + aboveRow + x + 1u)),
                                                  hn::Add(hn::LoadU(lanes, tile.source + belowRow + x - 1u),
                                                          hn::LoadU(lanes, tile.source + belowRow + x + 1u))),
                                          quarter);
            const auto horizontal = hn::Mul(hn::Add(left, right), half);
            const auto vertical = hn::Mul(hn::Add(above, below), half);

            // The row alternates between a green site and a colour site, so the parity of the lane
            // index against the parity the row starts on is the whole of the case selection.
            const auto position = hn::Add(hn::Iota(indices, 0u), hn::Set(indices, x - tile.xBegin));
            const auto evenLane = hn::Eq(hn::And(position, one), zero);
            const auto onGreen =
                greenAtBegin ? hn::RebindMask(lanes, evenLane) : hn::Not(hn::RebindMask(lanes, evenLane));

            hn::StoreU(hn::IfThenElse(onGreen, centre, orthogonal), lanes, tile.green + destinationRow + x);
            hn::StoreU(hn::IfThenElse(onGreen, horizontal, centre), lanes, rowColour + destinationRow + x);
            hn::StoreU(hn::IfThenElse(onGreen, vertical, diagonal), lanes, otherColour + destinationRow + x);
        }
        for (; x < tile.xEnd; ++x)
        {
            const float centre = tile.source[centreRow + x];
            const float above = tile.source[aboveRow + x];
            const float below = tile.source[belowRow + x];
            const float left = tile.source[centreRow + x - 1u];
            const float right = tile.source[centreRow + x + 1u];
            const float orthogonal = ((above + below) + (left + right)) * 0.25f;
            const float diagonal = ((tile.source[aboveRow + x - 1u] + tile.source[aboveRow + x + 1u]) +
                                    (tile.source[belowRow + x - 1u] + tile.source[belowRow + x + 1u])) *
                                   0.25f;
            const float horizontal = (left + right) * 0.5f;
            const float vertical = (above + below) * 0.5f;
            const bool onGreen = (((x - tile.xBegin) & 1u) == 0u) == greenAtBegin;
            tile.green[destinationRow + x] = onGreen ? centre : orthogonal;
            rowColour[destinationRow + x] = onGreen ? horizontal : centre;
            otherColour[destinationRow + x] = onGreen ? vertical : diagonal;
        }
    }
}

} // namespace HWY_NAMESPACE
} // namespace demosaic_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace demosaic_simd
{
namespace
{

/// The bilinear estimate compiled for the packaged floor.
constexpr DemosaicRunFn kBilinearAvx2 = HWY_CHOOSE_AVX2(bilinearRun);

/// What that body says it was compiled for.
constexpr InstructionSet (*kBilinearAvx2Isa)() noexcept = HWY_CHOOSE_AVX2(isaOfCompiledBilinear);

#if (HWY_TARGETS & HWY_AVX3) != 0
/// The same estimate compiled for the Skylake-server subset, on the builds that have it.
constexpr DemosaicRunFn kBilinearAvx3 = HWY_CHOOSE_AVX3(bilinearRun);

/// What that body says it was compiled for.
constexpr InstructionSet (*kBilinearAvx3Isa)() noexcept = HWY_CHOOSE_AVX3(isaOfCompiledBilinear);
#endif

} // namespace
} // namespace demosaic_simd

DemosaicRunFn simdBilinearRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_simd::kBilinearAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_simd::kBilinearAvx3;
#else
        // The vector abstraction marks this target broken on this compiler, so this build genuinely
        // contains no such kernel and says so rather than handing back the floor's under its name.
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        // Listed rather than covered by a default label, so a rung added to the ladder fails this
        // switch on a compiler that reports an unhandled case instead of silently answering null.
        return nullptr;
    }
    return nullptr;
}

InstructionSet simdBilinearIsa(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_simd::kBilinearAvx2Isa();
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_simd::kBilinearAvx3Isa();
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
