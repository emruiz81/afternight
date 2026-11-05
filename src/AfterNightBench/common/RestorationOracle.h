// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracles for the patch-denoise and deconvolution
// scenarios.
//
// Both families are measured against something the harness states rather than against the other
// engine, and in both cases the harness has to build the input itself for that to be possible.
//
// A deconvolution is handed a frame the harness blurred by a kernel the harness built, so the
// unblurred frame is the exact answer and the metric is the distance from it. Any engine that
// returns its input scores exactly the blur's own distance, so a row that fails to sharpen cannot
// pass by accident.
//
// A denoiser is handed a noisy frame and measured by how much of the noise it removed, using the
// harness's own row-difference estimate of the level rather than the engine's. That is a
// property of the answer rather than of the implementation, so the engine is judged on that number
// whatever its own internal estimate is.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// The full width at half maximum of the blur the deconvolution rows are measured against, in
/// pixels.
///
/// Three is a seeing-limited stellar image on a well-sampled frame: wide enough that the blur is
/// unmistakable in the metric and narrow enough that recovering it is a fair test rather than an
/// impossible one.
inline constexpr double kRestorationBlurFwhm = 3.0;

/// The full width at half maximum of a Gaussian, in units of its standard deviation.
inline constexpr double kRestorationFwhmPerSigma = 2.354820045030949382;

/// A normalised Gaussian kernel of a stated full width at half maximum.
///
/// Built here rather than taken from the engine, so the blur the metric is defined against is
/// the harness's and not a party to the comparison. The side is the odd number at or above four
/// standard deviations either side of the centre, which is where the profile has fallen below a
/// ten-thousandth of its peak.
///
/// @param fwhm the full width at half maximum, in pixels.
/// @param side where the side that was built is written.
/// @return the samples, row-major, summing to one.
[[nodiscard]] std::vector<float> restorationBlurKernel(double fwhm, std::uint32_t& side);

/// A frame convolved by a kernel, reflecting at the borders without repeating the edge.
///
/// @param samples the frame, `width * height` contiguous samples.
/// @param width the frame width.
/// @param height the frame height.
/// @param kernel the kernel, row-major.
/// @param side the kernel's side, which must be odd.
/// @return the blurred frame.
[[nodiscard]] std::vector<float> restorationBlur(std::span<const float> samples, std::uint32_t width,
                                                 std::uint32_t height, std::span<const float> kernel,
                                                 std::uint32_t side);

/// The root-mean-square distance between a restored frame and the frame it should have recovered.
///
/// A non-finite sample makes the whole measurement the largest finite double, which is what stops a
/// run that produced infinities from scoring well on the samples that survived.
///
/// @param restored what the engine produced.
/// @param original what it should have produced.
/// @return the distance, in the frame's own sample units.
[[nodiscard]] double restorationResidual(std::span<const float> restored, std::span<const float> original);

/// How much of a blur's own error survived a restoration.
///
/// One means the operation returned its input and zero means it recovered the original exactly, so
/// a row that fails to sharpen cannot pass by accident and the number is free of the fixture's own
/// amplitude. It is the metric the deconvolution rows are scored on.
///
/// @param restored what the engine produced.
/// @param blurredInput what the engine was given.
/// @param original what both should be compared against.
/// @return the surviving fraction, or the largest finite double when it is not finite.
[[nodiscard]] double restorationResidualRatio(std::span<const float> restored, std::span<const float> blurredInput,
                                              std::span<const float> original);

/// A canonical representation of one restoration.
///
/// The distance from the original plus a fixed stride of samples, so a digest changes when the
/// picture does and not only when the summary statistic does.
///
/// @param restored what the engine produced.
/// @param original what it should have produced.
/// @param width the frame width, so the sampled positions are a lattice rather than a run.
/// @return the text to digest.
[[nodiscard]] std::string restorationText(std::span<const float> restored, std::span<const float> original,
                                          std::uint32_t width);

/// The harness's own estimate of a frame's background noise level.
///
/// The median over rows of the sigma-clipped standard deviation of that row's first differences,
/// divided by the square root of two. Written here so that a denoiser is scored by one
/// definition: the engine has its own internal estimator, and scoring a denoiser on the engine's
/// own estimate would measure the estimator rather than the denoiser.
///
/// @param samples the frame.
/// @param width the frame width.
/// @param height the frame height.
/// @return the estimate, in the frame's own sample units.
[[nodiscard]] double restorationNoiseLevel(std::span<const float> samples, std::uint32_t width, std::uint32_t height);

/// How much of a frame's noise survived a denoise.
///
/// One means nothing was removed and zero means everything was; a value above one means the
/// operation added noise. Using the ratio rather than the level makes the row insensitive to the
/// fixture's own amplitude.
///
/// @param denoised what the engine produced.
/// @param noisy what it was given.
/// @param width the frame width.
/// @param height the frame height.
/// @return the surviving fraction, or the largest finite double when the answer is not finite.
[[nodiscard]] double restorationNoiseRatio(std::span<const float> denoised, std::span<const float> noisy,
                                           std::uint32_t width, std::uint32_t height);

/// A canonical representation of one denoise.
///
/// @param denoised what the engine produced.
/// @param noisy what it was given.
/// @param width the frame width.
/// @param height the frame height.
/// @return the text to digest.
[[nodiscard]] std::string restorationDenoiseText(std::span<const float> denoised, std::span<const float> noisy,
                                                 std::uint32_t width, std::uint32_t height);

} // namespace an::bench
