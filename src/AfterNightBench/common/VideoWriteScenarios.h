// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "Scenario.h"

#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an::bench
{
/// The container-writing workloads. Each writes eight frames of the same eight-bit grey picture, published into a
/// fresh file, and the picture is mirrored top to bottom before timing, so a writer that stores frames bottom-up
/// writes the same bytes as one that stores them top-down.
///
/// - `sequence.avi_write.8x_u8` writes 2048x2048 frames into an uncompressed AVI. Its output is digested over the
///   stored frames, in file order, rather than over the headers and indexes around them.
/// - `sequence.video_write.vp9_8x_u8` encodes 1024x1024 frames as VP9 in WebM at the standard quality with four
///   encoder threads, and with three on Linux, which leaves a core of a four-core host to the appending operation.
///   libvpx-vp9 writes the same stream at every thread count, so its output is digested over the coded packets.
/// - `sequence.video_write.h264_8x_u8` encodes the same frames as H.264 in MP4 at the standard quality on one
///   encoder thread, through the software encoder.
inline std::vector<ScenarioSpec> videoWriteScenarios()
{
    std::vector<ScenarioSpec> result;
    auto base = [](const char* id, std::uint32_t side) {
        ScenarioSpec scenario;
        scenario.id = id;
        scenario.group = "sequence";
        scenario.input.width = scenario.input.height = side;
        scenario.input.channels = 1u;
        scenario.input.sampleType = SampleType::UInt8;
        scenario.input.layout = Layout::Planar;
        scenario.input.byteOrder = ByteOrder::Little;
        scenario.corpusId = "synthetic.stars";
        scenario.seed = 20260822u;
        scenario.parameters["mode"] = "write";
        scenario.parameters["frames"] = "8";
        scenario.parameters["rows"] = "mirrored";
        scenario.correctnessClass = CorrectnessClass::BitExact;
        scenario.elementsProcessed = 8u * std::uint64_t{side} * side;
        scenario.bytesProcessed = scenario.elementsProcessed;
        scenario.allowedBackends = {BackendId::Unspecified};
        return scenario;
    };
    ScenarioSpec avi = base("sequence.avi_write.8x_u8", 2048u);
    avi.parameters["format"] = "AVI";
    avi.parameters["container"] = "avi";
    result.push_back(std::move(avi));
    ScenarioSpec vp9 = base("sequence.video_write.vp9_8x_u8", 1024u);
    vp9.parameters["format"] = "VP9";
    vp9.parameters["container"] = "webm";
    vp9.parameters["quality"] = "standard";
#if defined(__linux__)
    vp9.parameters["encoderThreads"] = "3";
#else
    vp9.parameters["encoderThreads"] = "4";
#endif
    result.push_back(std::move(vp9));
    ScenarioSpec h264 = base("sequence.video_write.h264_8x_u8", 1024u);
    h264.parameters["format"] = "H264";
    h264.parameters["container"] = "mp4";
    h264.parameters["quality"] = "standard";
    h264.parameters["encoderThreads"] = "1";
    result.push_back(std::move(h264));
    return result;
}

inline bool matchesVideoWriteScenario(const ScenarioSpec& scenario)
{
    auto parameters = scenario.parameters;
    parameters.erase("timedBatch");
    for (const auto& expected : videoWriteScenarios())
        if (scenario.id == expected.id)
            return scenario.group == expected.group && scenario.input == expected.input &&
                   parameters == expected.parameters && scenario.corpusId == expected.corpusId &&
                   scenario.seed == expected.seed && scenario.correctnessClass == expected.correctnessClass &&
                   scenario.elementsProcessed == expected.elementsProcessed &&
                   scenario.bytesProcessed == expected.bytesProcessed;
    return false;
}

inline bool isVideoWriteScenario(std::string_view id)
{
    return id.rfind("sequence.avi_write.", 0u) == 0u || id.rfind("sequence.video_write.", 0u) == 0u;
}

/// Makes a picture its own vertical reflection: every row of the lower half becomes the row of the upper half it
/// mirrors.
inline void mirrorRows(std::span<std::byte> samples, std::size_t rowBytes, std::size_t rows)
{
    for (std::size_t row = 0u; row < rows / 2u; ++row)
        std::memcpy(samples.data() + (rows - 1u - row) * rowBytes, samples.data() + row * rowBytes, rowBytes);
}
} // namespace an::bench
