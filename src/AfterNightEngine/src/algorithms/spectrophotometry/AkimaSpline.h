// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - Akima's interpolant over caller scratch: the piecewise cubic quadrature
// over the slope rule and boundary extrapolation `core/SplineTangents.h` states, so the synthetic
// photometry integrates to the same numbers the GNU Scientific Library's Akima interpolation
// produces, without an allocation per call or a process-wide error handler.
//
// The rule itself is not here. It is the same rule the tone curve joins its control points with, and
// it lives in the layer both may include; what is here is the power form this domain needs for the
// quadrature, derived from that rule's two end derivatives.

#pragma once

#include "core/SplineTangents.h"

#include <cstddef>
#include <span>

/// The spectrophotometry domain's private helpers: the Akima interpolant and quadrature over caller
/// scratch here, and the failure builders and typed channel reader in `SpectrophotometryInternal.h`.
namespace an::spectrophotometry_detail
{

/// Where one spline keeps its slopes and coefficients.
///
/// `slopes` holds `n + 4` values so the two extrapolated slopes before the first point and the two
/// after the last address naturally; `b`, `c` and `d` hold `n - 1` coefficients each, one per
/// interval, and are sized `n` for simplicity.
struct AkimaScratch
{
    /// `n + 4` slopes; index two is the first interval's.
    double* slopes = nullptr;

    /// `n` linear coefficients.
    double* b = nullptr;

    /// `n` quadratic coefficients.
    double* c = nullptr;

    /// `n` cubic coefficients.
    double* d = nullptr;
};

/// How many doubles one spline of `pointCount` points needs.
///
/// @param pointCount how many points.
/// @return the scratch length.
[[nodiscard]] constexpr std::size_t akimaScratchDoubles(const std::size_t pointCount) noexcept
{
    return (pointCount + 4u) + 3u * pointCount;
}

/// Carves one spline's scratch out of a block.
///
/// @param block at least `akimaScratchDoubles(pointCount)` doubles.
/// @param pointCount how many points.
/// @return the four regions.
[[nodiscard]] inline AkimaScratch akimaScratchOver(double* const block, const std::size_t pointCount) noexcept
{
    AkimaScratch scratch;
    scratch.slopes = block;
    scratch.b = block + pointCount + 4u;
    scratch.c = scratch.b + pointCount;
    scratch.d = scratch.c + pointCount;
    return scratch;
}

/// Computes the coefficients of the interpolant through `n` points.
///
/// The shared rule decides every segment's two end derivatives; this turns each of them into the
/// power form the evaluation and the quadrature below read. A segment the rule calls straight keeps
/// the library's own literal zeroes rather than the arithmetic that would nearly produce them.
/// Requires strictly increasing abscissae and at least `kAkimaMinimumPoints` points.
///
/// @param x the abscissae.
/// @param y the ordinates, as long as `x`.
/// @param scratch where the slopes and coefficients go.
inline void akimaInitialize(const std::span<const double> x, const std::span<const double> y,
                            const AkimaScratch& scratch) noexcept
{
    const std::size_t n = x.size();
    const std::span<double> slopes(scratch.slopes, akimaSlopeDoubles(n));
    akimaFillExtendedSlopes(x, y, slopes);

    for (std::size_t i = 0u; i + 1u < n; ++i)
    {
        const double secant = slopes[i + 2u];
        const SegmentTangents tangents = akimaSegmentTangents(slopes, i);
        if (tangents.straight)
        {
            scratch.b[i] = secant;
            scratch.c[i] = 0.0;
            scratch.d[i] = 0.0;
            continue;
        }
        const double h = x[i + 1u] - x[i];
        scratch.b[i] = tangents.left;
        scratch.c[i] = (3.0 * secant - 2.0 * tangents.left - tangents.right) / h;
        scratch.d[i] = (tangents.left + tangents.right - 2.0 * secant) / (h * h);
    }
}

/// The interval an abscissa falls in: the largest `i` below `n - 1` with `x[i] <= at`.
///
/// @param x the abscissae.
/// @param at the abscissa, inside `[x.front(), x.back()]`.
/// @return the interval index, in `[0, n - 2]`.
[[nodiscard]] inline std::size_t akimaInterval(const std::span<const double> x, const double at) noexcept
{
    std::size_t low = 0u;
    std::size_t high = x.size() - 1u;
    while (high > low + 1u)
    {
        const std::size_t middle = (high + low) / 2u;
        if (x[middle] > at)
        {
            high = middle;
        }
        else
        {
            low = middle;
        }
    }
    return low;
}

/// The interpolant's value.
///
/// @param x the abscissae.
/// @param y the ordinates.
/// @param scratch the coefficients `akimaInitialize` computed.
/// @param at the abscissa, inside `[x.front(), x.back()]`.
/// @return the value.
[[nodiscard]] inline double akimaEvaluate(const std::span<const double> x, const std::span<const double> y,
                                          const AkimaScratch& scratch, const double at) noexcept
{
    const std::size_t index = akimaInterval(x, at);
    const double dx = at - x[index];
    return y[index] + dx * (scratch.b[index] + dx * (scratch.c[index] + scratch.d[index] * dx));
}

/// The integral of one cubic segment between two abscissae inside its interval.
///
/// @param a0 the ordinate at the interval's start.
/// @param b the linear coefficient.
/// @param c the quadratic coefficient.
/// @param d the cubic coefficient.
/// @param x0 the interval's start.
/// @param from where the integral starts.
/// @param to where it ends.
/// @return the integral.
[[nodiscard]] inline double akimaSegmentIntegral(const double a0, const double b, const double c, const double d,
                                                 const double x0, const double from, const double to) noexcept
{
    const double r1 = from - x0;
    const double r2 = to - x0;
    const double r12 = r1 + r2;
    const double bterm = 0.5 * b * r12;
    const double cterm = (1.0 / 3.0) * c * (r1 * r1 + r2 * r2 + r1 * r2);
    const double dterm = 0.25 * d * r12 * (r1 * r1 + r2 * r2);
    return (to - from) * (a0 + bterm + cterm + dterm);
}

/// The interpolant's integral between two abscissae.
///
/// The partial segments at either end are integrated from their power form between the two limits,
/// and every whole segment between them in closed form over its full width.
///
/// @param x the abscissae.
/// @param y the ordinates.
/// @param scratch the coefficients `akimaInitialize` computed.
/// @param from where the integral starts, inside `[x.front(), x.back()]`.
/// @param to where it ends, at or after `from` and also inside `[x.front(), x.back()]`; it may lie
///        in a later segment than `from`.
/// @return the integral.
[[nodiscard]] inline double akimaIntegrate(const std::span<const double> x, const std::span<const double> y,
                                           const AkimaScratch& scratch, const double from, const double to) noexcept
{
    const std::size_t first = akimaInterval(x, from);
    const std::size_t last = akimaInterval(x, to);
    double result = 0.0;
    for (std::size_t i = first; i <= last; ++i)
    {
        const double low = x[i];
        const double high = x[i + 1u];
        const double dx = high - low;
        if (i == first || i == last)
        {
            const double x1 = i == first ? from : low;
            const double x2 = i == last ? to : high;
            result += akimaSegmentIntegral(y[i], scratch.b[i], scratch.c[i], scratch.d[i], low, x1, x2);
        }
        else
        {
            result += dx * (y[i] + dx * (0.5 * scratch.b[i] + dx * (scratch.c[i] / 3.0 + 0.25 * scratch.d[i] * dx)));
        }
    }
    return result;
}

} // namespace an::spectrophotometry_detail
