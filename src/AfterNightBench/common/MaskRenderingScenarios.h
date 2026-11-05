// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the mask overlay and brush stroke rows.

#pragma once

#include "Scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The seed of the rows' synthetic star fields.
inline constexpr std::uint64_t kMaskRenderingSeed = 20261003u;

/// The viewer's tinted overlay of an eight-bit mask, written as a layer of red, green, blue and alpha bytes.
inline constexpr const char* kMaskRenderTint = "masks.render_tint.u8";

/// A brush stroke over a paint plane: the tip, the dabs of every segment screened into a stroke plane, and the
/// stroke blended into the paint against the paint as it was.
inline constexpr const char* kDabBlend = "pointwise.dab_blend.f32";

/// The rows of the mask overlay and the brush stroke, timed over the complete composition each runs.
///
/// The overlay row tints a 4096 by 4096 eight-bit mask red: every pixel the tint, its alpha the mask's complement,
/// interleaved as the viewer paints it. The engine writes the bytes in one call, held to `MaskOverlayOracle.h`
/// exactly, because an eight-bit complement has no rounding to differ in.
///
/// The stroke row draws a 24-point zigzag across a 2048 by 2048 paint plane with a round brush 48 pixels wide at
/// hardness one half, flow one half and a tenth of the diameter between dabs, and blends it normally at opacity 0.8:
/// the stroke plane cleared, the tip built, every segment's dabs placed and screened in, and the stroke blended into
/// the paint once over the plane. The arithmetic is the drawing tools' own, restated: the oracle computes the same
/// Float32 expressions in the same order, so the row is held to `DabBlendOracle.h` bit for bit.
///
/// Each repeats its operation in one timed region the number of times `prepare_hard_floor_scenarios.py` chose on
/// `bench-windows` for its 150 ms floor at a safety factor of two, 143 and 86.
///
/// @return the two rows, in a fixed order.
inline std::vector<ScenarioSpec> maskRenderingScenarios()
{
    std::vector<ScenarioSpec> rows;

    ScenarioSpec tint;
    tint.id = kMaskRenderTint;
    tint.group = "masks";
    tint.operationVersion = 1;
    tint.inputSource = InputSource::Synthetic;
    tint.input.width = 4096u;
    tint.input.height = 4096u;
    tint.input.channels = 1u;
    tint.input.sampleType = SampleType::UInt8;
    tint.input.layout = Layout::Planar;
    tint.input.byteOrder = ByteOrder::Little;
    tint.corpusId = "synthetic.stars";
    tint.seed = kMaskRenderingSeed;
    tint.correctnessClass = CorrectnessClass::BitExact;
    tint.parameters["mode"] = "tint";
    tint.parameters["tint"] = "255,0,0";
    tint.parameters["invert"] = "false";
    tint.parameters[kTimedBatchParameter] = "143";
    tint.workUnit = "pixels";
    tint.elementsProcessed = std::uint64_t{tint.input.width} * tint.input.height;
    tint.bytesProcessed = tint.input.totalBytes() * 5u;
    tint.allowedBackends = {BackendId::Unspecified};
    rows.push_back(tint);

    ScenarioSpec stroke;
    stroke.id = kDabBlend;
    stroke.group = "pointwise";
    stroke.operationVersion = 1;
    stroke.inputSource = InputSource::Synthetic;
    stroke.input.width = 2048u;
    stroke.input.height = 2048u;
    stroke.input.channels = 1u;
    stroke.input.sampleType = SampleType::Float32;
    stroke.input.layout = Layout::Planar;
    stroke.input.byteOrder = ByteOrder::Little;
    stroke.corpusId = "synthetic.stars";
    stroke.seed = kMaskRenderingSeed;
    stroke.correctnessClass = CorrectnessClass::BitExact;
    stroke.parameters["diameter"] = "48";
    stroke.parameters["hardness"] = "0.5";
    stroke.parameters["spacing"] = "0.1";
    stroke.parameters["flow"] = "0.5";
    stroke.parameters["mode"] = "normal";
    stroke.parameters["opacity"] = "0.8";
    stroke.parameters["points"] = "24";
    stroke.parameters[kTimedBatchParameter] = "86";
    stroke.workUnit = "pixels";
    stroke.elementsProcessed = std::uint64_t{stroke.input.width} * stroke.input.height;
    stroke.bytesProcessed = stroke.input.totalBytes() * 4u;
    stroke.allowedBackends = {BackendId::Unspecified};
    rows.push_back(stroke);

    return rows;
}

} // namespace an::bench
