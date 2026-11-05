// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "Scenario.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{
/// Every streaming row: eight FITS frames through a read or a write, inline or queued.
///
/// The four original rows wait 5 ms per frame in their processing stage, a stand-in for work, so
/// most of what they time is that wait. The reads and writes with no delay time the same frames and
/// the same queues with nothing between them, which is the throughput of the reads and writes and
/// the cost of handing a frame over, apart from the waits.
inline std::vector<ScenarioSpec> streamingScenarios()
{
    std::vector<ScenarioSpec> result;
    auto add = [&result](const char* operation, const char* mode, const char* variant, const char* delay) {
        ScenarioSpec spec;
        spec.id = std::string("streaming.") + operation + "." + mode + variant + ".8x_f32";
        spec.group = "streaming";
        spec.input.width = spec.input.height = 1024u;
        spec.input.channels = 1u;
        spec.input.sampleType = SampleType::Float32;
        spec.input.layout = Layout::Planar;
        spec.input.byteOrder = ByteOrder::Little;
        spec.corpusId = "synthetic.stars";
        spec.seed = 20260822u;
        spec.parameters = {{"operation", operation},
                           {"mode", mode},
                           {"frames", "8"},
                           {"format", "FITS"},
                           {"processingDelayMilliseconds", delay},
                           {"residentQueueFrames", "2"}};
        spec.correctnessClass = CorrectnessClass::BitExact;
        spec.elementsProcessed = 8u * 1024u * 1024u;
        spec.bytesProcessed = spec.elementsProcessed * 4u;
        spec.allowedBackends = {BackendId::Unspecified};
        result.push_back(std::move(spec));
    };
    for (const auto* operation : {"read", "write"})
        for (const auto* mode : {"inline", "queued"})
            add(operation, mode, "", "5");
    for (const auto* operation : {"read", "write"})
        for (const auto* mode : {"inline", "queued"})
            add(operation, mode, ".nodelay", "0");
    return result;
}

/// The processing delay a streaming row declares, per frame.
///
/// @param spec a row `matchesStreamingScenario` accepted.
/// @return the wait its processing stage makes for every frame; zero makes none.
inline std::chrono::milliseconds streamingProcessingDelay(const ScenarioSpec& spec)
{
    return std::chrono::milliseconds(std::stoi(spec.parameters.at("processingDelayMilliseconds")));
}
/// The frames a queued streaming row keeps resident in its queue, read ahead of the processing stage or
/// written behind it, the frame being read or written included.
///
/// @param spec a row `matchesStreamingScenario` accepted.
/// @return the count the runner holds, stated in the engine's terms.
inline std::uint32_t streamingResidentQueueFrames(const ScenarioSpec& spec)
{
    return static_cast<std::uint32_t>(std::stoul(spec.parameters.at("residentQueueFrames")));
}
inline bool matchesStreamingScenario(const ScenarioSpec& spec)
{
    auto parameters = spec.parameters;
    parameters.erase("timedBatch");
    for (const auto& expected : streamingScenarios())
        if (spec.id == expected.id)
            return spec.group == expected.group && spec.input == expected.input && parameters == expected.parameters &&
                   spec.corpusId == expected.corpusId && spec.seed == expected.seed &&
                   spec.correctnessClass == expected.correctnessClass &&
                   spec.elementsProcessed == expected.elementsProcessed &&
                   spec.bytesProcessed == expected.bytesProcessed;
    return false;
}
} // namespace an::bench
