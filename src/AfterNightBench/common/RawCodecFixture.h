// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "Scenario.h"
#include "Fixture.h"
#include "Digest.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace an::bench
{
/// A full-scale, zero-black Bayer DNG makes normalization an identity mapping.
inline ScenarioSpec rawCodecScenario()
{
    ScenarioSpec scenario;
    scenario.id = "codec.raw_decode.u16";
    scenario.group = "codec";
    scenario.input.width = scenario.input.height = 1024u;
    scenario.input.channels = 1u;
    scenario.input.sampleType = SampleType::UInt16;
    scenario.input.layout = Layout::Planar;
    scenario.input.byteOrder = ByteOrder::Little;
    scenario.corpusId = "synthetic.ramp";
    scenario.seed = 20260822u;
    scenario.parameters["format"] = "RAW";
    scenario.parameters["operation"] = "decode";
    scenario.parameters["maximumCode"] = "65534";
    scenario.correctnessClass = CorrectnessClass::BitExact;
    scenario.bytesProcessed = scenario.input.totalBytes();
    scenario.elementsProcessed = std::uint64_t{scenario.input.width} * scenario.input.height;
    scenario.allowedBackends = {BackendId::Unspecified};
    return scenario;
}

/// The paired RAW compatibility workloads: decode alone, recorded orientation, and custom mosaic balance.
inline std::vector<ScenarioSpec> rawCodecScenarios()
{
    ScenarioSpec decode = rawCodecScenario();
    ScenarioSpec orientation = decode;
    orientation.id = "codec.raw_orientation.u16";
    orientation.parameters["operation"] = "orientation";
    orientation.parameters["orientation"] = "rotate90-clockwise";
    ScenarioSpec balance = decode;
    balance.id = "codec.raw_mosaic_balance.u16";
    balance.parameters["operation"] = "mosaic-balance";
    balance.parameters["whiteBalance"] = "custom-2,1,1.5";
    balance.correctnessClass = CorrectnessClass::EstimatorTolerance;
    balance.tolerance = 1.0;
    return {std::move(decode), std::move(orientation), std::move(balance)};
}

/// Whether a row is one of the paired RAW compatibility workloads, allowing only `timedBatch` to vary.
inline bool matchesRawCodecScenario(const ScenarioSpec& scenario)
{
    auto parameters = scenario.parameters;
    parameters.erase("timedBatch");
    for (const auto& expected : rawCodecScenarios())
        if (scenario.id == expected.id)
            return scenario.group == expected.group && scenario.input == expected.input &&
                   parameters == expected.parameters && scenario.corpusId == expected.corpusId &&
                   scenario.seed == expected.seed && scenario.correctnessClass == expected.correctnessClass &&
                   scenario.tolerance == expected.tolerance &&
                   scenario.elementsProcessed == expected.elementsProcessed &&
                   scenario.bytesProcessed == expected.bytesProcessed;
    return false;
}

/// Whether an identifier selects one of the RAW compatibility workloads.
inline bool isRawCodecScenario(std::string_view id)
{
    return id == "codec.raw_decode.u16" || id == "codec.raw_orientation.u16" || id == "codec.raw_mosaic_balance.u16";
}

/// Holds every sample below full scale, so a decoder's full-scale clamp, tested on its own, is not
/// what the rows time. The sample ceiling is part of the scenario identity and the transformed
/// payload is rehashed.
inline void prepareRawCodecFixture(Fixture& fixture)
{
    for (std::size_t i = 0u; i + 1u < fixture.bytes.size(); i += 2u)
        if (fixture.bytes[i] == 255u && fixture.bytes[i + 1u] == 255u)
            fixture.bytes[i] = 254u;
    fixture.digest = Sha256::of(fixture.bytes.data(), fixture.bytes.size());
}

/// Writes a minimal uncompressed Bayer DNG outside the timed decode, independently of the engine.
inline void writeRawCodecFixture(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                                 std::span<const std::uint8_t> payload, std::uint16_t orientation = 1u)
{
    if (payload.size() != static_cast<std::uint64_t>(width) * height * 2u || payload.size() > 0xffffffffu)
        throw std::runtime_error("invalid raw codec fixture payload");
    struct Tag
    {
        std::uint16_t key;
        std::uint16_t type;
        std::uint32_t count;
        std::vector<std::uint8_t> bytes;
    };
    const auto words = [](std::initializer_list<std::uint32_t> values, unsigned size = 4u) {
        std::vector<std::uint8_t> bytes;
        for (auto value : values)
            for (unsigned i = 0u; i < size; ++i)
                bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8u)));
        return bytes;
    };
    std::vector<Tag> tags{{254, 4, 1, words({0})},
                          {256, 4, 1, words({width})},
                          {257, 4, 1, words({height})},
                          {258, 3, 1, words({16}, 2)},
                          {259, 3, 1, words({1}, 2)},
                          {262, 3, 1, words({32803}, 2)},
                          {271, 2, 5, {'T', 'e', 's', 't', 0}},
                          {272, 2, 7, {'C', 'a', 'm', 'e', 'r', 'a', 0}},
                          {273, 4, 1, words({0})},
                          {274, 3, 1, words({orientation}, 2)},
                          {277, 3, 1, words({1}, 2)},
                          {278, 4, 1, words({height})},
                          {279, 4, 1, words({static_cast<std::uint32_t>(payload.size())})},
                          {284, 3, 1, words({1}, 2)},
                          {33421, 3, 2, words({2, 2}, 2)},
                          {33422, 1, 4, {0, 1, 1, 2}},
                          {50706, 1, 4, {1, 4, 0, 0}},
                          {50707, 1, 4, {1, 1, 0, 0}},
                          {50708, 2, 12, {'T', 'e', 's', 't', ' ', 'C', 'a', 'm', 'e', 'r', 'a', 0}},
                          {50710, 1, 3, {0, 1, 2}},
                          {50711, 3, 1, words({1}, 2)},
                          {50714, 5, 1, words({0, 1})},
                          {50717, 4, 1, words({65535})},
                          {50721, 10, 9, words({1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1})},
                          {50728, 5, 3, words({1, 1, 1, 1, 1, 1})},
                          {50778, 3, 1, words({21}, 2)}};
    std::vector<std::uint8_t> bytes{'I', 'I', 42, 0, 8, 0, 0, 0};
    const auto append = [&](const auto& part) { bytes.insert(bytes.end(), part.begin(), part.end()); };
    append(words({static_cast<std::uint32_t>(tags.size())}, 2));
    const auto extraStart = 14u + static_cast<std::uint32_t>(tags.size()) * 12u;
    std::vector<std::uint8_t> extra;
    std::size_t stripOffset = 0u;
    for (const auto& tag : tags)
    {
        append(words({tag.key, tag.type}, 2));
        append(words({tag.count}));
        if (tag.key == 273)
            stripOffset = bytes.size();
        if (tag.bytes.size() <= 4u)
        {
            append(tag.bytes);
            bytes.resize(bytes.size() + 4u - tag.bytes.size(), 0);
        }
        else
        {
            append(words({extraStart + static_cast<unsigned>(extra.size())}));
            extra.insert(extra.end(), tag.bytes.begin(), tag.bytes.end());
            if (extra.size() % 2u)
                extra.push_back(0);
        }
    }
    append(words({0}));
    append(extra);
    const auto offset = words({static_cast<unsigned>(bytes.size())});
    std::copy(offset.begin(), offset.end(), bytes.begin() + stripOffset);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    output.close();
    if (!output)
        throw std::runtime_error("cannot write raw codec fixture");
}
} // namespace an::bench
