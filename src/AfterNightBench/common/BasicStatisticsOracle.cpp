// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "BasicStatisticsOracle.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace an::bench
{
namespace
{

[[nodiscard]] double normalizedError(double actual, double expected) noexcept
{
    return std::abs(actual - expected) / (std::max)(1.0, std::abs(expected));
}

} // namespace

bool BasicStatisticsOracleWindow::isValid() const noexcept
{
    return sourceWidth != 0u && sourceHeight != 0u && width != 0u && height != 0u && originX < sourceWidth &&
           originY < sourceHeight && width <= sourceWidth - originX && height <= sourceHeight - originY;
}

bool BasicStatisticsOracleWindow::selects(std::uint32_t x, std::uint32_t y) const noexcept
{
    if (!isValid() || x < originX || y < originY || x >= originX + width || y >= originY + height)
    {
        return false;
    }
    const std::uint64_t ordinal = static_cast<std::uint64_t>(y - originY) * width + (x - originX);
    return excludedEvery == 0u || ordinal % excludedEvery != 0u;
}

std::optional<BasicStatisticsOracle> computeBasicStatisticsOracle(std::span<const float> samples) noexcept
{
    if (samples.empty())
    {
        return std::nullopt;
    }

    long double sum = 0.0L;
    double minimum = (std::numeric_limits<double>::max)();
    double maximum = (std::numeric_limits<double>::lowest)();
    for (const float sample : samples)
    {
        if (!std::isfinite(sample))
        {
            return std::nullopt;
        }
        const double value = static_cast<double>(sample);
        sum += static_cast<long double>(sample);
        minimum = (std::min)(minimum, value);
        maximum = (std::max)(maximum, value);
    }

    const long double mean = sum / static_cast<long double>(samples.size());
    long double centered = 0.0L;
    for (const float sample : samples)
    {
        const long double deviation = static_cast<long double>(sample) - mean;
        centered += deviation * deviation;
    }

    return BasicStatisticsOracle{.count = samples.size(),
                                 .minimum = minimum,
                                 .maximum = maximum,
                                 .mean = static_cast<double>(mean),
                                 .populationVariance =
                                     static_cast<double>(centered / static_cast<long double>(samples.size()))};
}

std::optional<BasicStatisticsOracle> computeBasicStatisticsOracle(std::span<const float> samples,
                                                                  const BasicStatisticsOracleWindow& window) noexcept
{
    const std::uint64_t sourceSamples = static_cast<std::uint64_t>(window.sourceWidth) * window.sourceHeight;
    if (!window.isValid() || sourceSamples != samples.size())
    {
        return std::nullopt;
    }

    std::size_t count = 0u;
    long double sum = 0.0L;
    double minimum = (std::numeric_limits<double>::max)();
    double maximum = (std::numeric_limits<double>::lowest)();
    for (std::uint32_t y = window.originY; y < window.originY + window.height; ++y)
    {
        for (std::uint32_t x = window.originX; x < window.originX + window.width; ++x)
        {
            if (!window.selects(x, y))
            {
                continue;
            }
            const float sample = samples[static_cast<std::size_t>(y) * window.sourceWidth + x];
            if (!std::isfinite(sample))
            {
                return std::nullopt;
            }
            const double value = static_cast<double>(sample);
            ++count;
            sum += static_cast<long double>(sample);
            minimum = (std::min)(minimum, value);
            maximum = (std::max)(maximum, value);
        }
    }
    if (count == 0u)
    {
        return std::nullopt;
    }

    const long double mean = sum / static_cast<long double>(count);
    long double centered = 0.0L;
    for (std::uint32_t y = window.originY; y < window.originY + window.height; ++y)
    {
        for (std::uint32_t x = window.originX; x < window.originX + window.width; ++x)
        {
            if (!window.selects(x, y))
            {
                continue;
            }
            const long double deviation =
                static_cast<long double>(samples[static_cast<std::size_t>(y) * window.sourceWidth + x]) - mean;
            centered += deviation * deviation;
        }
    }

    return BasicStatisticsOracle{.count = count,
                                 .minimum = minimum,
                                 .maximum = maximum,
                                 .mean = static_cast<double>(mean),
                                 .populationVariance = static_cast<double>(centered / static_cast<long double>(count))};
}

double basicStatisticsError(const BasicStatisticsOracle& actual, const BasicStatisticsOracle& expected) noexcept
{
    if (actual.count != expected.count || !std::isfinite(actual.minimum) || !std::isfinite(actual.maximum) ||
        !std::isfinite(actual.mean) || !std::isfinite(actual.populationVariance) || !std::isfinite(expected.minimum) ||
        !std::isfinite(expected.maximum) || !std::isfinite(expected.mean) ||
        !std::isfinite(expected.populationVariance))
    {
        return (std::numeric_limits<double>::max)();
    }
    return (std::max)({normalizedError(actual.minimum, expected.minimum),
                       normalizedError(actual.maximum, expected.maximum), normalizedError(actual.mean, expected.mean),
                       normalizedError(actual.populationVariance, expected.populationVariance)});
}

std::string basicStatisticsText(const BasicStatisticsOracle& statistics)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "count=" << statistics.count << '\n';
    stream << std::setprecision(17) << std::scientific;
    stream << "min=" << statistics.minimum << '\n';
    stream << "max=" << statistics.maximum << '\n';
    stream << "mean=" << statistics.mean << '\n';
    stream << "variance=" << statistics.populationVariance << '\n';
    return stream.str();
}

} // namespace an::bench
