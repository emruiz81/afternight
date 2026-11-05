// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The whole-output accumulators of a streaming stack, updated one frame at a
/// time and turned into the result once.
///
/// A stack whose combination is a mean, a sum or an extremum never holds more than one frame's
/// samples at once: the plan calls it `StackAccumulationStrategy::WholeOutputAccumulators`, the
/// working set takes one term per output element for each kept accumulator, and this header is the
/// arithmetic that writes into those terms. Two operations. `accumulateStackFrame` takes one frame's
/// samples - the whole frame, or one band of its rows - and updates every kept term under the
/// model's rules, and `accumulateStackFrames` does the same for every frame's band of one tile in
/// a single pass; `finalizeStackAccumulators` turns the terms into the output image, its validity
/// mask and each requested map. Neither allocates, neither reads a frame's bytes twice, and neither
/// decides anything the model and the plan did not already decide.
///
/// Four policies are stated here rather than left to the code.
///
/// **Precision.** Every term is Float64, which is the model's `accumulationPrecision`; the samples
/// arrive at the plan's tile width - Float32 for the seven representations up to thirty-two bits,
/// Float64 for Float64 storage - and are widened on read. A Float32 output accumulated in Float32
/// would lose a long stack's low bits, so the output width is a decision about what is written and
/// nothing else, and the narrowing happens once, in the finalization.
///
/// **Weight.** A sample contributes with the weight of its frame and channel from the stack's
/// per-frame table, times the local surface's weight at its pixel when the stack applies one. A
/// sample contributes exactly when that effective weight is positive and finite; a zero weight - a
/// frame the weighting excluded, a pixel the surface gave nothing - excludes the sample from every
/// term, the count included, so that a coverage map counts what contributed. Weights are not
/// normalized before they are applied: a weighted mean divides by the weight sum, so no scale is
/// assumed and none is applied.
///
/// **Validity.** The frame's own rule decides which samples are admitted, and a mask may have one
/// bit-plane shared by every channel or one per channel. A sample the rule admits and that is not a
/// finite number is refused as `ErrorCode::InvalidData`: the frame claimed something its bytes did
/// not keep, and an accumulator that quietly absorbed it would print the claim into every pixel.
///
/// **Overflow and determinism.** Counts are thirty-two bits because the model bounds a stack at
/// `kMaxStackFrames` frames, which this header proves at compile time; a Float64 term that leaves the
/// finite range is refused rather than carried. Each output element is written by exactly one
/// worker per frame and the frames arrive in the executor's order, so there is no merge across
/// threads at all: the same input produces the same bits at every admitted thread count and on
/// every compiled backend, and the differential tests hold the compiled rows to the scalar ones bit
/// for bit rather than to a tolerance.
///
/// Neither the extrema nor the mean is clamped to the unit interval, because a floating working
/// image keeps legitimate values outside it and a clamp is a conversion the caller states when it
/// narrows; and the variance counts only the samples that contributed.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace an
{

class StackModel;

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included: both operations borrow one for the duration of a call, and a
/// caller reaching them holds an execution context and has therefore already included that header.
class ExecutionContext;

// The counts are thirty-two bits wide, so the frame ceiling has to fit them with room to spare.
static_assert(kMaxStackFrames < (std::numeric_limits<std::uint32_t>::max)(),
              "a stack admits more frames than a thirty-two-bit count can hold");

/// How one admitted sample changes the running term of its output element.
///
/// The one fact about a combination the accumulator needs. It follows from the model and nothing
/// else, and `stackAccumulatorUpdateOf` is the constant expression that derives it.
enum class StackAccumulatorUpdate : std::uint8_t
{
    /// The sample times its effective weight is added to the running sum. What a mean and a sum
    /// accumulate; a mean divides by the weight sum in the finalization and a sum does not.
    WeightedSum,

    /// The running term becomes the smaller of itself and the sample; the first sample of an
    /// element replaces the term rather than comparing with it.
    Minimum,

    /// The running term becomes the larger of itself and the sample, with the same first-sample
    /// rule.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Maximum
};

/// Whether a value names an update this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param update the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackAccumulatorUpdate(StackAccumulatorUpdate update) noexcept
{
    return static_cast<std::uint8_t>(update) <= static_cast<std::uint8_t>(StackAccumulatorUpdate::Maximum);
}

/// The update's own name.
///
/// Thread-safe and allocation-free.
///
/// @param update the update to name. A value outside the enumeration is named
///        `"UnknownStackAccumulatorUpdate"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackAccumulatorUpdateName(StackAccumulatorUpdate update) noexcept;

/// The whole-output terms of one stack, borrowed as the working set hands them out.
///
/// Every span is one term over every output element in the output's packed planar order - channel
/// by channel, row by row - and a span is empty exactly when the stack does not keep that term, as
/// `stackAccumulatorKept` says. The variance moments hold two values per element, the running mean
/// first and the running sum of squared deviations second. `checkStackAccumulatorSet` is the rule
/// both operations apply before they read or write anything.
struct StackAccumulatorSet
{
    /// The running sum, or the running extremum, one per element.
    std::span<double> sums{};

    /// How many samples have contributed, one per element.
    std::span<std::uint32_t> counts{};

    /// The running sum of the effective weights, one per element, when the stack weighs its frames
    /// or applies a local surface.
    std::span<double> weightSums{};

    /// The running sum of the squared effective weights, one per element, when the stack writes a
    /// signal-to-noise gain map.
    std::span<double> weightSquareSums{};

    /// The running mean and the running sum of squared deviations, two per element, when the stack
    /// writes a variance map.
    std::span<double> varianceMoments{};
};

/// Checks that a set holds exactly the terms a model keeps, each over the model's output elements.
///
/// A kept term must be present at one entry per output element - two for the variance moments -
/// and begin on its own element's boundary; a term the model does not keep must be empty, because
/// a caller that hands one over believes it will be read and it will not be.
///
/// Thread-safe, reentrant, and allocation-free except for a failure it has to describe.
///
/// @param model the checked stack the set belongs to.
/// @param set the terms.
/// @return the number of output elements every term covers; `ErrorCode::IncompatibleArguments`
///         when the model holds sample history rather than whole-output accumulators, or when a
///         term is missing, offered, of another length or misaligned, naming the term.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint64_t> checkStackAccumulatorSet(const StackModel& model,
                                                                                  const StackAccumulatorSet& set);

/// Which of a band's samples mean something.
///
/// The frame's own claim, travelling beside its bytes as it does everywhere else in this engine.
/// A mask is required exactly when the rule is `ValidityRule::ExplicitMask`; it covers the band's
/// extent, and it has either one bit-plane shared by every channel or one bit-plane per channel.
struct StackBandValidity
{
    /// The rule.
    ValidityDescriptor descriptor{};

    /// The packed bits, borrowed for the call, when the rule needs them; null otherwise.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;
};

/// One frame's samples for one band of output rows, borrowed for the call.
///
/// The band is the unit the plan's tile names: full-width rows of one frame, at the tile width the
/// plan chose. Its layout may be planar, interleaved or mixed - channel `c` is read from plane
/// `c / samplesPerPixel` at component `c % samplesPerPixel` - and a planar layout is the one the
/// compiled row runs over.
struct StackFrameBand
{
    /// What the samples are. Must be the plan's tile width for the model's input:
    /// `SampleType::Float64` for Float64 storage and `SampleType::Float32` for every other
    /// representation.
    SampleType sampleType = SampleType::Float32;

    /// The first byte of the band's samples, never null.
    const void* samples = nullptr;

    /// Where the samples sit: an extent of the output's width, the band's rows and the output's
    /// channel count, at the element width of `sampleType`. Never null.
    const ImageLayout* layout = nullptr;

    /// Which frame of the stack this band belongs to, below the model's frame count. It selects the
    /// frame's row of the per-frame weight table.
    std::uint32_t frameIndex = 0u;

    /// The output row the band's first row lands on. The band must end at or before the output's
    /// last row.
    std::uint32_t firstOutputRow = 0u;

    /// Which of the band's samples mean something.
    StackBandValidity validity{};

    /// The local surface's weight at each of the band's pixels, row-major and shared by every
    /// channel, when the stack applies a local weighting; empty otherwise.
    std::span<const float> localWeights{};
};

/// Checks one frame's band against the model and the output it lands on.
///
/// The rule `accumulateStackFrame` applies before it reads a sample, published so that an operation
/// that takes every frame's band at once - the sample-history combinations - holds each band to the
/// same rule: the plan's tile width, a layout of the output's width and channels addressing every
/// channel exactly once, a frame index below the frame count, rows inside the output, a mask exactly
/// when the rule needs one and covering the band with one shared bit-plane or one per channel, and
/// local weights exactly when the model applies a surface.
///
/// Thread-safe, reentrant, and allocation-free except for a failure it has to describe.
///
/// @param model the checked stack.
/// @param band the band.
/// @return success when the band describes one frame of this stack; otherwise the refusal
///         `accumulateStackFrame` documents for it, under this function's name.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkStackFrameBand(const StackModel& model, const StackFrameBand& band);

/// What one frame's accumulation did.
struct StackAccumulationOutcome
{
    /// How many samples contributed to a term.
    std::uint64_t samplesAccumulated = 0u;

    /// How many samples the band's validity rule kept out.
    std::uint64_t samplesExcludedByValidity = 0u;

    /// How many admitted samples an effective weight that was not positive and finite kept out.
    std::uint64_t samplesExcludedByWeight = 0u;

    /// How many output elements the band covered, whether or not each received a sample.
    std::uint64_t elementsCovered = 0u;

    /// Which implementation the dispatch table selected for the execution.
    BackendSelection backend{};

    /// Whether a compiled row ran for at least one run of samples. False on the reference backend,
    /// on a build with no compiled row for the selected instruction set, and for a band whose
    /// layout is not planar.
    bool compiledRowUsed = false;

    /// Whether two outcomes say the same thing.
    ///
    /// @param left one outcome.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackAccumulationOutcome& left,
                                                   const StackAccumulationOutcome& right) noexcept = default;
};

/// Adds one frame's band to the running terms of its output elements.
///
/// For every sample of the band the rule admits, the effective weight is the frame's weight for
/// its channel times the local surface's weight at its pixel, or one when the stack weighs nothing;
/// a sample whose effective weight is positive and finite is accumulated under
/// `stackAccumulatorUpdateOf` into the sum or the extremum, counted, and added to the weight sum,
/// the squared weight sum and the variance moments when the stack keeps them. The frame's weights
/// are read from `frameWeights` only when the model weighs its frames; the local weights are read
/// only when the model applies a local surface, and are required then.
///
/// The band's rows and channels are spread across the execution's admitted workers, each output
/// element written by exactly one of them, so the result is independent of the thread count. The
/// compiled row of the selected backend runs over each planar row; the scalar row runs everywhere
/// else and on the reference backend, and the two store the same bits.
///
/// Thread-safe with respect to `execution`, which is only read; the set's terms are written and
/// must not be read or written by anything else during the call. Allocation-free except for a
/// failure it has to describe.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack.
/// @param set the terms, as `checkStackAccumulatorSet` accepts them.
/// @param band the frame's samples for one band of rows.
/// @param frameWeights the stack's per-frame weight table, one weight per frame and channel in
///        frame-major order, read only when the model weighs its frames; each weight of this frame
///        must then be finite and non-negative.
/// @return what was accumulated; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this
///         build cannot serve; `ErrorCode::IncompatibleArguments` when the set does not match the
///         model, the band is not at the tile width, its layout does not describe the output's
///         width and channels, its mask does not cover it, or the weights do not describe the
///         model; `ErrorCode::InvalidArgument` for a null pointer, a misaligned sample pointer, a
///         mask under a rule that needs none, no mask under the rule that needs one, or a frame
///         weight that is not finite and non-negative, naming its index; `ErrorCode::OutOfRange`
///         when `frameIndex` or the band's rows are outside the stack; and `ErrorCode::InvalidData`
///         when an admitted sample is not finite or a term leaves the finite range, after which the
///         set holds a partial frame and is no longer a stack's terms.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackAccumulationOutcome>
accumulateStackFrame(const ExecutionContext& execution, const StackModel& model, const StackAccumulatorSet& set,
                     const StackFrameBand& band, std::span<const double> frameWeights);

/// Accumulates every band of one tile - one band per frame, all over the same output rows - into
/// the terms in a single pass whose frame loop sits inside the row loop.
///
/// Each output row's terms are then re-touched from the private cache across the frames rather than
/// from memory once per frame, and the tile costs one hand-off to the workers rather than one per
/// frame; each element is written by exactly one worker, which folds the bands in the order they
/// are listed, so the bits are those of `accumulateStackFrame` called once per band in that order,
/// at any worker count.
///
/// Thread-safe with respect to `execution`, which is only read; the set's terms are written and
/// must not be read or written by anything else during the call. Allocates no buffers: weights
/// and layouts are borrowed, counts use the caller's entries, and worker totals have fixed size.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack.
/// @param set the terms, as `checkStackAccumulatorSet` accepts them.
/// @param bands the frames' bands of one tile, each as `accumulateStackFrame` accepts it, all of the
///        same rows at the same first output row and the same representation, in any frame order;
///        empty accumulates nothing.
/// @param frameWeights the stack's per-frame weight table, as for `accumulateStackFrame`.
/// @param admittedPerBand one entry per band in `bands` order; the samples each band's validity
///        rule admitted - accumulated plus excluded by weight - are added to it, which is how an
///        executor keeps a per-frame count across tiles. Must not be accessed during the call.
///        On cancellation or a numeric failure it retains the counts of rows already processed,
///        just as the accumulator terms retain their partial updates.
/// @return the outcome over every band; the refusals of `accumulateStackFrame` for any band, which
///         name the member exactly as it does; `ErrorCode::IncompatibleArguments` naming `bands`
///         when they do not describe the same rows, or `admittedPerBand` when it does not hold one
///         entry per band; nothing is written before every band has been checked.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackAccumulationOutcome>
accumulateStackFrames(const ExecutionContext& execution, const StackModel& model, const StackAccumulatorSet& set,
                      std::span<const StackFrameBand> bands, std::span<const double> frameWeights,
                      std::span<std::uint64_t> admittedPerBand);

/// Where the finalization writes, borrowed for the call.
///
/// The image, the mask and the maps are the working set's, in its packed planar order; a map is
/// supplied exactly when the model requests it, at one entry per output element.
struct StackFinalizeTargets
{
    /// What the image is written as. Must be the model's output representation.
    SampleType outputSampleType = SampleType::Float32;

    /// The image's samples, one per output element at `outputSampleType`.
    std::span<std::byte> outputImage{};

    /// The image's validity mask, one bit-plane per channel over the output's extent, or null to
    /// write none. Set for an element that received a sample and cleared for one that did not.
    const ValidityMaskView<std::uint8_t>* outputMask = nullptr;

    /// How many samples contributed to each element, when the model requests a coverage map.
    std::span<std::uint32_t> coverage{};

    /// The sample variance of the contributing samples, zero below two of them, when the model
    /// requests a variance map.
    std::span<float> variance{};

    /// The effective sample count of the weighted combination - the squared weight sum over the
    /// sum of squared weights, which is the count itself for uniform weights - when the model
    /// requests a signal-to-noise gain map.
    std::span<float> signalToNoiseGain{};

    /// The sum of the contributing weights, which is the count for uniform weights, when the
    /// model requests a weight-sum map.
    std::span<float> weightSum{};
};

/// Checks that finalization targets describe a model's output.
///
/// The rule `finalizeStackAccumulators` applies before it writes, published so that an operation
/// writing the same targets band by band - the sample-history combinations - holds them to the same
/// rule: the model's output representation, one sample per output element, a mask covering the
/// output with one bit-plane per channel when one is given, and each map supplied exactly when the
/// model requests it at one entry per element.
///
/// Thread-safe, reentrant, and allocation-free except for a failure it has to describe.
///
/// @param model the checked stack.
/// @param targets where the image, the mask and the maps go.
/// @return the number of output elements; `ErrorCode::IncompatibleArguments` when the
///         representation, the image's bytes, the mask or a map does not describe the output,
///         naming the target.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint64_t> checkStackFinalizeTargets(const StackModel& model,
                                                                                   const StackFinalizeTargets& targets);

/// What the finalization did.
struct StackFinalizeOutcome
{
    /// How many output elements were written, which is every one.
    std::uint64_t elementsWritten = 0u;

    /// How many elements received at least one sample and carry a value.
    std::uint64_t elementsWithSamples = 0u;

    /// How many elements received no sample, were written as zero and had their mask bit cleared.
    std::uint64_t elementsWithoutSamples = 0u;

    /// How many written values are not finite, including Float64 terms that overflow when the
    /// output is narrowed to Float32.
    std::uint64_t nonFiniteOutputs = 0u;

    /// Whether two outcomes say the same thing.
    ///
    /// @param left one outcome.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackFinalizeOutcome& left,
                                                   const StackFinalizeOutcome& right) noexcept = default;
};

/// Turns the running terms into the output image, its mask and its maps.
///
/// For an element with at least one sample the value is the weighted mean - the sum over the
/// weight sum, or over the count when the stack weighs nothing - under `StackCombination::Average`,
/// the sum itself under `StackCombination::Sum`, and the extremum under the two extrema; it is
/// narrowed once to the output representation, the mask bit is set, and each requested map is
/// written from the terms. An element with no sample is written as zero with its mask bit cleared
/// and every map entry zero. Nothing here scales a sum to a domain: what a sum means in the
/// output's numeric domain is the stack method's decision, stated beside the method.
///
/// Thread-safe with respect to `execution` and the set, which are only read; the targets are
/// written and must not be shared. Allocation-free except for a failure it has to describe.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack.
/// @param set the terms, as `checkStackAccumulatorSet` accepts them.
/// @param targets where the image, the mask and the maps go.
/// @return what was written; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint;
///         `ErrorCode::IncompatibleArguments` when the set does not match the model, the output
///         representation or the image's bytes are not the model's, the mask does not cover the
///         output, or a map is supplied that was not requested or missing that was, naming the
///         target.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackFinalizeOutcome>
finalizeStackAccumulators(const ExecutionContext& execution, const StackModel& model, const StackAccumulatorSet& set,
                          const StackFinalizeTargets& targets);

/// Turns the running terms into the output with every value divided by a stated divisor first.
///
/// The finalization above with one more decision stated by the caller: each element's value - the
/// mean, the sum or the extremum - is divided by `valueDivisor` before it is narrowed once, so the
/// output is still narrowed exactly once. The sum method's normalization to its peak is what asks
/// for it; a divisor of one is the finalization above, bit for bit. The maps are never divided:
/// a count, a variance and a weight sum are what they are whatever the values are scaled to.
///
/// Thread-safe with respect to `execution` and the set, which are only read; the targets are
/// written and must not be shared. Allocation-free except for a failure it has to describe.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack.
/// @param set the terms, as `checkStackAccumulatorSet` accepts them.
/// @param targets where the image, the mask and the maps go.
/// @param valueDivisor what every value is divided by before it is narrowed. Must be positive and
///        finite.
/// @return what was written, or the refusals of the finalization above; and
///         `ErrorCode::InvalidArgument` for a divisor that is not positive and finite, naming it.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackFinalizeOutcome>
finalizeStackAccumulators(const ExecutionContext& execution, const StackModel& model, const StackAccumulatorSet& set,
                          const StackFinalizeTargets& targets, double valueDivisor);

/// How rejected columns are combined; separate from which samples the estimator rejects.
enum class StackRejectionCombinationPolicy : std::uint8_t
{
    /// The Siril-compatible combination. A weighted stack re-reads the original value of every
    /// admitted frame that lies inside the survivors' closed value range and averages those with
    /// their frame weights; when that weighted sum or weight total is zero it writes the sum of the
    /// positive values in the range over the survivor count. When every admitted sample was
    /// rejected, the element is the median of the admitted samples and reports their coverage. The
    /// variance and weight maps describe the survivors, or every admitted sample in that exhausted
    /// case.
    SirilCompatible,

    /// Averages exactly the survivors' values with their frame weights. A value a replacing
    /// estimator substituted contributes, a survivor of zero weight does not, and an element with no
    /// contributor is written as zero with its mask bit cleared.
    SurvivorValues
};

/// What a sum stack's sums mean in the output.
///
/// A property of the method rather than of the request, so it is stated to `finalizeStackMethod`
/// beside the model; `kDefaultStackSumDomain` is what a caller that states none gets.
enum class StackSumDomain : std::uint8_t
{
    /// Every sum is divided by the largest sum over the elements that received a sample, so the
    /// brightest element is exactly one and the output is a fraction of full scale. A stack with
    /// no positive sum has nothing to normalize to, and its sums are written as they are with a
    /// warning said to the diagnostic sink.
    NormalizedToPeak,

    /// The sums are written as they are, and the output's numeric domain is restated as a linear
    /// quantity in the input's units, because a sum of fractions is no longer a fraction.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Raw
};

/// Whether a value names a sum domain this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param domain the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackSumDomain(StackSumDomain domain) noexcept
{
    return static_cast<std::uint8_t>(domain) <= static_cast<std::uint8_t>(StackSumDomain::Raw);
}

/// The domain's own name.
///
/// Thread-safe and allocation-free.
///
/// @param domain the domain to name. A value outside the enumeration is named
///        `"UnknownStackSumDomain"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackSumDomainName(StackSumDomain domain) noexcept;

/// The sum domain a caller that states none gets: every sum normalized to its peak.
inline constexpr StackSumDomain kDefaultStackSumDomain = StackSumDomain::NormalizedToPeak;

/// What the finalization of a streaming method did.
struct StackMethodFinalizeOutcome
{
    /// What the accumulators' finalization wrote.
    StackFinalizeOutcome finalize{};

    /// What the output's samples mean, as `stackMethodOutputDomain` states.
    NumericDomain outputDomain = NumericDomain::NormalizedLinear;

    /// The sum domain that was applied; inert for every combination but the sum.
    StackSumDomain sumDomain = kDefaultStackSumDomain;

    /// The largest sum over the elements that received a sample, for a sum normalized to its peak;
    /// zero for every other combination and domain, and when no element received a sample.
    double peakSum = 0.0;

    /// Whether the sums were divided by a positive peak.
    bool normalizedToPeak = false;

    /// Whether two outcomes say the same thing.
    ///
    /// @param left one outcome.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackMethodFinalizeOutcome& left,
                                                   const StackMethodFinalizeOutcome& right) noexcept = default;
};

} // namespace an
