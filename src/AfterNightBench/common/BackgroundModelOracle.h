// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracles for the background-model and
// dark-structure scenarios.
//
// Both families are measured against something the harness put into the frame rather than against
// another implementation, and in both cases that means the harness has to build the input.
//
// A background removal is handed a frame the harness added a stated gradient to, and is scored by
// how much of that gradient's large-scale variation survives. The measurement is a spread of tile
// medians rather than a whole-frame deviation, because the stars a background model has to ignore
// carry more deviation than the gradient does and would otherwise dominate the number: a correct
// removal would look like a failure. A median per tile is blind to them, and a spread rather than a
// mean is blind to the level the engine chose to put back.
//
// A dark-structure enhancement is handed a frame the harness cut a soft-edged well into, and is
// scored by how much more prominent that well became. The engine is measured on the harness's
// own disc, so the engine's own mask never decides what it is compared against.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// How many tiles a side the background metric measures its spread over.
///
/// Four, which is sixteen medians: enough that a gradient across the frame shows as a spread and
/// few enough that each tile still holds tens of thousands of samples for its median to be stable.
inline constexpr std::uint32_t kBackgroundMetricTiles = 4u;

/// The constant term of the gradient the background rows add to their fixture.
inline constexpr double kBackgroundGradientOffset = 0.08;

/// The coefficient of the normalised column in that gradient.
inline constexpr double kBackgroundGradientSlopeX = 0.06;

/// The coefficient of the normalised row.
inline constexpr double kBackgroundGradientSlopeY = -0.04;

/// The coefficient of the normalised column squared, which is what makes the surface curved enough
/// that a first-degree model cannot remove all of it.
inline constexpr double kBackgroundGradientCurvature = 0.03;

/// The sky level the dark-structure rows raise their fixture to before cutting the well.
///
/// The star fixture's own background sits near a fiftieth of full scale, which is far too low to cut
/// a structure into: the well would go negative and every clamp in the engine would decide the
/// row. Raising the sky first is what makes the well a structure rather than a floor.
inline constexpr double kDarkStructureSky = 0.45;

/// The depth of the well the dark-structure rows cut into their fixture.
inline constexpr double kDarkStructureDepth = 0.22;

/// The radius of that well, as a fraction of the frame's shorter side.
inline constexpr double kDarkStructureRadiusFraction = 0.18;

/// A frame with the harness's own illumination gradient added to it.
///
/// The gradient is a low-order polynomial in coordinates normalised across the frame, so the same
/// numbers describe the same surface whatever the frame's size.
///
/// @param samples the fixture, `width * height` contiguous samples.
/// @param width the frame width.
/// @param height the frame height.
/// @return the frame with the gradient added.
[[nodiscard]] std::vector<float> backgroundGradientFrame(std::span<const float> samples, std::uint32_t width,
                                                         std::uint32_t height);

/// The spread of a frame's tile medians, which is what a gradient shows up as.
///
/// @param samples the frame.
/// @param width the frame width.
/// @param height the frame height.
/// @return the standard deviation of the tile medians, in the frame's own sample units.
[[nodiscard]] double backgroundTileSpread(std::span<const float> samples, std::uint32_t width, std::uint32_t height);

/// How much of a gradient's large-scale variation survived a background removal.
///
/// One means the operation returned its input and zero means the frame came back flat, so a row
/// that fails to remove anything cannot pass by accident and the number is free of the gradient's
/// own amplitude.
///
/// @param corrected what the engine produced.
/// @param input what the engine was given.
/// @param width the frame width.
/// @param height the frame height.
/// @return the surviving fraction, or the largest finite double when it is not finite.
[[nodiscard]] double backgroundGradientRatio(std::span<const float> corrected, std::span<const float> input,
                                             std::uint32_t width, std::uint32_t height);

/// A canonical representation of one background removal.
///
/// The tile medians plus a fixed lattice of samples, so a digest changes when the picture does and
/// not only when the summary statistic does.
///
/// @param corrected what the engine produced.
/// @param width the frame width.
/// @param height the frame height.
/// @return the text to digest.
[[nodiscard]] std::string backgroundText(std::span<const float> corrected, std::uint32_t width, std::uint32_t height);

/// A frame with the harness's own dark structure cut into it.
///
/// The fixture is first raised to `kDarkStructureSky`, then a soft-edged well is cut at the frame's
/// centre, deep enough to be unmistakable and wide enough that a multiscale mask separates it from
/// the stars.
///
/// @param samples the fixture, `width * height` contiguous samples.
/// @param width the frame width.
/// @param height the frame height.
/// @return the frame with the well cut into it.
[[nodiscard]] std::vector<float> darkStructureFrame(std::span<const float> samples, std::uint32_t width,
                                                    std::uint32_t height);

/// How visible the harness's well is in a frame.
///
/// The median inside the well subtracted from the median of an annulus outside it, divided by that
/// annulus median: a Weber contrast rather than a difference. Medians, so stars in either region
/// change nothing; a ratio, because the operation being measured applies a tone curve that darkens
/// the whole frame, and every absolute difference in a darkened frame is smaller whatever happened
/// to the structure. Visibility is what the operation claims to change, and visibility is what a
/// relative contrast measures.
///
/// @param samples the frame.
/// @param width the frame width.
/// @param height the frame height.
/// @return the contrast, as a fraction of the surrounding level.
[[nodiscard]] double darkStructureContrast(std::span<const float> samples, std::uint32_t width, std::uint32_t height);

/// How much of the harness's well an enhancement left un-enhanced.
///
/// The contrast before divided by the contrast after, so that smaller is better and the row's
/// tolerance reads as a floor on how much the structure had to gain: one means the operation
/// returned its input, and a value above one means it made the structure *less* prominent. The
/// direction is the correctness classes' convention rather than this metric's preference - every
/// class compares a metric against a ceiling. Using a ratio makes the row insensitive to the
/// fixture's own amplitude.
///
/// @param enhanced what the engine produced.
/// @param input what the engine was given.
/// @param width the frame width.
/// @param height the frame height.
/// @return the surviving fraction, or the largest finite double when it is not finite.
[[nodiscard]] double darkStructureContrastRatio(std::span<const float> enhanced, std::span<const float> input,
                                                std::uint32_t width, std::uint32_t height);

/// A canonical representation of one dark-structure enhancement.
///
/// @param enhanced what the engine produced.
/// @param width the frame width.
/// @param height the frame height.
/// @return the text to digest.
[[nodiscard]] std::string darkStructureText(std::span<const float> enhanced, std::uint32_t width, std::uint32_t height);

} // namespace an::bench
