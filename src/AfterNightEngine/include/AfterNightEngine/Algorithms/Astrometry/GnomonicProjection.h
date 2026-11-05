// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The gnomonic (tangent-plane) projection used by astrometric matching and fitting.
///
/// `projectGnomonic` carries a sky direction onto the plane that touches the celestial sphere at a
/// tangent point, and `inverseGnomonic` carries a plane position back. This is the projection FITS
/// spells `TAN`: `WcsSolution` composes it with a linear matrix and a polynomial distortion, and the
/// plate solver projects catalogue stars through it. Neither function knows anything about pixels.
///
/// A plane position is the pair of standard coordinates: the direction's components along the
/// tangent point's local east and north, divided by its component along the tangent point, and
/// converted from radians to degrees as FITS intermediate world coordinates are. The distance of a
/// plane position from the origin is therefore the tangent of its angular distance from the tangent
/// point - the two agree near the tangent point, and the plane position grows without bound toward
/// ninety degrees from it, which is why a direction on or behind that horizon has no projection.
/// At a pole, east and north are fixed by the tangent point's right ascension.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

namespace an
{

/// One position on a gnomonic tangent plane, as standard coordinates in degrees.
///
/// Positive xDegrees points east at the tangent point and positive yDegrees points north. The
/// values are the plane coordinates the file comment defines rather than image pixels, and equal
/// angular offsets only near the tangent point: a caller applies its own plate scale and pixel-axis
/// convention after this projection.
struct GnomonicCoordinate
{
    /// Eastward tangent-plane coordinate, in degrees.
    double xDegrees = 0.0;

    /// Northward tangent-plane coordinate, in degrees.
    double yDegrees = 0.0;

    /// Whether two tangent-plane positions are the same two numbers.
    ///
    /// @param left one position.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const GnomonicCoordinate& left,
                                                   const GnomonicCoordinate& right) noexcept = default;
};

/// Projects one sky direction onto the plane tangent to another.
///
/// The calculation uses scalar Float64 trigonometry. Astrometric projection is a named Float64
/// path under the engine's precision policy: sub-pixel fits need the conditioning and no Float32
/// or approximate-vector-math alternative is exposed. The tangent point maps to exactly (0, 0).
///
/// Thread-safe, reentrant, and allocation-free on success.
///
/// @param direction the direction to project, with right ascension in [0, 360) degrees and
///        declination in [-90, 90] degrees.
/// @param tangentPoint the direction at the centre of the plane, in the same reference frame.
/// @return the east/north plane position; ErrorCode::InvalidArgument when either direction is not
///         a finite in-range SkyCoordinate; or ErrorCode::OutOfRange when the direction is on,
///         behind or within rounding of the tangent horizon, or has no finite plane coordinate.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GnomonicCoordinate> projectGnomonic(const SkyCoordinate& direction,
                                                                              const SkyCoordinate& tangentPoint);

/// Maps one gnomonic tangent-plane position back onto the sky.
///
/// This is the inverse of projectGnomonic over the open hemisphere in front of the tangent plane.
/// It is evaluated through a scaled three-dimensional direction, so every finite pair of plane
/// coordinates has a finite answer even when squaring or taking the direct hypotenuse would
/// overflow. The origin returns the tangent point exactly, including its conventional right
/// ascension when that point is a pole.
///
/// The calculation uses the same scalar Float64 precision contract as the forward projection.
/// Thread-safe, reentrant, and allocation-free on success.
///
/// @param coordinate the finite east/north plane position, in degrees.
/// @param tangentPoint the direction at the centre of the plane, in the same reference frame as the
///        returned direction.
/// @return the sky direction, with right ascension folded into [0, 360); or
///         ErrorCode::InvalidArgument when the plane position is not finite or the tangent point
///         is not a finite in-range SkyCoordinate.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SkyCoordinate> inverseGnomonic(const GnomonicCoordinate& coordinate,
                                                                         const SkyCoordinate& tangentPoint);

} // namespace an
