// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Histograms, exact order statistics, selection compaction and moving-window order filters.
///
/// Two tiers live here. `referenceHistogramKernel` and `referenceOrderStatisticKernel` are the scalar
/// oracles over one Float32 plane: they walk in one fixed order on the calling thread, and the order
/// statistic sorts a caller-owned copy of every selected sample. The plane primitives -
/// `accumulatePlaneHistogram`, `selectPlaneOrderStatistics`, `compactSelectedPlaneSamples` and their
/// `Over` forms - read any of the eight resident representations where they lie through a
/// `KernelPlaneSource`, divide the work into chunks derived from the work rather than from the thread
/// count, so a result is the same at every admitted width, and hold working memory bounded by the
/// ceilings below rather than by the sample count. `selectWindowOrderStatistic` is the filter that
/// median and other order filters are built from, and `selectFootprintOrderStatistic` the same filter
/// over the cells a structuring element marks.
///
/// Every entry point that reads a `KernelPlaneSource` shares these refusals:
/// `ErrorCode::NotInitialized` for an execution that holds nothing or has no memory resource;
/// `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` for a stopped one;
/// `ErrorCode::UnsupportedBackend` for an execution constrained to a backend this build has no
/// dispatch table for; `ErrorCode::InvalidArgument` for null samples or layout, an unknown sample
/// type, samples not aligned for their type, or a packed mask supplied under a rule that reads none
/// or missing under `ValidityRule::ExplicitMask`; `ErrorCode::IncompatibleArguments` for a layout whose
/// element width is not the sample type's, a plane or component the layout does not have, or a mask
/// that does not cover the source and its selected bit-plane; and `ErrorCode::MemoryBudgetExceeded`
/// or `ErrorCode::AllocationFailed` when the context refuses working memory. The entry points over a
/// Float32 `PlaneView` share the execution, cancellation and mask refusals, and refuse with
/// `ErrorCode::IncompatibleArguments` a plane with more than one sample per pixel or a destination
/// shaped unlike its source.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// The closed numeric interval divided by a histogram's caller-owned bins.
struct HistogramKernelParameters
{
    /// Inclusive smallest value in the first bin. Must be finite.
    double lowest = 0.0;

    /// Inclusive largest value in the last bin. Must be finite and greater than `lowest`.
    double highest = 1.0;
};

/// Result of the scalar histogram pass.
struct HistogramKernelResult
{
    /// Selection and bin-write counts. `outputsWritten` is the number placed in bins.
    KernelRunResult run{};

    /// Valid samples strictly below the histogram interval, including negative infinity.
    std::uint64_t belowRange = 0u;

    /// Valid samples strictly above the histogram interval, including positive infinity.
    std::uint64_t aboveRange = 0u;

    /// Valid not-a-number samples, which have no ordered histogram interval and enter no bin.
    std::uint64_t notANumber = 0u;
};

/// Result of one exact order-statistic selection.
struct OrderStatisticKernelResult
{
    /// Selection and scalar-write counts.
    KernelRunResult run{};

    /// Value at the requested zero-based rank after the stable total Float32 order. Numbers precede
    /// NaNs; numerically equal values use their representation bits as a tie-breaker.
    float value = 0.0f;

    /// Number of caller scratch elements populated and sorted.
    std::uint64_t scratchUsed = 0u;
};

/// Builds an exact bounded-domain histogram in caller-owned bins.
///
/// Bins must be disjoint from the source and are cleared after all arguments and the initial
/// cancellation state have been validated. The lowest and highest endpoints are included; equally
/// sized bins cover the interval, with the highest endpoint forced into the final bin. Invalid
/// samples are skipped. Valid infinities count below or above the interval and valid NaNs count
/// separately rather than entering a bin.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample input plane.
/// @param bins one or more counters to replace.
/// @param parameters the finite ordered interval.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the counts; `ErrorCode::EmptySelection` when no sample participates;
///         `ErrorCode::InvalidArgument` for an empty bin span, bins that overlap the source, or an
///         interval that is not finite and increasing; or a shared `PlaneView` refusal named in the
///         file comment. A stop observed after the bins are cleared leaves the rows before it counted
///         in them.
[[nodiscard]] AFTERNIGHTENGINE_API Result<HistogramKernelResult>
referenceHistogramKernel(const ExecutionContext& execution, PlaneView<const float> source,
                         std::span<std::uint64_t> bins, const HistogramKernelParameters& parameters,
                         const KernelValidityView& validity);

/// Selects one zero-based rank from the exact ascending scalar order.
///
/// The caller supplies at least one scratch element per logical input, disjoint from the source,
/// which bounds memory before scanning even when most inputs are invalid. The populated prefix is
/// sorted in place and remains available to the caller after success. Cancellation may leave
/// scratch partially populated or partially sorted; no scalar result is then produced.
///
/// The sort is an insertion sort under the total order, so the oracle's cost grows with the square
/// of the selected count; a checkpoint every 256 moves keeps a long sort stoppable.
/// `selectPlaneOrderStatistics` is the bounded-memory selection for real images.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample input plane.
/// @param rank zero-based rank among selected samples.
/// @param scratch caller-owned working elements, at least `source.sampleCount()` long.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the selected value and counts; `ErrorCode::IncompatibleArguments` when scratch is too
///         small; `ErrorCode::InvalidArgument` when scratch overlaps the source;
///         `ErrorCode::EmptySelection` when none participates; `ErrorCode::OutOfRange` when rank is
///         not below the selected count; or a shared `PlaneView` refusal named in the file comment.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OrderStatisticKernelResult>
referenceOrderStatisticKernel(const ExecutionContext& execution, PlaneView<const float> source, std::uint64_t rank,
                              std::span<float> scratch, const KernelValidityView& validity);

/// The most bins one histogram may divide its interval into.
///
/// A ceiling rather than a suggestion: the bins are the operation's private working memory once per
/// chunk, so an unbounded bin count is an unbounded allocation. Sixty-five thousand five hundred
/// and thirty-six is the whole of a sixteen-bit code space.
inline constexpr std::uint32_t kMaxHistogramBins = 65536u;

/// The context memory one histogram may hold for its per-chunk private counters.
///
/// The bin contract and this ceiling together decide the partition: a histogram of many bins is
/// walked by fewer chunks than one of few bins, so the working set stays bounded whatever the
/// caller asks for and whatever the machine offers. It is stated here rather than derived inside
/// the implementation because a caller reasoning about a memory budget has to be able to read it.
///
/// Two mebibytes, which is a measurement rather than a round number. Private counters are what let
/// the count be exact and reproducible without an atomic in the inner loop, and their cost is
/// `bins * chunks * 8` bytes written before the pass and read after it - which for a wide histogram
/// is several times the frame being counted. On a million sixteen-bit samples counted into the
/// whole code space, this ceiling gives three tables and 1.65 ms; four mebibytes gives seven and
/// 1.83 ms, and eight gives fifteen and 2.49 ms. Fewer, larger chunks win because a wide histogram
/// is bound by that traffic rather than by how many workers walk it, and a narrow one is unaffected
/// because its partition is decided by the work long before it reaches this ceiling.
inline constexpr std::uint64_t kMaxHistogramPartialBytes = 2u * 1024u * 1024u;

/// The most quantiles one selection may be asked for at once.
inline constexpr std::uint32_t kMaxOrderStatisticRanks = 16u;

/// The most samples a selection gathers before it resolves a rank by sorting them.
///
/// A narrowing pass reduces a rank to a bracket of values; once a bracket holds no more than this,
/// gathering it is cheaper than another pass. It is what bounds the gather buffer, and it is the
/// reason the primitive never holds one element per input sample.
inline constexpr std::uint32_t kMaxOrderGatherSamples = 4096u;

/// The most samples one order-statistic window may cover.
///
/// The window ceiling is stated and validated before execution rather than implied by the size of
/// a fixed array inside an implementation, which is what an implicit radius limit becomes the first
/// time a caller exceeds it. A caller wanting a wider window states a wider contract; it does not
/// discover this one by producing a wrong pixel.
inline constexpr std::uint32_t kMaxOrderWindowSamples = 1024u;

/// Where a histogram's closed interval comes from.
enum class HistogramDomain : std::uint8_t
{
    /// The caller states `lowest` and `highest`.
    Stated = 0u,

    /// The full scale of the source representation.
    ///
    /// An integer representation contributes its own smallest and largest values; a floating one
    /// contributes the normalized linear domain, which is the meaning integer full scale maps onto.
    /// Asking for it by name is what stops a caller writing 65535 into a call site and having that
    /// number outlive the representation it belonged to.
    RepresentationFullScale = 1u
};

/// The bin and domain contract of one histogram pass.
///
/// The bin count is the caller's span rather than a field here: the counters are caller-owned, so
/// how many there are is a property of the memory rather than of the request.
struct PlaneHistogramRequest
{
    /// Which of the two ways the interval is decided.
    HistogramDomain domain = HistogramDomain::Stated;

    /// Inclusive smallest value in the first bin under `HistogramDomain::Stated`. Must be finite.
    double lowest = 0.0;

    /// Inclusive largest value in the last bin under `HistogramDomain::Stated`. Must be finite and
    /// greater than `lowest`.
    double highest = 1.0;
};

/// What one histogram pass counted, and the partition it counted with.
struct PlaneHistogramResult
{
    /// Sample selection and bin writes. `outputsWritten` counts the samples placed in a bin.
    KernelRunResult run{};

    /// Chunks the plane was divided into, derived from the work and the bin count alone.
    std::uint64_t deterministicChunks = 0u;

    /// Workers the operation could actually use: the smaller of admitted concurrency and chunks.
    std::uint32_t plannedWorkers = 0u;

    /// Context memory held for the per-chunk counters, never above `kMaxHistogramPartialBytes`.
    std::uint64_t partialBytes = 0u;

    /// The resolved inclusive lower endpoint, which is the request's under `Stated`.
    double lowest = 0.0;

    /// The resolved inclusive upper endpoint.
    double highest = 0.0;

    /// Valid samples below the interval, negative infinity included.
    std::uint64_t belowRange = 0u;

    /// Valid samples above the interval, positive infinity included.
    std::uint64_t aboveRange = 0u;

    /// Valid not-a-number samples, which have no ordered interval and enter no bin.
    std::uint64_t notANumber = 0u;
};

/// How a quantile falling between two ranks is produced.
///
/// The position of quantile `q` among `n` selected samples is `q * (n - 1)`, so `0` and `1` name the
/// smallest and largest sample under every rule and the rules differ only in between.
enum class QuantileInterpolation : std::uint8_t
{
    /// The sample at the lower of the two bracketing ranks.
    LowerRank = 0u,

    /// The sample at the nearer rank; an exact half takes the higher one.
    NearestRank = 1u,

    /// Linear interpolation between the two bracketing samples.
    Linear = 2u
};

/// Whether a value names one of the quantile interpolation rules this build implements.
///
/// @param interpolation the value to inspect.
/// @return true for every supported interpolation rule.
[[nodiscard]] constexpr bool isQuantileInterpolation(QuantileInterpolation interpolation) noexcept
{
    return interpolation == QuantileInterpolation::LowerRank || interpolation == QuantileInterpolation::NearestRank ||
           interpolation == QuantileInterpolation::Linear;
}

/// What one selection is asked to produce beyond its quantiles.
struct PlaneOrderRequest
{
    /// Which rule resolves a quantile between two ranks.
    QuantileInterpolation interpolation = QuantileInterpolation::Linear;

    /// Whether to make the second descent that produces the median absolute deviation.
    ///
    /// It costs a further set of passes over the plane, so it is asked for rather than always
    /// produced; the median it is taken around is the one reported beside it.
    bool medianAbsoluteDeviation = false;

    /// The closed interval every admitted sample is read as held to, or nothing to read each sample as it is stored.
    ///
    /// A hold admits no sample and turns none away: the validity rule and the selection's value interval decide on
    /// the stored value, and an admitted sample is then ordered as the nearer end of the hold when it lies outside it.
    /// The extremes, every quantile, the median and the deviation are those of the held values, so a caller that
    /// measures what a display shows - every sample held to the unit interval - measures the stored samples where they
    /// lie rather than a copy of them held first. Both endpoints must be finite and ordered.
    std::optional<KernelValueWindow> hold{};
};

/// The exact order statistics of one selected image component.
struct PlaneOrderStatistics
{
    /// Sample selection and scalar writes.
    KernelRunResult run{};

    /// Chunks the plane was divided into, independent of the thread count.
    std::uint64_t deterministicChunks = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// How many times the selection read the plane, which is what its cost is made of.
    std::uint64_t planeScans = 0u;

    /// The most context memory held at once, which never scales with the sample count.
    std::uint64_t peakScratchBytes = 0u;

    /// Smallest selected sample.
    double minimum = 0.0;

    /// Largest selected sample.
    double maximum = 0.0;

    /// The quantile at one half, under the requested interpolation.
    double median = 0.0;

    /// The median of the absolute deviations from `median`, or a quiet NaN when not requested.
    double medianAbsoluteDeviation = 0.0;
};

/// What one windowed order statistic is asked for.
struct OrderWindowRequest
{
    /// Half the window width. The window is `2 * radiusX + 1` samples across.
    std::uint32_t radiusX = 1u;

    /// Half the window height. The window is `2 * radiusY + 1` samples down.
    std::uint32_t radiusY = 1u;

    /// Which quantile of the window to produce. One half is the median. Must be within `[0, 1]`.
    double quantile = 0.5;

    /// Which rule resolves a quantile between two ranks of the window.
    QuantileInterpolation interpolation = QuantileInterpolation::Linear;

    /// How a window sample outside the source footprint is supplied.
    ///
    /// `NotApplicable` omits an outside sample and therefore shrinks a border window. Every other mode
    /// supplies a full window.
    KernelBorderMode border = KernelBorderMode::Clamp;

    /// The window sample used outside the footprint under `KernelBorderMode::Constant`.
    float constantBorder = 0.0f;
};

/// What one windowed order statistic produced, and the memory it needed to do it.
struct OrderWindowResult
{
    /// Pixel selection and writes. An invalid center leaves its output untouched.
    KernelRunResult run{};

    /// The validated window size, never above `kMaxOrderWindowSamples`.
    std::uint32_t windowSamples = 0u;

    /// Chunks the rows were divided into.
    std::uint64_t deterministicChunks = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;

    /// Context memory held for the per-chunk window buffers. One buffer per chunk, never per pixel.
    std::uint64_t scratchBytes = 0u;
};

/// Counts one stored image component into caller-owned bins.
///
/// All eight resident representations are read directly from a planar, interleaved, or mixed
/// layout. Every sample is first counted into per-chunk private counters, and each bin is then
/// replaced by the sum of its column across the chunk tables. Counts are exact integers, so the
/// order of that sum cannot change a bin: one input produces identical counts at every admitted
/// thread count, and the chunk count is a function of the work and the bin count rather than of the
/// machine. An eight- or sixteen-bit integer component holding at least as many samples as its
/// representation has codes is counted per code instead, and each code then classified once, which
/// gives the same counts.
///
/// The interval is closed at both ends. Equally sized bins cover it and the high endpoint belongs
/// to the last bin rather than to one past the end, which is the difference between this and a
/// mapping that reserves its final bin for a single value. A valid sample below or above the
/// interval, an infinity included, is counted separately and enters no bin; a valid not-a-number
/// has no ordered position and is counted separately again.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the resident image component to read.
/// @param bins one or more counters to replace, disjoint from the source.
/// @param request the bin and domain contract.
/// @param validity the value rule and optional packed mask applied to the selected component.
/// @return the counts and the partition, which is a success even when no sample participates;
///         `ErrorCode::InvalidArgument` for an empty or overlapping bin span, an unknown domain, or a
///         non-finite or non-increasing stated interval; `ErrorCode::OutOfRange` for more than
///         `kMaxHistogramBins` bins; or a shared source refusal named in the file comment. The bins
///         are written only once counting has finished, so a stop observed while counting leaves
///         them untouched, and one observed while they are being replaced can leave some replaced
///         and the rest as they were.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlaneHistogramResult>
accumulatePlaneHistogram(const ExecutionContext& execution, const KernelPlaneSource& source,
                         std::span<std::uint64_t> bins, const PlaneHistogramRequest& request,
                         const KernelValidityView& validity);

/// Selects exact quantiles, the median, and optionally the median absolute deviation.
///
/// The answer is exact - the sample an ascending sort would place at the rank - and it is produced
/// without ever holding one working element per input sample. Every representation is widened once
/// to Float64 and ordered by the Float64 order-preserving key, so the selection is defined on one
/// processing domain rather than on eight; a boundary representation is therefore converted once at
/// the read rather than compared in its own type.
///
/// Narrowing is what keeps the memory bounded. A pass counts selected samples into a bounded
/// histogram over the interval they are known to lie in, which fixes the bracket the requested rank
/// falls in; a second pass gathers exactly that bracket into a bounded buffer and sorts it. Where a
/// rank and the one above it are both wanted, one linear interpolation needs both, the two are
/// adjacent and one gather almost always answers them together. A dense interior bracket of a
/// contiguous all-valid plane can be subdivided once more using the same bounded storage. Large
/// contiguous planes use wider histograms; the complete selection workspace, including the gather
/// buffer, remains within `kMaxHistogramPartialBytes`. When
/// narrowing cannot resolve a rank, including a repeated-value bracket larger than
/// `kMaxOrderGatherSamples`, selection falls back to fixing the key one eight-bit digit at a time.
/// This terminates once every bit is fixed. An eight- or sixteen-bit integer plane holding at least
/// as many samples as its representation has codes - 256 or 65,536 - is instead counted into a
/// table of how often each code occurs, within the same ceiling, and every rank, the median and the
/// deviation are read from that table: the same exact values from one read of the plane. A median with
/// its deviation and no other quantile, over a Float32 component of 1,048,576 samples or more every one
/// of which is selected but for a value window, is read twice instead of five times: one read counts
/// the median's bracket and the two brackets the deviation's rank lies in - one below the median and one
/// above it, where a sample of the plane puts them - together with the extremes, and one gathers the
/// median's bins and every sample whose deviation could be the deviation's rank. The answers are the
/// same exact values, and a plane those reads cannot settle - repeated values, brackets the ranks miss,
/// more candidates than the gather holds - is narrowed as above, the reads taken counted with its own.
/// `planeScans` reports the actual number of counting, narrowing, gathering and fallback passes.
///
/// The reported quantile values are written into `values`, which must be exactly as long as
/// `quantiles` and disjoint from both that span and the source samples. `median` is the quantile at
/// one half whether or not it was asked for, because the median absolute deviation is defined
/// around it. A selected non-finite sample is refused; use a validity rule such as `FiniteOnly`
/// when such stored values are no-data. Under a hold every admitted sample is ordered as held to
/// the request's interval, and the extremes, the quantiles, the median and the deviation are those
/// of the held values.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the resident image component to read.
/// @param quantiles the fractions to select, each within `[0, 1]`, at most
///        `kMaxOrderStatisticRanks` of them. May be empty.
/// @param values where the selected values go, exactly as long as `quantiles` and disjoint from
///        both the quantiles and source storage.
/// @param request the interpolation rule, whether the deviation is wanted, and the optional hold.
/// @param validity the value rule and optional packed mask applied to the selected component.
/// @return the statistics and the partition; `ErrorCode::IncompatibleArguments` when `values` is
///         not as long as `quantiles`; `ErrorCode::InvalidArgument` for an unknown interpolation,
///         overlapping writable output, or a hold that is not a finite ordered closed interval;
///         `ErrorCode::InvalidData` for a selected non-finite sample;
///         `ErrorCode::OutOfRange` for too many quantiles or one outside `[0, 1]`;
///         `ErrorCode::EmptySelection` when nothing participates; or a shared source refusal named
///         in the file comment.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlaneOrderStatistics>
selectPlaneOrderStatistics(const ExecutionContext& execution, const KernelPlaneSource& source,
                           std::span<const double> quantiles, std::span<double> values,
                           const PlaneOrderRequest& request, const KernelValidityView& validity);

/// Counts one stored image component over a rectangle and an optional interval of its values.
///
/// The same bin, domain, determinism and failure contract as `accumulatePlaneHistogram`, asked of
/// part of an image rather than all of it. The rectangle keeps the source row and plane strides and
/// an explicit packed mask keeps the complete source extent and is addressed in source-image
/// coordinates, so neither a cropped copy nor a cropped mask is built.
///
/// A value interval narrows the selection further and composes with the validity rule: a sample
/// participates when the rule admits it and the interval contains it. It is distinct from the
/// histogram interval, which decides which bin a participating sample lands in and counts what falls
/// outside it separately - so a caller can count the samples of one interval into the bins of
/// another, and `belowRange` and `aboveRange` stay about the bins.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the complete resident image component to read.
/// @param bins one or more counters to replace, disjoint from the source.
/// @param request the bin and domain contract.
/// @param validity the value rule and optional complete-source packed mask.
/// @param selection the contained rectangle and optional value interval; the rectangle may be empty.
/// @return the counts and the partition, which is a success when a non-empty rectangle has no
///         sample the rule and the interval admit; or the failures documented by
///         `accumulatePlaneHistogram`, plus `ErrorCode::EmptySelection` for a rectangle with no
///         width or no height, and `ErrorCode::InvalidArgument` for a rectangle outside the source or
///         a non-finite or inverted interval.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlaneHistogramResult>
accumulatePlaneHistogramOver(const ExecutionContext& execution, const KernelPlaneSource& source,
                             std::span<std::uint64_t> bins, const PlaneHistogramRequest& request,
                             const KernelValidityView& validity, const KernelPlaneSelection& selection);

/// Selects exact quantiles over one rectangle of a source and one interval of its values.
///
/// The same operation, numeric contract, bounded scratch and failures as
/// `selectPlaneOrderStatistics`, asked of part of an image rather than all of it. The rectangle
/// keeps the source row and plane strides and an explicit packed mask keeps the complete source
/// extent and is addressed in source-image coordinates, so neither a cropped copy nor a cropped
/// mask is built; a rectangle whose horizontal origin is not a multiple of eight is ordinary.
///
/// The optional value interval is what a clipped estimator descends with. It composes with the
/// validity rule rather than replacing it - a sample participates when the rule admits it and the
/// interval contains it - so an iteration that narrows the interval around a location re-ranks the
/// retained samples without ever materializing them. `run.selection.total` stays the number of
/// pixels the rectangle covers, so a retained fraction remains readable from the result.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the complete resident image component to read.
/// @param quantiles the fractions to select, each within `[0, 1]`, at most
///        `kMaxOrderStatisticRanks` of them. May be empty.
/// @param values where the selected values go, exactly as long as `quantiles` and disjoint from
///        both the quantiles and source storage.
/// @param request the interpolation rule and whether the deviation is wanted.
/// @param validity the value rule and optional complete-source packed mask.
/// @param selection the contained rectangle and optional value interval; the rectangle may be empty.
/// @return the statistics and the partition; or the failures documented by
///         `selectPlaneOrderStatistics`, including `ErrorCode::EmptySelection` for an empty
///         rectangle or an interval nothing falls in, and `ErrorCode::InvalidArgument` for a
///         rectangle outside the source or a non-finite or inverted interval.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlaneOrderStatistics>
selectPlaneOrderStatisticsOver(const ExecutionContext& execution, const KernelPlaneSource& source,
                               std::span<const double> quantiles, std::span<double> values,
                               const PlaneOrderRequest& request, const KernelValidityView& validity,
                               const KernelPlaneSelection& selection);

/// What one compaction pass considered, and what it wrote.
struct PlaneCompactionResult
{
    /// Sample selection and writes. `selection.valid` is how many samples the rules admitted,
    /// `selection.total` how many pixels the rectangle covered, and `outputsWritten` how many
    /// values were placed in the destination, which is zero for a counting call.
    KernelRunResult run{};

    /// Chunks the rectangle was divided into, independent of the thread count.
    std::uint64_t deterministicChunks = 0u;

    /// Workers the operation could actually use: the smaller of admitted concurrency and chunks.
    std::uint32_t plannedWorkers = 0u;

    /// Smallest admitted sample, or positive infinity when none was admitted.
    double minimum = 0.0;

    /// Largest admitted sample, or negative infinity when none was admitted.
    double maximum = 0.0;
};

/// Counts, or gathers, the samples one selection admits into contiguous Float64 storage.
///
/// The engine reads an image where it lies, and this is the one operation that says otherwise. It
/// exists because an estimator that reads its selection many times - a rank, then a deviation
/// around it, then an iteration that narrows around that - pays the source strides and the validity
/// rule on every one of those reads, and a caller that knows its selection is small can decide to
/// pay them once instead. Which of the two is faster is a property of the shape rather than of the
/// operation, so this is a building block a caller chooses rather than a step it always takes.
///
/// The destination is caller-owned, so what compaction costs is a number the caller allocated and
/// can refuse rather than a hidden allocation proportional to the image. An empty destination is
/// the counting call: nothing is written and the count comes back, which is how a caller sizes the
/// buffer before it commits to one. Samples are written in row-major source order.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the complete resident image component to read.
/// @param validity the value rule and optional complete-source packed mask.
/// @param selection the contained rectangle and optional value interval; the rectangle may be empty.
/// @param destination where the admitted samples go, disjoint from the source, or empty to count
///        without writing.
/// @return the counts and extremes; `ErrorCode::IncompatibleArguments` when a non-empty destination
///         is shorter than the admitted count, carrying the required and available element counts;
///         `ErrorCode::InvalidArgument` when the destination overlaps the source, the rectangle
///         lies outside it, or the interval is not a finite ordered one;
///         `ErrorCode::InvalidData` for an admitted non-finite sample; `ErrorCode::EmptySelection`
///         when nothing participates; or a shared source refusal named in the file comment.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlaneCompactionResult>
compactSelectedPlaneSamples(const ExecutionContext& execution, const KernelPlaneSource& source,
                            const KernelValidityView& validity, const KernelPlaneSelection& selection,
                            std::span<double> destination);

/// Produces one order statistic of a moving window over a Float32 plane.
///
/// The building block the median and other order filters are made of, with the window stated and
/// validated before any pixel is read. Source and destination must be disjoint. A window sample
/// outside the footprint follows the border rule; an invalid window sample is excluded from the
/// window rather than substituted, because an order statistic has no substitute value that does not
/// move the answer. A selected non-finite source sample is refused; use `FiniteOnly` when those
/// values are no-data. An invalid center produces no output at all and leaves its destination sample
/// untouched, so a center is always a member of its own window and the window is never empty.
///
/// Working memory is one window buffer per chunk of rows, taken once from the engine context, and
/// therefore never one allocation per output pixel.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the equally shaped output plane, disjoint from the source.
/// @param request the window, quantile, interpolation, and border contract.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the counts and the window plan; `ErrorCode::OutOfRange` when the window exceeds
///         `kMaxOrderWindowSamples` or the quantile is outside `[0, 1]`;
///         `ErrorCode::InvalidArgument` for an unknown interpolation, an unknown border, a
///         non-finite constant border, or overlapping buffers; `ErrorCode::InvalidData` for a
///         selected non-finite sample; `ErrorCode::NotInitialized`,
///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when the context has
///         no memory resource or refuses the window buffers; or a shared `PlaneView` refusal named
///         in the file comment.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OrderWindowResult>
selectWindowOrderStatistic(const ExecutionContext& execution, PlaneView<const float> source,
                           PlaneView<float> destination, const OrderWindowRequest& request,
                           const KernelValidityView& validity);

/// Produces one order statistic of a moving structuring element over a Float32 plane.
///
/// `selectWindowOrderStatistic` over the cells of the window the footprint marks rather than over the
/// whole rectangle: the rank a morphological selection takes, whose window is the shape a structuring
/// element draws. Every rule but the window's membership is that function's - the border, the
/// exclusion of an invalid sample, the refusal of a selected non-finite one, the untouched
/// destination of an invalid center and the working memory. A footprint need not mark its center,
/// so a center can have an empty window: every marked cell outside the plane under
/// `KernelBorderMode::NotApplicable`, or rejected by the validity rule. Such a center produces no
/// output and leaves its destination sample untouched, as an invalid one does.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the equally shaped output plane, disjoint from the source.
/// @param request the window's extent, the quantile, interpolation, and border contract.
/// @param footprint `(2 * request.radiusX + 1) * (2 * request.radiusY + 1)` bytes in row-major order,
///        the window's top row first, a nonzero byte marking a member; at least one is nonzero.
///        Borrowed for the call.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the counts and the window plan, whose `windowSamples` is the number of marked cells;
///         `ErrorCode::IncompatibleArguments` for a footprint whose length is not the window's;
///         `ErrorCode::InvalidArgument` for a footprint that marks no cell; or every failure
///         `selectWindowOrderStatistic` documents.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OrderWindowResult>
selectFootprintOrderStatistic(const ExecutionContext& execution, PlaneView<const float> source,
                              PlaneView<float> destination, const OrderWindowRequest& request,
                              std::span<const std::uint8_t> footprint, const KernelValidityView& validity);

} // namespace an
