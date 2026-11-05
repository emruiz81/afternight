// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the rejection estimators: one column of samples judged in place by the
// selected family's rule, on one worker's slice of the planned scratch.
//
// Two callers judge columns and both come here, so a verdict is the same whichever asked: the band
// driver in `StackRejection.cpp`, which judges every column of a tile and combines its survivors,
// and the one-column operations `rejectStackSampleColumn` and `evaluateStackGesdColumn`. A worker's
// slice is cut into the arrays the planner's `stackScratchBytesPerWorker` counts, in the order it
// counts them, and nothing is allocated: the tables every column shares - the linear fit's rank
// statistics and the deviate test's critical values - are prepared once before the columns are
// judged, and each column then reuses them.
//
// Three invariants hold in every estimator. The flags stay in frame order, so a verdict is always
// found at its frame's position. A replaced sample stays among the survivors and records the side
// of its first replacement. And a ranking is a separate table of frame indices: sorting it never
// permutes the column or its weights.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackRejection.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

namespace an
{

struct StackColumnFrame;

/// One sample's verdict inside the estimators: the published disposition under a shorter name.
///
/// `Kept` or `Missing` on entry; `Low` or `High` for a removal, and `ReplacedLow` or
/// `ReplacedHigh` for a sample the median-replacement family substituted.
using RejectionFlag = StackSampleDisposition;

/// One worker's rejection arrays, cut from its scratch slice by `rejectionWorkspace`.
///
/// Every pointer is into the caller's slice and lives as long as it; the record owns nothing and
/// is cheap to cut again. An array the model's family does not use is null. The two rank
/// statistics are held in the record rather than in the slice, so a record cut again must be
/// prepared again before a linear fit reads them.
struct RejectionWorkspace
{
    /// The column: one Float64 sample per frame of the model, in frame order. The
    /// median-replacement family writes its replacement values here.
    double* column = nullptr;

    /// One checked row record per frame, which the band driver fills for the row it is judging;
    /// the estimators do not read it.
    StackColumnFrame* frameRows = nullptr;

    /// One disposition per frame of the model, in frame order: `Kept` or `Missing` on entry, the
    /// verdicts on return.
    RejectionFlag* flags = nullptr;

    /// The working copy, eight bytes per frame: the survivors gathered at the tile width, the
    /// Float32 deviations and 32-bit bins of the median absolute deviation's histogram, the
    /// deviate test's sorted Float32 survivors, or the band driver's median fallback.
    std::byte* work = nullptr;

    /// The deviate test's critical values, indexed by sample count minus one and prepared for
    /// every count from three to the frame count; null for every other family.
    double* critical = nullptr;

    /// The linear fit's running-mean reciprocals, `1 / (i + 1)` formed in Float32 for every rank
    /// `i` below the frame count; null for every other family.
    double* reciprocals = nullptr;

    /// The ranking: the survivors' frame indices ordered by value and then by frame, for the
    /// linear fit and the deviate test; null for every other family.
    std::uint32_t* order = nullptr;

    /// The deviate test's provisional candidates in iteration order, room for
    /// `stackGesdtMaximumOutliers` of the frame count; null for every other family.
    StackGesdCandidate* candidates = nullptr;

    /// One effective weight per frame for the column being combined - its frame weight times its
    /// local weight - when the model weighs its frames or applies a local surface; null otherwise.
    /// The estimators do not read it.
    double* weights = nullptr;

    /// The linear fit's mean rank, `(frames - 1) / 2` formed in Float32 over every frame of the
    /// model.
    double meanRank = 0.0;

    /// The reciprocal of the ranks' population variance over every frame of the model, at the
    /// tile width.
    double inverseRankVariance = 0.0;
};

/// What the estimators recorded while judging one column.
struct RejectionCounts
{
    /// Low-side removals and replacements, added to, each repeated replacement counted.
    std::uint64_t low = 0u;

    /// High-side removals and replacements, added to, each repeated replacement counted.
    std::uint64_t high = 0u;

    /// How many provisional candidates the deviate test wrote to `RejectionWorkspace::candidates`;
    /// set rather than added to, and left alone by every other family.
    std::uint32_t candidates = 0u;
};

/// Cuts one worker's scratch slice into the model's rejection arrays.
///
/// The arrays are taken in the order and at the sizes `stackScratchBytesPerWorker` counts them,
/// each rounded by `alignedStackScratchTerm`. The median-replacement family writes into the
/// column itself and the winsorized family clamps the working copy. Nothing is written, and the slice's size and
/// alignment are the caller's to check.
///
/// @param model the checked stack, which applies a rejection.
/// @param bytes the worker's slice, at least `stackScratchBytesPerWorker` of the model.
/// @return the arrays; the rank statistics are zero until `prepareRejectionWorkspace` runs.
[[nodiscard]] RejectionWorkspace rejectionWorkspace(const StackModel& model, std::span<std::byte> bytes) noexcept;

/// Prepares the tables every column of the model reuses.
///
/// For the linear fit, the rank reciprocals in the slice and the mean rank and the inverse rank
/// variance in the record, all over every frame of the model; for the deviate test, the critical
/// value of every sample count from three to the frame count, with a cancellation checkpoint per
/// count. Every other family needs nothing and succeeds at once.
///
/// @param execution the operation, checked for cancellation.
/// @param model the checked stack.
/// @param workspace the arrays `rejectionWorkspace` cut.
/// @return success; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from a checkpoint; or
///         the bare `ErrorCode::InvalidArgument` or `ErrorCode::InvalidData` of
///         `studentizedDeviateCritical` when the significance forms no finite critical value.
[[nodiscard]] Status prepareRejectionWorkspace(const ExecutionContext& execution, const StackModel& model,
                                               RejectionWorkspace& workspace);

/// Judges one column in place under the model's rejection family.
///
/// Reads the survivors of `workspace.column` at the tile width, rewrites the flags to the
/// verdicts, writes the median-replacement family's replacements into the column, and adds the
/// events to `counts`. The rules are the adopted ones, described in the module
/// comment of `RejectionEstimators.cpp`. Cancellation is checked between the estimator's
/// iterations, and the reference backend makes the deviate test rescan every step.
///
/// @param execution the operation, checked for cancellation and asked for its backend.
/// @param model the checked stack.
/// @param workspace prepared arrays whose flags hold `Kept` or `Missing` for every frame of the
///        model and whose column holds a finite sample under every `Kept` flag.
/// @param counts receives the events.
/// @return success; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from a checkpoint; a
///         bare `ErrorCode::InvalidData` when a statistic leaves the finite range, the winsorized
///         deviation does not settle within 8,192 passes, or a sample does not fit the deviate
///         test's Float32; and a bare `ErrorCode::InvalidArgument` for a family no estimator knows.
[[nodiscard]] Status estimateRejection(const ExecutionContext& execution, const StackModel& model,
                                       const RejectionWorkspace& workspace, RejectionCounts& counts);

/// Whether a sample still contributes: kept, or replaced by the median-replacement family.
///
/// @param flag the sample's disposition.
/// @return true for `Kept`, `ReplacedLow` and `ReplacedHigh`.
[[nodiscard]] constexpr bool rejectionSurvivor(RejectionFlag flag) noexcept
{
    return flag == RejectionFlag::Kept || flag == RejectionFlag::ReplacedLow || flag == RejectionFlag::ReplacedHigh;
}

} // namespace an
