// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ColorTransformOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace an::bench
{
namespace
{

/// The CIE lightness helper's break point, whose cube is where the two segments meet.
constexpr long double kLabDelta = 6.0L / 29.0L;

/// The D65 tristimulus values the standards tabulate, normalized so that luminance is one.
constexpr long double kWhiteX = 0.95047L;
constexpr long double kWhiteY = 1.0L;
constexpr long double kWhiteZ = 1.08883L;

[[nodiscard]] long double decodeSrgb(long double value) noexcept
{
    return value <= 0.04045L ? value / 12.92L : std::pow((value + 0.055L) / 1.055L, 2.4L);
}

[[nodiscard]] long double labForward(long double ratio) noexcept
{
    return ratio > kLabDelta * kLabDelta * kLabDelta ? std::cbrt(ratio)
                                                     : ratio / (3.0L * kLabDelta * kLabDelta) + 4.0L / 29.0L;
}

[[nodiscard]] double normalizedError(double actual, double expected) noexcept
{
    return std::abs(actual - expected) / (std::max)(1.0, std::abs(expected));
}

[[nodiscard]] bool inUnitInterval(float value) noexcept
{
    return value >= 0.0f && value <= 1.0f;
}

} // namespace

std::vector<SrgbToLabProbe> computeSrgbToLabOracle(std::span<const float> red, std::span<const float> green,
                                                   std::span<const float> blue, std::uint32_t probeStride)
{
    std::vector<SrgbToLabProbe> probes;
    if (red.empty() || probeStride == 0u || green.size() != red.size() || blue.size() != red.size())
    {
        return probes;
    }
    for (std::size_t index = 0u; index < red.size(); index += probeStride)
    {
        if (!inUnitInterval(red[index]) || !inUnitInterval(green[index]) || !inUnitInterval(blue[index]))
        {
            continue;
        }
        const long double linearRed = decodeSrgb(static_cast<long double>(red[index]));
        const long double linearGreen = decodeSrgb(static_cast<long double>(green[index]));
        const long double linearBlue = decodeSrgb(static_cast<long double>(blue[index]));

        // The published sRGB primaries matrix, stated as the standard prints it.
        const long double x = 0.4124564L * linearRed + 0.3575761L * linearGreen + 0.1804375L * linearBlue;
        const long double y = 0.2126729L * linearRed + 0.7151522L * linearGreen + 0.0721750L * linearBlue;
        const long double z = 0.0193339L * linearRed + 0.1191920L * linearGreen + 0.9503041L * linearBlue;

        const long double fx = labForward(x / kWhiteX);
        const long double fy = labForward(y / kWhiteY);
        const long double fz = labForward(z / kWhiteZ);

        const long double lightness = 116.0L * fy - 16.0L;
        const long double greenRed = 500.0L * (fx - fy);
        const long double blueYellow = 200.0L * (fy - fz);

        probes.push_back(SrgbToLabProbe{static_cast<std::uint64_t>(index), static_cast<double>(lightness / 100.0L),
                                        static_cast<double>((greenRed + 128.0L) / 255.0L),
                                        static_cast<double>((blueYellow + 128.0L) / 255.0L)});
    }
    return probes;
}

double srgbToLabError(std::span<const float> lightness, std::span<const float> greenRed,
                      std::span<const float> blueYellow, const std::vector<SrgbToLabProbe>& probes) noexcept
{
    const double unusable = (std::numeric_limits<double>::max)();
    if (lightness.empty() || probes.empty() || greenRed.size() != lightness.size() ||
        blueYellow.size() != lightness.size())
    {
        return unusable;
    }
    double worst = 0.0;
    for (const SrgbToLabProbe& probe : probes)
    {
        if (probe.index >= lightness.size())
        {
            return unusable;
        }
        const std::size_t at = static_cast<std::size_t>(probe.index);
        const double producedLightness = static_cast<double>(lightness[at]);
        const double producedGreenRed = static_cast<double>(greenRed[at]);
        const double producedBlueYellow = static_cast<double>(blueYellow[at]);
        if (!std::isfinite(producedLightness) || !std::isfinite(producedGreenRed) ||
            !std::isfinite(producedBlueYellow) || !std::isfinite(probe.lightness) || !std::isfinite(probe.greenRed) ||
            !std::isfinite(probe.blueYellow))
        {
            return unusable;
        }
        worst = (std::max)(worst, normalizedError(producedLightness, probe.lightness));
        worst = (std::max)(worst, normalizedError(producedGreenRed, probe.greenRed));
        worst = (std::max)(worst, normalizedError(producedBlueYellow, probe.blueYellow));
    }
    return worst;
}

} // namespace an::bench
