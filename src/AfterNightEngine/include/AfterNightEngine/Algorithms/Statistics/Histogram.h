// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Counts, cumulative counts and percentiles over one image channel.
///
/// A histogram is what a person looks at. Almost everything an image application shows about the
/// distribution of a frame - the display curve, the screen stretch, the clipping warnings, the
/// autostretch a user presses once and accepts - is read off one, and it is recomputed every time
/// the selection or the display changes. So it is a hot path with a human at the end of it, and both
/// halves of that matter: it has to be fast, and it has to say what it counted.
///
/// **The bins are the caller's memory.** How many there are is the length of the span, not a field,
/// because the counters are the caller's and their size is a property of that memory. A display
/// asking for 256 and a screen stretch asking for 65,536 are the same operation with different
/// spans, and neither makes the engine hold a buffer whose size the caller cannot see.
///
/// **The interval is closed and stated, and what falls outside it is counted rather than clamped.**
/// Clamping an out-of-range sample into the first or last bin would give a frame with a bright tail
/// and a frame that is genuinely saturated the same last bin, so `belowRange`, `aboveRange` and
/// `notANumber` are separate numbers and no sample outside the interval enters a bin.
///
/// **A percentile from a histogram is approximate, and says so.** `histogramQuantileValue` resolves
/// a quantile to the resolution of the bins, which is what an autostretch actually wants: it is
/// reading a distribution it is about to draw. A caller that needs the exact sample an ascending
/// sort would place at a rank asks `Algorithms/Statistics/RobustStatistics.h` instead, which costs
/// more reads of the frame and answers exactly. Both exist because both questions are asked.
///
/// Every count is thread-count independent: the per-chunk counters are merged in a fixed order over
/// the chunk indices and the partition is a function of the work and the bin count alone.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <span>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named rather than included so a histogram result does not pull the scheduler implementation's
/// include closure into every consumer that stores one.
class ExecutionContext;

/// The interval one image histogram divides into its caller's bins.
///
/// The bin count is the caller's span rather than a field, for the reason this header opens with.
struct ImageHistogramRequest
{
    /// Which of the two ways the interval is decided.
    HistogramDomain domain = HistogramDomain::Stated;

    /// Inclusive smallest value in the first bin under `HistogramDomain::Stated`. Must be finite.
    double lowest = 0.0;

    /// Inclusive largest value in the last bin under `HistogramDomain::Stated`. Must be finite and
    /// greater than `lowest`.
    double highest = 1.0;
};

/// What one image histogram counted, and the partition it counted with.
///
/// The bins themselves are the caller's span; this is everything else the caller needs to read them.
/// The interval of bin `i` is
/// `[lowest + i * (highest - lowest) / binCount, lowest + (i + 1) * (highest - lowest) / binCount]`,
/// closed at the top for the last bin alone, which is what makes `highest` a value the histogram
/// counts rather than one it excludes.
struct ImageHistogram
{
    /// How many samples the selection admitted and how many pixels the region held.
    ValidityCount selection{};

    /// How many admitted samples entered a bin, which is the selection less the three counts below.
    std::uint64_t counted = 0u;

    /// Admitted samples below the interval, negative infinity included. None entered a bin.
    std::uint64_t belowRange = 0u;

    /// Admitted samples above the interval, positive infinity included. None entered a bin.
    std::uint64_t aboveRange = 0u;

    /// Admitted not-a-number samples, which have no ordered position and entered no bin.
    std::uint64_t notANumber = 0u;

    /// The resolved inclusive lower endpoint, which is the request's under `Stated`.
    double lowest = 0.0;

    /// The resolved inclusive upper endpoint.
    double highest = 0.0;

    /// How many bins the caller supplied.
    std::uint64_t binCount = 0u;

    /// Chunks the selection was divided into, derived from the work and the bin count alone.
    std::uint64_t deterministicChunks = 0u;

    /// Workers the operation could actually use: the smaller of admitted concurrency and chunks.
    std::uint32_t plannedWorkers = 0u;

    /// Context memory held for the per-chunk counters, never above `kMaxHistogramPartialBytes`.
    std::uint64_t partialBytes = 0u;
};

/// Which value inside the bracketing bin a quantile resolves to.
///
/// A histogram knows how many samples fell in a bin and nothing about where in it they fell, so
/// every rule below is a stated convention rather than an approximation of one true answer. Naming
/// the convention is what lets two callers agree about a number neither of them can refine.
enum class HistogramQuantileRule : std::uint8_t
{
    /// The bracketing bin's inclusive lower endpoint. The conservative reading, and the one a
    /// clipping threshold usually wants.
    LowerEdge = 0u,

    /// The middle of the bracketing bin. The unbiased reading when nothing is known about the
    /// distribution inside a bin.
    BinCenter = 1u,

    /// Linear interpolation across the bracketing bin, assuming its samples are spread evenly
    /// through it. The reading a display curve wants, and the one that moves smoothly as the frame
    /// changes.
    Interpolated = 2u
};

/// Whether a value names one of the quantile rules this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rule the value to inspect.
/// @return true for every supported rule.
[[nodiscard]] constexpr bool isHistogramQuantileRule(HistogramQuantileRule rule) noexcept
{
    return rule == HistogramQuantileRule::LowerEdge || rule == HistogramQuantileRule::BinCenter ||
           rule == HistogramQuantileRule::Interpolated;
}

/// Counts one channel of an image known only at run time into caller-owned bins.
///
/// The source layout may be planar, interleaved or mixed and may contain row and plane padding. A
/// channel is counted in plane-major order across the image rather than assumed to be a plane. The
/// selection is stated exactly as it is for every operation on this surface: the image's validity
/// rule, the packed mask when that rule names one, and an optional rectangle read where it lies.
///
/// The bins are replaced, not accumulated into, and they are written only once every sample has been
/// counted into private per-chunk tables - so a call refused on its arguments, or stopped while
/// counting, leaves the caller's counters as they were. They must not overlap the source. A
/// non-empty region whose selection admits nothing is the one refusal that does clear them, because
/// an all-zero histogram is the truthful count of nothing; it is still reported as
/// `ErrorCode::EmptySelection` with both counts, as every operation on this surface reports one. An
/// empty region is refused before the bins are looked at.
///
/// All eight resident representations are read directly, and a not-a-number or an infinity is
/// counted beside the bins rather than refused. `HistogramDomain::RepresentationFullScale` asks for
/// an integer representation's own extremes, which for a sixteen-bit image and 65,536 bins puts each
/// code in a bin of its own, and for a floating one the normalized interval `[0, 1]`; asking for it
/// by name is what stops a caller writing 65535 into a call site and having that number outlive the
/// representation it belonged to.
///
/// Thread-safe with respect to the source and selection, which are only read. Calls using separate
/// execution contexts may run concurrently.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the resident representation the layout's element width describes.
/// @param samples first byte of the image storage. Must not be null and must be aligned for the
///        resident representation.
/// @param layout validated image geometry and strides.
/// @param channelIndex channel to count, below `layout.extent().channelCount()`.
/// @param selection validity, optional full-image mask and optional region.
/// @param request the interval and how it is decided.
/// @param bins one or more counters to replace, disjoint from the source and at most
///        `kMaxHistogramBins` of them.
/// @return the counts and the partition; `ErrorCode::EmptySelection` when the region is empty or
///         nothing participates; `ErrorCode::InvalidArgument` for an empty or overlapping bin span,
///         an unknown domain, a non-finite or non-increasing stated interval, a null or misaligned
///         source, an unknown representation, a layout whose element size is not its size, a
///         channel the layout does not have, a region outside the source, or a mask missing under
///         `ValidityRule::ExplicitMask` or supplied under another rule;
///         `ErrorCode::IncompatibleArguments` when the mask does not cover the image;
///         `ErrorCode::OutOfRange` for more than `kMaxHistogramBins` bins;
///         `ErrorCode::NotInitialized` for an execution that holds nothing; `ErrorCode::Cancelled`
///         or `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this build
///         has no dispatch table for; or `ErrorCode::MemoryBudgetExceeded` or
///         `ErrorCode::AllocationFailed` when the per-chunk counters are refused. A stop during the
///         final merge may leave some ranges of bins holding their counts and the rest as they were.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageHistogram>
computeImageHistogram(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                      const ImageLayout& layout, std::uint32_t channelIndex, const StatisticsSelection& selection,
                      const ImageHistogramRequest& request, std::span<std::uint64_t> bins);

/// Counts one channel of a checked typed image view into caller-owned bins.
///
/// The typed spelling of `computeImageHistogram`. It adds no interval, selection or counting rule:
/// the representation, sample pointer and layout are read from the view, and the untyped entry point
/// owns every check and computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param channelIndex channel to count.
/// @param selection validity, optional mask and optional region.
/// @param request the interval and how it is decided.
/// @param bins the counters to replace.
/// @return whatever `computeImageHistogram` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<ImageHistogram>
computeHistogram(const ExecutionContext& execution, const ImageView<const SampleT>& source, std::uint32_t channelIndex,
                 const StatisticsSelection& selection, const ImageHistogramRequest& request,
                 std::span<std::uint64_t> bins)
{
    return computeImageHistogram(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), channelIndex,
                                 selection, request, bins);
}

/// Turns bin counts into cumulative counts.
///
/// Entry `i` of the destination is the sum of source entries `0` through `i`, so the last entry is
/// the total that entered a bin. The two spans may be the same span, which accumulates in place;
/// they may not otherwise overlap, because a partial overlap would read counts this has already
/// replaced.
///
/// It is a serial prefix sum and is deliberately not parallel: a cumulative histogram is at most
/// `kMaxHistogramBins` entries, which is far below the size at which dividing the work would pay
/// for the second pass a parallel scan needs.
///
/// @param bins the counts to accumulate, at least one.
/// @param cumulative where the running totals go, exactly as long as `bins`.
/// @return success; `ErrorCode::InvalidArgument` for an empty span or a partial overlap;
///         `ErrorCode::IncompatibleArguments` when the two spans differ in length; or
///         `ErrorCode::InvalidData` when the total does not fit an unsigned sixty-four-bit count.
[[nodiscard]] AFTERNIGHTENGINE_API Status accumulateCumulativeCounts(std::span<const std::uint64_t> bins,
                                                                     std::span<std::uint64_t> cumulative);

/// The value at one quantile of a counted distribution, to the resolution of its bins.
///
/// The rank sought is `quantile * total`, where the total is the last cumulative entry, and the
/// bracketing bin is the first whose running total reaches it. `HistogramQuantileRule` then decides
/// which value inside that bin is reported. A quantile of zero is the interval's lower endpoint and
/// one is its upper endpoint under every rule, so the two ends of a distribution do not depend on a
/// convention.
///
/// The samples outside the interval are not part of the distribution this reads: a caller that
/// cares about them reads `belowRange` and `aboveRange` and decides what they mean, because whether
/// a saturated sample belongs in a percentile is a question about the frame rather than about the
/// arithmetic.
///
/// @param histogram the result that produced the counts, which supplies the interval and bin count.
/// @param cumulative the running totals from `accumulateCumulativeCounts`, as long as the histogram
///        has bins.
/// @param quantile the fraction, within `[0, 1]`.
/// @param rule which value inside the bracketing bin is reported.
/// @return the value; `ErrorCode::IncompatibleArguments` when the span does not match the
///         histogram's bin count; `ErrorCode::InvalidArgument` for an unknown rule or a histogram
///         whose interval is not finite and increasing; `ErrorCode::OutOfRange` for a quantile
///         outside `[0, 1]` or not a number; or `ErrorCode::EmptySelection` when no sample entered a
///         bin.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> histogramQuantileValue(const ImageHistogram& histogram,
                                                                         std::span<const std::uint64_t> cumulative,
                                                                         double quantile, HistogramQuantileRule rule);

/// What `computeImageHistogram` and `computeHistogram` do to the image they read.
///
/// Counting samples into bins writes the caller's bin array and no sample of the image, so this is
/// a reduction like the two statistics families, and it changes nothing about the image it counted.
inline constexpr OperationEffect kImageHistogramEffect{.samples = SampleEffect::Reads};
} // namespace an
