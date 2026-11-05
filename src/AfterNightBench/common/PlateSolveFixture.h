// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral hinted plate-solve fixture.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace an::bench
{

/// One shared image/catalog correspondence in the plate-solve fixture.
struct PlateSolveFixtureStar
{
    /// Observed horizontal pixel coordinate.
    double imageX = 0.0;

    /// Observed vertical pixel coordinate.
    double imageY = 0.0;

    /// Catalog right ascension in degrees.
    double rightAscensionDegrees = 0.0;

    /// Catalog declination in degrees.
    double declinationDegrees = 0.0;

    /// Catalog magnitude and brightness rank.
    double magnitude = 0.0;

    /// Stable provider identifier.
    std::uint64_t identifier = 0u;
};

/// The complete synthetic field the plate-solve row consumes.
struct PlateSolveFixture
{
    /// Image width in pixels.
    std::uint32_t width = 1024u;

    /// Image height in pixels.
    std::uint32_t height = 768u;

    /// True field-centre right ascension in degrees.
    double centerRightAscensionDegrees = 83.6331;

    /// True field-centre declination in degrees.
    double centerDeclinationDegrees = 22.0145;

    /// True square-pixel scale in arcseconds per pixel.
    double scaleArcsecPerPixel = 2.4;

    /// Corresponding image and catalog points, brightest first.
    std::vector<PlateSolveFixtureStar> stars{};
};

/// Builds the fixed rotated linear field used by the paired plate-solve scenario.
///
/// Sky directions are produced by the tangent-plane equations in this neutral fixture rather than
/// by the engine, so the engine never supplies its own answer.
///
/// @return forty-two exact correspondences spread across a 1024 by 768 frame.
[[nodiscard]] inline PlateSolveFixture makePlateSolveFixture()
{
    PlateSolveFixture fixture;
    constexpr double angle = 17.0 * std::numbers::pi / 180.0;
    const double scaleDegrees = fixture.scaleArcsecPerPixel / 3600.0;
    const double cd11 = -scaleDegrees * std::cos(angle);
    const double cd12 = scaleDegrees * std::sin(angle);
    const double cd21 = scaleDegrees * std::sin(angle);
    const double cd22 = scaleDegrees * std::cos(angle);
    const double ra0 = fixture.centerRightAscensionDegrees * std::numbers::pi / 180.0;
    const double dec0 = fixture.centerDeclinationDegrees * std::numbers::pi / 180.0;
    fixture.stars.reserve(42u);

    std::uint32_t state = 0x6d2b79f5u;
    const auto nextCoordinate = [&state]() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    for (std::uint32_t ordinal = 0u; ordinal < 42u; ++ordinal)
    {
        // Integer recurrence and fixed decimal scaling make this irregular field bit-for-bit
        // reproducible without borrowing the engine's random-number implementation. The
        // irregular geometry is intentional: a rectangular lattice admits several nearly
        // exact but incorrect affine correspondences and therefore is not a sound solver
        // fixture.
        const double imageX = 48.5 + static_cast<double>(nextCoordinate() % 928000u) / 1000.0;
        const double imageY = 48.5 + static_cast<double>(nextCoordinate() % 672000u) / 1000.0;
        const double u = imageX - 0.5 * fixture.width;
        const double v = imageY - 0.5 * fixture.height;
        const double planeX = (cd11 * u + cd12 * v) * std::numbers::pi / 180.0;
        const double planeY = (cd21 * u + cd22 * v) * std::numbers::pi / 180.0;
        const double rho = std::hypot(planeX, planeY);
        double ra = ra0;
        double dec = dec0;
        if (rho > 0.0)
        {
            const double c = std::atan(rho);
            const double sinC = std::sin(c);
            const double cosC = std::cos(c);
            dec = std::asin(cosC * std::sin(dec0) + planeY * sinC * std::cos(dec0) / rho);
            ra = ra0 + std::atan2(planeX * sinC, rho * std::cos(dec0) * cosC - planeY * std::sin(dec0) * sinC);
        }
        double raDegrees = std::fmod(ra * 180.0 / std::numbers::pi, 360.0);
        if (raDegrees < 0.0)
        {
            raDegrees += 360.0;
        }
        fixture.stars.push_back(PlateSolveFixtureStar{.imageX = imageX,
                                                      .imageY = imageY,
                                                      .rightAscensionDegrees = raDegrees,
                                                      .declinationDegrees = dec * 180.0 / std::numbers::pi,
                                                      .magnitude = 4.0 + 0.1 * ordinal,
                                                      .identifier = 1000000u + ordinal});
    }
    return fixture;
}

/// Canonical fixture text used for the cross-runner input digest.
///
/// @param fixture shared field to serialize.
/// @return fixed-order, round-trip-precision dimensions, truth, and correspondences.
[[nodiscard]] inline std::string plateSolveFixtureText(const PlateSolveFixture& fixture)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    stream << "width=" << fixture.width << '\n';
    stream << "height=" << fixture.height << '\n';
    stream << "ra=" << fixture.centerRightAscensionDegrees << '\n';
    stream << "dec=" << fixture.centerDeclinationDegrees << '\n';
    stream << "scale=" << fixture.scaleArcsecPerPixel << '\n';
    for (const PlateSolveFixtureStar& star : fixture.stars)
    {
        stream << "star=" << star.imageX << ',' << star.imageY << ',' << star.rightAscensionDegrees << ','
               << star.declinationDegrees << ',' << star.magnitude << ',' << star.identifier << '\n';
    }
    return stream.str();
}

/// Angular separation of two sky directions.
///
/// @param firstRa first right ascension in degrees.
/// @param firstDec first declination in degrees.
/// @param secondRa second right ascension in degrees.
/// @param secondDec second declination in degrees.
/// @return great-circle separation in arcseconds.
[[nodiscard]] inline double plateSolveSeparationArcsec(double firstRa, double firstDec, double secondRa,
                                                       double secondDec) noexcept
{
    const double toRadians = std::numbers::pi / 180.0;
    const double dDec = (secondDec - firstDec) * toRadians;
    const double dRa = (secondRa - firstRa) * toRadians;
    const double haversine =
        std::sin(dDec * 0.5) * std::sin(dDec * 0.5) +
        std::cos(firstDec * toRadians) * std::cos(secondDec * toRadians) * std::sin(dRa * 0.5) * std::sin(dRa * 0.5);
    return 2.0 * std::asin(std::sqrt(std::clamp(haversine, 0.0, 1.0))) * 180.0 / std::numbers::pi * 3600.0;
}

/// One solver's largest error against the shared field truth.
///
/// @param solvedRa solved field-centre right ascension in degrees.
/// @param solvedDec solved field-centre declination in degrees.
/// @param solvedScale solved scale in arcseconds per pixel.
/// @param rmsResidual solver-reported image residual in pixels.
/// @param fixture shared truth.
/// @return maximum of angular error in arcseconds, scale error, and non-negative RMS; the largest
///         finite double when any answer is non-finite.
[[nodiscard]] inline double plateSolveCorrectness(double solvedRa, double solvedDec, double solvedScale,
                                                  double rmsResidual, const PlateSolveFixture& fixture) noexcept
{
    if (!std::isfinite(solvedRa) || !std::isfinite(solvedDec) || !std::isfinite(solvedScale) ||
        !std::isfinite(rmsResidual))
    {
        return (std::numeric_limits<double>::max)();
    }
    return (std::max)({plateSolveSeparationArcsec(solvedRa, solvedDec, fixture.centerRightAscensionDegrees,
                                                  fixture.centerDeclinationDegrees),
                       std::abs(solvedScale - fixture.scaleArcsecPerPixel), (std::max)(rmsResidual, 0.0)});
}

/// Canonical output text used for one runner's complete result digest.
///
/// @param solvedRa solved centre right ascension.
/// @param solvedDec solved centre declination.
/// @param solvedScale solved scale in arcseconds per pixel.
/// @param matches accepted correspondence count.
/// @param rmsResidual final RMS in pixels.
/// @return fixed-order, round-trip-precision result text.
[[nodiscard]] inline std::string plateSolveResultText(double solvedRa, double solvedDec, double solvedScale,
                                                      std::uint64_t matches, double rmsResidual)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    stream << "ra=" << solvedRa << '\n';
    stream << "dec=" << solvedDec << '\n';
    stream << "scale=" << solvedScale << '\n';
    stream << "matches=" << matches << '\n';
    stream << "rms=" << rmsResidual << '\n';
    return stream.str();
}

} // namespace an::bench
