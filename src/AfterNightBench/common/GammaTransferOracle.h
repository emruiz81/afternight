// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared gamma-transfer scenario.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// One sample independently evaluated by the gamma oracle.
struct GammaTransferProbe
{
    std::uint64_t index = 0u; ///< Row-major sample index.
    double value = 0.0; ///< The transfer value, evaluated in `long double` and narrowed once.
};

/// Evaluates a gamma transfer at deterministic strided probes.
///
/// The transfer is `x` raised to the reciprocal of the stated gamma, which is what gamma correction
/// means here. It is evaluated here rather than by the engine, which applies a compiled polynomial, so
/// that the engine is held to an independent answer.
///
/// A sample outside the transfer domain - negative, or not a number - is skipped rather than probed:
/// the engine answers a quiet not-a-number there, and a not-a-number cannot be a tolerance.
///
/// @param samples the tight plane.
/// @param gamma the stated gamma. Must be finite and nonzero.
/// @param probeStride distance between probes; must be nonzero.
/// @return the independent probes, or empty when an argument is inconsistent.
[[nodiscard]] std::vector<GammaTransferProbe> computeGammaTransferOracle(std::span<const float> samples, double gamma,
                                                                         std::uint32_t probeStride);

/// Evaluates a gamma transfer followed by a two-segment normalized linear curve.
///
/// The curve passes through `(0, 0)`, (`middleInput`, `middleOutput`) and `(1, 1)`. Its input is
/// clamped to `[0, 1]`, matching the benchmark's declared curve-domain policy. This remains in the
/// engine-neutral harness so the fused engine program is checked against code it does not call.
///
/// @param samples the tight source plane.
/// @param gamma positive finite gamma whose reciprocal is the first stage's exponent.
/// @param middleInput interior curve input in `(0, 1)`.
/// @param middleOutput finite curve output in `[0, 1]`.
/// @param probeStride distance between probes; must be nonzero.
/// @return the independent probes, or empty when an argument is inconsistent.
[[nodiscard]] std::vector<GammaTransferProbe> computeGammaLinearCurveOracle(std::span<const float> samples,
                                                                            double gamma, double middleInput,
                                                                            double middleOutput,
                                                                            std::uint32_t probeStride);

/// Largest absolute-or-relative error of a produced tight plane at the oracle probes.
///
/// @param produced the transferred samples.
/// @param probes the independent expected values.
/// @return the normalized maximum, or the largest finite double for malformed or non-finite input.
[[nodiscard]] double gammaTransferError(std::span<const float> produced,
                                        const std::vector<GammaTransferProbe>& probes) noexcept;

} // namespace an::bench
