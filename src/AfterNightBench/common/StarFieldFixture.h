// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - a planted star field with known truth, and how a detection scores.
//
// Star detection is the one operation whose quality cannot be measured by comparing two detections
// with each other: they will disagree in the last digit of every fitted parameter, a few marginal
// stars will fall either side of a threshold, and neither list is the answer. So the synthetic row
// plants the stars first and scores the engine against the list that produced the pixels. That makes
// a difference between two runs attributable - one found a planted star and the other did not -
// rather than merely visible.
//
// This layer links no engine, so what it produces is a buffer of samples and a list of truths;
// which detector reads them is the runner's business. The scoring is here too, so a difference in
// the numbers cannot come from matching detections to truth by different rules.
//
// The real-capture rows have no truth and are not scored this way. They are scored on the median
// relative residual of the fits the engine accepted, which is a property of the engine's own
// answer and is comparable across runs because it is dimensionless.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <numbers>
#include <span>
#include <vector>

namespace an::bench
{

/// One planted star, as the fixture knows it.
struct PlantedStarTruth
{
    /// Horizontal centre, in pixels, with the first pixel's centre at one half.
    double x = 0.0;

    /// Vertical centre.
    double y = 0.0;

    /// Peak above the background.
    double amplitude = 0.0;

    /// Standard deviation along the major axis, in pixels.
    double sigmaMajor = 0.0;

    /// Standard deviation along the minor axis.
    double sigmaMinor = 0.0;

    /// Major-axis position angle, in radians.
    double angle = 0.0;

    /// The mean of the two full widths at half maximum, in pixels.
    [[nodiscard]] double meanFullWidth() const noexcept
    {
        // Two times the square root of two times the natural logarithm of two, the same constant
        // the engine's own star records carry.
        return 0.5 * (sigmaMajor + sigmaMinor) * 2.35482004503;
    }
};

/// A rendered field and the stars planted into it.
struct PlantedStarField
{
    /// The frame, row-major and tight.
    std::vector<float> samples{};

    /// What was planted.
    std::vector<PlantedStarTruth> stars{};

    /// The sky every sample sits on.
    double background = 0.0;
};

/// One detection, as a scoring reads it.
struct DetectedStarSummary
{
    /// Horizontal position in the frame, in pixels.
    double x = 0.0;

    /// Vertical position.
    double y = 0.0;

    /// The mean of the two fitted full widths, in pixels.
    double meanFullWidth = 0.0;
};

/// How one detection scored against the field that produced it.
struct PlantedStarScore
{
    /// Planted stars a detection matched.
    std::uint64_t matched = 0u;

    /// Planted stars nothing matched.
    std::uint64_t missed = 0u;

    /// Detections that matched no planted star.
    std::uint64_t spurious = 0u;

    /// Root mean square of the distance between a matched pair, in pixels.
    double positionRootMeanSquare = 0.0;

    /// Root mean square of the relative error of the matched stars' mean full widths.
    double fullWidthRootMeanSquare = 0.0;

    /// Matched over planted.
    [[nodiscard]] double recall() const noexcept
    {
        const std::uint64_t planted = matched + missed;
        return planted == 0u ? 0.0 : static_cast<double>(matched) / static_cast<double>(planted);
    }

    /// Matched over detected.
    [[nodiscard]] double precision() const noexcept
    {
        const std::uint64_t detected = matched + spurious;
        return detected == 0u ? 0.0 : static_cast<double>(matched) / static_cast<double>(detected);
    }

    /// The harmonic mean of precision and recall.
    [[nodiscard]] double harmonicMean() const noexcept
    {
        const double p = precision();
        const double r = recall();
        return p + r <= 0.0 ? 0.0 : 2.0 * p * r / (p + r);
    }

    /// What a scenario reports as its correctness metric: one minus the harmonic mean.
    ///
    /// Expressed as a shortfall rather than a score because every correctness class is evaluated
    /// as "metric at or below tolerance", so a figure that rises when the
    /// operation does better would fail the row at exactly the values that prove it worked.
    [[nodiscard]] double shortfall() const noexcept
    {
        return 1.0 - harmonicMean();
    }
};

/// One integer hash, so a field is the same field on every platform and in every run.
[[nodiscard]] inline std::uint64_t plantedStarHash(std::uint64_t value) noexcept
{
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

/// A hashed value in [0, 1).
[[nodiscard]] inline double plantedStarUnit(std::uint64_t value) noexcept
{
    return static_cast<double>(plantedStarHash(value) >> 11) / static_cast<double>(1ull << 53);
}

/// A hashed value in [low, high).
[[nodiscard]] inline double plantedStarRange(std::uint64_t value, double low, double high) noexcept
{
    return low + plantedStarUnit(value) * (high - low);
}

/// Plants and renders one field of known stars over a noisy sky.
///
/// One star per square cell, jittered inside the middle half of it, so no two land closer than half
/// a cell; cells whose star would fall within one cell of the frame border are left empty so every
/// planted star has room for the box a detector reads. The noise is a sum of twelve uniforms, which
/// is within a per cent of a normal draw over four standard deviations.
///
/// @param width frame width in pixels.
/// @param height frame height in pixels.
/// @param cell the square cell edge, in pixels, that each star gets.
/// @param seed the seed every choice derives from.
/// @return the rendered field and its truth.
[[nodiscard]] inline PlantedStarField makePlantedStarField(std::uint32_t width, std::uint32_t height,
                                                           std::uint32_t cell, std::uint64_t seed)
{
    constexpr double kBackground = 0.05;
    constexpr double kNoise = 0.0015;
    PlantedStarField field{};
    field.background = kBackground;
    field.samples.assign(static_cast<std::size_t>(width) * height, static_cast<float>(kBackground));

    const std::uint32_t cellsX = cell == 0u ? 0u : width / cell;
    const std::uint32_t cellsY = cell == 0u ? 0u : height / cell;
    const auto margin = static_cast<double>(cell);
    for (std::uint32_t cellY = 0u; cellY < cellsY; ++cellY)
    {
        for (std::uint32_t cellX = 0u; cellX < cellsX; ++cellX)
        {
            const std::uint64_t key =
                seed ^ (static_cast<std::uint64_t>(cellX) << 1u) ^ (static_cast<std::uint64_t>(cellY) << 22u);
            PlantedStarTruth star{};
            star.x = (static_cast<double>(cellX) + 0.25 + 0.5 * plantedStarUnit(key + 1u)) * cell;
            star.y = (static_cast<double>(cellY) + 0.25 + 0.5 * plantedStarUnit(key + 2u)) * cell;
            if (star.x < margin || star.y < margin || star.x > width - margin || star.y > height - margin)
            {
                continue;
            }
            star.amplitude = plantedStarRange(key + 3u, 0.08, 0.6);
            star.sigmaMajor = plantedStarRange(key + 4u, 1.4, 2.6);
            star.sigmaMinor = star.sigmaMajor * plantedStarRange(key + 5u, 0.75, 1.0);
            star.angle = plantedStarRange(key + 6u, 0.0, std::numbers::pi);
            field.stars.push_back(star);
        }
    }

    for (const PlantedStarTruth& star : field.stars)
    {
        const double reach = 5.0 * star.sigmaMajor;
        const auto left = static_cast<std::int64_t>(std::floor(star.x - reach));
        const auto right = static_cast<std::int64_t>(std::ceil(star.x + reach));
        const auto top = static_cast<std::int64_t>(std::floor(star.y - reach));
        const auto bottom = static_cast<std::int64_t>(std::ceil(star.y + reach));
        const double cosine = std::cos(star.angle);
        const double sine = std::sin(star.angle);
        const double inverseMajor = 1.0 / (2.0 * star.sigmaMajor * star.sigmaMajor);
        const double inverseMinor = 1.0 / (2.0 * star.sigmaMinor * star.sigmaMinor);
        for (std::int64_t row = top; row <= bottom; ++row)
        {
            if (row < 0 || row >= static_cast<std::int64_t>(height))
            {
                continue;
            }
            for (std::int64_t column = left; column <= right; ++column)
            {
                if (column < 0 || column >= static_cast<std::int64_t>(width))
                {
                    continue;
                }
                const double offsetX = static_cast<double>(column) + 0.5 - star.x;
                const double offsetY = static_cast<double>(row) + 0.5 - star.y;
                const double alongMajor = cosine * offsetX - sine * offsetY;
                const double alongMinor = sine * offsetX + cosine * offsetY;
                field.samples[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(column)] +=
                    static_cast<float>(star.amplitude * std::exp(-(alongMajor * alongMajor * inverseMajor +
                                                                   alongMinor * alongMinor * inverseMinor)));
            }
        }
    }

    for (std::size_t index = 0u; index < field.samples.size(); ++index)
    {
        double sum = 0.0;
        for (std::uint64_t draw = 0u; draw < 12u; ++draw)
        {
            sum += plantedStarUnit((seed * 1000003u + static_cast<std::uint64_t>(index)) * 12u + draw);
        }
        field.samples[index] += static_cast<float>(kNoise * (sum - 6.0));
    }
    return field;
}

/// The canonical text of one frame's star measurements, which a full-output digest covers.
///
/// Fixed precision and a fixed field order, written once here so two runs cannot digest the
/// same answer differently. It carries what a consumer of a star profile actually reads - how many
/// stars, the seeing, the shape, the frame quality, the signal and the sky - rather than the star
/// list itself, which two implementations will never agree on to the last digit.
///
/// @param stars how many stars were accepted.
/// @param fullWidth the median mean full width, in pixels.
/// @param roundness the median axis ratio.
/// @param quality the frame-quality figure.
/// @param signalToNoise the median amplitude over background noise.
/// @param background the measured sky.
/// @param backgroundNoise the measured noise.
/// @return the text.
[[nodiscard]] inline std::string starDetectionText(std::uint64_t stars, double fullWidth, double roundness,
                                                   double quality, double signalToNoise, double background,
                                                   double backgroundNoise)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    stream << "stars=" << stars << '\n';
    stream << "fullWidth=" << fullWidth << '\n';
    stream << "roundness=" << roundness << '\n';
    stream << "quality=" << quality << '\n';
    stream << "signalToNoise=" << signalToNoise << '\n';
    stream << "background=" << background << '\n';
    stream << "backgroundNoise=" << backgroundNoise << '\n';
    return stream.str();
}

/// The median of a run of relative fit residuals, reordering it.
///
/// What a real-capture row is scored on: the fitted residual of one star divided by its own
/// amplitude, so it is dimensionless and comparable between two builds that will never agree on
/// which stars they found. Zero when nothing was measured, which is the honest figure for a frame
/// in which no star was fitted.
///
/// @param values the residuals, which are partially reordered.
/// @return the median, or zero for an empty run.
[[nodiscard]] inline double medianRelativeResidual(std::vector<double>& values)
{
    if (values.empty())
    {
        return 0.0;
    }
    const std::size_t upper = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(upper), values.end());
    const double high = values[upper];
    if (values.size() % 2u == 1u)
    {
        return high;
    }
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(upper - 1u),
                     values.begin() + static_cast<std::ptrdiff_t>(upper));
    const double low = values[upper - 1u];
    return low + 0.5 * (high - low);
}

/// Scores a detection against the field it ran on.
///
/// Each planted star claims the nearest unclaimed detection within `radius` pixels. Greedy nearest
/// matching is enough because the field plants one star per cell and the tolerance is a small
/// fraction of a cell, so no two planted stars compete for one detection.
///
/// @param field the planted field.
/// @param detected what a detector found, in any order.
/// @param radius how far a detection may sit from a planted star and still be it, in pixels.
/// @return the score.
[[nodiscard]] inline PlantedStarScore
scorePlantedStarField(const PlantedStarField& field, std::span<const DetectedStarSummary> detected, double radius = 2.0)
{
    PlantedStarScore score{};
    std::vector<bool> claimed(detected.size(), false);
    double positionSquares = 0.0;
    double widthSquares = 0.0;
    for (const PlantedStarTruth& planted : field.stars)
    {
        std::size_t best = detected.size();
        double bestDistance = radius;
        for (std::size_t index = 0u; index < detected.size(); ++index)
        {
            if (claimed[index])
            {
                continue;
            }
            const double offsetX = detected[index].x - planted.x;
            const double offsetY = detected[index].y - planted.y;
            const double distance = std::sqrt(offsetX * offsetX + offsetY * offsetY);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = index;
            }
        }
        if (best == detected.size())
        {
            ++score.missed;
            continue;
        }
        claimed[best] = true;
        ++score.matched;
        positionSquares += bestDistance * bestDistance;
        const double relative = (detected[best].meanFullWidth - planted.meanFullWidth()) / planted.meanFullWidth();
        widthSquares += relative * relative;
    }
    for (const bool taken : claimed)
    {
        score.spurious += taken ? 0u : 1u;
    }
    if (score.matched > 0u)
    {
        const auto count = static_cast<double>(score.matched);
        score.positionRootMeanSquare = std::sqrt(positionSquares / count);
        score.fullWidthRootMeanSquare = std::sqrt(widthSquares / count);
    }
    return score;
}

} // namespace an::bench
