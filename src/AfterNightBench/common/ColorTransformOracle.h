// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared colour-transform scenario.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// One pixel independently converted from encoded sRGB to the packed perceptual triple.
///
/// The packing is the one the engine writes for this scenario: lightness divided by a hundred
/// and the two opponent channels shifted by 128 and divided by 255, so every value lies in the unit
/// interval and can be compared as a normalized magnitude.
struct SrgbToLabProbe
{
    std::uint64_t index = 0u; ///< Row-major pixel index within one plane.
    double lightness = 0.0; ///< `L / 100`, evaluated in `long double` and narrowed once.
    double greenRed = 0.0; ///< `(a + 128) / 255`.
    double blueYellow = 0.0; ///< `(b + 128) / 255`.
};

/// Converts encoded sRGB pixels to packed CIE L*a*b* at deterministic strided probes.
///
/// Evaluated here rather than by the engine so that the engine is held to an independent answer, with
/// the matrix built from the primaries' chromaticities.
///
/// The published sRGB primaries matrix, the D65 white and the CIE definitions are used exactly as
/// the standards state them, and every stage is evaluated in `long double`.
///
/// A pixel any of whose channels lies outside `[0, 1]` is skipped rather than probed: an
/// out-of-range value may map to a not-a-number, and a not-a-number cannot be a tolerance.
///
/// @param red the tight red plane.
/// @param green the tight green plane, of the red plane's length.
/// @param blue the tight blue plane, of the red plane's length.
/// @param probeStride distance between probes; must be nonzero.
/// @return the independent probes, or empty when an argument is inconsistent.
[[nodiscard]] std::vector<SrgbToLabProbe> computeSrgbToLabOracle(std::span<const float> red,
                                                                 std::span<const float> green,
                                                                 std::span<const float> blue,
                                                                 std::uint32_t probeStride);

/// Largest absolute-or-relative error of three produced tight planes at the oracle probes.
///
/// @param lightness the produced lightness plane.
/// @param greenRed the produced first opponent plane.
/// @param blueYellow the produced second opponent plane.
/// @param probes the independent expected values.
/// @return the normalized maximum over all three channels, or the largest finite double for
///         malformed or non-finite input.
[[nodiscard]] double srgbToLabError(std::span<const float> lightness, std::span<const float> greenRed,
                                    std::span<const float> blueYellow,
                                    const std::vector<SrgbToLabProbe>& probes) noexcept;

} // namespace an::bench
