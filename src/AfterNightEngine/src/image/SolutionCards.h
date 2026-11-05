// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the cards that state an image's astrometric solution in XISF's vocabulary.
//
// A solution reaches an image's cards in two vocabularies: the FITS world-coordinate keywords the card bridge reads and
// writes (`isWcsKeyword`), and the properties of XISF's AstrometricSolution namespace, beside which PixInsight keeps
// its own properties of the same solution - how it was generated and the grids it is evaluated through. Whatever
// withdraws or replaces a solution, or keeps one source's when it combines several, takes the cards of both, so an
// image never carries one solution in one vocabulary and another in the other.

#pragma once

#include <AfterNightEngine/Image/CodecMetadata.h>

#include <string_view>

namespace an
{
/// The namespace XISF defines for an image's astrometric solution.
inline constexpr std::string_view kXisfSolutionPrefix = "AstrometricSolution:";

/// PixInsight's own namespace for the same solution.
inline constexpr std::string_view kPixInsightSolutionPrefix = "PCL:AstrometricSolution:";

/// Whether a card is a property of an XISF image's astrometric solution, in XISF's namespace or PixInsight's.
///
/// Thread-safe and allocation-free.
///
/// @param card the card to classify.
/// @return true for an XISF card whose keyword begins with either prefix.
[[nodiscard]] inline bool isXisfSolutionCard(const CodecCard& card) noexcept
{
    const std::string_view keyword = card.keyword();
    return card.origin() == CodecNamespace::Xisf &&
           (keyword.starts_with(kXisfSolutionPrefix) || keyword.starts_with(kPixInsightSolutionPrefix));
}
} // namespace an
