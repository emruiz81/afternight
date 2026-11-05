// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The stack's frame weights written out from the rules the engine states for `computeStackFrameWeights`, for the cases
/// that hold the stacking stage, the Frame Quality dashboard's preview and the workflow report to the weights the stack
/// applies. Written out here, apart from the code under test, which asks the engine.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace an::tests::integration
{

/// Raw weights over their mean, as the engine leaves every weighting's table.
[[nodiscard]] inline std::vector<double> overTheirMean(std::vector<double> raw)
{
    const double mean = std::accumulate(raw.begin(), raw.end(), 0.0) / static_cast<double>(raw.size());
    for (double& weight : raw)
    {
        weight /= mean;
    }
    return raw;
}

/// The width weighting: each width penalized by twice itself times the stars it lacks against the reference's over its
/// own stars, and never below a tenth of itself; the inverse square rescaled so the widest frame is zero and the
/// narrowest one; raised to the floor; over the mean.
[[nodiscard]] inline std::vector<double> stackWidthWeights(const std::vector<double>& widths,
                                                           const std::vector<double>& stars,
                                                           const std::size_t reference, const double floor)
{
    std::vector<double> effective(widths.size());
    for (std::size_t frame = 0; frame < widths.size(); ++frame)
    {
        const double penalty = 2.0 * widths[frame] * (stars[reference] - stars[frame]) / stars[frame];
        effective[frame] = (std::max)(widths[frame] + penalty, 0.1 * widths[frame]);
    }
    const double narrowest = *std::min_element(effective.begin(), effective.end());
    const double widest = *std::max_element(effective.begin(), effective.end());
    std::vector<double> raw(widths.size());
    for (std::size_t frame = 0; frame < widths.size(); ++frame)
    {
        const double rescaled = (1.0 / (effective[frame] * effective[frame]) - 1.0 / (widest * widest)) /
                                (1.0 / (narrowest * narrowest) - 1.0 / (widest * widest));
        raw[frame] = (std::max)(rescaled, floor);
    }
    return overTheirMean(raw);
}

/// The roundness-weighted star count: each count times its stars' roundness, normalized between the fewest and the
/// most, squared, raised to the floor, over the mean.
[[nodiscard]] inline std::vector<double> stackRoundnessWeightedStarWeights(const std::vector<double>& stars,
                                                                           const std::vector<double>& roundness,
                                                                           const double floor)
{
    std::vector<double> effective(stars.size());
    for (std::size_t frame = 0; frame < stars.size(); ++frame)
    {
        effective[frame] = stars[frame] * roundness[frame];
    }
    const double fewest = *std::min_element(effective.begin(), effective.end());
    const double most = *std::max_element(effective.begin(), effective.end());
    std::vector<double> raw(stars.size());
    for (std::size_t frame = 0; frame < stars.size(); ++frame)
    {
        const double normal = (effective[frame] - fewest) / (most - fewest);
        raw[frame] = (std::max)(normal * normal, floor);
    }
    return overTheirMean(raw);
}

/// The noise weighting over frames whose scales are equal: one over the square of each frame's background noise, over
/// the mean.
[[nodiscard]] inline std::vector<double> stackNoiseWeights(const std::vector<double>& noise)
{
    std::vector<double> raw(noise.size());
    for (std::size_t frame = 0; frame < noise.size(); ++frame)
    {
        raw[frame] = 1.0 / (noise[frame] * noise[frame]);
    }
    return overTheirMean(raw);
}

} // namespace an::tests::integration
