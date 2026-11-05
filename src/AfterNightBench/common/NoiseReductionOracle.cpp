// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "NoiseReductionOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace an::bench
{
namespace
{

/// The normalized distance between what a runner produced and what the definition says.
///
/// @param actual what the runner wrote.
/// @param expected what the definition holds.
/// @return the absolute error below one and the relative error above it.
[[nodiscard]] double normalizedError(double actual, double expected) noexcept
{
    return std::abs(actual - expected) / (std::max)(1.0, std::abs(expected));
}

/// The inside coordinate a reflect-about-the-edge border answers an outside coordinate with.
///
/// @param index the coordinate, which may be negative or beyond the axis.
/// @param size how many samples the axis holds, at least one.
/// @return the inside coordinate.
[[nodiscard]] std::int64_t reflectExcludingEdge(std::int64_t index, std::int64_t size) noexcept
{
    if (size == 1)
    {
        return 0;
    }
    const std::int64_t period = 2 * size - 2;
    std::int64_t folded = index % period;
    if (folded < 0)
    {
        folded += period;
    }
    return folded < size ? folded : period - folded;
}

/// The median of a collection, with an even population resolving to the mean of its two middles.
///
/// @param values the samples, sorted in place.
/// @return the middle value.
[[nodiscard]] double medianOfSorted(std::vector<float>& values) noexcept
{
    std::sort(values.begin(), values.end());
    const std::size_t upper = values.size() / 2u;
    return (values.size() & 1u) != 0u ? static_cast<double>(values[upper])
                                      : (static_cast<double>(values[upper - 1u]) + values[upper]) * 0.5;
}

/// Whether a picture and a probe stride describe something an oracle can walk.
///
/// @param samples the plane.
/// @param width the plane width.
/// @param height the plane height.
/// @param stride how many positions separate two probes.
/// @return true when the oracle may proceed.
[[nodiscard]] bool walkable(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                            std::uint32_t stride) noexcept
{
    return samples.size() >= static_cast<std::size_t>(width) * height && stride != 0u && width != 0u && height != 0u;
}

} // namespace

std::vector<PlaneProbe> computeIdentityOracle(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                                              std::uint32_t stride)
{
    std::vector<PlaneProbe> probes;
    if (!walkable(samples, width, height, stride))
    {
        return probes;
    }
    const std::size_t imageSamples = static_cast<std::size_t>(width) * height;
    probes.reserve(imageSamples / stride + 1u);
    for (std::size_t index = 0u; index < imageSamples; index += stride)
    {
        probes.push_back(PlaneProbe{.x = static_cast<std::uint32_t>(index % width),
                                    .y = static_cast<std::uint32_t>(index / width),
                                    .value = static_cast<double>(samples[index])});
    }
    return probes;
}

std::vector<PlaneProbe> computeChambolleOracle(std::span<const float> samples, const std::uint32_t width,
                                               const std::uint32_t height, const double weight,
                                               const std::uint32_t iterations, const std::uint32_t stride)
{
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<double> answer(count, 0.0);
    std::vector<double> divergence(count, 0.0);
    std::vector<double> dualY(count, 0.0);
    std::vector<double> dualX(count, 0.0);
    std::vector<double> gradientY(count, 0.0);
    std::vector<double> gradientX(count, 0.0);
    constexpr double kStep = 0.25;

    for (std::uint32_t iteration = 0u; iteration < iterations; ++iteration)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                if (iteration != 0u)
                {
                    double value = -(dualY[index] + dualX[index]);
                    if (y != 0u)
                    {
                        value += dualY[index - width];
                    }
                    if (x != 0u)
                    {
                        value += dualX[index - 1u];
                    }
                    divergence[index] = value;
                }
                answer[index] = static_cast<double>(samples[index]) + divergence[index];
            }
        }
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                gradientY[index] = y + 1u == height ? 0.0 : answer[index + width] - answer[index];
                gradientX[index] = x + 1u == width ? 0.0 : answer[index + 1u] - answer[index];
            }
        }
        for (std::size_t index = 0u; index < count; ++index)
        {
            const double length = std::sqrt(gradientY[index] * gradientY[index] + gradientX[index] * gradientX[index]) *
                                      (kStep / weight) +
                                  1.0;
            dualY[index] = (dualY[index] - kStep * gradientY[index]) / length;
            dualX[index] = (dualX[index] - kStep * gradientX[index]) / length;
        }
    }

    std::vector<PlaneProbe> probes;
    for (std::size_t index = 0u; index < count; index += stride)
    {
        probes.push_back(PlaneProbe{.x = static_cast<std::uint32_t>(index % width),
                                    .y = static_cast<std::uint32_t>(index / width),
                                    .value = answer[index]});
    }
    return probes;
}

double totalVariationEnergy(std::span<const float> answer, std::span<const float> source, const std::uint32_t width,
                            const std::uint32_t height, const double fidelity)
{
    double variation = 0.0;
    double distance = 0.0;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const double down = y + 1u == height ? 0.0 : static_cast<double>(answer[index + width]) - answer[index];
            const double right = x + 1u == width ? 0.0 : static_cast<double>(answer[index + 1u]) - answer[index];
            variation += std::sqrt(down * down + right * right);
            const double difference = static_cast<double>(answer[index]) - source[index];
            distance += difference * difference;
        }
    }
    return variation + 0.5 * fidelity * distance;
}

double addedNoiseVariance(std::span<const float> produced, std::span<const float> source)
{
    const std::size_t count = (std::min)(produced.size(), source.size());
    if (count == 0u)
    {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t index = 0u; index < count; ++index)
    {
        sum += static_cast<double>(produced[index]) - source[index];
    }
    const double mean = sum / static_cast<double>(count);
    double squared = 0.0;
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double centred = (static_cast<double>(produced[index]) - source[index]) - mean;
        squared += centred * centred;
    }
    return squared / static_cast<double>(count);
}

std::vector<PlaneProbe> computeBilateralOracle(std::span<const float> samples, std::uint32_t width,
                                               std::uint32_t height, std::uint32_t radius, double spatialSigma,
                                               double rangeSigma, std::uint32_t stride)
{
    std::vector<PlaneProbe> probes;
    if (!walkable(samples, width, height, stride) || !(spatialSigma > 0.0) || !(rangeSigma > 0.0))
    {
        return probes;
    }
    const std::size_t imageSamples = static_cast<std::size_t>(width) * height;
    probes.reserve(imageSamples / stride + 1u);
    const double spatialScale = -0.5 / (spatialSigma * spatialSigma);
    const double rangeScale = -0.5 / (rangeSigma * rangeSigma);
    const std::int64_t limit = static_cast<std::int64_t>(radius) * static_cast<std::int64_t>(radius);
    const std::int64_t columns = static_cast<std::int64_t>(width);
    const std::int64_t rows = static_cast<std::int64_t>(height);

    for (std::size_t index = 0u; index < imageSamples; index += stride)
    {
        const auto x = static_cast<std::int64_t>(index % width);
        const auto y = static_cast<std::int64_t>(index / width);
        const double centre = static_cast<double>(samples[index]);
        double weighted = 0.0;
        double weights = 0.0;
        for (std::int64_t dy = -static_cast<std::int64_t>(radius); dy <= static_cast<std::int64_t>(radius); ++dy)
        {
            for (std::int64_t dx = -static_cast<std::int64_t>(radius); dx <= static_cast<std::int64_t>(radius); ++dx)
            {
                const std::int64_t squared = dx * dx + dy * dy;
                if (squared > limit)
                {
                    continue;
                }
                const std::int64_t column = reflectExcludingEdge(x + dx, columns);
                const std::int64_t row = reflectExcludingEdge(y + dy, rows);
                const double neighbour = static_cast<double>(
                    samples[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(column)]);
                const double difference = neighbour - centre;
                const double weight = std::exp(static_cast<double>(squared) * spatialScale) *
                                      std::exp(difference * difference * rangeScale);
                weighted += weight * neighbour;
                weights += weight;
            }
        }
        probes.push_back(PlaneProbe{.x = static_cast<std::uint32_t>(x),
                                    .y = static_cast<std::uint32_t>(y),
                                    .value = weights > 0.0 ? weighted / weights : centre});
    }
    return probes;
}

std::vector<PlaneProbe> computeImpulseOracle(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                                             std::uint32_t radius, double highThreshold, std::uint32_t stride)
{
    std::vector<PlaneProbe> probes;
    if (!walkable(samples, width, height, stride))
    {
        return probes;
    }
    const std::size_t imageSamples = static_cast<std::size_t>(width) * height;
    probes.reserve(imageSamples / stride + 1u);
    std::vector<float> values;
    values.reserve((2u * static_cast<std::size_t>(radius) + 1u) * (2u * radius + 1u));

    for (std::size_t index = 0u; index < imageSamples; index += stride)
    {
        const auto x = static_cast<std::uint32_t>(index % width);
        const auto y = static_cast<std::uint32_t>(index / width);
        const std::uint32_t firstX = x > radius ? x - radius : 0u;
        const std::uint32_t lastX = x + (std::min)(radius, width - 1u - x);
        const std::uint32_t firstY = y > radius ? y - radius : 0u;
        const std::uint32_t lastY = y + (std::min)(radius, height - 1u - y);
        values.clear();
        for (std::uint32_t row = firstY; row <= lastY; ++row)
        {
            for (std::uint32_t column = firstX; column <= lastX; ++column)
            {
                values.push_back(samples[static_cast<std::size_t>(row) * width + column]);
            }
        }
        const double median = medianOfSorted(values);
        const double sample = static_cast<double>(samples[index]);
        const bool replaced = sample > median && sample != 0.0 && (sample - median) / std::abs(sample) > highThreshold;
        probes.push_back(PlaneProbe{.x = x, .y = y, .value = replaced ? median : sample});
    }
    return probes;
}

std::vector<RgbProbe> computeAverageNeutralOracle(std::span<const float> samples, std::uint32_t width,
                                                  std::uint32_t height, std::uint32_t stride)
{
    std::vector<RgbProbe> probes;
    const std::size_t planeSamples = static_cast<std::size_t>(width) * height;
    if (width == 0u || height == 0u || stride == 0u || samples.size() < planeSamples * 3u)
    {
        return probes;
    }
    probes.reserve(planeSamples / stride + 1u);
    for (std::size_t index = 0u; index < planeSamples; index += stride)
    {
        const double red = static_cast<double>(samples[index]);
        const double green = static_cast<double>(samples[planeSamples + index]);
        const double blue = static_cast<double>(samples[2u * planeSamples + index]);
        probes.push_back(RgbProbe{.x = static_cast<std::uint32_t>(index % width),
                                  .y = static_cast<std::uint32_t>(index / width),
                                  .value = {red, (std::min)(green, 0.5 * (red + blue)), blue}});
    }
    return probes;
}

std::vector<PlaneProbe> computeHorizontalBandingOracle(std::span<const float> samples, std::uint32_t width,
                                                       std::uint32_t height, std::uint32_t stride)
{
    std::vector<PlaneProbe> probes;
    if (!walkable(samples, width, height, stride))
    {
        return probes;
    }
    const std::size_t sampleCount = static_cast<std::size_t>(width) * height;
    std::vector<float> population(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(sampleCount));
    const double background = medianOfSorted(population);
    std::vector<double> offsets(height, 0.0);
    std::vector<float> row(width, 0.0f);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        std::copy_n(samples.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * width), width,
                    row.begin());
        offsets[y] = background - medianOfSorted(row);
    }
    const double smallest = *std::min_element(offsets.begin(), offsets.end());
    probes.reserve(sampleCount / stride + 1u);
    for (std::size_t index = 0u; index < sampleCount; index += stride)
    {
        const auto y = static_cast<std::uint32_t>(index / width);
        probes.push_back(PlaneProbe{.x = static_cast<std::uint32_t>(index % width),
                                    .y = y,
                                    .value = static_cast<double>(samples[index]) + offsets[y] - smallest});
    }
    return probes;
}

double planeProbeError(std::span<const float> produced, std::uint32_t width,
                       const std::vector<PlaneProbe>& probes) noexcept
{
    if (probes.empty() || width == 0u)
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (const PlaneProbe& probe : probes)
    {
        const std::size_t offset = static_cast<std::size_t>(probe.y) * width + probe.x;
        if (offset >= produced.size() || !std::isfinite(produced[offset]) || !std::isfinite(probe.value))
        {
            return (std::numeric_limits<double>::max)();
        }
        worst = (std::max)(worst, normalizedError(static_cast<double>(produced[offset]), probe.value));
    }
    return worst;
}

std::string planeProbeText(std::span<const float> produced, std::uint32_t width, const std::vector<PlaneProbe>& probes)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "probes=" << probes.size() << '\n';
    stream << std::setprecision(9) << std::scientific;
    for (const PlaneProbe& probe : probes)
    {
        const std::size_t offset = static_cast<std::size_t>(probe.y) * width + probe.x;
        stream << probe.x << ',' << probe.y << '=';
        stream << (offset < produced.size() ? static_cast<double>(produced[offset])
                                            : std::numeric_limits<double>::quiet_NaN());
        stream << '\n';
    }
    return stream.str();
}

double rgbProbeError(std::span<const float> produced, std::uint32_t width, std::uint32_t height,
                     const std::vector<RgbProbe>& probes) noexcept
{
    const std::size_t planeSamples = static_cast<std::size_t>(width) * height;
    if (width == 0u || height == 0u || probes.empty() || produced.size() < planeSamples * 3u)
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (const RgbProbe& probe : probes)
    {
        const std::size_t pixel = static_cast<std::size_t>(probe.y) * width + probe.x;
        if (pixel >= planeSamples)
        {
            return (std::numeric_limits<double>::max)();
        }
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            const double actual = static_cast<double>(produced[channel * planeSamples + pixel]);
            if (!std::isfinite(actual) || !std::isfinite(probe.value[channel]))
            {
                return (std::numeric_limits<double>::max)();
            }
            worst = (std::max)(worst, normalizedError(actual, probe.value[channel]));
        }
    }
    return worst;
}

std::string rgbProbeText(std::span<const float> produced, std::uint32_t width, std::uint32_t height,
                         const std::vector<RgbProbe>& probes)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "probes=" << probes.size() << '\n';
    stream << std::setprecision(9) << std::scientific;
    const std::size_t planeSamples = static_cast<std::size_t>(width) * height;
    for (const RgbProbe& probe : probes)
    {
        const std::size_t pixel = static_cast<std::size_t>(probe.y) * width + probe.x;
        stream << probe.x << ',' << probe.y << '=';
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            if (channel != 0u)
            {
                stream << ',';
            }
            const std::size_t offset = channel * planeSamples + pixel;
            stream << (offset < produced.size() ? static_cast<double>(produced[offset])
                                                : std::numeric_limits<double>::quiet_NaN());
        }
        stream << '\n';
    }
    return stream.str();
}

} // namespace an::bench
