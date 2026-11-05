// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "MedianFilterOracle.h"

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
/// @param expected what the sorted window holds in the middle.
/// @return the absolute error below one and the relative error above it.
[[nodiscard]] double normalizedError(double actual, double expected) noexcept
{
    return std::abs(actual - expected) / (std::max)(1.0, std::abs(expected));
}

} // namespace

std::vector<MedianProbe> computeMedianFilterOracle(std::span<const float> samples, std::uint32_t width,
                                                   std::uint32_t height, std::uint32_t radius, std::uint32_t stride)
{
    std::vector<MedianProbe> probes;
    if (samples.size() < static_cast<std::size_t>(width) * height || stride == 0u || width == 0u || height == 0u)
    {
        return probes;
    }
    const std::size_t imageSamples = static_cast<std::size_t>(width) * height;
    const std::size_t window = (2u * static_cast<std::size_t>(radius) + 1u) * (2u * radius + 1u);
    probes.reserve(imageSamples / stride + 1u);

    std::vector<float> values;
    values.reserve(window);
    for (std::size_t index = 0u; index < imageSamples; index += stride)
    {
        const std::uint32_t x = static_cast<std::uint32_t>(index % width);
        const std::uint32_t y = static_cast<std::uint32_t>(index / width);
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
        std::sort(values.begin(), values.end());
        const std::size_t upper = values.size() / 2u;
        const double median = (values.size() & 1u) != 0u
                                  ? static_cast<double>(values[upper])
                                  : (static_cast<double>(values[upper - 1u]) + values[upper]) * 0.5;
        probes.push_back(MedianProbe{.x = x, .y = y, .value = median});
    }
    return probes;
}

std::vector<MedianProbe> computeFootprintMedianOracle(std::span<const float> samples, std::uint32_t width,
                                                      std::uint32_t height, std::uint32_t radius,
                                                      std::span<const std::uint8_t> element, std::uint32_t stride)
{
    std::vector<MedianProbe> probes;
    const std::size_t side = 2u * static_cast<std::size_t>(radius) + 1u;
    if (samples.size() < static_cast<std::size_t>(width) * height || stride == 0u || width == 0u || height == 0u ||
        element.size() != side * side)
    {
        return probes;
    }
    const std::size_t imageSamples = static_cast<std::size_t>(width) * height;
    probes.reserve(imageSamples / stride + 1u);

    std::vector<float> values;
    values.reserve(element.size());
    for (std::size_t index = 0u; index < imageSamples; index += stride)
    {
        const auto x = static_cast<std::int64_t>(index % width);
        const auto y = static_cast<std::int64_t>(index / width);
        values.clear();
        for (std::size_t row = 0u; row < side; ++row)
        {
            for (std::size_t column = 0u; column < side; ++column)
            {
                const std::int64_t sampleX = x + static_cast<std::int64_t>(column) - radius;
                const std::int64_t sampleY = y + static_cast<std::int64_t>(row) - radius;
                if (element[row * side + column] == 0u || sampleX < 0 || sampleY < 0 || sampleX >= width ||
                    sampleY >= height)
                {
                    continue;
                }
                values.push_back(
                    samples[static_cast<std::size_t>(sampleY) * width + static_cast<std::size_t>(sampleX)]);
            }
        }
        if (values.empty())
        {
            continue;
        }
        std::sort(values.begin(), values.end());
        const std::size_t upper = values.size() / 2u;
        const double median = (values.size() & 1u) != 0u
                                  ? static_cast<double>(values[upper])
                                  : (static_cast<double>(values[upper - 1u]) + values[upper]) * 0.5;
        probes.push_back(
            MedianProbe{.x = static_cast<std::uint32_t>(x), .y = static_cast<std::uint32_t>(y), .value = median});
    }
    return probes;
}

double medianFilterError(std::span<const float> produced, std::uint32_t width,
                         const std::vector<MedianProbe>& probes) noexcept
{
    if (probes.empty() || width == 0u)
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (const MedianProbe& probe : probes)
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

std::string medianFilterText(std::span<const float> produced, std::uint32_t width,
                             const std::vector<MedianProbe>& probes)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "probes=" << probes.size() << '\n';
    stream << std::setprecision(9) << std::scientific;
    for (const MedianProbe& probe : probes)
    {
        const std::size_t offset = static_cast<std::size_t>(probe.y) * width + probe.x;
        stream << probe.x << ',' << probe.y << '=';
        stream << (offset < produced.size() ? static_cast<double>(produced[offset])
                                            : std::numeric_limits<double>::quiet_NaN());
        stream << '\n';
    }
    return stream.str();
}

} // namespace an::bench
