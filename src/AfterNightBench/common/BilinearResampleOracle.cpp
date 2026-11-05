// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "BilinearResampleOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace an::bench
{
namespace
{

[[nodiscard]] std::uint32_t clampIndex(std::int64_t index, std::uint32_t size) noexcept
{
    if (index < 0)
    {
        return 0u;
    }
    return index >= static_cast<std::int64_t>(size) ? size - 1u : static_cast<std::uint32_t>(index);
}

[[nodiscard]] double normalizedError(double actual, double expected) noexcept
{
    return std::abs(actual - expected) / (std::max)(1.0, std::abs(expected));
}

} // namespace

double bilinearResizeCoordinate(std::uint32_t destinationIndex, std::uint32_t sourceSize,
                                std::uint32_t destinationSize) noexcept
{
    return (static_cast<double>(destinationIndex) + 0.5) * static_cast<double>(sourceSize) /
               static_cast<double>(destinationSize) -
           0.5;
}

std::vector<BilinearResampleProbe>
computeBilinearResizeOracle(std::span<const float> samples, std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                            std::uint32_t destinationWidth, std::uint32_t destinationHeight, std::uint32_t probeStride)
{
    std::vector<BilinearResampleProbe> probes;
    if (sourceWidth == 0u || sourceHeight == 0u || destinationWidth == 0u || destinationHeight == 0u ||
        probeStride == 0u || samples.size() < static_cast<std::size_t>(sourceWidth) * sourceHeight)
    {
        return probes;
    }
    const std::size_t destinationCount = static_cast<std::size_t>(destinationWidth) * destinationHeight;
    probes.reserve(destinationCount / probeStride + 1u);
    for (std::size_t index = 0u; index < destinationCount; index += probeStride)
    {
        const std::uint32_t x = static_cast<std::uint32_t>(index % destinationWidth);
        const std::uint32_t y = static_cast<std::uint32_t>(index / destinationWidth);
        const double sourceX = bilinearResizeCoordinate(x, sourceWidth, destinationWidth);
        const double sourceY = bilinearResizeCoordinate(y, sourceHeight, destinationHeight);
        const std::int64_t x0 = static_cast<std::int64_t>(std::floor(sourceX));
        const std::int64_t y0 = static_cast<std::int64_t>(std::floor(sourceY));
        const long double fractionX = static_cast<long double>(sourceX - static_cast<double>(x0));
        const long double fractionY = static_cast<long double>(sourceY - static_cast<double>(y0));
        long double sum = 0.0L;
        for (std::uint32_t yTap = 0u; yTap < 2u; ++yTap)
        {
            const std::uint32_t sourceRow = clampIndex(y0 + yTap, sourceHeight);
            const long double weightY = yTap == 0u ? 1.0L - fractionY : fractionY;
            for (std::uint32_t xTap = 0u; xTap < 2u; ++xTap)
            {
                const std::uint32_t sourceColumn = clampIndex(x0 + xTap, sourceWidth);
                const long double weightX = xTap == 0u ? 1.0L - fractionX : fractionX;
                sum +=
                    weightX * weightY *
                    static_cast<long double>(samples[static_cast<std::size_t>(sourceRow) * sourceWidth + sourceColumn]);
            }
        }
        probes.push_back(BilinearResampleProbe{.x = x, .y = y, .value = static_cast<double>(sum)});
    }
    return probes;
}

double bilinearResizeError(std::span<const float> produced, std::uint32_t destinationWidth,
                           const std::vector<BilinearResampleProbe>& probes) noexcept
{
    if (destinationWidth == 0u || probes.empty())
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (const BilinearResampleProbe& probe : probes)
    {
        const std::size_t index = static_cast<std::size_t>(probe.y) * destinationWidth + probe.x;
        if (index >= produced.size() || !std::isfinite(produced[index]) || !std::isfinite(probe.value))
        {
            return (std::numeric_limits<double>::max)();
        }
        worst = (std::max)(worst, normalizedError(produced[index], probe.value));
    }
    return worst;
}

double unitRangeResizeError(std::span<const float> produced, std::uint32_t probeStride) noexcept
{
    if (produced.empty() || probeStride == 0u)
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < produced.size(); index += probeStride)
    {
        const double value = static_cast<double>(produced[index]);
        if (!std::isfinite(value))
        {
            return (std::numeric_limits<double>::max)();
        }
        worst = (std::max)(worst, value < 0.0 ? -value : (value > 1.0 ? value - 1.0 : 0.0));
    }
    return worst;
}

} // namespace an::bench
