// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared median-filter scenarios.
//
// The median of a square window is not an approximation of anything: it is the sample an ascending
// sort places in the middle, and any two implementations that agree about the window agree about
// the answer exactly. So the oracle here is that definition, evaluated in the harness rather than
// by the engine.
//
// **The border is part of the comparison.** A border pixel is ranked over the part of its window
// that falls inside the image. The engine names that convention
// `KernelBorderMode::NotApplicable`, so the engine and the oracle rank the same truncated window at every
// position instead of restricting the oracle to the interior and hiding a compatibility gap.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// One position the oracle ranked, and the median it found there.
struct MedianProbe
{
    /// The probe column.
    std::uint32_t x = 0u;

    /// The probe row.
    std::uint32_t y = 0u;

    /// The middle sample of the sorted window around it.
    double value = 0.0;
};

/// Ranks the truncated window around a deterministic subset of image positions.
///
/// The nominal window is `2 * radius + 1` square. Coordinates outside the image are omitted, so a
/// border window is smaller and an even population resolves to the mean of its two middle samples.
/// Probes are taken at a fixed stride through the complete row-major image, which includes the top
/// left corner and keeps every position stable between runs.
///
/// @param samples the input plane, `width * height` contiguous samples.
/// @param width the plane width.
/// @param height the plane height.
/// @param radius half the square window.
/// @param stride how many image positions separate one probe from the next.
/// @return the probes, in ascending row-major order. Empty when the picture has no samples or the
///         stride is zero.
[[nodiscard]] std::vector<MedianProbe> computeMedianFilterOracle(std::span<const float> samples, std::uint32_t width,
                                                                 std::uint32_t height, std::uint32_t radius,
                                                                 std::uint32_t stride);

/// Ranks the cells a structuring element marks around a deterministic subset of image positions.
///
/// The element is `2 * radius + 1` square, row-major, a non-zero cell marked; the window is the
/// marked cells that fall inside the image, ranked as `computeMedianFilterOracle` ranks a whole one.
/// A whole element is that oracle's window exactly.
///
/// @param samples the input plane, `width * height` contiguous samples.
/// @param width the plane width.
/// @param height the plane height.
/// @param radius half the element's side.
/// @param element the element's cells; its size must be the side squared.
/// @param stride how many image positions separate one probe from the next.
/// @return the probes, in ascending row-major order. Empty when the picture has no samples, the
///         stride is zero or the element is not the side squared.
[[nodiscard]] std::vector<MedianProbe> computeFootprintMedianOracle(std::span<const float> samples, std::uint32_t width,
                                                                    std::uint32_t height, std::uint32_t radius,
                                                                    std::span<const std::uint8_t> element,
                                                                    std::uint32_t stride);

/// The largest normalized deviation of a produced plane from the oracle at its probes.
///
/// Values whose magnitude is below one use absolute error; larger values use relative error. An
/// empty probe set or a non-finite sample returns the largest finite double, so a result stays
/// schema-valid and fails its declared tolerance.
///
/// @param produced the plane the runner wrote.
/// @param width the plane width.
/// @param probes what the oracle found.
/// @return the largest normalized deviation.
[[nodiscard]] double medianFilterError(std::span<const float> produced, std::uint32_t width,
                                       const std::vector<MedianProbe>& probes) noexcept;

/// A canonical representation of what a runner produced at the probe positions.
///
/// @param produced the plane the runner wrote.
/// @param width the plane width.
/// @param probes the positions to report.
/// @return one line per probe, in ascending row-major order.
[[nodiscard]] std::string medianFilterText(std::span<const float> produced, std::uint32_t width,
                                           const std::vector<MedianProbe>& probes);

} // namespace an::bench
