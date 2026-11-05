// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definition of a process run on the CIE lightness of an sRGB-encoded colour frame.
#pragma once

#include "Results.h"
#include "Scenario.h"

#include <array>
#include <cstdint>
#include <span>

namespace an::bench
{

/// One sRGB-encoded colour, its L*a*b* lightness raised to `1 / gamma` in the unit-interval packing `L / 100`, and the
/// colour recomposed from it - written from the definitions in `long double`: the IEC 61966-2-1 transfer, the sRGB
/// primaries' matrix derived from their chromaticities and the tabulated D65 white, and the CIE 1976 L*a*b* functions.
/// Nothing is clamped.
[[nodiscard]] std::array<long double, 3u> channelScopeLightnessGamma(const std::array<long double, 3u>& encoded,
                                                                     long double gamma);

/// True for the channel scope row.
[[nodiscard]] bool requiresChannelScopeOracle(const ScenarioSpec& scenario);

/// Holds a planar Float32 recomposition to the definition, both held to the unit interval first. Sets the
/// correctness metric to the largest absolute difference, and counts
/// the samples the definition puts outside the unit interval.
void checkChannelScopeOutput(std::span<const float> planarInput, std::uint32_t width, std::uint32_t height,
                             std::span<const float> planarOutput, double gamma, ResultRecord& result);

} // namespace an::bench
