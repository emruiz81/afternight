// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - standard HEALPix indexing and selective cone queries.
//
// The equations are independently adapted to this engine's checked types from the public HEALPix
// definition (Gorski et al. 2005, ApJ 622, 759) and are differentially guarded against the HEALPix
// 3.83 reference values. No HEALPix library or process-wide state is linked into the engine.
//
// A cone query walks every iso-latitude ring, skips a ring whose latitude is already further than
// the radius, solves the longitude half-width the cone subtends on each remaining ring from the
// haversine identity, and tests only the centres in that interval with the exact separation. The
// interval is widened by a pixel on each side, so rounding in the half-width can only add a
// centre the exact test then refuses, never drop one it would admit.

#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kHalfPi = 0.5 * std::numbers::pi;
constexpr double kTwoThirds = 2.0 / 3.0;
constexpr const char* kPixelAtOperation = "an::skyCoordinateToHealpixPixel";
constexpr const char* kPixelCenterOperation = "an::healpixPixelCenter";
constexpr const char* kPixelRadiusOperation = "an::healpixMaximumPixelRadiusDegrees";
constexpr const char* kDiscOperation = "an::queryHealpixDisc";

/// The face-to-ring latitude coefficient from the HEALPix face layout.
constexpr std::array<std::int32_t, 12u> kFaceRingLatitude{2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4};

/// The face-to-ring longitude coefficient from the HEALPix face layout.
constexpr std::array<std::int32_t, 12u> kFaceRingLongitude{1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7};

/// Everything needed to enumerate one iso-latitude ring.
struct RingInfo
{
    /// First zero-based ring pixel.
    std::uint32_t firstPixel = 0u;

    /// Number of pixels in the ring.
    std::uint32_t pixelCount = 0u;

    /// Sine of the ring-centre declination.
    double z = 0.0;

    /// Whether the first centre is half a longitudinal step from zero.
    bool shifted = false;
};

/// Builds an algorithms-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @param context typed facts a caller may act on.
/// @return the complete failure.
[[nodiscard]] Status healpixFailure(const ErrorCode code, const char* const message, const char* const operation,
                                    std::vector<StatusContextField> context = {})
{
    return Status::failure(code, StatusDetail{.message = message,
                                              .operation = operation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = std::move(context)});
}

/// The integer square root, corrected around the floating estimate.
///
/// @param value a value small enough for the exact 32-bit HEALPix index arithmetic.
/// @return the largest integer whose square does not exceed value.
[[nodiscard]] std::uint32_t integerSquareRoot(const std::uint64_t value) noexcept
{
    std::uint64_t root = static_cast<std::uint64_t>(std::sqrt(static_cast<double>(value)));
    while ((root + 1u) * (root + 1u) <= value)
    {
        ++root;
    }
    while (root * root > value)
    {
        --root;
    }
    return static_cast<std::uint32_t>(root);
}

/// Interleaves the low sixteen bits with zero bits for a nested face index.
///
/// @param value the face-local axis coordinate.
/// @return the coordinate bits in the even positions.
[[nodiscard]] std::uint32_t spreadBits(const std::uint32_t value) noexcept
{
    std::uint32_t spread = 0u;
    for (std::uint32_t bit = 0u; bit < 16u; ++bit)
    {
        spread |= ((value >> bit) & 1u) << (2u * bit);
    }
    return spread;
}

/// Extracts the bits in the even positions of a nested face index.
///
/// @param value the interleaved bits.
/// @return the compact face-local axis coordinate.
[[nodiscard]] std::uint32_t compressBits(const std::uint32_t value) noexcept
{
    std::uint32_t compact = 0u;
    for (std::uint32_t bit = 0u; bit < 16u; ++bit)
    {
        compact |= ((value >> (2u * bit)) & 1u) << bit;
    }
    return compact;
}

/// Describes one ring counted from one at the north pole.
///
/// @param side the grid's Nside.
/// @param fullPixelCount the grid's full-sky pixel count.
/// @param ring the one-based ring in [1, 4*Nside-1].
/// @return the ring geometry and address range.
[[nodiscard]] RingInfo describeRing(const std::uint32_t side, const std::uint32_t fullPixelCount,
                                    const std::uint32_t ring) noexcept
{
    const double sideSquared = static_cast<double>(side) * static_cast<double>(side);
    if (ring < side)
    {
        return RingInfo{.firstPixel = 2u * ring * (ring - 1u),
                        .pixelCount = 4u * ring,
                        .z = 1.0 - (static_cast<double>(ring) * static_cast<double>(ring)) / (3.0 * sideSquared),
                        .shifted = true};
    }
    if (ring < 3u * side)
    {
        const std::uint32_t northCapPixels = 2u * side * (side - 1u);
        const std::uint32_t count = 4u * side;
        return RingInfo{.firstPixel = northCapPixels + (ring - side) * count,
                        .pixelCount = count,
                        .z = (2.0 * static_cast<double>(side) - static_cast<double>(ring)) *
                             (2.0 / (3.0 * static_cast<double>(side))),
                        .shifted = ((ring - side) & 1u) == 0u};
    }

    const std::uint32_t southRing = 4u * side - ring;
    return RingInfo{.firstPixel = fullPixelCount - 2u * southRing * (southRing + 1u),
                    .pixelCount = 4u * southRing,
                    .z = (static_cast<double>(southRing) * static_cast<double>(southRing)) / (3.0 * sideSquared) - 1.0,
                    .shifted = true};
}

/// Finds the one-based ring containing a standard ring index.
///
/// @param side the grid's Nside.
/// @param fullPixelCount the grid's full-sky pixel count.
/// @param ringPixel the valid zero-based ring index.
/// @return its one-based ring.
[[nodiscard]] std::uint32_t ringForPixel(const std::uint32_t side, const std::uint32_t fullPixelCount,
                                         const std::uint32_t ringPixel) noexcept
{
    const std::uint32_t northCapPixels = 2u * side * (side - 1u);
    if (ringPixel < northCapPixels)
    {
        return (1u + integerSquareRoot(1u + 2u * static_cast<std::uint64_t>(ringPixel))) >> 1u;
    }
    if (ringPixel < fullPixelCount - northCapPixels)
    {
        return ((ringPixel - northCapPixels) / (4u * side)) + side;
    }
    return 4u * side -
           ((1u + integerSquareRoot(2u * static_cast<std::uint64_t>(fullPixelCount - ringPixel) - 1u)) >> 1u);
}

/// Returns a ring pixel's centre without validating the already-proved index.
///
/// @param side the grid's Nside.
/// @param fullPixelCount the grid's full-sky pixel count.
/// @param ringPixel the valid zero-based ring index.
/// @return the nominal centre.
[[nodiscard]] SkyCoordinate ringPixelCenter(const std::uint32_t side, const std::uint32_t fullPixelCount,
                                            const std::uint32_t ringPixel) noexcept
{
    const std::uint32_t ring = ringForPixel(side, fullPixelCount, ringPixel);
    const RingInfo info = describeRing(side, fullPixelCount, ring);
    const std::uint32_t position = ringPixel - info.firstPixel;
    const double step = kTwoPi / static_cast<double>(info.pixelCount);
    double longitude = (static_cast<double>(position) + (info.shifted ? 0.5 : 0.0)) * step;
    if (longitude >= kTwoPi)
    {
        longitude -= kTwoPi;
    }
    return SkyCoordinate{.rightAscensionDegrees = longitude * kDegreesPerRadian,
                         .declinationDegrees = std::asin(std::clamp(info.z, -1.0, 1.0)) * kDegreesPerRadian};
}

/// Maps one valid direction to a standard ring index.
///
/// @param side the grid's Nside.
/// @param fullPixelCount the grid's full-sky pixel count.
/// @param coordinate the already-validated direction.
/// @return the owning zero-based ring index.
[[nodiscard]] std::uint32_t coordinateToRingPixel(const std::uint32_t side, const std::uint32_t fullPixelCount,
                                                  const SkyCoordinate& coordinate) noexcept
{
    const double longitude = coordinate.rightAscensionDegrees * kRadiansPerDegree;
    const double z = std::sin(coordinate.declinationDegrees * kRadiansPerDegree);
    const double absoluteZ = std::abs(z);
    const double quadrantPosition = longitude / kHalfPi;
    const std::uint32_t northCapPixels = 2u * side * (side - 1u);

    if (absoluteZ <= kTwoThirds)
    {
        const double ascending = static_cast<double>(side) * (0.5 + quadrantPosition - z * 0.75);
        const double descending = static_cast<double>(side) * (0.5 + quadrantPosition + z * 0.75);
        const std::int32_t ascendingEdge = static_cast<std::int32_t>(std::floor(ascending));
        const std::int32_t descendingEdge = static_cast<std::int32_t>(std::floor(descending));
        const std::int32_t relativeRing = static_cast<std::int32_t>(side) + 1 + ascendingEdge - descendingEdge;
        const std::int32_t shift = 1 - (relativeRing & 1);
        const std::int32_t pixelsPerRing = static_cast<std::int32_t>(4u * side);
        const std::int32_t wrapped =
            ascendingEdge + descendingEdge - static_cast<std::int32_t>(side) + shift + 1 + 2 * pixelsPerRing;
        const std::uint32_t position = static_cast<std::uint32_t>((wrapped / 2) & (pixelsPerRing - 1));
        return northCapPixels + static_cast<std::uint32_t>(relativeRing - 1) * 4u * side + position;
    }

    const double withinQuadrant = quadrantPosition - std::floor(quadrantPosition);
    const double ringScale = static_cast<double>(side) * std::sqrt((std::max)(3.0 * (1.0 - absoluteZ), 0.0));
    const std::uint32_t ascendingEdge = static_cast<std::uint32_t>(std::floor(withinQuadrant * ringScale));
    const std::uint32_t descendingEdge = static_cast<std::uint32_t>(std::floor((1.0 - withinQuadrant) * ringScale));
    const std::uint32_t polarRing = ascendingEdge + descendingEdge + 1u;
    std::uint32_t position = static_cast<std::uint32_t>(std::floor(quadrantPosition * polarRing));
    const std::uint32_t pixelsInRing = 4u * polarRing;
    if (position >= pixelsInRing)
    {
        position -= pixelsInRing;
    }
    return z > 0.0 ? 2u * polarRing * (polarRing - 1u) + position
                   : fullPixelCount - 2u * polarRing * (polarRing + 1u) + position;
}

/// Maps one valid direction to a standard nested index.
///
/// @param order the grid order.
/// @param side the grid's Nside.
/// @param coordinate the already-validated direction.
/// @return the owning zero-based nested index.
[[nodiscard]] std::uint32_t coordinateToNestedPixel(const std::uint8_t order, const std::uint32_t side,
                                                    const SkyCoordinate& coordinate) noexcept
{
    const double longitude = coordinate.rightAscensionDegrees * kRadiansPerDegree;
    const double z = std::sin(coordinate.declinationDegrees * kRadiansPerDegree);
    const double absoluteZ = std::abs(z);
    const double quadrantPosition = longitude / kHalfPi;

    std::int32_t face = 0;
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
    if (absoluteZ <= kTwoThirds)
    {
        const double ascending = static_cast<double>(side) * (0.5 + quadrantPosition - z * 0.75);
        const double descending = static_cast<double>(side) * (0.5 + quadrantPosition + z * 0.75);
        const std::int32_t ascendingEdge = static_cast<std::int32_t>(std::floor(ascending));
        const std::int32_t descendingEdge = static_cast<std::int32_t>(std::floor(descending));
        const std::int32_t ascendingFace = ascendingEdge >> order;
        const std::int32_t descendingFace = descendingEdge >> order;
        face = ascendingFace == descendingFace ? (ascendingFace | 4)
                                               : (ascendingFace < descendingFace ? ascendingFace : descendingFace + 8);
        x = static_cast<std::uint32_t>(descendingEdge) & (side - 1u);
        y = side - (static_cast<std::uint32_t>(ascendingEdge) & (side - 1u)) - 1u;
    }
    else
    {
        const std::int32_t quadrant = (std::min)(3, static_cast<std::int32_t>(std::floor(quadrantPosition)));
        const double withinQuadrant = quadrantPosition - static_cast<double>(quadrant);
        const double ringScale = static_cast<double>(side) * std::sqrt((std::max)(3.0 * (1.0 - absoluteZ), 0.0));
        const std::uint32_t ascendingEdge =
            (std::min)(side - 1u, static_cast<std::uint32_t>(std::floor(withinQuadrant * ringScale)));
        const std::uint32_t descendingEdge =
            (std::min)(side - 1u, static_cast<std::uint32_t>(std::floor((1.0 - withinQuadrant) * ringScale)));
        if (z >= 0.0)
        {
            face = quadrant;
            x = side - descendingEdge - 1u;
            y = side - ascendingEdge - 1u;
        }
        else
        {
            face = quadrant + 8;
            x = ascendingEdge;
            y = descendingEdge;
        }
    }

    const std::uint32_t facePixels = side * side;
    return static_cast<std::uint32_t>(face) * facePixels + spreadBits(x) + (spreadBits(y) << 1u);
}

/// Converts a valid nested index into the physically identical ring index.
///
/// @param side the grid's Nside.
/// @param fullPixelCount the grid's full-sky pixel count.
/// @param nestedPixel the valid nested index.
/// @return the corresponding ring index.
[[nodiscard]] std::uint32_t nestedToRingPixel(const std::uint32_t side, const std::uint32_t fullPixelCount,
                                              const std::uint32_t nestedPixel) noexcept
{
    const std::uint32_t facePixels = side * side;
    const std::uint32_t face = nestedPixel / facePixels;
    const std::uint32_t withinFace = nestedPixel % facePixels;
    const std::int32_t x = static_cast<std::int32_t>(compressBits(withinFace));
    const std::int32_t y = static_cast<std::int32_t>(compressBits(withinFace >> 1u));
    const std::int32_t ring = kFaceRingLatitude[face] * static_cast<std::int32_t>(side) - x - y - 1;
    const RingInfo info = describeRing(side, fullPixelCount, static_cast<std::uint32_t>(ring));
    const std::int32_t ringSide = static_cast<std::int32_t>(info.pixelCount / 4u);
    const std::int32_t shift = info.shifted ? 0 : 1;
    std::int32_t position = (kFaceRingLongitude[face] * ringSide + x - y + 1 + shift) / 2;
    if (position < 1)
    {
        position += static_cast<std::int32_t>(4u * side);
    }
    return info.firstPixel + static_cast<std::uint32_t>(position - 1);
}

/// Converts a valid ring index into the physically identical nested index.
///
/// The pixel centre is strictly interior to its pixel, so applying the standard nested ownership
/// equation to that centre is an exact numbering conversion without the considerably larger
/// ring-to-face case table.
///
/// @param order the grid order.
/// @param side the grid's Nside.
/// @param fullPixelCount the grid's full-sky pixel count.
/// @param ringPixel the valid ring index.
/// @return the corresponding nested index.
[[nodiscard]] std::uint32_t ringToNestedPixel(const std::uint8_t order, const std::uint32_t side,
                                              const std::uint32_t fullPixelCount,
                                              const std::uint32_t ringPixel) noexcept
{
    return coordinateToNestedPixel(order, side, ringPixelCenter(side, fullPixelCount, ringPixel));
}

/// The squared sine of half the great-circle separation.
///
/// Unlike a cosine comparison this retains precision for sub-arcsecond radii.
///
/// @param firstDeclination first declination in radians.
/// @param firstLongitude first right ascension in radians.
/// @param secondDeclination second declination in radians.
/// @param secondLongitude second right ascension in radians.
/// @return the haversine of the separation in [0, 1].
[[nodiscard]] double separationHaversine(const double firstDeclination, const double firstLongitude,
                                         const double secondDeclination, const double secondLongitude) noexcept
{
    const double declinationTerm = std::sin((secondDeclination - firstDeclination) * 0.5);
    const double longitudeTerm = std::sin((secondLongitude - firstLongitude) * 0.5);
    return std::clamp(declinationTerm * declinationTerm +
                          std::cos(firstDeclination) * std::cos(secondDeclination) * longitudeTerm * longitudeTerm,
                      0.0, 1.0);
}

} // namespace

const char* healpixOrderingName(const HealpixOrdering ordering) noexcept
{
    switch (ordering)
    {
    case HealpixOrdering::Ring:
        return "Ring";
    case HealpixOrdering::Nested:
        return "Nested";
    }
    return "UnknownHealpixOrdering";
}

Result<HealpixPixelIndex> skyCoordinateToHealpixPixel(const HealpixGrid& grid, const SkyCoordinate& coordinate)
{
    if (!isHealpixGrid(grid))
    {
        return healpixFailure(ErrorCode::InvalidArgument, "the HEALPix grid has an unsupported order or numbering",
                              kPixelAtOperation, {StatusContextField::ofText(ContextKey::Parameter, "grid")});
    }
    if (!isSkyCoordinate(coordinate))
    {
        return healpixFailure(ErrorCode::InvalidArgument, "the HEALPix direction is not finite and in range",
                              kPixelAtOperation, {StatusContextField::ofText(ContextKey::Parameter, "coordinate")});
    }

    const std::uint32_t side = healpixSideCount(grid);
    const std::uint32_t fullPixelCount = healpixPixelCount(grid);
    const std::uint32_t value = grid.ordering == HealpixOrdering::Ring
                                    ? coordinateToRingPixel(side, fullPixelCount, coordinate)
                                    : coordinateToNestedPixel(grid.order, side, coordinate);
    return HealpixPixelIndex{.grid = grid, .value = value};
}

Result<SkyCoordinate> healpixPixelCenter(const HealpixPixelIndex& pixel)
{
    if (!isHealpixGrid(pixel.grid))
    {
        return healpixFailure(ErrorCode::InvalidArgument, "the HEALPix grid has an unsupported order or numbering",
                              kPixelCenterOperation, {StatusContextField::ofText(ContextKey::Parameter, "pixel.grid")});
    }

    const std::uint32_t fullPixelCount = healpixPixelCount(pixel.grid);
    if (pixel.value >= fullPixelCount)
    {
        return healpixFailure(ErrorCode::OutOfRange, "the HEALPix pixel index is outside its grid",
                              kPixelCenterOperation,
                              {StatusContextField::ofCount(ContextKey::Requested, pixel.value),
                               StatusContextField::ofCount(ContextKey::Limit, fullPixelCount)});
    }

    const std::uint32_t side = healpixSideCount(pixel.grid);
    const std::uint32_t ringPixel = pixel.grid.ordering == HealpixOrdering::Ring
                                        ? pixel.value
                                        : nestedToRingPixel(side, fullPixelCount, pixel.value);
    return ringPixelCenter(side, fullPixelCount, ringPixel);
}

Result<double> healpixMaximumPixelRadiusDegrees(const HealpixGrid& grid)
{
    if (!isHealpixGrid(grid))
    {
        return healpixFailure(ErrorCode::InvalidArgument, "the HEALPix grid has an unsupported order or numbering",
                              kPixelRadiusOperation, {StatusContextField::ofText(ContextKey::Parameter, "grid")});
    }

    // The construction the HEALPix reference uses for this bound: the angle between the points
    // (z = 2/3, longitude pi / (4 Nside)) and (z = 1 - (1 - 1/Nside)^2 / 3, longitude zero).
    const double side = static_cast<double>(healpixSideCount(grid));
    const double firstZ = kTwoThirds;
    const double firstLongitude = std::numbers::pi / (4.0 * side);
    const double poleStep = 1.0 - 1.0 / side;
    const double secondZ = 1.0 - (poleStep * poleStep) / 3.0;
    const double dot =
        firstZ * secondZ + std::sqrt((1.0 - firstZ * firstZ) * (1.0 - secondZ * secondZ)) * std::cos(firstLongitude);
    return std::acos(std::clamp(dot, -1.0, 1.0)) * kDegreesPerRadian;
}

Result<HealpixDiscResult> queryHealpixDisc(const ExecutionContext& execution, const HealpixGrid& grid,
                                           const HealpixDiscQuery& query)
{
    if (!execution.valid())
    {
        return healpixFailure(ErrorCode::NotInitialized, "the HEALPix query has no engine context", kDiscOperation);
    }
    if (!isHealpixGrid(grid))
    {
        return healpixFailure(ErrorCode::InvalidArgument, "the HEALPix grid has an unsupported order or numbering",
                              kDiscOperation, {StatusContextField::ofText(ContextKey::Parameter, "grid")});
    }
    if (!isSkyCoordinate(query.center))
    {
        return healpixFailure(ErrorCode::InvalidArgument, "the HEALPix cone centre is not finite and in range",
                              kDiscOperation, {StatusContextField::ofText(ContextKey::Parameter, "query.center")});
    }
    if (!std::isfinite(query.radiusDegrees) || query.radiusDegrees < 0.0 || query.radiusDegrees > 180.0)
    {
        return healpixFailure(ErrorCode::InvalidArgument, "the HEALPix cone radius must be in [0, 180] degrees",
                              kDiscOperation,
                              {StatusContextField::ofText(ContextKey::Parameter, "query.radiusDegrees")});
    }
    if (!isHealpixDiscCoverage(query.coverage))
    {
        return healpixFailure(ErrorCode::InvalidArgument, "the HEALPix cone coverage rule is unsupported",
                              kDiscOperation, {StatusContextField::ofText(ContextKey::Parameter, "query.coverage")});
    }
    if (query.maximumPixels == 0u)
    {
        return healpixFailure(ErrorCode::InvalidArgument, "a HEALPix query must permit at least one pixel",
                              kDiscOperation,
                              {StatusContextField::ofText(ContextKey::Parameter, "query.maximumPixels")});
    }
    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
    {
        return cancelled;
    }

    HealpixDiscResult result;
    const std::uint32_t side = healpixSideCount(grid);
    const std::uint32_t fullPixelCount = healpixPixelCount(grid);
    result.pixels.reserve((std::min)({query.maximumPixels, fullPixelCount, std::uint32_t{256}}));

    const auto appendRingPixel = [&](const std::uint32_t ringPixel) {
        if (result.pixels.size() >= query.maximumPixels)
        {
            return false;
        }
        const std::uint32_t value = grid.ordering == HealpixOrdering::Ring
                                        ? ringPixel
                                        : ringToNestedPixel(grid.order, side, fullPixelCount, ringPixel);
        result.pixels.push_back(HealpixPixelIndex{.grid = grid, .value = value});
        return true;
    };
    const auto limitFailure = [&]() {
        return healpixFailure(
            ErrorCode::LimitExceeded, "the HEALPix cone contains more pixels than the query permits", kDiscOperation,
            {StatusContextField::ofCount(ContextKey::Requested, static_cast<std::uint64_t>(query.maximumPixels) + 1u),
             StatusContextField::ofCount(ContextKey::Limit, query.maximumPixels)});
    };

    // A point belongs to one pixel by the same half-open convention as the scalar address
    // operation, so a zero-radius lookup answers that one pixel, directly and exactly.
    if (query.radiusDegrees == 0.0)
    {
        const std::uint32_t ringPixel = coordinateToRingPixel(side, fullPixelCount, query.center);
        result.candidateCentersExamined = 1u;
        if (!appendRingPixel(ringPixel))
        {
            return limitFailure();
        }
        return result;
    }

    double effectiveRadiusDegrees = query.radiusDegrees;
    if (query.coverage == HealpixDiscCoverage::ConservativeOverlap)
    {
        const Result<double> maximumRadius = healpixMaximumPixelRadiusDegrees(grid);
        effectiveRadiusDegrees = (std::min)(180.0, effectiveRadiusDegrees + *maximumRadius);
    }

    const double centreDeclination = query.center.declinationDegrees * kRadiansPerDegree;
    const double centreLongitude = query.center.rightAscensionDegrees * kRadiansPerDegree;
    const double radiusRadians = effectiveRadiusDegrees * kRadiansPerDegree;
    const double halfRadiusSine = std::sin(radiusRadians * 0.5);
    const double radiusHaversine = halfRadiusSine * halfRadiusSine;
    // One unit in the last place above the radius, so a centre exactly on the cone's edge is
    // inside whichever way the two haversines round.
    const double inclusiveRadiusHaversine = std::nextafter(radiusHaversine, std::numeric_limits<double>::infinity());

    const std::uint32_t ringCount = 4u * side - 1u;
    for (std::uint32_t ring = 1u; ring <= ringCount; ++ring)
    {
        if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
        {
            return cancelled;
        }
        ++result.ringsExamined;

        const RingInfo info = describeRing(side, fullPixelCount, ring);
        const double ringDeclination = std::asin(std::clamp(info.z, -1.0, 1.0));
        const double declinationHalfSine = std::sin((ringDeclination - centreDeclination) * 0.5);
        const double declinationHaversine = declinationHalfSine * declinationHalfSine;
        if (declinationHaversine > inclusiveRadiusHaversine)
        {
            continue;
        }

        const double longitudeWeight = std::cos(centreDeclination) * std::cos(ringDeclination);
        double allowedLongitudeHaversine = 1.0;
        if (longitudeWeight > 0.0)
        {
            allowedLongitudeHaversine = (inclusiveRadiusHaversine - declinationHaversine) / longitudeWeight;
            if (allowedLongitudeHaversine < 0.0)
            {
                continue;
            }
        }

        const double step = kTwoPi / static_cast<double>(info.pixelCount);
        const double offset = info.shifted ? 0.5 * step : 0.0;
        const auto testPosition = [&](const std::uint32_t position) {
            ++result.candidateCentersExamined;
            const double longitude = offset + static_cast<double>(position) * step;
            if (separationHaversine(centreDeclination, centreLongitude, ringDeclination, longitude) >
                inclusiveRadiusHaversine)
            {
                return true;
            }
            return appendRingPixel(info.firstPixel + position);
        };

        if (allowedLongitudeHaversine >= 1.0)
        {
            for (std::uint32_t position = 0u; position < info.pixelCount; ++position)
            {
                if (!testPosition(position))
                {
                    return limitFailure();
                }
            }
            continue;
        }

        const double halfLongitude = 2.0 * std::asin(std::sqrt(std::clamp(allowedLongitudeHaversine, 0.0, 1.0)));
        const double lower = (centreLongitude - halfLongitude - offset) / step;
        const double upper = (centreLongitude + halfLongitude - offset) / step;
        std::int64_t first = static_cast<std::int64_t>(std::floor(lower)) - 1;
        std::int64_t last = static_cast<std::int64_t>(std::ceil(upper)) + 1;
        if (last - first + 1 >= static_cast<std::int64_t>(info.pixelCount))
        {
            first = 0;
            last = static_cast<std::int64_t>(info.pixelCount) - 1;
        }

        for (std::int64_t unwrapped = first; unwrapped <= last; ++unwrapped)
        {
            std::int64_t wrapped = unwrapped % static_cast<std::int64_t>(info.pixelCount);
            if (wrapped < 0)
            {
                wrapped += info.pixelCount;
            }
            if (!testPosition(static_cast<std::uint32_t>(wrapped)))
            {
                return limitFailure();
            }
        }
    }

    std::sort(result.pixels.begin(), result.pixels.end(),
              [](const HealpixPixelIndex& left, const HealpixPixelIndex& right) { return left.value < right.value; });
    result.pixels.erase(std::unique(result.pixels.begin(), result.pixels.end()), result.pixels.end());
    return result;
}

} // namespace an
