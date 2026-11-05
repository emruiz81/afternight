// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - generalized, histogram, asinh and screen-transfer stretches.
//
// Also the home of the midtones transfer function `MidtonesTransfer.h` publishes, since the screen
// transfer is one of these families. It is evaluated as its definition multiplied through by the
// endpoint range, from the input's distances to both endpoints, which cancels nothing and cannot
// leave the unit interval; an image applies it one row of a plane at a time, with the channel's
// transfer planned once.
//
// Each nonlinear family is a straight line below the low protection point and above the high one
// and the family's curve either side of the symmetry point, planned once per request and normalized
// to run from zero at zero to one at one. The forward histogram's coefficients are evaluated in
// double, and histogram inversion uses the normalized rational primitive rather than a separate
// closed form per inverse. The asinh curve is planned once from its primitive for both directions,
// with tangent slopes that square nothing - squaring the amount would overflow above about 1e154 -
// and is inverted through the hyperbolic sine of the normalized primitive. The generalized-hyperbolic
// curve is planned once from its exponent for both directions, which keeps its precision at every
// admitted amount and beside each regime boundary, and is inverted directly - where it saturates,
// from what it has still to rise.

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>

#include "kernels/reference/MidtonesTransferRow.h"
#include "kernels/reference/AlgorithmKernels.h"

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <type_traits>

namespace an
{
namespace
{

constexpr const char* kScalarOperation = "an::evaluateGeneralizedStretch";
constexpr const char* kMtfOperation = "an::evaluateMidtonesTransfer";
constexpr const char* kAutoStretchOperation = "an::deriveAutoStretch";
constexpr const char* kImageOperation = "an::stretchImage";
constexpr const char* kHistogramOperation = "an::remapStretchHistogram";

[[nodiscard]] Status stretchFailure(ErrorCode code, const char* message, const char* operation)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool finiteAndUnit(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

[[nodiscard]] Status validateMidtones(const MidtonesTransferParameters& parameters, const char* operation)
{
    if (!finiteAndUnit(parameters.midtones) || !finiteAndUnit(parameters.shadows) ||
        !finiteAndUnit(parameters.highlights) || parameters.highlights < parameters.shadows)
    {
        return stretchFailure(ErrorCode::InvalidArgument,
                              "midtones parameters must be finite unit values with ordered endpoints", operation);
    }
    return Status::success();
}

// A Float64 endpoint scaled for a Float32 row, as two Float32 parts: the scaled endpoint rounded, and
// its remainder - exact in Float64, since the scaling is by a power of two - rounded.
[[nodiscard]] std::array<float, 2u> splitScaledEndpoint(double endpoint) noexcept
{
    const double scaled = endpoint * static_cast<double>(kMidtonesTransferFloat32Scale);
    const float high = static_cast<float>(scaled);
    return {high, static_cast<float>(scaled - static_cast<double>(high))};
}

// The largest Float32 value at or below a bound, which selects exactly the Float32 inputs at or below
// it; infinity for an infinite bound.
[[nodiscard]] float float32AtOrBelow(double bound) noexcept
{
    const float nearest = static_cast<float>(bound);
    return static_cast<double>(nearest) > bound ? std::nextafter(nearest, -std::numeric_limits<float>::infinity())
                                                : nearest;
}

// The smallest Float32 value at or above a bound, which selects exactly the Float32 inputs at or above
// it.
[[nodiscard]] float float32AtOrAbove(double bound) noexcept
{
    const float nearest = static_cast<float>(bound);
    return static_cast<double>(nearest) < bound ? std::nextafter(nearest, std::numeric_limits<float>::infinity())
                                                : nearest;
}

[[nodiscard]] MidtonesTransferCoefficients planMidtonesTransfer(const MidtonesTransferParameters& parameters) noexcept
{
    MidtonesTransferCoefficients plan{.midtones = parameters.midtones,
                                      .complement = 1.0 - parameters.midtones,
                                      .shadows = parameters.shadows,
                                      .highlights = parameters.highlights,
                                      .zeroThrough = parameters.shadows >= 1.0 ? std::numeric_limits<double>::infinity()
                                                                               : parameters.shadows};
    plan.float32 =
        MidtonesTransferFloat32Plan{.admitted = plan.midtones >= kMidtonesTransferFloat32Floor &&
                                                plan.complement >= kMidtonesTransferFloat32Floor &&
                                                plan.highlights - plan.shadows >= kMidtonesTransferFloat32Floor,
                                    .midtones = static_cast<float>(plan.midtones),
                                    .complement = static_cast<float>(plan.complement),
                                    .shadows = splitScaledEndpoint(plan.shadows),
                                    .highlights = splitScaledEndpoint(plan.highlights),
                                    .zeroThrough = float32AtOrBelow(plan.zeroThrough),
                                    .oneFrom = float32AtOrAbove(plan.highlights)};
    return plan;
}

// The scalar authority of the midtones transfer. The compiled row evaluates the same fraction in
// Float32 within a stated bound of it, or performs these operations in this order in Float64. The
// definition `(m - 1) x / ((2m - 1) x - m)` of the rescaled input `x = (v - s) / (h - s)` is
// multiplied through by `h - s`. Between the endpoints both distances are positive, so nothing
// cancels - the definition's denominator does, for a balance near one beside the highlight point -
// and the rise is part of the denominator, so the value is in the unit interval, positive zero at a
// balance of one. A Float64 input one step below the highlight point can round to the highlight
// point's own rescaled distance, which the definition turns into a quotient of zeros at a balance of
// one.
[[nodiscard]] double evaluateMidtonesPlanned(double value, const MidtonesTransferCoefficients& c) noexcept
{
    if (value <= c.shadows)
    {
        return 0.0;
    }
    if (value >= c.highlights)
    {
        return 1.0;
    }
    const double rise = c.complement * (value - c.shadows);
    return rise / (c.midtones * (c.highlights - value) + rise);
}

[[nodiscard]] double evaluateMidtonesUnchecked(double value, const MidtonesTransferParameters& parameters) noexcept
{
    return evaluateMidtonesPlanned(value, planMidtonesTransfer(parameters));
}

// The planned coefficients of one piecewise curve. The forward histogram's carry their
// conventional names: `qlp`, `q0`, `qwp`, `q1` and `q` normalize the curve, and each
// `a`/`b`/`c`/`d`/`e` group is one piece - 1 the line below the low protection point, 2 and 3 the
// curve below and above the symmetry point, 4 the line above the high protection point.
struct StretchCoefficients
{
    double qlp = 0.0;
    double q0 = 0.0;
    double qwp = 0.0;
    double q1 = 0.0;
    double q = 0.0;
    double a1 = 0.0;
    double b1 = 0.0;
    double a2 = 0.0;
    double b2 = 0.0;
    double c2 = 0.0;
    double d2 = 0.0;
    double e2 = 0.0;
    double a3 = 0.0;
    double b3 = 0.0;
    double c3 = 0.0;
    double d3 = 0.0;
    double e3 = 0.0;
    double a4 = 0.0;
    double b4 = 0.0;

    // Endpoint-anchored inverse histogram pieces. The upper half is reflected about one.
    // Positive denominator terms avoid cancellation at large amounts and near protection points.
    struct HistogramInverse
    {
        bool identity = true;
        bool scaledTails = false;
        double center = 0.0;
        double extent = 1.0;
        double split = 1.0;
        double low = 0.0;
        double highComplement = 0.0;
        double lowBoundary = 1.0;
        double highBoundary = 1.0;
        double lowLinear = 1.0;
        double highLinear = 1.0;
        double lowNumerator = 1.0;
        double lowDenominator = 1.0;
        double highDenominator = 1.0;
        double curvature = 0.0;
    } histogramInverse;

    // The asinh curve in both directions, planned from its primitive with tangent slopes that square
    // nothing; squaring the amount would overflow above about 1e154.
    struct Asinh
    {
        bool identity = true;
        AsinhCoefficients coefficients{};
    } asinh;

    // The generalized-hyperbolic curve in both directions, planned from its exponent rather than
    // as the pieces above: those add one to a product of the amount that loses it at a large
    // amount, and subtract nearly equal powers when the bias is near a regime boundary.
    struct GeneralizedHyperbolic
    {
        bool identity = true;
        GeneralizedHyperbolicCoefficients coefficients{};
    } generalizedHyperbolic;
};

// The exponent `E(t)` a distance `t` from the symmetry point; see `GeneralizedHyperbolicRegime`.
[[nodiscard]] double generalizedHyperbolicExponent(double distance, const GeneralizedHyperbolicCoefficients& c) noexcept
{
    return c.regime == GeneralizedHyperbolicRegime::Exponential ? c.amount * distance
                                                                : std::log1p(c.rate * distance) * c.inverseScale;
}

// The curve's rise `G` at a distance whose exponent is `exponent`.
[[nodiscard]] double generalizedHyperbolicCurve(double exponent, const GeneralizedHyperbolicCoefficients& c) noexcept
{
    return c.regime == GeneralizedHyperbolicRegime::Logarithmic
               ? exponent * c.inverseAmount
               : -std::expm1(-c.saturation * exponent) * c.inverseSaturationRate;
}

// Plans both directions of one generalized-hyperbolic request; see
// `GeneralizedHyperbolicCoefficients` for each value and `GeneralizedHyperbolicRegime` for which
// member a bias selects.
[[nodiscard]] StretchCoefficients::GeneralizedHyperbolic
planGeneralizedHyperbolic(const GeneralizedStretchParameters& parameters) noexcept
{
    StretchCoefficients::GeneralizedHyperbolic result;
    const double amount = parameters.amount;
    const double bias = parameters.bias;
    if (amount < kGeneralizedHyperbolicIdentityAmount)
    {
        return result;
    }
    result.identity = false;
    GeneralizedHyperbolicCoefficients& c = result.coefficients;
    const double magnitude = std::abs(bias);
    c.amount = amount;
    c.inverseAmount = 1.0 / amount;
    if (bias == -1.0)
    {
        c.regime = GeneralizedHyperbolicRegime::Logarithmic;
        c.saturation = 0.0;
    }
    else if (magnitude < 0x1p-80 || magnitude * amount < 0x1p-60)
    {
        c.regime = GeneralizedHyperbolicRegime::Exponential;
    }
    else
    {
        c.regime =
            bias < 0.0 ? GeneralizedHyperbolicRegime::IntegralPower : GeneralizedHyperbolicRegime::HyperbolicPower;
        c.scale = magnitude;
        c.saturation = bias < 0.0 ? 1.0 + bias : 1.0;
    }
    c.inverseScale = 1.0 / c.scale;
    c.rate = c.scale * amount;
    c.inverseRate = 1.0 / c.rate;
    c.logRate = std::log(c.rate);
    c.saturationRate = c.saturation * amount;
    c.inverseSaturation = c.saturation == 0.0 ? 0.0 : 1.0 / c.saturation;
    c.inverseSaturationRate = c.saturation == 0.0 ? 0.0 : 1.0 / c.saturationRate;
    c.symmetryPoint = parameters.symmetryPoint;
    c.lowProtection = parameters.lowProtection;
    c.highProtection = parameters.highProtection;
    c.lowDistance = parameters.symmetryPoint - parameters.lowProtection;
    c.highDistance = parameters.highProtection - parameters.symmetryPoint;
    // The slope is exp(-E), and exp(-(1 + b) E) above zero bias.
    const double slopeExponent = c.regime == GeneralizedHyperbolicRegime::HyperbolicPower ? 1.0 + bias : 1.0;
    const double lowExponent = generalizedHyperbolicExponent(c.lowDistance, c);
    const double highExponent = generalizedHyperbolicExponent(c.highDistance, c);
    c.lowCurve = generalizedHyperbolicCurve(lowExponent, c);
    c.highCurve = generalizedHyperbolicCurve(highExponent, c);
    c.lowSlope = std::exp(-slopeExponent * lowExponent);
    c.highSlope = std::exp(-slopeExponent * highExponent);
    c.lowTail = std::exp(-c.saturation * lowExponent);
    c.highTail = std::exp(-c.saturation * highExponent);
    c.largeExponents = c.regime != GeneralizedHyperbolicRegime::Exponential &&
                       0.75 * kGeneralizedHyperbolicLargeExponent < c.scale * (std::max)(lowExponent, highExponent);
    c.lowLine = parameters.lowProtection * c.lowSlope;
    c.highLine = (1.0 - parameters.highProtection) * c.highSlope;
    c.lowRise = c.lowLine + c.lowCurve;
    c.extent = c.lowRise + (c.highLine + c.highCurve);
    // The inverse's units: a power of two bringing the extent into [1, 2), kept only where every
    // value survives the exchange exactly. It is never below one, so a rise - at most the extent -
    // neither overflows nor loses a bit; only a quotient can, which multiplying it back reveals. An
    // extent that is not a positive finite number fails the plan's check and keeps unit one here.
    const int power = c.extent > 0.0 && std::isfinite(c.extent) ? -std::ilogb(c.extent) : 0;
    const double unit = std::ldexp(1.0, std::clamp(power, 0, 1023));
    const double unitSaturationRate = c.saturationRate / unit;
    const double unitAmount = amount / unit;
    const bool exact = unitSaturationRate * unit == c.saturationRate && unitAmount * unit == amount;
    const double exchange = exact ? unit : 1.0;
    c.unitExtent = c.extent * exchange;
    c.unitLowLine = c.lowLine * exchange;
    c.unitHighLine = c.highLine * exchange;
    c.unitLowCurve = c.lowCurve * exchange;
    c.unitHighCurve = c.highCurve * exchange;
    c.unitLowRise = c.lowRise * exchange;
    c.unitSaturationRate = exact ? unitSaturationRate : c.saturationRate;
    c.unitAmount = exact ? unitAmount : amount;
    return result;
}

[[nodiscard]] bool generalizedHyperbolicIsFinite(const StretchCoefficients::GeneralizedHyperbolic& plan) noexcept
{
    const GeneralizedHyperbolicCoefficients& c = plan.coefficients;
    const std::array<double, 21u> values{
        c.inverseAmount, c.rate,           c.inverseRate,       c.logRate,
        c.inverseScale,  c.saturationRate, c.inverseSaturation, c.inverseSaturationRate,
        c.lowCurve,      c.highCurve,      c.lowSlope,          c.highSlope,
        c.lowTail,       c.highTail,       c.lowLine,           c.highLine,
        c.lowRise,       c.extent,         c.unitExtent,        c.unitSaturationRate,
        c.unitAmount};
    return plan.identity || (c.extent > 0.0 && std::all_of(values.begin(), values.end(),
                                                           [](double value) { return std::isfinite(value); }));
}

// The scalar authority of the forward generalized-hyperbolic curve. Each piece's rise is measured
// from its own end of the unit interval and divided by the extent once. Zero and below answer
// positive zero and one and above answer one; every other value is clamped to the unit interval.
[[nodiscard]] double evaluateGeneralizedHyperbolic(double value,
                                                   const StretchCoefficients::GeneralizedHyperbolic& plan) noexcept
{
    const double x = std::clamp(value, 0.0, 1.0);
    if (x <= 0.0 || x >= 1.0)
    {
        return x <= 0.0 ? 0.0 : 1.0;
    }
    if (plan.identity)
    {
        return x;
    }
    const GeneralizedHyperbolicCoefficients& c = plan.coefficients;
    const bool upper = !(x < c.symmetryPoint);
    double rise = 0.0;
    if (x < c.lowProtection)
    {
        rise = x * c.lowSlope;
    }
    else if (!upper)
    {
        rise = c.lowLine +
               (c.lowCurve - generalizedHyperbolicCurve(generalizedHyperbolicExponent(c.symmetryPoint - x, c), c));
    }
    else if (x <= c.highProtection)
    {
        rise = c.highLine +
               (c.highCurve - generalizedHyperbolicCurve(generalizedHyperbolicExponent(x - c.symmetryPoint, c), c));
    }
    else
    {
        rise = (1.0 - x) * c.highSlope;
    }
    const double share = rise / c.extent;
    return std::clamp(upper ? 1.0 - share : share, 0.0, 1.0);
}

// How far from the symmetry point, at most `limit`, a curve piece is `drop` below its value at
// `limit`, where it rises `limitCurve` and has `limitTail` still to rise; the drop and the rise are in
// the plan's units. A remaining rise below one half is solved from its logarithm, and a larger one
// from the rise itself; a target beyond the curve's reach answers the limit. The compiled rows write
// each step the same way.
[[nodiscard]] double generalizedHyperbolicDistance(double drop, double limit, double limitCurve, double limitTail,
                                                   const GeneralizedHyperbolicCoefficients& c) noexcept
{
    const double fallen = (std::max)(drop, 0.0);
    const double rise = (std::max)(limitCurve - fallen, 0.0);
    if (rise == 0.0)
    {
        return 0.0;
    }
    double exponent = 0.0;
    if (c.regime == GeneralizedHyperbolicRegime::Logarithmic)
    {
        exponent = c.unitAmount * rise;
    }
    else
    {
        const double remaining = limitTail + c.unitSaturationRate * fallen;
        exponent = remaining < 0.5 ? -std::log(remaining) * c.inverseSaturation
                                   : -std::log1p(-(std::min)(c.unitSaturationRate * rise, 1.0)) * c.inverseSaturation;
    }
    double distance = 0.0;
    if (c.regime == GeneralizedHyperbolicRegime::Exponential)
    {
        distance = exponent * c.inverseAmount;
    }
    else
    {
        const double scaled = c.scale * exponent;
        distance = kGeneralizedHyperbolicLargeExponent < scaled ? std::exp((std::min)(scaled - c.logRate, 1.0))
                                                                : std::expm1(scaled) * c.inverseRate;
    }
    return std::isnan(distance) ? limit : (std::min)(distance, limit);
}

// The scalar authority of the inverse generalized-hyperbolic curve, with the forward's endpoints.
[[nodiscard]] double invertGeneralizedHyperbolic(double value,
                                                 const StretchCoefficients::GeneralizedHyperbolic& plan) noexcept
{
    const double y = std::clamp(value, 0.0, 1.0);
    if (y <= 0.0 || y >= 1.0)
    {
        return y <= 0.0 ? 0.0 : 1.0;
    }
    if (plan.identity)
    {
        return y;
    }
    const GeneralizedHyperbolicCoefficients& c = plan.coefficients;
    // The value's rise from either end, in the plan's units; see `GeneralizedHyperbolicCoefficients`.
    const double below = y * c.unitExtent;
    const double above = (1.0 - y) * c.unitExtent;
    double x = 0.0;
    if (below < c.unitLowLine)
    {
        x = y * c.extent / c.lowSlope;
    }
    else if (above < c.unitHighLine)
    {
        x = 1.0 - (1.0 - y) * c.extent / c.highSlope;
    }
    else if (below < c.unitLowRise)
    {
        x = c.symmetryPoint -
            generalizedHyperbolicDistance(below - c.unitLowLine, c.lowDistance, c.unitLowCurve, c.lowTail, c);
    }
    else
    {
        x = c.symmetryPoint +
            generalizedHyperbolicDistance(above - c.unitHighLine, c.highDistance, c.unitHighCurve, c.highTail, c);
    }
    return std::clamp(x, 0.0, 1.0);
}

// Plans both directions of one asinh request; see `AsinhCoefficients` for what each value is.
[[nodiscard]] StretchCoefficients::Asinh planAsinh(const GeneralizedStretchParameters& parameters) noexcept
{
    StretchCoefficients::Asinh result;
    const double amount = parameters.amount;
    if (amount < kAsinhIdentityAmount)
    {
        return result;
    }
    result.identity = false;
    AsinhCoefficients& c = result.coefficients;
    const double lowDistance = parameters.symmetryPoint - parameters.lowProtection;
    const double highDistance = parameters.highProtection - parameters.symmetryPoint;
    // The tangent slope amount / sqrt(1 + (amount distance)^2), written so nothing is squared: the
    // product is at most the amount, and the slope at the symmetry point is the amount itself.
    const double lowSlope = amount / std::hypot(1.0, amount * lowDistance);
    const double highSlope = amount / std::hypot(1.0, amount * highDistance);
    c.amount = amount;
    c.lowProtection = parameters.lowProtection;
    c.highProtection = parameters.highProtection;
    c.lowPrimitive = -std::asinh(amount * lowDistance);
    c.highPrimitive = std::asinh(amount * highDistance);
    c.origin = c.lowPrimitive - parameters.lowProtection * lowSlope;
    c.extent = c.highPrimitive + (1.0 - parameters.highProtection) * highSlope - c.origin;
    c.symmetryPoint = parameters.symmetryPoint;
    c.lowGradient = lowSlope / c.extent;
    c.highGradient = highSlope / c.extent;
    // Both measured from zero: where a tangent line at the symmetry point is steep enough to take
    // nearly all of the extent, the curve's share is below a rounding of one and would be lost.
    c.lowOutput = parameters.lowProtection * lowSlope / c.extent;
    c.highOutput = (c.highPrimitive - c.origin) / c.extent;
    c.lowScale = c.extent / lowSlope;
    c.highScale = c.extent / highSlope;
    c.inverseAmount = 1.0 / amount;
    c.logTwiceAmount = std::log(amount) + std::numbers::ln2;
    c.largeArguments = (std::max)(-c.lowPrimitive, c.highPrimitive) > kAsinhInverseLargeArgument;
    return result;
}

[[nodiscard]] bool asinhIsFinite(const StretchCoefficients::Asinh& plan) noexcept
{
    const AsinhCoefficients& c = plan.coefficients;
    const std::array<double, 13u> values{
        c.origin,     c.extent,   c.lowPrimitive, c.highPrimitive, c.lowGradient,    c.highGradient, c.lowOutput,
        c.highOutput, c.lowScale, c.highScale,    c.inverseAmount, c.logTwiceAmount, c.amount};
    return plan.identity || (c.extent > 0.0 && std::all_of(values.begin(), values.end(),
                                                           [](double value) { return std::isfinite(value); }));
}

// The scalar authority of the forward asinh, which the compiled row reproduces with the vector
// library's inverse hyperbolic sine: every comparison and clamp below is written the way the row
// writes it. Zero and below answer positive zero, one and above answer one.
[[nodiscard]] double evaluateAsinh(double value, const StretchCoefficients::Asinh& plan) noexcept
{
    const double x = std::clamp(value, 0.0, 1.0);
    if (x <= 0.0 || x >= 1.0)
    {
        return x <= 0.0 ? 0.0 : 1.0;
    }
    if (plan.identity)
    {
        return x;
    }
    const AsinhCoefficients& c = plan.coefficients;
    double y = 0.0;
    if (x < c.lowProtection)
    {
        y = x * c.lowGradient;
    }
    else if (x < c.highProtection)
    {
        const bool upper = !(x < c.symmetryPoint);
        const double primitive = std::asinh(c.amount * (upper ? x - c.symmetryPoint : c.symmetryPoint - x));
        y = ((upper ? primitive : -primitive) - c.origin) / c.extent;
    }
    else
    {
        y = 1.0 - (1.0 - x) * c.highGradient;
    }
    return std::clamp(y, 0.0, 1.0);
}

// The scalar authority of the inverse asinh, which the compiled row reproduces with the vector
// library's hyperbolic sine and exponential: every comparison and clamp below is written the way
// the row writes it. Zero and below answer positive zero, one and above answer one.
[[nodiscard]] double evaluateAsinhInverse(double value, const StretchCoefficients::Asinh& plan) noexcept
{
    const double y = std::clamp(value, 0.0, 1.0);
    if (y <= 0.0 || y >= 1.0)
    {
        return y <= 0.0 ? 0.0 : 1.0;
    }
    if (plan.identity)
    {
        return y;
    }
    const AsinhCoefficients& c = plan.coefficients;
    double x = 0.0;
    if (y < c.lowOutput)
    {
        x = y * c.lowScale;
    }
    else if (c.highOutput < y)
    {
        x = 1.0 - (1.0 - y) * c.highScale;
    }
    else
    {
        const double argument = std::clamp(c.origin + y * c.extent, c.lowPrimitive, c.highPrimitive);
        const double magnitude = std::abs(argument);
        const double offset = kAsinhInverseLargeArgument < magnitude
                                  ? std::exp(magnitude - c.logTwiceAmount)
                                  : std::sinh((std::min)(magnitude, kAsinhInverseLargeArgument)) * c.inverseAmount;
        x = argument < 0.0 ? c.symmetryPoint - offset : c.symmetryPoint + offset;
    }
    return std::clamp(x, 0.0, 1.0);
}

[[nodiscard]] StretchCoefficients::HistogramInverse
planHistogramInverse(const GeneralizedStretchParameters& parameters) noexcept
{
    StretchCoefficients::HistogramInverse result;
    if (parameters.amount == 0.0 || parameters.lowProtection == parameters.highProtection)
    {
        return result;
    }
    result.identity = false;
    const double left = parameters.symmetryPoint - parameters.lowProtection;
    const double right = parameters.highProtection - parameters.symmetryPoint;
    // Twice a finite amount need not be representable. Use its reciprocal above one,
    // and multiply the extent before doubling below, so even an admitted maximal amount
    // never forms an infinite intermediate. Tiny amounts use the nonsingular identity limit.
    const double reciprocal = parameters.amount > 1.0 ? 0.5 / parameters.amount : 0.0;
    const double a =
        parameters.amount > 1.0 ? reciprocal / (reciprocal + left) : 1.0 / (1.0 + (2.0 * parameters.amount) * left);
    const double b =
        parameters.amount > 1.0 ? reciprocal / (reciprocal + right) : 1.0 / (1.0 + (2.0 * parameters.amount) * right);
    const double lowTail = parameters.lowProtection * a * a;
    const double highTail = (1.0 - parameters.highProtection) * b * b;
    const double leftIntegral = left * a + lowTail;
    const double extent = leftIntegral + right * b + highTail;
    result.center = parameters.symmetryPoint;
    result.extent = extent;
    result.split = leftIntegral / extent;
    result.low = parameters.lowProtection;
    result.highComplement = 1.0 - parameters.highProtection;
    // Normalize before the second reciprocal multiplication: a squared reciprocal can
    // underflow even when its normalized boundary and Double tail samples are representable.
    result.lowBoundary = parameters.lowProtection * (a / extent) * a;
    result.highBoundary = result.highComplement * (b / extent) * b;
    result.lowNumerator = extent / a;
    result.lowDenominator = a;
    result.highDenominator = b;
    // Subnormal boundaries use a bounded fraction of their endpoint in the general
    // evaluator, keeping the reciprocal slope itself from overflowing during planning.
    const double minimumNormal = (std::numeric_limits<double>::min)();
    result.scaledTails = (result.lowBoundary > 0.0 && result.lowBoundary < minimumNormal) ||
                         (result.highBoundary > 0.0 && result.highBoundary < minimumNormal);
    result.lowLinear = result.lowBoundary < minimumNormal ? 0.0 : parameters.lowProtection / result.lowBoundary;
    result.highLinear = result.highBoundary < minimumNormal ? 0.0 : result.highComplement / result.highBoundary;
    result.curvature = (parameters.amount * extent) * 2.0;
    return result;
}

[[nodiscard]] double evaluateHistogramInverse(double value, const StretchCoefficients::HistogramInverse& c) noexcept
{
    const double y = std::clamp(value, 0.0, 1.0);
    if (y <= 0.0 || y >= 1.0)
    {
        return y <= 0.0 ? 0.0 : 1.0;
    }
    if (y <= c.split)
    {
        if (y <= c.lowBoundary)
        {
            const double tail = c.scaledTails ? (y / c.lowBoundary) * c.low : y * c.lowLinear;
            return std::clamp(tail, 0.0, 1.0);
        }
        const double delta = y - c.lowBoundary;
        return std::clamp(c.low + delta * c.lowNumerator / (c.lowDenominator + delta * c.curvature), 0.0, 1.0);
    }
    const double reflected = 1.0 - y;
    if (reflected <= c.highBoundary)
    {
        const double tail = c.scaledTails ? (reflected / c.highBoundary) * c.highComplement : reflected * c.highLinear;
        return std::clamp(1.0 - tail, 0.0, 1.0);
    }
    const double delta = reflected - c.highBoundary;
    return std::clamp(c.center + (y - c.split) * c.extent / (c.highDenominator + delta * c.curvature), 0.0, 1.0);
}

[[nodiscard]] Status validateStretchParameters(const GeneralizedStretchParameters& parameters, const char* operation)
{
    if (!isStretchFunction(parameters.function))
    {
        return stretchFailure(ErrorCode::InvalidArgument, "the request names no supported stretch function", operation);
    }
    if (!std::isfinite(parameters.bias) || parameters.bias < kMinimumGeneralizedHyperbolicBias ||
        parameters.bias > kMaximumGeneralizedHyperbolicBias || !std::isfinite(parameters.amount) ||
        parameters.amount < 0.0 || !finiteAndUnit(parameters.lowProtection) ||
        !finiteAndUnit(parameters.symmetryPoint) || !finiteAndUnit(parameters.highProtection) ||
        parameters.symmetryPoint < parameters.lowProtection || parameters.highProtection < parameters.symmetryPoint ||
        !finiteAndUnit(parameters.blackPoint) || !finiteAndUnit(parameters.whitePoint))
    {
        return stretchFailure(ErrorCode::InvalidArgument,
                              "stretch parameters must be finite, normalized, ordered and have nonnegative amount",
                              operation);
    }
    if (parameters.function == StretchFunction::ScreenTransfer)
    {
        for (const MidtonesTransferParameters& channel : parameters.screenTransfer)
        {
            const Status valid = validateMidtones(channel, operation);
            if (valid.failed())
            {
                return valid;
            }
        }
    }
    return Status::success();
}

[[nodiscard]] bool coefficientsAreFinite(const StretchCoefficients& c) noexcept
{
    return std::isfinite(c.qlp) && std::isfinite(c.q0) && std::isfinite(c.qwp) && std::isfinite(c.q1) &&
           std::isfinite(c.q) && std::isfinite(c.a1) && std::isfinite(c.b1) && std::isfinite(c.a2) &&
           std::isfinite(c.b2) && std::isfinite(c.c2) && std::isfinite(c.d2) && std::isfinite(c.e2) &&
           std::isfinite(c.a3) && std::isfinite(c.b3) && std::isfinite(c.c3) && std::isfinite(c.d3) &&
           std::isfinite(c.e3) && std::isfinite(c.a4) && std::isfinite(c.b4);
}

[[nodiscard]] Result<StretchCoefficients> makeCoefficients(const GeneralizedStretchParameters& parameters,
                                                           StretchFunction forwardFunction, const char* operation)
{
    StretchCoefficients c;
    const double d = parameters.amount;
    const double lp = parameters.lowProtection;
    const double sp = parameters.symmetryPoint;
    const double hp = parameters.highProtection;
    if (d == 0.0 || forwardFunction == StretchFunction::Linear || forwardFunction == StretchFunction::Invert ||
        forwardFunction == StretchFunction::ScreenTransfer)
    {
        return c;
    }

    if (forwardFunction == StretchFunction::GeneralizedHyperbolic)
    {
        c.generalizedHyperbolic = planGeneralizedHyperbolic(parameters);
        if (!generalizedHyperbolicIsFinite(c.generalizedHyperbolic))
        {
            return stretchFailure(ErrorCode::InvalidArgument,
                                  "the stretch parameters produce singular or non-finite coefficients", operation);
        }
        return c;
    }
    if (forwardFunction == StretchFunction::Asinh)
    {
        c.asinh = planAsinh(parameters);
        if (!asinhIsFinite(c.asinh))
        {
            return stretchFailure(ErrorCode::InvalidArgument,
                                  "the stretch parameters produce singular or non-finite coefficients", operation);
        }
        return c;
    }
    if (forwardFunction == StretchFunction::Histogram)
    {
        const double m = 1.0 / (2.0 * (d + 1.0));
        const double lowDenominator = (1.0 - 2.0 * m) * (lp - sp) - m;
        const double highDenominator = (2.0 * m - 1.0) * (hp - sp) - m;
        c.qlp = (m - 1.0) * (lp - sp) / lowDenominator;
        c.q0 = c.qlp + lp * (m - 1.0) * m * std::pow(lowDenominator, -2.0);
        c.qwp = (m - 1.0) * (hp - sp) / highDenominator;
        c.q1 = c.qwp + (hp - 1.0) * (m - 1.0) * m * std::pow(highDenominator, -2.0);
        c.q = 1.0 / (c.q1 - c.q0);
        c.b1 = m * (1.0 - m) * std::pow(lowDenominator, -2.0) * c.q;
        c.a2 = -c.q0 * c.q;
        c.b2 = (m - 1.0) * c.q;
        c.c2 = -c.b2 * sp;
        c.d2 = 1.0 - 2.0 * m;
        c.e2 = -c.d2 * sp - m;
        c.a3 = -c.q0 * c.q;
        c.b3 = (m - 1.0) * c.q;
        c.c3 = -c.b3 * sp;
        c.d3 = 2.0 * m - 1.0;
        c.e3 = -c.d3 * sp - m;
        c.a4 = (c.qwp - hp * (1.0 - m) * m * std::pow(highDenominator, -2.0) - c.q0) * c.q;
        c.b4 = -m * (m - 1.0) * std::pow(highDenominator, -2.0) * c.q;
    }

    if (!coefficientsAreFinite(c))
    {
        return stretchFailure(ErrorCode::InvalidArgument,
                              "the stretch parameters produce singular or non-finite coefficients", operation);
    }
    if (parameters.function == StretchFunction::InverseHistogram)
    {
        c.histogramInverse = planHistogramInverse(parameters);
    }
    return c;
}

[[nodiscard]] double evaluateForwardUnchecked(double value, const GeneralizedStretchParameters& parameters,
                                              StretchFunction function, const StretchCoefficients& c) noexcept
{
    if (function == StretchFunction::GeneralizedHyperbolic)
    {
        return evaluateGeneralizedHyperbolic(value, c.generalizedHyperbolic);
    }
    if (function == StretchFunction::Asinh)
    {
        return evaluateAsinh(value, c.asinh);
    }
    const double x = std::clamp(value, 0.0, 1.0);
    const double d = parameters.amount;
    const double lp = parameters.lowProtection;
    const double sp = parameters.symmetryPoint;
    const double hp = parameters.highProtection;
    if (d == 0.0)
    {
        return x;
    }
    if (function == StretchFunction::Histogram)
    {
        if (x < lp)
        {
            return c.a1 + c.b1 * x;
        }
        if (x < sp)
        {
            return c.a2 + (c.b2 * x + c.c2) / (c.d2 * x + c.e2);
        }
        if (x < hp)
        {
            return c.a3 + (c.b3 * x + c.c3) / (c.d3 * x + c.e3);
        }
        return c.a4 + c.b4 * x;
    }
    return x;
}

// The forward family an inverse family is solved against; every other function is its own.
[[nodiscard]] StretchFunction forwardOf(StretchFunction function) noexcept
{
    switch (function)
    {
    case StretchFunction::InverseGeneralizedHyperbolic:
        return StretchFunction::GeneralizedHyperbolic;
    case StretchFunction::InverseHistogram:
        return StretchFunction::Histogram;
    case StretchFunction::InverseAsinh:
        return StretchFunction::Asinh;
    default:
        return function;
    }
}

[[nodiscard]] bool isInverse(StretchFunction function) noexcept
{
    return function == StretchFunction::InverseGeneralizedHyperbolic || function == StretchFunction::InverseHistogram ||
           function == StretchFunction::InverseAsinh;
}

// Every nonlinear family has a direct inverse; a target beyond the range answers that end's input.
[[nodiscard]] double inverseForwardUnchecked(double target, StretchFunction forwardFunction,
                                             const StretchCoefficients& coefficients) noexcept
{
    if (forwardFunction == StretchFunction::Histogram)
    {
        return evaluateHistogramInverse(target, coefficients.histogramInverse);
    }
    if (forwardFunction == StretchFunction::Asinh)
    {
        return evaluateAsinhInverse(target, coefficients.asinh);
    }
    return invertGeneralizedHyperbolic(target, coefficients.generalizedHyperbolic);
}

[[nodiscard]] double evaluatePlanned(double value, const GeneralizedStretchParameters& parameters,
                                     const StretchCoefficients& coefficients, std::uint32_t channel) noexcept
{
    const double normalized = std::clamp(value, 0.0, 1.0);
    switch (parameters.function)
    {
    case StretchFunction::Linear:
        if (parameters.whitePoint <= parameters.blackPoint)
        {
            return normalized <= parameters.blackPoint ? 0.0 : 1.0;
        }
        return std::clamp(((std::min)(normalized, parameters.whitePoint) - parameters.blackPoint) /
                              (parameters.whitePoint - parameters.blackPoint),
                          0.0, 1.0);
    case StretchFunction::Invert:
        return 1.0 - normalized;
    case StretchFunction::ScreenTransfer:
        return evaluateMidtonesUnchecked(normalized, parameters.screenTransfer[(std::min)(channel, 2u)]);
    default:
        break;
    }
    const StretchFunction forwardFunction = forwardOf(parameters.function);
    if (isInverse(parameters.function))
    {
        return inverseForwardUnchecked(normalized, forwardFunction, coefficients);
    }
    return evaluateForwardUnchecked(normalized, parameters, forwardFunction, coefficients);
}

[[nodiscard]] Status validateStatistics(std::span<const StretchChannelStatistics> statistics, double shadowsClipping,
                                        double targetBackground, const char* operation)
{
    if (statistics.empty() || !std::isfinite(shadowsClipping) || !finiteAndUnit(targetBackground))
    {
        return stretchFailure(ErrorCode::InvalidArgument,
                              "automatic stretch needs channel statistics and finite normalized controls", operation);
    }
    for (const StretchChannelStatistics& channel : statistics)
    {
        if (!finiteAndUnit(channel.median) || !std::isfinite(channel.medianAbsoluteDeviation) ||
            channel.medianAbsoluteDeviation < 0.0)
        {
            return stretchFailure(ErrorCode::InvalidArgument,
                                  "automatic stretch statistics must have normalized medians and nonnegative MADs",
                                  operation);
        }
    }
    return Status::success();
}

[[nodiscard]] bool usesInvertedAutoStretch(std::span<const StretchChannelStatistics> statistics) noexcept
{
    return std::all_of(statistics.begin(), statistics.end(),
                       [](const StretchChannelStatistics& channel) { return channel.median > 0.5; });
}

[[nodiscard]] double guardedSigma(const StretchChannelStatistics& statistics) noexcept
{
    const double sigma = statistics.medianAbsoluteDeviation * kMedianAbsoluteDeviationToSigma;
    return sigma == 0.0 ? 0.001 : sigma;
}

// One channel's automatic screen transfer: a shadow point `shadowsClipping`
// sigmas from the median, no lower than zero, and a midtones balance of the transfer function at
// the median's distance above it, with `targetBackground` as the balance. An inverted image takes a
// highlight point the same number of sigmas on the other side, no higher than one, and the
// complement of the balance computed from the median's distance below it.
[[nodiscard]] MidtonesTransferParameters deriveChannelAutoStretch(const StretchChannelStatistics& statistics,
                                                                  double shadowsClipping, double targetBackground,
                                                                  bool inverted) noexcept
{
    MidtonesTransferParameters result;
    const double sigma = guardedSigma(statistics);
    if (!inverted)
    {
        result.shadows = (std::max)(0.0, statistics.median + shadowsClipping * sigma);
        result.highlights = 1.0;
        result.midtones = evaluateMidtonesUnchecked(statistics.median - result.shadows,
                                                    MidtonesTransferParameters{.midtones = targetBackground});
    }
    else
    {
        result.shadows = 0.0;
        result.highlights = (std::min)(1.0, statistics.median - shadowsClipping * sigma);
        result.midtones = 1.0 - evaluateMidtonesUnchecked(result.highlights - statistics.median,
                                                          MidtonesTransferParameters{.midtones = targetBackground});
    }
    return result;
}

[[nodiscard]] bool maskAccepts(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
    const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

[[nodiscard]] bool sampleAligned(const void* samples, std::uint32_t elementBytes) noexcept
{
    return reinterpret_cast<std::uintptr_t>(samples) % elementBytes == 0u;
}

[[nodiscard]] bool exactAlias(const void* source, const ImageLayout& sourceLayout, const void* destination,
                              const ImageLayout& destinationLayout) noexcept
{
    return source == destination && sourceLayout == destinationLayout;
}

[[nodiscard]] Status validateValidity(const KernelValidityView& validity, const ImageLayout& layout,
                                      const void* destination, const ImageLayout& destinationLayout)
{
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return stretchFailure(ErrorCode::InvalidArgument,
                                  "a stretch validity view supplies a mask for a rule that does not use one",
                                  kImageOperation);
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return stretchFailure(ErrorCode::InvalidArgument, "an explicit-mask stretch validity view supplies no mask",
                              kImageOperation);
    }
    if (validity.mask->width() != layout.extent().width() || validity.mask->height() != layout.extent().height() ||
        validity.maskPlane >= validity.mask->planeCount())
    {
        return stretchFailure(ErrorCode::IncompatibleArguments,
                              "a stretch validity mask does not cover the image and selected plane", kImageOperation);
    }
    if (regionsOverlap(validity.mask->data(), validity.mask->spannedByteCount(),
                       static_cast<const std::uint8_t*>(destination), destinationLayout.totalBytes()))
    {
        return stretchFailure(ErrorCode::InvalidArgument, "a stretch validity mask overlaps the writable destination",
                              kImageOperation);
    }
    return Status::success();
}

struct StretchColorPlans
{
    std::optional<ColorConversionPlan> toLab{};
    std::optional<ColorConversionPlan> fromLab{};
    std::optional<ColorConversionPlan> toHsv{};
    std::optional<ColorConversionPlan> fromHsv{};
};

[[nodiscard]] Result<StretchColorPlans> makeColorPlans(const StretchImageRequest& request, std::uint32_t channels)
{
    StretchColorPlans plans;
    if (request.stretch.function == StretchFunction::Linear || request.colorMode == StretchColorMode::Independent ||
        channels == 1u)
    {
        return plans;
    }
    if (channels != 3u && channels != 4u)
    {
        return stretchFailure(ErrorCode::IncompatibleArguments,
                              "a color stretch needs three RGB channels and optional alpha", kImageOperation);
    }
    const ChannelSchema expected = channels == 4u ? ChannelSchema::Rgba : ChannelSchema::Rgb;
    if (request.colorEncoding.schema != expected ||
        checkColorEncoding(request.colorEncoding) != ColorEncodingError::None)
    {
        return stretchFailure(ErrorCode::InvalidArgument,
                              "the stretch color encoding is invalid or disagrees with the image channel count",
                              kImageOperation);
    }
    ColorEncoding rgb = request.colorEncoding;
    rgb.schema = ChannelSchema::Rgb;
    if (request.colorMode == StretchColorMode::Lightness || request.colorMode == StretchColorMode::Saturation)
    {
        ColorEncoding lab;
        lab.schema = ChannelSchema::Lab;
        lab.whitePoint = rgb.whitePoint;
        lab.perceptual = PerceptualEncoding::UnitPacked;
        Result<ColorConversionPlan> forward = planColorConversion(rgb, lab);
        Result<ColorConversionPlan> reverse = planColorConversion(lab, rgb);
        if (!forward.hasValue())
        {
            return forward.status();
        }
        if (!reverse.hasValue())
        {
            return reverse.status();
        }
        plans.toLab = *forward;
        plans.fromLab = *reverse;
    }
    if (request.colorMode == StretchColorMode::Saturation)
    {
        ColorEncoding hsv = rgb;
        hsv.schema = ChannelSchema::Hsv;
        Result<ColorConversionPlan> forward = planColorConversion(rgb, hsv);
        Result<ColorConversionPlan> reverse = planColorConversion(hsv, rgb);
        if (!forward.hasValue())
        {
            return forward.status();
        }
        if (!reverse.hasValue())
        {
            return reverse.status();
        }
        plans.toHsv = *forward;
        plans.fromHsv = *reverse;
    }
    return plans;
}

// One RGB or RGBA pixel under a colour mode. The luminance modes scale the selected channels by the
// ratio of stretched to unstretched luminance, and where that would push the largest channel past
// one they blend toward the channels stretched independently, by the fraction that brings it back
// to one, so the channels keep their ratios wherever that does not clip. Alpha is always copied.
[[nodiscard]] Status stretchColorPixel(const StretchImageRequest& request, const StretchCoefficients& coefficients,
                                       const StretchColorPlans& plans, const std::array<double, 4u>& source,
                                       std::uint32_t channels, std::array<double, 4u>& output)
{
    output = source;
    if (request.colorMode == StretchColorMode::Lightness)
    {
        std::array<double, 3u> lab{};
        Status converted = convertColorPixel(*plans.toLab, std::span<const double>(source.data(), 3u), lab);
        if (converted.failed())
        {
            return converted;
        }
        lab[0] = evaluatePlanned(lab[0], request.stretch, coefficients, 0u);
        converted = convertColorPixel(*plans.fromLab, lab, std::span<double>(output.data(), 3u));
        if (converted.failed())
        {
            return converted;
        }
    }
    else if (request.colorMode == StretchColorMode::Saturation)
    {
        std::array<double, 3u> originalLab{};
        std::array<double, 3u> hsv{};
        std::array<double, 3u> adjustedRgb{};
        std::array<double, 3u> adjustedLab{};
        Status converted = convertColorPixel(*plans.toLab, std::span<const double>(source.data(), 3u), originalLab);
        if (converted.failed())
        {
            return converted;
        }
        converted = convertColorPixel(*plans.toHsv, std::span<const double>(source.data(), 3u), hsv);
        if (converted.failed())
        {
            return converted;
        }
        hsv[1] = evaluatePlanned(hsv[1], request.stretch, coefficients, 0u);
        converted = convertColorPixel(*plans.fromHsv, hsv, adjustedRgb);
        if (converted.failed())
        {
            return converted;
        }
        converted = convertColorPixel(*plans.toLab, adjustedRgb, adjustedLab);
        if (converted.failed())
        {
            return converted;
        }
        adjustedLab[0] = originalLab[0];
        converted = convertColorPixel(*plans.fromLab, adjustedLab, std::span<double>(output.data(), 3u));
        if (converted.failed())
        {
            return converted;
        }
    }
    else if (request.colorMode == StretchColorMode::Color)
    {
        std::array<double, 3u> coefficientsNormalized = request.luminanceCoefficients;
        for (double& coefficient : coefficientsNormalized)
        {
            coefficient = (std::max)(0.0, coefficient);
        }
        const double coefficientSum = coefficientsNormalized[0] + coefficientsNormalized[1] + coefficientsNormalized[2];
        if (coefficientSum <= 0.0)
        {
            coefficientsNormalized = {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
        }
        else
        {
            for (double& coefficient : coefficientsNormalized)
            {
                coefficient /= coefficientSum;
            }
        }
        const double luminance = coefficientsNormalized[0] * source[0] + coefficientsNormalized[1] * source[1] +
                                 coefficientsNormalized[2] * source[2];
        const double stretched = evaluatePlanned(luminance, request.stretch, coefficients, 0u);
        const double maximum = (std::max)({source[0], source[1], source[2]});
        if (luminance <= 1.0e-12)
        {
            const double neutral = maximum <= 1.0e-12 ? std::clamp(stretched, 0.0, 1.0) : 0.0;
            output[0] = output[1] = output[2] = neutral;
        }
        else
        {
            const double scale = stretched / luminance;
            const double adjustment = request.overflow == StretchOverflowPolicy::Rescale
                                          ? (std::max)(1.0, stretched * maximum / luminance)
                                          : 1.0;
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                output[channel] = std::clamp(source[channel] * scale / adjustment, 0.0, 1.0);
            }
        }
    }
    else
    {
        std::array<double, 3u> weights{};
        const std::uint32_t active = static_cast<std::uint32_t>(request.channelEnabled[0]) +
                                     static_cast<std::uint32_t>(request.channelEnabled[1]) +
                                     static_cast<std::uint32_t>(request.channelEnabled[2]);
        if (active == 0u)
        {
            return Status::success();
        }
        if (request.colorMode == StretchColorMode::HumanLuminance && active == 3u)
        {
            weights = {0.2126, 0.7152, 0.0722};
        }
        else
        {
            const double weight = 1.0 / active;
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                weights[channel] = request.channelEnabled[channel] ? weight : 0.0;
            }
        }
        const double luminance = weights[0] * source[0] + weights[1] * source[1] + weights[2] * source[2];
        const double stretchedLuminance = evaluatePlanned(luminance, request.stretch, coefficients, 0u);
        const double scale = luminance == 0.0 ? 0.0 : stretchedLuminance / luminance;
        std::array<double, 3u> luminanceStretched{};
        std::array<double, 3u> independentlyStretched{};
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            luminanceStretched[channel] = source[channel] * scale;
            if (request.channelEnabled[channel])
            {
                independentlyStretched[channel] =
                    evaluatePlanned(source[channel], request.stretch, coefficients, channel);
            }
        }
        const double scaledMaximum = (std::max)({luminanceStretched[0], luminanceStretched[1], luminanceStretched[2]});
        const double independentMaximum =
            (std::max)({independentlyStretched[0], independentlyStretched[1], independentlyStretched[2]});
        const double difference = scaledMaximum - independentMaximum;
        const double blend = independentMaximum + difference > 1.0 && difference != 0.0
                                 ? (std::min)(1.0, (1.0 - independentMaximum) / difference)
                                 : 1.0;
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            if (request.channelEnabled[channel])
            {
                output[channel] = (1.0 - blend) * independentlyStretched[channel] + blend * luminanceStretched[channel];
            }
        }
    }
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        output[channel] = std::clamp(output[channel], 0.0, 1.0);
    }
    if (channels == 4u)
    {
        output[3] = source[3];
    }
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] const SampleT* channelRow(const void* samples, const ImageLayout& layout, std::uint32_t channel,
                                        std::uint32_t y) noexcept
{
    return reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(samples) +
                                            *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
}

template <typename SampleT>
[[nodiscard]] SampleT* channelRow(void* samples, const ImageLayout& layout, std::uint32_t channel,
                                  std::uint32_t y) noexcept
{
    return reinterpret_cast<SampleT*>(static_cast<std::uint8_t*>(samples) +
                                      *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
}

template <ResidentSample SampleT, bool Inverse>
[[nodiscard]] Result<StretchImageResult>
stretchHistogramMono(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
                     void* destinationSamples, const ImageLayout& destinationLayout,
                     const GeneralizedStretchParameters& parameters, const StretchCoefficients& coefficients)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 16u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        const auto c = coefficients.histogramInverse;
        const std::uint32_t rowWidth = width;
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto* source = channelRow<SampleT>(sourceSamples, sourceLayout, 0u, static_cast<std::uint32_t>(y));
            auto* destination =
                channelRow<SampleT>(destinationSamples, destinationLayout, 0u, static_cast<std::uint32_t>(y));
            // Validate a cache-resident row before writing it. The arithmetic loop has no error
            // exit, and exact aliasing is safe because every iteration reads only its own sample.
            bool invalid = false;
            for (std::uint32_t x = 0; x < rowWidth; ++x)
            {
                invalid |= !std::isfinite(source[x]);
            }
            if (invalid)
            {
                return stretchFailure(ErrorCode::InvalidData, "a selected stretch sample is not finite",
                                      kImageOperation);
            }
            if constexpr (!Inverse)
            {
                // Keep the forward authority's arithmetic and rounding, without the per-pixel
                // channel arrays and family dispatch of the general image walk.
                for (std::uint32_t x = 0; x < rowWidth; ++x)
                {
                    const double value =
                        evaluateForwardUnchecked(source[x], parameters, StretchFunction::Histogram, coefficients);
                    if (!std::isfinite(value))
                    {
                        return stretchFailure(ErrorCode::InvalidData, "a stretch produced a non-finite sample",
                                              kImageOperation);
                    }
                    destination[x] = static_cast<SampleT>(value);
                }
            }
            else if (c.identity)
            {
                for (std::uint32_t x = 0; x < rowWidth; ++x)
                {
                    destination[x] = (std::max)(SampleT{0}, (std::min)(source[x], SampleT{1}));
                }
            }
            else
            {
                for (std::uint32_t x = 0; x < rowWidth; ++x)
                {
                    const double input = std::clamp(static_cast<double>(source[x]), 0.0, 1.0);
                    const bool lower = input <= c.split;
                    const double value = lower ? input : 1.0 - input;
                    const double boundary = lower ? c.lowBoundary : c.highBoundary;
                    const double linear = lower ? c.lowLinear : c.highLinear;
                    const double origin = lower ? c.low : c.center;
                    const double numerator = lower ? c.lowNumerator : c.extent;
                    const double denominator = lower ? c.lowDenominator : c.highDenominator;
                    const double delta = (std::max)(0.0, value - boundary);
                    const double distance = lower ? delta : input - c.split;
                    const double curved = origin + distance * numerator / (denominator + delta * c.curvature);
                    const double tail = lower ? value * linear : 1.0 - value * linear;
                    const double mapped = value <= boundary ? tail : curved;
                    const double result = input == 0.0 ? 0.0 : (input == 1.0 ? 1.0 : mapped);
                    destination[x] = static_cast<SampleT>(std::clamp(result, 0.0, 1.0));
                }
            }
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    StretchImageResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total;
    return result;
}

// Whether every sample of a row is finite, read from its exponent bits, which a compiler
// vectorizes where it would call a library classification per sample.
template <ResidentSample SampleT> [[nodiscard]] bool rowIsFinite(const SampleT* row, std::uint32_t width) noexcept
{
    using Bits = std::conditional_t<sizeof(SampleT) == 4u, std::uint32_t, std::uint64_t>;
    constexpr Bits kExponent = [] {
        if constexpr (sizeof(SampleT) == 4u)
        {
            return Bits{0x7f800000u};
        }
        else
        {
            return Bits{0x7ff0000000000000u};
        }
    }();
    Bits nonFinite = 0u;
    for (std::uint32_t x = 0; x < width; ++x)
    {
        nonFinite |= (std::bit_cast<Bits>(row[x]) & kExponent) == kExponent ? Bits{1u} : Bits{0u};
    }
    return nonFinite == 0u;
}

// A one-channel all-valid asinh in either direction: each row is checked while it is cache-resident
// and then written through the compiled Float32 row where the execution's table has one, or through
// the scalar authority otherwise - which is always the case for Float64 and for the reference
// backend.
template <ResidentSample SampleT, bool Inverse>
[[nodiscard]] Result<StretchImageResult>
stretchAsinhMono(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
                 void* destinationSamples, const ImageLayout& destinationLayout, const StretchCoefficients::Asinh& plan)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    AsinhFloat32RunFn compiled = nullptr;
    if constexpr (std::is_same_v<SampleT, float>)
    {
        if (!plan.identity)
        {
            compiled = Inverse ? asinhInverseFloat32RunFor(execution) : asinhFloat32RunFor(execution);
        }
    }
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 48u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto* source = channelRow<SampleT>(sourceSamples, sourceLayout, 0u, static_cast<std::uint32_t>(y));
            auto* destination =
                channelRow<SampleT>(destinationSamples, destinationLayout, 0u, static_cast<std::uint32_t>(y));
            // Exact aliasing is safe because each sample is read before its own write.
            if (!rowIsFinite(source, width))
            {
                return stretchFailure(ErrorCode::InvalidData, "a selected stretch sample is not finite",
                                      kImageOperation);
            }
            if (plan.identity)
            {
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    destination[x] = (std::max)(SampleT{0}, (std::min)(source[x], SampleT{1}));
                }
                continue;
            }
            if constexpr (std::is_same_v<SampleT, float>)
            {
                if (compiled != nullptr)
                {
                    compiled(source, destination, width, plan.coefficients);
                    continue;
                }
            }
            for (std::uint32_t x = 0; x < width; ++x)
            {
                destination[x] = static_cast<SampleT>(Inverse ? evaluateAsinhInverse(source[x], plan)
                                                              : evaluateAsinh(source[x], plan));
            }
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    StretchImageResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total;
    return result;
}

// A one-channel all-valid generalized-hyperbolic curve in either direction: each row is checked
// while it is cache-resident and then written through the compiled Float32 row where the
// execution's table has one, or through the scalar authority otherwise - which is always the case
// for Float64 and for the reference backend.
template <ResidentSample SampleT, bool Inverse>
[[nodiscard]] Result<StretchImageResult>
stretchGeneralizedHyperbolicMono(const ExecutionContext& execution, const void* sourceSamples,
                                 const ImageLayout& sourceLayout, void* destinationSamples,
                                 const ImageLayout& destinationLayout,
                                 const StretchCoefficients::GeneralizedHyperbolic& plan)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    GeneralizedHyperbolicFloat32RunFn compiled = nullptr;
    if constexpr (std::is_same_v<SampleT, float>)
    {
        if (!plan.identity)
        {
            compiled = Inverse ? generalizedHyperbolicInverseFloat32RunFor(execution)
                               : generalizedHyperbolicFloat32RunFor(execution);
        }
    }
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 64u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto* source = channelRow<SampleT>(sourceSamples, sourceLayout, 0u, static_cast<std::uint32_t>(y));
            auto* destination =
                channelRow<SampleT>(destinationSamples, destinationLayout, 0u, static_cast<std::uint32_t>(y));
            // Exact aliasing is safe because each sample is read before its own write.
            bool invalid = false;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                invalid |= !std::isfinite(source[x]);
            }
            if (invalid)
            {
                return stretchFailure(ErrorCode::InvalidData, "a selected stretch sample is not finite",
                                      kImageOperation);
            }
            if (plan.identity)
            {
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    destination[x] = (std::max)(SampleT{0}, (std::min)(source[x], SampleT{1}));
                }
                continue;
            }
            if constexpr (std::is_same_v<SampleT, float>)
            {
                if (compiled != nullptr)
                {
                    compiled(source, destination, width, plan.coefficients);
                    continue;
                }
            }
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const double value = Inverse ? invertGeneralizedHyperbolic(source[x], plan)
                                             : evaluateGeneralizedHyperbolic(source[x], plan);
                destination[x] = static_cast<SampleT>(value);
            }
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    StretchImageResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total;
    return result;
}

// A destination of at least this many bytes is written with streaming stores. A pass that does as
// little per sample as the midtones transfer is limited by memory traffic, and a streaming store
// writes a line without first reading it; a destination this large, with its source, does not stay
// cached for its next reader anyway. Below it the ordinary stores, which leave the output cached, are
// the faster ones.

// An all-valid screen transfer in independent mode on one channel or on planes of one channel
// each: every plane of a row is checked, then each selected plane is written through its channel's
// planned transfer - the compiled Float32 row where the execution's table has one, the scalar
// authority otherwise, which is always the case for Float64 and for the reference backend - and
// every other plane is copied clamped, as the general walk copies it. The first row of each range
// is checked on its own; every later one is checked while the row before it is written - by the
// compiled row, which reads it beside the divisions, or after the plane is written otherwise - so no
// row is written before all its planes are known to be finite.
template <ResidentSample SampleT>
[[nodiscard]] Result<StretchImageResult>
stretchMidtonesTransferPlanes(const ExecutionContext& execution, const void* sourceSamples,
                              const ImageLayout& sourceLayout, void* destinationSamples,
                              const ImageLayout& destinationLayout, const StretchImageRequest& request)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    std::array<MidtonesTransferCoefficients, 4u> plans{};
    std::array<bool, 4u> selected{};
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        plans[channel] = planMidtonesTransfer(request.stretch.screenTransfer[(std::min)(channel, 2u)]);
        selected[channel] = channels == 1u || request.channelEnabled[channel];
    }
    MidtonesTransferFloat32RunFn compiled = nullptr;
    if constexpr (std::is_same_v<SampleT, float>)
    {
        compiled = midtonesTransferFloat32RunFor(execution);
    }
    const bool stream = destinationLayout.totalBytes() >= kMidtonesTransferStreamingBytes &&
                        !exactAlias(sourceSamples, sourceLayout, destinationSamples, destinationLayout);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 8u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        // Whether each plane of the row about to be written is finite.
        std::array<bool, 4u> finite{};
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            finite[channel] = rowIsFinite(
                channelRow<SampleT>(sourceSamples, sourceLayout, channel, static_cast<std::uint32_t>(range.begin)),
                width);
        }
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                if (!finite[channel])
                {
                    return stretchFailure(ErrorCode::InvalidData, "a selected stretch sample is not finite",
                                          kImageOperation);
                }
            }
            // Exact aliasing is safe because each sample is read before its own write, and the next
            // row is read before anything writes it.
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const auto* source = channelRow<SampleT>(sourceSamples, sourceLayout, channel, y);
                auto* destination = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y);
                const SampleT* upcoming =
                    y64 + 1u < range.end ? channelRow<SampleT>(sourceSamples, sourceLayout, channel, y + 1u) : nullptr;
                if constexpr (std::is_same_v<SampleT, float>)
                {
                    if (selected[channel] && compiled != nullptr)
                    {
                        finite[channel] = compiled(source, destination, width, plans[channel], stream, upcoming);
                        continue;
                    }
                }
                if (selected[channel])
                {
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        destination[x] = static_cast<SampleT>(evaluateMidtonesPlanned(
                            std::clamp(static_cast<double>(source[x]), 0.0, 1.0), plans[channel]));
                    }
                }
                else
                {
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        destination[x] = std::clamp(source[x], SampleT{0}, SampleT{1});
                    }
                }
                finite[channel] = upcoming == nullptr || rowIsFinite(upcoming, width);
            }
        }
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    StretchImageResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = result.run.selection.total;
    result.run.outputsWritten = result.run.selection.total * channels;
    return result;
}

template <ResidentSample SampleT>
[[nodiscard]] Result<StretchImageResult>
stretchTyped(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
             void* destinationSamples, const ImageLayout& destinationLayout, const StretchImageRequest& request,
             const KernelValidityView& validity, const StretchCoefficients& coefficients,
             const StretchColorPlans& colorPlans)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    if (request.stretch.function == StretchFunction::ScreenTransfer &&
        validity.descriptor.rule() == ValidityRule::AllValid &&
        (channels == 1u || (request.colorMode == StretchColorMode::Independent &&
                            sourceLayout.samplesPerPixel() == 1u && destinationLayout.samplesPerPixel() == 1u)))
    {
        return stretchMidtonesTransferPlanes<SampleT>(execution, sourceSamples, sourceLayout, destinationSamples,
                                                      destinationLayout, request);
    }
    if (channels == 1u && validity.descriptor.rule() == ValidityRule::AllValid)
    {
        if (request.stretch.function == StretchFunction::InverseHistogram && !coefficients.histogramInverse.scaledTails)
        {
            return stretchHistogramMono<SampleT, true>(execution, sourceSamples, sourceLayout, destinationSamples,
                                                       destinationLayout, request.stretch, coefficients);
        }
        if (request.stretch.function == StretchFunction::Histogram)
        {
            return stretchHistogramMono<SampleT, false>(execution, sourceSamples, sourceLayout, destinationSamples,
                                                        destinationLayout, request.stretch, coefficients);
        }
        if (request.stretch.function == StretchFunction::Asinh)
        {
            return stretchAsinhMono<SampleT, false>(execution, sourceSamples, sourceLayout, destinationSamples,
                                                    destinationLayout, coefficients.asinh);
        }
        if (request.stretch.function == StretchFunction::InverseAsinh)
        {
            return stretchAsinhMono<SampleT, true>(execution, sourceSamples, sourceLayout, destinationSamples,
                                                   destinationLayout, coefficients.asinh);
        }
        if (request.stretch.function == StretchFunction::GeneralizedHyperbolic)
        {
            return stretchGeneralizedHyperbolicMono<SampleT, false>(execution, sourceSamples, sourceLayout,
                                                                    destinationSamples, destinationLayout,
                                                                    coefficients.generalizedHyperbolic);
        }
        if (request.stretch.function == StretchFunction::InverseGeneralizedHyperbolic)
        {
            return stretchGeneralizedHyperbolicMono<SampleT, true>(execution, sourceSamples, sourceLayout,
                                                                   destinationSamples, destinationLayout,
                                                                   coefficients.generalizedHyperbolic);
        }
    }
    const bool independent = request.stretch.function == StretchFunction::Linear ||
                             request.colorMode == StretchColorMode::Independent || channels == 1u;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * (independent ? 16u : 72u)};
    std::atomic<std::uint64_t> validPixels{0u};
    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::array<const SampleT*, 4u> sourceRows{};
        std::array<SampleT*, 4u> destinationRows{};
        std::uint64_t localValid = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                sourceRows[channel] = channelRow<SampleT>(sourceSamples, sourceLayout, channel, y);
                destinationRows[channel] = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y);
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                bool accepted = maskAccepts(validity, x, y);
                std::array<double, 4u> source{};
                std::array<double, 4u> output{};
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const SampleT value =
                        sourceRows[channel][static_cast<std::size_t>(x) * sourceLayout.samplesPerPixel() +
                                            channel % sourceLayout.samplesPerPixel()];
                    source[channel] = static_cast<double>(value);
                    if (!sampleIsValidByValue(validity.descriptor, value))
                    {
                        accepted = false;
                    }
                }
                if (!accepted)
                {
                    continue;
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    if (!std::isfinite(source[channel]))
                    {
                        return stretchFailure(ErrorCode::InvalidData, "a selected stretch sample is not finite",
                                              kImageOperation);
                    }
                    source[channel] = std::clamp(source[channel], 0.0, 1.0);
                }
                output = source;
                if (independent)
                {
                    for (std::uint32_t channel = 0u; channel < channels; ++channel)
                    {
                        const bool enabled = channels == 1u || request.channelEnabled[channel];
                        if (enabled)
                        {
                            output[channel] = evaluatePlanned(source[channel], request.stretch, coefficients, channel);
                        }
                    }
                }
                else
                {
                    const Status colored =
                        stretchColorPixel(request, coefficients, colorPlans, source, channels, output);
                    if (colored.failed())
                    {
                        return colored;
                    }
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    if (!std::isfinite(output[channel]))
                    {
                        return stretchFailure(ErrorCode::InvalidData, "a stretch produced a non-finite sample",
                                              kImageOperation);
                    }
                    destinationRows[channel][static_cast<std::size_t>(x) * destinationLayout.samplesPerPixel() +
                                             channel % destinationLayout.samplesPerPixel()] =
                        static_cast<SampleT>(output[channel]);
                }
                ++localValid;
            }
        }
        validPixels.fetch_add(localValid, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    StretchImageResult result;
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = validPixels.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid * channels;
    return result;
}

} // namespace

MidtonesTransferCoefficients planScreenTransferChannel(const MidtonesTransferParameters& parameters) noexcept
{
    return planMidtonesTransfer(parameters);
}

void transferScreenTransferRow(MidtonesTransferFloat32RunFn compiled, const MidtonesTransferCoefficients& plan,
                               const float* source, float* destination, std::uint32_t width, bool stream) noexcept
{
    if (compiled != nullptr)
    {
        // The row's answer about a next row is the stretch's check before writing it, which has no next row here.
        static_cast<void>(compiled(source, destination, width, plan, stream && source != destination, nullptr));
        return;
    }
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        destination[x] =
            static_cast<float>(evaluateMidtonesPlanned(std::clamp(static_cast<double>(source[x]), 0.0, 1.0), plan));
    }
}

Result<double> evaluateMidtonesTransfer(double value, const MidtonesTransferParameters& parameters)
{
    const Status valid = validateMidtones(parameters, kMtfOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(value))
    {
        return stretchFailure(ErrorCode::InvalidData, "a midtones input is not finite", kMtfOperation);
    }
    return evaluateMidtonesUnchecked(value, parameters);
}

Result<double> evaluateInverseMidtonesTransfer(double value, const MidtonesTransferParameters& parameters)
{
    const Status valid = validateMidtones(parameters, kMtfOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(value))
    {
        return stretchFailure(ErrorCode::InvalidData, "an inverse-midtones input is not finite", kMtfOperation);
    }
    const double target = std::clamp(value, 0.0, 1.0);
    if (target <= 0.0 || parameters.highlights == parameters.shadows)
    {
        return parameters.shadows;
    }
    if (target >= 1.0)
    {
        return parameters.highlights;
    }
    double low = parameters.shadows;
    double high = parameters.highlights;
    for (std::uint32_t iteration = 0u; iteration < 64u; ++iteration)
    {
        const double middle = (low + high) * 0.5;
        if (evaluateMidtonesUnchecked(middle, parameters) < target)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return (low + high) * 0.5;
}

Result<MidtonesTransferParameters> deriveLinkedAutoStretch(std::span<const StretchChannelStatistics> statistics,
                                                           double shadowsClipping, double targetBackground)
{
    const Status valid = validateStatistics(statistics, shadowsClipping, targetBackground, kAutoStretchOperation);
    if (valid.failed())
    {
        return valid;
    }
    const bool inverted = usesInvertedAutoStretch(statistics);
    double endpoint = 0.0;
    double median = 0.0;
    for (const StretchChannelStatistics& channel : statistics)
    {
        median += channel.median;
        endpoint += inverted ? channel.median - shadowsClipping * guardedSigma(channel)
                             : channel.median + shadowsClipping * guardedSigma(channel);
    }
    endpoint /= static_cast<double>(statistics.size());
    median /= static_cast<double>(statistics.size());
    MidtonesTransferParameters result;
    if (!inverted)
    {
        result.shadows = (std::max)(0.0, endpoint);
        result.highlights = 1.0;
        result.midtones = evaluateMidtonesUnchecked(median - result.shadows,
                                                    MidtonesTransferParameters{.midtones = targetBackground});
    }
    else
    {
        result.shadows = 0.0;
        result.highlights = (std::min)(1.0, endpoint);
        result.midtones = 1.0 - evaluateMidtonesUnchecked(result.highlights - median,
                                                          MidtonesTransferParameters{.midtones = targetBackground});
    }
    return result;
}

Status deriveUnlinkedAutoStretch(std::span<const StretchChannelStatistics> statistics, double shadowsClipping,
                                 double targetBackground, std::span<MidtonesTransferParameters> destination)
{
    const Status valid = validateStatistics(statistics, shadowsClipping, targetBackground, kAutoStretchOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (statistics.size() != destination.size())
    {
        return stretchFailure(ErrorCode::InvalidArgument,
                              "unlinked automatic stretch input and output spans have different lengths",
                              kAutoStretchOperation);
    }
    const bool inverted = usesInvertedAutoStretch(statistics);
    for (std::size_t channel = 0u; channel < statistics.size(); ++channel)
    {
        destination[channel] =
            deriveChannelAutoStretch(statistics[channel], shadowsClipping, targetBackground, inverted);
    }
    return Status::success();
}

Result<double> evaluateGeneralizedStretch(double value, const GeneralizedStretchParameters& parameters,
                                          std::uint32_t channel)
{
    const Status valid = validateStretchParameters(parameters, kScalarOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(value))
    {
        return stretchFailure(ErrorCode::InvalidData, "a scalar stretch input is not finite", kScalarOperation);
    }
    const Result<StretchCoefficients> coefficients =
        makeCoefficients(parameters, forwardOf(parameters.function), kScalarOperation);
    if (!coefficients.hasValue())
    {
        return coefficients.status();
    }
    const double result = evaluatePlanned(value, parameters, *coefficients, channel);
    if (!std::isfinite(result))
    {
        return stretchFailure(ErrorCode::InvalidData, "a scalar stretch produced a non-finite value", kScalarOperation);
    }
    return result;
}

Result<StretchImageResult> stretchImage(const ExecutionContext& execution, SampleType sampleType,
                                        const void* sourceSamples, const ImageLayout& sourceLayout,
                                        void* destinationSamples, const ImageLayout& destinationLayout,
                                        const StretchImageRequest& request, const KernelValidityView& validity)
{
    if (!execution.valid())
    {
        return stretchFailure(ErrorCode::NotInitialized, "a stretch was given an empty execution context",
                              kImageOperation);
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> backend = selectBackend(execution);
    if (!backend.hasValue())
    {
        return backend.status();
    }
    const Status parametersValid = validateStretchParameters(request.stretch, kImageOperation);
    if (parametersValid.failed())
    {
        return parametersValid;
    }
    if (!isStretchColorMode(request.colorMode) || !isStretchOverflowPolicy(request.overflow) ||
        !std::all_of(request.luminanceCoefficients.begin(), request.luminanceCoefficients.end(),
                     [](double coefficient) { return std::isfinite(coefficient); }))
    {
        return stretchFailure(ErrorCode::InvalidArgument, "a stretch image policy is malformed", kImageOperation);
    }
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return stretchFailure(ErrorCode::IncompatibleArguments,
                              "a normalized stretch runs on a Float32 or Float64 working image", kImageOperation);
    }
    if (sourceSamples == nullptr || destinationSamples == nullptr)
    {
        return stretchFailure(ErrorCode::InvalidArgument, "a stretch image has a null sample pointer", kImageOperation);
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (sourceLayout.elementSizeBytes() != elementBytes || destinationLayout.elementSizeBytes() != elementBytes ||
        !sampleAligned(sourceSamples, elementBytes) || !sampleAligned(destinationSamples, elementBytes))
    {
        return stretchFailure(ErrorCode::InvalidArgument,
                              "a stretch layout or pointer does not describe its resident sample type",
                              kImageOperation);
    }
    if (!(sourceLayout.extent() == destinationLayout.extent()))
    {
        return stretchFailure(ErrorCode::IncompatibleArguments,
                              "the stretch source and destination do not have one extent", kImageOperation);
    }
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    if (channels > 4u)
    {
        return stretchFailure(ErrorCode::IncompatibleArguments, "this stretch supports at most four channels",
                              kImageOperation);
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(sourceSamples), sourceLayout.totalBytes(),
                       static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()) &&
        !exactAlias(sourceSamples, sourceLayout, destinationSamples, destinationLayout))
    {
        return stretchFailure(ErrorCode::IncompatibleArguments, "a stretch source partially overlaps its destination",
                              kImageOperation);
    }
    const Status validityStatus = validateValidity(validity, sourceLayout, destinationSamples, destinationLayout);
    if (validityStatus.failed())
    {
        return validityStatus;
    }
    Result<StretchCoefficients> coefficients =
        makeCoefficients(request.stretch, forwardOf(request.stretch.function), kImageOperation);
    if (!coefficients.hasValue())
    {
        return coefficients.status();
    }
    Result<StretchColorPlans> colorPlans = makeColorPlans(request, channels);
    if (!colorPlans.hasValue())
    {
        return colorPlans.status();
    }
    if (sampleType == SampleType::Float32)
    {
        return stretchTyped<float>(execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout,
                                   request, validity, *coefficients, *colorPlans);
    }
    return stretchTyped<double>(execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout, request,
                                validity, *coefficients, *colorPlans);
}

Status remapStretchHistogram(const ExecutionContext& execution, std::span<const std::uint64_t> source,
                             std::span<std::uint64_t> destination, const GeneralizedStretchParameters& parameters)
{
    if (!execution.valid())
    {
        return stretchFailure(ErrorCode::NotInitialized, "a histogram stretch has no execution context",
                              kHistogramOperation);
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> backend = selectBackend(execution);
    if (!backend.hasValue())
    {
        return backend.status();
    }
    const Status valid = validateStretchParameters(parameters, kHistogramOperation);
    if (valid.failed())
    {
        return valid;
    }
    if (source.empty() || source.size() != destination.size() ||
        regionsOverlap(reinterpret_cast<const std::uint8_t*>(source.data()), source.size_bytes(),
                       reinterpret_cast<const std::uint8_t*>(destination.data()), destination.size_bytes()))
    {
        return stretchFailure(ErrorCode::InvalidArgument,
                              "histogram stretch spans must be nonempty, equal in length and non-overlapping",
                              kHistogramOperation);
    }
    const Result<StretchCoefficients> coefficients =
        makeCoefficients(parameters, forwardOf(parameters.function), kHistogramOperation);
    if (!coefficients.hasValue())
    {
        return coefficients.status();
    }
    std::uint64_t totalCount = 0u;
    for (const std::uint64_t count : source)
    {
        if (count > (std::numeric_limits<std::uint64_t>::max)() - totalCount)
        {
            return stretchFailure(ErrorCode::InvalidData, "histogram stretch output count overflowed uint64",
                                  kHistogramOperation);
        }
        totalCount += count;
    }
    std::fill(destination.begin(), destination.end(), 0u);
    const double denominator = source.size() == 1u ? 1.0 : static_cast<double>(source.size() - 1u);
    for (std::size_t inputBin = 0u; inputBin < source.size(); ++inputBin)
    {
        if ((inputBin & 4095u) == 0u)
        {
            const Status checkpoint = execution.checkCancellation();
            if (checkpoint.failed())
            {
                return checkpoint;
            }
        }
        const double value = static_cast<double>(inputBin) / denominator;
        const double transformed = evaluatePlanned(value, parameters, *coefficients, 0u);
        const std::size_t outputBin =
            static_cast<std::size_t>(std::llround(std::clamp(transformed, 0.0, 1.0) * denominator));
        destination[outputBin] += source[inputBin];
    }
    return Status::success();
}

} // namespace an
