// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A direction on the celestial sphere.
///
/// Its own header because two areas need it and neither owns it. An image's astrometric solution
/// states where a pixel points; a catalog entry states where an object is; a plate model turns one
/// into the other. All three mean the same two numbers, and a type that three areas each declare
/// for themselves is three types that happen to agree until one of them changes.
///
/// It lives in the image area rather than the algorithm area because the layer graph runs one way:
/// an algorithm may name an image concept and an image may not name an algorithm. Putting the shared
/// direction here is what lets the catalog and the world-coordinate model share it without the image
/// model reaching upward for it.

#pragma once

namespace an
{

/// A direction on the celestial sphere, in degrees.
///
/// Degrees rather than radians because every catalogue, every header and every user interface in
/// this domain states them in degrees, and a unit conversion at each of those boundaries is a
/// conversion that will eventually be left out at one of them.
///
/// Both members are equatorial and carry their unit in their name. Nothing here states an equinox
/// or an epoch: a catalog states those once for all of its entries, and mixing two frames inside one
/// table is a provider mistake this record cannot repair.
struct SkyCoordinate
{
    /// Right ascension, in degrees, normally in `[0, 360)`.
    double rightAscensionDegrees = 0.0;

    /// Declination, in degrees, normally in `[-90, 90]`.
    double declinationDegrees = 0.0;

    /// Whether two directions are the same two numbers.
    ///
    /// Exact equality of the two numbers, which is what a metadata comparison and a table lookup
    /// both want; an angular tolerance is a question for whatever is comparing directions, not for
    /// the value type.
    ///
    /// @param left one direction.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const SkyCoordinate& left,
                                                   const SkyCoordinate& right) noexcept = default;
};

} // namespace an
