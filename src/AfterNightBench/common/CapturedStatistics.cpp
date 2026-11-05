// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "CapturedStatistics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace an::bench
{
namespace
{

/// One distinct value of a capture and how many samples hold it.
using CountedValue = std::pair<double, std::uint64_t>;

/// The unit-interval sample one code becomes, widened as the sorted reference widens it.
[[nodiscard]] double sampleOf(std::uint32_t code) noexcept
{
    return static_cast<double>(static_cast<float>(code) / 65535.0f);
}

/// The value at one zero-based rank of an ascending list of distinct values and their counts.
[[nodiscard]] double valueAtRank(const std::vector<CountedValue>& values, std::uint64_t rank) noexcept
{
    for (const auto& [value, frequency] : values)
    {
        if (rank < frequency)
        {
            return value;
        }
        rank -= frequency;
    }
    return values.back().first;
}

/// The quantile at one half, interpolated between ranks exactly as the sorted reference does.
[[nodiscard]] double medianOfCounted(const std::vector<CountedValue>& values, std::size_t count) noexcept
{
    const double position = 0.5 * static_cast<double>(count - 1u);
    const double floored = std::floor(position);
    const auto lower = static_cast<std::uint64_t>(floored);
    const double fraction = position - floored;
    const double lowerValue = valueAtRank(values, lower);
    if (fraction == 0.0)
    {
        return lowerValue;
    }
    return lowerValue + fraction * (valueAtRank(values, lower + 1u) - lowerValue);
}

} // namespace

std::vector<float> makeUnitIntervalCapture(std::span<const std::uint8_t> codes)
{
    if (codes.size() % 2u != 0u)
    {
        return {};
    }
    std::vector<float> samples(codes.size() / 2u);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        const auto code = static_cast<std::uint16_t>(codes[2u * index] | (codes[2u * index + 1u] << 8u));
        samples[index] = static_cast<float>(code) / 65535.0f;
    }
    return samples;
}

std::optional<OrderStatisticsOracle> computeCapturedOrderStatisticsOracle(std::span<const std::uint8_t> codes)
{
    if (codes.empty() || codes.size() % 2u != 0u)
    {
        return std::nullopt;
    }
    std::vector<std::uint64_t> frequencies(65536u, 0u);
    for (std::size_t index = 0u; index < codes.size(); index += 2u)
    {
        ++frequencies[static_cast<std::size_t>(codes[index] | (codes[index + 1u] << 8u))];
    }
    std::vector<CountedValue> values;
    for (std::uint32_t code = 0u; code < 65536u; ++code)
    {
        if (frequencies[code] != 0u)
        {
            values.emplace_back(sampleOf(code), frequencies[code]);
        }
    }
    const std::size_t count = codes.size() / 2u;
    const double median = medianOfCounted(values, count);

    std::vector<CountedValue> deviations;
    deviations.reserve(values.size());
    for (const auto& [value, frequency] : values)
    {
        deviations.emplace_back(std::abs(value - median), frequency);
    }
    std::sort(deviations.begin(), deviations.end(),
              [](const CountedValue& left, const CountedValue& right) { return left.first < right.first; });
    return OrderStatisticsOracle{
        .count = count, .median = median, .medianAbsoluteDeviation = medianOfCounted(deviations, count)};
}

double completeStatisticsError(const CompleteStatistics& actual, const BasicStatisticsOracle& basic,
                               const OrderStatisticsOracle& order) noexcept
{
    const BasicStatisticsOracle basicPart{.count = actual.count,
                                          .minimum = actual.minimum,
                                          .maximum = actual.maximum,
                                          .mean = actual.mean,
                                          .populationVariance = actual.populationVariance};
    const OrderStatisticsOracle orderPart{
        .count = actual.count, .median = actual.median, .medianAbsoluteDeviation = actual.medianAbsoluteDeviation};
    return (std::max)(basicStatisticsError(basicPart, basic), orderStatisticsError(orderPart, order));
}

std::string completeStatisticsText(const CompleteStatistics& statistics)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "count=" << statistics.count << '\n';
    stream << std::setprecision(17) << std::scientific;
    stream << "min=" << statistics.minimum << '\n';
    stream << "max=" << statistics.maximum << '\n';
    stream << "mean=" << statistics.mean << '\n';
    stream << "variance=" << statistics.populationVariance << '\n';
    stream << "median=" << statistics.median << '\n';
    stream << "mad=" << statistics.medianAbsoluteDeviation << '\n';
    stream << "avg_dev=" << statistics.averageAbsoluteDeviation << '\n';
    stream << "ssqr=" << statistics.sumOfSquares << '\n';
    stream << "asqr=" << statistics.meanOfSquares << '\n';
    stream << "bwmv=" << statistics.biweightMidvariance << '\n';
    stream << "bg_noise=" << statistics.backgroundNoise << '\n';
    stream << "location=" << statistics.location << '\n';
    stream << "scale=" << statistics.scale << '\n';
    return stream.str();
}

} // namespace an::bench
