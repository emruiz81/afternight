// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "BackgroundModelOracle.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace an::bench
{
namespace
{

/// How many samples apart the digest lattice is.
constexpr std::uint32_t kDigestStride = 41u;

/// How far outside the well's radius the surrounding annulus begins, as a multiple of it.
constexpr double kAnnulusInner = 1.6;

/// Where it ends.
constexpr double kAnnulusOuter = 2.2;

/// A coordinate mapped onto the interval the harness's gradient is expressed over.
///
/// @param value the pixel coordinate.
/// @param extent how many pixels the axis holds.
/// @return the normalised coordinate, spanning `[-1, 1]` across the frame.
[[nodiscard]] double normalised(double value, std::uint32_t extent)
{
    if (extent <= 1u)
    {
        return 0.0;
    }
    return value / static_cast<double>(extent - 1u) * 2.0 - 1.0;
}

/// The median of a mutable array, which it reorders.
///
/// @param values the array.
/// @return the median, or zero for an empty array.
[[nodiscard]] double medianOf(std::vector<double>& values)
{
    if (values.empty())
    {
        return 0.0;
    }
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if ((values.size() % 2u) != 0u)
    {
        return upper;
    }
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle - 1u), values.end());
    return (values[middle - 1u] + upper) * 0.5;
}

/// The medians of a frame's tiles, in row-major tile order.
///
/// @param samples the frame.
/// @param width the frame width.
/// @param height the frame height.
/// @return one median per tile, or an empty vector when the frame cannot be tiled.
[[nodiscard]] std::vector<double> tileMedians(std::span<const float> samples, std::uint32_t width, std::uint32_t height)
{
    if (width < kBackgroundMetricTiles || height < kBackgroundMetricTiles ||
        samples.size() < static_cast<std::size_t>(width) * height)
    {
        return {};
    }
    std::vector<double> medians;
    medians.reserve(static_cast<std::size_t>(kBackgroundMetricTiles) * kBackgroundMetricTiles);
    std::vector<double> tile;
    for (std::uint32_t row = 0u; row < kBackgroundMetricTiles; ++row)
    {
        const std::uint32_t firstRow = height * row / kBackgroundMetricTiles;
        const std::uint32_t lastRow = height * (row + 1u) / kBackgroundMetricTiles;
        for (std::uint32_t column = 0u; column < kBackgroundMetricTiles; ++column)
        {
            const std::uint32_t firstColumn = width * column / kBackgroundMetricTiles;
            const std::uint32_t lastColumn = width * (column + 1u) / kBackgroundMetricTiles;
            tile.clear();
            for (std::uint32_t y = firstRow; y < lastRow; ++y)
            {
                for (std::uint32_t x = firstColumn; x < lastColumn; ++x)
                {
                    const float sample = samples[static_cast<std::size_t>(y) * width + x];
                    if (std::isfinite(sample))
                    {
                        tile.push_back(static_cast<double>(sample));
                    }
                }
            }
            medians.push_back(medianOf(tile));
        }
    }
    return medians;
}

/// The standard deviation of a set of numbers.
///
/// @param values the numbers.
/// @return the deviation, or zero for fewer than two.
[[nodiscard]] double deviationOf(const std::vector<double>& values)
{
    if (values.size() < 2u)
    {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : values)
    {
        sum += value;
    }
    const double mean = sum / static_cast<double>(values.size());
    double squares = 0.0;
    for (const double value : values)
    {
        squares += (value - mean) * (value - mean);
    }
    return std::sqrt(squares / static_cast<double>(values.size()));
}

/// The radius of the harness's well over one frame, in pixels.
///
/// @param width the frame width.
/// @param height the frame height.
/// @return the radius.
[[nodiscard]] double wellRadius(std::uint32_t width, std::uint32_t height)
{
    return kDarkStructureRadiusFraction * static_cast<double>((std::min)(width, height));
}

} // namespace

std::vector<float> backgroundGradientFrame(std::span<const float> samples, std::uint32_t width, std::uint32_t height)
{
    std::vector<float> frame(samples.begin(), samples.end());
    if (frame.size() < static_cast<std::size_t>(width) * height)
    {
        return frame;
    }
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const double ny = normalised(static_cast<double>(y), height);
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double nx = normalised(static_cast<double>(x), width);
            const double gradient = kBackgroundGradientOffset + kBackgroundGradientSlopeX * nx +
                                    kBackgroundGradientSlopeY * ny + kBackgroundGradientCurvature * nx * nx;
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            frame[index] = static_cast<float>(static_cast<double>(frame[index]) + gradient);
        }
    }
    return frame;
}

double backgroundTileSpread(std::span<const float> samples, std::uint32_t width, std::uint32_t height)
{
    const std::vector<double> medians = tileMedians(samples, width, height);
    return deviationOf(medians);
}

double backgroundGradientRatio(std::span<const float> corrected, std::span<const float> input, std::uint32_t width,
                               std::uint32_t height)
{
    for (const float sample : corrected)
    {
        if (!std::isfinite(sample))
        {
            return (std::numeric_limits<double>::max)();
        }
    }
    const double before = backgroundTileSpread(input, width, height);
    if (!(before > 0.0))
    {
        return (std::numeric_limits<double>::max)();
    }
    const double after = backgroundTileSpread(corrected, width, height);
    const double ratio = after / before;
    return std::isfinite(ratio) ? ratio : (std::numeric_limits<double>::max)();
}

std::string backgroundText(std::span<const float> corrected, std::uint32_t width, std::uint32_t height)
{
    std::ostringstream text;
    text << std::setprecision(9);
    const std::vector<double> medians = tileMedians(corrected, width, height);
    for (const double median : medians)
    {
        text << median << ';';
    }
    text << '|';
    for (std::size_t index = 0u; index < corrected.size(); index += kDigestStride)
    {
        text << corrected[index] << ';';
    }
    return text.str();
}

std::vector<float> darkStructureFrame(std::span<const float> samples, std::uint32_t width, std::uint32_t height)
{
    std::vector<float> frame(samples.begin(), samples.end());
    if (frame.size() < static_cast<std::size_t>(width) * height)
    {
        return frame;
    }
    const double radius = wellRadius(width, height);
    const double centreX = static_cast<double>(width) * 0.5;
    const double centreY = static_cast<double>(height) * 0.5;
    for (float& sample : frame)
    {
        sample = static_cast<float>(static_cast<double>(sample) + kDarkStructureSky);
    }
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = static_cast<double>(x) - centreX;
            const double dy = static_cast<double>(y) - centreY;
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius)
            {
                continue;
            }
            // A raised-cosine profile, so the well has an edge a multiscale mask can follow rather
            // than a step every scale of the transform would ring on.
            const double fall = 0.5 * (1.0 + std::cos(distance / radius * 3.14159265358979323846));
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            frame[index] = static_cast<float>(static_cast<double>(frame[index]) - kDarkStructureDepth * fall);
        }
    }
    return frame;
}

double darkStructureContrast(std::span<const float> samples, std::uint32_t width, std::uint32_t height)
{
    if (samples.size() < static_cast<std::size_t>(width) * height)
    {
        return 0.0;
    }
    const double radius = wellRadius(width, height);
    const double centreX = static_cast<double>(width) * 0.5;
    const double centreY = static_cast<double>(height) * 0.5;
    std::vector<double> inside;
    std::vector<double> around;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const double dx = static_cast<double>(x) - centreX;
            const double dy = static_cast<double>(y) - centreY;
            const double distance = std::sqrt(dx * dx + dy * dy);
            const float sample = samples[static_cast<std::size_t>(y) * width + x];
            if (!std::isfinite(sample))
            {
                continue;
            }
            if (distance <= radius * 0.5)
            {
                inside.push_back(static_cast<double>(sample));
            }
            else if (distance >= radius * kAnnulusInner && distance <= radius * kAnnulusOuter)
            {
                around.push_back(static_cast<double>(sample));
            }
        }
    }
    if (inside.empty() || around.empty())
    {
        return 0.0;
    }
    const double surround = medianOf(around);
    if (!(surround > 0.0))
    {
        return 0.0;
    }
    return (surround - medianOf(inside)) / surround;
}

double darkStructureContrastRatio(std::span<const float> enhanced, std::span<const float> input, std::uint32_t width,
                                  std::uint32_t height)
{
    for (const float sample : enhanced)
    {
        if (!std::isfinite(sample))
        {
            return (std::numeric_limits<double>::max)();
        }
    }
    const double before = darkStructureContrast(input, width, height);
    const double after = darkStructureContrast(enhanced, width, height);
    if (!(before > 0.0) || !(after > 0.0))
    {
        return (std::numeric_limits<double>::max)();
    }
    const double ratio = before / after;
    return std::isfinite(ratio) ? ratio : (std::numeric_limits<double>::max)();
}

std::string darkStructureText(std::span<const float> enhanced, std::uint32_t width, std::uint32_t height)
{
    std::ostringstream text;
    text << std::setprecision(9);
    text << darkStructureContrast(enhanced, width, height) << '|';
    for (std::size_t index = 0u; index < enhanced.size(); index += kDigestStride)
    {
        text << enhanced[index] << ';';
    }
    return text.str();
}

} // namespace an::bench
