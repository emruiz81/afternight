// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "HistogramOracle.h"

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

constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

/// Which bin of a closed interval one value falls in, under the mapping the engine publishes.
[[nodiscard]] std::size_t oracleBin(double value, double lowest, double highest, std::size_t bins) noexcept
{
    if (value <= lowest)
    {
        return 0u;
    }
    if (value >= highest)
    {
        return bins - 1u;
    }
    const double scaled = (value - lowest) / (highest - lowest) * static_cast<double>(bins);
    if (!(scaled > 0.0))
    {
        return 0u;
    }
    if (!(scaled < static_cast<double>(bins)))
    {
        return bins - 1u;
    }
    return static_cast<std::size_t>(scaled);
}

} // namespace

std::uint64_t histogramChecksum(std::span<const std::uint64_t> bins) noexcept
{
    std::uint64_t hash = kFnvOffsetBasis;
    for (const std::uint64_t count : bins)
    {
        for (unsigned shift = 0u; shift < 64u; shift += 8u)
        {
            hash ^= (count >> shift) & 0xffull;
            hash *= kFnvPrime;
        }
    }
    return hash;
}

std::optional<HistogramOracle> summariseHistogram(std::span<const std::uint64_t> bins)
{
    if (bins.empty())
    {
        return std::nullopt;
    }
    HistogramOracle histogram;
    histogram.binCount = bins.size();
    histogram.checksum = histogramChecksum(bins);
    for (std::size_t index = 0u; index < bins.size(); ++index)
    {
        histogram.counted += static_cast<std::size_t>(bins[index]);
        if (bins[index] > histogram.largestBin)
        {
            histogram.largestBin = bins[index];
            histogram.largestBinIndex = index;
        }
    }
    return histogram;
}

std::optional<HistogramOracle> computeFullScaleHistogramOracle(std::span<const std::uint16_t> samples, std::size_t bins)
{
    if (samples.empty() || bins != 65536u)
    {
        return std::nullopt;
    }
    std::vector<std::uint64_t> counts(bins, 0u);
    for (const std::uint16_t code : samples)
    {
        ++counts[code];
    }
    return summariseHistogram(counts);
}

std::optional<HistogramOracle> computeWindowedHistogramOracle(std::span<const float> samples,
                                                              const BasicStatisticsOracleWindow& window, double lowest,
                                                              double highest, std::size_t bins,
                                                              std::vector<std::uint64_t>& counts)
{
    if (!window.isValid() || bins == 0u || !std::isfinite(lowest) || !std::isfinite(highest) || !(highest > lowest) ||
        samples.size() != static_cast<std::size_t>(window.sourceWidth) * window.sourceHeight)
    {
        return std::nullopt;
    }
    counts.assign(bins, 0u);
    std::size_t selected = 0u;
    for (std::uint32_t y = window.originY; y < window.originY + window.height; ++y)
    {
        for (std::uint32_t x = window.originX; x < window.originX + window.width; ++x)
        {
            if (!window.selects(x, y))
            {
                continue;
            }
            ++selected;
            const double value = static_cast<double>(samples[static_cast<std::size_t>(y) * window.sourceWidth + x]);
            if (std::isnan(value) || value < lowest || value > highest)
            {
                continue;
            }
            ++counts[oracleBin(value, lowest, highest, bins)];
        }
    }
    if (selected == 0u)
    {
        return std::nullopt;
    }
    return summariseHistogram(counts);
}

double histogramOracleQuantile(std::span<const std::uint64_t> counts, double lowest, double highest,
                               double quantile) noexcept
{
    if (counts.empty())
    {
        return lowest;
    }
    std::uint64_t total = 0u;
    for (const std::uint64_t count : counts)
    {
        total += count;
    }
    if (total == 0u)
    {
        return lowest;
    }
    if (quantile <= 0.0)
    {
        return lowest;
    }
    if (quantile >= 1.0)
    {
        return highest;
    }
    const double width = (highest - lowest) / static_cast<double>(counts.size());
    const double rank = quantile * static_cast<double>(total);
    std::uint64_t running = 0u;
    for (std::size_t index = 0u; index < counts.size(); ++index)
    {
        const std::uint64_t below = running;
        running += counts[index];
        if (static_cast<double>(running) < rank)
        {
            continue;
        }
        const double edge = lowest + static_cast<double>(index) * width;
        if (counts[index] == 0u)
        {
            return (std::min)(edge, highest);
        }
        const double within = (rank - static_cast<double>(below)) / static_cast<double>(counts[index]);
        return (std::min)(edge + within * width, highest);
    }
    return highest;
}

double histogramError(const HistogramOracle& actual, const HistogramOracle& expected) noexcept
{
    const bool agrees = actual.counted == expected.counted && actual.binCount == expected.binCount &&
                        actual.checksum == expected.checksum && actual.largestBin == expected.largestBin &&
                        actual.largestBinIndex == expected.largestBinIndex;
    return agrees ? 0.0 : (std::numeric_limits<double>::max)();
}

std::string histogramPercentileText(std::span<const double> values)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << std::scientific;
    for (std::size_t index = 0u; index < values.size(); ++index)
    {
        stream << "quantile" << index << "=" << values[index] << '\n';
    }
    return stream.str();
}

std::string histogramText(const HistogramOracle& histogram)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "counted=" << histogram.counted << '\n';
    stream << "bins=" << histogram.binCount << '\n';
    stream << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << histogram.checksum << std::dec << '\n';
    stream << std::setfill(' ');
    stream << "largestBin=" << histogram.largestBin << '\n';
    stream << "largestBinIndex=" << histogram.largestBinIndex << '\n';
    return stream.str();
}

} // namespace an::bench
