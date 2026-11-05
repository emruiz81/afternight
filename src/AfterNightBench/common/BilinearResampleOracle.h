// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared bilinear-resize scenario.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// One destination position independently evaluated by the resize oracle.
struct BilinearResampleProbe
{
    std::uint32_t x = 0u; ///< Destination column.
    std::uint32_t y = 0u; ///< Destination row.
    double value = 0.0; ///< Bilinear value accumulated in `long double` and narrowed once.
};

/// Maps a destination pixel center to its source pixel-center coordinate.
///
/// This is the half-pixel resize convention of the engine's coordinate map. Keeping it engine-neutral
/// prevents the adapter from choosing a subtly different transform.
///
/// @param destinationIndex the zero-based destination center.
/// @param sourceSize the positive source axis length.
/// @param destinationSize the positive destination axis length.
/// @return the corresponding source coordinate, where integers name source pixel centers.
[[nodiscard]] double bilinearResizeCoordinate(std::uint32_t destinationIndex, std::uint32_t sourceSize,
                                              std::uint32_t destinationSize) noexcept;

/// Evaluates a clamp-border bilinear resize at deterministic row-major probes.
///
/// @param samples the tight source plane.
/// @param sourceWidth source width.
/// @param sourceHeight source height.
/// @param destinationWidth destination width.
/// @param destinationHeight destination height.
/// @param probeStride row-major distance between probes; must be nonzero.
/// @return independent probes, or empty when an argument is inconsistent.
[[nodiscard]] std::vector<BilinearResampleProbe>
computeBilinearResizeOracle(std::span<const float> samples, std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                            std::uint32_t destinationWidth, std::uint32_t destinationHeight, std::uint32_t probeStride);

/// Largest absolute-or-relative error of a produced tight plane at the oracle probes.
///
/// @param produced the destination samples.
/// @param destinationWidth destination row width.
/// @param probes the independent expected values.
/// @return the normalized maximum, or the largest finite double for malformed/non-finite input.
[[nodiscard]] double bilinearResizeError(std::span<const float> produced, std::uint32_t destinationWidth,
                                         const std::vector<BilinearResampleProbe>& probes) noexcept;

/// Largest sampled excursion outside the finite normalized range.
///
/// This is the mode-neutral guard used by interpolation performance rows whose full numeric
/// contract is already covered by the geometry differential tests.
[[nodiscard]] double unitRangeResizeError(std::span<const float> produced, std::uint32_t probeStride) noexcept;

} // namespace an::bench
