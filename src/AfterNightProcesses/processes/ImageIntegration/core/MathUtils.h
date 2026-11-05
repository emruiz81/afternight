// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace ImageIntegration
{

/**
 * @brief Clamp a value to the [0, 1] range.
 *
 * Commonly used for normalized weights, confidence scores, and opacity values.
 *
 * @param v The value to clamp
 * @return The clamped value in [0.0, 1.0]
 */
[[nodiscard]] inline constexpr double clamp01(double v) noexcept
{
    return std::clamp(v, 0.0, 1.0);
}

/// The factor that makes a median absolute deviation estimate a normal distribution's standard deviation.
inline constexpr double kMadToSigma = 1.4826;

/**
 * @brief The median of a list of per-frame figures: the middle value, or the mean of the two middle values when the
 * list has an even length.
 *
 * The one median the integration model's policies share - the filter statistics, the outlier detector and the
 * reference choice read a list of per-frame measurements through it, so two policies never disagree about the middle
 * of the same list.
 *
 * @param values the figures, taken by value and reordered in place; non-finite values are the caller's to leave out.
 * @return the median, or 0 for an empty list.
 */
[[nodiscard]] inline double medianOf(std::vector<double> values)
{
    if (values.empty())
    {
        return 0.0;
    }
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if (values.size() % 2 != 0)
    {
        return upper;
    }
    const double lower = *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return (lower + upper) / 2.0;
}

/**
 * @brief The median absolute deviation of a list about a stated median, scaled by `kMadToSigma` to estimate a
 * standard deviation.
 *
 * @param values the figures.
 * @param median the list's median, as `medianOf` answers it.
 * @return the scaled deviation, or 0 for a list of fewer than two values.
 */
[[nodiscard]] inline double scaledMadOf(const std::vector<double>& values, const double median)
{
    if (values.size() < 2)
    {
        return 0.0;
    }
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (const double value : values)
    {
        deviations.push_back(std::abs(value - median));
    }
    return medianOf(std::move(deviations)) * kMadToSigma;
}

} // namespace ImageIntegration
