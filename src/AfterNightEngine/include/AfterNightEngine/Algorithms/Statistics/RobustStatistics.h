// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Median, quantiles, deviation and clipped estimators over one image channel.
///
/// The estimators an astronomical pipeline actually decides with. A mean and a standard deviation
/// describe a frame only until one hot pixel, one satellite trail or one cosmic ray hit reaches it;
/// what normalization, rejection and background modelling ask for instead is a rank and a deviation
/// around it, which move by one sample when the worst sample in the frame doubles.
///
/// **Nothing here holds one working element per input sample.** Ranking a copy of every absolute
/// deviation costs an allocation and a full copy per call - per pixel per iteration inside a
/// rejection loop. The order-statistic substrate underneath this operation narrows a rank by
/// counting instead, so the memory it holds is a function of the partition and of a stated gather
/// ceiling rather than of the image, and `peakScratchBytes` reports it. A caller that wants the
/// other trade makes it explicitly through `RoiSelectionStrategy`.
///
/// **Selection is stated exactly as it is for basic statistics** - the same `StatisticsSelection`,
/// carrying the image's validity rule, the packed mask when that rule names one, and an optional
/// rectangle - so the two operations cannot come to disagree about which samples an image has.
///
/// **A clipped estimator is a rule, not a mood.** `SigmaClippingRequest` states how far from the
/// current location a sample may sit and still count, in units of the current scale, and how many
/// times that may be re-decided. Each iteration re-ranks the samples the interval retains; it does
/// not rank a compacted survivor list, so no iteration is reading a copy that the one before it
/// made. The result carries the interval it stopped at, whether it stopped because the retained
/// count stopped moving, and both the robust and the ordinary moments of what it retained.
///
/// Every result is thread-count independent under both `ReductionDeterminism` policies, because the
/// order statistics underneath it are exact: a rank is the sample an ascending sort would place
/// there, and a partition cannot move it.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named rather than included so a statistics result does not pull the scheduler implementation's
/// include closure into every consumer that stores one.
class ExecutionContext;

/// What multiplying a median absolute deviation by yields a standard-deviation-like scale.
///
/// For normally distributed samples the median absolute deviation is `0.6745` standard deviations,
/// so this reciprocal is what makes a robust scale comparable with a standard deviation. It is a
/// property of the normal distribution and not of this engine; a caller whose samples are not
/// approximately normal should read `medianAbsoluteDeviation` and scale it itself.
inline constexpr double kMedianAbsoluteDeviationToSigma = 1.4826022185056018;

/// How a robust operation reads the samples its selection admits.
///
/// A robust estimator reads its selection several times - once to count and bound it, twice for
/// each rank it resolves, and again for every clipping iteration - and each of those reads pays the
/// source strides and the validity rule again. Which of the two answers below is faster is a
/// property of the selection's shape rather than of the operation, so it is a choice with a stated
/// default rather than a decision buried in an implementation.
enum class RoiSelectionStrategy : std::uint8_t
{
    /// Read the samples where they lie, through the source strides, on every pass.
    ///
    /// Holds nothing proportional to the selection. Always available, and the only answer when the
    /// selection is too large for the compaction ceiling.
    StridedSource = 0u,

    /// Gather the admitted samples once into contiguous engine memory, then read that.
    ///
    /// Costs eight bytes per admitted sample, taken from the engine context and counted against its
    /// budget, and it is refused above `kMaxCompactSelectionBytes`. Every pass after the gather then
    /// reads contiguous memory and evaluates no validity rule at all, which is what it trades its
    /// two extra passes - one to count, one to gather - for. On both shapes measured so far - a
    /// masked region of half a frame read five times, and a whole frame read twenty-seven times by
    /// a clipping iteration - that trade lost, because a rectangle of a planar frame is already
    /// close to a sequential read. It is kept because the shape it should win on, a sparse
    /// selection scattered through a large frame, has not been measured, and because the choice
    /// belongs to a caller that knows its own data.
    CompactSelection = 1u,

    /// Let the operation choose between the two.
    ///
    /// On the evidence above the engine chooses `StridedSource`, and that is what this currently
    /// means. It is a distinct enumerator rather than a synonym so that a caller stating it is
    /// asking for whatever the engine has measured to be best rather than for one of the two by
    /// name. The result reports which one ran, so a caller never has to infer it.
    Automatic = 2u
};

/// Whether a value names one of the selection strategies this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param strategy the value to inspect.
/// @return true for every supported strategy.
[[nodiscard]] constexpr bool isRoiSelectionStrategy(RoiSelectionStrategy strategy) noexcept
{
    return strategy == RoiSelectionStrategy::StridedSource || strategy == RoiSelectionStrategy::CompactSelection ||
           strategy == RoiSelectionStrategy::Automatic;
}

/// The most engine memory one compacted selection may hold.
///
/// A ceiling rather than a suggestion. Compaction is the one place this operation holds memory
/// proportional to its input, so how much that may be is stated here where a caller reasoning about
/// a budget can read it, rather than discovered when a full frame of a large sensor is asked for.
/// Sixty-four mebibytes is eight million samples, which is a generous region of interest and a
/// fraction of one full frame.
inline constexpr std::uint64_t kMaxCompactSelectionBytes = 64ull * 1024ull * 1024ull;

/// The most clipping iterations one request may ask for.
///
/// The iteration stops on its own when the retained count stops moving, and in practice does so
/// within a handful of passes. The ceiling is what stops a caller turning a bounded estimator into
/// an unbounded number of reads of the frame by writing a large number into one field.
inline constexpr std::uint32_t kMaxSigmaClippingIterations = 32u;

/// How far from the current location a sample may sit and still be counted.
///
/// The interval is `[location - lowSigma * scale, location + highSigma * scale]`, where the
/// location is the current median and the scale is the current median absolute deviation scaled by
/// `kMedianAbsoluteDeviationToSigma`. The two sides are separate because sky background is not
/// symmetric: stars, trails and hits are bright, and the same rule that should reject them would
/// throw away real signal if applied as tightly below the background. Each endpoint is clamped to
/// the selection's extremes, which removes no sample and keeps the endpoints finite.
struct SigmaClippingRequest
{
    /// How many scales below the location the interval reaches. Must be finite and not negative.
    double lowSigma = 3.0;

    /// How many scales above the location the interval reaches. Must be finite and not negative.
    double highSigma = 3.0;

    /// The most times the interval may be re-decided. At least one, at most
    /// `kMaxSigmaClippingIterations`.
    std::uint32_t maximumIterations = 5u;
};

/// What one robust-statistics operation is asked to produce.
///
/// The median and the extremes are always produced: a median costs one descent whether or not it
/// was named, and the deviation and the clipping are defined around it. Everything else is asked
/// for, because each costs further reads of the frame.
struct RobustStatisticsRequest
{
    /// The fractions to select, each within `[0, 1]`, at most `kMaxOrderStatisticRanks` of them.
    ///
    /// Borrowed for the call. May be empty, which asks for the median alone.
    std::span<const double> quantiles{};

    /// Which rule resolves a quantile falling between two ranks.
    QuantileInterpolation interpolation = QuantileInterpolation::Linear;

    /// Whether to produce the median absolute deviation and the scale derived from it.
    ///
    /// Implied by `sigmaClipping`, which is defined in terms of that scale.
    bool medianAbsoluteDeviation = false;

    /// The clipping rule, or nothing to produce no clipped estimator.
    std::optional<SigmaClippingRequest> sigmaClipping{};

    /// How the operation reads the samples its selection admits.
    RoiSelectionStrategy roiStrategy = RoiSelectionStrategy::Automatic;

    /// The closed interval every admitted sample is read as held to, or nothing to read each sample as it is stored.
    ///
    /// A hold admits no sample and turns none away: the selection's rule, mask and region decide on the stored
    /// value, and an admitted sample lying outside the hold is then read as its nearer end. The extremes, the
    /// median, every quantile and the deviation are those of the held values, under either strategy, so a caller
    /// that measures what a display shows - every sample held to the unit interval - measures the stored samples
    /// where they lie rather than a copy of them held first. Both endpoints must be finite and ordered, and a hold is
    /// refused beside `sigmaClipping`, whose interval is read on the stored values.
    std::optional<KernelValueWindow> hold{};
};

/// The location, scale and moments of the samples a clipping rule retained.
///
/// Present in a result exactly when the request asked for clipping. Every field describes the final
/// interval: a caller comparing two frames compares estimators taken over intervals of their own,
/// which is what makes them robust and is also why the interval is reported beside them.
struct SigmaClippedStatistics
{
    /// How many samples the final interval retained, and how many the selection admitted before it.
    ValidityCount selection{};

    /// The final interval's inclusive lower endpoint.
    double lowest = 0.0;

    /// The final interval's inclusive upper endpoint.
    double highest = 0.0;

    /// The median of the retained samples.
    double location = 0.0;

    /// Their median absolute deviation scaled by `kMedianAbsoluteDeviationToSigma`.
    double scale = 0.0;

    /// Their median absolute deviation, unscaled.
    double medianAbsoluteDeviation = 0.0;

    /// Their arithmetic mean, accumulated through the deterministic Float64 tree.
    double mean = 0.0;

    /// Their population standard deviation, dividing by the retained count.
    double standardDeviation = 0.0;

    /// How many intervals were decided, which is at least one and at most the requested maximum.
    std::uint32_t iterations = 0u;

    /// Whether some iteration retained exactly as many samples as the interval before it - the
    /// unclipped selection, for the first - which stops the iteration; false when the iteration
    /// ceiling stopped it first.
    ///
    /// False is a fact about the request rather than a failure: it says the estimator was still
    /// moving when it was told to stop, and a caller that cares raises `maximumIterations`.
    bool converged = false;
};

/// Robust order statistics of one selected image channel.
///
/// `selection.valid` is how many samples participated and `selection.total` how many pixels the
/// selected region held before validity was applied. No value exists for an empty selection; the
/// operation returns `ErrorCode::EmptySelection` instead.
struct RobustStatistics
{
    /// How many samples participated and how many pixels the selected region held.
    ValidityCount selection{};

    /// Smallest participating sample.
    double minimum = 0.0;

    /// Largest participating sample.
    double maximum = 0.0;

    /// The quantile at one half under the requested interpolation rule.
    double median = 0.0;

    /// The median of the absolute deviations from `median`, or a quiet NaN when not asked for.
    double medianAbsoluteDeviation = 0.0;

    /// `medianAbsoluteDeviation * kMedianAbsoluteDeviationToSigma`, or a quiet NaN when not asked
    /// for.
    double robustScale = 0.0;

    /// The clipped estimator, present exactly when the request asked for clipping.
    std::optional<SigmaClippedStatistics> clipped{};

    /// How many times the samples were read, which is what the operation cost.
    ///
    /// One pass counts and bounds the selection; each rank that has to be resolved costs two more;
    /// each clipping iteration costs what its own ranking does, and the clipped moments two more; a
    /// compacted selection pays two further passes, one to count and one to gather, and reads
    /// contiguous memory after them. A median with its deviation over a large Float32 selection that
    /// no mask or rule decides is counted and gathered in two passes together where the samples allow
    /// it, and one an eight- or sixteen-bit plane holds is read once into a table of its codes.
    std::uint64_t planeScans = 0u;

    /// The most engine memory held at once, including any compacted selection.
    std::uint64_t peakScratchBytes = 0u;

    /// Which strategy actually read the samples, which is never `RoiSelectionStrategy::Automatic`.
    RoiSelectionStrategy roiStrategy = RoiSelectionStrategy::StridedSource;
};

/// Computes robust statistics for one channel of an image known only at run time.
///
/// The source layout may be planar, interleaved or mixed and may contain row and plane padding. A
/// channel is counted in plane-major order across the image rather than assumed to be a plane. The
/// source, layout-owned geometry, selection mask and region, requested quantiles and output span
/// are borrowed for the call and no caller storage but `quantileValues` is changed.
///
/// Every reported rank is exact - the sample an ascending sort of the selection would place there -
/// and is produced without holding one working element per input sample. A selected non-finite
/// sample is refused with `ErrorCode::InvalidData`; a caller whose stored non-finite values mean
/// no-data states `ValidityRule::FiniteOnly` and they are filtered instead.
///
/// Under `RoiSelectionStrategy::Automatic` the operation reads the source where it lies, which is
/// what measurement has favoured. It compacts only when `RoiSelectionStrategy::CompactSelection` is
/// asked for by name, and then refuses a selection above `kMaxCompactSelectionBytes` rather than
/// falling back to the strided read. Both strategies select from the same samples, so every rank -
/// the extremes, the quantiles, the median, the deviation, the clipped location and scale, and
/// every count - is bit-identical between them, because a rank is a sample rather than an
/// accumulation. The clipped mean and standard deviation are accumulations, and a compacted plane
/// is a different shape from the rectangle it was gathered from, so its deterministic partition
/// associates the same terms differently: those two agree between strategies to within the
/// reduction error bound rather than bit for bit. Within one strategy they are bit-identical at
/// every admitted thread count.
///
/// Thread-safe with respect to the source and selection, which are only read. Calls using separate
/// execution contexts may run concurrently.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the resident representation the layout's element width describes.
/// @param samples first byte of the image storage. Must not be null and must be aligned for the
///        resident representation.
/// @param layout validated image geometry and strides.
/// @param channelIndex channel to rank, below `layout.extent().channelCount()`.
/// @param selection validity, optional full-image mask and optional region.
/// @param request which estimators are wanted, under which rules, read which way.
/// @param quantileValues where the requested quantiles go, exactly as long as `request.quantiles`
///        and disjoint from both it and the source storage.
/// @return the estimators and the facts about how they were produced;
///         `ErrorCode::EmptySelection` when the region is empty, nothing participates, or a clipping
///         interval retains no sample; `ErrorCode::IncompatibleArguments` when `quantileValues` is
///         not as long as the requested quantiles, the mask does not cover the image, or a hold is
///         asked for beside a clipping rule; `ErrorCode::InvalidArgument` for an unknown
///         interpolation rule or strategy, a clipping rule with a non-finite or negative width, a
///         hold that is not a finite ordered closed interval, a null or misaligned source, an unknown
///         representation, a layout whose element size is not its size, a channel the layout does
///         not have, a region outside the source, a mask missing under `ValidityRule::ExplicitMask`
///         or supplied under another rule, a `quantileValues` span overlapping the source or the
///         requested quantiles, or a compacted selection the engine context's limits cannot
///         describe; `ErrorCode::OutOfRange` for too many quantiles, a quantile outside `[0, 1]`, an
///         iteration count outside its ceiling, or a compaction above `kMaxCompactSelectionBytes`;
///         `ErrorCode::InvalidData` for a selected non-finite sample; `ErrorCode::NotInitialized`
///         for an execution that holds nothing; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this build
///         has no dispatch table for; or `ErrorCode::MemoryBudgetExceeded` or
///         `ErrorCode::AllocationFailed` when working memory is refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RobustStatistics>
computeImageRobustStatistics(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                             const ImageLayout& layout, std::uint32_t channelIndex,
                             const StatisticsSelection& selection, const RobustStatisticsRequest& request,
                             std::span<double> quantileValues = {});

/// Computes robust statistics from a checked typed image view.
///
/// The typed spelling of `computeImageRobustStatistics`. It adds no numeric, selection or ordering
/// rule: the representation, sample pointer and layout are read from the view, and the untyped entry
/// point owns every check and computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param channelIndex channel to rank.
/// @param selection validity, optional mask and optional region.
/// @param request which estimators are wanted, under which rules, read which way.
/// @param quantileValues where the requested quantiles go.
/// @return whatever `computeImageRobustStatistics` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<RobustStatistics>
computeRobustStatistics(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                        std::uint32_t channelIndex, const StatisticsSelection& selection,
                        const RobustStatisticsRequest& request, std::span<double> quantileValues = {})
{
    return computeImageRobustStatistics(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), channelIndex,
                                        selection, request, quantileValues);
}

/// What `computeImageRobustStatistics` and `computeRobustStatistics` do to the image they read.
///
/// The same declaration as the basic statistics next door, for the same reason: an exact rank, a
/// median absolute deviation and a clipped location are numbers taken from a selection, and a
/// selection over part of an image changes nothing about the image.
inline constexpr OperationEffect kRobustStatisticsEffect{.samples = SampleEffect::Reads};
} // namespace an
