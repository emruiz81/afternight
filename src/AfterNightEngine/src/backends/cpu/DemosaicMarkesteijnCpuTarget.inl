// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the Markesteijn lane passes, included once per Highway target by each
// Markesteijn method's translation unit.
//
// The estimate is the port in `kernels/reference/DemosaicKernelsMarkesteijn.cpp`, derived from Frank
// Markesteijn's X-Trans algorithm as adapted to RawTherapee by Ingo Weyrich in 2014 and carried by
// librtprocess (`demosaic/markesteijn.cc`, which names no copyright holder), distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. What is here is AfterNight's: the lane-wise spelling of the
// reference's passes in its order of evaluation. The provenance note is
// `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// The one-pass and three-pass methods share these two lane passes and differ only in the pass count
// they give the scalar authority, so the lane passes are written once here rather than twice.
// Each method is still its own unit, so the object each instruction-set rung costs stays
// attributable to it. The includer defines `AN_MARK_SIMD_NAMESPACE`, `AN_MARK_ISA_NAME`,
// `AN_MARK_RUN_NAME` and `AN_MARK_PASSES` before including this, and supplies the Highway headers
// inside `HWY_BEFORE_NAMESPACE`, `kernels/reference/DemosaicKernelVectorOps.h` and `<cstdint>`.
//
// Every lane pass states the scalar text's operations in its order, association included, and the
// samples a vector does not cover run that same arithmetic one at a time, so the compiled estimate
// equals the scalar authority bit for bit.

namespace an
{
namespace AN_MARK_SIMD_NAMESPACE
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// Which rung of the ladder this compilation of the including unit belongs to.
///
/// @return the instruction set this body was compiled for.
[[nodiscard]] InstructionSet AN_MARK_ISA_NAME() noexcept
{
#if HWY_TARGET == HWY_AVX3
    return InstructionSet::Avx512Skylake;
#else
    return InstructionSet::Avx2;
#endif
}

/// Converts one direction's interleaved red, green and blue into a luminance and two scaled colour
/// differences, ten samples in from every edge.
///
/// The `DemosaicMarkesteijnVectorOps::lumaChroma` pass, which the authority calls only when the tile
/// does not ask for the perceptual space.
///
/// @param rgb the direction's interleaved samples, three per pixel.
/// @param pixels how many pixels one plane of `lab` holds.
/// @param rows how many rows the planes hold.
/// @param columns how many samples one row holds, which is also the row stride.
/// @param lab where the luminance plane and the two colour-difference planes go, one after another.
void markesteijnLumaChroma(const float* rgb, std::uint64_t pixels, std::uint64_t rows, std::uint64_t columns,
                           float* lab) noexcept
{
    const hn::ScalableTag<float> lanes;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto redCoefficient = hn::Set(lanes, 0.2627f);
    const auto greenCoefficient = hn::Set(lanes, 0.6780f);
    const auto blueCoefficient = hn::Set(lanes, 0.0593f);
    const auto blueChromaScale = hn::Set(lanes, 0.56433f);
    const auto redChromaScale = hn::Set(lanes, 0.67815f);
    for (std::uint64_t row = 10u; row < rows - 10u; ++row)
    {
        std::uint64_t column = 10u;
        for (; column + width <= columns - 10u; column += width)
        {
            const std::uint64_t index = row * columns + column;
            hn::Vec<decltype(lanes)> red;
            hn::Vec<decltype(lanes)> green;
            hn::Vec<decltype(lanes)> blue;
            hn::LoadInterleaved3(lanes, rgb + 3u * index, red, green, blue);
            const auto luma = hn::Add(hn::Add(hn::Mul(redCoefficient, red), hn::Mul(greenCoefficient, green)),
                                      hn::Mul(blueCoefficient, blue));
            hn::StoreU(luma, lanes, lab + index);
            hn::StoreU(hn::Mul(hn::Sub(blue, luma), blueChromaScale), lanes, lab + pixels + index);
            hn::StoreU(hn::Mul(hn::Sub(red, luma), redChromaScale), lanes, lab + 2u * pixels + index);
        }
        for (; column < columns - 10u; ++column)
        {
            const std::uint64_t index = row * columns + column;
            const float* const centre = rgb + 3u * index;
            const float luma = 0.2627f * centre[0] + 0.6780f * centre[1] + 0.0593f * centre[2];
            lab[index] = luma;
            lab[pixels + index] = (centre[2] - luma) * 0.56433f;
            lab[2u * pixels + index] = (centre[0] - luma) * 0.67815f;
        }
    }
}

/// Measures how much one direction bends: the summed squared second differences of the three planes
/// of `lab` along `offset`, eleven samples in from every edge.
///
/// The `DemosaicMarkesteijnVectorOps::derivative` pass.
///
/// @param lab the luminance plane and the two colour planes, one after another.
/// @param pixels how many pixels one plane holds.
/// @param rows how many rows each plane holds.
/// @param columns how many samples one row holds, which is also the row stride.
/// @param offset the signed distance, in samples, between a sample and the neighbour the second
///        difference reads on each side.
/// @param cieLab whether `lab` holds the perceptual space, which folds the lightness term into the
///        two colour terms before they are squared.
/// @param destination where the measure goes, one value per pixel.
void markesteijnDerivative(const float* lab, std::uint64_t pixels, std::uint64_t rows, std::uint64_t columns,
                           std::int64_t offset, bool cieLab, float* destination) noexcept
{
    const hn::ScalableTag<float> lanes;
    const std::uint64_t width = hn::Lanes(lanes);
    const auto two = hn::Set(lanes, 2.0f);
    const auto firstCieScale = hn::Set(lanes, 2.1551724f);
    const auto secondCieScale = hn::Set(lanes, 0.86206896f);
    for (std::uint64_t row = 11u; row < rows - 11u; ++row)
    {
        std::uint64_t column = 11u;
        for (; column + width <= columns - 11u; column += width)
        {
            const std::uint64_t index = row * columns + column;
            const float* const lightness = lab + index;
            const float* const first = lab + pixels + index;
            const float* const second = lab + 2u * pixels + index;
            const auto bend =
                hn::Sub(hn::Sub(hn::Mul(two, hn::LoadU(lanes, lightness)), hn::LoadU(lanes, lightness + offset)),
                        hn::LoadU(lanes, lightness - offset));
            auto firstBend = hn::Sub(hn::Sub(hn::Mul(two, hn::LoadU(lanes, first)), hn::LoadU(lanes, first + offset)),
                                     hn::LoadU(lanes, first - offset));
            auto secondBend =
                hn::Sub(hn::Sub(hn::Mul(two, hn::LoadU(lanes, second)), hn::LoadU(lanes, second + offset)),
                        hn::LoadU(lanes, second - offset));
            if (cieLab)
            {
                firstBend = hn::Add(firstBend, hn::Mul(bend, firstCieScale));
                secondBend = hn::Sub(secondBend, hn::Mul(bend, secondCieScale));
            }
            const auto result =
                hn::Add(hn::Add(hn::Mul(bend, bend), hn::Mul(firstBend, firstBend)), hn::Mul(secondBend, secondBend));
            hn::StoreU(result, lanes, destination + index);
        }
        for (; column < columns - 11u; ++column)
        {
            const std::uint64_t index = row * columns + column;
            const float* const lightness = lab + index;
            const float* const first = lab + pixels + index;
            const float* const second = lab + 2u * pixels + index;
            const float bend = 2.0f * lightness[0] - lightness[offset] - lightness[-offset];
            float firstBend = 2.0f * first[0] - first[offset] - first[-offset];
            float secondBend = 2.0f * second[0] - second[offset] - second[-offset];
            if (cieLab)
            {
                firstBend += bend * 2.1551724f;
                secondBend -= bend * 0.86206896f;
            }
            destination[index] = bend * bend + firstBend * firstBend + secondBend * secondBend;
        }
    }
}

/// Estimates one tile through the shared Markesteijn authority, with the two lane passes above
/// substituted and `AN_MARK_PASSES` as the method's pass count.
///
/// @param tile the region to write and the picture to read it from.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds; a shortfall writes nothing.
void AN_MARK_RUN_NAME(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    const DemosaicMarkesteijnVectorOps ops{.lumaChroma = &markesteijnLumaChroma, .derivative = &markesteijnDerivative};
    demosaicMarkesteijnBodyWithOps(tile, scratch, scratchFloats, AN_MARK_PASSES, &ops);
}

} // namespace HWY_NAMESPACE
} // namespace AN_MARK_SIMD_NAMESPACE
} // namespace an
