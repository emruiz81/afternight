// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Shared deterministic catalog text and cone centres for the catalog comparison.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// How many rows the shared catalog text holds.
///
/// Close to the thirteen thousand rows of the deep-sky table and the nine thousand of the
/// bright-star table this engine ships, so the row is measuring the size of catalog a person
/// actually loads rather than a toy or a stress case.
inline constexpr std::size_t kCatalogFixtureEntryCount = 16384u;

/// How many cone queries one timed region asks.
inline constexpr std::size_t kCatalogFixtureQueryCount = 64u;

/// The radius, in degrees, every cone in the region scenario uses.
inline constexpr double kCatalogFixtureQueryRadiusDegrees = 10.0;

/// The faintest magnitude the region scenario admits.
inline constexpr double kCatalogFixtureMagnitudeLimit = 20.0;

/// The HEALPix order used by the provider-tile query comparison.
///
/// Order five is the shipping Gaia catalogue's tile grid, so this measures the operation at the
/// resolution a plate solve currently pays rather than at an arbitrary stress resolution.
inline constexpr std::uint8_t kHealpixFixtureOrder = 5u;

/// The radius, in degrees, of every provider-tile query.
inline constexpr double kHealpixFixtureQueryRadiusDegrees = 2.0;

/// One cone the region scenario asks about.
struct CatalogFixtureCone
{
    /// Centre right ascension, in degrees, in `[0, 360)`.
    double rightAscensionDegrees = 0.0;

    /// Centre declination, in degrees, in `[-90, 90]`.
    double declinationDegrees = 0.0;
};

/// Builds the bright-star catalog text the runner parses.
///
/// The fixture samples are the entropy source, so the runner's recorded fixture digest proves that
/// every run reads the same text. Positions are spread over the whole sphere by an
/// area-preserving mapping of the sample value, so a cone holds roughly the same number of entries
/// wherever it is centred. Magnitudes are distinct by construction: an engine may order equal
/// magnitudes by different tie-breaks, and a scenario whose answer depended on that would be
/// measuring the tie-break rather than the lookup.
///
/// @param source finite samples, at least `kCatalogFixtureEntryCount` of them.
/// @return the whole catalog text, in the seven-column bright-star layout.
[[nodiscard]] inline std::string makeCatalogFixtureText(std::span<const float> source)
{
    static const char* const kSpectralTypes[] = {"O5V", "B2III", "A0m...", "F0Ib", "G2V", "K2IIIp", "M1Ia"};

    std::string text;
    text.reserve(kCatalogFixtureEntryCount * 64u);
    text.append("# a generated bright-star table\n");
    text.append("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n");

    const std::size_t available = source.size();
    for (std::size_t index = 0u; index < kCatalogFixtureEntryCount; ++index)
    {
        const double sample = available == 0u ? 0.0 : std::abs(static_cast<double>(source[index % available]));
        const double fraction = sample - std::floor(sample);
        const double rightAscension = std::fmod(fraction * 360.0 + static_cast<double>(index) * 0.021978, 360.0);
        // An area-preserving declination: the sine of the declination is uniform, so entries are not
        // crowded at the poles and a cone holds a comparable count wherever it sits.
        const double sine = 2.0 * std::fmod(fraction * 7.0 + static_cast<double>(index % 977u) * 0.001023, 1.0) - 1.0;
        const double declination = std::asin(sine) * 180.0 / 3.14159265358979323846;
        const double magnitude = -1.5 + static_cast<double>(index) * 0.0005;

        text.append("HIP ");
        text.append(std::to_string(index));
        text.push_back(',');
        if ((index % 7u) == 0u)
        {
            text.append("Star ");
            text.append(std::to_string(index));
        }
        text.push_back(',');
        text.append("alpha ");
        text.append(std::to_string(index % 88u));
        text.push_back(',');
        text.append(std::to_string(rightAscension));
        text.push_back(',');
        text.append(std::to_string(declination));
        text.push_back(',');
        text.append(std::to_string(magnitude));
        text.push_back(',');
        text.append(kSpectralTypes[index % 7u]);
        text.push_back('\n');
    }
    return text;
}

/// The cones the region scenario asks about, spread over the whole sphere.
///
/// @return `kCatalogFixtureQueryCount` centres, the same ones in every run.
[[nodiscard]] inline std::vector<CatalogFixtureCone> makeCatalogFixtureCones()
{
    std::vector<CatalogFixtureCone> cones;
    cones.reserve(kCatalogFixtureQueryCount);
    for (std::size_t index = 0u; index < kCatalogFixtureQueryCount; ++index)
    {
        const double turn = static_cast<double>(index) / static_cast<double>(kCatalogFixtureQueryCount);
        const double sine = 2.0 * turn - 1.0 + (1.0 / static_cast<double>(kCatalogFixtureQueryCount));
        cones.push_back(CatalogFixtureCone{.rightAscensionDegrees = std::fmod(turn * 1440.0, 360.0),
                                           .declinationDegrees = std::asin(sine) * 180.0 / 3.14159265358979323846});
    }
    return cones;
}

/// The provider-tile cones, shared with the ordinary catalogue lookup scenario.
///
/// Reusing the same area-preserving set makes the HEALPix row cover the right-ascension seam and
/// both polar regions without inventing a second distribution whose two runner copies could drift.
///
/// @return `kCatalogFixtureQueryCount` centres, the same ones in every run.
[[nodiscard]] inline std::vector<CatalogFixtureCone> makeHealpixFixtureCones()
{
    return makeCatalogFixtureCones();
}

/// How many rows the shared deep-sky catalog text holds.
///
/// The shipping deep-sky table has about thirteen thousand, of which a hundred-odd are the
/// alias rows one row of the table names again under a survey designation. This is that shape.
inline constexpr std::size_t kDeepSkyFixtureEntryCount = 13000u;

/// How many of those rows are alias rows.
inline constexpr std::size_t kDeepSkyFixtureAliasCount = 110u;

/// Builds the deep-sky catalog text the runner reads.
///
/// Nine columns, every row named and every magnitude distinct and measured: a row whose place in the
/// answer depended on a tie-break or on how an unmeasured magnitude is treated would be measuring that
/// rather than the load.
/// Every alias row shares its common name with one survey row and sits half a degree from it, so
/// the alias pass has the work to do that the shipping table gives it.
///
/// @param source finite samples, at least `kDeepSkyFixtureEntryCount` of them.
/// @return the whole catalog text, in the nine-column extended-object layout.
[[nodiscard]] inline std::string makeDeepSkyFixtureText(std::span<const float> source)
{
    static const char* const kObjectTypes[] = {"Gal", "OpC", "GlC", "PN", "Neb", "SNR", "DrkN"};

    const std::size_t surveyCount = kDeepSkyFixtureEntryCount - kDeepSkyFixtureAliasCount;
    const std::size_t available = source.size();
    const auto rightAscensionAt = [&](std::size_t index) {
        const double sample = available == 0u ? 0.0 : std::abs(static_cast<double>(source[index % available]));
        const double fraction = sample - std::floor(sample);
        return std::fmod(fraction * 360.0 + static_cast<double>(index) * 0.017761, 360.0);
    };
    const auto declinationAt = [&](std::size_t index) {
        const double sample = available == 0u ? 0.0 : std::abs(static_cast<double>(source[index % available]));
        const double fraction = sample - std::floor(sample);
        // An area-preserving declination, kept a degree away from either pole so that an alias row
        // half a degree from its survey row is never clamped into a different separation.
        const double sine = 2.0 * std::fmod(fraction * 11.0 + static_cast<double>(index % 883u) * 0.001133, 1.0) - 1.0;
        const double declination = std::asin(sine) * 180.0 / 3.14159265358979323846;
        return (std::max)(-89.0, (std::min)(89.0, declination));
    };

    std::string text;
    text.reserve(kDeepSkyFixtureEntryCount * 72u);
    text.append("# a generated deep-sky table\n");
    text.append("id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,"
                "magnitude\n");

    const auto appendRow = [&](const std::string& identifier, std::size_t nameIndex, double rightAscension,
                               double declination, std::size_t magnitudeIndex) {
        const double major = 1.0 + static_cast<double>(nameIndex % 61u) * 0.5;
        text.append(identifier);
        text.append(",Object ");
        text.append(std::to_string(nameIndex));
        text.push_back(',');
        text.append(std::to_string(rightAscension));
        text.push_back(',');
        text.append(std::to_string(declination));
        text.push_back(',');
        text.append(std::to_string(major));
        text.push_back(',');
        text.append(std::to_string(major * 0.625));
        text.push_back(',');
        text.append(std::to_string(static_cast<double>(nameIndex % 180u)));
        text.push_back(',');
        text.append(kObjectTypes[nameIndex % 7u]);
        text.push_back(',');
        // Distinct by construction and far apart against a one-microdegree tie window,
        // and never at or above the value its parser reads as "nobody has measured this".
        text.append(std::to_string(1.0 + static_cast<double>(magnitudeIndex) * 0.001));
        text.push_back('\n');
    };

    for (std::size_t index = 0u; index < surveyCount; ++index)
    {
        appendRow("NGC " + std::to_string(1000u + index), index, rightAscensionAt(index), declinationAt(index), index);
    }
    for (std::size_t alias = 0u; alias < kDeepSkyFixtureAliasCount; ++alias)
    {
        const std::size_t partner = (alias * 113u) % surveyCount;
        const double declination = declinationAt(partner);
        appendRow("C " + std::to_string(alias + 1u), partner, rightAscensionAt(partner),
                  declination > 0.0 ? declination - 0.5 : declination + 0.5, surveyCount + alias);
    }
    return text;
}

} // namespace an::bench
