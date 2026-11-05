// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FixtureStatistics.h"

#include <algorithm>
#include <cmath>

namespace an::tests::fixtures
{

std::vector<FixtureChannelStatistics> computeFixtureStatistics(const FixtureSamples& samples)
{
    std::vector<FixtureChannelStatistics> statistics(samples.channelCount());
    std::vector<double> finite;
    finite.reserve(static_cast<std::size_t>(samples.width()) * samples.height());
    for (std::uint32_t channel = 0u; channel < samples.channelCount(); ++channel)
    {
        FixtureChannelStatistics& result = statistics[channel];
        finite.clear();
        long double sum = 0.0L;
        for (std::uint32_t y = 0u; y < samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < samples.width(); ++x)
            {
                const double value = samples.value(x, y, channel);
                if (!std::isfinite(value))
                {
                    ++result.nonFinite;
                    continue;
                }
                finite.push_back(value);
                sum += static_cast<long double>(value);
            }
        }
        result.count = finite.size();
        if (finite.empty())
        {
            continue;
        }
        const auto [lowest, highest] = std::minmax_element(finite.begin(), finite.end());
        result.minimum = *lowest;
        result.maximum = *highest;
        result.sum = static_cast<double>(sum);
        result.mean = static_cast<double>(sum / static_cast<long double>(finite.size()));
        std::sort(finite.begin(), finite.end());
        const std::size_t middle = finite.size() / 2u;
        result.median =
            finite.size() % 2u == 1u
                ? finite[middle]
                : static_cast<double>((static_cast<long double>(finite[middle - 1u]) + finite[middle]) / 2.0L);
    }
    return statistics;
}

} // namespace an::tests::fixtures
