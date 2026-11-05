// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Deviation, square, biweight and background-noise image statistics.
///
/// The estimators the image-statistics and PixelMath surfaces report beyond a mean and a median,
/// gathered into one result so that the centres they share are computed once
/// rather than once per number. The selection is the `StatisticsSelection` of the basic and robust
/// statistics, and the result reuses their centres: the mean from `computeImageBasicStatistics`,
/// the median and median absolute deviation from `computeImageRobustStatistics`, around which this
/// operation accumulates the deviations, squares and biweight sums in one further pass. The basic
/// statistics travel with the result, so a caller showing the extremes and the spread beside these
/// estimators does not measure the selection again, and a caller that reads no iterative k-sigma
/// estimator says so in its `AdvancedStatisticsRequest` rather than paying for a ranking it discards.
///
/// Two estimators are not textbook ones and are stated exactly where `AdvancedStatistics` declares
/// them: the background noise, taken from each row's first differences, and the simplified
/// iterative k-sigma location and scale.
///
/// Every number is the same at every admitted thread count: the ranks are exact, and the
/// accumulations this file adds use fixed row partitions and an ordered merge.

#pragma once

#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// Deviation, square, robust-scale and row-difference estimators of one selected image channel.
///
/// All values are computed in the source image's stated numeric domain after exact widening to
/// Float64. `sumOfSquares` is the sum of each sample squared and `meanOfSquares` divides that sum by
/// `selection.valid`. `backgroundNoise` is the median, over the rows holding at least two first
/// differences, of each row's population standard deviation of those differences after up to three
/// strict five-sigma clipping passes, divided by the square root of two; an even number of rows
/// averages the two middle deviations. `iterativeKsigmaLocation` and `iterativeKsigmaScale` are a
/// simplified IKSS estimator: retain the samples within six median absolute deviations of the
/// original median, rank that retained population again, and multiply the square root of its
/// biweight midvariance by 0.991. Invalid samples are skipped and the next valid sample is
/// differenced from the preceding valid sample.
struct AdvancedStatistics
{
    /// How many samples participated and how many pixels the selected region held.
    ValidityCount selection{};

    /// Arithmetic mean used as the center of `averageAbsoluteDeviation`.
    double mean = 0.0;

    /// Median used as the center of `biweightMidvariance`.
    double median = 0.0;

    /// Median absolute deviation used as the biweight scale.
    double medianAbsoluteDeviation = 0.0;

    /// Mean absolute distance from `mean`.
    double averageAbsoluteDeviation = 0.0;

    /// Sum of the squares of every participating sample.
    double sumOfSquares = 0.0;

    /// `sumOfSquares / selection.valid`.
    double meanOfSquares = 0.0;

    /// Tukey biweight midvariance of every participating sample around `median`, with tuning
    /// constant nine times `medianAbsoluteDeviation`; zero when that deviation is zero.
    double biweightMidvariance = 0.0;

    /// Robust first-difference background-noise estimate, or zero when no row has two differences.
    double backgroundNoise = 0.0;

    /// Median of the six-MAD-clipped population. The unclipped `median` instead when that
    /// population's own median absolute deviation is zero, and zero when the original one is or
    /// when the request left the iterative k-sigma estimators out.
    double iterativeKsigmaLocation = 0.0;

    /// Square root of the clipped biweight midvariance multiplied by 0.991, or zero when either
    /// median absolute deviation is zero or when the request left the estimator out.
    double iterativeKsigmaScale = 0.0;

    /// Samples retained by the six-MAD interval and samples admitted before that clipping; both
    /// zero when the original median absolute deviation is zero or the request left the iterative
    /// k-sigma estimators out, because no clipping runs.
    ValidityCount iterativeKsigmaSelection{};

    /// Number of complete or selected-plane scans performed by this operation and its delegates.
    std::uint64_t planeScans = 0u;

    /// Most context-accounted temporary memory held at once.
    std::uint64_t peakScratchBytes = 0u;

    /// The basic statistics of the same selection, which `selection` and `mean` are taken from:
    /// the extremes, the sum, the population variance and standard deviation, and the error bounds
    /// of the sum and the centred moment, exactly as `computeImageBasicStatistics` reports them.
    BasicStatistics basic{};
};

/// What `computeImageAdvancedStatistics` estimates beyond the estimators every call returns.
struct AdvancedStatisticsRequest
{
    /// Whether to rank the samples within six median absolute deviations of the median again for
    /// `iterativeKsigmaLocation` and `iterativeKsigmaScale`. That ranking is a further selection
    /// over the plane, as costly as the median and its deviation; a caller that reads neither
    /// estimator states false, and the result then carries zero for both and for
    /// `iterativeKsigmaSelection`, as it does when the median absolute deviation is zero.
    bool iterativeKsigma = true;
};

/// Computes the advanced statistics the image-statistics and PixelMath surfaces report.
///
/// The source may be planar, interleaved or mixed and may carry row and plane padding. Selection,
/// region and mask semantics are exactly those of `computeImageBasicStatistics`. The operation is
/// deterministic across admitted thread counts: rank estimators use the robust-statistics kernel,
/// while the remaining accumulations use up to sixteen fixed consecutive row partitions, merged in
/// partition order regardless of admitted concurrency. Each row's clipping retains its original
/// sample order. Temporary row differences and row estimates come from the execution's engine-context
/// memory resource, with at most sixteen row buffers held at once. The basic statistics the
/// operation starts from are returned in `basic`, and the six-MAD population is ranked again only
/// when the request asks for the iterative k-sigma estimators.
///
/// Thread-safe with respect to the source and selection, which are only read. Cancellation is
/// checked between delegated estimators and at row boundaries.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the resident representation the layout's element width describes.
/// @param samples first byte of the image storage. Must not be null and must be sample-aligned.
/// @param layout validated image geometry and strides.
/// @param channelIndex channel to reduce, below `layout.extent().channelCount()`.
/// @param selection validity, optional full-image mask and optional region.
/// @param request which optional estimators to compute; the default computes every one.
/// @return all advanced estimators, their shared centers and the basic statistics they came from;
///         `ErrorCode::EmptySelection` when the region is empty or nothing participates;
///         `ErrorCode::InvalidData` for a selected non-finite sample or a sum or squared
///         accumulation that cannot be represented;
///         `ErrorCode::NotInitialized` for an execution that holds nothing; `ErrorCode::Cancelled`
///         or `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this build
///         has no dispatch table for; `ErrorCode::InvalidArgument` for a null or misaligned source,
///         an unknown representation, a layout whose element size is not its size, a channel the
///         layout does not have, a region outside the image, or a mask missing under
///         `ValidityRule::ExplicitMask` or supplied under another rule;
///         `ErrorCode::IncompatibleArguments` for a mask that does not cover the image; or
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when working memory is
///         refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<AdvancedStatistics>
computeImageAdvancedStatistics(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                               const ImageLayout& layout, std::uint32_t channelIndex,
                               const StatisticsSelection& selection = {},
                               const AdvancedStatisticsRequest& request = {});

/// Computes advanced statistics from a checked typed image view.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param channelIndex channel to reduce.
/// @param selection validity, optional mask and optional region.
/// @param request which optional estimators to compute; the default computes every one.
/// @return whatever `computeImageAdvancedStatistics` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<AdvancedStatistics>
computeAdvancedStatistics(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                          std::uint32_t channelIndex, const StatisticsSelection& selection = {},
                          const AdvancedStatisticsRequest& request = {})
{
    return computeImageAdvancedStatistics(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(),
                                          channelIndex, selection, request);
}

/// What `computeImageAdvancedStatistics` and `computeAdvancedStatistics` do to their source image.
inline constexpr OperationEffect kAdvancedStatisticsEffect{.samples = SampleEffect::Reads};

} // namespace an
