// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "GammaTransferOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace an::bench
{
namespace
{

[[nodiscard]] double normalizedError(double actual, double expected) noexcept
{
    return std::abs(actual - expected) / (std::max)(1.0, std::abs(expected));
}

} // namespace

std::vector<GammaTransferProbe> computeGammaTransferOracle(std::span<const float> samples, double gamma,
                                                           std::uint32_t probeStride)
{
    std::vector<GammaTransferProbe> probes;
    if (samples.empty() || probeStride == 0u || !std::isfinite(gamma) || gamma == 0.0)
    {
        return probes;
    }
    const long double exponent = 1.0L / static_cast<long double>(gamma);
    for (std::size_t index = 0u; index < samples.size(); index += probeStride)
    {
        const float sample = samples[index];
        if (!(sample >= 0.0f))
        {
            continue;
        }
        const long double transferred = std::pow(static_cast<long double>(sample), exponent);
        probes.push_back(GammaTransferProbe{static_cast<std::uint64_t>(index), static_cast<double>(transferred)});
    }
    return probes;
}

std::vector<GammaTransferProbe> computeGammaLinearCurveOracle(std::span<const float> samples, double gamma,
                                                              double middleInput, double middleOutput,
                                                              std::uint32_t probeStride)
{
    std::vector<GammaTransferProbe> probes;
    if (samples.empty() || probeStride == 0u || !std::isfinite(gamma) || !(gamma > 0.0) ||
        !std::isfinite(middleInput) || !(middleInput > 0.0 && middleInput < 1.0) || !std::isfinite(middleOutput) ||
        middleOutput < 0.0 || middleOutput > 1.0)
    {
        return probes;
    }
    const long double exponent = 1.0L / static_cast<long double>(gamma);
    const long double middleX = static_cast<long double>(middleInput);
    const long double middleY = static_cast<long double>(middleOutput);
    for (std::size_t index = 0u; index < samples.size(); index += probeStride)
    {
        const float sample = samples[index];
        if (!(sample >= 0.0f))
        {
            continue;
        }
        const long double gammaValue = std::pow(static_cast<long double>(sample), exponent);
        const long double bounded = (std::min)((std::max)(gammaValue, 0.0L), 1.0L);
        const long double curved = bounded <= middleX
                                       ? bounded * middleY / middleX
                                       : middleY + (bounded - middleX) * (1.0L - middleY) / (1.0L - middleX);
        probes.push_back(GammaTransferProbe{static_cast<std::uint64_t>(index), static_cast<double>(curved)});
    }
    return probes;
}

double gammaTransferError(std::span<const float> produced, const std::vector<GammaTransferProbe>& probes) noexcept
{
    const double unusable = (std::numeric_limits<double>::max)();
    if (produced.empty() || probes.empty())
    {
        return unusable;
    }
    double worst = 0.0;
    for (const GammaTransferProbe& probe : probes)
    {
        if (probe.index >= produced.size())
        {
            return unusable;
        }
        const double actual = static_cast<double>(produced[static_cast<std::size_t>(probe.index)]);
        if (!std::isfinite(actual) || !std::isfinite(probe.value))
        {
            return unusable;
        }
        worst = (std::max)(worst, normalizedError(actual, probe.value));
    }
    return worst;
}

} // namespace an::bench
