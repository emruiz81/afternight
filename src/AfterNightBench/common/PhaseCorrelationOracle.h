// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared phase-correlation scenario.
//
// A translation measurement has one input a harness can state the answer for exactly: a frame
// against a cyclically shifted copy of itself. The wrap is what makes the displacement the whole
// truth - no content leaves, none is invented - so the expected translation is the negated shift,
// and the residual the engine leaves against it is a property of its windowing and refinement
// rather than of the fixture. The runner derives the target from the fixture with the function
// here, so every run is handed byte-identical pairs.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// The horizontal cyclic shift the scenario applies to the target's content, in pixels.
inline constexpr std::int32_t kPhaseCorrelationShiftX = 7;

/// The vertical cyclic shift, in pixels.
inline constexpr std::int32_t kPhaseCorrelationShiftY = -4;

/// A copy of a frame with its content cyclically shifted.
///
/// The content moves by `(shiftX, shiftY)` - a positive shift moves it right and down - and what
/// leaves one edge re-enters at the other.
///
/// @param samples the frame to shift, `width * height` contiguous samples.
/// @param width the frame width.
/// @param height the frame height.
/// @param shiftX how far the content moves right.
/// @param shiftY how far the content moves down.
/// @return the shifted copy.
[[nodiscard]] std::vector<float> cyclicallyShiftedFrame(std::span<const float> samples, std::uint32_t width,
                                                        std::uint32_t height, std::int32_t shiftX, std::int32_t shiftY);

/// The distance of a measured translation from the shift's exact answer.
///
/// Content displaced by `(shiftX, shiftY)` is mapped back by the negated pair, so the residual is
/// the Euclidean distance from that. A non-finite measurement returns the largest finite double.
///
/// @param translationX the measured horizontal translation.
/// @param translationY the measured vertical translation.
/// @param shiftX the shift the target was derived with.
/// @param shiftY the shift the target was derived with.
/// @return the residual, in pixels.
[[nodiscard]] double phaseCorrelationResidual(double translationX, double translationY, std::int32_t shiftX,
                                              std::int32_t shiftY) noexcept;

/// A canonical representation of one measurement.
///
/// @param translationX the measured horizontal translation.
/// @param translationY the measured vertical translation.
/// @param response the measurement's reported quality.
/// @return the three values, one line each.
[[nodiscard]] std::string phaseCorrelationText(double translationX, double translationY, double response);

} // namespace an::bench
