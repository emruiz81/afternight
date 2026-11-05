// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Complete drizzle scenarios, including allocation, mapping, accumulation and output finalization.
#pragma once

#include "DrizzleAccumulationFixture.h"

namespace an::bench
{

inline constexpr std::array<std::size_t, 6u> kDrizzleExecutionKernels{0u, 1u, 5u, 0u, 1u, 5u};
inline constexpr std::uint32_t kDrizzleExecutionFrames = 3u;
inline constexpr std::uint32_t kDrizzleExecutionSourceRows = 256u;

inline std::array<ScenarioSpec, 6u> drizzleExecutionScenarios()
{
    std::array<ScenarioSpec, 6u> result;
    const auto kernels = drizzleAccumulationScenarios();
    for (std::size_t i = 0u; i < result.size(); ++i)
    {
        auto& scenario = result[i];
        scenario = kernels[kDrizzleExecutionKernels[i]];
        scenario.id = "drizzle.execute." + scenario.parameters.at("kernel");
        if (i >= 3u)
        {
            scenario.id = "drizzle.execute.sum." + scenario.parameters.at("kernel");
            scenario.parameters["combination"] = "peak-normalized-sum";
        }
        scenario.parameters["measurement"] = "allocate-map-accumulate-finalize";
        scenario.parameters["frameCount"] = "3";
        scenario.parameters["frameOffsets"] = "0,0.125,0.25";
        scenario.parameters["sourceBandRows"] = std::to_string(kDrizzleExecutionSourceRows);
        scenario.elementsProcessed *= kDrizzleExecutionFrames;
        scenario.bytesProcessed = scenario.elementsProcessed * 4u +
                                  static_cast<std::uint64_t>(scenario.input.width) * scenario.input.height * 8u;
    }
    return result;
}

inline std::optional<std::size_t> drizzleExecutionScenarioIndex(const ScenarioSpec& scenario)
{
    const auto expected = drizzleExecutionScenarios();
    for (std::size_t i = 0u; i < expected.size(); ++i)
    {
        auto parameters = scenario.parameters;
        parameters.erase(kTimedBatchParameter);
        if (scenario.id == expected[i].id && scenario.input == expected[i].input &&
            parameters == expected[i].parameters && scenario.inputSource == InputSource::Synthetic &&
            scenario.corpusId == expected[i].corpusId)
            return kDrizzleExecutionKernels[i];
    }
    return std::nullopt;
}

inline std::array<std::vector<float>, kDrizzleExecutionFrames> drizzleExecutionFrames(std::span<const float> input)
{
    std::array<std::vector<float>, kDrizzleExecutionFrames> result;
    for (std::size_t frame = 0u; frame < result.size(); ++frame)
    {
        result[frame].assign(input.begin(), input.end());
        for (auto& value : result[frame])
            value += static_cast<float>(frame) * 0.125f;
    }
    return result;
}

inline DrizzleAccumulationOracle
drizzleExecutionOracle(const std::array<std::vector<float>, kDrizzleExecutionFrames>& frames, std::size_t kernel,
                       bool peakSum = false)
{
    auto result = drizzleAccumulationOracle(frames[0], kernel);
    for (std::size_t frame = 1u; frame < frames.size(); ++frame)
    {
        const auto next = drizzleAccumulationOracle(frames[frame], kernel);
        for (std::size_t i = 0u; i < result.means.size(); ++i)
            result.means[i] += next.means[i];
    }
    for (std::size_t i = 0u; i < result.means.size(); ++i)
    {
        result.means[i] /= static_cast<double>(frames.size());
        result.weights[i] *= static_cast<double>(frames.size());
    }
    if (peakSum)
    {
        double peak = 0.0;
        for (std::size_t i = 0u; i < result.means.size(); ++i)
        {
            result.means[i] = result.weights[i] > 0.0 ? result.means[i] * result.weights[i] : 0.0;
            peak = (std::max)(peak, result.means[i]);
        }
        if (peak > 0.0)
            for (auto& value : result.means)
                value = std::clamp(value / peak, 0.0, 1.0);
    }
    return result;
}

} // namespace an::bench
