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
/// The SER container workloads: eight 2048x2048 sixteen-bit mono frames read
/// from a container into retained frames, and the same eight frames written into a container and
/// published. The container carries unknown dates at the Unix epoch and a trailer of unknown timestamps,
/// so two runs write files that compare bit for bit.
inline std::vector<ScenarioSpec> serSequenceScenarios()
{
    std::vector<ScenarioSpec> result;
    for (const auto* mode : {"read", "write"})
    {
        ScenarioSpec scenario;
        scenario.id = std::string("sequence.ser_") + mode + ".8x_u16";
        scenario.group = "sequence";
        scenario.input.width = scenario.input.height = 2048u;
        scenario.input.channels = 1u;
        scenario.input.sampleType = SampleType::UInt16;
        scenario.input.layout = Layout::Planar;
        scenario.input.byteOrder = ByteOrder::Little;
        scenario.corpusId = "synthetic.stars";
        scenario.seed = 20260822u;
        scenario.parameters["format"] = "SER";
        scenario.parameters["mode"] = mode;
        scenario.parameters["frames"] = "8";
        scenario.correctnessClass = CorrectnessClass::BitExact;
        scenario.elementsProcessed = 8u * 2048u * 2048u;
        scenario.bytesProcessed = scenario.elementsProcessed * 2u;
        scenario.allowedBackends = {BackendId::Unspecified};
        result.push_back(std::move(scenario));
    }
    return result;
}

inline bool matchesSerSequenceScenario(const ScenarioSpec& scenario)
{
    auto parameters = scenario.parameters;
    parameters.erase("timedBatch");
    for (const auto& expected : serSequenceScenarios())
        if (scenario.id == expected.id)
            return scenario.group == expected.group && scenario.input == expected.input &&
                   parameters == expected.parameters && scenario.corpusId == expected.corpusId &&
                   scenario.seed == expected.seed && scenario.correctnessClass == expected.correctnessClass &&
                   scenario.elementsProcessed == expected.elementsProcessed &&
                   scenario.bytesProcessed == expected.bytesProcessed;
    return false;
}

inline bool isSerSequenceScenario(std::string_view id)
{
    return id.rfind("sequence.ser_", 0u) == 0u;
}
} // namespace an::bench
