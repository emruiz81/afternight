// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-neutral drizzle geometry and a direct algebraic coordinate oracle.
#pragma once

#include "Scenario.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace an::bench
{

inline constexpr std::uint32_t kDrizzleGeometryWidth = 513u;
inline constexpr std::uint32_t kDrizzleGeometryHeight = 509u;
inline constexpr double kDrizzleGeometryScale = 1.75;
inline constexpr std::array<double, 9u> kDrizzleGeometryMatrix{1.001, -0.03,  2.5,     0.025, 0.999,
                                                               -1.75, 1.0e-5, -2.0e-5, 1.0};
inline constexpr std::array<double, 6u> kDrizzleGeometryPolynomialX{0.00002, 1.0, 0.0001, 0.0004, -0.0002, 0.0003};
inline constexpr std::array<double, 6u> kDrizzleGeometryPolynomialY{-0.00001, -0.0002, 1.0, -0.0003, 0.0001, 0.0002};
inline constexpr std::array<double, 3u> kDrizzleGeometrySipX{1.74e-6, -6.2e-7, 4.1e-7};
inline constexpr std::array<double, 3u> kDrizzleGeometrySipY{3.9e-7, 1.31e-6, -1.58e-6};

/// Coordinates here use integer centres. The runner adapts the geometry at its own boundary.
inline std::array<ScenarioSpec, 4u> drizzleGeometryScenarios()
{
    std::array<ScenarioSpec, 4u> scenarios;
    const std::array<const char*, 4u> ids{"drizzle.geometry.projective.dense", "drizzle.geometry.sip_polynomial.dense",
                                          "drizzle.geometry.sip_polynomial.bounded", "drizzle.geometry.corners"};
    for (std::size_t i = 0u; i < scenarios.size(); ++i)
    {
        auto& scenario = scenarios[i];
        scenario.id = ids[i];
        scenario.group = "drizzle";
        scenario.input.width = kDrizzleGeometryWidth;
        scenario.input.height = kDrizzleGeometryHeight;
        scenario.corpusId = "synthetic.uniform_noise";
        scenario.seed = 7u;
        scenario.correctnessClass = CorrectnessClass::GeometricResidual;
        scenario.tolerance = 0.0001;
        scenario.parameters = {{"fixtureMapping", "drizzle-source-map-v1"},
                               {"scale", "1.75"},
                               {"geometry", i == 0u ? "projective" : "sip-polynomial2"},
                               {"storage", i == 2u ? "bounded-32-halo-1" : "dense"}};
        scenario.elementsProcessed =
            static_cast<std::uint64_t>(kDrizzleGeometryWidth) * (kDrizzleGeometryHeight + (i == 2u ? 30u : 0u));
        if (i == 3u)
        {
            scenario.parameters["evaluation"] = "corners-half-0.5";
            scenario.elementsProcessed *= 4u;
        }
        scenario.bytesProcessed = scenario.elementsProcessed * 2u * sizeof(float);
        scenario.workUnit = "coordinate-pairs";
        scenario.allowedBackends = {BackendId::Unspecified};
    }
    return scenarios;
}

/// Refuses altered geometry or layout rather than silently measuring the built-in fixture.
inline std::optional<std::size_t> drizzleGeometryScenarioIndex(const ScenarioSpec& scenario)
{
    const auto expected = drizzleGeometryScenarios();
    for (std::size_t i = 0u; i < expected.size(); ++i)
    {
        if (scenario.id != expected[i].id || scenario.input != expected[i].input)
            continue;
        auto parameters = scenario.parameters;
        parameters.erase(kTimedBatchParameter);
        if (parameters == expected[i].parameters && scenario.inputSource == InputSource::Synthetic &&
            scenario.corpusId == expected[i].corpusId)
            return i;
    }
    return std::nullopt;
}

/// Direct long-double algebra, independent of the engine's polynomial and mapping code.
inline std::array<double, 2u> drizzleGeometryOracle(std::uint32_t column, std::uint32_t row, bool curved)
{
    long double x = column;
    long double y = row;
    if (curved)
    {
        const long double u = x - 256.0L;
        const long double v = y - 254.0L;
        x += kDrizzleGeometrySipX[0] * u * u + kDrizzleGeometrySipX[1] * u * v + kDrizzleGeometrySipX[2] * v * v;
        y += kDrizzleGeometrySipY[0] * u * u + kDrizzleGeometrySipY[1] * u * v + kDrizzleGeometrySipY[2] * v * v;
    }
    const auto& h = kDrizzleGeometryMatrix;
    const long double denominator = h[6] * x + h[7] * y + h[8];
    const long double px = (h[0] * x + h[1] * y + h[2]) / denominator;
    const long double py = (h[3] * x + h[4] * y + h[5]) / denominator;
    x = px;
    y = py;
    if (curved)
    {
        const long double u = (x - 256.0L) / 256.0L;
        const long double v = (y - 254.0L) / 256.0L;
        const std::array<long double, 6u> terms{1.0L, u, v, u * u, u * v, v * v};
        x = y = 0.0L;
        for (std::size_t i = 0u; i < terms.size(); ++i)
        {
            x += kDrizzleGeometryPolynomialX[i] * terms[i];
            y += kDrizzleGeometryPolynomialY[i] * terms[i];
        }
        x = x * 256.0L + 256.0L;
        y = y * 256.0L + 254.0L;
    }
    return {static_cast<double>((x - 0.25L) * kDrizzleGeometryScale + 0.5L * (kDrizzleGeometryScale - 1.0L)),
            static_cast<double>((y + 0.75L) * kDrizzleGeometryScale + 0.5L * (kDrizzleGeometryScale - 1.0L))};
}

/// Maximum absolute coordinate error in output pixels, with non-finite samples failing closed.
inline double drizzleGeometryResidual(std::uint32_t firstRow, std::span<const float> xMap, std::span<const float> yMap,
                                      bool curved)
{
    if (xMap.size() != yMap.size() || xMap.size() % kDrizzleGeometryWidth != 0u)
        return std::numeric_limits<double>::infinity();
    double worst = 0.0;
    for (std::size_t i = 0u; i < xMap.size(); ++i)
    {
        if (!std::isfinite(xMap[i]) || !std::isfinite(yMap[i]))
            return std::numeric_limits<double>::infinity();
        const auto expected =
            drizzleGeometryOracle(static_cast<std::uint32_t>(i % kDrizzleGeometryWidth),
                                  firstRow + static_cast<std::uint32_t>(i / kDrizzleGeometryWidth), curved);
        worst = (std::max)({worst, std::abs(static_cast<double>(xMap[i]) - expected[0]),
                            std::abs(static_cast<double>(yMap[i]) - expected[1])});
    }
    return worst;
}

/// Each corner is interleaved x,y; the map taps are independently computed and quantized first.
template <typename CoordinateReader> inline double drizzleCornerResidual(std::size_t count, CoordinateReader read)
{
    if (count != static_cast<std::size_t>(kDrizzleGeometryWidth) * kDrizzleGeometryHeight)
        return std::numeric_limits<double>::infinity();
    double worst = 0.0;
    for (std::size_t i = 0u; i < count; ++i)
    {
        const auto x = static_cast<std::uint32_t>(i % kDrizzleGeometryWidth);
        const auto y = static_cast<std::uint32_t>(i / kDrizzleGeometryWidth);
        for (std::size_t corner = 0u; corner < 4u; ++corner)
        {
            const double px = x + (corner == 0u || corner == 3u ? -0.5 : 0.5);
            const double py = y + (corner < 2u ? 0.5 : -0.5);
            const auto bx = static_cast<std::uint32_t>(std::clamp(std::floor(px), 0.0, kDrizzleGeometryWidth - 2.0));
            const auto by = static_cast<std::uint32_t>(std::clamp(std::floor(py), 0.0, kDrizzleGeometryHeight - 2.0));
            const float fx = static_cast<float>(px - bx), fy = static_cast<float>(py - by);
            const auto a = drizzleGeometryOracle(bx, by, true), b = drizzleGeometryOracle(bx + 1u, by, true);
            const auto c = drizzleGeometryOracle(bx, by + 1u, true), d = drizzleGeometryOracle(bx + 1u, by + 1u, true);
            for (std::size_t axis = 0u; axis < 2u; ++axis)
            {
                const float expected = (1.0f - fx) * (1.0f - fy) * static_cast<float>(a[axis]) +
                                       fx * (1.0f - fy) * static_cast<float>(b[axis]) +
                                       (1.0f - fx) * fy * static_cast<float>(c[axis]) +
                                       fx * fy * static_cast<float>(d[axis]);
                const double value = read(i, corner, axis);
                if (!std::isfinite(value))
                    return std::numeric_limits<double>::infinity();
                worst = (std::max)(worst, std::abs(value - expected));
            }
        }
    }
    return worst;
}

inline double drizzleCornerResidual(std::span<const std::array<float, 8u>> output)
{
    return drizzleCornerResidual(output.size(), [&](std::size_t pixel, std::size_t corner, std::size_t axis) {
        return output[pixel][corner * 2u + axis];
    });
}

} // namespace an::bench
