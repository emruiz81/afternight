// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "Scenario.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace an::bench
{
// Stable container codes for comparison only: unknown, FITS, XISF, RAW, PNG, JPEG, TIFF.
struct CodecLookupCase
{
    std::string_view path;
    std::uint8_t expected;
};

inline constexpr std::array<CodecLookupCase, 32u> kCodecLookupCases{{{"C:/astro/2026/session/light.FITS", 1u},
                                                                     {"C:/astro/2026/session/light.fit", 1u},
                                                                     {"C:/astro/2026/session/light.FtS", 1u},
                                                                     {"/astro/session/master.XISF", 2u},
                                                                     {"/astro/session/light.nef", 3u},
                                                                     {"/astro/session/light.CRW", 3u},
                                                                     {"/astro/session/light.cr2", 3u},
                                                                     {"/astro/session/light.CR3", 3u},
                                                                     {"/astro/session/light.dng", 3u},
                                                                     {"/astro/session/light.ARW", 3u},
                                                                     {"/astro/session/light.sr2", 3u},
                                                                     {"/astro/session/light.SRF", 3u},
                                                                     {"/astro/session/light.dcr", 3u},
                                                                     {"/astro/session/light.K25", 3u},
                                                                     {"/astro/session/light.kdc", 3u},
                                                                     {"/astro/session/light.ERF", 3u},
                                                                     {"/astro/session/light.mrw", 3u},
                                                                     {"/astro/session/light.ORF", 3u},
                                                                     {"/astro/session/light.pef", 3u},
                                                                     {"/astro/session/light.RAF", 3u},
                                                                     {"/astro/session/light.raw", 3u},
                                                                     {"/astro/session/light.X3F", 3u},
                                                                     {"/astro/\xc3\xa1/\xe6\x98\x9f.PnG", 4u},
                                                                     {"/astro/session/master.tiff", 6u},
                                                                     {"/astro/session/master.TIF", 6u},
                                                                     {"C:\\astro\\session\\preview.jpg", 5u},
                                                                     {"/astro/session/preview.JPEG", 5u},
                                                                     {"/astro/session/preview.jpe", 5u},
                                                                     {"/astro/session/unknown.bin", 0u},
                                                                     {"/astro/session/unnamed", 0u},
                                                                     {"", 0u},
                                                                     {"/astro/session/trailing.", 0u}}};

inline std::string codecLookupFixtureText()
{
    std::string text;
    for (const auto& item : kCodecLookupCases)
    {
        text += std::to_string(item.path.size()) + ':';
        text += item.path;
        text += ':' + std::to_string(item.expected) + '\n';
    }
    return text;
}

inline ScenarioSpec codecLookupScenario()
{
    ScenarioSpec result;
    result.id = "io.codec_format.path_lookup";
    result.group = "io";
    result.corpusId = "synthetic.uniform_noise";
    result.seed = 7u;
    result.input.width = result.input.height = result.input.channels = 1u;
    result.correctnessClass = CorrectnessClass::ExactAfterMapping;
    result.tolerance = 0.0;
    result.parameters = {
        {"paths", codecLookupFixtureText()}, {"measurement", "lookup-all-paths"}, {kTimedBatchParameter, "4096"}};
    result.elementsProcessed = kCodecLookupCases.size();
    for (const auto& item : kCodecLookupCases)
        result.bytesProcessed += item.path.size();
    result.workUnit = "paths";
    result.allowedBackends = {BackendId::Unspecified};
    return result;
}

inline bool matchesCodecLookupScenario(const ScenarioSpec& scenario)
{
    auto expected = codecLookupScenario();
    auto parameters = scenario.parameters;
    parameters.erase(kTimedBatchParameter);
    expected.parameters.erase(kTimedBatchParameter);
    return scenario.id == expected.id && scenario.input == expected.input && parameters == expected.parameters &&
           scenario.inputSource == InputSource::Synthetic && scenario.corpusId == expected.corpusId &&
           scenario.seed == expected.seed;
}
} // namespace an::bench
