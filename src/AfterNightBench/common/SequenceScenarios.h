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
inline std::vector<ScenarioSpec> sequenceScenarios(bool boundedCache = true)
{
    std::vector<ScenarioSpec> result;
    for (const auto* mode : {"lazy", "eager", "cached", "dummy"})
    {
        if (!boundedCache && std::string_view(mode) == "cached")
            continue;
        ScenarioSpec scenario;
        scenario.id = std::string("sequence.") + mode + ".4x_f32";
        scenario.group = "sequence";
        scenario.input.width = scenario.input.height = 2048u;
        scenario.input.channels = 1u;
        scenario.input.sampleType = SampleType::Float32;
        scenario.input.layout = Layout::Planar;
        scenario.input.byteOrder = ByteOrder::Little;
        scenario.corpusId = "synthetic.stars";
        scenario.seed = 20260822u;
        scenario.parameters["mode"] = mode;
        scenario.parameters["frames"] = "4";
        scenario.parameters["format"] = "FITS";
        scenario.correctnessClass = CorrectnessClass::BitExact;
        scenario.elementsProcessed = 4u * 2048u * 2048u;
        scenario.bytesProcessed = scenario.elementsProcessed * 4u;
        scenario.allowedBackends = {BackendId::Unspecified};
        result.push_back(std::move(scenario));
    }
    return result;
}

inline bool matchesSequenceScenario(const ScenarioSpec& scenario, bool boundedCache = true)
{
    auto parameters = scenario.parameters;
    parameters.erase("timedBatch");
    for (const auto& expected : sequenceScenarios(boundedCache))
        if (scenario.id == expected.id)
            return scenario.group == expected.group && scenario.input == expected.input &&
                   parameters == expected.parameters && scenario.corpusId == expected.corpusId &&
                   scenario.seed == expected.seed && scenario.correctnessClass == expected.correctnessClass &&
                   scenario.elementsProcessed == expected.elementsProcessed &&
                   scenario.bytesProcessed == expected.bytesProcessed;
    return false;
}

/// Eight FITS frames and their `.anseq` manifest published as one image-list sequence.
inline ScenarioSpec imageListWriteScenario()
{
    ScenarioSpec scenario;
    scenario.id = "sequence.image_list_write.8x_f32";
    scenario.group = "image-list-write";
    scenario.input.width = scenario.input.height = 1024u;
    scenario.input.channels = 1u;
    scenario.input.sampleType = SampleType::Float32;
    scenario.input.layout = Layout::Planar;
    scenario.input.byteOrder = ByteOrder::Little;
    scenario.corpusId = "synthetic.stars";
    scenario.seed = 20260822u;
    scenario.parameters = {{"mode", "write"}, {"frames", "8"}, {"format", "FITS"}};
    scenario.correctnessClass = CorrectnessClass::BitExact;
    scenario.elementsProcessed = 8u * 1024u * 1024u;
    scenario.bytesProcessed = scenario.elementsProcessed * 4u;
    scenario.allowedBackends = {BackendId::Unspecified};
    return scenario;
}

/// Whether a row is the paired image-list publication workload, allowing only `timedBatch` to vary.
inline bool matchesImageListWriteScenario(const ScenarioSpec& scenario)
{
    auto parameters = scenario.parameters;
    parameters.erase("timedBatch");
    const auto expected = imageListWriteScenario();
    return scenario.id == expected.id && scenario.group == expected.group && scenario.input == expected.input &&
           parameters == expected.parameters && scenario.corpusId == expected.corpusId &&
           scenario.seed == expected.seed && scenario.correctnessClass == expected.correctnessClass &&
           scenario.elementsProcessed == expected.elementsProcessed &&
           scenario.bytesProcessed == expected.bytesProcessed;
}
} // namespace an::bench
