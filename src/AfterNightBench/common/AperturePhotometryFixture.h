// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - planted RGB field and enclosed-flux oracle for the aperture
// photometry rows.
//
// The runner measures the planted stars on three planes with fixed radii. Coordinates in the fixture
// use the convention in which the first sample centre is 0.5, and the fluxes are judged as estimates
// of the analytic enclosed Gaussian flux rather than as digests.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace an::bench
{

/// Width of the aperture-photometry fixture frame, in pixels.
inline constexpr std::uint32_t kAperturePhotometryFixtureWidth = 1024u;

/// Height of the aperture-photometry fixture frame, in pixels.
inline constexpr std::uint32_t kAperturePhotometryFixtureHeight = 1024u;

/// Cell edge, in pixels; one star is planted per cell.
inline constexpr std::uint32_t kAperturePhotometryFixtureCell = 32u;

/// The fixed aperture radius the engine measures with, in pixels.
inline constexpr double kAperturePhotometryFixtureApertureRadius = 4.0;

/// The fixed annulus inner radius, in pixels.
inline constexpr double kAperturePhotometryFixtureAnnulusInner = 8.0;

/// The fixed annulus outer radius, in pixels.
inline constexpr double kAperturePhotometryFixtureAnnulusOuter = 14.0;

/// Stable name of the engine-neutral planting and scoring mapping.
inline constexpr const char* kAperturePhotometryFixtureMapping = "planted-rgb-aperture-v1";

/// One planted star of the aperture fixture.
struct AperturePhotometryFixtureStar
{
    /// Horizontal centre, in pixels, with the first sample centre at 0.5.
    double x = 0.0;

    /// Vertical centre.
    double y = 0.0;

    /// The round Gaussian's standard deviation, in pixels.
    double sigma = 1.6;

    /// Peak above the background per channel: red, green, blue.
    double amplitude[3] = {0.0, 0.0, 0.0};

    /// The full width at half maximum the engine is told, in pixels.
    [[nodiscard]] double fullWidth() const noexcept
    {
        return sigma * 2.35482004503;
    }

    /// The analytic flux of one channel inside the fixed aperture.
    ///
    /// `A * 2 * pi * sigma^2 * (1 - exp(-r^2 / (2 sigma^2)))`, which is what an exact integration of
    /// the planted Gaussian over the aperture would return.
    [[nodiscard]] double enclosedFlux(std::size_t channel) const noexcept
    {
        const double radius = kAperturePhotometryFixtureApertureRadius;
        return amplitude[channel] * 2.0 * std::numbers::pi * sigma * sigma *
               (1.0 - std::exp(-(radius * radius) / (2.0 * sigma * sigma)));
    }
};

/// The whole fixture: three tight planes and the stars planted into them.
struct AperturePhotometryFixture
{
    /// Red, green and blue planes, each tight and `width * height` long, concatenated.
    std::vector<float> samples{};

    /// The planted stars, in planting order.
    std::vector<AperturePhotometryFixtureStar> stars{};

    /// Per-channel sky level.
    double background[3] = {0.050, 0.062, 0.041};
};

/// One integer hash, so a field is the same field on every platform.
[[nodiscard]] inline std::uint64_t apertureFixtureHash(std::uint64_t value) noexcept
{
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

/// A hashed value in [low, high).
[[nodiscard]] inline double apertureFixtureRange(std::uint64_t value, double low, double high) noexcept
{
    return low +
           (static_cast<double>(apertureFixtureHash(value) >> 11) / static_cast<double>(1ull << 53)) * (high - low);
}

/// Plants one round Gaussian per cell on three planes with a smooth per-channel colour.
///
/// @param seed the scenario seed every choice is derived from.
/// @return the planes and the stars.
[[nodiscard]] inline AperturePhotometryFixture makeAperturePhotometryFixture(std::uint64_t seed)
{
    constexpr std::uint32_t width = kAperturePhotometryFixtureWidth;
    constexpr std::uint32_t height = kAperturePhotometryFixtureHeight;
    constexpr std::uint32_t cell = kAperturePhotometryFixtureCell;
    const std::size_t planeSize = static_cast<std::size_t>(width) * height;
    AperturePhotometryFixture fixture;
    fixture.samples.assign(planeSize * 3u, 0.0f);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::size_t index = 0u; index < planeSize; ++index)
        {
            fixture.samples[channel * planeSize + index] = static_cast<float>(fixture.background[channel]);
        }
    }
    const std::uint32_t cellsX = width / cell;
    const std::uint32_t cellsY = height / cell;
    for (std::uint32_t cellY = 1u; cellY + 1u < cellsY; ++cellY)
    {
        for (std::uint32_t cellX = 1u; cellX + 1u < cellsX; ++cellX)
        {
            const std::uint64_t key = seed * 1000003u + (static_cast<std::uint64_t>(cellY) << 20u) + cellX;
            AperturePhotometryFixtureStar star;
            star.x = (static_cast<double>(cellX) + 0.35 + 0.3 * apertureFixtureRange(key + 1u, 0.0, 1.0)) * cell;
            star.y = (static_cast<double>(cellY) + 0.35 + 0.3 * apertureFixtureRange(key + 2u, 0.0, 1.0)) * cell;
            star.sigma = apertureFixtureRange(key + 3u, 1.3, 2.1);
            const double green = apertureFixtureRange(key + 4u, 0.08, 0.5);
            const double color = apertureFixtureRange(key + 5u, -0.3, 2.5);
            star.amplitude[0] = green * std::exp(0.32 * color);
            star.amplitude[1] = green;
            star.amplitude[2] = green * std::exp(-0.40 * color);
            fixture.stars.push_back(star);
        }
    }
    for (const AperturePhotometryFixtureStar& star : fixture.stars)
    {
        const double reach = 5.0 * star.sigma;
        const auto left = static_cast<std::int64_t>(std::floor(star.x - reach));
        const auto right = static_cast<std::int64_t>(std::ceil(star.x + reach));
        const auto top = static_cast<std::int64_t>(std::floor(star.y - reach));
        const auto bottom = static_cast<std::int64_t>(std::ceil(star.y + reach));
        const double inverse = 1.0 / (2.0 * star.sigma * star.sigma);
        for (std::int64_t row = (std::max)(std::int64_t{0}, top);
             row <= (std::min)(static_cast<std::int64_t>(height) - 1, bottom); ++row)
        {
            for (std::int64_t column = (std::max)(std::int64_t{0}, left);
                 column <= (std::min)(static_cast<std::int64_t>(width) - 1, right); ++column)
            {
                const double dx = static_cast<double>(column) + 0.5 - star.x;
                const double dy = static_cast<double>(row) + 0.5 - star.y;
                const double shape = std::exp(-(dx * dx + dy * dy) * inverse);
                const std::size_t at = static_cast<std::size_t>(row) * width + static_cast<std::size_t>(column);
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    fixture.samples[channel * planeSize + at] += static_cast<float>(star.amplitude[channel] * shape);
                }
            }
        }
    }
    return fixture;
}

/// The mean relative distance between measured fluxes and the analytic enclosed fluxes.
///
/// @param fixture the planted field.
/// @param measuredFlux `3 * stars` measured fluxes, star-major, red then green then blue.
/// @param accepted one flag per star saying whether it was measured at all.
/// @return the mean over accepted stars and channels, or one when nothing was accepted.
[[nodiscard]] inline double scoreAperturePhotometry(const AperturePhotometryFixture& fixture,
                                                    const std::vector<double>& measuredFlux,
                                                    const std::vector<bool>& accepted)
{
    double sum = 0.0;
    std::size_t count = 0u;
    for (std::size_t index = 0u; index < fixture.stars.size() && index < accepted.size(); ++index)
    {
        if (!accepted[index])
        {
            continue;
        }
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            const double expected = fixture.stars[index].enclosedFlux(channel);
            sum += std::abs(measuredFlux[index * 3u + channel] / expected - 1.0);
            ++count;
        }
    }
    return count == 0u ? 1.0 : sum / static_cast<double>(count);
}

} // namespace an::bench
