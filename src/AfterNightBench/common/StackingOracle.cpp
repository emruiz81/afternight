// AfterNight benchmark system - engine-neutral oracles for the shared stacking scenarios.
//
// The auto-adaptive oracle restates DeepSkyStacker's `AutoAdaptiveWeightedAverage`
// (https://github.com/deepskystacker/DSS), whose licence states `Copyright (c) 2006-2019, LucCoiffier`
// and `Copyright (c) 2018-2023, David C. Partridge, Tony Cook, Mat Draper, Simon C. Smith, Vitali
// Pelenjow, Michal Schulz, Martin Toeltsch` and the three-clause BSD terms (BSD-3-Clause); it is part of
// that derived work and is distributed under the GNU General Public License, version 3 or any later
// version, with that notice kept, in full in `src/AfterNightEngine/THIRD_PARTY_NOTICES.md`. The
// provenance note is `docs/dev/DEEPSKYSTACKER_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "StackingOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace an::bench
{
namespace
{

/// Whether every frame is the result's size.
[[nodiscard]] bool framesMatch(std::span<const std::span<const float>> frames, std::span<const float> result) noexcept
{
    for (const std::span<const float> frame : frames)
    {
        if (frame.size() != result.size())
        {
            return false;
        }
    }
    return true;
}

/// The non-zero samples of one element in frame order.
void gatherColumn(std::span<const std::span<const float>> frames, std::size_t index, std::vector<double>& column)
{
    column.clear();
    for (const std::span<const float> frame : frames)
    {
        const float sample = frame[index];
        if (sample != 0.0f)
        {
            column.push_back(static_cast<double>(sample));
        }
    }
}

/// The worst of a running residual and a new one, treating a non-finite residual as the worst.
void widen(double& worst, double expected, float actual) noexcept
{
    const double residual = std::fabs(static_cast<double>(actual) - expected);
    if (!(residual <= worst))
    {
        worst = residual;
    }
}

/// As `widen`, accepting the expected value held to the unit interval as well when the row allows it.
void widenAccepting(double& worst, double expected, float actual, StackClamp clamp) noexcept
{
    if (clamp == StackClamp::Accepted)
    {
        const double clamped = std::clamp(expected, 0.0, 1.0);
        const double residual = (std::min)(std::fabs(static_cast<double>(actual) - expected),
                                           std::fabs(static_cast<double>(actual) - clamped));
        if (!(residual <= worst))
        {
            worst = residual;
        }
        return;
    }
    widen(worst, expected, actual);
}

/// The plain mean of a column, zero for an empty one.
[[nodiscard]] double meanOf(const std::vector<double>& column) noexcept
{
    if (column.empty())
    {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : column)
    {
        sum = sum + value;
    }
    return sum / static_cast<double>(column.size());
}

/// The adopted auto-adaptive weighted mean of a column, in its own operation order.
[[nodiscard]] double autoAdaptiveOf(std::vector<double>& column, std::vector<double>& weights) noexcept
{
    // These four constants mirror the engine's published ones; the direct suite pins the engine's
    // against the same algorithm, and this file cannot include an engine header.
    constexpr double kScale = 65535.0;
    constexpr std::uint32_t kIterations = 5u;
    constexpr double kSmallSigma = 5.0;
    constexpr std::size_t kMinimumSamples = 3u;
    const std::size_t count = column.size();
    if (count == 0u)
    {
        return 0.0;
    }
    const double n = static_cast<double>(count);
    double sum = 0.0;
    for (const double value : column)
    {
        sum = sum + value * kScale;
    }
    double average = sum / n;
    if (count < kMinimumSamples)
    {
        return average / kScale;
    }
    weights.assign(count, 0.0);
    for (std::uint32_t iteration = 0u; iteration < kIterations; ++iteration)
    {
        double squares = 0.0;
        for (const double value : column)
        {
            const double deviation = value * kScale - average;
            squares = squares + deviation * deviation;
        }
        const double sigma = std::sqrt(squares / n);
        if (iteration == 0u && sigma <= kSmallSigma)
        {
            return average / kScale;
        }
        for (std::size_t index = 0u; index < count; ++index)
        {
            const double distance = std::fabs(column[index] * kScale - average);
            const double ratio = distance / sigma;
            weights[index] = 1.0 / (1.0 + ratio * ratio);
        }
        double total = 0.0;
        for (const double weight : weights)
        {
            total = total + weight;
        }
        double next = 0.0;
        for (std::size_t index = 0u; index < count; ++index)
        {
            next = next + (column[index] * kScale * weights[index]) / total;
        }
        average = next;
    }
    return average / kScale;
}

} // namespace

double averageStackResidual(std::span<const std::span<const float>> frames, std::span<const float> result,
                            StackClamp clamp) noexcept
{
    if (!framesMatch(frames, result))
    {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        double sum = 0.0;
        std::uint32_t count = 0u;
        for (const std::span<const float> frame : frames)
        {
            const float sample = frame[index];
            if (sample == 0.0f)
            {
                continue;
            }
            sum = sum + static_cast<double>(sample);
            ++count;
        }
        widenAccepting(worst, count == 0u ? 0.0 : sum / static_cast<double>(count), result[index], clamp);
    }
    return worst;
}

double weightedAverageStackResidual(std::span<const std::span<const float>> frames, std::span<const double> weights,
                                    std::span<const float> result, StackClamp clamp) noexcept
{
    if (!framesMatch(frames, result) || weights.size() != frames.size())
    {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        double sum = 0.0;
        double total = 0.0;
        for (std::size_t frame = 0u; frame < frames.size(); ++frame)
        {
            const float sample = frames[frame][index];
            if (sample == 0.0f)
            {
                continue;
            }
            sum = sum + static_cast<double>(sample) * weights[frame];
            total = total + weights[frame];
        }
        widenAccepting(worst, total == 0.0 ? 0.0 : sum / total, result[index], clamp);
    }
    return worst;
}

double sumStackResidual(std::span<const std::span<const float>> frames, std::span<const float> result) noexcept
{
    if (!framesMatch(frames, result))
    {
        return std::numeric_limits<double>::infinity();
    }
    std::vector<double> sums(result.size(), 0.0);
    double peak = 0.0;
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        double sum = 0.0;
        for (const std::span<const float> frame : frames)
        {
            const float sample = frame[index];
            if (sample != 0.0f)
            {
                sum = sum + static_cast<double>(sample);
            }
        }
        sums[index] = sum;
        peak = sum > peak ? sum : peak;
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        const double expected = peak > 0.0 ? std::clamp(sums[index] / peak, 0.0, 1.0) : 0.0;
        widen(worst, expected, result[index]);
    }
    return worst;
}

double medianStackResidual(std::span<const std::span<const float>> frames, std::span<const float> result,
                           StackClamp clamp) noexcept
{
    if (!framesMatch(frames, result))
    {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    std::vector<double> column;
    column.reserve(frames.size());
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        gatherColumn(frames, index, column);
        double expected = 0.0;
        if (!column.empty())
        {
            std::sort(column.begin(), column.end());
            const std::size_t half = column.size() / 2u;
            expected = column.size() % 2u == 1u ? column[half] : (column[half - 1u] + column[half]) * 0.5;
        }
        widenAccepting(worst, expected, result[index], clamp);
    }
    return worst;
}

double minimumStackResidual(std::span<const std::span<const float>> frames, std::span<const float> result) noexcept
{
    if (!framesMatch(frames, result))
    {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        double expected = 0.0;
        bool any = false;
        for (const std::span<const float> frame : frames)
        {
            const float sample = frame[index];
            if (sample == 0.0f)
            {
                continue;
            }
            expected = any ? (std::min)(expected, static_cast<double>(sample)) : static_cast<double>(sample);
            any = true;
        }
        widen(worst, expected, result[index]);
    }
    return worst;
}

double maximumStackResidual(std::span<const std::span<const float>> frames, std::span<const float> result) noexcept
{
    if (!framesMatch(frames, result))
    {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        double expected = 0.0;
        bool any = false;
        for (const std::span<const float> frame : frames)
        {
            const float sample = frame[index];
            if (sample == 0.0f)
            {
                continue;
            }
            expected = any ? (std::max)(expected, static_cast<double>(sample)) : static_cast<double>(sample);
            any = true;
        }
        widen(worst, expected, result[index]);
    }
    return worst;
}

double autoAdaptiveStackResidual(std::span<const std::span<const float>> frames, std::span<const float> result) noexcept
{
    if (!framesMatch(frames, result))
    {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    std::vector<double> column;
    std::vector<double> weights;
    column.reserve(frames.size());
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        gatherColumn(frames, index, column);
        widen(worst, autoAdaptiveOf(column, weights), result[index]);
    }
    return worst;
}

double rejectedAverageStackResidual(std::span<const std::span<const float>> frames, std::uint32_t width,
                                    std::span<const StackRowRange> excludedRows, std::span<const float> result) noexcept
{
    if (!framesMatch(frames, result) || excludedRows.size() != frames.size() || width == 0u)
    {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    std::vector<double> column;
    column.reserve(frames.size());
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        const auto row = static_cast<std::uint32_t>(index / width);
        column.clear();
        for (std::size_t frame = 0u; frame < frames.size(); ++frame)
        {
            if (row >= excludedRows[frame].begin && row < excludedRows[frame].end)
            {
                continue;
            }
            const float sample = frames[frame][index];
            if (sample != 0.0f)
            {
                column.push_back(static_cast<double>(sample));
            }
        }
        widen(worst, meanOf(column), result[index]);
    }
    return worst;
}

PlantedStackMovement plantedStackMovement(std::span<const float> planted, std::span<const float> clean,
                                          std::span<const std::span<const float>> plantedFrames,
                                          std::span<const std::span<const float>> captured, float plantedValue,
                                          float ceiling) noexcept
{
    PlantedStackMovement movement;
    if (planted.size() != clean.size() || captured.empty() || plantedFrames.size() != captured.size() ||
        !framesMatch(captured, clean) || !framesMatch(plantedFrames, clean))
    {
        movement.fraction = std::numeric_limits<double>::infinity();
        return movement;
    }
    const double kept =
        (static_cast<double>(plantedValue) - static_cast<double>(ceiling)) / static_cast<double>(captured.size());
    const double bound = kept / 2.0;
    for (std::size_t index = 0u; index < planted.size(); ++index)
    {
        if (!std::isfinite(planted[index]) || !std::isfinite(clean[index]))
        {
            movement.fraction = std::numeric_limits<double>::infinity();
            return movement;
        }
        bool apart = true;
        bool replaced = false;
        for (std::size_t frame = 0u; frame < captured.size(); ++frame)
        {
            apart = apart && captured[frame][index] < ceiling;
            replaced = replaced || plantedFrames[frame][index] != captured[frame][index];
        }
        if (!apart || !replaced)
        {
            continue;
        }
        ++movement.judged;
        const double shift = std::fabs(static_cast<double>(planted[index]) - static_cast<double>(clean[index]));
        movement.largest = (std::max)(movement.largest, shift);
        movement.moved += shift > bound ? 1u : 0u;
    }
    movement.fraction =
        movement.judged == 0u ? 0.0 : static_cast<double>(movement.moved) / static_cast<double>(movement.judged);
    return movement;
}

double planeResidual(std::span<const float> expected, std::span<const float> result) noexcept
{
    if (expected.size() != result.size())
    {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (std::size_t index = 0u; index < result.size(); ++index)
    {
        widen(worst, static_cast<double>(expected[index]), result[index]);
    }
    return worst;
}

} // namespace an::bench
