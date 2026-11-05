// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the display presentation rows.

#pragma once

#include "Scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The seed of the presentation rows' synthetic star fields.
inline constexpr std::uint64_t kPresentationSeed = 20261001u;

/// The automatic stretch of a colour frame to packed display pixels, as a viewer paints it.
inline constexpr const char* kPresentationAutostretchRgba = "presentation.autostretch_rgba.f32";

/// Every n-th sample of every n-th row of a sixteen-bit frame to bytes, as a viewer paints it below full zoom.
inline constexpr const char* kPresentationSubsampled8bit = "presentation.subsampled_8bit.u16";

/// The subsample factor of the subsampled row: a quarter zoom.
inline constexpr std::uint32_t kPresentationSubsampleFactor = 4u;

/// The display presentation rows, timed over the complete composition each viewer path runs per paint.
///
/// The automatic row measures the frame's per-channel median and median deviation, derives an unlinked
/// midtones transfer from them with the display defaults, applies it and packs the result as four bytes a
/// pixel; the clock holds all four, because the viewer does all four whenever the image changes. The
/// subsampled row presents every fourth sample of every fourth row of a sixteen-bit frame linearly, the
/// viewer's fast path below full zoom. Both are held to the definition in `PresentationOracle.h`
/// within two codes. Each repeats its
/// operation in one timed region the number of times `prepare_hard_floor_scenarios.py` chose for this host's
/// 150 ms floor at a safety factor of two.
///
/// @return the two rows, in a fixed order.
inline std::vector<ScenarioSpec> presentationScenarios()
{
    std::vector<ScenarioSpec> rows;

    ScenarioSpec rgba;
    rgba.id = kPresentationAutostretchRgba;
    rgba.group = "presentation";
    rgba.operationVersion = 1;
    rgba.inputSource = InputSource::Synthetic;
    rgba.input.width = 2048u;
    rgba.input.height = 2048u;
    rgba.input.channels = 3u;
    rgba.input.sampleType = SampleType::Float32;
    rgba.input.layout = Layout::Planar;
    rgba.input.byteOrder = ByteOrder::Little;
    rgba.corpusId = "synthetic.stars";
    rgba.seed = kPresentationSeed;
    rgba.correctnessClass = CorrectnessClass::AbsoluteRelative;
    rgba.tolerance = 2.0;
    rgba.parameters["stretch"] = "automatic";
    rgba.parameters["linked"] = "false";
    rgba.parameters["output"] = "rgba8";
    rgba.parameters["subsample"] = "1";
    rgba.parameters[kTimedBatchParameter] = "17";
    rgba.workUnit = "pixels";
    rgba.elementsProcessed = std::uint64_t{rgba.input.width} * rgba.input.height;
    rgba.bytesProcessed = rgba.input.totalBytes() + rgba.elementsProcessed * 4u;
    rgba.allowedBackends = {BackendId::Unspecified};
    rows.push_back(rgba);

    ScenarioSpec subsampled;
    subsampled.id = kPresentationSubsampled8bit;
    subsampled.group = "presentation";
    subsampled.operationVersion = 1;
    subsampled.inputSource = InputSource::Synthetic;
    subsampled.input.width = 4096u;
    subsampled.input.height = 4096u;
    subsampled.input.channels = 1u;
    subsampled.input.sampleType = SampleType::UInt16;
    subsampled.input.layout = Layout::Planar;
    subsampled.input.byteOrder = ByteOrder::Little;
    subsampled.corpusId = "synthetic.stars";
    subsampled.seed = kPresentationSeed;
    subsampled.correctnessClass = CorrectnessClass::AbsoluteRelative;
    subsampled.tolerance = 2.0;
    subsampled.parameters["stretch"] = "linear";
    subsampled.parameters["output"] = "interleaved8";
    subsampled.parameters["subsample"] = std::to_string(kPresentationSubsampleFactor);
    subsampled.parameters[kTimedBatchParameter] = "1374";
    subsampled.workUnit = "pixels";
    const std::uint64_t presented = std::uint64_t{subsampled.input.width / kPresentationSubsampleFactor} *
                                    (subsampled.input.height / kPresentationSubsampleFactor);
    subsampled.elementsProcessed = presented;
    subsampled.bytesProcessed = presented * (2u + 1u);
    subsampled.allowedBackends = {BackendId::Unspecified};
    rows.push_back(subsampled);

    return rows;
}

} // namespace an::bench
