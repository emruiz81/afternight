// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definitions of the viewer's tinted mask overlay and of a brush stroke blended into a paint plane.

#include "MaskRenderingOracle.h"

#include "MaskRenderingScenarios.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace an::bench
{

namespace
{

/// A value read as a coverage: below zero or not a number is zero, above one is one.
[[nodiscard]] float coverage(float value)
{
    if (std::isnan(value) || value < 0.0f)
    {
        return 0.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

/// The tip's coverage of a sample at a distance from its centre.
[[nodiscard]] float tipSample(float distance, float radius, float hardness)
{
    if (distance > radius)
    {
        return 0.0f;
    }
    if (hardness >= 0.999f)
    {
        return 1.0f;
    }
    const float inner = radius * hardness;
    if (distance <= inner)
    {
        return 1.0f;
    }
    const float range = (std::max)(radius - inner, 0.0001f);
    const float t = coverage((distance - inner) / range);
    const float edge = std::exp(-4.0f);
    return coverage((std::exp(-(4.0f * t * t)) - edge) / (1.0f - edge));
}

/// One coordinate of a uniform Catmull-Rom curve.
[[nodiscard]] float curveAt(double before, double start, double end, double after, float t)
{
    const float p0 = static_cast<float>(before);
    const float p1 = static_cast<float>(start);
    const float p2 = static_cast<float>(end);
    const float p3 = static_cast<float>(after);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (p2 - p0) * t + ((2.0f * p0) - (5.0f * p1) + (4.0f * p2) - p3) * t2 +
                   (-p0 + (3.0f * p1) - (3.0f * p2) + p3) * t3);
}

[[nodiscard]] float chord(const DabBlendPoint& from, const DabBlendPoint& to)
{
    return std::hypot(static_cast<float>(to.x - from.x), static_cast<float>(to.y - from.y));
}

} // namespace

bool requiresMaskOverlayOracle(const ScenarioSpec& scenario)
{
    return scenario.id == kMaskRenderTint;
}

void checkMaskOverlayOutput(std::span<const std::uint8_t> mask, std::span<const std::uint8_t> output, std::uint8_t red,
                            std::uint8_t green, std::uint8_t blue, ResultRecord& result)
{
    double largest = output.size() == mask.size() * 4u ? 0.0 : 255.0;
    double differing = 0.0;
    const std::size_t pixels = (std::min)(mask.size(), output.size() / 4u);
    for (std::size_t index = 0u; index < pixels; ++index)
    {
        const std::uint8_t expected[4] = {red, green, blue, static_cast<std::uint8_t>(255u - mask[index])};
        double pixelLargest = 0.0;
        for (std::size_t channel = 0u; channel < 4u; ++channel)
        {
            pixelLargest = (std::max)(pixelLargest, std::fabs(static_cast<double>(output[index * 4u + channel]) -
                                                              static_cast<double>(expected[channel])));
        }
        largest = (std::max)(largest, pixelLargest);
        differing += pixelLargest != 0.0 ? 1.0 : 0.0;
    }
    result.correctnessMetric = largest;
    result.counters["maskOverlayOracleVersion"] = 1.0;
    result.counters["maskOverlayOraclePixels"] = static_cast<double>(pixels);
    result.counters["maskOverlayOracleMaxAbs"] = largest;
    result.counters["maskOverlayOracleDifferingPixels"] = differing;
}

std::vector<DabBlendPoint> dabBlendStrokePoints(std::uint32_t width, std::uint32_t height)
{
    std::vector<DabBlendPoint> points;
    const double span = static_cast<double>(width) - 128.0;
    for (std::uint32_t index = 0u; index < 24u; ++index)
    {
        const double x = 64.0 + std::floor(span * index / 23.0 * 4.0) / 4.0;
        const double row = (index % 2u == 0u ? 0.2 : 0.8) * height + 3.5 * index;
        points.push_back(DabBlendPoint{.x = x, .y = std::floor(row * 4.0) / 4.0});
    }
    return points;
}

bool requiresDabBlendOracle(const ScenarioSpec& scenario)
{
    return scenario.id == kDabBlend;
}

std::vector<float> dabBlendExpected(std::span<const float> paint, std::uint32_t width, std::uint32_t height)
{
    // The tip.
    const int requested = (std::max)(1, static_cast<int>(std::ceil(kDabBlendDiameter)));
    const int side = requested % 2 == 0 ? requested + 1 : requested;
    const float radius = (std::max)(kDabBlendDiameter * 0.5f, 0.5f);
    const float centre = static_cast<float>(side - 1) * 0.5f;
    std::vector<float> tip(static_cast<std::size_t>(side) * side, 0.0f);
    for (int y = 0; y < side; ++y)
    {
        for (int x = 0; x < side; ++x)
        {
            const float dx = static_cast<float>(x) - centre;
            const float dy = static_cast<float>(y) - centre;
            tip[static_cast<std::size_t>(y * side + x)] =
                tipSample(std::sqrt((dx * dx) + (dy * dy)), radius, kDabBlendHardness);
        }
    }

    // The dabs, segment by segment, screened into a cleared stroke plane.
    std::vector<float> stroke(static_cast<std::size_t>(width) * height, 0.0f);
    const float spacing = (std::max)(1.0f, kDabBlendDiameter * kDabBlendSpacing);
    const float strength = kDabBlendFlow * 1.0f;
    const auto stamp = [&](double px, double py) {
        const long cx = std::lround(px);
        const long cy = std::lround(py);
        for (int ky = 0; ky < side; ++ky)
        {
            const long y = cy - side / 2 + ky;
            if (y < 0 || y >= static_cast<long>(height))
            {
                continue;
            }
            for (int kx = 0; kx < side; ++kx)
            {
                const long x = cx - side / 2 + kx;
                if (x < 0 || x >= static_cast<long>(width))
                {
                    continue;
                }
                const float alpha = tip[static_cast<std::size_t>(ky * side + kx)] * strength;
                if (alpha <= 0.0f)
                {
                    continue;
                }
                float& sample = stroke[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
                sample = 1.0f - ((1.0f - sample) * (1.0f - alpha));
            }
        }
    };
    const std::vector<DabBlendPoint> points = dabBlendStrokePoints(width, height);
    stamp(points[0].x, points[0].y);
    for (std::size_t index = 1u; index < points.size(); ++index)
    {
        const DabBlendPoint& p1 = points[index - 1u];
        const DabBlendPoint& p2 = points[index];
        const DabBlendPoint& p0 = points[index >= 2u ? index - 2u : index - 1u];
        const DabBlendPoint p3{.x = p2.x + (p2.x - p1.x), .y = p2.y + (p2.y - p1.y)};
        const float estimate = chord(p1, p2) + (0.25f * chord(p0, p1)) + (0.25f * chord(p2, p3));
        if (estimate <= 0.001f)
        {
            stamp(p2.x, p2.y);
            continue;
        }
        const int steps = (std::max)(1, static_cast<int>(std::ceil(estimate / spacing)));
        for (int step = 1; step <= steps; ++step)
        {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            stamp(curveAt(p0.x, p1.x, p2.x, p3.x, t), curveAt(p0.y, p1.y, p2.y, p3.y, t));
        }
    }

    // The stroke blended normally into the paint, from the paint as it was.
    std::vector<float> painted(paint.begin(), paint.end());
    for (std::size_t index = 0u; index < painted.size(); ++index)
    {
        const float source = coverage(stroke[index]);
        if (!(source > 0.0f))
        {
            continue;
        }
        const float base = coverage(paint[index]);
        painted[index] = coverage(base + ((source - base) * kDabBlendOpacity));
    }
    return painted;
}

void checkDabBlendOutput(std::span<const float> paint, std::uint32_t width, std::uint32_t height,
                         std::span<const float> output, ResultRecord& result)
{
    const std::vector<float> expected = dabBlendExpected(paint, width, height);
    double largest = expected.size() == output.size() ? 0.0 : 1.0;
    double differing = 0.0;
    const std::size_t count = (std::min)(expected.size(), output.size());
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double difference = std::fabs(static_cast<double>(output[index]) - static_cast<double>(expected[index]));
        largest = std::isnan(difference) ? 1.0 : (std::max)(largest, difference);
        differing += difference != 0.0 ? 1.0 : 0.0;
    }
    result.correctnessMetric = largest;
    result.counters["dabBlendOracleVersion"] = 1.0;
    result.counters["dabBlendOracleSamples"] = static_cast<double>(count);
    result.counters["dabBlendOracleMaxAbs"] = largest;
    result.counters["dabBlendOracleDifferingSamples"] = differing;
}

} // namespace an::bench
