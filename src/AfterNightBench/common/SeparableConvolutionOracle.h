// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared separable-Gaussian scenario.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// One position the oracle evaluated and the value it produced there.
struct ConvolutionProbe
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
    double value = 0.0;
};

/// The taps the runner convolves with.
///
/// A normalized Gaussian sampled at integer offsets from the center: `exp(-d^2 / (2 sigma^2))`
/// divided by the sum. Stated here rather than inside the engine so that the filter is the
/// harness's and the comparison is about how the engine applies it.
///
/// @param radius half the support, so the array is `2 * radius + 1` long.
/// @param sigma the standard deviation in samples. Must be positive.
/// @return the taps, summing to one.
[[nodiscard]] std::vector<double> gaussianSeparableTaps(std::uint32_t radius, double sigma);

/// The support a standard deviation gives a floating image's Gaussian.
///
/// It rounds `sigma * 8 + 1` to an odd kernel size. The runner and the oracle take the radius from
/// here so that the engine is not filtering with a wider kernel than the reference.
///
/// @param sigma the standard deviation in samples. Must be positive.
/// @return half the support.
[[nodiscard]] std::uint32_t gaussianRadiusForSigma(double sigma);

/// Evaluates a separable Gaussian convolution at a deterministic subset of positions.
///
/// The evaluation is the two-dimensional definition rather than two passes, accumulated in
/// `long double`, with the border mirrored about the edge sample without repeating it - the rule
/// the runner asks for. Doing it at probes rather than everywhere keeps an independent
/// reference affordable at production sizes; the probe positions are a fixed stride through the
/// row-major order, so they include edges, corners and interiors and do not move between runs.
///
/// @param samples the input plane, `width * height` contiguous samples.
/// @param width the plane width.
/// @param height the plane height.
/// @param taps the separable taps, of odd length.
/// @param stride how many samples separate one probe from the next in row-major order.
/// @return the probes, in ascending row-major order.
[[nodiscard]] std::vector<ConvolutionProbe> computeSeparableGaussianOracle(std::span<const float> samples,
                                                                           std::uint32_t width, std::uint32_t height,
                                                                           std::span<const double> taps,
                                                                           std::uint32_t stride);

/// The largest normalized deviation of a produced plane from the oracle at its probes.
///
/// Values whose magnitude is below one use absolute error; larger values use relative error. An
/// empty probe set or a non-finite sample returns the largest finite double, so a result stays
/// schema-valid and fails its declared tolerance.
[[nodiscard]] double separableConvolutionError(std::span<const float> produced, std::uint32_t width,
                                               const std::vector<ConvolutionProbe>& probes) noexcept;

/// A canonical representation of what a runner produced at the probe positions.
[[nodiscard]] std::string separableConvolutionText(std::span<const float> produced, std::uint32_t width,
                                                   const std::vector<ConvolutionProbe>& probes);

} // namespace an::bench
