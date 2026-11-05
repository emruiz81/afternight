// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stack methods: what each combination of a stack's samples is, stated
/// beside the kernels that compute it.
///
/// A sum has a named domain: `StackSumDomain::NormalizedToPeak` divides every sum by the largest one
/// so the brightest element is exactly one and the output is a fraction of full scale, and
/// `StackSumDomain::Raw` writes the sums as they are and restates the output's numeric domain as a
/// linear quantity in the input's units. `stackMethodOutputDomain` is the closed table from an input
/// domain and a sum domain to the output's, and the one cell it refuses - a raw sum of nonlinearly
/// encoded samples, which is neither an encoded fraction nor a linear quantity - is refused by name
/// before a term is touched. The auto-adaptive mean scales by `kAutoAdaptiveSampleScale`
/// unconditionally - its early exit would otherwise fire on every unit-range column, and no robust
/// weighting would happen - iterates `kAutoAdaptiveIterations` times, escapes to the plain mean below
/// `kAutoAdaptiveSmallSigma` and needs `kAutoAdaptiveMinimumSamples`, each a named constant rather
/// than a literal inside a loop.
///
/// The streaming combinations - the mean, the sum and the two extrema - are computed by the
/// whole-output accumulators in `Algorithms/Stacking/StackAccumulators.h`, one frame at a time;
/// `finalizeStackMethod` is their finalization with the sum's domain applied. The two sample-history
/// combinations - the exact median and the auto-adaptive mean - are computed by
/// `combineStackSampleHistory` from every frame's band for one band of output rows, straight into
/// the output image, its mask and its maps, one column of samples at a time on the per-worker
/// scratch the plan sized. Each column kernel is written once as a scalar row and once as a
/// compiled row that judges `kStackSampleColumnLanes` columns at a time, and the two store identical
/// bits: a signed zero is normalized on read, the median is an exact selection, and the auto-adaptive
/// iteration is spelled in the adopted algorithm's own operation order on both.
///
/// A column whose largest sample is not positive takes its robust mean rather than zero; a sum with
/// no positive peak is written as it is, with a warning; a column holds the samples each frame's
/// validity rule admits, with no exact-zero convention and no clamp of the median or the
/// auto-adaptive mean to the unit interval; and the variance map is a two-pass sample variance.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

class StackModel;

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included: both operations borrow one for the duration of a call, and a
/// caller reaching them holds an execution context and has therefore already included that header.
class ExecutionContext;

/// How many times the auto-adaptive weighted mean re-weights a column around its running mean.
///
/// The adopted algorithm's own count, which its author recorded as enough for the weights to
/// settle. Not a parameter of the request, because the combination is the one with no tunable.
inline constexpr std::uint32_t kAutoAdaptiveIterations = 5u;

/// The dispersion, in scaled sample units, below which the auto-adaptive mean returns the plain
/// mean instead of iterating.
///
/// A column whose samples scaled by `kAutoAdaptiveSampleScale` spread by five units or less is a
/// column of one value plus noise the weighting could only amplify, and the adopted algorithm
/// escapes on its first pass. Stated in the scaled units the algorithm works in.
inline constexpr double kAutoAdaptiveSmallSigma = 5.0;

/// The fewest samples the auto-adaptive weighted mean re-weights.
///
/// A column of one or two samples has no dispersion to weight by, and the adopted algorithm takes
/// their plain mean.
inline constexpr std::uint32_t kAutoAdaptiveMinimumSamples = 3u;

/// Checks that a sum domain describes this stack's output.
///
/// The same rule as `stackMethodOutputDomain`, reported the way an operation reports, so an
/// executor can refuse before it reads a frame rather than after it accumulated every one.
///
/// Thread-safe, reentrant, and allocation-free except for a failure it has to describe.
///
/// @param model the checked stack.
/// @param sumDomain what a sum's sums mean.
/// @return success when the output has a domain; `ErrorCode::InvalidArgument` for a value outside
///         `StackSumDomain`; `ErrorCode::IncompatibleArguments` for the raw sum of an encoded
///         input, naming the domain.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkStackSumDomain(const StackModel& model, StackSumDomain sumDomain);

/// Which of the sample-history combinations a column kernel computes.
///
/// The two combinations that judge every sample of a pixel together and take no rejection. A
/// rejection's survivors are also held as sample history, but they are combined by the estimator
/// that chose them, which is not part of this header.
enum class StackSampleHistoryCombination : std::uint8_t
{
    /// The exact median of the admitted samples: the middle one, or the mean of the two middle
    /// ones.
    Median,

    /// The auto-adaptive weighted mean of the admitted samples, scaled by
    /// `kAutoAdaptiveSampleScale` and iterated `kAutoAdaptiveIterations` times.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AutoAdaptiveWeightedAverage
};

/// Whether a value names a combination this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param combination the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackSampleHistoryCombination(StackSampleHistoryCombination combination) noexcept
{
    return static_cast<std::uint8_t>(combination) <=
           static_cast<std::uint8_t>(StackSampleHistoryCombination::AutoAdaptiveWeightedAverage);
}

/// The combination's own name.
///
/// Thread-safe and allocation-free.
///
/// @param combination the combination to name. A value outside the enumeration is named
///        `"UnknownStackSampleHistoryCombination"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
stackSampleHistoryCombinationName(StackSampleHistoryCombination combination) noexcept;

/// Turns a streaming stack's running terms into its output, under the method's domain.
///
/// For every combination but the sum this is `finalizeStackAccumulators` exactly, and the sum
/// domain is inert. For a sum under `StackSumDomain::NormalizedToPeak` the largest sum over the
/// elements that received a sample is reduced first - deterministically, the same at every thread
/// count - and every value is divided by it before it is narrowed once to the output
/// representation, so the brightest element is exactly one; a peak that is not positive normalizes
/// nothing, the sums are written as they are, and the diagnostic sink is told at
/// `DiagnosticSeverity::Warning`. Under `StackSumDomain::Raw` the sums are written as they are. What
/// was applied is said to the sink in one sentence at `DiagnosticSeverity::Info`, and an execution
/// given no sink is served in silence.
///
/// Thread-safe with respect to `execution` and the set, which are only read; the targets are
/// written and must not be shared. Allocates the reduction's partials and only a failure it has to
/// describe.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack.
/// @param sumDomain what a sum's sums mean; inert for every other combination.
/// @param set the terms, as `checkStackAccumulatorSet` accepts them.
/// @param targets where the image, the mask and the maps go, as `checkStackFinalizeTargets`
///        accepts them.
/// @return what was written; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint;
///         `ErrorCode::InvalidArgument` for a sum domain outside its enumeration;
///         `ErrorCode::IncompatibleArguments` when the raw sum of an encoded input is asked for,
///         when the set does not match the model, or when the targets do not describe the output,
///         each naming what does not fit.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackMethodFinalizeOutcome>
finalizeStackMethod(const ExecutionContext& execution, const StackModel& model, StackSumDomain sumDomain,
                    const StackAccumulatorSet& set, const StackFinalizeTargets& targets);

/// What one band's sample-history combination did.
struct StackSampleHistoryOutcome
{
    /// How many samples the frames' rules admitted into a column.
    std::uint64_t samplesAdmitted = 0u;

    /// How many samples the frames' rules kept out.
    std::uint64_t samplesExcludedByValidity = 0u;

    /// How many output elements the band covered, which is every element of its rows and channels.
    std::uint64_t elementsCombined = 0u;

    /// How many of them received at least one sample and carry a value.
    std::uint64_t elementsWithSamples = 0u;

    /// How many received no sample, were written as zero and had their mask bit cleared.
    std::uint64_t elementsWithoutSamples = 0u;

    /// Which implementation the dispatch table selected for the execution.
    BackendSelection backend{};

    /// Whether a compiled row combined at least one run of columns. False on the reference
    /// backend, on a build with no compiled row for the selected instruction set, and for a
    /// history in which any band's layout is not planar.
    bool compiledRowUsed = false;

    /// Whether two outcomes say the same thing.
    ///
    /// @param left one outcome.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackSampleHistoryOutcome& left,
                                                   const StackSampleHistoryOutcome& right) noexcept = default;
};

/// Combines every frame's samples for one band of output rows, column by column, into the output.
///
/// The band is the plan's tile: one `StackFrameBand` per frame, all at the tile width, of the same
/// rows and the same first output row, in frame order with each band's `frameIndex` its position.
/// For every output element of the band the column is the samples of that pixel and channel each
/// frame's own rule admits, in frame order; the element's value is the column's median or its
/// auto-adaptive weighted mean under `stackSampleHistoryCombinationOf`, narrowed once to the output
/// representation; its mask bit is set; the coverage map takes the column's length, the variance map
/// the sample variance of the column over one less than its length and zero below two samples, and
/// the gain and weight-sum maps the length, because neither combination weighs anything. An element
/// no frame reached is written as zero with its bit cleared and every map entry zero.
///
/// The rows and channels of the band are spread across the execution's admitted workers, each
/// output element written by exactly one of them and each worker judging its columns on its own
/// slice of scratch, so the result is independent of the thread count. The compiled row of the
/// selected backend judges `kStackSampleColumnLanes` columns at a time when every band is planar;
/// the scalar row runs everywhere else and on the reference backend, and the two store the same
/// bits.
///
/// Thread-safe with respect to `execution` and the bands, which are only read; the scratch and the
/// targets are written and must not be shared. Allocates only a failure it has to describe: each
/// worker writes one record per frame into the reserved tail of its own scratch slice.
///
/// A rejection model delegates to `combineRejectedStackSampleHistory` with empty weights and
/// rejection maps. Use that entry point when the model requests either of them.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack: a median, an auto-adaptive mean, or an unweighted rejection
///        with no rejection maps.
/// @param frames one band per frame of the model, as `checkStackFrameBand` accepts each, agreeing
///        in rows and first output row, with `frameIndex` equal to the band's position.
/// @param workerScratch one slice per admitted worker, at least `admittedConcurrency` of them, each
///        holding at least `stackScratchBytesPerWorker` of the model beginning on a
///        `kStackScratchAlignmentBytes` boundary, as `StackWorkingSet::workerScratch` hands them out.
/// @param targets where the image, the mask and the maps go, as `checkStackFinalizeTargets`
///        accepts them; only the band's rows are written.
/// @return what was combined; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this
///         build cannot serve; `ErrorCode::IncompatibleArguments` when the model's samples are
///         combined elsewhere, when the frame count, a band's shape or first row, a scratch slice's
///         size or count, or a target does not describe the model, naming it;
///         `ErrorCode::InvalidArgument` for a band out of its place, a misaligned scratch slice, or
///         whatever `checkStackFrameBand` refuses of a band; `ErrorCode::OutOfRange` when a band's
///         rows lie outside the output; and `ErrorCode::InvalidData` when an admitted sample is not
///         finite or a combined value leaves the finite range, after which the band's rows of the
///         targets hold a partial result.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackSampleHistoryOutcome>
combineStackSampleHistory(const ExecutionContext& execution, const StackModel& model,
                          std::span<const StackFrameBand> frames, std::span<const std::span<std::byte>> workerScratch,
                          const StackFinalizeTargets& targets);

} // namespace an
