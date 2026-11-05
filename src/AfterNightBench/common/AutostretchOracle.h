// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The complete autostretch's definition and the evidence the runner records for it.
//
// The composition is three steps, and the engine is held to each one: its statistics to the exact
// median and median absolute deviation of every channel, within the estimator tier the statistics rows
// use; its transfer to the one the definition derives from the statistics it reported, so a step that
// measured well and derived badly cannot hide behind one that measured badly; and every output sample
// to that transfer applied to the input. How far the output lies from the definition taken from the
// exact statistics is recorded rather than judged.

#pragma once

#include "PresentationOracle.h"
#include "Results.h"
#include "Scenario.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// The largest difference an autostretch row's reported median or deviation may have from the exact one.
inline constexpr double kAutostretchStatisticsTolerance = 1e-4;

/// The largest difference a reported transfer's shadows, midtones or highlights may have from the one the
/// definition derives from the reported statistics.
inline constexpr double kAutostretchTransferTolerance = 1e-6;

/// What the engine reported for one autostretch row.
struct AutostretchReport
{
    /// Colour channels measured and stretched: one or three.
    std::uint32_t channels = 0u;

    /// Each channel's median.
    std::array<double, 3u> medians{};

    /// Each channel's median absolute deviation, not scaled to a sigma.
    std::array<double, 3u> deviations{};

    /// The transfer each channel took; the statistics rows report none.
    std::array<PresentationTransfer, 3u> transfers{};
};

/// A colour frame made from a little-endian sixteen-bit RGGB capture.
///
/// Each two-by-two block becomes one pixel: its red code over 65535, the sum of its two green codes over
/// 131070 and its blue code over 65535, each one division in single precision. The planes are written one
/// after another, red first. An odd last row or column is dropped.
///
/// @param codes the capture's bytes.
/// @param width the capture's width.
/// @param height the capture's height.
/// @return the three planes, or nothing for bytes that are not one code a pixel.
[[nodiscard]] std::vector<float> makeBinnedColourCapture(std::span<const std::uint8_t> codes, std::uint32_t width,
                                                         std::uint32_t height);

/// The linked automatic stretch, written from its definition: one transfer for every channel, whose shadow
/// point is the mean over channels of each median plus `clipping` normalized deviations, held at zero or
/// above, and whose balance maps the mean median's height above it to `target` - or, when every channel's
/// median is above one half, the mirror of that about the highlights.
///
/// @param locations each channel's median and deviation.
/// @param clipping the shadows clipping.
/// @param target the target background.
/// @return the transfer; the identity for no channels.
[[nodiscard]] PresentationTransfer linkedPresentationTransfer(std::span<const PresentationLocation> locations,
                                                              double clipping, double target);

/// The exact median and deviation of each channel of planar samples, as the composition defines them.
///
/// @param samples the planes, one after another.
/// @param channels how many planes.
/// @return each channel's location.
[[nodiscard]] std::vector<PresentationLocation> exactAutostretchLocations(std::span<const float> samples,
                                                                          std::uint32_t channels);

/// The exact median and deviation of a little-endian sixteen-bit capture carried into the unit interval,
/// found by counting codes rather than by sorting, as the statistics rows find them.
///
/// @param codes the capture's bytes.
/// @return the one channel's location, or nothing for bytes that are not whole codes.
[[nodiscard]] std::vector<PresentationLocation> exactCapturedLocations(std::span<const std::uint8_t> codes);

/// A canonical text of reported statistics, for a statistics row's digest.
///
/// @param report what the engine reported.
/// @return one line a channel: its median and deviation to seventeen digits.
[[nodiscard]] std::string autostretchStatisticsText(const AutostretchReport& report);

/// Records the evidence for one autostretch row and judges what it can.
///
/// Writes the reported statistics and transfers, the largest statistics, transfer and output errors, and
/// the largest difference from the definition taken from `exact`. Sets the correctness metric to the
/// largest output error for a row that writes an image and to the largest statistics error for one that
/// measures only.
///
/// @param scenario the row.
/// @param input the samples the composition read, planar.
/// @param output the samples it wrote, planar; empty for a statistics row.
/// @param reported what the engine reported.
/// @param exact each channel's exact location.
/// @param result the record the evidence is written to.
void checkAutostretchOutput(const ScenarioSpec& scenario, std::span<const float> input, std::span<const float> output,
                            const AutostretchReport& reported, std::span<const PresentationLocation> exact,
                            ResultRecord& result);

/// True for the complete autostretch rows.
[[nodiscard]] bool requiresAutostretchOracle(const ScenarioSpec& scenario);

/// Whether a record meets an autostretch row's rules: its statistics within
/// `kAutostretchStatisticsTolerance`, its transfers within `kAutostretchTransferTolerance` of the ones its
/// statistics define, and its output within the row's tolerance of its transfer.
///
/// @param scenario the row.
/// @param record the result for it.
/// @return true when every rule is met and every piece of evidence is present.
[[nodiscard]] bool autostretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& record);

} // namespace an::bench
