// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Deterministic histogram remapping through a declared curve.
///
/// A transformed histogram is an approximation of a transformed image: a source bin knows only a
/// count and an interval, not the samples' positions inside it. This surface therefore makes the two
/// useful approximations names instead of presenting either as exact. Counts are always conserved.
///
/// Each source bin is represented by its centre. The centre goes through the curve of
/// `Algorithms/Pointwise/ToneCurve.h`, and the bin's whole count follows it into the destination -
/// into one bin, or split between the two destination bins whose centres bracket it. Because the
/// count is placed rather than resampled, a flat stretch of the curve piles many source bins into
/// one destination bin and a steep one leaves destination bins empty, as the transformed image would.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Algorithms/Statistics/Histogram.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <span>

namespace an
{

/// One operation's runtime policy and permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// How one source-bin count is placed after its center passes through the curve.
enum class HistogramTransformApproximation : std::uint8_t
{
    /// Places the complete count in the nearest destination bin.
    NearestBin = 0u,

    /// Splits the count between adjacent destination-bin centers in proportion to distance.
    /// Integer counts are divided by truncating the upper share and assigning the remainder to the
    /// lower bin, which preserves the total deterministically.
    LinearSplit = 1u
};

/// Whether a value names a histogram-transform approximation this build supports.
///
/// @param approximation the value to inspect.
/// @return true for both supported approximations.
[[nodiscard]] constexpr bool isHistogramTransformApproximation(HistogramTransformApproximation approximation) noexcept
{
    return approximation == HistogramTransformApproximation::NearestBin ||
           approximation == HistogramTransformApproximation::LinearSplit;
}

/// What a transformed value outside the destination histogram interval does.
enum class HistogramTransformRangePolicy : std::uint8_t
{
    /// Places it in the nearest end bin and counts that placement in the result.
    Clamp = 0u,

    /// Refuses before any destination count is written.
    Reject = 1u
};

/// Whether a value names a histogram-transform range policy this build supports.
///
/// @param policy the value to inspect.
/// @return true for both supported policies.
[[nodiscard]] constexpr bool isHistogramTransformRangePolicy(HistogramTransformRangePolicy policy) noexcept
{
    return policy == HistogramTransformRangePolicy::Clamp || policy == HistogramTransformRangePolicy::Reject;
}

/// Curve, destination interval and approximation for one histogram transform.
struct HistogramTransformRequest
{
    /// Curve control points, borrowed for the call.
    std::span<const CurveControlPoint> points{};

    /// How the curve joins its points.
    CurveInterpolation interpolation = CurveInterpolation::MonotoneCubic;

    /// What a source-bin center outside the curve's domain does.
    CurveDomainPolicy curveDomainPolicy = CurveDomainPolicy::Clamp;

    /// Inclusive value represented by the destination histogram's lower edge.
    double outputLow = 0.0;

    /// Inclusive value represented by the destination histogram's upper edge, greater than
    /// `outputLow`.
    double outputHigh = 1.0;

    /// How transformed source-bin counts are placed.
    HistogramTransformApproximation approximation = HistogramTransformApproximation::LinearSplit;

    /// What transformed values outside `[outputLow, outputHigh]` do.
    HistogramTransformRangePolicy rangePolicy = HistogramTransformRangePolicy::Clamp;
};

/// Counts and approximation details from one histogram transform.
struct HistogramTransformResult
{
    /// Sum of every source bin and, on success, every destination bin.
    std::uint64_t count = 0u;

    /// Source counts whose transformed center lay below the destination interval and were clamped.
    std::uint64_t clampedLow = 0u;

    /// Source counts whose transformed center lay above the destination interval and were clamped.
    std::uint64_t clampedHigh = 0u;

    /// Which named placement rule ran.
    HistogramTransformApproximation approximation = HistogramTransformApproximation::LinearSplit;

    /// Source bins that held a non-zero count and were evaluated.
    std::uint64_t sourceBinsVisited = 0u;
};

/// Remaps an image histogram through a curve into caller-owned destination bins.
///
/// The center of source bin `i` is evaluated through the curve, then the named approximation places
/// its count in the destination. The source histogram's out-of-range and not-a-number samples are
/// not present in `sourceBins` and remain the caller's separate decision. Source and destination may
/// not overlap. All arguments and every transformed center are checked before the destination is
/// cleared, so any refusal leaves the caller's counters unchanged.
///
/// This operation is serial over at most `kMaxHistogramBins` bins. Parallelizing it would add a
/// private destination histogram per worker and more merge traffic than the curve evaluations save.
/// Temporary placements come from the execution's counted memory resource. Cancellation is checked
/// while validating and planning placements and once immediately before the destination is cleared;
/// the short commit pass then completes so cancellation cannot expose a partial histogram.
///
/// @param execution runtime policy, cancellation and the context-owned memory resource.
/// @param sourceHistogram interval and counted total associated with `sourceBins`.
/// @param sourceBins source counts, exactly `sourceHistogram.binCount` entries.
/// @param request curve, destination interval, approximation and range policy.
/// @param destinationBins one or more counters to replace, disjoint from `sourceBins` and no more
///        than `kMaxHistogramBins` entries.
/// @return the conserved count and clamp counts; `ErrorCode::NotInitialized` for an empty execution
///         or one with no memory resource; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`
///         at a pre-commit checkpoint; `ErrorCode::EmptySelection` when the source count is zero;
///         `ErrorCode::InvalidArgument` for an empty bin span, a source or destination interval that
///         is not finite and increasing, an unknown approximation or range policy, overlapping bin
///         spans, or - from the curve - malformed points, an unknown curve policy or too few points;
///         `ErrorCode::IncompatibleArguments` when the source length disagrees with
///         `sourceHistogram.binCount` or its sum with `sourceHistogram.counted`;
///         `ErrorCode::OutOfRange` for too many bins, a source-bin centre outside the curve's domain
///         under `CurveDomainPolicy::Reject`, or a transformed value outside the destination interval
///         under `HistogramTransformRangePolicy::Reject`; `ErrorCode::InvalidData` for a count
///         overflow or non-finite curve result; or `ErrorCode::MemoryBudgetExceeded` or
///         `ErrorCode::AllocationFailed` when the placement table is refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<HistogramTransformResult>
transformImageHistogram(const ExecutionContext& execution, const ImageHistogram& sourceHistogram,
                        std::span<const std::uint64_t> sourceBins, const HistogramTransformRequest& request,
                        std::span<std::uint64_t> destinationBins);

} // namespace an
