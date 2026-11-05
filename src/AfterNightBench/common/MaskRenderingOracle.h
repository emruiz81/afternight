// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definitions of the viewer's tinted mask overlay and of a brush stroke blended into a paint plane.
#pragma once

#include "Results.h"
#include "Scenario.h"

#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// True for the tinted mask overlay row.
[[nodiscard]] bool requiresMaskOverlayOracle(const ScenarioSpec& scenario);

/// Holds a tinted overlay of an eight-bit mask to its definition: every pixel four bytes, the tint's red, green and
/// blue and an alpha of 255 minus the mask's code. Sets the correctness metric to the largest byte difference from it,
/// and counts the pixels that differ at all.
void checkMaskOverlayOutput(std::span<const std::uint8_t> mask, std::span<const std::uint8_t> output, std::uint8_t red,
                            std::uint8_t green, std::uint8_t blue, ResultRecord& result);

/// The stroke row's brush: a round tip 48 pixels wide at hardness one half, flow one half, a tenth of the diameter
/// between dabs, blended normally at opacity 0.8 with the pressure one.
inline constexpr float kDabBlendDiameter = 48.0f;
inline constexpr float kDabBlendHardness = 0.5f;
inline constexpr float kDabBlendSpacing = 0.1f;
inline constexpr float kDabBlendFlow = 0.5f;
inline constexpr float kDabBlendOpacity = 0.8f;

/// One point of the stroke row's stroke.
struct DabBlendPoint
{
    double x = 0.0;
    double y = 0.0;
};

/// The stroke row's 24 points: a zigzag across the plane between a fifth and four fifths of its height, each
/// coordinate a multiple of a quarter pixel so the runner and the definition hold it exactly.
[[nodiscard]] std::vector<DabBlendPoint> dabBlendStrokePoints(std::uint32_t width, std::uint32_t height);

/// True for the brush stroke row.
[[nodiscard]] bool requiresDabBlendOracle(const ScenarioSpec& scenario);

/// The stroke row's paint plane after the stroke, by definition: the tip's samples at their offsets from its centre,
/// each segment's dabs at equal steps of its Catmull-Rom curve rounded to pixels, every dab screened into a cleared
/// stroke plane, and every pixel the stroke covers blended normally from its value before the stroke, all in Float32
/// in the stated order; every other pixel keeps its value.
[[nodiscard]] std::vector<float> dabBlendExpected(std::span<const float> paint, std::uint32_t width,
                                                  std::uint32_t height);

/// Holds a stroke row's paint plane to the definition. Sets the correctness metric to the largest absolute difference
/// from it, and counts the samples that differ at all.
void checkDabBlendOutput(std::span<const float> paint, std::uint32_t width, std::uint32_t height,
                         std::span<const float> output, ResultRecord& result);

} // namespace an::bench
