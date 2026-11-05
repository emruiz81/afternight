// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the three sky figures a detection needs, measured in parallel passes.
//
// Private to the star domain. `measureStarChannel` is the complete measurement of a channel and is
// what a caller asks for when it wants the deviation and the robust scale as well; this is what the
// detector itself runs, and it exists because those two estimators are most of the cost of the
// complete one and the detector reads neither.
//
// The threshold a candidate scan compares against is the sky plus a multiple of the noise, the
// dynamic range is full scale above the sky, and nothing else in a detection reads a frame-wide
// statistic. So this produces exactly the median, the largest sample and the row-difference noise,
// and it produces them from the tight Float32 working plane the detection has already built rather
// than from the caller's frame through its strides.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>

namespace an
{

class ExecutionContext;

namespace stars_detail
{

/// What a detection needs to know about the sky it is searching.
struct DetectionSky
{
    /// The median of the admitted samples, in the plane's numeric domain.
    double median = 0.0;

    /// The largest admitted sample.
    double maximum = 0.0;

    /// The robust first-difference background-noise estimate.
    double backgroundNoise = 0.0;

    /// How many samples the validity rule admitted.
    std::uint64_t admittedSamples = 0u;
};

/// Measures the sky of one tight Float32 plane.
///
/// Three passes over the searched rectangle, each on the operation's threads, and a fourth when the
/// two middle ranks of an even count fall in different buckets. The first builds a histogram of the
/// high half of each sample's order-preserving key and takes the largest sample; the second
/// histograms the low half within the bucket the median falls in; the last reproduces the engine's
/// row-difference noise estimator row by row. The median is the same number
/// `computeImageAdvancedStatistics` reports - the average of the two middle values for an even
/// count - and the noise is the same estimator with the same clipping schedule, evaluated on rows
/// instead of on one serial walk.
///
/// Thread-safe with respect to `execution` and the plane, which is only read. It makes one
/// context-accounted allocation for the per-worker histograms and row estimates and releases it
/// before returning. Cancellation is checked at chunk boundaries.
///
/// @param execution the operation policy and engine-context lease.
/// @param plane the first sample of a tight single-channel Float32 plane.
/// @param width the plane's width in samples, which is also its row stride.
/// @param height the plane's height in rows.
/// @param region the rectangle to measure, which must lie inside the plane.
/// @param validity which samples participate, with the mask plane already selected.
/// @return the three figures; `ErrorCode::InvalidArgument` for a null plane or an empty region or
///         one outside the plane; `ErrorCode::EmptySelection` when the rule admits no sample;
///         `ErrorCode::InvalidData` when an admitted sample is not finite; or a cancellation or
///         memory-budget failure. Failures name `an::detectStars`, the one operation that calls
///         this.
[[nodiscard]] Result<DetectionSky> measureDetectionSky(const ExecutionContext& execution, const float* plane,
                                                       std::uint32_t width, std::uint32_t height,
                                                       const StatisticsRegion& region,
                                                       const KernelValidityView& validity);

} // namespace stars_detail

} // namespace an
