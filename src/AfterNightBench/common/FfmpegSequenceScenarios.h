// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "Scenario.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an::bench
{
/// The video workload: eight 2048x2048 sixteen-bit mono frames stored losslessly
/// as FFV1 in Matroska, with sixteen slices per frame, opened, decoded into retained frames and closed.
/// The runner writes the container with the bitexact FFmpeg writer before timing, so the files and
/// the decoded frames compare bit for bit.
inline std::vector<ScenarioSpec> ffmpegSequenceScenarios()
{
    ScenarioSpec scenario;
    scenario.id = "sequence.ffmpeg_read.ffv1_8x_u16";
    scenario.group = "sequence";
    scenario.input.width = scenario.input.height = 2048u;
    scenario.input.channels = 1u;
    scenario.input.sampleType = SampleType::UInt16;
    scenario.input.layout = Layout::Planar;
    scenario.input.byteOrder = ByteOrder::Little;
    scenario.corpusId = "synthetic.stars";
    scenario.seed = 20260822u;
    scenario.parameters["format"] = "FFV1";
    scenario.parameters["container"] = "matroska";
    scenario.parameters["mode"] = "read";
    scenario.parameters["frames"] = "8";
    scenario.parameters["slices"] = "16";
    scenario.correctnessClass = CorrectnessClass::BitExact;
    scenario.elementsProcessed = 8u * 2048u * 2048u;
    scenario.bytesProcessed = scenario.elementsProcessed * 2u;
    scenario.allowedBackends = {BackendId::Unspecified};
    ScenarioSpec cached = scenario;
    cached.id = "sequence.ffmpeg_cache.ffv1_8x_u16";
    cached.parameters["mode"] = "cache";
    cached.parameters["access"] = "0,1,0,1,0,1,2,3";
    cached.parameters["cacheFrames"] = "2";
    return {std::move(scenario), std::move(cached)};
}

inline bool matchesFfmpegSequenceScenario(const ScenarioSpec& scenario)
{
    auto parameters = scenario.parameters;
    parameters.erase("timedBatch");
    for (const auto& expected : ffmpegSequenceScenarios())
        if (scenario.id == expected.id)
            return scenario.group == expected.group && scenario.input == expected.input &&
                   parameters == expected.parameters && scenario.corpusId == expected.corpusId &&
                   scenario.seed == expected.seed && scenario.correctnessClass == expected.correctnessClass &&
                   scenario.elementsProcessed == expected.elementsProcessed &&
                   scenario.bytesProcessed == expected.bytesProcessed;
    return false;
}

inline bool isFfmpegSequenceScenario(std::string_view id)
{
    return id.rfind("sequence.ffmpeg_", 0u) == 0u;
}
} // namespace an::bench
