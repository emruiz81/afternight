// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Typed HEALPix grids, pixel addresses, and bounded cone queries.
///
/// HEALPix divides the sphere into twelve base faces of equal area, each subdivided into Nside by
/// Nside pixels, and numbers the pixels either ring by ring or hierarchically within each face. A
/// `HealpixGrid` carries the order and the numbering together, and a `HealpixPixelIndex` carries
/// its grid, so an address from one numbering cannot be read as the other. The functions here turn
/// a direction into its owning pixel, a pixel into its centre, and a cone into the pixels it
/// selects - walking only the iso-latitude rings and the longitude intervals the cone reaches -
/// in scalar Float64, with no process-wide state. Orders above `kMaximumHealpixOrder` are refused.
///
/// The ring numbering in the equatorial belt follows the standard definition, and
/// `HealpixDiscCoverage::ConservativeOverlap` widens a cone by the exact maximum centre-to-corner
/// distance rather than by an empirical multiple of an area-equivalent radius.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The largest HEALPix order represented by the public 32-bit pixel address.
///
/// At order 13 the full sky has 805,306,368 pixels. Order 14 would have 3,221,225,472: representable
/// as an unsigned number, but past the range of a signed 32-bit index.
inline constexpr std::uint8_t kMaximumHealpixOrder = 13u;

/// The most pixels one cone query returns unless the caller asks for fewer.
inline constexpr std::uint32_t kDefaultMaximumHealpixQueryPixels = 1000000u;

/// Which standard HEALPix numbering scheme addresses a grid.
enum class HealpixOrdering : std::uint8_t
{
    /// Iso-latitude rings numbered from north to south.
    Ring = 0u,

    /// Hierarchical face-local Morton indices.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Nested = 1u
};

/// Whether a value names one of the two standard numbering schemes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param ordering the value to inspect.
/// @return true for Ring or Nested; false for an out-of-range cast.
[[nodiscard]] constexpr bool isHealpixOrdering(HealpixOrdering ordering) noexcept
{
    return ordering == HealpixOrdering::Ring || ordering == HealpixOrdering::Nested;
}

/// A complete HEALPix grid identity.
///
/// The order and numbering travel together so an index from a nested catalog cannot silently be
/// read as a ring index. The order is the base-two logarithm of Nside, in [0, 13].
struct HealpixGrid
{
    /// Resolution order: Nside is two raised to this value.
    std::uint8_t order = 0u;

    /// How pixels in this grid are numbered.
    HealpixOrdering ordering = HealpixOrdering::Ring;

    /// Whether two grid identities are the same.
    ///
    /// @param left one grid.
    /// @param right the other.
    /// @return true when both the order and numbering agree.
    [[nodiscard]] friend constexpr bool operator==(const HealpixGrid& left,
                                                   const HealpixGrid& right) noexcept = default;
};

/// Whether a grid has a supported order and numbering scheme.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param grid the grid to inspect.
/// @return true when its order is at most kMaximumHealpixOrder and its ordering is known.
[[nodiscard]] constexpr bool isHealpixGrid(const HealpixGrid& grid) noexcept
{
    return grid.order <= kMaximumHealpixOrder && isHealpixOrdering(grid.ordering);
}

/// The grid's Nside resolution parameter.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param grid the grid to inspect.
/// @return two raised to order; zero when the order or numbering is unsupported.
[[nodiscard]] constexpr std::uint32_t healpixSideCount(const HealpixGrid& grid) noexcept
{
    return isHealpixGrid(grid) ? (std::uint32_t{1} << grid.order) : 0u;
}

/// How many equal-area pixels cover the full grid.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param grid the grid to inspect.
/// @return 12 times Nside squared; zero when the grid is unsupported.
[[nodiscard]] constexpr std::uint32_t healpixPixelCount(const HealpixGrid& grid) noexcept
{
    const std::uint32_t side = healpixSideCount(grid);
    return side == 0u ? 0u : 12u * side * side;
}

/// The numbering scheme's own name.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param ordering the numbering to name.
/// @return a null-terminated string literal with static storage duration;
///         UnknownHealpixOrdering for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* healpixOrderingName(HealpixOrdering ordering) noexcept;

/// One pixel address, including the grid in which its numeric value has meaning.
struct HealpixPixelIndex
{
    /// The grid this address belongs to.
    HealpixGrid grid{};

    /// Zero-based pixel number in grid.ordering, less than healpixPixelCount(grid).
    std::uint32_t value = 0u;

    /// Whether two addresses identify the same pixel in the same grid.
    ///
    /// @param left one address.
    /// @param right the other.
    /// @return true when the grid and numeric value agree.
    [[nodiscard]] friend constexpr bool operator==(const HealpixPixelIndex& left,
                                                   const HealpixPixelIndex& right) noexcept = default;
};

/// Finds the pixel that owns one direction under the grid's half-open boundary convention.
///
/// The standard HEALPix equations are evaluated in scalar Float64 and the returned address carries
/// its order and numbering. A direction exactly on a shared edge is assigned deterministically to
/// one adjacent pixel, as every complete pixelisation must assign it.
///
/// Thread-safe, reentrant, and allocation-free on success.
///
/// @param grid the order and numbering of the grid to address.
/// @param coordinate the finite direction, in the ranges declared by SkyCoordinate.
/// @return the owning pixel; ErrorCode::InvalidArgument when the grid or direction is invalid.
[[nodiscard]] AFTERNIGHTENGINE_API Result<HealpixPixelIndex>
skyCoordinateToHealpixPixel(const HealpixGrid& grid, const SkyCoordinate& coordinate);

/// Returns the nominal centre of one HEALPix pixel.
///
/// Both numbering schemes reach the same physical centres. Ring indices are inverted directly;
/// nested indices are de-interleaved within their face and then mapped to the corresponding ring.
/// The calculation is scalar Float64 and introduces no precision selector.
///
/// Thread-safe, reentrant, and allocation-free on success.
///
/// @param pixel the grid-qualified pixel address.
/// @return the centre direction; ErrorCode::InvalidArgument for an unsupported grid, or
///         ErrorCode::OutOfRange when pixel.value is outside that grid.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SkyCoordinate> healpixPixelCenter(const HealpixPixelIndex& pixel);

/// The largest angular distance from any pixel centre to any corner in a grid.
///
/// This is the exact HEALPix max-pixel-radius bound, in degrees, not an area-equivalent radius
/// multiplied by an empirical factor. It is independent of the numbering scheme and is the safety
/// distance used by conservative overlap queries.
///
/// Thread-safe, reentrant, and allocation-free on success.
///
/// @param grid the grid whose geometric bound is requested.
/// @return the maximum radius in degrees; ErrorCode::InvalidArgument for an unsupported grid.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> healpixMaximumPixelRadiusDegrees(const HealpixGrid& grid);

/// Which relation between a cone and a HEALPix pixel a query returns.
enum class HealpixDiscCoverage : std::uint8_t
{
    /// Pixels whose nominal centres are inside or on the cone.
    Centers = 0u,

    /// A conservative superset of pixels that can overlap the cone.
    ///
    /// The cone is expanded by the grid's exact maximum centre-to-corner distance. This can return
    /// pixels that do not intersect the cone, but it cannot omit a pixel that does; provider code
    /// may therefore decode the answer without losing a catalog record near a tile boundary.
    ConservativeOverlap = 1u
};

/// Whether a value names a supported cone-coverage rule.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param coverage the value to inspect.
/// @return true for either declared rule; false for an out-of-range cast.
[[nodiscard]] constexpr bool isHealpixDiscCoverage(HealpixDiscCoverage coverage) noexcept
{
    return coverage == HealpixDiscCoverage::Centers || coverage == HealpixDiscCoverage::ConservativeOverlap;
}

/// One bounded cone query over a HEALPix grid.
struct HealpixDiscQuery
{
    /// Centre of the cone.
    SkyCoordinate center{};

    /// Angular radius in degrees, finite and in [0, 180].
    double radiusDegrees = 0.0;

    /// Whether pixel centres or conservative overlaps are returned.
    HealpixDiscCoverage coverage = HealpixDiscCoverage::ConservativeOverlap;

    /// The most pixel addresses this query is allowed to return, greater than zero.
    std::uint32_t maximumPixels = kDefaultMaximumHealpixQueryPixels;
};

/// The addresses and work counts produced by one cone query.
struct HealpixDiscResult
{
    /// Grid-qualified pixel addresses, strictly ascending by numeric value with no duplicates.
    std::vector<HealpixPixelIndex> pixels{};

    /// How many iso-latitude rings were visited.
    ///
    /// Every ring of the grid - four times Nside minus one - for a positive radius, and zero for a
    /// zero radius, which is answered without walking any. A ring the cone does not reach costs
    /// one comparison.
    std::uint32_t ringsExamined = 0u;

    /// How many pixel centres passed through the final exact angular test.
    ///
    /// This makes a selective query distinguishable from a hidden scan of the full sky in both a
    /// unit test and a benchmark report.
    std::uint64_t candidateCentersExamined = 0u;
};

/// Finds the HEALPix pixels selected by a cone without scanning the full sky.
///
/// The operation walks at most four times Nside minus one iso-latitude rings, solves the longitude
/// interval on each intersecting ring, and tests only centres in those intervals. Centers is the
/// exact centre-in-disc relation. ConservativeOverlap expands the cone by
/// healpixMaximumPixelRadiusDegrees, deliberately returning a safe superset for catalog-tile
/// loading. A zero-radius query returns the single pixel that owns its centre under either rule.
///
/// Ring output is generated directly. Nested output converts the same physical pixels and sorts by
/// the nested address, so changing the numbering changes only the numbers and their order, never
/// the region of sky selected. All trigonometry is scalar Float64 under the astrometric precision
/// policy.
///
/// Runs on the calling thread, checks cancellation at every ring, and allocates only the returned
/// addresses. A failure returns no partial list.
///
/// @param execution the operation policy and engine-context lease.
/// @param grid the order and numbering to query.
/// @param query the cone, coverage rule and output bound.
/// @return the selected addresses and work counts; ErrorCode::NotInitialized when execution
///         holds nothing; ErrorCode::InvalidArgument for an unsupported grid, an invalid centre,
///         radius or coverage rule, or a zero output bound; ErrorCode::LimitExceeded when the
///         answer would exceed maximumPixels - the query stops at the first pixel past the bound,
///         so the failure carries one more than the bound as the requested count, not the size the
///         whole answer would have had; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<HealpixDiscResult>
queryHealpixDisc(const ExecutionContext& execution, const HealpixGrid& grid, const HealpixDiscQuery& query);

} // namespace an
