// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "SeparableConvolutionOracle.h"

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

/// The inside coordinate a mirror that does not repeat the edge sample answers with.
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

[[nodiscard]] double normalizedError(double actual, double expected) noexcept
{
    return std::abs(actual - expected) / (std::max)(1.0, std::abs(expected));
}

} // namespace

std::vector<double> gaussianSeparableTaps(std::uint32_t radius, double sigma)
{
    std::vector<double> taps(2u * static_cast<std::size_t>(radius) + 1u, 0.0);
    double total = 0.0;
    for (std::size_t index = 0u; index < taps.size(); ++index)
    {
        const double offset = static_cast<double>(index) - static_cast<double>(radius);
        taps[index] = std::exp(-0.5 * offset * offset / (sigma * sigma));
        total += taps[index];
    }
    for (double& tap : taps)
    {
        tap /= total;
    }
    return taps;
}

std::uint32_t gaussianRadiusForSigma(double sigma)
{
    const double size = std::floor(sigma * 8.0 + 1.0 + 0.5);
    std::uint32_t kernel = static_cast<std::uint32_t>((std::max)(1.0, size));
    kernel |= 1u;
    return kernel / 2u;
}

std::vector<ConvolutionProbe> computeSeparableGaussianOracle(std::span<const float> samples, std::uint32_t width,
                                                             std::uint32_t height, std::span<const double> taps,
                                                             std::uint32_t stride)
{
    std::vector<ConvolutionProbe> probes;
    if (samples.size() < static_cast<std::size_t>(width) * height || taps.empty() || taps.size() % 2u == 0u ||
        stride == 0u)
    {
        return probes;
    }
    const std::int64_t radius = static_cast<std::int64_t>(taps.size() / 2u);
    const std::size_t total = static_cast<std::size_t>(width) * height;
    probes.reserve(total / stride + 1u);
    for (std::size_t index = 0u; index < total; index += stride)
    {
        const std::uint32_t x = static_cast<std::uint32_t>(index % width);
        const std::uint32_t y = static_cast<std::uint32_t>(index / width);
        long double sum = 0.0L;
        for (std::size_t down = 0u; down < taps.size(); ++down)
        {
            const std::int64_t sampleY =
                reflectExcludingEdge(static_cast<std::int64_t>(y) + static_cast<std::int64_t>(down) - radius, height);
            for (std::size_t across = 0u; across < taps.size(); ++across)
            {
                const std::int64_t sampleX = reflectExcludingEdge(
                    static_cast<std::int64_t>(x) + static_cast<std::int64_t>(across) - radius, width);
                const long double weight =
                    static_cast<long double>(taps[down]) * static_cast<long double>(taps[across]);
                sum += weight *
                       static_cast<long double>(
                           samples[static_cast<std::size_t>(sampleY) * width + static_cast<std::size_t>(sampleX)]);
            }
        }
        probes.push_back(ConvolutionProbe{.x = x, .y = y, .value = static_cast<double>(sum)});
    }
    return probes;
}

double separableConvolutionError(std::span<const float> produced, std::uint32_t width,
                                 const std::vector<ConvolutionProbe>& probes) noexcept
{
    if (probes.empty() || width == 0u)
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (const ConvolutionProbe& probe : probes)
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

std::string separableConvolutionText(std::span<const float> produced, std::uint32_t width,
                                     const std::vector<ConvolutionProbe>& probes)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "probes=" << probes.size() << '\n';
    stream << std::setprecision(9) << std::scientific;
    for (const ConvolutionProbe& probe : probes)
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
