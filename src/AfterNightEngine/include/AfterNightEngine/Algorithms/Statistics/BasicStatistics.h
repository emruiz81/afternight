// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Count, extrema and stable moments over one image channel.
///
/// A statistic is asked of a selection, not inferred from sample values. The selection combines the
/// image's declared validity rule, an optional packed mask when that rule names one, and an optional
/// rectangular region. Exact zero therefore remains data unless `ZeroIsNoData` was stated, and a
/// non-finite sample is rejected unless `FiniteOnly` was stated.
///
/// The region is a view into the source. It keeps the source row and plane strides and the mask stays
/// in full-image coordinates, so a masked region is reduced without compacting samples or building a
/// NaN-filled intermediate image.
///
/// Every result is thread-count independent. Basic statistics routinely feed normalization,
/// rejection and weighting decisions, so this operation takes the stronger guarantee under both
/// `ReductionDeterminism` policies. Its fixed partition and pairwise merge are the same on every
/// admitted width; the selected CPU instruction set remains observable through backend dispatch.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <cstdint>
#include <optional>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named rather than included so a statistics result does not pull the scheduler implementation's
/// include closure into every consumer that stores one.
class ExecutionContext;

/// A rectangular image selection in pixel coordinates.
///
/// The right and bottom edges are exclusive. The complete rectangle must lie within the source
/// image. Either dimension may be zero; that is an empty selection and returns
/// `ErrorCode::EmptySelection` with both counts zero. Coordinates are relative to the source view
/// passed to `computeImageBasicStatistics`.
struct StatisticsRegion
{
    /// First selected column, counted from the source view's left edge.
    std::uint32_t originX = 0u;

    /// First selected row, counted from the source view's top edge.
    std::uint32_t originY = 0u;

    /// Number of selected columns. Zero makes the region empty.
    std::uint32_t width = 0u;

    /// Number of selected rows. Zero makes the region empty.
    std::uint32_t height = 0u;
};

/// Which samples a statistics operation considers.
///
/// One record for every operation on this surface rather than one per operation: what a selection
/// is does not change between counting samples and ranking them, and two spellings of it would be
/// free to drift about whether exact zero is data.
///
/// `validity` is always present. `mask` is required exactly when that descriptor uses
/// `ValidityRule::ExplicitMask`, and otherwise must be null. A shared one-plane mask applies to
/// every image channel; a mask with one plane per image channel selects the matching plane. The
/// mask and optional region are borrowed for one call and may be read concurrently by the worker
/// threads, so the caller must not mutate or destroy them until the call returns.
struct StatisticsSelection
{
    /// The value or mask rule used to decide whether a sample participates.
    ValidityDescriptor validity{};

    /// Full-image packed validity bits, or null when `validity` does not name an explicit mask.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;

    /// The rectangular selection, or empty to consider the whole source view.
    std::optional<StatisticsRegion> region{};
};

/// A conservative forward bound for one accumulator in a statistics result.
///
/// For finite inputs without overflow, `absoluteError` is derived from the maximum number of
/// rounded Float64 additions from any term to the root and an upward-rounded sum of term
/// magnitudes. It bounds accumulation around the operation's published mean; it does not turn a
/// reporting-only result into an arbitrary relative tolerance under cancellation.
struct StatisticsAccumulatorErrorBound
{
    /// Maximum rounded additions from one term to the merged accumulator.
    std::uint64_t maximumAdditionDepth = 0u;

    /// Conservative upper bound on the exact sum of the terms' absolute values.
    double sumAbsoluteTerms = 0.0;

    /// Conservative absolute error around the published accumulator.
    double absoluteError = 0.0;
};

/// Count, extrema and stable population moments of one selected image channel.
///
/// All arithmetic is in Float64. `selection.valid` is the sample count and `selection.total` is the
/// number of pixels in the region before validity is applied. Variance divides by that valid count,
/// not by count minus one, and standard deviation is its non-negative square root. A one-sample
/// selection therefore has zero variance and standard deviation. No value exists for an empty
/// selection; the operation returns `ErrorCode::EmptySelection` instead.
struct BasicStatistics
{
    /// How many samples participated and how many pixels the selected region held.
    ValidityCount selection{};

    /// Smallest selected sample, widened to Float64, which is exact for every resident
    /// representation.
    double minimum = 0.0;

    /// Largest selected sample, widened to Float64 exactly.
    double maximum = 0.0;

    /// Selected samples accumulated through the fixed Float64 tree.
    double sum = 0.0;

    /// `sum / selection.valid`, evaluated once after the first pass is merged.
    double mean = 0.0;

    /// Sum of squared deviations from `mean`, accumulated by a second fixed pass.
    double sumSquaredDeviations = 0.0;

    /// `sumSquaredDeviations / selection.valid`.
    double populationVariance = 0.0;

    /// Non-negative square root of `populationVariance`.
    double populationStandardDeviation = 0.0;

    /// Forward bound for `sum`.
    StatisticsAccumulatorErrorBound sumError{};

    /// Forward bound for `sumSquaredDeviations` around the published `mean`.
    StatisticsAccumulatorErrorBound centeredMomentError{};
};

/// Computes basic statistics for one channel of an image known only at run time.
///
/// The source layout may be planar, interleaved or mixed and may contain row and plane padding. A
/// channel is counted in plane-major order across the image rather than assumed to be a plane. The
/// source, layout-owned geometry, selection mask and region are borrowed for the call and no caller
/// storage is changed. The operation makes one context-accounted temporary allocation bounded by
/// the deterministic reduction limit and checks cancellation at scheduler chunk boundaries.
///
/// Thread-safe with respect to the source and selection, which are only read. Calls using separate
/// execution contexts may run concurrently.
///
/// @param execution the operation policy and engine-context lease.
/// @param sampleType the resident representation the layout's element width describes.
/// @param samples first byte of the image storage. Must not be null and must be aligned for the
///        resident representation.
/// @param layout validated image geometry and strides.
/// @param channelIndex channel to reduce, below `layout.extent().channelCount()`.
/// @param selection validity, optional full-image mask and optional region.
/// @return the complete population statistics and forward bounds; `ErrorCode::EmptySelection`
///         carrying selected and total counts when the region is empty or nothing participates;
///         `ErrorCode::InvalidData` for a selected non-finite value or overflowing accumulator;
///         `ErrorCode::NotInitialized` for an execution that holds nothing; `ErrorCode::Cancelled`
///         or `ErrorCode::DeadlineExceeded` when the operation was stopped;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this build
///         has no dispatch table for; `ErrorCode::InvalidArgument` for a null or misaligned source,
///         an unknown representation, a layout whose element size is not its size, a channel the
///         layout does not have, a region outside the image, or a mask missing under
///         `ValidityRule::ExplicitMask` or supplied under another rule;
///         `ErrorCode::IncompatibleArguments` for a mask that does not cover the image with one
///         shared plane or one plane per channel; or `ErrorCode::MemoryBudgetExceeded` or
///         `ErrorCode::AllocationFailed` when the working memory is refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<BasicStatistics>
computeImageBasicStatistics(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                            const ImageLayout& layout, std::uint32_t channelIndex,
                            const StatisticsSelection& selection = {});

/// Computes basic statistics from a checked typed image view.
///
/// The typed spelling of `computeImageBasicStatistics`. It adds no numeric or validity rule: the
/// representation, sample pointer and layout are read from the view, and the untyped entry point
/// owns every check and computation.
///
/// @tparam SampleT one of the eight resident sample representations.
/// @param execution the operation policy and engine-context lease.
/// @param source the checked read-only image view.
/// @param channelIndex channel to reduce.
/// @param selection validity, optional mask and optional region.
/// @return whatever `computeImageBasicStatistics` reports.
template <ResidentSample SampleT>
[[nodiscard]] Result<BasicStatistics>
computeBasicStatistics(const ExecutionContext& execution, const ImageView<const SampleT>& source,
                       std::uint32_t channelIndex, const StatisticsSelection& selection = {})
{
    return computeImageBasicStatistics(execution, kSampleTypeOf<SampleT>, source.data(), source.layout(), channelIndex,
                                       selection);
}

/// What `computeImageBasicStatistics` and `computeBasicStatistics` do to the image they read.
///
/// A reduction produces numbers and writes no sample, so every metadata object survives untouched
/// and there is no result image whose validity could be claimed. The policy is
/// `ValidityPolicy::Preserve` rather than `ValidityPolicy::Reject` because these statistics are
/// defined over whatever the selection kept: an image with meaningless samples is reduced over the
/// meaningful ones and reports how many there were, rather than being refused.
inline constexpr OperationEffect kBasicStatisticsEffect{.samples = SampleEffect::Reads};
} // namespace an
