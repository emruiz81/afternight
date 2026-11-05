// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "OrderStatisticsOracle.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <vector>

namespace an::bench
{
namespace
{

[[nodiscard]] double normalizedError(double actual, double expected) noexcept
{
    return std::abs(actual - expected) / (std::max)(1.0, std::abs(expected));
}

/// The quantile at one half of an already ascending sequence, interpolating between ranks.
[[nodiscard]] double medianOf(const std::vector<double>& sorted) noexcept
{
    const double position = 0.5 * static_cast<double>(sorted.size() - 1u);
    const double floored = std::floor(position);
    const std::size_t lower = static_cast<std::size_t>(floored);
    const double fraction = position - floored;
    if (fraction == 0.0)
    {
        return sorted[lower];
    }
    return sorted[lower] + fraction * (sorted[lower + 1u] - sorted[lower]);
}

/// The exact deviation of an already ascending sequence around a stated centre.
[[nodiscard]] double deviationOf(const std::vector<double>& sorted, double centre)
{
    std::vector<double> deviations;
    deviations.reserve(sorted.size());
    for (const double value : sorted)
    {
        deviations.push_back(std::abs(value - centre));
    }
    std::sort(deviations.begin(), deviations.end());
    return medianOf(deviations);
}

/// What multiplying a median absolute deviation by yields a standard-deviation-like scale.
///
/// The same normal-distribution constant the engine publishes. It is repeated here rather than
/// included, because an oracle that shares a constant with the thing it checks is checking less.
constexpr double kDeviationToSigma = 1.4826022185056018;

} // namespace

std::optional<OrderStatisticsOracle> computeOrderStatisticsOracle(std::span<const float> samples)
{
    if (samples.empty())
    {
        return std::nullopt;
    }
    std::vector<double> values;
    values.reserve(samples.size());
    for (const float sample : samples)
    {
        if (!std::isfinite(sample))
        {
            return std::nullopt;
        }
        values.push_back(static_cast<double>(sample));
    }
    std::sort(values.begin(), values.end());
    const double median = medianOf(values);

    for (double& value : values)
    {
        value = std::abs(value - median);
    }
    std::sort(values.begin(), values.end());
    return OrderStatisticsOracle{
        .count = samples.size(), .median = median, .medianAbsoluteDeviation = medianOf(values)};
}

std::optional<OrderStatisticsOracle> computeOrderStatisticsOracle(std::span<const float> samples,
                                                                  const BasicStatisticsOracleWindow& window)
{
    if (!window.isValid() || samples.size() != static_cast<std::size_t>(window.sourceWidth) * window.sourceHeight)
    {
        return std::nullopt;
    }
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(window.width) * window.height);
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
            values.push_back(static_cast<double>(sample));
        }
    }
    if (values.empty())
    {
        return std::nullopt;
    }
    std::sort(values.begin(), values.end());
    const double median = medianOf(values);
    return OrderStatisticsOracle{
        .count = values.size(), .median = median, .medianAbsoluteDeviation = deviationOf(values, median)};
}

std::optional<SigmaClippedOracle> computeSigmaClippedOracle(std::span<const float> samples, double lowSigma,
                                                            double highSigma, std::uint32_t maximumIterations)
{
    if (samples.empty() || maximumIterations == 0u)
    {
        return std::nullopt;
    }
    std::vector<double> values;
    values.reserve(samples.size());
    for (const float sample : samples)
    {
        if (!std::isfinite(sample))
        {
            return std::nullopt;
        }
        values.push_back(static_cast<double>(sample));
    }
    std::sort(values.begin(), values.end());
    const double smallest = values.front();
    const double largest = values.back();
    double location = medianOf(values);
    double scale = deviationOf(values, location) * kDeviationToSigma;
    std::size_t retained = values.size();
    std::vector<double> kept;
    for (std::uint32_t iteration = 0u; iteration < maximumIterations; ++iteration)
    {
        const double lowest = (std::max)(smallest, location - lowSigma * scale);
        const double highest = (std::min)(largest, location + highSigma * scale);
        kept.clear();
        for (const double value : values)
        {
            if (value >= lowest && value <= highest)
            {
                kept.push_back(value);
            }
        }
        if (kept.empty())
        {
            return std::nullopt;
        }
        location = medianOf(kept);
        scale = deviationOf(kept, location) * kDeviationToSigma;
        const bool stable = kept.size() == retained;
        retained = kept.size();
        if (stable)
        {
            break;
        }
    }
    long double sum = 0.0L;
    for (const double value : kept)
    {
        sum += static_cast<long double>(value);
    }
    const long double mean = sum / static_cast<long double>(kept.size());
    long double squared = 0.0L;
    for (const double value : kept)
    {
        const long double deviation = static_cast<long double>(value) - mean;
        squared += deviation * deviation;
    }
    return SigmaClippedOracle{.retained = retained,
                              .location = location,
                              .scale = scale,
                              .mean = static_cast<double>(mean),
                              .standardDeviation =
                                  static_cast<double>(std::sqrt(squared / static_cast<long double>(kept.size())))};
}

double orderStatisticsError(const OrderStatisticsOracle& actual, const OrderStatisticsOracle& expected) noexcept
{
    if (actual.count != expected.count || !std::isfinite(actual.median) ||
        !std::isfinite(actual.medianAbsoluteDeviation) || !std::isfinite(expected.median) ||
        !std::isfinite(expected.medianAbsoluteDeviation))
    {
        return (std::numeric_limits<double>::max)();
    }
    return (std::max)(normalizedError(actual.median, expected.median),
                      normalizedError(actual.medianAbsoluteDeviation, expected.medianAbsoluteDeviation));
}

double sigmaClippedError(const SigmaClippedOracle& actual, const SigmaClippedOracle& expected) noexcept
{
    if (actual.retained != expected.retained || !std::isfinite(actual.location) || !std::isfinite(actual.scale) ||
        !std::isfinite(actual.mean) || !std::isfinite(actual.standardDeviation) || !std::isfinite(expected.location) ||
        !std::isfinite(expected.scale) || !std::isfinite(expected.mean) || !std::isfinite(expected.standardDeviation))
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = normalizedError(actual.location, expected.location);
    worst = (std::max)(worst, normalizedError(actual.scale, expected.scale));
    worst = (std::max)(worst, normalizedError(actual.mean, expected.mean));
    return (std::max)(worst, normalizedError(actual.standardDeviation, expected.standardDeviation));
}

std::string sigmaClippedText(const SigmaClippedOracle& statistics)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "retained=" << statistics.retained << '\n';
    stream << std::setprecision(17) << std::scientific;
    stream << "location=" << statistics.location << '\n';
    stream << "scale=" << statistics.scale << '\n';
    stream << "mean=" << statistics.mean << '\n';
    stream << "stddev=" << statistics.standardDeviation << '\n';
    return stream.str();
}

std::string orderStatisticsText(const OrderStatisticsOracle& statistics)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "count=" << statistics.count << '\n';
    stream << std::setprecision(17) << std::scientific;
    stream << "median=" << statistics.median << '\n';
    stream << "mad=" << statistics.medianAbsoluteDeviation << '\n';
    return stream.str();
}

} // namespace an::bench
