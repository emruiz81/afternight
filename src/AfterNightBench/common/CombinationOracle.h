// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definitions of the image combination and external-tool hand-off rows: their derived inputs and
// the outputs a run is held to.
#pragma once

#include "Results.h"
#include "Scenario.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// True for the four combination and hand-off rows and the edge tightening row.
[[nodiscard]] bool requiresCombinationOracle(const ScenarioSpec& scenario);

/// The LRGB row's settings: the corpus's second LRGB setting.
inline constexpr double kLrgbLightnessWeight = 0.6;
inline constexpr double kLrgbLightnessMidtones = 0.3;
inline constexpr double kLrgbSaturationMidtones = 0.4;
inline constexpr std::array<double, 3u> kLrgbChannelWeights{0.9, 1.0, 0.8};

/// The recombination row's edge tightening.
inline constexpr double kRecombinationTightening = 0.5;

/// The edge tightening row's amount: the recombination row's.
inline constexpr double kEdgeTightenAmount = kRecombinationTightening;

/// The product's twenty-seven blend modes, by their dialog's labels, in its menu's order.
inline constexpr std::array<const char*, 27u> kBlendModeLabels{
    "Replace",       "Darken/Min",  "Multiply",   "Color Burn",  "Linear Burn",
    "Darker Color",  "Lighten/Max", "Screen",     "Color Dodge", "Linear Dodge/Add",
    "Lighter Color", "Overlay",     "Soft Light", "Hard Light",  "Vivid Light",
    "Linear Light",  "Pin Light",   "Difference", "Exclusion",   "Subtract",
    "Divide",        "Power",       "Arctan",     "Hue",         "Saturation",
    "Color",         "Lightness"};

/// The LRGB row's luminance frame, derived from its colour frame: `min(1, 0.3 R + 0.6 G + 0.1 B + 0.02)` per pixel,
/// computed in double and rounded once to Float32. The colour frame is planar and tightly packed.
[[nodiscard]] std::vector<float> lrgbLuminanceFrame(std::span<const float> color, std::uint32_t width,
                                                    std::uint32_t height);

/// The blend row's second frame, derived from its first: each channel is one less the next channel of the first,
/// mirrored left to right, computed in double and rounded once to Float32. Planar and tightly packed.
[[nodiscard]] std::vector<float> blendSecondFrame(std::span<const float> first, std::uint32_t width,
                                                  std::uint32_t height);

/// The recombination row's starless background: a gentle slope per channel, `0.04 + 0.03 x / width + 0.02 y / height
/// + 0.01 c`, rounded to Float32. Planar and tightly packed, three channels.
[[nodiscard]] std::vector<float> recombinationStarless(std::uint32_t width, std::uint32_t height);

/// The recombination row's mask: four times the stars' first channel, held at one, rounded to Float32. One plane.
[[nodiscard]] std::vector<float> recombinationMask(std::span<const float> stars, std::uint32_t width,
                                                   std::uint32_t height);

/// Holds the LRGB row's output to its definition: the colour decomposed into packed CIE L*C*h* through the sRGB
/// transfer and primaries and the D65 white, the luminance and the chroma stretched by their midtones, the lightness
/// blended by its weight, the colour recomposed, held to the unit interval, weighed and held again, within 1.0e-4.
/// Sets the correctness metric to the largest difference.
void checkLrgbOutput(std::span<const float> color, std::span<const float> luminance, std::uint32_t width,
                     std::uint32_t height, std::span<const float> output, ResultRecord& result);

/// A running check of the blend row's twenty-seven outputs.
struct BlendModesCheck
{
    double largest = 0.0;
    double samples = 0.0;
    double differing = 0.0;
};

/// Holds one blend mode's output, the mode named by its index in `kBlendModeLabels`, to its definition: the
/// separable modes their formulas, the four colour-selecting and colour-exchanging modes through CIE L*a*b* and HSI as
/// the blend modes define them. Both frames are planar and tightly packed.
void checkBlendModeOutput(std::size_t mode, std::span<const float> target, std::span<const float> blend,
                          std::uint32_t width, std::uint32_t height, std::span<const float> output,
                          BlendModesCheck& check);

/// Sets the blend row's correctness metric to the largest difference over all twenty-seven modes; the tolerance is
/// the CIE modes', 1.0e-4.
void finishBlendModesCheck(const BlendModesCheck& check, ResultRecord& result);

/// Holds the recombination row's output to its definition: the stars less one half of a twentieth of the sum of
/// their four Sobel magnitudes, held to the unit interval, added to the starless background and held at one, and
/// blended over it by the mask. The first and last columns are left out, because an edge finder may read across a
/// row's end there or read a zero border; everything else is the same Float32 arithmetic.
/// Sets the correctness metric to the largest difference elsewhere.
void checkRecombinationOutput(std::span<const float> stars, std::span<const float> starless,
                              std::span<const float> mask, std::uint32_t width, std::uint32_t height,
                              std::span<const float> output, ResultRecord& result);

/// Holds the edge tightening row's output to its definition: each sample less one half of a twentieth of the sum of
/// its four Sobel magnitudes, a row past the frame's edge read as zero, held to the unit interval. The first and last
/// columns are left out, as the recombination check leaves them out. Sets the correctness metric to the largest
/// difference elsewhere.
void checkEdgeTightenOutput(std::span<const float> stars, std::uint32_t width, std::uint32_t height,
                            std::span<const float> output, ResultRecord& result);

/// Holds the hand-off row's restored image to its definition: the frame mapped into the unit interval, each channel
/// stretched by the automatic transfer its exact median and deviation give, rounded to sixteen-bit codes, taken back
/// through the transfer's inverse and mapped back, within 2.0e-3. Sets the correctness metric to the largest
/// difference.
void checkHandoffOutput(std::span<const float> source, std::uint32_t width, std::uint32_t height,
                        std::span<const float> restored, ResultRecord& result);

} // namespace an::bench
