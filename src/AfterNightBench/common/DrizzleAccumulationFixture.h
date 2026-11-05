// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Matched kernel fixtures and an engine-neutral direct gather oracle.
#pragma once

#include "Scenario.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

namespace an::bench
{

inline constexpr std::uint32_t kDrizzleAccumulationWidth = 513u;
inline constexpr std::uint32_t kDrizzleAccumulationHeight = 509u;

inline std::array<ScenarioSpec, 6u> drizzleAccumulationScenarios()
{
    std::array<ScenarioSpec, 6u> scenarios;
    const std::array<const char*, 6u> names{"square", "point", "turbo", "gaussian", "lanczos2", "lanczos3"};
    for (std::size_t i = 0u; i < scenarios.size(); ++i)
    {
        auto& scenario = scenarios[i];
        scenario.id = std::string("drizzle.accumulation.") + names[i];
        scenario.group = "drizzle";
        scenario.input.width = kDrizzleAccumulationWidth;
        scenario.input.height = kDrizzleAccumulationHeight;
        scenario.corpusId = "synthetic.uniform_noise";
        scenario.seed = 7u;
        scenario.correctnessClass = CorrectnessClass::AbsoluteRelative;
        scenario.tolerance = 0.0001;
        scenario.parameters = {{"fixtureMapping", "quarter-pixel-translation-v1"},
                               {"scale", "1"},
                               {"pixelFraction", "1"},
                               {"kernel", names[i]},
                               {"measurement", "clear-and-accumulate"}};
        scenario.elementsProcessed = static_cast<std::uint64_t>(scenario.input.width) * scenario.input.height;
        scenario.bytesProcessed = scenario.elementsProcessed * 28u;
        scenario.workUnit = "source-samples";
        scenario.allowedBackends = {BackendId::Unspecified};
    }
    return scenarios;
}

inline std::optional<std::size_t> drizzleAccumulationScenarioIndex(const ScenarioSpec& scenario)
{
    const auto expected = drizzleAccumulationScenarios();
    for (std::size_t i = 0u; i < expected.size(); ++i)
    {
        auto parameters = scenario.parameters;
        parameters.erase(kTimedBatchParameter);
        if (scenario.id == expected[i].id && scenario.input == expected[i].input &&
            parameters == expected[i].parameters && scenario.inputSource == InputSource::Synthetic &&
            scenario.corpusId == expected[i].corpusId)
            return i;
    }
    return std::nullopt;
}

struct DrizzleAccumulationOracle
{
    std::vector<double> means;
    std::vector<double> weights;
};

// Destination-first gather over an analytic translated lattice, with independently evaluated
// coefficients. No engine mapping, interpolation, table or scatter operation is called.
inline DrizzleAccumulationOracle drizzleAccumulationOracle(std::span<const float> samples, std::size_t kernel)
{
    DrizzleAccumulationOracle result{std::vector<double>(samples.size()), std::vector<double>(samples.size())};
    const auto axis = [kernel](long double distance) {
        if (kernel == 1u)
            return std::abs(distance) < 0.5L ? 1.0L : 0.0L;
        if (kernel == 0u || kernel == 2u)
            return (std::max)(0.0L, 1.0L - std::abs(distance));
        if (kernel == 3u)
        {
            constexpr long double kSigma = 1.0L / 2.3548L;
            if (distance < -2.25L || distance > 1.75L)
                return 0.0L;
            return std::exp(-0.5L * distance * distance / (kSigma * kSigma)) /
                   std::sqrt(2 * std::numbers::pi_v<long double> * kSigma * kSigma);
        }
        const int order = kernel == 4u ? 2 : 3;
        const long double x = std::floor(std::abs(distance) * 100.0L + 0.5L) / 100.0L;
        if (x >= order)
            return 0.0L;
        if (x == 0.0L)
            return 1.0L;
        const long double a = std::numbers::pi_v<long double> * x;
        return std::sin(a) / a * std::sin(a / order) / (a / order);
    };
    std::array<long double, 9u> factors{};
    for (int i = -4; i <= 4; ++i)
        factors[i + 4] = axis(i - 0.25L);
    for (int y = 0; y < static_cast<int>(kDrizzleAccumulationHeight); ++y)
        for (int x = 0; x < static_cast<int>(kDrizzleAccumulationWidth); ++x)
        {
            long double sum = 0.0L, weightSum = 0.0L;
            for (int sy = (std::max)(0, y - 4);
                 sy <= (std::min)(static_cast<int>(kDrizzleAccumulationHeight) - 1, y + 4); ++sy)
                for (int sx = (std::max)(0, x - 4);
                     sx <= (std::min)(static_cast<int>(kDrizzleAccumulationWidth) - 1, x + 4); ++sx)
                {
                    const long double weight = factors[x - sx + 4] * factors[y - sy + 4];
                    if (std::abs(weight) < 1e-10L)
                        continue;
                    sum += samples[static_cast<std::size_t>(sy) * kDrizzleAccumulationWidth + sx] * weight;
                    weightSum += weight;
                }
            const auto index = static_cast<std::size_t>(y) * kDrizzleAccumulationWidth + x;
            result.means[index] = weightSum > 0 ? static_cast<double>(sum / weightSum) : 0.0;
            result.weights[index] = static_cast<double>(weightSum);
        }
    return result;
}

template <typename ReadMean, typename ReadWeight>
double drizzleAccumulationResidual(const DrizzleAccumulationOracle& oracle, ReadMean mean, ReadWeight weight)
{
    double worst = 0.0;
    for (std::size_t i = 0u; i < oracle.means.size(); ++i)
    {
        const double value = mean(i), total = weight(i);
        if (!std::isfinite(value) || !std::isfinite(total))
            return std::numeric_limits<double>::infinity();
        worst = (std::max)({worst, std::abs(value - oracle.means[i]), std::abs(total - oracle.weights[i])});
    }
    return worst;
}

} // namespace an::bench
