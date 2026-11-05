// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The global normalization of one captured light to another, written from its definition.
#include "NormalizationPairOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace an::bench
{
namespace
{

/// The middle of some values, the mean of the two middle ones for an even count. Reorders them.
[[nodiscard]] double middleOf(std::vector<double>& values)
{
    const std::size_t count = values.size();
    const auto upper = values.begin() + static_cast<std::ptrdiff_t>(count / 2u);
    std::nth_element(values.begin(), upper, values.end());
    const double high = *upper;
    if (count % 2u == 1u)
    {
        return high;
    }
    const double low = *std::max_element(values.begin(), upper);
    return (low + high) / 2.0;
}

} // namespace

NormalizationPairLevel normalizationPairLevel(std::span<const float> samples)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const float sample : samples)
    {
        if (sample != 0.0f && std::isfinite(sample))
        {
            values.push_back(static_cast<double>(sample));
        }
    }
    NormalizationPairLevel level;
    level.samples = values.size();
    if (values.empty())
    {
        return level;
    }
    level.median = middleOf(values);
    for (double& value : values)
    {
        value = std::abs(value - level.median);
    }
    level.deviation = middleOf(values);
    return level;
}

std::optional<NormalizationPairMatch> normalizationPairMatch(std::string_view method,
                                                             const NormalizationPairLevel& reference,
                                                             const NormalizationPairLevel& target)
{
    NormalizationPairMatch match;
    if (method == "Additive")
    {
        match.offset = reference.median - target.median;
        return match;
    }
    if (method == "Multiplicative")
    {
        match.scale = target.median == 0.0 ? 1.0 : reference.median / target.median;
        return match;
    }
    if (method == "AdditiveScaling")
    {
        match.scale =
            reference.deviation == 0.0 || target.deviation == 0.0 ? 1.0 : reference.deviation / target.deviation;
        match.offset = reference.median - target.median * match.scale;
        return match;
    }
    return std::nullopt;
}

double normalizationPairError(std::span<const float> normalized, std::span<const float> target,
                              const NormalizationPairMatch& match)
{
    if (normalized.size() != target.size())
    {
        return (std::numeric_limits<double>::max)();
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < normalized.size(); ++index)
    {
        const float actual = normalized[index];
        if (target[index] == 0.0f)
        {
            if (actual != 0.0f)
            {
                return (std::numeric_limits<double>::max)();
            }
            continue;
        }
        if (!std::isfinite(actual))
        {
            return (std::numeric_limits<double>::max)();
        }
        const double expected = static_cast<double>(target[index]) * match.scale + match.offset;
        const double magnitude = std::abs(expected);
        const double error = std::abs(static_cast<double>(actual) - expected) / (magnitude < 1.0 ? 1.0 : magnitude);
        worst = (std::max)(worst, error);
    }
    return worst;
}

} // namespace an::bench
