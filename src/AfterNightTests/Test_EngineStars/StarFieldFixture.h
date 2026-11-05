// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A synthetic star field with known truth, and the scoring a detection is held to against it.
//
// The accuracy cases of this suite need something an oracle over a single operation cannot give
// them: a frame whose stars are known before anything looks for them. So the field is planted here
// - positions, amplitudes, widths and angle all chosen from a seeded hash - rendered as the same
// elliptical Gaussian the fit is written against, and the detection is then scored against the list
// that produced it rather than against another detector.
//
// Everything here is deterministic and platform-independent: the hash is integer arithmetic, the
// render evaluates the standard library's exponential at points derived from it, and no result
// depends on the order in which the stars were drawn.

#pragma once

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace an::test
{

/// One planted star, as the field knows it.
struct PlantedStar
{
    /// Horizontal centre in the frame, in pixels, with the first pixel's centre at one half.
    double x = 0.0;

    /// Vertical centre in the frame.
    double y = 0.0;

    /// Peak above the background.
    double amplitude = 0.0;

    /// Standard deviation along the major axis, in pixels.
    double sigmaMajor = 0.0;

    /// Standard deviation along the minor axis.
    double sigmaMinor = 0.0;

    /// Major-axis position angle, in radians.
    double angle = 0.0;

    /// The major-axis full width at half maximum this star was drawn with.
    [[nodiscard]] double fullWidthMajor() const noexcept
    {
        return sigmaMajor * kGaussianFullWidthPerSigma;
    }

    /// The minor-axis full width at half maximum.
    [[nodiscard]] double fullWidthMinor() const noexcept
    {
        return sigmaMinor * kGaussianFullWidthPerSigma;
    }

    /// The mean of the two full widths, which is what a detection's metric reports.
    [[nodiscard]] double meanFullWidth() const noexcept
    {
        return 0.5 * (fullWidthMajor() + fullWidthMinor());
    }
};

/// What a field was asked to look like.
struct StarFieldRequest
{
    /// Frame width in pixels.
    std::uint32_t width = 512u;

    /// Frame height in pixels.
    std::uint32_t height = 512u;

    /// How many pixels of frame each planted star gets, as a square cell edge.
    std::uint32_t cell = 48u;

    /// The sky level every sample sits on.
    double background = 0.05;

    /// The standard deviation of the noise added to every sample.
    double noise = 0.0015;

    /// The smallest planted amplitude.
    double minimumAmplitude = 0.08;

    /// The largest planted amplitude.
    double maximumAmplitude = 0.6;

    /// The smallest planted major-axis standard deviation, in pixels.
    double minimumSigma = 1.4;

    /// The largest planted major-axis standard deviation.
    double maximumSigma = 2.6;

    /// The smallest axis ratio a planted star may have.
    double minimumRoundness = 0.75;

    /// How many single-sample defects to scatter over the frame.
    ///
    /// A hot pixel is what a detector must not report as a star: one sample far above the sky with
    /// no profile under it. A surface smoothed too little turns each of them into a local maximum.
    std::uint32_t hotPixels = 0u;

    /// The amplitude of the smooth structure the stars sit on, as a fraction of full scale.
    ///
    /// Nebulosity: a sum of a few low-frequency sinusoids. It is what makes a real frame harder
    /// than a flat sky, because its own gradients produce broad local maxima that a narrow
    /// smoothing kernel cannot tell from a faint star.
    double nebulosity = 0.0;

    /// The seed every choice is derived from.
    std::uint64_t seed = 7u;
};

/// A rendered field and the stars that were planted into it.
struct StarField
{
    /// The frame, row-major and tight.
    std::vector<float> samples{};

    /// What was planted.
    std::vector<PlantedStar> stars{};

    /// The request that produced it.
    StarFieldRequest request{};
};

/// One integer hash, so a field is the same field on every platform.
[[nodiscard]] inline std::uint64_t fieldHash(std::uint64_t value) noexcept
{
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

/// A hashed value in [0, 1).
[[nodiscard]] inline double fieldUnit(std::uint64_t value) noexcept
{
    return static_cast<double>(fieldHash(value) >> 11) / static_cast<double>(1ull << 53);
}

/// A hashed value in [low, high).
[[nodiscard]] inline double fieldRange(std::uint64_t value, double low, double high) noexcept
{
    return low + fieldUnit(value) * (high - low);
}

/// A zero-mean unit-variance draw, from twelve uniforms.
///
/// The sum of twelve uniforms minus six is within a per cent of a normal draw over four standard
/// deviations, which is far closer than any noise model a fixture needs and costs nothing but
/// twelve hashes.
[[nodiscard]] inline double fieldNormal(std::uint64_t value) noexcept
{
    double sum = 0.0;
    for (std::uint64_t index = 0u; index < 12u; ++index)
    {
        sum += fieldUnit(value * 12u + index);
    }
    return sum - 6.0;
}

/// Plants and renders one field.
///
/// One star per cell, jittered inside the middle half of it so no two stars land closer than half a
/// cell and none lands on a frame edge. Cells whose star would fall within two cell edges of the
/// frame border are left empty, so every planted star has room for the box a detector will read.
[[nodiscard]] inline StarField makeStarField(const StarFieldRequest& request)
{
    StarField field{};
    field.request = request;
    field.samples.assign(static_cast<std::size_t>(request.width) * request.height,
                         static_cast<float>(request.background));

    const std::uint32_t cellsX = request.width / request.cell;
    const std::uint32_t cellsY = request.height / request.cell;
    const double margin = static_cast<double>(request.cell);
    for (std::uint32_t cellY = 0u; cellY < cellsY; ++cellY)
    {
        for (std::uint32_t cellX = 0u; cellX < cellsX; ++cellX)
        {
            const std::uint64_t key =
                request.seed ^ (static_cast<std::uint64_t>(cellX) << 1u) ^ (static_cast<std::uint64_t>(cellY) << 22u);
            PlantedStar star{};
            star.x = (static_cast<double>(cellX) + 0.25 + 0.5 * fieldUnit(key + 1u)) * request.cell;
            star.y = (static_cast<double>(cellY) + 0.25 + 0.5 * fieldUnit(key + 2u)) * request.cell;
            if (star.x < margin || star.y < margin || star.x > request.width - margin ||
                star.y > request.height - margin)
            {
                continue;
            }
            star.amplitude = fieldRange(key + 3u, request.minimumAmplitude, request.maximumAmplitude);
            star.sigmaMajor = fieldRange(key + 4u, request.minimumSigma, request.maximumSigma);
            star.sigmaMinor = star.sigmaMajor * fieldRange(key + 5u, request.minimumRoundness, 1.0);
            star.angle = fieldRange(key + 6u, 0.0, std::numbers::pi);
            field.stars.push_back(star);
        }
    }

    for (const PlantedStar& star : field.stars)
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
        for (std::int64_t row = (std::max)(std::int64_t{0}, top);
             row <= (std::min)(static_cast<std::int64_t>(request.height) - 1, bottom); ++row)
        {
            for (std::int64_t column = (std::max)(std::int64_t{0}, left);
                 column <= (std::min)(static_cast<std::int64_t>(request.width) - 1, right); ++column)
            {
                // Sample at the pixel's centre, which is the convention the fit measures in.
                const double offsetX = static_cast<double>(column) + 0.5 - star.x;
                const double offsetY = static_cast<double>(row) + 0.5 - star.y;
                const double alongMajor = cosine * offsetX - sine * offsetY;
                const double alongMinor = sine * offsetX + cosine * offsetY;
                const double value =
                    star.amplitude *
                    std::exp(-(alongMajor * alongMajor * inverseMajor + alongMinor * alongMinor * inverseMinor));
                field.samples[static_cast<std::size_t>(row) * request.width + static_cast<std::size_t>(column)] +=
                    static_cast<float>(value);
            }
        }
    }

    if (request.nebulosity > 0.0)
    {
        for (std::uint32_t row = 0u; row < request.height; ++row)
        {
            for (std::uint32_t column = 0u; column < request.width; ++column)
            {
                const double u = static_cast<double>(column) / static_cast<double>(request.width);
                const double v = static_cast<double>(row) / static_cast<double>(request.height);
                const double structure = 0.5 * std::sin(6.0 * u + 1.1) * std::cos(4.0 * v + 0.3) +
                                         0.3 * std::sin(13.0 * v - 0.7) * std::cos(11.0 * u + 2.2) +
                                         0.2 * std::sin(23.0 * (u + v));
                field.samples[static_cast<std::size_t>(row) * request.width + column] +=
                    static_cast<float>(request.nebulosity * (structure + 1.0) * 0.5);
            }
        }
    }

    if (request.noise > 0.0)
    {
        for (std::size_t index = 0u; index < field.samples.size(); ++index)
        {
            field.samples[index] += static_cast<float>(
                request.noise * fieldNormal(request.seed * 1000003u + static_cast<std::uint64_t>(index)));
        }
    }
    for (std::uint32_t index = 0u; index < request.hotPixels; ++index)
    {
        const std::uint64_t key = request.seed * 7919u + index;
        const auto column = static_cast<std::uint32_t>(fieldUnit(key + 1u) * (request.width - 32u)) + 16u;
        const auto row = static_cast<std::uint32_t>(fieldUnit(key + 2u) * (request.height - 32u)) + 16u;
        const double level = fieldRange(key + 3u, 0.05, 0.9);
        field.samples[static_cast<std::size_t>(row) * request.width + column] += static_cast<float>(level);
    }
    return field;
}

/// How a detection scored against the field that produced it.
struct StarFieldScore
{
    /// Planted stars a detection matched.
    std::size_t matched = 0u;

    /// Planted stars nothing matched.
    std::size_t missed = 0u;

    /// Detections that matched no planted star.
    std::size_t spurious = 0u;

    /// Root mean square of the distance between a matched pair, in pixels.
    double positionRootMeanSquare = 0.0;

    /// Largest distance between a matched pair, in pixels.
    double positionWorst = 0.0;

    /// Mean of the signed relative error of the matched stars' mean full widths.
    double fullWidthBias = 0.0;

    /// Root mean square of that relative error.
    double fullWidthRootMeanSquare = 0.0;

    /// Matched over planted.
    [[nodiscard]] double recall() const noexcept
    {
        const std::size_t planted = matched + missed;
        return planted == 0u ? 0.0 : static_cast<double>(matched) / static_cast<double>(planted);
    }

    /// Matched over detected.
    [[nodiscard]] double precision() const noexcept
    {
        const std::size_t detected = matched + spurious;
        return detected == 0u ? 0.0 : static_cast<double>(matched) / static_cast<double>(detected);
    }
};

/// Scores a detection against the field it ran on.
///
/// Each planted star claims the nearest unclaimed detection within `radius` pixels. Greedy nearest
/// matching is enough because the field plants one star per cell and the tolerance is a small
/// fraction of a cell, so no two planted stars compete for one detection.
[[nodiscard]] inline StarFieldScore scoreStarField(const StarField& field, std::span<const DetectedStar> detected,
                                                   double radius = 2.0)
{
    StarFieldScore score{};
    std::vector<bool> claimed(detected.size(), false);
    double positionSquares = 0.0;
    double widthSum = 0.0;
    double widthSquares = 0.0;
    for (const PlantedStar& planted : field.stars)
    {
        std::size_t best = detected.size();
        double bestDistance = radius;
        for (std::size_t index = 0u; index < detected.size(); ++index)
        {
            if (claimed[index])
            {
                continue;
            }
            const double offsetX = detected[index].positionX - planted.x;
            const double offsetY = detected[index].positionY - planted.y;
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
        score.positionWorst = (std::max)(score.positionWorst, bestDistance);
        const double relative =
            (detected[best].metrics.meanFullWidth - planted.meanFullWidth()) / planted.meanFullWidth();
        widthSum += relative;
        widthSquares += relative * relative;
    }
    score.spurious = static_cast<std::size_t>(std::count(claimed.begin(), claimed.end(), false));
    if (score.matched > 0u)
    {
        const auto count = static_cast<double>(score.matched);
        score.positionRootMeanSquare = std::sqrt(positionSquares / count);
        score.fullWidthBias = widthSum / count;
        score.fullWidthRootMeanSquare = std::sqrt(widthSquares / count);
    }
    return score;
}

} // namespace an::test
