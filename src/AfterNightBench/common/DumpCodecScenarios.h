// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "Scenario.h"

#include <cstdint>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace an::bench
{
inline std::vector<ScenarioSpec> dumpCodecScenarios()
{
    std::vector<ScenarioSpec> result;
    for (const auto* mode : {"roundtrip", "strip", "narrow"})
    {
        ScenarioSpec scenario;
        scenario.id = std::string("codec.dump_") + mode + ".f32";
        scenario.group = "codec";
        scenario.input.width = scenario.input.height = 2048u;
        scenario.input.channels = 1u;
        scenario.input.sampleType = SampleType::Float32;
        scenario.input.layout = Layout::Planar;
        scenario.input.byteOrder = ByteOrder::Little;
        scenario.corpusId = "synthetic.stars";
        scenario.seed = 20260822u;
        scenario.parameters["format"] = "DUMP";
        scenario.parameters["access"] = mode;
        scenario.correctnessClass = CorrectnessClass::BitExact;
        scenario.elementsProcessed = std::string_view(mode) == "roundtrip" ? 4194304u
                                     : std::string_view(mode) == "strip"   ? 2097152u
                                                                           : 262144u;
        scenario.bytesProcessed = scenario.elementsProcessed * 4u;
        scenario.allowedBackends = {BackendId::Unspecified};
        result.push_back(std::move(scenario));
    }
    return result;
}
inline bool matchesDumpCodecScenario(const ScenarioSpec& scenario)
{
    const auto access = scenario.parameters.find("access");
    const auto format = scenario.parameters.find("format");
    return scenario.input.width == 2048u && scenario.input.height == 2048u && scenario.input.channels == 1u &&
           scenario.input.sampleType == SampleType::Float32 && scenario.input.layout == Layout::Planar &&
           access != scenario.parameters.end() && format != scenario.parameters.end() && format->second == "DUMP" &&
           (access->second == "roundtrip" || access->second == "strip" || access->second == "narrow") &&
           scenario.id == "codec.dump_" + access->second + ".f32";
}
} // namespace an::bench
