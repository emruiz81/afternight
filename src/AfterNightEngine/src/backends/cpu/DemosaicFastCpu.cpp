// AfterNightEngine - the gradient-weighted estimate, compiled once per instruction set.
//
// The estimate is the port in `kernels/reference/DemosaicKernelsFast.cpp`, derived from RawTherapee's
// fast demosaic as librtprocess carries it (`demosaic/bayerfast.cc`), which states
// `copyright (c) 2008-2010  Emil Martinec <ejmartin@uchicago.edu>` and is distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. What is here is AfterNight's: the lane-wise spelling of the
// reference's passes in its order of evaluation. The provenance note is
// `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// This is the cheapest estimate the family offers and the one a registration detection loop runs on
// every frame, where it takes most of the loop's time, so it is the one place in this family where a
// vector kernel changes what a user waits for rather than a number in a report.
//
// **It is bit-exact against the scalar reference, and that costs one thing on purpose.** Every sum
// here is associated exactly as the reference associates it - left to right, four terms - rather
// than pairwise, which is what a vector body would otherwise do and what would be marginally better
// conditioned. Pairing them would make this kernel disagree with the reference in the last bits, and
// then a conformance row would have to carry a tolerance instead of an equality. The reference in
// turn associates the way librtprocess's scalar dialect does, because that dialect is this method's
// upstream reference. Contraction is off in every translation unit of this engine and the
// weights are exact divisions rather than the twelve-bit reciprocal the library's own vector dialect
// uses, so the equality holds rather than nearly holding.
//
// **Three of the four passes write half the pixels of a row.** Rather than deinterleaving each row,
// every lane computes its value and the row's column parity selects between the new value and the
// one already there. That is a select against a gather, and it keeps the vector body the same shape
// as the scalar one - which is what makes reading the two side by side a check rather than an act of
// faith.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "backends/cpu/DemosaicFastCpu.h"

#include <cmath>
#include <cstdint>

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/DemosaicFastCpu.cpp"
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
[[nodiscard]] InstructionSet isaOfCompiledFast() noexcept
{
#if HWY_TARGET == HWY_AVX3
    return InstructionSet::Avx512Skylake;
#else
    return InstructionSet::Avx2;
#endif
}

/// The directional weight one gradient earns, in the reference's own arithmetic.
///
/// @param gradient the summed absolute differences along one direction.
/// @return the weight.
[[nodiscard]] inline float scalarWeight(float gradient) noexcept
{
    const float base = 4.0f + gradient;
    return 16.0f / (base * base);
}

/// Estimates one tile, lane by lane.
///
/// @param tile the region to write and the picture to read it from.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds.
void fastRun(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    constexpr std::uint32_t kPad = 2u;
    const std::uint32_t regionWidth = tile.xEnd - tile.xBegin;
    const std::uint32_t regionHeight = tile.yEnd - tile.yBegin;
    const std::uint64_t paddedWidth = static_cast<std::uint64_t>(regionWidth) + 2u * kPad;
    const std::uint64_t paddedHeight = static_cast<std::uint64_t>(regionHeight) + 2u * kPad;
    const std::uint64_t plane = paddedWidth * paddedHeight;
    if (regionWidth == 0u || regionHeight == 0u || scratchFloats < 3u * plane)
    {
        return;
    }
    float* const greenTile = scratch;
    float* const redTile = scratch + plane;
    float* const blueTile = scratch + 2u * plane;

    const std::uint32_t padBeginX = tile.xBegin - kPad;
    const std::uint32_t padBeginY = tile.yBegin - kPad;
    const CfaDescriptor& cfa = *tile.cfa;
    const float clipSum = 4.0f * tile.clipPoint;
    // Signed, and every neighbour above a row is reached by moving the row pointer rather than by
    // subtracting the stride from the column.  A column index is smaller than a stride, so the second
    // form wraps the unsigned index round and forms the address by pointer overflow.
    const auto sourceStride = static_cast<std::int64_t>(tile.sourceStride);

    const hn::ScalableTag<float> lanes;
    const hn::RebindToUnsigned<decltype(lanes)> indices;
    const auto width = static_cast<std::uint32_t>(hn::Lanes(lanes));
    const auto four = hn::Set(lanes, 4.0f);
    const auto sixteen = hn::Set(lanes, 16.0f);
    const auto quarter = hn::Set(lanes, 0.25f);
    const auto clipVector = hn::Set(lanes, clipSum);
    const auto oneIndex = hn::Set(indices, 1u);
    const auto zeroIndex = hn::Zero(indices);

    /// The lanes of a run whose column parity matches the row's green parity.
    const auto greenLanes = [&](std::uint32_t x, std::uint32_t origin, bool greenAtOrigin) {
        const auto position = hn::Add(hn::Iota(indices, 0u), hn::Set(indices, x - origin));
        const auto even = hn::RebindMask(lanes, hn::Eq(hn::And(position, oneIndex), zeroIndex));
        return greenAtOrigin ? even : hn::Not(even);
    };
    /// The directional weight one gradient earns.
    const auto weightOf = [&](auto gradient) {
        const auto base = hn::Add(four, gradient);
        return hn::Div(sixteen, hn::Mul(base, base));
    };

    // Green everywhere, and the measured sample parked in both colour planes.
    for (std::uint32_t y = padBeginY; y < padBeginY + paddedHeight; ++y)
    {
        const std::uint64_t tileRow = static_cast<std::uint64_t>(y - padBeginY) * paddedWidth;
        const bool greenAtStart = cfa.colorAt(padBeginX, y) == CfaColor::Green;
        const float* const source = tile.source + static_cast<std::uint64_t>(y) * tile.sourceStride;
        const std::uint32_t last = padBeginX + static_cast<std::uint32_t>(paddedWidth);
        std::uint32_t x = padBeginX;
        for (; x + width <= last; x += width)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            const auto measured = hn::LoadU(lanes, source + x);
            hn::StoreU(measured, lanes, redTile + index);
            hn::StoreU(measured, lanes, blueTile + index);

            const auto above = hn::LoadU(lanes, source - sourceStride + x);
            const auto below = hn::LoadU(lanes, source + sourceStride + x);
            const auto left = hn::LoadU(lanes, source + x - 1u);
            const auto right = hn::LoadU(lanes, source + x + 1u);
            const auto verticalSpan = hn::Abs(hn::Sub(below, above));
            const auto horizontalSpan = hn::Abs(hn::Sub(right, left));
            const auto upward = weightOf(hn::Add(
                hn::Add(verticalSpan, hn::Abs(hn::Sub(measured, hn::LoadU(lanes, source - 2 * sourceStride + x)))),
                hn::Abs(hn::Sub(above, hn::LoadU(lanes, source - 3 * sourceStride + x)))));
            const auto downward = weightOf(hn::Add(
                hn::Add(verticalSpan, hn::Abs(hn::Sub(measured, hn::LoadU(lanes, source + 2 * sourceStride + x)))),
                hn::Abs(hn::Sub(below, hn::LoadU(lanes, source + 3 * sourceStride + x)))));
            const auto leftward =
                weightOf(hn::Add(hn::Add(horizontalSpan, hn::Abs(hn::Sub(measured, hn::LoadU(lanes, source + x - 2u)))),
                                 hn::Abs(hn::Sub(left, hn::LoadU(lanes, source + x - 3u)))));
            const auto rightward =
                weightOf(hn::Add(hn::Add(horizontalSpan, hn::Abs(hn::Sub(measured, hn::LoadU(lanes, source + x + 2u)))),
                                 hn::Abs(hn::Sub(right, hn::LoadU(lanes, source + x + 3u)))));
            const auto numerator =
                hn::Add(hn::Add(hn::Add(hn::Mul(upward, above), hn::Mul(downward, below)), hn::Mul(leftward, left)),
                        hn::Mul(rightward, right));
            const auto denominator = hn::Add(hn::Add(hn::Add(upward, downward), leftward), rightward);
            hn::StoreU(
                hn::IfThenElse(greenLanes(x, padBeginX, greenAtStart), measured, hn::Div(numerator, denominator)),
                lanes, greenTile + index);
        }
        // Fewer than one Highway vector remains. Letting the compiler vectorize this scalar tail
        // emits a second masked vector body for work that can never fill one lane group.
#if defined(__clang__)
#pragma clang loop vectorize(disable)
#pragma clang loop interleave(disable)
#endif
        for (; x < last; ++x)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            const float measured = source[x];
            redTile[index] = measured;
            blueTile[index] = measured;
            if ((((x - padBeginX) & 1u) == 0u) == greenAtStart)
            {
                greenTile[index] = measured;
                continue;
            }
            const float above = *(source - sourceStride + x);
            const float below = *(source + sourceStride + x);
            const float left = source[x - 1u];
            const float right = source[x + 1u];
            const float verticalSpan = std::fabs(below - above);
            const float horizontalSpan = std::fabs(right - left);
            const float upward = scalarWeight(verticalSpan + std::fabs(measured - *(source - 2 * sourceStride + x)) +
                                              std::fabs(above - *(source - 3 * sourceStride + x)));
            const float downward = scalarWeight(verticalSpan + std::fabs(measured - *(source + 2 * sourceStride + x)) +
                                                std::fabs(below - *(source + 3 * sourceStride + x)));
            const float leftward =
                scalarWeight(horizontalSpan + std::fabs(measured - source[x - 2u]) + std::fabs(left - source[x - 3u]));
            const float rightward =
                scalarWeight(horizontalSpan + std::fabs(measured - source[x + 2u]) + std::fabs(right - source[x + 3u]));
            greenTile[index] = (upward * above + downward * below + leftward * left + rightward * right) /
                               (upward + downward + leftward + rightward);
        }
    }

    // The opposite colour at every red and blue site, from the colour difference over its diagonals.
    for (std::uint32_t y = tile.yBegin - 1u; y < tile.yEnd + 1u; ++y)
    {
        const std::uint64_t tileRow = static_cast<std::uint64_t>(y - padBeginY) * paddedWidth;
        const std::uint32_t firstX = tile.xBegin - 1u;
        const bool greenAtStart = cfa.colorAt(firstX, y) == CfaColor::Green;
        const std::uint32_t offset = greenAtStart ? 1u : 0u;
        const bool writesBlue = cfa.colorAt(firstX + offset, y) == CfaColor::Red;
        float* const destinationPlane = writesBlue ? blueTile : redTile;
        const float* const source = tile.source + static_cast<std::uint64_t>(y) * tile.sourceStride;
        const std::uint32_t last = tile.xEnd + 1u;
        std::uint32_t x = firstX;
        for (; x + width <= last; x += width)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            const auto greenCorners = hn::Add(hn::Add(hn::Add(hn::LoadU(lanes, greenTile + index - paddedWidth - 1u),
                                                              hn::LoadU(lanes, greenTile + index - paddedWidth + 1u)),
                                                      hn::LoadU(lanes, greenTile + index + paddedWidth + 1u)),
                                              hn::LoadU(lanes, greenTile + index + paddedWidth - 1u));
            const auto measuredCorners = hn::Add(hn::Add(hn::Add(hn::LoadU(lanes, source - sourceStride + x - 1u),
                                                                 hn::LoadU(lanes, source - sourceStride + x + 1u)),
                                                         hn::LoadU(lanes, source + sourceStride + x + 1u)),
                                                 hn::LoadU(lanes, source + sourceStride + x - 1u));
            const auto estimate =
                hn::Sub(hn::LoadU(lanes, greenTile + index),
                        hn::Mul(quarter, hn::Sub(greenCorners, hn::Min(clipVector, measuredCorners))));
            const auto onColour = hn::Not(greenLanes(x, firstX, greenAtStart));
            hn::StoreU(hn::IfThenElse(onColour, estimate, hn::LoadU(lanes, destinationPlane + index)), lanes,
                       destinationPlane + index);
        }
        // This is another sub-vector tail; the explicit Highway loop above owns vector work.
#if defined(__clang__)
#pragma clang loop vectorize(disable)
#pragma clang loop interleave(disable)
#endif
        for (; x < last; ++x)
        {
            if ((((x - firstX) & 1u) == 0u) == greenAtStart)
            {
                continue;
            }
            const std::uint64_t index = tileRow + (x - padBeginX);
            const std::uint64_t above = index - paddedWidth;
            const std::uint64_t below = index + paddedWidth;
            const float greenCorners =
                greenTile[above - 1u] + greenTile[above + 1u] + greenTile[below + 1u] + greenTile[below - 1u];
            const float measuredCorners = *(source - sourceStride + x - 1u) + *(source - sourceStride + x + 1u) +
                                          *(source + sourceStride + x + 1u) + *(source + sourceStride + x - 1u);
            destinationPlane[index] =
                greenTile[index] - 0.25f * (greenCorners - (clipSum < measuredCorners ? clipSum : measuredCorners));
        }
    }

    // Both colours at every green site, and the tile written out.
    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t tileRow = static_cast<std::uint64_t>(y - padBeginY) * paddedWidth;
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        const bool greenAtStart = cfa.colorAt(tile.xBegin, y) == CfaColor::Green;
        std::uint32_t x = tile.xBegin;
        for (; x + width <= tile.xEnd; x += width)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            const auto centre = hn::LoadU(lanes, greenTile + index);
            const auto greenAbove = hn::LoadU(lanes, greenTile + index - paddedWidth);
            const auto greenBelow = hn::LoadU(lanes, greenTile + index + paddedWidth);
            const auto greenLeft = hn::LoadU(lanes, greenTile + index - 1u);
            const auto greenRight = hn::LoadU(lanes, greenTile + index + 1u);
            const auto onGreen = greenLanes(x, tile.xBegin, greenAtStart);

            const auto redDifference =
                hn::Add(hn::Add(hn::Add(hn::Sub(greenAbove, hn::LoadU(lanes, redTile + index - paddedWidth)),
                                        hn::Sub(greenBelow, hn::LoadU(lanes, redTile + index + paddedWidth))),
                                hn::Sub(greenLeft, hn::LoadU(lanes, redTile + index - 1u))),
                        hn::Sub(greenRight, hn::LoadU(lanes, redTile + index + 1u)));
            const auto blueDifference =
                hn::Add(hn::Add(hn::Add(hn::Sub(greenAbove, hn::LoadU(lanes, blueTile + index - paddedWidth)),
                                        hn::Sub(greenBelow, hn::LoadU(lanes, blueTile + index + paddedWidth))),
                                hn::Sub(greenLeft, hn::LoadU(lanes, blueTile + index - 1u))),
                        hn::Sub(greenRight, hn::LoadU(lanes, blueTile + index + 1u)));
            const auto red = hn::IfThenElse(onGreen, hn::Sub(centre, hn::Mul(quarter, redDifference)),
                                            hn::LoadU(lanes, redTile + index));
            const auto blue = hn::IfThenElse(onGreen, hn::Sub(centre, hn::Mul(quarter, blueDifference)),
                                             hn::LoadU(lanes, blueTile + index));
            hn::StoreU(red, lanes, redTile + index);
            hn::StoreU(blue, lanes, blueTile + index);
            hn::StoreU(red, lanes, tile.red + destinationRow + x);
            hn::StoreU(centre, lanes, tile.green + destinationRow + x);
            hn::StoreU(blue, lanes, tile.blue + destinationRow + x);
        }
        // This is another sub-vector tail; the explicit Highway loop above owns vector work.
#if defined(__clang__)
#pragma clang loop vectorize(disable)
#pragma clang loop interleave(disable)
#endif
        for (; x < tile.xEnd; ++x)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            if ((((x - tile.xBegin) & 1u) == 0u) == greenAtStart)
            {
                const std::uint64_t above = index - paddedWidth;
                const std::uint64_t below = index + paddedWidth;
                const float centre = greenTile[index];
                redTile[index] =
                    centre - 0.25f * ((greenTile[above] - redTile[above]) + (greenTile[below] - redTile[below]) +
                                      (greenTile[index - 1u] - redTile[index - 1u]) +
                                      (greenTile[index + 1u] - redTile[index + 1u]));
                blueTile[index] =
                    centre - 0.25f * ((greenTile[above] - blueTile[above]) + (greenTile[below] - blueTile[below]) +
                                      (greenTile[index - 1u] - blueTile[index - 1u]) +
                                      (greenTile[index + 1u] - blueTile[index + 1u]));
            }
            tile.red[destinationRow + x] = redTile[index];
            tile.green[destinationRow + x] = greenTile[index];
            tile.blue[destinationRow + x] = blueTile[index];
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

/// The gradient-weighted estimate compiled for the packaged floor.
constexpr DemosaicRunFn kFastAvx2 = HWY_CHOOSE_AVX2(fastRun);

/// What that body says it was compiled for.
constexpr InstructionSet (*kFastAvx2Isa)() noexcept = HWY_CHOOSE_AVX2(isaOfCompiledFast);

#if (HWY_TARGETS & HWY_AVX3) != 0
/// The same estimate compiled for the Skylake-server subset, on the builds that have it.
constexpr DemosaicRunFn kFastAvx3 = HWY_CHOOSE_AVX3(fastRun);

/// What that body says it was compiled for.
constexpr InstructionSet (*kFastAvx3Isa)() noexcept = HWY_CHOOSE_AVX3(isaOfCompiledFast);
#endif

} // namespace
} // namespace demosaic_simd

DemosaicRunFn simdFastRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_simd::kFastAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_simd::kFastAvx3;
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

InstructionSet simdFastIsa(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_simd::kFastAvx2Isa();
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_simd::kFastAvx3Isa();
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
