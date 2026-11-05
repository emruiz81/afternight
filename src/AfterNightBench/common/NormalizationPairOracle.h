// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The global normalization of one captured light to another, written from its definition.
//
// A match is one scale and one offset that take the target's samples onto the reference's level: the
// additive method moves the target's median onto the reference's, the multiplicative one scales it there,
// and the additive scaling method scales the target's spread onto the reference's and then moves its
// median there. Each frame is measured over its own samples that are data - an exact zero is none - by
// the exact median and the exact median of the absolute deviations from it. A zero spread or a zero
// target median leaves the factor at one, as the engine defines it. A normalized sample is the
// target's sample times the scale plus the offset, and a zero stays a zero.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace an::bench
{

/// One frame's location and spread, over the samples that are data.
struct NormalizationPairLevel
{
    /// The exact median.
    double median = 0.0;

    /// The exact median of the absolute deviations from it.
    double deviation = 0.0;

    /// How many samples were data.
    std::uint64_t samples = 0u;
};

/// The match a method makes.
struct NormalizationPairMatch
{
    double scale = 1.0;
    double offset = 0.0;
};

/// The exact level of a frame's non-zero, finite samples.
///
/// @param samples the frame.
/// @return the level; a frame with no data has no samples and a zero level.
[[nodiscard]] NormalizationPairLevel normalizationPairLevel(std::span<const float> samples);

/// The match a method makes between two levels.
///
/// @param method "Additive", "Multiplicative" or "AdditiveScaling".
/// @param reference the reference's level.
/// @param target the target's level.
/// @return the match, or nothing for a method this definition does not name.
[[nodiscard]] std::optional<NormalizationPairMatch> normalizationPairMatch(std::string_view method,
                                                                           const NormalizationPairLevel& reference,
                                                                           const NormalizationPairLevel& target);

/// How far a normalized target is from the definition's.
///
/// Each sample is held to the target's times the match's scale plus its offset - absolutely below one,
/// relatively above - and a zero of the target must stay a zero. A sample that lands on zero may be written
/// as zero or moved to the smallest normal magnitude; both are accepted.
///
/// @param normalized the target after normalization.
/// @param target the target before it.
/// @param match the definition's match.
/// @return the largest error, or the largest finite double when a zero moved, a data sample is not
///     finite or the lengths differ.
[[nodiscard]] double normalizationPairError(std::span<const float> normalized, std::span<const float> target,
                                            const NormalizationPairMatch& match);

} // namespace an::bench
