// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - scalar Float64 gnomonic projection and its stable inverse.

#include <AfterNightEngine/Algorithms/Astrometry/GnomonicProjection.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace an
{
namespace
{

constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;
constexpr double kHorizonDotTolerance = 32.0 * std::numeric_limits<double>::epsilon();
constexpr const char* kForwardOperation = "an::projectGnomonic";
constexpr const char* kInverseOperation = "an::inverseGnomonic";

/// A unit Cartesian direction or one of the tangent basis vectors.
struct Vector3
{
    /// First component.
    double x = 0.0;

    /// Second component.
    double y = 0.0;

    /// Third component.
    double z = 0.0;
};

/// The three orthonormal directions at one tangent point.
struct TangentBasis
{
    /// Outward normal of the tangent plane.
    Vector3 centre{};

    /// Increasing-right-ascension direction.
    Vector3 east{};

    /// Increasing-declination direction.
    Vector3 north{};
};

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @return the complete failure.
[[nodiscard]] Status projectionFailure(const ErrorCode code, const char* const message, const char* const operation)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Turns one sky coordinate into a unit Cartesian direction.
///
/// @param coordinate the already-validated direction.
/// @return its unit vector.
[[nodiscard]] Vector3 skyVector(const SkyCoordinate& coordinate) noexcept
{
    const double rightAscension = coordinate.rightAscensionDegrees * kRadiansPerDegree;
    const double declination = coordinate.declinationDegrees * kRadiansPerDegree;
    const double cosineDeclination = std::cos(declination);
    return Vector3{.x = cosineDeclination * std::cos(rightAscension),
                   .y = cosineDeclination * std::sin(rightAscension),
                   .z = std::sin(declination)};
}

/// Builds the tangent plane's right-handed orthonormal basis.
///
/// @param tangentPoint the already-validated plane centre.
/// @return the outward, eastward and northward directions.
[[nodiscard]] TangentBasis tangentBasis(const SkyCoordinate& tangentPoint) noexcept
{
    const double rightAscension = tangentPoint.rightAscensionDegrees * kRadiansPerDegree;
    const double declination = tangentPoint.declinationDegrees * kRadiansPerDegree;
    const double sineRightAscension = std::sin(rightAscension);
    const double cosineRightAscension = std::cos(rightAscension);
    const double sineDeclination = std::sin(declination);
    const double cosineDeclination = std::cos(declination);

    return TangentBasis{.centre = Vector3{.x = cosineDeclination * cosineRightAscension,
                                          .y = cosineDeclination * sineRightAscension,
                                          .z = sineDeclination},
                        .east = Vector3{.x = -sineRightAscension, .y = cosineRightAscension, .z = 0.0},
                        .north = Vector3{.x = -sineDeclination * cosineRightAscension,
                                         .y = -sineDeclination * sineRightAscension,
                                         .z = cosineDeclination}};
}

/// Scalar product of two three-dimensional vectors.
///
/// @param left one vector.
/// @param right the other.
/// @return their scalar product.
[[nodiscard]] double dot(const Vector3& left, const Vector3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

} // namespace

Result<GnomonicCoordinate> projectGnomonic(const SkyCoordinate& direction, const SkyCoordinate& tangentPoint)
{
    if (!isSkyCoordinate(direction) || !isSkyCoordinate(tangentPoint))
    {
        return projectionFailure(ErrorCode::InvalidArgument,
                                 "the projected direction and tangent point must be finite sky coordinates",
                                 kForwardOperation);
    }

    if (direction == tangentPoint)
    {
        return GnomonicCoordinate{};
    }

    const Vector3 projected = skyVector(direction);
    const TangentBasis basis = tangentBasis(tangentPoint);
    const double denominator = dot(projected, basis.centre);
    // An exact right angle can produce a small positive dot product because pi/2 and the trigonometric
    // results are rounded. Treat that numerical uncertainty as the horizon instead of dividing by it.
    if (!(denominator > kHorizonDotTolerance))
    {
        return projectionFailure(ErrorCode::OutOfRange, "the direction is on or behind the gnomonic tangent horizon",
                                 kForwardOperation);
    }

    const double xDegrees = (dot(projected, basis.east) / denominator) * kDegreesPerRadian;
    const double yDegrees = (dot(projected, basis.north) / denominator) * kDegreesPerRadian;
    if (!std::isfinite(xDegrees) || !std::isfinite(yDegrees))
    {
        return projectionFailure(ErrorCode::OutOfRange, "the direction has no finite gnomonic plane coordinate",
                                 kForwardOperation);
    }

    return GnomonicCoordinate{.xDegrees = xDegrees, .yDegrees = yDegrees};
}

Result<SkyCoordinate> inverseGnomonic(const GnomonicCoordinate& coordinate, const SkyCoordinate& tangentPoint)
{
    if (!std::isfinite(coordinate.xDegrees) || !std::isfinite(coordinate.yDegrees) || !isSkyCoordinate(tangentPoint))
    {
        return projectionFailure(ErrorCode::InvalidArgument,
                                 "the plane position and tangent point must be finite and in range", kInverseOperation);
    }

    if (coordinate.xDegrees == 0.0 && coordinate.yDegrees == 0.0)
    {
        return tangentPoint;
    }

    // The direction is `centre + x * east + y * north`. Dividing all three terms by the largest of
    // one, |x| and |y| keeps every product finite for any finite plane position, and the
    // normalisation below removes that scale again.
    const double x = coordinate.xDegrees * kRadiansPerDegree;
    const double y = coordinate.yDegrees * kRadiansPerDegree;
    const double scale = (std::max)({1.0, std::abs(x), std::abs(y)});
    const double scaledX = x / scale;
    const double scaledY = y / scale;
    const TangentBasis basis = tangentBasis(tangentPoint);

    Vector3 direction{.x = (basis.centre.x / scale) + scaledX * basis.east.x + scaledY * basis.north.x,
                      .y = (basis.centre.y / scale) + scaledX * basis.east.y + scaledY * basis.north.y,
                      .z = (basis.centre.z / scale) + scaledX * basis.east.z + scaledY * basis.north.z};
    const double length = std::hypot(std::hypot(direction.x, direction.y), direction.z);
    direction.x /= length;
    direction.y /= length;
    direction.z /= length;

    double rightAscensionDegrees = std::atan2(direction.y, direction.x) * kDegreesPerRadian;
    if (rightAscensionDegrees < 0.0)
    {
        rightAscensionDegrees += 360.0;
    }
    if (rightAscensionDegrees >= 360.0)
    {
        rightAscensionDegrees -= 360.0;
    }
    const double declinationDegrees = std::asin(std::clamp(direction.z, -1.0, 1.0)) * kDegreesPerRadian;
    return SkyCoordinate{.rightAscensionDegrees = rightAscensionDegrees, .declinationDegrees = declinationDegrees};
}

} // namespace an
