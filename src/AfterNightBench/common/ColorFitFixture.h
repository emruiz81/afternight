// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - shared photometric colour samples for the gain-fit comparison.
//
// The engine adapter fits these samples. The samples are perfect under the shared colour model
// with two planted channel factors, except for a deterministic minority of gross outliers that the
// robust clipping is expected to remove, so the recovered gains are known exactly and the row can be
// held to them rather than to the engine's own answer.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an::bench
{

/// The red channel factor the samples were rendered with; a perfect fit recovers one over it.
inline constexpr double kColorFitFixtureRedFactor = 1.6;

/// The blue channel factor the samples were rendered with.
inline constexpr double kColorFitFixtureBlueFactor = 0.7;

/// Stable name of the engine-neutral sample mapping.
inline constexpr const char* kColorFitFixtureMapping = "gaia-color-samples-v1";

/// One colour sample, in engine-neutral terms.
struct ColorFitFixtureSample
{
    /// The catalog G magnitude.
    double magnitudeG = 0.0;

    /// The BP minus RP colour index, or absent for a star the catalog has no colour for.
    std::optional<double> colorIndexBpMinusRp{};

    /// Instrumental red, green and blue fluxes.
    double redFlux = 0.0;
    double greenFlux = 0.0;
    double blueFlux = 0.0;
};

/// Derives colour samples from a benchmark fixture both adapters already share.
///
/// Using the fixture samples as the entropy source makes its recorded digest prove the identity of
/// the samples too. Every eleventh sample carries no colour index; every seventeenth is a gross red
/// outlier and every nineteenth a gross blue one.
///
/// @param source finite normalized samples, one per colour sample.
/// @return one deterministic colour sample per source sample, in the same order.
[[nodiscard]] inline std::vector<ColorFitFixtureSample> makeColorFitFixture(std::span<const float> source)
{
    std::vector<ColorFitFixtureSample> result(source.size());
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        const double value = std::abs(static_cast<double>(source[index]));
        ColorFitFixtureSample& sample = result[index];
        sample.magnitudeG = 9.0 + std::fmod(value * 3.7 + static_cast<double>(index % 97u) * 0.03, 4.0);
        if (index % 11u != 0u)
        {
            sample.colorIndexBpMinusRp = -0.3 + std::fmod(value * 2.9 + static_cast<double>(index % 53u) * 0.05, 2.8);
        }
        const double color = sample.colorIndexBpMinusRp.value_or(0.0);
        const double clamped = color < -0.5 ? -0.5 : (color > 3.5 ? 3.5 : color);
        sample.greenFlux = 20000.0 - 3.0 * static_cast<double>(index % 4001u);
        sample.redFlux = sample.greenFlux * std::exp(0.32 * clamped) * kColorFitFixtureRedFactor;
        sample.blueFlux = sample.greenFlux * std::exp(-0.40 * clamped) * kColorFitFixtureBlueFactor;
        if (index % 17u == 0u)
        {
            sample.redFlux *= 3.0;
        }
        if (index % 19u == 0u)
        {
            sample.blueFlux *= 0.25;
        }
    }
    return result;
}

/// How far recovered gains sit from the planted answer.
///
/// @param redGain the fitted red gain.
/// @param blueGain the fitted blue gain.
/// @return the larger relative distance of the two products from one.
[[nodiscard]] inline double scoreColorFit(double redGain, double blueGain) noexcept
{
    const double red = std::abs(redGain * kColorFitFixtureRedFactor - 1.0);
    const double blue = std::abs(blueGain * kColorFitFixtureBlueFactor - 1.0);
    return red > blue ? red : blue;
}

} // namespace an::bench
