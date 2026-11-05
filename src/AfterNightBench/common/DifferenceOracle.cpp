// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definition of the comparison view's image difference.

#include "DifferenceOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace an::bench
{

std::vector<float> differenceSecondInput(std::span<const float> first, std::uint32_t width)
{
    std::vector<float> second(first.size());
    if (first.empty())
    {
        return second;
    }
    const std::size_t offset = (static_cast<std::size_t>(width) * 7u + 3u) % first.size();
    for (std::size_t index = 0u; index < first.size(); ++index)
    {
        second[index] = first[(index + offset) % first.size()];
    }
    return second;
}

bool requiresDifferenceOracle(const ScenarioSpec& scenario)
{
    return scenario.id == "pointwise.difference.f32";
}

void checkImageDifferenceOutput(std::span<const float> first, std::span<const float> second,
                                std::span<const float> output, ResultRecord& result)
{
    double largest = first.size() == output.size() && second.size() == output.size() ? 0.0 : 1.0;
    double differing = 0.0;
    const std::size_t count = (std::min)({first.size(), second.size(), output.size()});
    for (std::size_t index = 0u; index < count; ++index)
    {
        const float expected =
            static_cast<float>(std::fabs(static_cast<double>(first[index]) - static_cast<double>(second[index])));
        const double difference = std::fabs(static_cast<double>(output[index]) - static_cast<double>(expected));
        largest = std::isnan(difference) ? 1.0 : (std::max)(largest, difference);
        differing += difference != 0.0 ? 1.0 : 0.0;
    }
    result.correctnessMetric = largest;
    result.counters["differenceOracleVersion"] = 1.0;
    result.counters["differenceOracleSamples"] = static_cast<double>(count);
    result.counters["differenceOracleMaxAbs"] = largest;
    result.counters["differenceOracleDifferingSamples"] = differing;
}

} // namespace an::bench
