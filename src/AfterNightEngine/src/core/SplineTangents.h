// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the three published interpolation rules, as end-of-segment derivatives.
//
// Two domains join points with a cubic: the tone curve a caller edits, and the spectral response a
// synthetic magnitude integrates. They want different things out of the result - one evaluates it,
// the other also integrates it - but the rule that decides the derivatives is the same rule, and a
// second copy of it is a second answer waiting to be found. It is written once here, in the layer
// both may include.
//
// The rules are Akima's (H. Akima, J. ACM 17, 1970), Steffen's monotone one (M. Steffen, Astron.
// Astrophys. 239, 1990) and the natural cubic, each following the GNU Scientific Library's (GSL's)
// formulation of it, so an interpolant agrees with that library's own.
//
// **A rule is stated per segment, not per node.** A natural cubic and Steffen's method are
// continuously differentiable, so the derivative at a node is one number; Akima's, as that library
// implements it, is not - where a segment's weight denominator vanishes that library makes the
// whole segment a straight line, and the derivative it leaves at the shared node need not be the
// next segment's. Returning both ends of each segment is what lets this file reproduce that
// library's answer rather than a tidier one.
//
// Nothing here allocates, reports or fails: a caller has already established that the abscissae are
// finite and strictly increasing and that there are enough of them for the rule it asked for.

#pragma once

#include <cmath>
#include <cstddef>
#include <span>

namespace an
{

/// The fewest points a natural cubic spline is defined over.
inline constexpr std::size_t kNaturalCubicMinimumPoints = 3u;

/// The fewest points Akima's rule is defined over: two interior slopes on each side of a segment.
inline constexpr std::size_t kAkimaMinimumPoints = 5u;

/// The fewest points Steffen's rule is defined over.
inline constexpr std::size_t kSteffenMinimumPoints = 3u;

/// The derivatives a cubic segment meets its two end points with.
struct SegmentTangents
{
    /// The derivative at the segment's left end point.
    double left = 0.0;

    /// The derivative at the segment's right end point.
    double right = 0.0;

    /// Whether Akima's weight denominator vanished, so the segment is this rule's straight line.
    ///
    /// `left` and `right` are then both the segment's own secant, which is the same cubic. A caller
    /// holding the segment in power form reads this instead, because GSL writes the two higher
    /// coefficients as literal zeroes where an arithmetic derivation of them would not be.
    bool straight = false;
};

/// How many doubles `naturalCubicTangents` needs beside its output.
///
/// @param pointCount how many points.
/// @return the scratch length: six working vectors of one double per point.
[[nodiscard]] constexpr std::size_t naturalCubicScratchDoubles(const std::size_t pointCount) noexcept
{
    return 6u * pointCount;
}

/// How many doubles `akimaFillExtendedSlopes` writes.
///
/// @param pointCount how many points.
/// @return the slope array's length: one per segment plus two extrapolated at each end.
[[nodiscard]] constexpr std::size_t akimaSlopeDoubles(const std::size_t pointCount) noexcept
{
    return pointCount + 4u;
}

/// The secant slope of every segment, plus the two the rule extrapolates past each end.
///
/// `slopes[k + 2]` is segment `k`'s secant, so the two before the first point and the two after the
/// last address naturally.
///
/// @param x the abscissae, strictly increasing, at least `kAkimaMinimumPoints` of them.
/// @param y the ordinates, as long as `x`.
/// @param slopes where they go; at least `akimaSlopeDoubles(x.size())` long.
inline void akimaFillExtendedSlopes(const std::span<const double> x, const std::span<const double> y,
                                    const std::span<double> slopes) noexcept
{
    const std::size_t n = x.size();
    for (std::size_t i = 0u; i + 1u < n; ++i)
    {
        slopes[i + 2u] = (y[i + 1u] - y[i]) / (x[i + 1u] - x[i]);
    }
    slopes[0] = 3.0 * slopes[2] - 2.0 * slopes[3];
    slopes[1] = 2.0 * slopes[2] - slopes[3];
    slopes[n + 1u] = 2.0 * slopes[n] - slopes[n - 1u];
    slopes[n + 2u] = 3.0 * slopes[n] - 2.0 * slopes[n - 1u];
}

/// The derivatives Akima's rule gives one segment's two end points.
///
/// The weights are the neighbouring slope differences, exactly as GSL forms them. Where a weight
/// denominator vanishes GSL makes the segment a straight line at its own secant, which is the
/// `left == right == secant` answer here; the derivative the next segment starts with is then not
/// this one's, which is the asymmetry this return type exists for.
///
/// @param slopes the array `akimaFillExtendedSlopes` wrote.
/// @param segment which segment, below one less than the number of points.
/// @return the derivative at each of its ends.
[[nodiscard]] inline SegmentTangents akimaSegmentTangents(const std::span<const double> slopes,
                                                          const std::size_t segment) noexcept
{
    const std::size_t i = segment;
    const double weight = std::abs(slopes[i + 3u] - slopes[i + 2u]) + std::abs(slopes[i + 1u] - slopes[i]);
    if (weight == 0.0)
    {
        return SegmentTangents{slopes[i + 2u], slopes[i + 2u], true};
    }
    const double nextWeight = std::abs(slopes[i + 4u] - slopes[i + 3u]) + std::abs(slopes[i + 2u] - slopes[i + 1u]);
    const double alpha = std::abs(slopes[i + 1u] - slopes[i]) / weight;
    double right = slopes[i + 2u];
    if (nextWeight != 0.0)
    {
        const double nextAlpha = std::abs(slopes[i + 2u] - slopes[i + 1u]) / nextWeight;
        right = (1.0 - nextAlpha) * slopes[i + 2u] + nextAlpha * slopes[i + 3u];
    }
    return SegmentTangents{(1.0 - alpha) * slopes[i + 1u] + alpha * slopes[i + 2u], right, false};
}

/// Steffen's sign function, which answers +1 for zero of either sign.
///
/// GSL spells it this way rather than through `copysign`, whose answer for a negative zero is -1,
/// and the two rules part on a segment whose secant is -0.0.
///
/// @param value the value whose sign decides the answer.
/// @return +1.0 or -1.0.
[[nodiscard]] inline double steffenSign(const double value) noexcept
{
    return value < 0.0 ? -1.0 : 1.0;
}

/// The derivative Steffen's rule gives each node.
///
/// A parabola through each node and its two neighbours proposes a derivative; the rule then limits
/// it to what keeps the segment monotone, which is what makes an extremum only ever land on a data
/// point. The two end nodes take their own segment's secant, which is the paper's "simplest
/// possibility" and GSL's choice.
///
/// @param x the abscissae, strictly increasing, at least `kSteffenMinimumPoints` of them.
/// @param y the ordinates, as long as `x`.
/// @param tangents where the derivatives go, as long as `x`.
inline void steffenTangents(const std::span<const double> x, const std::span<const double> y,
                            const std::span<double> tangents) noexcept
{
    const std::size_t n = x.size();
    tangents[0] = (y[1] - y[0]) / (x[1] - x[0]);
    for (std::size_t i = 1u; i + 1u < n; ++i)
    {
        const double width = x[i + 1u] - x[i];
        const double previousWidth = x[i] - x[i - 1u];
        const double secant = (y[i + 1u] - y[i]) / width;
        const double previousSecant = (y[i] - y[i - 1u]) / previousWidth;
        const double parabolic = (previousSecant * width + secant * previousWidth) / (previousWidth + width);
        const double limit =
            (std::min)(std::abs(previousSecant), (std::min)(std::abs(secant), 0.5 * std::abs(parabolic)));
        tangents[i] = (steffenSign(previousSecant) + steffenSign(secant)) * limit;
    }
    tangents[n - 1u] = (y[n - 1u] - y[n - 2u]) / (x[n - 1u] - x[n - 2u]);
}

/// The derivative a natural cubic spline gives each node.
///
/// The spline is the twice-differentiable one whose second derivative vanishes at both ends. Its
/// second-derivative coefficients come from the symmetric tridiagonal system over the interior
/// nodes, solved by the same `L.D.L^t` factorization GSL uses, and each node's first derivative then
/// follows from the segment it starts.
///
/// @param x the abscissae, strictly increasing, at least `kNaturalCubicMinimumPoints` of them.
/// @param y the ordinates, as long as `x`.
/// @param tangents where the derivatives go, as long as `x`.
/// @param scratch at least `naturalCubicScratchDoubles(x.size())` doubles of working space.
inline void naturalCubicTangents(const std::span<const double> x, const std::span<const double> y,
                                 const std::span<double> tangents, const std::span<double> scratch) noexcept
{
    const std::size_t n = x.size();
    const std::size_t systemSize = n - 2u;
    double* const c = scratch.data();
    double* const g = c + n;
    double* const diagonal = g + n;
    double* const offDiagonal = diagonal + n;
    double* const alpha = offDiagonal + n;
    double* const gamma = alpha + n;

    c[0] = 0.0;
    c[n - 1u] = 0.0;
    for (std::size_t i = 0u; i < systemSize; ++i)
    {
        const double width = x[i + 1u] - x[i];
        const double nextWidth = x[i + 2u] - x[i + 1u];
        offDiagonal[i] = nextWidth;
        diagonal[i] = 2.0 * (nextWidth + width);
        g[i] = 3.0 * ((y[i + 2u] - y[i + 1u]) / nextWidth - (y[i + 1u] - y[i]) / width);
    }

    if (systemSize == 1u)
    {
        c[1] = g[0] / diagonal[0];
    }
    else
    {
        // L.D.L^t over the symmetric system, in the order the library's own solver visits it:
        // `alpha` is D's diagonal and `gamma` is L's sub-diagonal.
        alpha[0] = diagonal[0];
        gamma[0] = offDiagonal[0] / alpha[0];
        for (std::size_t i = 1u; i + 1u < systemSize; ++i)
        {
            alpha[i] = diagonal[i] - offDiagonal[i - 1u] * gamma[i - 1u];
            gamma[i] = offDiagonal[i] / alpha[i];
        }
        alpha[systemSize - 1u] = diagonal[systemSize - 1u] - offDiagonal[systemSize - 2u] * gamma[systemSize - 2u];

        for (std::size_t i = 1u; i < systemSize; ++i)
        {
            g[i] = g[i] - gamma[i - 1u] * g[i - 1u];
        }
        for (std::size_t i = 0u; i < systemSize; ++i)
        {
            g[i] = g[i] / alpha[i];
        }
        c[systemSize] = g[systemSize - 1u];
        for (std::size_t j = 0u; j + 1u < systemSize; ++j)
        {
            const std::size_t i = systemSize - 2u - j;
            c[i + 1u] = g[i] - gamma[i] * c[i + 2u];
        }
    }

    for (std::size_t i = 0u; i + 1u < n; ++i)
    {
        const double width = x[i + 1u] - x[i];
        tangents[i] = (y[i + 1u] - y[i]) / width - width * (c[i + 1u] + 2.0 * c[i]) / 3.0;
    }
    const double lastWidth = x[n - 1u] - x[n - 2u];
    const double lastSecant = (y[n - 1u] - y[n - 2u]) / lastWidth;
    tangents[n - 1u] = lastSecant + lastWidth * (2.0 * c[n - 1u] + c[n - 2u]) / 3.0;
}

} // namespace an
