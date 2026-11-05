// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "PhaseCorrelationOracle.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace an::bench
{

std::vector<float> cyclicallyShiftedFrame(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                                          std::int32_t shiftX, std::int32_t shiftY)
{
    std::vector<float> shifted(samples.size(), 0.0f);
    const auto signedWidth = static_cast<std::int64_t>(width);
    const auto signedHeight = static_cast<std::int64_t>(height);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        // The destination row (x, y) holds the source's (x - shiftX, y - shiftY), wrapped.
        const std::int64_t fromY =
            ((static_cast<std::int64_t>(y) - shiftY) % signedHeight + signedHeight) % signedHeight;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const std::int64_t fromX =
                ((static_cast<std::int64_t>(x) - shiftX) % signedWidth + signedWidth) % signedWidth;
            shifted[static_cast<std::size_t>(y) * width + x] =
                samples[static_cast<std::size_t>(fromY) * width + static_cast<std::size_t>(fromX)];
        }
    }
    return shifted;
}

double phaseCorrelationResidual(double translationX, double translationY, std::int32_t shiftX,
                                std::int32_t shiftY) noexcept
{
    if (!std::isfinite(translationX) || !std::isfinite(translationY))
    {
        return std::numeric_limits<double>::max();
    }
    const double errorX = translationX - static_cast<double>(-shiftX);
    const double errorY = translationY - static_cast<double>(-shiftY);
    return std::sqrt(errorX * errorX + errorY * errorY);
}

std::string phaseCorrelationText(double translationX, double translationY, double response)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    stream << "translationX=" << translationX << '\n';
    stream << "translationY=" << translationY << '\n';
    stream << "response=" << response << '\n';
    return stream.str();
}

} // namespace an::bench
