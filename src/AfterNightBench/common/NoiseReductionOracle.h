// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracles for the multiscale and noise-reduction
// scenarios.
//
// Three definitions live here, and each of them is a definition rather than an implementation: what
// a multiscale round trip has to reproduce, what a bilateral weight is, and which samples an impulse
// threshold names. Evaluating them in the harness rather than in the engine is what makes the
// engine's answer comparable to something other than itself.
//
// **A multiscale round trip has one correct answer and the harness already holds it.** Decomposing
// an image into layers and adding them back with unit weights is the identity, so the oracle for
// that scenario is the input, and a difference is a transform that lost or invented energy rather
// than a tolerance argument.
//
// **The bilateral weight is two Gaussians multiplied together over a circular window.** The border
// is part of the comparison: it reflects about the edge sample, which is the engine's default, so a
// probe near an edge measures the same filter the interior does.
//
// **An impulse threshold is a relative deviation from a truncated-window median.** The truncated
// window is a convention the engine names, so the engine and the oracle rank the same population at
// every position and the comparison is about which samples were replaced rather than about what
// happens at a border.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// One position the oracle evaluated, and the value it expects there.
struct PlaneProbe
{
    /// The probe column.
    std::uint32_t x = 0u;

    /// The probe row.
    std::uint32_t y = 0u;

    /// What the definition produces at that position.
    double value = 0.0;
};

/// One RGB pixel the chromatic-noise oracle evaluated.
struct RgbProbe
{
    /// The probe column.
    std::uint32_t x = 0u;

    /// The probe row.
    std::uint32_t y = 0u;

    /// Expected planar red, green and blue values.
    std::array<double, 3u> value{};
};

/// Records the input itself at a deterministic subset of image positions.
///
/// The oracle for an operation whose correct answer is what it was given: a multiscale round trip
/// with unit weights, or any identity a runner claims.
///
/// @param samples the input plane, `width * height` contiguous samples.
/// @param width the plane width.
/// @param height the plane height.
/// @param stride how many image positions separate one probe from the next.
/// @return the probes, in ascending row-major order. Empty when the picture has no samples or the
///         stride is zero.
[[nodiscard]] std::vector<PlaneProbe> computeIdentityOracle(std::span<const float> samples, std::uint32_t width,
                                                            std::uint32_t height, std::uint32_t stride);

/// Evaluates a bilateral filter at a deterministic subset of image positions.
///
/// Every neighbour inside the disc of the stated radius contributes the product of a spatial
/// Gaussian in its distance and a range Gaussian in its difference from the centre, and the sum is
/// normalized by the weights. Coordinates outside the image reflect about the edge sample.
///
/// @param samples the input plane, `width * height` contiguous samples.
/// @param width the plane width.
/// @param height the plane height.
/// @param radius half the window, in pixels.
/// @param spatialSigma how far, in pixels, distance alone allows a neighbour.
/// @param rangeSigma how far, in the plane's own units, a difference alone allows a neighbour.
/// @param stride how many image positions separate one probe from the next.
/// @return the probes, in ascending row-major order.
[[nodiscard]] std::vector<PlaneProbe> computeBilateralOracle(std::span<const float> samples, std::uint32_t width,
                                                             std::uint32_t height, std::uint32_t radius,
                                                             double spatialSigma, double rangeSigma,
                                                             std::uint32_t stride);

/// Evaluates Chambolle's total-variation iteration at a deterministic subset of image positions.
///
/// The definition rather than the engine's arrangement of it: a serial scalar walk of the dual
/// iteration over the whole plane, at double precision, for exactly `iterations` steps with no
/// stopping rule. The gradient is a forward difference with the last row and column at zero and the
/// divergence is its adjoint, which is what makes the fixed point the minimum of the functional.
///
/// It costs `iterations` passes over the plane and is meant to be evaluated once per scenario.
///
/// @param samples the input plane, `width * height` contiguous samples.
/// @param width the plane width.
/// @param height the plane height.
/// @param weight the denoising weight: larger smooths more.
/// @param iterations how many steps to take.
/// @param stride how many image positions separate one probe from the next.
/// @return the probes, in ascending row-major order.
[[nodiscard]] std::vector<PlaneProbe> computeChambolleOracle(std::span<const float> samples, std::uint32_t width,
                                                             std::uint32_t height, double weight,
                                                             std::uint32_t iterations, std::uint32_t stride);

/// The Rudin-Osher-Fatemi functional a total-variation denoiser minimizes.
///
/// The total variation of the answer plus the weighted squared distance from the input. Two methods
/// that reach the same minimum report the same number, which is the only comparison available
/// between two iterations whose intermediate states are not meant to match.
///
/// @param answer the denoised plane.
/// @param source the plane it was denoised from, the same length.
/// @param width the plane width.
/// @param height the plane height.
/// @param fidelity the weight of the data term.
/// @return the functional's value.
[[nodiscard]] double totalVariationEnergy(std::span<const float> answer, std::span<const float> source,
                                          std::uint32_t width, std::uint32_t height, double fidelity);

/// The sample variance of a plane's difference from another.
///
/// What a noise generator is measured against: the distribution it was asked for is a statement
/// about this number, and it is the one property of an added noise field that does not depend on
/// which stream produced it.
///
/// @param produced the plane with noise in it.
/// @param source the plane it was added to, the same length.
/// @return the variance of the difference.
[[nodiscard]] double addedNoiseVariance(std::span<const float> produced, std::span<const float> source);

/// Evaluates an impulse rejection at a deterministic subset of image positions.
///
/// The median is taken over the part of the square window that falls inside the image, and a sample
/// is replaced by it when it stands more than the threshold above its own magnitude away. A sample
/// of exactly zero has no relative deviation and is kept.
///
/// @param samples the input plane, `width * height` contiguous samples.
/// @param width the plane width.
/// @param height the plane height.
/// @param radius half the square window.
/// @param highThreshold how far above the median, relative to the sample, is too far.
/// @param stride how many image positions separate one probe from the next.
/// @return the probes, in ascending row-major order.
[[nodiscard]] std::vector<PlaneProbe> computeImpulseOracle(std::span<const float> samples, std::uint32_t width,
                                                           std::uint32_t height, std::uint32_t radius,
                                                           double highThreshold, std::uint32_t stride);

/// Evaluates average-neutral chromatic-noise suppression over planar RGB samples.
///
/// @param samples three tightly packed planes in red, green, blue order.
/// @param width the image width.
/// @param height the image height.
/// @param stride how many pixels separate probes.
/// @return expected RGB probes, or none for an invalid shape or stride.
[[nodiscard]] std::vector<RgbProbe> computeAverageNeutralOracle(std::span<const float> samples, std::uint32_t width,
                                                                std::uint32_t height, std::uint32_t stride);

/// Evaluates horizontal banding suppression with full amount and no highlight protection.
///
/// Each row is shifted by the global median minus its own median, normalized by the smallest shift
/// so no row is pulled below the original floor.
///
/// @param samples one tightly packed plane.
/// @param width the image width.
/// @param height the image height.
/// @param stride how many pixels separate probes.
/// @return expected output probes, or none for an invalid shape or stride.
[[nodiscard]] std::vector<PlaneProbe> computeHorizontalBandingOracle(std::span<const float> samples,
                                                                     std::uint32_t width, std::uint32_t height,
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
[[nodiscard]] double planeProbeError(std::span<const float> produced, std::uint32_t width,
                                     const std::vector<PlaneProbe>& probes) noexcept;

/// A canonical representation of what a runner produced at the probe positions.
///
/// @param produced the plane the runner wrote.
/// @param width the plane width.
/// @param probes the positions to report.
/// @return one line per probe, in ascending row-major order.
[[nodiscard]] std::string planeProbeText(std::span<const float> produced, std::uint32_t width,
                                         const std::vector<PlaneProbe>& probes);

/// The largest normalized error across the three planes at RGB probes.
///
/// @param produced three tightly packed RGB planes.
/// @param width the image width.
/// @param height the image height.
/// @param probes expected RGB values.
/// @return the worst normalized error, or the largest finite double for invalid input.
[[nodiscard]] double rgbProbeError(std::span<const float> produced, std::uint32_t width, std::uint32_t height,
                                   const std::vector<RgbProbe>& probes) noexcept;

/// A canonical representation of planar RGB values at the oracle probes.
///
/// @param produced three tightly packed RGB planes.
/// @param width the image width.
/// @param height the image height.
/// @param probes positions to report.
/// @return one line per RGB probe.
[[nodiscard]] std::string rgbProbeText(std::span<const float> produced, std::uint32_t width, std::uint32_t height,
                                       const std::vector<RgbProbe>& probes);

} // namespace an::bench
