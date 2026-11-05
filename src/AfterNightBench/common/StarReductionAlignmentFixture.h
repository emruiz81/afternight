// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral star-reduction and alignment fixtures.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// The two fractional planes consumed by the direct star-reduction row.
struct StarReductionFixture
{
    /// The picture whose stars are to be reduced.
    std::vector<float> target{};

    /// A starless estimate of the same picture.
    std::vector<float> starless{};
};

/// Maps one shared entropy plane to a compatible target/starless pair.
///
/// The starless level remains below the target at every sample, and both remain strictly inside the
/// unit interval. Every run calls this function, so the benchmark cannot differ because one side
/// synthesized a different pair.
///
/// @param entropy finite fixture samples; non-finite values are treated as zero by the fixture.
/// @return the two tight Float32 planes.
[[nodiscard]] inline StarReductionFixture makeStarReductionFixture(std::span<const float> entropy)
{
    StarReductionFixture fixture;
    fixture.target.resize(entropy.size());
    fixture.starless.resize(entropy.size());
    for (std::size_t index = 0u; index < entropy.size(); ++index)
    {
        const double unit =
            std::isfinite(entropy[index]) ? std::clamp(static_cast<double>(entropy[index]), 0.0, 1.0) : 0.0;
        const double starless = 0.04 + 0.56 * unit;
        const double starSignal = 0.015 + 0.24 * static_cast<double>((index * 37u + 11u) % 251u) / 250.0;
        fixture.starless[index] = static_cast<float>(starless);
        fixture.target[index] = static_cast<float>(starless + (1.0 - starless) * starSignal);
    }
    return fixture;
}

/// The strong star-reduction equation for one sample.
///
/// @param target target sample in the unit interval.
/// @param starless corresponding starless sample.
/// @param iterations number of applications, in [1, 3].
/// @return the reduced sample.
[[nodiscard]] inline double strongStarReductionOracle(double target, double starless, std::uint32_t iterations) noexcept
{
    double value = target * target + (1.0 - target) * starless;
    for (std::uint32_t iteration = 1u; iteration < iterations; ++iteration)
    {
        value = value * value + (1.0 - value) * starless;
    }
    return std::clamp(value, 0.0, 1.0);
}

/// Largest absolute-or-relative error against the shared strong-reduction equation.
///
/// @param produced tight reduced plane.
/// @param fixture the inputs that were reduced.
/// @param iterations equation iterations.
/// @return the largest error, or the largest finite double for incompatible or non-finite output.
[[nodiscard]] inline double starReductionMaximumError(std::span<const float> produced,
                                                      const StarReductionFixture& fixture,
                                                      std::uint32_t iterations) noexcept
{
    if (produced.size() != fixture.target.size() || produced.size() != fixture.starless.size())
    {
        return (std::numeric_limits<double>::max)();
    }
    double maximum = 0.0;
    for (std::size_t index = 0u; index < produced.size(); ++index)
    {
        if (!std::isfinite(produced[index]))
        {
            return (std::numeric_limits<double>::max)();
        }
        const double expected = strongStarReductionOracle(fixture.target[index], fixture.starless[index], iterations);
        const double absolute = std::abs(static_cast<double>(produced[index]) - expected);
        const double error = std::abs(expected) < 1.0 ? absolute : absolute / std::abs(expected);
        maximum = (std::max)(maximum, error);
    }
    return maximum;
}

/// One engine-neutral star coordinate.
struct AlignmentFixturePoint
{
    /// Horizontal image position in pixels.
    double x = 0.0;

    /// Vertical image position in pixels.
    double y = 0.0;
};

/// A source field, transformed reference field and the known transform between them.
struct StarAlignmentFixture
{
    /// Source points in synthetic brightness order.
    std::vector<AlignmentFixturePoint> source{};

    /// Reference points in the same brightness order, followed by unrelated faint points.
    std::vector<AlignmentFixturePoint> reference{};

    /// Row-major homogeneous source-to-reference matrix.
    std::array<double, 9> expectedTransform{};
};

/// Builds a deterministic similarity-alignment fixture with unrelated reference points.
///
/// @param pointCount number of corresponding points.
/// @param outlierCount number of unrelated faint reference points appended after them.
/// @param seed fixture seed.
/// @return the complete field and exact transform.
[[nodiscard]] inline StarAlignmentFixture makeStarAlignmentFixture(std::uint32_t pointCount, std::uint32_t outlierCount,
                                                                   std::uint64_t seed)
{
    StarAlignmentFixture fixture;
    const double angle = 0.09 + static_cast<double>(seed % 17u) * 0.001;
    const double scale = 0.985 + static_cast<double>(seed % 11u) * 0.001;
    const double cosine = scale * std::cos(angle);
    const double sine = scale * std::sin(angle);
    fixture.expectedTransform = {cosine, -sine, 31.0, sine, cosine, -17.0, 0.0, 0.0, 1.0};
    fixture.source.reserve(pointCount);
    fixture.reference.reserve(static_cast<std::size_t>(pointCount) + outlierCount);
    for (std::uint32_t index = 0u; index < pointCount; ++index)
    {
        const AlignmentFixturePoint source{
            .x = 17.0 + static_cast<double>((index * 73u + seed % 29u) % 997u) + 0.13 * index,
            .y = 23.0 + static_cast<double>((index * 151u + seed % 31u) % 991u) + 0.07 * index};
        fixture.source.push_back(source);
        fixture.reference.push_back(
            {.x = cosine * source.x - sine * source.y + 31.0, .y = sine * source.x + cosine * source.y - 17.0});
    }
    for (std::uint32_t index = 0u; index < outlierCount; ++index)
    {
        fixture.reference.push_back({.x = 1200.0 + 71.0 * index + static_cast<double>(seed % 13u),
                                     .y = 80.0 + 113.0 * index + static_cast<double>(seed % 19u)});
    }
    return fixture;
}

/// Root-mean-square mapping error of a transform against the corresponding fixture points.
///
/// @param transform row-major homogeneous source-to-reference matrix.
/// @param fixture field whose first reference points are the truth.
/// @return residual in pixels, or the largest finite double for an invalid transform.
[[nodiscard]] inline double starAlignmentResidual(const std::array<double, 9>& transform,
                                                  const StarAlignmentFixture& fixture) noexcept
{
    if (fixture.source.empty() || fixture.reference.size() < fixture.source.size())
    {
        return (std::numeric_limits<double>::max)();
    }
    double squares = 0.0;
    for (std::size_t index = 0u; index < fixture.source.size(); ++index)
    {
        const AlignmentFixturePoint& source = fixture.source[index];
        const double denominator = transform[6] * source.x + transform[7] * source.y + transform[8];
        if (!std::isfinite(denominator) || std::abs(denominator) <= 1.0e-15)
        {
            return (std::numeric_limits<double>::max)();
        }
        const double x = (transform[0] * source.x + transform[1] * source.y + transform[2]) / denominator;
        const double y = (transform[3] * source.x + transform[4] * source.y + transform[5]) / denominator;
        if (!std::isfinite(x) || !std::isfinite(y))
        {
            return (std::numeric_limits<double>::max)();
        }
        const double dx = x - fixture.reference[index].x;
        const double dy = y - fixture.reference[index].y;
        squares += dx * dx + dy * dy;
    }
    return std::sqrt(squares / static_cast<double>(fixture.source.size()));
}

/// Canonical fixture text used for its cross-runner digest.
///
/// @param fixture field to serialize.
/// @return fixed-order, round-trip-precision coordinates and expected matrix.
[[nodiscard]] inline std::string starAlignmentFixtureText(const StarAlignmentFixture& fixture)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    for (double value : fixture.expectedTransform)
    {
        stream << "m=" << value << '\n';
    }
    for (const AlignmentFixturePoint& point : fixture.source)
    {
        stream << "s=" << point.x << ',' << point.y << '\n';
    }
    for (const AlignmentFixturePoint& point : fixture.reference)
    {
        stream << "r=" << point.x << ',' << point.y << '\n';
    }
    return stream.str();
}

/// Canonical transform text used for a complete output digest.
///
/// @param transform row-major homogeneous matrix.
/// @param matches accepted point-pair count.
/// @param residual mapping residual against known truth.
/// @return fixed-order, round-trip-precision result text.
[[nodiscard]] inline std::string starAlignmentResultText(const std::array<double, 9>& transform, std::uint64_t matches,
                                                         double residual)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    for (double value : transform)
    {
        stream << "m=" << value << '\n';
    }
    stream << "matches=" << matches << '\n';
    stream << "residual=" << residual << '\n';
    return stream.str();
}

} // namespace an::bench
