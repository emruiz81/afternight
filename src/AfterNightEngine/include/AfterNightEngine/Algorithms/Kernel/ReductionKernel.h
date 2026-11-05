// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Sums, moments, extrema and norms of one image component, with a stated error bound.
///
/// `referenceSumKernel` is the scalar oracle of `KernelOperation::ReductionSum`: one Float32 plane
/// summed into Float64 in row-major order on the calling thread. `reducePlaneStatistics` and
/// `reducePlaneStatisticsOver` are the primitives the engine's statistics are built on. They read
/// any of the eight resident representations where they lie, partition the work into chunks derived
/// from the input rather than from the thread count, merge the chunks through a fixed pairwise
/// tree, and so produce the same bits at every admitted thread count for one dispatched target.
/// Each sum they report comes with a forward error bound derived from the depth of that tree.
///
/// Both primitives refuse with `ErrorCode::NotInitialized` an execution that holds nothing or has no
/// memory resource; with `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` a stopped one; with
/// `ErrorCode::UnsupportedBackend` one constrained to a backend this build has no dispatch table
/// for; with `ErrorCode::InvalidArgument` null samples or layout, an unknown sample type, samples not
/// aligned for their type, or a packed mask supplied under a rule that reads none or missing under
/// `ValidityRule::ExplicitMask`; with `ErrorCode::IncompatibleArguments` a layout whose element width
/// is not the sample type's, a plane or component the layout does not have, or a mask that does not
/// cover the source and its selected bit-plane; and with `ErrorCode::MemoryBudgetExceeded` or
/// `ErrorCode::AllocationFailed` a refusal of their working memory.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleType.h>

namespace an
{

/// Result of the scalar sum reduction.
struct ReductionKernelResult
{
    /// Selection and write counts. A reduction writes one scalar exactly when it succeeds.
    KernelRunResult run{};

    /// Row-major sum accumulated in Float64.
    double sum = 0.0;
};

/// Sums selected Float32 samples into Float64 in fixed row-major order.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample input plane.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the sum and counts; `ErrorCode::EmptySelection` when no sample participates;
///         `ErrorCode::NotInitialized` for an execution that holds nothing; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the execution stops; `ErrorCode::IncompatibleArguments`
///         for a plane with more than one sample per pixel or a mask that does not cover it; or
///         `ErrorCode::InvalidArgument` for a mask supplied under a rule that reads none or missing
///         under `ValidityRule::ExplicitMask`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ReductionKernelResult> referenceSumKernel(const ExecutionContext& execution,
                                                                                    PlaneView<const float> source,
                                                                                    const KernelValidityView& validity);

/// The deterministic reduction partition and the parallel width that walked it.
struct ReductionPlan
{
    /// Fixed chunks derived from input size and operation cost, independent of thread count.
    std::uint64_t deterministicChunks = 0u;

    /// Workers the operation could actually use: the smaller of admitted concurrency and chunks.
    std::uint32_t plannedWorkers = 0u;

    /// Conservative maximum additions from one leaf term to the root of any reported sum.
    std::uint64_t maximumAdditionDepth = 0u;
};

/// A forward absolute-error bound for one additive reduction tree.
///
/// For finite inputs without overflow, `absoluteError` is `gamma(depth) * sumAbsoluteTerms`, where
/// `gamma(d) = d*u/(1-d*u)` and `u` is the Float64 unit roundoff. `sumAbsoluteTerms` is rounded
/// upward from the computed non-negative sum so the bound does not assume that sum was exact.
struct ReductionErrorBound
{
    /// The addition depth used by the bound.
    std::uint64_t maximumAdditionDepth = 0u;

    /// A conservative upper bound on the exact sum of the terms' absolute values.
    double sumAbsoluteTerms = 0.0;

    /// The resulting absolute error bound around the reported accumulator.
    double absoluteError = 0.0;
};

/// Stable moments, extrema, count, and norms of one selected image component.
struct ReductionStatistics
{
    /// Sample selection and scalar outputs. Nine scalar quantities are written on success.
    KernelRunResult run{};

    /// The fixed partition and actual available worker width.
    ReductionPlan plan{};

    /// Smallest selected sample, compared after exact widening to Float64.
    double minimum = 0.0;

    /// Largest selected sample, compared after exact widening to Float64.
    double maximum = 0.0;

    /// Selected samples accumulated in Float64 through the fixed tree.
    double sum = 0.0;

    /// `sum / count`, evaluated once after the tree is merged.
    double mean = 0.0;

    /// Sum of `(sample - mean)^2`, accumulated by a second fixed pass for numeric stability.
    double sumSquaredDeviations = 0.0;

    /// `sumSquaredDeviations / count`; zero for a one-sample selection.
    double populationVariance = 0.0;

    /// Sum of absolute sample magnitudes through the fixed tree.
    double l1Norm = 0.0;

    /// Square root of the fixed-tree sum of squared sample magnitudes.
    double l2Norm = 0.0;

    /// Largest absolute sample magnitude.
    double infinityNorm = 0.0;

    /// Forward bound for `sum`.
    ReductionErrorBound sumError{};

    /// Forward bound for `sumSquaredDeviations`.
    ReductionErrorBound centeredMomentError{};

    /// Forward bound for `l1Norm`.
    ReductionErrorBound l1Error{};

    /// Forward bound for the squared accumulator from which `l2Norm` is derived.
    ReductionErrorBound squaredL2Error{};
};

/// Computes the stable reduction primitives for one stored image component.
///
/// All eight resident representations are read directly. Floating inputs accumulate in Float64;
/// integers are widened to Float64, which is exact for each of them, and carry the same reported
/// forward bound - nothing infers exactness from their range. The first pass fuses count, extrema,
/// sum, L1, squared L2, and infinity norm; the second accumulates deviations from the fixed
/// first-pass mean. Both use the same thread-count-independent chunks and fixed pairwise merge
/// tree, so one binary, input, dispatched target, and options produce identical result bits at
/// every admitted thread count.
///
/// The validity rule is applied to the selected component. A packed mask must cover the image and
/// `KernelValidityView::maskPlane` selects its bit-plane. An empty selection returns
/// `ErrorCode::EmptySelection`; a selected non-finite value or arithmetic overflow returns
/// `ErrorCode::InvalidData` rather than publishing a meaningless error bound.
///
/// The operation makes one context-accounted temporary allocation of one partial result per chunk,
/// and there are at most `kMaxDeterministicChunks` chunks. Cancellation is checked at scheduler
/// chunk boundaries; no caller storage is changed.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the resident image component to read.
/// @param validity its value rule and optional packed mask.
/// @return all primitives, partition facts, and error bounds; `ErrorCode::EmptySelection` when no
///         sample participates; `ErrorCode::InvalidData` for a selected non-finite sample or an
///         accumulator that overflows; or a refusal the file comment names.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ReductionStatistics>
reducePlaneStatistics(const ExecutionContext& execution, const KernelPlaneSource& source,
                      const KernelValidityView& validity);

/// Computes stable reduction primitives over one rectangle and optional value interval.
///
/// This has the same numeric, determinism, allocation, cancellation, and failure contract as
/// `reducePlaneStatistics`. The source keeps its resident row and plane strides; no sample is
/// compacted. An explicit packed mask must have the complete source extent and is addressed in
/// source-image coordinates, so a non-byte-aligned horizontal origin needs no temporary mask.
///
/// A value interval narrows the selection further, and is how a clipped estimator asks for the
/// moments of the samples a clipping rule retained without materializing that subset anywhere. It
/// composes with the validity rule rather than replacing it: a sample participates when the rule
/// admits it and the interval contains it. The reported `run.selection.total` remains the number of
/// pixels the rectangle covers, so the retained fraction stays legible.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the complete resident image component to read.
/// @param validity its value rule and optional complete-source packed mask.
/// @param selection the contained rectangle and optional value interval; the rectangle may be empty.
/// @return all primitives, partition facts, and error bounds; or the failures documented by
///         `reducePlaneStatistics`, including `ErrorCode::EmptySelection` for an empty rectangle or
///         an interval nothing falls in, and `ErrorCode::InvalidArgument` for a rectangle outside
///         the source or a non-finite or inverted interval.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ReductionStatistics>
reducePlaneStatisticsOver(const ExecutionContext& execution, const KernelPlaneSource& source,
                          const KernelValidityView& validity, const KernelPlaneSelection& selection);

} // namespace an
