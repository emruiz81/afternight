// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - catalog angles, separations and the canonical spelling of a designation.

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>

#include "algorithms/catalog/CatalogInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <string_view>

namespace an
{
namespace
{

using catalog_detail::isAsciiDigit;
using catalog_detail::isAsciiSpace;
using catalog_detail::startsWithFolded;
using catalog_detail::trimAscii;

constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;
constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;

/// One catalog whose designations this engine knows how to spell.
struct CatalogPrefix
{
    /// How the prefix is written in the text a person types, matched ignoring ASCII case.
    std::string_view spelling;

    /// How this engine writes it. Differs from `spelling` where a catalog has a long form.
    std::string_view canonical;

    /// Whether the catalog separates its prefix from its number with a hyphen rather than a space.
    bool hyphenSeparator;
};

/// The prefix table, in the order it is tried.
///
/// Order matters: a text beginning with `Mel` also begins with `M`, so the shorter prefix is tried
/// first and rejected because what follows it is not a number, and the longer one is reached. The
/// loop below costs one comparison per prefix and never revisits a byte.
///
/// Every prefix is written the way its catalog spells it, whatever its length, so `mel20` becomes
/// `Mel 20`, `vdb141` becomes `VdB 141` and `abell426` becomes `Abell 426` - the spellings a lookup
/// finds, in a value whose whole purpose is to be looked up.
constexpr std::array<CatalogPrefix, 24u> kCatalogPrefixes{{
    {.spelling = "M", .canonical = "M", .hyphenSeparator = false},
    {.spelling = "NGC", .canonical = "NGC", .hyphenSeparator = false},
    {.spelling = "IC", .canonical = "IC", .hyphenSeparator = false},
    {.spelling = "UGC", .canonical = "UGC", .hyphenSeparator = false},
    {.spelling = "PGC", .canonical = "PGC", .hyphenSeparator = false},
    {.spelling = "Caldwell", .canonical = "C", .hyphenSeparator = false},
    {.spelling = "Ced", .canonical = "Ced", .hyphenSeparator = false},
    {.spelling = "Lyn", .canonical = "Lyn", .hyphenSeparator = false},
    {.spelling = "Mel", .canonical = "Mel", .hyphenSeparator = false},
    {.spelling = "Cr", .canonical = "Cr", .hyphenSeparator = false},
    {.spelling = "Tr", .canonical = "Tr", .hyphenSeparator = false},
    {.spelling = "St", .canonical = "St", .hyphenSeparator = false},
    {.spelling = "Sh2", .canonical = "Sh2", .hyphenSeparator = true},
    {.spelling = "LDN", .canonical = "LDN", .hyphenSeparator = false},
    {.spelling = "LBN", .canonical = "LBN", .hyphenSeparator = false},
    {.spelling = "Abell", .canonical = "Abell", .hyphenSeparator = false},
    {.spelling = "Arp", .canonical = "Arp", .hyphenSeparator = false},
    {.spelling = "HCG", .canonical = "HCG", .hyphenSeparator = false},
    {.spelling = "VdB", .canonical = "VdB", .hyphenSeparator = false},
    {.spelling = "RCW", .canonical = "RCW", .hyphenSeparator = false},
    {.spelling = "Gum", .canonical = "Gum", .hyphenSeparator = false},
    {.spelling = "Barnard", .canonical = "Barnard", .hyphenSeparator = false},
    {.spelling = "C", .canonical = "C", .hyphenSeparator = false},
    {.spelling = "B", .canonical = "B", .hyphenSeparator = false},
}};

/// Where the number begins after a prefix, allowing the separators a person types.
///
/// @param text the trimmed designation.
/// @param position the first byte after the prefix.
/// @return the position of the first digit, or `std::string_view::npos` when no number follows.
[[nodiscard]] std::size_t numberPosition(std::string_view text, std::size_t position) noexcept
{
    while (position < text.size() && isAsciiSpace(text[position]))
    {
        ++position;
    }
    if (position < text.size() && text[position] == '-')
    {
        ++position;
    }
    while (position < text.size() && isAsciiSpace(text[position]))
    {
        ++position;
    }
    if (position >= text.size() || !isAsciiDigit(text[position]))
    {
        return std::string_view::npos;
    }
    return position;
}

} // namespace

double normalizeRightAscensionDegrees(const double rightAscensionDegrees) noexcept
{
    if (std::isnan(rightAscensionDegrees))
    {
        return rightAscensionDegrees;
    }
    double normalized = std::fmod(rightAscensionDegrees, 360.0);
    if (normalized < 0.0)
    {
        normalized += 360.0;
    }
    // A tiny negative angle folds to exactly 360 in floating point, which is outside the half-open
    // turn this function promises.
    if (normalized >= 360.0)
    {
        normalized = 0.0;
    }
    return normalized;
}

bool isSkyCoordinate(const SkyCoordinate& coordinate) noexcept
{
    return std::isfinite(coordinate.rightAscensionDegrees) && coordinate.rightAscensionDegrees >= 0.0 &&
           coordinate.rightAscensionDegrees < 360.0 && std::isfinite(coordinate.declinationDegrees) &&
           coordinate.declinationDegrees >= -90.0 && coordinate.declinationDegrees <= 90.0;
}

double angularSeparationDegrees(const SkyCoordinate& first, const SkyCoordinate& second) noexcept
{
    const double firstRightAscension = first.rightAscensionDegrees * kRadiansPerDegree;
    const double firstDeclination = first.declinationDegrees * kRadiansPerDegree;
    const double secondRightAscension = second.rightAscensionDegrees * kRadiansPerDegree;
    const double secondDeclination = second.declinationDegrees * kRadiansPerDegree;

    const double halfDeclination = std::sin((secondDeclination - firstDeclination) * 0.5);
    const double halfRightAscension = std::sin((secondRightAscension - firstRightAscension) * 0.5);
    const double haversine =
        (halfDeclination * halfDeclination) +
        (std::cos(firstDeclination) * std::cos(secondDeclination) * halfRightAscension * halfRightAscension);
    if (std::isnan(haversine))
    {
        return haversine;
    }
    const double clamped = std::clamp(haversine, 0.0, 1.0);
    return 2.0 * std::asin(std::sqrt(clamped)) * kDegreesPerRadian;
}

std::string normalizeCatalogDesignation(const std::string_view designation)
{
    const std::string_view text = trimAscii(designation);
    for (const CatalogPrefix& prefix : kCatalogPrefixes)
    {
        if (!startsWithFolded(text, prefix.spelling))
        {
            continue;
        }
        const std::size_t number = numberPosition(text, prefix.spelling.size());
        if (number == std::string_view::npos)
        {
            continue;
        }
        std::string result;
        result.reserve(prefix.canonical.size() + 1u + (text.size() - number));
        result.append(prefix.canonical);
        result.push_back(prefix.hyphenSeparator ? '-' : ' ');
        result.append(text.substr(number));
        return result;
    }
    return std::string(designation);
}

} // namespace an
