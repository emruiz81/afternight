// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the demosaic rows over a real colour-filter capture.

#pragma once

#include "Scenario.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The capture the demosaic capture rows read: a 180 s RGGB light from a one-shot-colour camera, uncalibrated.
inline constexpr const char* kDemosaicCaptureCorpusId = "ngc253.qhy183c.light";
/// Its width in pixels.
inline constexpr std::uint32_t kDemosaicCaptureWidth = 5544u;
/// Its height in pixels.
inline constexpr std::uint32_t kDemosaicCaptureHeight = 3684u;

/// The demosaic rows over the capture, derived from the synthetic rows.
///
/// The real-data audit measured these six methods on this frame; these rows make that measurement a
/// permanent one. Each keeps its source row's method, pattern, white level, backend constraint,
/// correctness class, tolerance and definition check, and reads the capture's
/// sixteen-bit codes carried into the unit interval before the clock, as the statistics rows do.
///
/// @param catalogue the rows declared so far, which must hold the six synthetic rows.
/// @return the six capture rows, in the order of their methods below; a source row that is missing
///     yields no row.
inline std::vector<ScenarioSpec> demosaicCaptureScenarios(const std::vector<ScenarioSpec>& catalogue)
{
    constexpr std::array<const char*, 6u> kMethods = {"fast", "rcd", "amaze", "lmmse", "vng4", "luminance_ahd"};
    std::vector<ScenarioSpec> rows;
    for (const char* method : kMethods)
    {
        const std::string sourceId = std::string("demosaic.") + method + ".f32";
        for (const ScenarioSpec& source : catalogue)
        {
            if (source.id != sourceId)
            {
                continue;
            }
            ScenarioSpec captured = source;
            captured.id = std::string("demosaic.") + method + ".ngc253.f32";
            captured.inputSource = InputSource::Corpus;
            captured.corpusId = kDemosaicCaptureCorpusId;
            captured.input.width = kDemosaicCaptureWidth;
            captured.input.height = kDemosaicCaptureHeight;
            captured.input.channels = 1u;
            captured.input.sampleType = SampleType::UInt16;
            captured.input.rowStrideBytes = 0u;
            captured.input.planeStrideBytes = 0u;
            captured.parameters["samples"] = "codes-over-65535";
            captured.elementsProcessed = std::uint64_t{kDemosaicCaptureWidth} * kDemosaicCaptureHeight;
            captured.bytesProcessed = captured.elementsProcessed * sizeof(float);
            rows.push_back(std::move(captured));
        }
    }
    return rows;
}

} // namespace an::bench
