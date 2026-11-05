// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What one catalog entry is, in owned storage and stated units.
///
/// `CatalogEntry` is the one record every catalog provider produces and every query reads - a star
/// and a galaxy alike. Its strings are owned, its angles are degrees and arcminutes as the member
/// names say, and a fact nobody measured is an absent optional rather than a sentinel value. The
/// photometric band is stated once by the catalog that holds the entries, not per entry.
///
/// Beside it are the angle vocabulary the catalog surfaces share - folding a right ascension into
/// one turn, checking a direction, the great-circle separation - and the canonical spelling of a
/// typed designation. All of them are pure functions with no state.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace an
{

/// The most entries one owned in-memory catalog holds.
///
/// A ceiling rather than a capacity: past it, an owned table of named entries is the wrong shape
/// for the data and a streaming provider is the right one, so the operation says so instead of
/// spending several gigabytes discovering it.
inline constexpr std::uint32_t kMaxCatalogEntries = 4000000u;

/// The same right ascension folded into `[0, 360)`.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param rightAscensionDegrees any finite angle in degrees.
/// @return the folded angle; a not-a-number input is returned unchanged, because inventing an angle
///         for it would hide the mistake that produced it, and an infinite one becomes
///         not-a-number.
[[nodiscard]] AFTERNIGHTENGINE_API double normalizeRightAscensionDegrees(double rightAscensionDegrees) noexcept;

/// Whether a direction is finite and inside the ranges its members declare.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param coordinate the direction to inspect.
/// @return true when right ascension is finite and in `[0, 360)` and declination is finite and in
///         `[-90, 90]`.
[[nodiscard]] AFTERNIGHTENGINE_API bool isSkyCoordinate(const SkyCoordinate& coordinate) noexcept;

/// The great-circle angle between two directions, in degrees.
///
/// The haversine form, which stays accurate for the small separations a catalog match is made of,
/// where the cosine form loses most of its significant digits.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param first one direction.
/// @param second the other.
/// @return the separation in degrees, in `[0, 180]`; not-a-number when either direction is.
[[nodiscard]] AFTERNIGHTENGINE_API double angularSeparationDegrees(const SkyCoordinate& first,
                                                                   const SkyCoordinate& second) noexcept;

/// The apparent size and orientation of an extended object.
///
/// Present only for an entry a catalog measured a size for. A point source has none, which is what
/// distinguishes "not extended" from "extended by zero".
struct CatalogExtent
{
    /// Major-axis angular size, in arcminutes, at least zero.
    double majorAxisArcminutes = 0.0;

    /// Minor-axis angular size, in arcminutes, at least zero and normally at most the major axis.
    double minorAxisArcminutes = 0.0;

    /// Position angle of the major axis, in degrees east of north.
    double positionAngleDegrees = 0.0;

    /// Whether two extents are the same three numbers.
    ///
    /// @param left one extent.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const CatalogExtent& left,
                                                   const CatalogExtent& right) noexcept = default;
};

/// One catalog entry: a named direction and the optional facts a catalog carries about it.
///
/// One record for stars and for extended objects, so a query is written once for both. An unmeasured
/// fact is an absent optional rather than a sentinel: a magnitude of 99 stored for "unknown" would be
/// a legitimate magnitude for the faintest thing a survey catalog carries, and no arithmetic could
/// tell it apart from a measurement.
///
/// Every string is owned, so an entry outlives the text it was parsed from. What band the magnitude
/// is in is stated once by the catalog that holds the entry, not repeated per row.
struct CatalogEntry
{
    /// The catalog designation, such as `"NGC 7000"` or `"HIP 32349"`. Never empty in a built
    /// catalog.
    std::string identifier;

    /// The proper name, such as `"North America Nebula"` or `"Sirius"`. Empty when the catalog
    /// carries none.
    std::string commonName;

    /// A second designation the entry answers to, such as a Bayer letter. Empty when there is none.
    std::string designation;

    /// What kind of object this is, in the source catalog's own vocabulary. Empty when unstated.
    ///
    /// Deliberately free text rather than an enumeration: it is carried and displayed, never
    /// switched on, and every provider spells its own taxonomy differently.
    std::string objectType;

    /// The spectral classification, in the source catalog's own vocabulary. Empty when unstated.
    std::string spectralType;

    /// Where the entry is, in the catalog's stated reference frame.
    SkyCoordinate coordinate{};

    /// Apparent magnitude in the catalog's own photometric band, or absent when unmeasured.
    std::optional<double> magnitude{};

    /// Angular size and orientation, or absent for a point source or an unmeasured one.
    std::optional<CatalogExtent> extent{};

    /// Whether two entries carry the same facts.
    ///
    /// @param left one entry.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend bool operator==(const CatalogEntry& left, const CatalogEntry& right) = default;
};

/// The canonical spelling of a catalog designation a person typed.
///
/// Inserts the missing separator between a known catalog prefix and its number, and spells the
/// prefix the way its catalog does: `"m83"` becomes `"M 83"`, `"ngc7000"` becomes `"NGC 7000"`,
/// `"sh2-240"` becomes `"Sh2-240"`, and both `"caldwell14"` and `"c14"` become `"C 14"`. Text that
/// does not begin with a known prefix followed by digits is returned unchanged, so this is safe to
/// apply to any query before looking it up.
///
/// The scan is bounded and carries no state: it examines a fixed prefix table against at most the
/// leading characters of the input and never backtracks over the whole string, and it keeps no
/// compiled pattern in process-wide state.
///
/// Thread-safe and reentrant. It allocates only the returned string.
///
/// @param designation the text to canonicalize, which need not outlive the call.
/// @return the canonical spelling, or a copy of the input when no prefix matched.
[[nodiscard]] AFTERNIGHTENGINE_API std::string normalizeCatalogDesignation(std::string_view designation);

/// Which way an ordered catalog answer runs.
///
/// Its own vocabulary rather than the star domain's: a catalog of galaxies has nothing to do with a
/// detected star, and the two surfaces are ordered by different keys even when the direction is
/// spelled the same.
enum class CatalogSortDirection : std::uint8_t
{
    /// Smallest first. Brightest first for a magnitude, which is smaller when brighter.
    Ascending = 0u,

    /// Largest first.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Descending = 1u
};

/// Whether a value names a direction this build orders by.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param direction the value to inspect.
/// @return true for one of the two declared values; false for an out-of-range cast.
[[nodiscard]] constexpr bool isCatalogSortDirection(CatalogSortDirection direction) noexcept
{
    return direction == CatalogSortDirection::Ascending || direction == CatalogSortDirection::Descending;
}

} // namespace an
