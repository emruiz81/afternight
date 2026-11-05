// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the LCh adjustment row, compiled once per instruction set.
//
// Isolated from the shared SIMD table so its per-instruction-set object cost remains attributable. The row evaluates
// its scalar authority's steps - `adjustImageLch` through the colour conversion's stages, in their order - in Float32
// lanes: the transfer's powers and the cube root are composed from the vector library's exponential and logarithm,
// the hue is its arc tangent of two arguments and the chroma's return its sine and cosine. Every selection the
// authority makes with a comparison is made here with the same comparison over both answers, so only those functions
// can differ from it, within the library's stated tier; a pixel whose three samples are not all finite is left as the
// destination holds it and not counted; and the last vector of a row is partial rather than a scalar tail, so every
// pixel takes the same arithmetic.

#include "backends/cpu/ColorAdjustmentCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/ColorAdjustmentCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

// The transcendental polynomials; see the shared table unit for why they are asked for by name.
#include <hwy/contrib/math/math-inl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numbers>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace color_adjustment_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// The lanes every step of the row computes in.
using Tag = hn::ScalableTag<float>;

/// One vector of those lanes.
using Vec = hn::Vec<Tag>;

/// A non-negative base raised to an exponent: the exponential of the exponent times the logarithm, which is what the
/// vector library offers. A base of zero is answered zero directly, since the library's logarithm has no answer for
/// it, and the authority's power of zero is zero for every positive exponent the row uses.
///
/// @param tag the lane description.
/// @param base the bases, none negative.
/// @param exponent the exponent, broadcast, positive.
/// @return the powers.
[[nodiscard]] HWY_INLINE Vec powerOf(Tag tag, Vec base, Vec exponent) noexcept
{
    const Vec zero = hn::Zero(tag);
    const Vec raised = hn::Exp(tag, hn::Mul(exponent, hn::Log(tag, base)));
    return hn::IfThenElse(hn::Eq(base, zero), zero, raised);
}

/// The linear value the sRGB curve gives an encoded one: the magnitude through the curve, the sign put back.
///
/// @param tag the lane description.
/// @param value the encoded values.
/// @return the linear values.
[[nodiscard]] HWY_INLINE Vec decodeSrgb(Tag tag, Vec value) noexcept
{
    const Vec magnitude = hn::Abs(value);
    const Vec linearSegment = hn::Mul(magnitude, hn::Set(tag, 1.0f / 12.92f));
    // The curve's power as the square times the power of two fifths: the composed power's distance from the exact
    // grows with the size of its exponent, and the square costs one rounding.
    const Vec base = hn::Mul(hn::Add(magnitude, hn::Set(tag, 0.055f)), hn::Set(tag, 1.0f / 1.055f));
    const Vec curve = hn::Mul(hn::Mul(base, base), powerOf(tag, base, hn::Set(tag, 0.4f)));
    const Vec linear = hn::IfThenElse(hn::Le(magnitude, hn::Set(tag, 0.04045f)), linearSegment, curve);
    return hn::CopySign(linear, value);
}

/// The encoded value the sRGB curve gives a linear one: the magnitude through the curve, the sign put back.
///
/// @param tag the lane description.
/// @param value the linear values.
/// @return the encoded values.
[[nodiscard]] HWY_INLINE Vec encodeSrgb(Tag tag, Vec value) noexcept
{
    const Vec magnitude = hn::Abs(value);
    const Vec linearSegment = hn::Mul(magnitude, hn::Set(tag, 12.92f));
    const Vec curve =
        hn::MulAdd(hn::Set(tag, 1.055f), powerOf(tag, magnitude, hn::Set(tag, 1.0f / 2.4f)), hn::Set(tag, -0.055f));
    const Vec encoded = hn::IfThenElse(hn::Le(magnitude, hn::Set(tag, 0.0031308f)), linearSegment, curve);
    return hn::CopySign(encoded, value);
}

/// The CIE lightness helper's linear-segment bound.
constexpr float kLabDelta = 6.0f / 29.0f;

/// The perceptual compression the Lab transform applies to a tristimulus ratio: the cube root above the bound's cube,
/// the linear segment below it.
///
/// @param tag the lane description.
/// @param ratio the ratios.
/// @return the compressed values.
[[nodiscard]] HWY_INLINE Vec labForward(Tag tag, Vec ratio) noexcept
{
    const Vec cubeRoot = powerOf(tag, hn::Max(ratio, hn::Zero(tag)), hn::Set(tag, 1.0f / 3.0f));
    const Vec segment =
        hn::MulAdd(ratio, hn::Set(tag, 1.0f / (3.0f * kLabDelta * kLabDelta)), hn::Set(tag, 4.0f / 29.0f));
    return hn::IfThenElse(hn::Gt(ratio, hn::Set(tag, kLabDelta * kLabDelta * kLabDelta)), cubeRoot, segment);
}

/// The inverse of `labForward`.
///
/// @param tag the lane description.
/// @param compressed the compressed values.
/// @return the ratios.
[[nodiscard]] HWY_INLINE Vec labInverse(Tag tag, Vec compressed) noexcept
{
    const Vec cube = hn::Mul(hn::Mul(compressed, compressed), compressed);
    const Vec segment =
        hn::Mul(hn::Set(tag, 3.0f * kLabDelta * kLabDelta), hn::Sub(compressed, hn::Set(tag, 4.0f / 29.0f)));
    return hn::IfThenElse(hn::Gt(compressed, hn::Set(tag, kLabDelta)), cube, segment);
}

/// A row of three channels multiplied by a matrix, each output the row's coefficients applied left to right.
///
/// @param tag the lane description.
/// @param m the nine coefficients, row by row.
/// @param first the first input channel.
/// @param second the second input channel.
/// @param third the third input channel.
/// @param outFirst the first output channel.
/// @param outSecond the second output channel.
/// @param outThird the third output channel.
HWY_INLINE void applyMatrix(Tag tag, const std::array<double, 9u>& m, Vec first, Vec second, Vec third, Vec& outFirst,
                            Vec& outSecond, Vec& outThird) noexcept
{
    const auto coefficient = [tag, &m](std::size_t index) { return hn::Set(tag, static_cast<float>(m[index])); };
    outFirst = hn::MulAdd(coefficient(2u), third, hn::MulAdd(coefficient(1u), second, hn::Mul(coefficient(0u), first)));
    outSecond =
        hn::MulAdd(coefficient(5u), third, hn::MulAdd(coefficient(4u), second, hn::Mul(coefficient(3u), first)));
    outThird = hn::MulAdd(coefficient(8u), third, hn::MulAdd(coefficient(7u), second, hn::Mul(coefficient(6u), first)));
}

/// Writes one row of the LCh adjustment.
///
/// @param red the red samples.
/// @param green the green samples.
/// @param blue the blue samples.
/// @param redOut where the adjusted red samples are written; may be `red`.
/// @param greenOut where the adjusted green samples are written; may be `green`.
/// @param blueOut where the adjusted blue samples are written; may be `blue`.
/// @param count how many pixels.
/// @param c the conversion's constants and the adjustment.
/// @return how many pixels were adjusted: every one whose three samples are finite.
std::uint64_t lchAdjustFloat32RunOf(const float* red, const float* green, const float* blue, float* redOut,
                                    float* greenOut, float* blueOut, std::uint64_t count,
                                    const LchAdjustCoefficients& c) noexcept
{
    const Tag tag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const Vec zero = hn::Zero(tag);
    const Vec one = hn::Set(tag, 1.0f);
    const Vec inverseWhiteX = hn::Set(tag, static_cast<float>(1.0 / c.white[0]));
    const Vec inverseWhiteY = hn::Set(tag, static_cast<float>(1.0 / c.white[1]));
    const Vec inverseWhiteZ = hn::Set(tag, static_cast<float>(1.0 / c.white[2]));
    const Vec whiteX = hn::Set(tag, static_cast<float>(c.white[0]));
    const Vec whiteY = hn::Set(tag, static_cast<float>(c.white[1]));
    const Vec whiteZ = hn::Set(tag, static_cast<float>(c.white[2]));
    const Vec lightnessOffset = hn::Set(tag, static_cast<float>(c.lightnessOffset));
    const Vec chromaScale = hn::Set(tag, static_cast<float>(c.chromaScale));
    const Vec hueOffset = hn::Set(tag, static_cast<float>(c.hueOffsetDegrees));
    const Vec fullTurn = hn::Set(tag, 360.0f);
    const Vec degreesPerRadian = hn::Set(tag, static_cast<float>(180.0 / std::numbers::pi));
    const Vec radiansPerDegree = hn::Set(tag, static_cast<float>(std::numbers::pi / 180.0));
    const bool srgb = c.transfer == LchAdjustTransfer::Srgb;
    std::uint64_t adjusted = 0u;
    for (std::uint64_t index = 0u; index < count; index += lanes)
    {
        const std::size_t partial = static_cast<std::size_t>((std::min)(lanes, count - index));
        Vec r = hn::LoadN(tag, red + index, partial);
        Vec g = hn::LoadN(tag, green + index, partial);
        Vec b = hn::LoadN(tag, blue + index, partial);
        const auto finite =
            hn::And(hn::FirstN(tag, partial), hn::And(hn::And(hn::IsFinite(r), hn::IsFinite(g)), hn::IsFinite(b)));
        if (srgb)
        {
            r = decodeSrgb(tag, r);
            g = decodeSrgb(tag, g);
            b = decodeSrgb(tag, b);
        }
        Vec x;
        Vec y;
        Vec z;
        applyMatrix(tag, c.toXyz, r, g, b, x, y, z);
        const Vec fx = labForward(tag, hn::Mul(x, inverseWhiteX));
        const Vec fy = labForward(tag, hn::Mul(y, inverseWhiteY));
        const Vec fz = labForward(tag, hn::Mul(z, inverseWhiteZ));
        Vec lightness = hn::MulAdd(hn::Set(tag, 116.0f), fy, hn::Set(tag, -16.0f));
        Vec a = hn::Mul(hn::Set(tag, 500.0f), hn::Sub(fx, fy));
        Vec bb = hn::Mul(hn::Set(tag, 200.0f), hn::Sub(fy, fz));
        // Chroma and a hue in degrees on [0, 360); a grey pixel, with no chroma, has the hue of the authority's arc
        // tangent of two zeros, which is zero.
        Vec chroma = hn::Sqrt(hn::MulAdd(a, a, hn::Mul(bb, bb)));
        Vec hue = hn::Mul(hn::Atan2(tag, bb, a), degreesPerRadian);
        hue = hn::IfThenElse(hn::Lt(hue, zero), hn::Add(hue, fullTurn), hue);
        hue = hn::IfThenElse(hn::Eq(chroma, zero), zero, hue);
        // The adjustment, as the authority orders and bounds it.
        lightness = hn::Clamp(hn::Add(lightness, lightnessOffset), zero, hn::Set(tag, 100.0f));
        chroma = hn::Clamp(hn::Mul(chroma, chromaScale), zero, hn::Set(tag, 200.0f));
        hue = hn::Add(hue, hueOffset);
        hue = hn::Sub(hue, hn::Mul(fullTurn, hn::Floor(hn::Div(hue, fullTurn))));
        // Back through a* and b*, XYZ and the matrix.
        const Vec radians = hn::Mul(hue, radiansPerDegree);
        a = hn::Mul(chroma, hn::Cos(tag, radians));
        bb = hn::Mul(chroma, hn::Sin(tag, radians));
        const Vec gy = hn::Mul(hn::Add(lightness, hn::Set(tag, 16.0f)), hn::Set(tag, 1.0f / 116.0f));
        const Vec gx = hn::MulAdd(a, hn::Set(tag, 1.0f / 500.0f), gy);
        const Vec gz = hn::Sub(gy, hn::Mul(bb, hn::Set(tag, 1.0f / 200.0f)));
        x = hn::Mul(whiteX, labInverse(tag, gx));
        y = hn::Mul(whiteY, labInverse(tag, gy));
        z = hn::Mul(whiteZ, labInverse(tag, gz));
        applyMatrix(tag, c.fromXyz, x, y, z, r, g, b);
        if (srgb)
        {
            r = encodeSrgb(tag, r);
            g = encodeSrgb(tag, g);
            b = encodeSrgb(tag, b);
        }
        r = hn::Clamp(r, zero, one);
        g = hn::Clamp(g, zero, one);
        b = hn::Clamp(b, zero, one);
        if (!hn::AllTrue(tag, finite))
        {
            // A pixel the authority skips stays what its destination holds, which may be the source itself.
            r = hn::IfThenElse(finite, r, hn::LoadN(tag, redOut + index, partial));
            g = hn::IfThenElse(finite, g, hn::LoadN(tag, greenOut + index, partial));
            b = hn::IfThenElse(finite, b, hn::LoadN(tag, blueOut + index, partial));
        }
        hn::StoreN(r, tag, redOut + index, partial);
        hn::StoreN(g, tag, greenOut + index, partial);
        hn::StoreN(b, tag, blueOut + index, partial);
        adjusted += static_cast<std::uint64_t>(hn::CountTrue(tag, finite));
    }
    return adjusted;
}

} // namespace HWY_NAMESPACE
} // namespace color_adjustment_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace color_adjustment_simd
{
namespace
{

constexpr ColorAdjustmentCpuRuns kAvx2Runs{.lchAdjustFloat32 = HWY_CHOOSE_AVX2(lchAdjustFloat32RunOf)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr ColorAdjustmentCpuRuns kAvx3Runs{.lchAdjustFloat32 = HWY_CHOOSE_AVX3(lchAdjustFloat32RunOf)};
#endif

} // namespace
} // namespace color_adjustment_simd

const ColorAdjustmentCpuRuns* simdColorAdjustmentRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &color_adjustment_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &color_adjustment_simd::kAvx3Runs;
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
