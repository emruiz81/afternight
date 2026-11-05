// AfterNightEngine - the compiled luminance-only adaptive homogeneity-directed estimate, one per instruction set.
//
// The estimate is the port in `kernels/reference/DemosaicKernelsLuminanceAhd.cpp`, derived from the AHD
// implementation in the librtprocess copy of RawTherapee (`demosaic/ahd.cc`), which states
// `Copyright (c) 2018 Ingo Weyrich (heckflosse67@gmx.de)` and is distributed under the GNU General Public
// License, version 3 or any later version; this file is part of that
// derived work and is distributed under the same licence. What is here is AfterNight's: lane-wise spellings of
// four of the authority's passes - the green candidates, the colour candidates, the homogeneity counts and the
// choice - each in the reference's order of evaluation, with every `std::min` and `std::max` comparison made as the
// standard library makes it, so the passes write the reference's bits. The lightness between them, a
// double-precision cube root, stays the authority's scalar text. A row whose filter does not alternate one green
// site with one other colour, and the columns past the last whole vector, run the scalar text as well.
// The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "backends/cpu/DemosaicLuminanceAhdCpu.h"

#include "kernels/reference/DemosaicBayerKernel.h"
#include "kernels/reference/DemosaicKernelVectorOps.h"

#include <cstddef>
#include <cstdint>

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/DemosaicLuminanceAhdCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace demosaic_luminance_ahd_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// The reference's `std::max`: the second value where the first is less than it, the first otherwise.
///
/// @param first the value kept on a tie or when either is not a number.
/// @param second the other value.
/// @return the larger, as the reference chooses it.
template <class V> HWY_INLINE V referenceMax(const V first, const V second)
{
    return hn::IfThenElse(hn::Lt(first, second), second, first);
}

/// The reference's `std::min`: the second value where it is less than the first, the first otherwise.
///
/// @param first the value kept on a tie or when either is not a number.
/// @param second the other value.
/// @return the smaller, as the reference chooses it.
template <class V> HWY_INLINE V referenceMin(const V first, const V second)
{
    return hn::IfThenElse(hn::Lt(second, first), second, first);
}

/// `demosaic_bayer_detail::fifthOrderGreen`, lane by lane.
template <class D, class V>
HWY_INLINE V fifthOrderGreenLanes(D tag, V centre, V beforeTwo, V beforeOne, V afterOne, V afterTwo)
{
    const V sum = hn::Add(hn::Add(beforeOne, centre), afterOne);
    const V estimate =
        hn::Mul(hn::Set(tag, 0.25f), hn::Sub(hn::Sub(hn::Mul(hn::Set(tag, 2.0f), sum), beforeTwo), afterTwo));
    return referenceMax(referenceMin(estimate, beforeOne), referenceMin(afterOne, referenceMax(estimate, beforeOne)));
}

/// `demosaic_bayer_detail::luminanceAhdClamp`, lane by lane.
template <class V> HWY_INLINE V heldToSensorRange(V value, V zero, V white)
{
    return referenceMin(white, referenceMax(zero, value));
}

/// One where a direction's difference is within the threshold, zero elsewhere.
template <class V> HWY_INLINE V withinThreshold(V difference, V threshold, V one)
{
    return hn::IfThenElseZero(hn::Le(difference, threshold), one);
}

/// The lanes of a vector whose index is odd.
template <class D> HWY_INLINE hn::Mask<D> oddLanes(D tag)
{
    const hn::RebindToSigned<D> indices;
    const auto one = hn::Set(indices, 1);
    return hn::RebindMask(tag, hn::Eq(hn::And(hn::Iota(indices, 0), one), one));
}

/// How one row of the filter array is laid out from a column on.
struct BayerRow
{
    /// Whether the row alternates one green site with one other colour, red or blue.
    bool regular = false;

    /// Whether the column the row is read from is a green site.
    bool firstGreen = false;

    /// Whether the row's other colour is red.
    bool otherRed = false;
};

/// Reads one row's layout from four of its columns.
///
/// @param tile the picture and filter array.
/// @param x the first column a vector covers.
/// @param y the row.
/// @return the layout; a row that is not regular runs the scalar text.
[[nodiscard]] BayerRow bayerRow(const DemosaicTile& tile, std::uint32_t x, std::uint32_t y) noexcept
{
    const CfaColor first = tile.cfa->colorAt(x, y);
    const CfaColor second = tile.cfa->colorAt(x + 1u, y);
    const bool alternates = ((first == CfaColor::Green) != (second == CfaColor::Green)) &&
                            tile.cfa->colorAt(x + 2u, y) == first && tile.cfa->colorAt(x + 3u, y) == second;
    const CfaColor other = first == CfaColor::Green ? second : first;
    return BayerRow{.regular = alternates && (other == CfaColor::Red || other == CfaColor::Blue),
                    .firstGreen = first == CfaColor::Green,
                    .otherRed = other == CfaColor::Red};
}

/// The green candidates' pass of `DemosaicLuminanceAhdVectorOps`.
void greensAhd(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
               std::uint64_t columns, float* horizontalGreen, float* verticalGreen) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = hn::Lanes(tag);
    const auto odd = oddLanes(tag);
    const std::uint64_t stride = tile.sourceStride;
    for (std::uint64_t localY = 2u; localY + 2u < rows; ++localY)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
        const BayerRow row = bayerRow(tile, padBeginX + 2u, y);
        std::uint64_t localX = 2u;
        if (row.regular)
        {
            // Every vector starts an even number of columns after the row's first, so its lanes alternate as the
            // row's first two columns do.
            const auto green = row.firstGreen ? hn::Not(odd) : odd;
            const float* const centre = tile.source + static_cast<std::uint64_t>(y) * stride + padBeginX;
            for (; localX + lanes + 2u <= columns; localX += lanes)
            {
                const float* const at = centre + localX;
                const auto measured = hn::LoadU(tag, at);
                const auto horizontal =
                    fifthOrderGreenLanes(tag, measured, hn::LoadU(tag, at - 2), hn::LoadU(tag, at - 1),
                                         hn::LoadU(tag, at + 1), hn::LoadU(tag, at + 2));
                const auto vertical =
                    fifthOrderGreenLanes(tag, measured, hn::LoadU(tag, at - 2u * stride), hn::LoadU(tag, at - stride),
                                         hn::LoadU(tag, at + stride), hn::LoadU(tag, at + 2u * stride));
                const std::uint64_t index = localY * columns + localX;
                hn::StoreU(hn::IfThenElse(green, measured, horizontal), tag, horizontalGreen + index);
                hn::StoreU(hn::IfThenElse(green, measured, vertical), tag, verticalGreen + index);
            }
        }
        for (; localX + 2u < columns; ++localX)
        {
            const std::uint64_t index = localY * columns + localX;
            demosaic_bayer_detail::luminanceAhdGreens(tile, padBeginX + static_cast<std::uint32_t>(localX), y,
                                                      horizontalGreen[index], verticalGreen[index]);
        }
    }
}

/// One candidate's red and blue at a vector of pixels, before the sensor range holds them.
template <class D, class V>
HWY_INLINE void candidateColours(D tag, const BayerRow& row, hn::Mask<D> green, V measured, const float* at,
                                 const float* up, const float* down, const float* candidate, std::uint64_t columns,
                                 V& red, V& blue)
{
    const auto half = hn::Set(tag, 0.5f);
    const auto quarter = hn::Set(tag, 0.25f);
    const auto g = hn::LoadU(tag, candidate);
    const auto horizontal =
        hn::Add(g, hn::Mul(half, hn::Add(hn::Sub(hn::LoadU(tag, at - 1), hn::LoadU(tag, candidate - 1)),
                                         hn::Sub(hn::LoadU(tag, at + 1), hn::LoadU(tag, candidate + 1)))));
    const auto vertical =
        hn::Add(g, hn::Mul(half, hn::Add(hn::Sub(hn::LoadU(tag, up), hn::LoadU(tag, candidate - columns)),
                                         hn::Sub(hn::LoadU(tag, down), hn::LoadU(tag, candidate + columns)))));
    const auto opposite = hn::Add(
        g, hn::Mul(quarter,
                   hn::Add(hn::Add(hn::Add(hn::Sub(hn::LoadU(tag, up - 1), hn::LoadU(tag, candidate - columns - 1)),
                                           hn::Sub(hn::LoadU(tag, up + 1), hn::LoadU(tag, candidate - columns + 1))),
                                   hn::Sub(hn::LoadU(tag, down - 1), hn::LoadU(tag, candidate + columns - 1))),
                           hn::Sub(hn::LoadU(tag, down + 1), hn::LoadU(tag, candidate + columns + 1)))));
    // A green site takes red along the axis red was measured on; the row's other colour is its own measurement
    // and the opposite one comes from the diagonals.
    red = hn::IfThenElse(green, row.otherRed ? horizontal : vertical, row.otherRed ? measured : opposite);
    blue = hn::IfThenElse(green, row.otherRed ? vertical : horizontal, row.otherRed ? opposite : measured);
}

/// The colour candidates' pass of `DemosaicLuminanceAhdVectorOps`.
void coloursAhd(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
                std::uint64_t columns, const float* horizontalGreen, const float* verticalGreen, float* horizontalRed,
                float* horizontalBlue, float* verticalRed, float* verticalBlue) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = hn::Lanes(tag);
    const auto odd = oddLanes(tag);
    const auto zero = hn::Zero(tag);
    const auto white = hn::Set(tag, tile.whiteLevel);
    const std::uint64_t stride = tile.sourceStride;
    for (std::uint64_t localY = 3u; localY + 3u < rows; ++localY)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
        const BayerRow row = bayerRow(tile, padBeginX + 3u, y);
        std::uint64_t localX = 3u;
        if (row.regular)
        {
            const auto green = row.firstGreen ? hn::Not(odd) : odd;
            const float* const centre = tile.source + static_cast<std::uint64_t>(y) * stride + padBeginX;
            for (; localX + lanes + 3u <= columns; localX += lanes)
            {
                const float* const at = centre + localX;
                const float* const up = at - stride;
                const float* const down = at + stride;
                const std::uint64_t index = localY * columns + localX;
                const auto measured = hn::LoadU(tag, at);
                for (std::uint32_t candidate = 0u; candidate < 2u; ++candidate)
                {
                    const float* const greens = candidate == 0u ? horizontalGreen : verticalGreen;
                    float* const reds = candidate == 0u ? horizontalRed : verticalRed;
                    float* const blues = candidate == 0u ? horizontalBlue : verticalBlue;
                    hn::Vec<decltype(tag)> red;
                    hn::Vec<decltype(tag)> blue;
                    candidateColours(tag, row, green, measured, at, up, down, greens + index, columns, red, blue);
                    // Every site but a red one holds its red estimate to the sensor range, and every site but a
                    // blue one its blue.
                    const auto redHeld = heldToSensorRange(red, zero, white);
                    const auto blueHeld = heldToSensorRange(blue, zero, white);
                    red = row.otherRed ? hn::IfThenElse(green, redHeld, red) : redHeld;
                    blue = row.otherRed ? blueHeld : hn::IfThenElse(green, blueHeld, blue);
                    hn::StoreU(red, tag, reds + index);
                    hn::StoreU(blue, tag, blues + index);
                }
            }
        }
        for (; localX + 3u < columns; ++localX)
        {
            demosaic_bayer_detail::luminanceAhdColours(tile, horizontalGreen, verticalGreen, columns, localX, localY,
                                                       padBeginX + static_cast<std::uint32_t>(localX), y, horizontalRed,
                                                       horizontalBlue, verticalRed, verticalBlue);
        }
    }
}

/// The homogeneity counts' pass of `DemosaicLuminanceAhdVectorOps`.
void homogeneityAhd(std::uint64_t rows, std::uint64_t columns, const float* horizontalLightness,
                    const float* verticalLightness, float* horizontalHomogeneity, float* verticalHomogeneity) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = hn::Lanes(tag);
    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0f);
    for (std::uint64_t localY = 4u; localY + 4u < rows; ++localY)
    {
        std::uint64_t localX = 4u;
        for (; localX + lanes + 4u <= columns; localX += lanes)
        {
            const std::uint64_t index = localY * columns + localX;
            const float* const h = horizontalLightness + index;
            const float* const v = verticalLightness + index;
            const auto hc = hn::LoadU(tag, h);
            const auto vc = hn::LoadU(tag, v);
            const auto h0 = hn::Abs(hn::Sub(hc, hn::LoadU(tag, h - 1)));
            const auto h1 = hn::Abs(hn::Sub(hc, hn::LoadU(tag, h + 1)));
            const auto h2 = hn::Abs(hn::Sub(hc, hn::LoadU(tag, h - columns)));
            const auto h3 = hn::Abs(hn::Sub(hc, hn::LoadU(tag, h + columns)));
            const auto v0 = hn::Abs(hn::Sub(vc, hn::LoadU(tag, v - 1)));
            const auto v1 = hn::Abs(hn::Sub(vc, hn::LoadU(tag, v + 1)));
            const auto v2 = hn::Abs(hn::Sub(vc, hn::LoadU(tag, v - columns)));
            const auto v3 = hn::Abs(hn::Sub(vc, hn::LoadU(tag, v + columns)));
            const auto threshold = referenceMin(referenceMax(h0, h1), referenceMax(v2, v3));
            const auto horizontal = hn::Add(hn::Add(hn::Add(hn::Add(zero, withinThreshold(h0, threshold, one)),
                                                            withinThreshold(h1, threshold, one)),
                                                    withinThreshold(h2, threshold, one)),
                                            withinThreshold(h3, threshold, one));
            const auto vertical = hn::Add(hn::Add(hn::Add(hn::Add(zero, withinThreshold(v0, threshold, one)),
                                                          withinThreshold(v1, threshold, one)),
                                                  withinThreshold(v2, threshold, one)),
                                          withinThreshold(v3, threshold, one));
            hn::StoreU(horizontal, tag, horizontalHomogeneity + index);
            hn::StoreU(vertical, tag, verticalHomogeneity + index);
        }
        for (; localX + 4u < columns; ++localX)
        {
            const std::uint64_t index = localY * columns + localX;
            demosaic_bayer_detail::luminanceAhdHomogeneity(horizontalLightness, verticalLightness, index, columns,
                                                           horizontalHomogeneity[index], verticalHomogeneity[index]);
        }
    }
}

/// The choice's pass of `DemosaicLuminanceAhdVectorOps`.
void chooseAhd(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t columns,
               const float* const* candidates, const float* horizontalHomogeneity,
               const float* verticalHomogeneity) noexcept
{
    const hn::ScalableTag<float> tag;
    const std::uint64_t lanes = hn::Lanes(tag);
    const auto zero = hn::Zero(tag);
    const auto half = hn::Set(tag, 0.5f);
    const auto stride = static_cast<std::ptrdiff_t>(columns);
    float* const destinations[3] = {tile.red, tile.green, tile.blue};
    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t localY = y - padBeginY;
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        std::uint32_t x = tile.xBegin;
        for (; x + lanes <= tile.xEnd; x += static_cast<std::uint32_t>(lanes))
        {
            const auto index = static_cast<std::ptrdiff_t>(localY * columns + (x - padBeginX));
            auto horizontalAgreement = zero;
            auto verticalAgreement = zero;
            for (std::ptrdiff_t offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for (std::ptrdiff_t offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    const std::ptrdiff_t neighbour = index + offsetY * stride + offsetX;
                    horizontalAgreement =
                        hn::Add(horizontalAgreement, hn::LoadU(tag, horizontalHomogeneity + neighbour));
                    verticalAgreement = hn::Add(verticalAgreement, hn::LoadU(tag, verticalHomogeneity + neighbour));
                }
            }
            const auto horizontalWins = hn::Gt(horizontalAgreement, verticalAgreement);
            const auto verticalWins = hn::Gt(verticalAgreement, horizontalAgreement);
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                const auto horizontal = hn::LoadU(tag, candidates[channel] + index);
                const auto vertical = hn::LoadU(tag, candidates[channel + 3u] + index);
                const auto tied = hn::Mul(half, hn::Add(horizontal, vertical));
                hn::StoreU(hn::IfThenElse(horizontalWins, horizontal, hn::IfThenElse(verticalWins, vertical, tied)),
                           tag, destinations[channel] + destinationRow + x);
            }
        }
        for (; x < tile.xEnd; ++x)
        {
            demosaic_bayer_detail::luminanceAhdChoose(
                candidates, horizontalHomogeneity, verticalHomogeneity, localY * columns + (x - padBeginX), columns,
                tile.red[destinationRow + x], tile.green[destinationRow + x], tile.blue[destinationRow + x]);
        }
    }
}

/// Estimates one tile through the luminance-only AHD authority, with the four passes above substituted.
void luminanceAhdRun(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    const DemosaicLuminanceAhdVectorOps ops{
        .greens = &greensAhd, .colours = &coloursAhd, .homogeneity = &homogeneityAhd, .choose = &chooseAhd};
    demosaicLuminanceAhdBody(tile, scratch, scratchFloats, &ops);
}

} // namespace HWY_NAMESPACE
} // namespace demosaic_luminance_ahd_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace demosaic_luminance_ahd_simd
{
namespace
{

constexpr DemosaicRunFn kLuminanceAhdAvx2 = HWY_CHOOSE_AVX2(luminanceAhdRun);

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr DemosaicRunFn kLuminanceAhdAvx3 = HWY_CHOOSE_AVX3(luminanceAhdRun);
#endif

} // namespace
} // namespace demosaic_luminance_ahd_simd

DemosaicRunFn simdLuminanceAhdRun(InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return demosaic_luminance_ahd_simd::kLuminanceAhdAvx2;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return demosaic_luminance_ahd_simd::kLuminanceAhdAvx3;
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
