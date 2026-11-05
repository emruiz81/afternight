// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the asinh, generalized-hyperbolic and midtones transfer stretch rows,
// compiled once per instruction set.
//
// Isolated from the shared SIMD table so its per-instruction-set object cost remains attributable.
// Each row widens each Float32 sample and evaluates its scalar authority's Float64 pieces a vector
// at a time, with the transcendental functions taken from the vector library's Float64
// polynomials. The asinh rows evaluate two tangent lines and, between them, the normalized primitive
// `asinh(amount t)` forward and the curve `symmetry + sinh(P) / amount` inverse; the
// generalized-hyperbolic rows evaluate one member's curve through
// its exponent, per the regime the plan names, in either direction. Each selection is written as
// the scalar authority's comparison, so only those functions can differ from it, within the
// library's stated tier. Where the library's documented range is narrower than the scalar
// function's, the row states the rest itself: `log1p` of a negative argument is formed from the
// logarithm, as the library forms a positive one, and an argument the scalar logarithm answers
// with minus infinity is answered by the lane's limit directly. The midtones transfer is rational,
// so its row needs no library function: it evaluates its scalar authority's fraction in Float32,
// from scaled distances to endpoints held as two Float32 parts, within a stated bound of the
// authority, and a transfer too extreme for that as the authority's own Float64 operations. It
// can write a large destination with streaming stores, since a pass that does so little per sample
// is limited by memory traffic, and it checks the next row while it writes, since checking a row on
// its own reads it from memory with nothing to overlap. Its store fence is the x86 instruction
// written directly: the vector library's own compiles to nothing under a compiler that does not
// define `__SSE2__`, as Microsoft's does not.

#include "backends/cpu/StretchCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/StretchCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

// The transcendental polynomials; see the shared table unit for why they are asked for by name.
#include <hwy/contrib/math/math-inl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#if HWY_ARCH_X86
#include <xmmintrin.h>
#endif

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace stretch_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// Writes one contiguous row of the planned forward asinh.
///
/// Every lane measures its distance from the symmetry point, takes the inverse hyperbolic sine of the
/// amount times it, and keeps the piece the scalar authority's comparisons choose: the lower tangent
/// below the low protection point, the curve below the high one, the upper tangent from it. A tangent
/// lane's curve value is computed and discarded; its argument is at most the amount, inside the
/// library's range. The final vector is partial rather than a scalar tail, so every sample takes the
/// same arithmetic.
///
/// @param source the samples to stretch, each finite.
/// @param destination where the stretched samples are written; may be `source`.
/// @param count how many samples.
/// @param c the planned coefficients.
void asinhFloat32RunOf(const float* source, float* destination, std::uint64_t count,
                       const AsinhCoefficients& c) noexcept
{
    const hn::ScalableTag<double> tag;
    const hn::Rebind<float, decltype(tag)> narrow;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0);
    const auto amount = hn::Set(tag, c.amount);
    const auto symmetry = hn::Set(tag, c.symmetryPoint);
    const auto origin = hn::Set(tag, c.origin);
    const auto extent = hn::Set(tag, c.extent);
    const auto lowProtection = hn::Set(tag, c.lowProtection);
    const auto highProtection = hn::Set(tag, c.highProtection);
    const auto lowGradient = hn::Set(tag, c.lowGradient);
    const auto highGradient = hn::Set(tag, c.highGradient);
    for (std::uint64_t index = 0u; index < count; index += lanes)
    {
        const std::size_t partial = static_cast<std::size_t>((std::min)(lanes, count - index));
        const auto input = hn::PromoteTo(tag, hn::LoadN(narrow, source + index, partial));
        const auto low = hn::IfThenElse(hn::Lt(input, zero), zero, input);
        const auto x = hn::IfThenElse(hn::Lt(one, low), one, low);
        const auto upper = hn::Not(hn::Lt(x, symmetry));
        const auto distance = hn::IfThenElse(upper, hn::Sub(x, symmetry), hn::Sub(symmetry, x));
        const auto primitive = hn::Asinh(tag, hn::Mul(amount, distance));
        const auto curve = hn::Div(hn::Sub(hn::IfThenElse(upper, primitive, hn::Neg(primitive)), origin), extent);
        const auto lowerLine = hn::Mul(x, lowGradient);
        const auto upperLine = hn::Sub(one, hn::Mul(hn::Sub(one, x), highGradient));
        const auto piece = hn::IfThenElse(hn::Lt(x, lowProtection), lowerLine,
                                          hn::IfThenElse(hn::Lt(x, highProtection), curve, upperLine));
        const auto clampedLow = hn::IfThenElse(hn::Lt(piece, zero), zero, piece);
        const auto clamped = hn::IfThenElse(hn::Lt(one, clampedLow), one, clampedLow);
        const auto result = hn::IfThenElse(hn::Le(x, zero), zero, hn::IfThenElse(hn::Le(one, x), one, clamped));
        hn::StoreN(hn::DemoteTo(narrow, result), narrow, destination + index, partial);
    }
}

/// Writes one contiguous row of the planned inverse asinh.
///
/// Every lane evaluates all three pieces and keeps the one the scalar authority's comparisons
/// choose: the lower tangent below `lowOutput`, the upper above `highOutput`, the curve between.
/// The curve's argument is held to its own range before the hyperbolic sine, so a tangent lane
/// never asks the polynomial for a value outside its domain, and past
/// `kAsinhInverseLargeArgument` the exponential form replaces the quotient. The final vector is
/// partial rather than a scalar tail, so every sample takes the same arithmetic.
///
/// @param source the samples to invert, each finite.
/// @param destination where the inverted samples are written; may be `source`.
/// @param count how many samples.
/// @param c the planned coefficients.
void asinhInverseFloat32RunOf(const float* source, float* destination, std::uint64_t count,
                              const AsinhCoefficients& c) noexcept
{
    const hn::ScalableTag<double> tag;
    const hn::Rebind<float, decltype(tag)> narrow;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0);
    const auto origin = hn::Set(tag, c.origin);
    const auto extent = hn::Set(tag, c.extent);
    const auto lowPrimitive = hn::Set(tag, c.lowPrimitive);
    const auto highPrimitive = hn::Set(tag, c.highPrimitive);
    const auto symmetry = hn::Set(tag, c.symmetryPoint);
    const auto lowOutput = hn::Set(tag, c.lowOutput);
    const auto highOutput = hn::Set(tag, c.highOutput);
    const auto lowScale = hn::Set(tag, c.lowScale);
    const auto highScale = hn::Set(tag, c.highScale);
    const auto inverseAmount = hn::Set(tag, c.inverseAmount);
    const auto logTwiceAmount = hn::Set(tag, c.logTwiceAmount);
    const auto largeArgument = hn::Set(tag, kAsinhInverseLargeArgument);
    for (std::uint64_t x = 0u; x < count; x += lanes)
    {
        const std::size_t partial = static_cast<std::size_t>((std::min)(lanes, count - x));
        const auto input = hn::PromoteTo(tag, hn::LoadN(narrow, source + x, partial));
        // `input < 0 ? 0 : input`, then `> 1 ? 1`, as the scalar clamp orders them.
        const auto low = hn::IfThenElse(hn::Lt(input, zero), zero, input);
        const auto y = hn::IfThenElse(hn::Lt(one, low), one, low);
        const auto primitive = hn::Add(origin, hn::Mul(y, extent));
        const auto aboveLow = hn::IfThenElse(hn::Lt(primitive, lowPrimitive), lowPrimitive, primitive);
        const auto argument = hn::IfThenElse(hn::Lt(highPrimitive, aboveLow), highPrimitive, aboveLow);
        const auto magnitude = hn::Abs(argument);
        auto offset = hn::Mul(hn::Sinh(tag, hn::Min(magnitude, largeArgument)), inverseAmount);
        if (c.largeArguments)
        {
            const auto exponential = hn::Exp(tag, hn::Sub(magnitude, logTwiceAmount));
            offset = hn::IfThenElse(hn::Lt(largeArgument, magnitude), exponential, offset);
        }
        const auto curve = hn::IfThenElse(hn::Lt(argument, zero), hn::Sub(symmetry, offset), hn::Add(symmetry, offset));
        const auto lowerLine = hn::Mul(y, lowScale);
        const auto upperLine = hn::Sub(one, hn::Mul(hn::Sub(one, y), highScale));
        const auto piece =
            hn::IfThenElse(hn::Lt(y, lowOutput), lowerLine, hn::IfThenElse(hn::Lt(highOutput, y), upperLine, curve));
        const auto clampedLow = hn::IfThenElse(hn::Lt(piece, zero), zero, piece);
        const auto clamped = hn::IfThenElse(hn::Lt(one, clampedLow), one, clampedLow);
        const auto result = hn::IfThenElse(hn::Le(y, zero), zero, hn::IfThenElse(hn::Le(one, y), one, clamped));
        hn::StoreN(hn::DemoteTo(narrow, result), narrow, destination + x, partial);
    }
}

/// Writes one contiguous row of a planned forward generalized-hyperbolic curve for one member.
///
/// Every lane measures its distance from the symmetry point, evaluates the member's curve there
/// and keeps the piece the scalar authority's comparisons choose. A line lane's curve value is
/// computed and discarded; its distance is at most one, inside the library's range.
///
/// @param source the samples to stretch, each finite.
/// @param destination where the stretched samples are written; may be `source`.
/// @param count how many samples.
/// @param c the planned coefficients, whose regime is `kRegime`.
template <GeneralizedHyperbolicRegime kRegime>
void generalizedHyperbolicForwardRow(const float* source, float* destination, std::uint64_t count,
                                     const GeneralizedHyperbolicCoefficients& c) noexcept
{
    const hn::ScalableTag<double> tag;
    const hn::Rebind<float, decltype(tag)> narrow;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0);
    const auto amount = hn::Set(tag, c.amount);
    const auto inverseAmount = hn::Set(tag, c.inverseAmount);
    const auto rate = hn::Set(tag, c.rate);
    const auto inverseScale = hn::Set(tag, c.inverseScale);
    const auto negatedSaturation = hn::Set(tag, -c.saturation);
    const auto inverseSaturationRate = hn::Set(tag, c.inverseSaturationRate);
    const auto symmetry = hn::Set(tag, c.symmetryPoint);
    const auto lowProtection = hn::Set(tag, c.lowProtection);
    const auto highProtection = hn::Set(tag, c.highProtection);
    const auto lowCurve = hn::Set(tag, c.lowCurve);
    const auto highCurve = hn::Set(tag, c.highCurve);
    const auto lowSlope = hn::Set(tag, c.lowSlope);
    const auto highSlope = hn::Set(tag, c.highSlope);
    const auto lowLine = hn::Set(tag, c.lowLine);
    const auto highLine = hn::Set(tag, c.highLine);
    const auto extent = hn::Set(tag, c.extent);
    for (std::uint64_t index = 0u; index < count; index += lanes)
    {
        const std::size_t partial = static_cast<std::size_t>((std::min)(lanes, count - index));
        const auto input = hn::PromoteTo(tag, hn::LoadN(narrow, source + index, partial));
        const auto low = hn::IfThenElse(hn::Lt(input, zero), zero, input);
        const auto x = hn::IfThenElse(hn::Lt(one, low), one, low);
        const auto upper = hn::Not(hn::Lt(x, symmetry));
        const auto distance = hn::IfThenElse(upper, hn::Sub(x, symmetry), hn::Sub(symmetry, x));
        hn::Vec<decltype(tag)> exponent;
        if constexpr (kRegime == GeneralizedHyperbolicRegime::Exponential)
        {
            exponent = hn::Mul(amount, distance);
        }
        else
        {
            exponent = hn::Mul(hn::Log1p(tag, hn::Mul(rate, distance)), inverseScale);
        }
        hn::Vec<decltype(tag)> curve;
        if constexpr (kRegime == GeneralizedHyperbolicRegime::Logarithmic)
        {
            curve = hn::Mul(exponent, inverseAmount);
        }
        else
        {
            curve = hn::Mul(hn::Neg(hn::Expm1(tag, hn::Mul(negatedSaturation, exponent))), inverseSaturationRate);
        }
        const auto lowerLine = hn::Mul(x, lowSlope);
        const auto lowerCurve = hn::Add(lowLine, hn::Sub(lowCurve, curve));
        const auto upperCurve = hn::Add(highLine, hn::Sub(highCurve, curve));
        const auto upperLine = hn::Mul(hn::Sub(one, x), highSlope);
        const auto upperRise = hn::IfThenElse(hn::Le(x, highProtection), upperCurve, upperLine);
        const auto rise =
            hn::IfThenElse(hn::Lt(x, lowProtection), lowerLine, hn::IfThenElse(upper, upperRise, lowerCurve));
        const auto share = hn::Div(rise, extent);
        const auto y = hn::IfThenElse(upper, hn::Sub(one, share), share);
        const auto clampedLow = hn::IfThenElse(hn::Lt(y, zero), zero, y);
        const auto clamped = hn::IfThenElse(hn::Lt(one, clampedLow), one, clampedLow);
        const auto result = hn::IfThenElse(hn::Le(x, zero), zero, hn::IfThenElse(hn::Le(one, x), one, clamped));
        hn::StoreN(hn::DemoteTo(narrow, result), narrow, destination + index, partial);
    }
}

/// Writes one contiguous row of a planned inverse generalized-hyperbolic curve for one member.
///
/// Every lane solves the curve piece on its side of the symmetry point's value, from its remaining
/// rise below one half and from the rise itself otherwise, and keeps the piece the scalar
/// authority's comparisons choose. A lane whose remaining rise or complement is zero - where the
/// scalar logarithm is minus infinity - answers its limit without asking the library for it.
///
/// @param source the samples to invert, each finite.
/// @param destination where the inverted samples are written; may be `source`.
/// @param count how many samples.
/// @param c the planned coefficients, whose regime is `kRegime`.
template <GeneralizedHyperbolicRegime kRegime>
void generalizedHyperbolicInverseRow(const float* source, float* destination, std::uint64_t count,
                                     const GeneralizedHyperbolicCoefficients& c) noexcept
{
    const hn::ScalableTag<double> tag;
    const hn::Rebind<float, decltype(tag)> narrow;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));
    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0);
    const auto half = hn::Set(tag, 0.5);
    const auto unitAmount = hn::Set(tag, c.unitAmount);
    const auto inverseAmount = hn::Set(tag, c.inverseAmount);
    const auto scale = hn::Set(tag, c.scale);
    const auto inverseRate = hn::Set(tag, c.inverseRate);
    const auto logRate = hn::Set(tag, c.logRate);
    const auto unitSaturationRate = hn::Set(tag, c.unitSaturationRate);
    const auto inverseSaturation = hn::Set(tag, c.inverseSaturation);
    const auto largeExponent = hn::Set(tag, kGeneralizedHyperbolicLargeExponent);
    const auto symmetry = hn::Set(tag, c.symmetryPoint);
    const auto lowDistance = hn::Set(tag, c.lowDistance);
    const auto highDistance = hn::Set(tag, c.highDistance);
    const auto unitLowCurve = hn::Set(tag, c.unitLowCurve);
    const auto unitHighCurve = hn::Set(tag, c.unitHighCurve);
    const auto lowTail = hn::Set(tag, c.lowTail);
    const auto highTail = hn::Set(tag, c.highTail);
    const auto lowSlope = hn::Set(tag, c.lowSlope);
    const auto highSlope = hn::Set(tag, c.highSlope);
    const auto unitLowLine = hn::Set(tag, c.unitLowLine);
    const auto unitHighLine = hn::Set(tag, c.unitHighLine);
    const auto unitLowRise = hn::Set(tag, c.unitLowRise);
    const auto extent = hn::Set(tag, c.extent);
    const auto unitExtent = hn::Set(tag, c.unitExtent);
    for (std::uint64_t index = 0u; index < count; index += lanes)
    {
        const std::size_t partial = static_cast<std::size_t>((std::min)(lanes, count - index));
        const auto input = hn::PromoteTo(tag, hn::LoadN(narrow, source + index, partial));
        const auto low = hn::IfThenElse(hn::Lt(input, zero), zero, input);
        const auto y = hn::IfThenElse(hn::Lt(one, low), one, low);
        // The rise from either end in the plan's units, as the scalar authority measures it.
        const auto below = hn::Mul(y, unitExtent);
        const auto above = hn::Mul(hn::Sub(one, y), unitExtent);
        const auto lowerSide = hn::Lt(below, unitLowRise);
        const auto drop = hn::IfThenElse(lowerSide, hn::Sub(below, unitLowLine), hn::Sub(above, unitHighLine));
        const auto limit = hn::IfThenElse(lowerSide, lowDistance, highDistance);
        const auto limitCurve = hn::IfThenElse(lowerSide, unitLowCurve, unitHighCurve);
        const auto fallen = hn::IfThenElse(hn::Lt(drop, zero), zero, drop);
        const auto unclampedRise = hn::Sub(limitCurve, fallen);
        const auto rise = hn::IfThenElse(hn::Lt(unclampedRise, zero), zero, unclampedRise);
        hn::Vec<decltype(tag)> exponent;
        hn::Mask<decltype(tag)> exhausted = hn::FirstN(tag, 0u);
        if constexpr (kRegime == GeneralizedHyperbolicRegime::Logarithmic)
        {
            exponent = hn::Mul(unitAmount, rise);
        }
        else
        {
            const auto limitTail = hn::IfThenElse(lowerSide, lowTail, highTail);
            const auto remaining = hn::Add(limitTail, hn::Mul(unitSaturationRate, fallen));
            const auto fromTail = hn::Lt(remaining, half);
            const auto product = hn::Mul(unitSaturationRate, rise);
            const auto fraction = hn::IfThenElse(hn::Lt(one, product), one, product);
            const auto complement = hn::Sub(one, fraction);
            const auto argument = hn::IfThenElse(fromTail, remaining, complement);
            exhausted = hn::Eq(argument, zero);
            const auto logarithm = hn::Log(tag, hn::IfThenElse(exhausted, one, argument));
            // log1p(-m) is log(1 - m) * -m / ((1 - m) - 1), and -m where 1 - m rounds to one.
            const auto unit = hn::Eq(complement, one);
            const auto negated = hn::Neg(fraction);
            const auto quotient = hn::Div(negated, hn::IfThenElse(unit, one, hn::Sub(complement, one)));
            const auto direct = hn::IfThenElse(unit, negated, hn::Mul(logarithm, quotient));
            exponent = hn::Mul(hn::Neg(hn::IfThenElse(fromTail, logarithm, direct)), inverseSaturation);
        }
        hn::Vec<decltype(tag)> distance;
        if constexpr (kRegime == GeneralizedHyperbolicRegime::Exponential)
        {
            distance = hn::Mul(exponent, inverseAmount);
        }
        else
        {
            const auto scaled = hn::Mul(scale, exponent);
            const auto large = hn::Lt(largeExponent, scaled);
            distance = hn::Mul(hn::Expm1(tag, hn::IfThenElse(large, largeExponent, scaled)), inverseRate);
            if (c.largeExponents)
            {
                const auto shifted = hn::Sub(scaled, logRate);
                const auto exponential = hn::Exp(tag, hn::IfThenElse(hn::Lt(one, shifted), one, shifted));
                distance = hn::IfThenElse(large, exponential, distance);
            }
        }
        const auto reached = hn::IfThenElse(hn::Lt(distance, limit), distance, limit);
        const auto offset = hn::IfThenElse(hn::Eq(rise, zero), zero, hn::IfThenElse(exhausted, limit, reached));
        const auto curve = hn::IfThenElse(lowerSide, hn::Sub(symmetry, offset), hn::Add(symmetry, offset));
        const auto lowerLine = hn::Div(hn::Mul(y, extent), lowSlope);
        const auto upperLine = hn::Sub(one, hn::Div(hn::Mul(hn::Sub(one, y), extent), highSlope));
        const auto piece = hn::IfThenElse(hn::Lt(below, unitLowLine), lowerLine,
                                          hn::IfThenElse(hn::Lt(above, unitHighLine), upperLine, curve));
        const auto clampedLow = hn::IfThenElse(hn::Lt(piece, zero), zero, piece);
        const auto clamped = hn::IfThenElse(hn::Lt(one, clampedLow), one, clampedLow);
        const auto result = hn::IfThenElse(hn::Le(y, zero), zero, hn::IfThenElse(hn::Le(one, y), one, clamped));
        hn::StoreN(hn::DemoteTo(narrow, result), narrow, destination + index, partial);
    }
}

/// Writes one contiguous row of a planned forward generalized-hyperbolic curve.
///
/// @param source the samples to stretch, each finite.
/// @param destination where the stretched samples are written; may be `source`.
/// @param count how many samples.
/// @param c the planned coefficients.
void generalizedHyperbolicFloat32RunOf(const float* source, float* destination, std::uint64_t count,
                                       const GeneralizedHyperbolicCoefficients& c) noexcept
{
    switch (c.regime)
    {
    case GeneralizedHyperbolicRegime::Exponential:
        generalizedHyperbolicForwardRow<GeneralizedHyperbolicRegime::Exponential>(source, destination, count, c);
        return;
    case GeneralizedHyperbolicRegime::Logarithmic:
        generalizedHyperbolicForwardRow<GeneralizedHyperbolicRegime::Logarithmic>(source, destination, count, c);
        return;
    case GeneralizedHyperbolicRegime::IntegralPower:
        generalizedHyperbolicForwardRow<GeneralizedHyperbolicRegime::IntegralPower>(source, destination, count, c);
        return;
    case GeneralizedHyperbolicRegime::HyperbolicPower:
        generalizedHyperbolicForwardRow<GeneralizedHyperbolicRegime::HyperbolicPower>(source, destination, count, c);
        return;
    }
}

/// Writes one contiguous row of a planned inverse generalized-hyperbolic curve.
///
/// @param source the samples to invert, each finite.
/// @param destination where the inverted samples are written; may be `source`.
/// @param count how many samples.
/// @param c the planned coefficients.
void generalizedHyperbolicInverseFloat32RunOf(const float* source, float* destination, std::uint64_t count,
                                              const GeneralizedHyperbolicCoefficients& c) noexcept
{
    switch (c.regime)
    {
    case GeneralizedHyperbolicRegime::Exponential:
        generalizedHyperbolicInverseRow<GeneralizedHyperbolicRegime::Exponential>(source, destination, count, c);
        return;
    case GeneralizedHyperbolicRegime::Logarithmic:
        generalizedHyperbolicInverseRow<GeneralizedHyperbolicRegime::Logarithmic>(source, destination, count, c);
        return;
    case GeneralizedHyperbolicRegime::IntegralPower:
        generalizedHyperbolicInverseRow<GeneralizedHyperbolicRegime::IntegralPower>(source, destination, count, c);
        return;
    case GeneralizedHyperbolicRegime::HyperbolicPower:
        generalizedHyperbolicInverseRow<GeneralizedHyperbolicRegime::HyperbolicPower>(source, destination, count, c);
        return;
    }
}

/// The Float32 lane arithmetic of one planned midtones transfer a Float32 plan admits, with its
/// constants broadcast once per row.
///
/// Every lane scales its sample, measures both distances from the two-part endpoints - the shadow
/// point's parts subtracted larger first, the highlight point's remainder added after the difference
/// - and divides the rise by the denominator once, then keeps the authority's selections through
/// the plan's Float32 bounds: one from `oneFrom`, then zero through `zeroThrough`, which wins where
/// the two coincide. The bounds stand in for clamping the input to the unit interval, as in the
/// Float64 lanes. A lane the selections replace may compute anything, including a quotient of
/// infinities; nothing reads it.
///
/// @tparam DF the full Float32 vector the row loads and stores.
template <class DF> struct MidtonesTransferFloat32Lanes
{
    using VF = hn::Vec<DF>;

    /// Broadcasts the plan.
    ///
    /// @param c the planned transfer; its Float32 plan is admitted.
    explicit MidtonesTransferFloat32Lanes(const MidtonesTransferCoefficients& c)
        : zero(hn::Zero(DF())), one(hn::Set(DF(), 1.0f)), scale(hn::Set(DF(), kMidtonesTransferFloat32Scale)),
          midtones(hn::Set(DF(), c.float32.midtones)), complement(hn::Set(DF(), c.float32.complement)),
          shadows0(hn::Set(DF(), c.float32.shadows[0])), shadows1(hn::Set(DF(), c.float32.shadows[1])),
          highlights0(hn::Set(DF(), c.float32.highlights[0])), highlights1(hn::Set(DF(), c.float32.highlights[1])),
          zeroThrough(hn::Set(DF(), c.float32.zeroThrough)), oneFrom(hn::Set(DF(), c.float32.oneFrom))
    {
    }

    /// The transfer of one full vector of Float32 samples.
    ///
    /// @param samples the samples, each finite.
    /// @return their images.
    HWY_INLINE VF operator()(const VF samples) const
    {
        const VF scaled = hn::Mul(samples, scale);
        const VF above = hn::Sub(hn::Sub(scaled, shadows0), shadows1);
        const VF below = hn::Add(hn::Sub(highlights0, scaled), highlights1);
        const VF rise = hn::Mul(complement, above);
        const VF value = hn::Div(rise, hn::Add(hn::Mul(midtones, below), rise));
        const VF top = hn::IfThenElse(hn::Le(oneFrom, samples), one, value);
        return hn::IfThenElse(hn::Le(samples, zeroThrough), zero, top);
    }

    /// Zero, which a sample at or below the shadow point writes.
    VF zero;
    /// One, which a sample at or above the highlight point writes.
    VF one;
    /// `kMidtonesTransferFloat32Scale`.
    VF scale;
    /// The midtones balance `m`.
    VF midtones;
    /// `1 - m`.
    VF complement;
    /// The scaled shadow point, rounded.
    VF shadows0;
    /// Its remainder.
    VF shadows1;
    /// The scaled highlight point, rounded.
    VF highlights0;
    /// Its remainder.
    VF highlights1;
    /// The largest input written as zero.
    VF zeroThrough;
    /// The smallest input written as one.
    VF oneFrom;
};

/// The Float64 lane arithmetic of one planned midtones transfer a Float32 plan does not admit, with
/// its constants broadcast once per row.
///
/// Each full Float32 vector is widened in two halves, and every lane evaluates the scalar
/// authority's operations in its order and keeps its selections: one at or above the highlight
/// point, then zero through `zeroThrough`, which wins where the two coincide. That bound stands in
/// for clamping the input to the unit interval, which costs this row about two fifths of its time
/// and changes no selection it makes. A lane the selections replace may compute a quotient of zeros;
/// nothing reads it.
///
/// @tparam DF the full Float32 vector the row loads and stores.
template <class DF> struct MidtonesTransferLanes
{
    using DH = hn::Half<DF>;
    using DD = hn::Repartition<double, DF>;
    using VD = hn::Vec<DD>;
    using VF = hn::Vec<DF>;

    /// Broadcasts the plan.
    ///
    /// @param c the planned transfer.
    explicit MidtonesTransferLanes(const MidtonesTransferCoefficients& c)
        : zero(hn::Zero(DD())), one(hn::Set(DD(), 1.0)), midtones(hn::Set(DD(), c.midtones)),
          complement(hn::Set(DD(), c.complement)), shadows(hn::Set(DD(), c.shadows)),
          highlights(hn::Set(DD(), c.highlights)), zeroThrough(hn::Set(DD(), c.zeroThrough))
    {
    }

    /// The transfer of one vector of widened samples.
    ///
    /// @param sample the samples, each finite.
    /// @return their images.
    HWY_INLINE VD curve(const VD input) const
    {
        const VD rise = hn::Mul(complement, hn::Sub(input, shadows));
        const VD value = hn::Div(rise, hn::Add(hn::Mul(midtones, hn::Sub(highlights, input)), rise));
        const VD top = hn::IfThenElse(hn::Le(highlights, input), one, value);
        return hn::IfThenElse(hn::Le(input, zeroThrough), zero, top);
    }

    /// The transfer of one full vector of Float32 samples, through both widened halves.
    ///
    /// @param samples the samples, each finite.
    /// @return their images, narrowed.
    HWY_INLINE VF operator()(const VF samples) const
    {
        const VD low = curve(hn::PromoteTo(DD(), hn::LowerHalf(DH(), samples)));
        const VD high = curve(hn::PromoteTo(DD(), hn::UpperHalf(DH(), samples)));
        return hn::Combine(DF(), hn::DemoteTo(DH(), high), hn::DemoteTo(DH(), low));
    }

    /// Zero, which a sample at or below the shadow point writes.
    VD zero;
    /// One, which a sample at or above the highlight point writes.
    VD one;
    /// The midtones balance `m`.
    VD midtones;
    /// `1 - m`.
    VD complement;
    /// The shadow point.
    VD shadows;
    /// The highlight point.
    VD highlights;
    /// The largest input written as zero.
    VD zeroThrough;
};

/// Whether one vector of Float32 samples holds a value whose exponent bits are all set.
///
/// @param d the vector's tag.
/// @param samples the samples.
/// @return the lanes that are infinite or not a number.
template <class DF> HWY_INLINE auto nonFiniteLanes(const DF d, const hn::Vec<DF> samples)
{
    const hn::RebindToUnsigned<DF> du;
    const auto exponent = hn::Set(du, 0x7f800000u);
    return hn::RebindMask(d, hn::Eq(hn::And(hn::BitCast(du, samples), exponent), exponent));
}

/// Writes one contiguous row of a planned midtones transfer, with ordinary or streaming stores, and
/// checks the next row while it writes this one.
///
/// A streaming row writes the samples before its first aligned vector, and any after its last full
/// one, as partial vectors with ordinary stores, and fences its streaming stores before it returns.
/// Each vector of `upcoming` at the same offset is loaded beside the vector it transfers and tested
/// for a sample that is not finite. A caller that checks a row before writing it would otherwise
/// read each row from memory with nothing to overlap it, which cost about a quarter of a call's time;
/// loaded here, the next row arrives while the divisions run.
///
/// @tparam Lanes the lane arithmetic, Float32 or Float64.
/// @tparam kStream whether full vectors are written with streaming stores.
/// @param source the samples to transfer, each finite.
/// @param destination where the transferred samples are written; may be `source` unless streaming.
/// @param count how many samples.
/// @param c the planned transfer.
/// @param upcoming `count` samples to check, or null.
/// @return whether every sample of `upcoming` is finite; true when it is null.
template <template <class> class Lanes, bool kStream>
bool midtonesTransferRow(const float* source, float* destination, std::uint64_t count,
                         const MidtonesTransferCoefficients& c, const float* upcoming) noexcept
{
    const hn::ScalableTag<float> wide;
    const Lanes<decltype(wide)> transfer(c);
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(wide));
    // With no row to check, the source is checked again: it is finite and already being read.
    const float* checked = upcoming != nullptr ? upcoming : source;
    auto nonFinite = hn::MaskFalse(wide);
    std::uint64_t x = 0u;
    if constexpr (kStream)
    {
        const std::uint64_t offset = (reinterpret_cast<std::uintptr_t>(destination) / sizeof(float)) % lanes;
        x = (std::min)(count, offset == 0u ? std::uint64_t{0u} : lanes - offset);
        if (x != 0u)
        {
            const std::size_t head = static_cast<std::size_t>(x);
            nonFinite = hn::Or(nonFinite, nonFiniteLanes(wide, hn::LoadN(wide, checked, head)));
            hn::StoreN(transfer(hn::LoadN(wide, source, head)), wide, destination, head);
        }
        for (; x + lanes <= count; x += lanes)
        {
            nonFinite = hn::Or(nonFinite, nonFiniteLanes(wide, hn::LoadU(wide, checked + x)));
            hn::Stream(transfer(hn::LoadU(wide, source + x)), wide, destination + x);
        }
    }
    else
    {
        for (; x + lanes <= count; x += lanes)
        {
            nonFinite = hn::Or(nonFinite, nonFiniteLanes(wide, hn::LoadU(wide, checked + x)));
            hn::StoreU(transfer(hn::LoadU(wide, source + x)), wide, destination + x);
        }
    }
    if (x < count)
    {
        const std::size_t partial = static_cast<std::size_t>(count - x);
        nonFinite = hn::Or(nonFinite, nonFiniteLanes(wide, hn::LoadN(wide, checked + x, partial)));
        hn::StoreN(transfer(hn::LoadN(wide, source + x, partial)), wide, destination + x, partial);
    }
#if HWY_ARCH_X86
    if constexpr (kStream)
    {
        _mm_sfence();
    }
#endif
    return hn::AllFalse(wide, nonFinite);
}

/// Writes one contiguous row of a planned midtones transfer, in Float32 where its plan admits that,
/// and checks the next; see `midtonesTransferRow`.
///
/// @param source the samples to transfer, each finite.
/// @param destination where the transferred samples are written.
/// @param count how many samples.
/// @param c the planned transfer.
/// @param stream whether full vectors are written with streaming stores.
/// @param upcoming `count` samples to check, or null.
/// @return whether every sample of `upcoming` is finite; true when it is null.
bool midtonesTransferFloat32RunOf(const float* source, float* destination, std::uint64_t count,
                                  const MidtonesTransferCoefficients& c, bool stream, const float* upcoming) noexcept
{
    if (c.float32.admitted)
    {
        return stream
                   ? midtonesTransferRow<MidtonesTransferFloat32Lanes, true>(source, destination, count, c, upcoming)
                   : midtonesTransferRow<MidtonesTransferFloat32Lanes, false>(source, destination, count, c, upcoming);
    }
    return stream ? midtonesTransferRow<MidtonesTransferLanes, true>(source, destination, count, c, upcoming)
                  : midtonesTransferRow<MidtonesTransferLanes, false>(source, destination, count, c, upcoming);
}

} // namespace HWY_NAMESPACE
} // namespace stretch_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace stretch_simd
{
namespace
{

constexpr StretchCpuRuns kAvx2Runs{.asinhFloat32 = HWY_CHOOSE_AVX2(asinhFloat32RunOf),
                                   .asinhInverseFloat32 = HWY_CHOOSE_AVX2(asinhInverseFloat32RunOf),
                                   .generalizedHyperbolicFloat32 = HWY_CHOOSE_AVX2(generalizedHyperbolicFloat32RunOf),
                                   .generalizedHyperbolicInverseFloat32 =
                                       HWY_CHOOSE_AVX2(generalizedHyperbolicInverseFloat32RunOf),
                                   .midtonesTransferFloat32 = HWY_CHOOSE_AVX2(midtonesTransferFloat32RunOf)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr StretchCpuRuns kAvx3Runs{.asinhFloat32 = HWY_CHOOSE_AVX3(asinhFloat32RunOf),
                                   .asinhInverseFloat32 = HWY_CHOOSE_AVX3(asinhInverseFloat32RunOf),
                                   .generalizedHyperbolicFloat32 = HWY_CHOOSE_AVX3(generalizedHyperbolicFloat32RunOf),
                                   .generalizedHyperbolicInverseFloat32 =
                                       HWY_CHOOSE_AVX3(generalizedHyperbolicInverseFloat32RunOf),
                                   .midtonesTransferFloat32 = HWY_CHOOSE_AVX3(midtonesTransferFloat32RunOf)};
#endif

} // namespace
} // namespace stretch_simd

const StretchCpuRuns* simdStretchRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &stretch_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &stretch_simd::kAvx3Runs;
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
