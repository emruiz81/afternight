// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Rejection over a band of sample histories: which samples of each pixel survive, and how the
/// survivors are combined.
///
/// A rejection judges every sample of an output pixel against the others, so a stack with one is a
/// sample-history stack: every frame's band of one tile is held at once, and each column - one
/// pixel of one channel across the frames - is judged by the selected family's estimator on the
/// worker's own scratch. `combineRejectedStackSampleHistory` judges and combines a whole band into
/// the output image, its mask and its maps; its second overload also counts, per frame, what it
/// admitted and rejected. `rejectStackSampleColumn` and `evaluateStackGesdColumn` run the same
/// estimator on one column without combining it, for a caller that needs the verdicts themselves.
///
/// The estimators are the sigma, median absolute deviation, median-replacement, winsorized,
/// linear-fit, percentile and generalized extreme Studentized deviate (GESD) families, each kept to
/// its adopted rule's answers - the GESD statistics included, which that rule computes in Float32
/// even for Float64 samples. Which survivors make a pixel's value is a separate choice,
/// `StackRejectionCombinationPolicy`, whose default is the Siril-compatible combination. Nothing
/// here allocates on success: every working array lives in the per-worker scratch the plan sized.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>

namespace an
{

class StackModel;

/// One sample's disposition, always indexed by its original frame position.
enum class StackSampleDisposition : std::int8_t
{
    Low = -1, ///< Removed below the estimator's centre.
    Kept = 0, ///< Admitted and unchanged.
    High = 1, ///< Removed above the estimator's centre.
    Missing = 2, ///< Excluded by the caller's validity rule; never judged.
    ReplacedLow = 3, ///< Contributes its replacement value; its first replacement was low.
    ReplacedHigh = 4 ///< Contributes its replacement value; its first replacement was high.
};

/// Counts from one column, independent of any later weighting or combination.
struct StackColumnRejectionOutcome
{
    /// The samples that entered the estimator: those whose disposition started as `Kept`.
    std::uint32_t admitted = 0u;

    /// The samples kept or replaced once the estimator finished.
    std::uint32_t surviving = 0u;

    /// Removals and replacements below the centre, counting every repeated replacement.
    std::uint64_t rejectedLow = 0u;

    /// Removals and replacements above the centre, counting every repeated replacement.
    std::uint64_t rejectedHigh = 0u;

    /// The samples first rejected or replaced below the centre, as a rejection map counts them.
    std::uint32_t mappedLow = 0u;

    /// The samples first rejected or replaced above the centre, as a rejection map counts them.
    std::uint32_t mappedHigh = 0u;
};

/// Judges a Float32 column without combining it, keeping every sample at its frame's position.
///
/// For a caller that needs to know exactly which samples survive - a diagnostic, or a drizzle that
/// combines elsewhere - this runs the estimator `combineRejectedStackSampleHistory` runs on each
/// column. Each disposition starts as `StackSampleDisposition::Kept` or
/// `StackSampleDisposition::Missing`; a missing sample is neither read arithmetically nor changed.
/// On success the dispositions hold the verdicts and only replaced samples change value, a
/// replacement's disposition recording the side of its first replacement. On failure the samples
/// and the dispositions are unchanged; the scratch may not be.
///
/// Allocates nothing on success, inside the estimator included. Cancellation is checked on entry,
/// while the GESD critical values are prepared and between the estimator's iterations. Reentrant
/// with disjoint buffers.
///
/// @param execution the operation the column is judged for. Must hold something.
/// @param model a checked stack with a rejection whose tile width is Float32; its frame count
///        supplies the linear fit's rank statistics over every frame even when the column is
///        shorter.
/// @param samples the column, at most the model's frame count; may be empty.
/// @param dispositions one flag per sample in frame order, read and then overwritten on success.
/// @param scratch one slice of at least `stackScratchBytesPerWorker` of the model, beginning on a
///        `kStackScratchAlignmentBytes` boundary. The three buffers must not overlap.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from a checkpoint;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this
///         build cannot serve; `ErrorCode::IncompatibleArguments` when the model has no rejection or
///         another tile width, the column is longer than the frame count or not as long as the
///         dispositions, or the scratch is short; `ErrorCode::InvalidArgument` for a misaligned
///         scratch, overlapping buffers, a disposition other than `Kept` or `Missing`, or a GESD
///         significance that is not strictly between zero and one once rounded to Float32; and
///         `ErrorCode::InvalidData` when an admitted sample is not finite or the estimator's
///         arithmetic leaves the finite range. Refusals made here name the operation and rejected
///         parameter; backend, checkpoint and critical-value failures retain their source detail.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackColumnRejectionOutcome>
rejectStackSampleColumn(const ExecutionContext& execution, const StackModel& model, std::span<float> samples,
                        std::span<StackSampleDisposition> dispositions, std::span<std::byte> scratch);

/// Judges a Float64 column without combining it, keeping every sample at its frame's position.
///
/// The Float32 overload's contract for a model whose tile width is Float64. GESD deliberately
/// computes its statistics in Float32, as its adopted rule does, while the survivors keep their
/// Float64 values; an admitted value outside the Float32 range is refused rather than clipped.
///
/// @param execution the operation the column is judged for. Must hold something.
/// @param model a checked stack with a rejection whose tile width is Float64.
/// @param samples the column, at most the model's frame count; may be empty.
/// @param dispositions one `Kept` or `Missing` flag per sample, overwritten on success.
/// @param scratch the aligned, disjoint worker slice the plan sizes.
/// @return the counts, or the Float32 overload's failures, with the samples and the flags
///         unchanged on failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackColumnRejectionOutcome>
rejectStackSampleColumn(const ExecutionContext& execution, const StackModel& model, std::span<double> samples,
                        std::span<StackSampleDisposition> dispositions, std::span<std::byte> scratch);

/// The endpoint provisionally removed from GESD's active sorted interval.
///
/// Distinct from the side of a confirmed rejection, which is measured from the original median.
enum class StackGesdCandidateSide : std::uint8_t
{
    Low, ///< The lower endpoint, also chosen when both endpoint distances tie.
    High ///< The upper endpoint when strictly farther from the active mean.
};

/// One provisional GESD removal, in iteration order, with its original frame identity.
struct StackGesdCandidate
{
    /// The sample's position in the supplied column, counting missing positions.
    std::uint32_t frameIndex = 0u;

    /// The endpoint removed for the next iteration.
    StackGesdCandidateSide side = StackGesdCandidateSide::Low;

    /// Whether this iteration's statistic strictly exceeds its critical value.
    bool significant = false;

    /// Whether this or a later candidate is significant, so this sample is finally rejected.
    bool confirmed = false;
};

/// A column's confirmed counts together with the length of its provisional candidate report.
struct StackGesdColumnOutcome
{
    /// The same counts `rejectStackSampleColumn` returns.
    StackColumnRejectionOutcome rejection{};

    /// How many leading entries of the supplied candidate buffer were written.
    std::uint32_t candidatesWritten = 0u;
};

/// Judges a Float32 GESD column and reports its provisional decisions on success.
///
/// Runs the estimator `rejectStackSampleColumn` runs, backend selection included, and also writes
/// every provisional removal, confirmed or not, in iteration order. A constant column and one with
/// fewer than three admitted samples produce no candidate, and a missing sample never becomes one.
/// Entries past `candidatesWritten` are untouched. The samples, the dispositions and the candidate
/// records are unchanged on failure; the scratch may not be. A candidate's flags carry decisions,
/// not the moment statistics behind them.
///
/// Allocates nothing on success. Cancellation is checked on entry, while the critical values are
/// prepared and between the iterations. Reentrant with disjoint buffers.
///
/// @param execution the operation the column is judged for. Must hold something.
/// @param model a checked stack with `StackRejection::GeneralizedEsdTest` and a Float32 tile width.
/// @param samples the column in frame order, at most the model's frame count; may be empty.
/// @param dispositions one `Kept` or `Missing` flag per sample; the confirmed verdicts on success.
/// @param scratch one slice of at least `stackScratchBytesPerWorker` of the model, beginning on a
///        `kStackScratchAlignmentBytes` boundary.
/// @param candidates the caller's records, at least `stackGesdtMaximumOutliers(samples.size(),
///        model.request().rejection.gesdt.maximumOutlierFraction)` of them and aligned for a
///        `StackGesdCandidate`. All four buffers must be mutually disjoint.
/// @return the counts, or `rejectStackSampleColumn`'s failures; `ErrorCode::IncompatibleArguments`
///         also covers a model with another rejection and a short candidate buffer, and
///         `ErrorCode::InvalidArgument` a misaligned or overlapping one.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackGesdColumnOutcome>
evaluateStackGesdColumn(const ExecutionContext& execution, const StackModel& model, std::span<float> samples,
                        std::span<StackSampleDisposition> dispositions, std::span<std::byte> scratch,
                        std::span<StackGesdCandidate> candidates);

/// Judges a Float64 GESD column and reports its provisional decisions on success.
///
/// The Float32 overload's contract for a model whose tile width is Float64. The statistics are
/// computed in Float32 and the original Float64 sample values are kept.
///
/// @param execution the operation the column is judged for. Must hold something.
/// @param model a checked stack with `StackRejection::GeneralizedEsdTest` and a Float64 tile width.
/// @param samples the column in frame order, at most the model's frame count; may be empty.
/// @param dispositions one `Kept` or `Missing` flag per sample; the confirmed verdicts on success.
/// @param scratch the aligned, disjoint worker slice the plan sizes.
/// @param candidates the aligned, disjoint caller records, sized as for the Float32 overload.
/// @return the counts, or the Float32 overload's failures, including `ErrorCode::InvalidData`
///         when an admitted value or a statistic cannot be represented in Float32.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackGesdColumnOutcome>
evaluateStackGesdColumn(const ExecutionContext& execution, const StackModel& model, std::span<double> samples,
                        std::span<StackSampleDisposition> dispositions, std::span<std::byte> scratch,
                        std::span<StackGesdCandidate> candidates);

/// Rejected-frame counts in packed planar output order, borrowed exclusively for the call.
///
/// Supply exactly the maps the model requests, one entry per output element. A replaced sample
/// counts at most once per frame, on the side of its first replacement; a sample the validity rule
/// excluded never counts as a rejection.
struct StackRejectionMaps
{
    std::span<std::uint32_t> low{}; ///< Low counts under `RejectionMapForm::LowAndHigh`.
    std::span<std::uint32_t> high{}; ///< High counts under `RejectionMapForm::LowAndHigh`.
    std::span<std::uint32_t> merged{}; ///< Both sides under `RejectionMapForm::Merged`.
};

/// Counts from the rejection and combination of one band.
struct StackRejectionOutcome
{
    /// The band's counts and the selected backend; `compiledRowUsed` is true when a compiled
    /// rejection row judged and combined at least one row.
    StackSampleHistoryOutcome history{};

    /// Removals and replacements below the centre, counting every repeated replacement.
    std::uint64_t rejectedLow = 0u;

    /// Removals and replacements above the centre, counting every repeated replacement.
    std::uint64_t rejectedHigh = 0u;

    /// Frames first rejected on the low side, summed over the band's columns.
    std::uint64_t mappedLow = 0u;

    /// Frames first rejected on the high side, summed over the band's columns.
    std::uint64_t mappedHigh = 0u;

    /// The samples that contributed to a value under the selected combination policy.
    std::uint64_t samplesCombined = 0u;

    /// Survivors whose effective weight is not positive and finite.
    std::uint64_t samplesExcludedByWeight = 0u;

    /// The planner's per-worker scratch reservation; no pixel or iteration allocates on success.
    std::uint64_t scratchBytesPerWorker = 0u;
};

/// Rejects each column of a band and combines its survivors with their original frame weights.
///
/// Runs the selected family's adopted rule: the sigma families keep the cumulative rejection guard,
/// the median absolute deviation family keeps the previous iteration's centre and the
/// unscaled histogram deviation, the linear fit keeps rank statistics over every
/// frame, and GESD runs every candidate and then confirms back through the last significant one.
/// Estimation runs at the tile's floating width, except GESD's Float32 statistics, and the
/// combination accumulates in Float64. The bands' samples are never changed.
///
/// Each frame's validity rule is applied before estimation, so exact zero is data under the
/// all-valid rule; an admitted sample that is not finite, and arithmetic that leaves the finite
/// range, are refused. Under `StackRejectionCombinationPolicy::SurvivorValues` an element with no
/// contributing survivor is written as zero with its mask bit cleared and zero in every
/// combination map; the coverage map counts the contributing survivors, the variance map is their
/// unweighted sample variance, and the gain map is the squared weight sum over the sum of squared
/// weights; a replaced sample keeps its frame's count and weight.
/// `StackRejectionCombinationPolicy::SirilCompatible` differs where its enumerator says. Under
/// either policy the rejection maps count first rejections.
///
/// Reentrant with disjoint targets and scratch. The scratch slices must not overlap one another or
/// any input or output, and no input may overlap an output; every such overlap is refused before
/// anything is written, as is every other invalid argument. Cancellation is checked on entry,
/// while the critical values are prepared, before each column - each row where a compiled row
/// judges a whole row - and between the estimator's iterations; a cancellation or an arithmetic
/// failure leaves a partly written band, which must be discarded.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model a checked stack with a rejection, which only `StackCombination::Average` admits.
/// @param frames the same bands, order and geometry `combineStackSampleHistory` accepts.
/// @param workerScratch one aligned, disjoint slice per admitted worker, each at least
///        `stackScratchBytesPerWorker` of the model.
/// @param targets where the image, the mask and the combination maps go, as
///        `checkStackFinalizeTargets` accepts them.
/// @param frameWeights finite non-negative weights in frame-major, channel-minor order exactly
///        when the model weighs its frames; empty otherwise. Local weights travel in the bands.
/// @param maps the rejection maps the model requests.
/// @param combinationPolicy how survivors become a value: the Siril-compatible combination by
///        default, or the survivors' values directly.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from a checkpoint;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this build
///         cannot serve; `ErrorCode::IncompatibleArguments` when the model has no rejection, or a
///         band, a scratch slice, a target, a map or the weight table does not describe the model,
///         naming it; `ErrorCode::InvalidArgument` for an unknown policy, a weight that is not
///         finite and non-negative, an overlap, a band out of its place or a misaligned slice;
///         `ErrorCode::InvalidArgument` or `ErrorCode::InvalidData`, naming
///         `rejection.gesdt.significance`, when no finite Float32 critical table can be formed for
///         the GESD significance; whatever `checkStackFrameBand` refuses of a band; and
///         `ErrorCode::InvalidData` for a non-finite admitted sample or result.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackRejectionOutcome> combineRejectedStackSampleHistory(
    const ExecutionContext& execution, const StackModel& model, std::span<const StackFrameBand> frames,
    std::span<const std::span<std::byte>> workerScratch, const StackFinalizeTargets& targets,
    std::span<const double> frameWeights = {}, const StackRejectionMaps& maps = {},
    StackRejectionCombinationPolicy combinationPolicy = StackRejectionCombinationPolicy::SirilCompatible);

/// The per-frame counts a rejection keeps beside its maps, borrowed exclusively for the call.
///
/// `admitted` and `rejected` hold one count per frame of the stack and are added to rather than
/// written, so a caller combining a stack band by band accumulates them across the bands: a
/// frame's admitted count grows by every sample of its band the frame's rule admitted, and its
/// rejected count by every column in which that frame's sample was first rejected on either side,
/// which are the events the rejection maps count. `workerRows` is the working memory the counts
/// are gathered in before they are summed in worker order: two counts per frame for every worker
/// the execution was admitted, overwritten by every call.
struct StackRejectionFrameCounters
{
    /// One count per frame, added to: the samples the frame's rule admitted.
    std::span<std::uint64_t> admitted{};

    /// One count per frame, added to: the columns in which the frame was first rejected.
    std::span<std::uint64_t> rejected{};

    /// Two counts per frame for every admitted worker, in worker-major order; overwritten.
    std::span<std::uint64_t> workerRows{};
};

/// Rejects each column and averages its survivors, counting per frame what it admitted and rejected.
///
/// The operation above with one addition: every worker counts, for each frame, the samples the
/// frame's rule admitted and the columns in which the frame's sample was first rejected, into its
/// own row of `counters.workerRows`; after the band the rows are summed in worker order into
/// `counters.admitted` and `counters.rejected`, so the counts are the same at every thread count
/// and a caller reading them across bands sees each frame's share of what the maps count. The
/// output, the maps and the outcome are exactly the operation above's.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model a checked stack with a rejection.
/// @param frames the same bands, order and geometry `combineStackSampleHistory` accepts.
/// @param workerScratch one aligned, disjoint slice per admitted worker, each at least
///        `stackScratchBytesPerWorker` of the model.
/// @param targets where the image, the mask and the combination maps go.
/// @param frameWeights finite non-negative weights in frame-major, channel-minor order exactly
///        when the model weighs its frames; empty otherwise.
/// @param maps the rejection maps the model requests.
/// @param combinationPolicy how survivors become a value, as for the operation above.
/// @param counters where the per-frame counts go: `admitted` and `rejected` of exactly the
///        model's frame count each, and `workerRows` of exactly two counts per frame for every
///        admitted worker, all disjoint from every other buffer.
/// @return the counts, or the refusals of the operation above; `ErrorCode::IncompatibleArguments`
///         when a counter span is not the stated length or not aligned for its counts, naming it;
///         `ErrorCode::InvalidArgument` when one overlaps another buffer.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackRejectionOutcome> combineRejectedStackSampleHistory(
    const ExecutionContext& execution, const StackModel& model, std::span<const StackFrameBand> frames,
    std::span<const std::span<std::byte>> workerScratch, const StackFinalizeTargets& targets,
    std::span<const double> frameWeights, const StackRejectionMaps& maps,
    StackRejectionCombinationPolicy combinationPolicy, const StackRejectionFrameCounters& counters);

} // namespace an
