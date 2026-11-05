// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracles for the shared stacking scenarios.
//
// Every oracle here is the closed form of what the engine computes for a stacking row, written
// once in double and owned by no runner, so a row is scored against arithmetic. The engine reads an
// exact zero as no-data for these rows, by a stated rule, so every oracle skips exact zeros, combines
// the remaining samples of each element in frame order, and leaves an element no frame reached at
// zero.

#pragma once

#include <cstdint>
#include <span>

namespace an::bench
{

/// A run of rows `[begin, end)` of one frame.
struct StackRowRange
{
    std::uint32_t begin = 0u; ///< First row, inclusive.
    std::uint32_t end = 0u; ///< One past the last row.
};

/// Whether a stacked element may be the oracle's value held to the unit interval instead of the value.
///
/// Captured lights carry calibrated samples below zero and hot pixels above one, and a stacked output may
/// be held to the unit interval; a row over them accepts either answer at each element, which changes
/// nothing where the oracle's value is inside the interval.
enum class StackClamp : std::uint8_t
{
    /// Only the oracle's value is accepted.
    Refused,

    /// The oracle's value or its clamp to [0, 1] is accepted.
    Accepted
};

/// What planting gross outliers did to a rejecting stack, over the elements where a planted sample is one.
struct PlantedStackMovement
{
    /// Elements judged: a planted sample replaced a captured one, and every captured sample is below the ceiling.
    std::uint64_t judged = 0u;

    /// The judged elements whose result moved by more than half what keeping the planted sample would move it.
    std::uint64_t moved = 0u;

    /// The largest movement over the judged elements; reported, not judged.
    double largest = 0.0;

    /// `moved` over `judged`, zero with nothing judged, infinity on a size mismatch or a non-finite element.
    double fraction = 0.0;
};

/// Compares a rejecting stack of planted frames with the same engine's stack of the frames as captured.
///
/// No engine-neutral closed form exists for what an iterated estimator keeps of a real sky, which has outliers of
/// its own. What a rejection family can be held to is that it rejects a gross outlier: keeping a planted sample
/// moves an element of `frames` by at least `(planted - ceiling) / frames`, and rejecting it moves the element by
/// about what replacing one ordinary sample does. An element is judged where a planted sample replaced a captured
/// one and every captured sample is below `ceiling`, so the planted value stands apart from the sky; a star core
/// or a hot pixel is left out. A family that keeps the outlier moves nearly every judged element by more than
/// half the smaller amount; one that rejects it moves only the few whose own captured samples include outliers
/// that the planted one tips.
///
/// @param planted the stack of the planted frames.
/// @param clean the same engine's stack of the frames as captured, by the same request.
/// @param plantedFrames the planted frames, one tight plane each.
/// @param captured the frames as captured, one tight plane each.
/// @param plantedValue the value planted.
/// @param ceiling the value every captured sample of a judged element is below.
/// @return the movement.
[[nodiscard]] PlantedStackMovement plantedStackMovement(std::span<const float> planted, std::span<const float> clean,
                                                        std::span<const std::span<const float>> plantedFrames,
                                                        std::span<const std::span<const float>> captured,
                                                        float plantedValue, float ceiling) noexcept;

/// The plain average of several frames at every element, held against a result.
///
/// The oracle sums the non-zero samples of each element in frame order in double and divides by
/// their count.
///
/// @param frames one tight plane per frame, all of `result.size()` samples.
/// @param result the stacked plane.
/// @param clamp whether the oracle's value held to the unit interval is also accepted.
/// @return the largest absolute residual of the result against the oracle over every element, or
///         infinity when a frame is not the result's size.
[[nodiscard]] double averageStackResidual(std::span<const std::span<const float>> frames, std::span<const float> result,
                                          StackClamp clamp = StackClamp::Refused) noexcept;

/// The weighted average with one weight per frame, held against a result.
///
/// Each element is the sum of `weight * sample` over its non-zero samples divided by the sum of
/// those weights, so the result is invariant to a common scale of the weights, which is what lets
/// an engine normalize them first.
///
/// @param frames one tight plane per frame.
/// @param weights one weight per frame, positive.
/// @param result the stacked plane.
/// @param clamp whether the oracle's value held to the unit interval is also accepted.
/// @return the largest absolute residual, or infinity on a size mismatch.
[[nodiscard]] double weightedAverageStackResidual(std::span<const std::span<const float>> frames,
                                                  std::span<const double> weights, std::span<const float> result,
                                                  StackClamp clamp = StackClamp::Refused) noexcept;

/// The peak-normalized sum, held against a result.
///
/// Each element is the sum of its non-zero samples; the result is every sum divided by the largest
/// sum over the plane and clamped to the unit interval, or zero everywhere when no sum is
/// positive. That is the domain the engine publishes a sum in by default.
///
/// @param frames one tight plane per frame.
/// @param result the stacked plane.
/// @return the largest absolute residual, or infinity on a size mismatch.
[[nodiscard]] double sumStackResidual(std::span<const std::span<const float>> frames,
                                      std::span<const float> result) noexcept;

/// The exact median of each element's non-zero samples, the mean of the two middle ones for an
/// even count, held against a result.
///
/// @param frames one tight plane per frame.
/// @param result the stacked plane.
/// @param clamp whether the oracle's value held to the unit interval is also accepted.
/// @return the largest absolute residual, or infinity on a size mismatch.
[[nodiscard]] double medianStackResidual(std::span<const std::span<const float>> frames, std::span<const float> result,
                                         StackClamp clamp = StackClamp::Refused) noexcept;

/// The smallest non-zero sample of each element, held against a result.
///
/// @param frames one tight plane per frame.
/// @param result the stacked plane.
/// @return the largest absolute residual, or infinity on a size mismatch.
[[nodiscard]] double minimumStackResidual(std::span<const std::span<const float>> frames,
                                          std::span<const float> result) noexcept;

/// The largest non-zero sample of each element, held against a result.
///
/// @param frames one tight plane per frame.
/// @param result the stacked plane.
/// @return the largest absolute residual, or infinity on a size mismatch.
[[nodiscard]] double maximumStackResidual(std::span<const std::span<const float>> frames,
                                          std::span<const float> result) noexcept;

/// The auto-adaptive weighted mean of each element's non-zero samples, held against a result.
///
/// The adopted algorithm, spelled in its own operation order: the samples scaled by 65,535, the
/// plain mean, then five iterations that weigh each sample by `1 / (1 + (distance / sigma)^2)`
/// around the current mean, with the plain mean kept when the first sigma is five or less and for
/// fewer than three samples.
///
/// @param frames one tight plane per frame.
/// @param result the stacked plane.
/// @return the largest absolute residual, or infinity on a size mismatch.
[[nodiscard]] double autoAdaptiveStackResidual(std::span<const std::span<const float>> frames,
                                               std::span<const float> result) noexcept;

/// The average of the samples that were not planted, held against a rejected mean.
///
/// Each frame excludes its own run of rows from every element under them; the remaining non-zero
/// samples of each element are averaged in frame order. This is what every rejection family
/// produces when it rejects exactly the planted samples, within the differences of the families'
/// treatments of the survivors that the row's tolerance covers.
///
/// @param frames one tight plane per frame.
/// @param width the frame width, which turns an element index into a row.
/// @param excludedRows one row range per frame.
/// @param result the stacked plane.
/// @return the largest absolute residual, or infinity on a size mismatch.
[[nodiscard]] double rejectedAverageStackResidual(std::span<const std::span<const float>> frames, std::uint32_t width,
                                                  std::span<const StackRowRange> excludedRows,
                                                  std::span<const float> result) noexcept;

/// A result held to a known plane element for element.
///
/// @param expected the plane the result must reproduce.
/// @param result the stacked plane.
/// @return the largest absolute residual, or infinity on a size mismatch.
[[nodiscard]] double planeResidual(std::span<const float> expected, std::span<const float> result) noexcept;

} // namespace an::bench
