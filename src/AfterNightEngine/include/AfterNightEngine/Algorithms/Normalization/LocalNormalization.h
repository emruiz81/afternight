// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Local normalization: sample, model, fit, scale, apply and persist as
/// separate steps.
///
/// A global match brings one frame's level and spread to the reference's with two numbers per
/// channel. That is the right answer for frames whose sky differs by an exposure and a pedestal,
/// and the wrong one for frames whose sky differs by a gradient - a moon that rose during the
/// session, a light dome on one side of the mosaic - because no affine can remove a slope. Local
/// normalization models each frame's background as a smooth surface over a coarse node grid,
/// takes the frame's own surface away, and puts the reference's back:
///
///   normalized(x, y) = k * (sample(x, y) - target(x, y)) + reference(x, y)
///
/// where `target` and `reference` are the two surfaces interpolated between their nodes and `k`
/// is one scale per channel, or a surface of its own when the caller asks for one.
///
/// Each stage is an entry point: a node is sampled, a set of samples becomes one background estimate,
/// the grid is fitted, the scale is estimated, the grids are applied, and the whole set of
/// coefficients is written to and read from one lossless text document. A grid is a span of
/// Float64 node values the caller owns and sized from the shape, so it can live beside a frame for
/// a session or in a document for a year without the engine deciding either.
///
/// Which samples of a frame mean something is the frame's stated validity, exactly as it is for
/// the global match: whether an exact zero is registration's uncovered canvas or ordinary data is
/// `ValidityRule::ZeroIsNoData` against `ValidityRule::AllValid` on the frame view, stated once there.
///
/// A request field outside its stated interval is refused rather than silently clamped. A node no
/// fallback recovers takes the median of its channel's fitted nodes. The interpolation mixes node rows
/// before columns. The scale is estimated from residual ratios only.

#pragma once

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an
{

/// The most cells a local-normalization grid has along either axis.
///
/// Two hundred and fifty-six. A grid holds one more node than cells per axis, so the largest grid has
/// 66,049 nodes per channel.
inline constexpr std::uint32_t kMaxLocalNormalizationGridSize = 256u;

/// The smallest side a background sampling region may be forced to, in pixels.
inline constexpr std::uint32_t kMinLocalNormalizationRoiExtent = 8u;

/// The largest side a background sampling region may be forced to, in pixels.
inline constexpr std::uint32_t kMaxLocalNormalizationRoiExtent = 4096u;

/// The largest fraction of a cell a background sampling region may extend past it on each side.
inline constexpr double kMaxLocalNormalizationRoiOverlap = 0.95;

/// The smallest sigma-clipping multiplier a background estimator accepts.
inline constexpr double kMinLocalNormalizationSigmaClip = 0.5;

/// The largest sigma-clipping multiplier a background estimator accepts.
inline constexpr double kMaxLocalNormalizationSigmaClip = 10.0;

/// The most sigma-clipping iterations a background estimator runs.
inline constexpr std::uint32_t kMaxLocalNormalizationSigmaClipIterations = 10u;

/// The fewest samples one node's region is asked for.
inline constexpr std::uint32_t kMinLocalNormalizationNodeSamples = 256u;

/// The most samples one node's region is asked for.
inline constexpr std::uint32_t kMaxLocalNormalizationNodeSamples = 8192u;

/// One sample per this many region pixels, between the two bounds above.
inline constexpr std::uint32_t kLocalNormalizationSampleDivisor = 16u;

/// The fewest admitted samples a node's estimate is trusted from without a fallback.
inline constexpr std::uint32_t kMinLocalNormalizationValidSamples = 64u;

/// The fewest admitted samples the robust-centre fallback runs over.
inline constexpr std::uint32_t kMinLocalNormalizationRobustCentreSamples = 8u;

/// How many times a region is grown, by one cell a step, when its node has too few samples.
inline constexpr std::uint32_t kLocalNormalizationExpansionSteps = 2u;

/// The smallest side a scale-grid sampling region is forced to, in pixels.
inline constexpr std::uint32_t kMinLocalNormalizationScaleRoiExtent = 32u;

/// The fewest samples one scale-grid node is asked for.
inline constexpr std::uint32_t kMinLocalNormalizationScaleNodeSamples = 64u;

/// The most samples one scale-grid node is asked for.
inline constexpr std::uint32_t kMaxLocalNormalizationScaleNodeSamples = 1024u;

/// The fewest residual ratios a scale is estimated from before it falls back to unity.
inline constexpr std::uint32_t kMinLocalNormalizationScaleRatios = 16u;

/// The smallest scale the residual-ratio estimator reports.
inline constexpr double kMinLocalNormalizationScale = 0.1;

/// The largest scale the residual-ratio estimator reports.
inline constexpr double kMaxLocalNormalizationScale = 10.0;

/// The largest single residual ratio the estimator admits.
inline constexpr double kMaxLocalNormalizationRatio = 100.0;

/// The smallest residual, in the image's own units, that takes part in a ratio.
inline constexpr double kLocalNormalizationResidualEpsilon = 1.0e-6;

/// How many robust deviations a scale-grid residual must clear to take part in a ratio.
inline constexpr double kLocalNormalizationScaleSigmaThreshold = 5.0;

/// The most grids one combination takes the per-node median of.
inline constexpr std::uint32_t kMaxLocalNormalizationGridMembers = 1024u;

/// The most bytes one persisted coefficient document may hold.
inline constexpr std::uint64_t kMaxLocalNormalizationDocumentBytes = 64ull * 1024ull * 1024ull;

/// The most frames one persisted coefficient document may carry.
inline constexpr std::uint32_t kMaxLocalNormalizationDocumentFrames = 100000u;

/// The version of the persisted text form this build writes and the only one it reads.
inline constexpr std::uint32_t kLocalNormalizationDocumentVersion = 1u;

/// How a grid's node values are interpolated between nodes.
enum class LocalGridInterpolation : std::uint8_t
{
    /// The weighted mean of the four nodes around a position.
    Bilinear,

    /// The Catmull-Rom cubic through the sixteen nodes around a position, clamped to their range
    /// so an overshoot between two nodes cannot invent a level neither holds.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Bicubic
};

/// Whether a value names an interpolation this build implements.
///
/// @param interpolation the value to inspect.
/// @return true for both interpolations.
[[nodiscard]] constexpr bool isLocalGridInterpolation(LocalGridInterpolation interpolation) noexcept
{
    return static_cast<std::uint8_t>(interpolation) <= static_cast<std::uint8_t>(LocalGridInterpolation::Bicubic);
}

/// The interpolation's own name.
///
/// @param interpolation the interpolation to name. A value outside the enumeration is named
///        `"UnknownLocalGridInterpolation"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
localGridInterpolationName(LocalGridInterpolation interpolation) noexcept;

/// Which robust centre describes one node's background.
enum class LocalBackgroundEstimator : std::uint8_t
{
    /// A stated percentile of the admitted samples.
    Percentile,

    /// The median of the samples that survive iterated sigma clipping around the median.
    SigmaClippedMedian,

    /// The mode estimate `3 * median - 2 * mean` of the samples that survive that clipping,
    /// clamped to their range.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SigmaClippedMode
};

/// Whether a value names an estimator this build implements.
///
/// @param estimator the value to inspect.
/// @return true for every implemented estimator.
[[nodiscard]] constexpr bool isLocalBackgroundEstimator(LocalBackgroundEstimator estimator) noexcept
{
    return static_cast<std::uint8_t>(estimator) <=
           static_cast<std::uint8_t>(LocalBackgroundEstimator::SigmaClippedMode);
}

/// The estimator's own name.
///
/// @param estimator the estimator to name. A value outside the enumeration is named
///        `"UnknownLocalBackgroundEstimator"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
localBackgroundEstimatorName(LocalBackgroundEstimator estimator) noexcept;

/// Whether a colour frame receives one scale or one per channel.
enum class LocalScaleMode : std::uint8_t
{
    /// One scale, measured on the plan's scale channel and applied to every channel, so a
    /// normalization cannot move a colour balance.
    Linked,

    /// One scale per channel, for a channel set whose members were not exposed alike.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    PerChannel
};

/// Whether a value names a scale mode this build implements.
///
/// @param mode the value to inspect.
/// @return true for both modes.
[[nodiscard]] constexpr bool isLocalScaleMode(LocalScaleMode mode) noexcept
{
    return static_cast<std::uint8_t>(mode) <= static_cast<std::uint8_t>(LocalScaleMode::PerChannel);
}

/// The mode's own name.
///
/// @param mode the mode to name. A value outside the enumeration is named `"UnknownLocalScaleMode"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* localScaleModeName(LocalScaleMode mode) noexcept;

/// How a grid is smoothed in node space.
enum class LocalGridSmoothing : std::uint8_t
{
    /// The mean over a square window of nodes, which is what a scale grid is smoothed with.
    Box,

    /// A separable Gaussian over nodes, which is what a background grid is smoothed with.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Gaussian
};

/// Whether a value names a smoothing this build implements.
///
/// @param smoothing the value to inspect.
/// @return true for both smoothings.
[[nodiscard]] constexpr bool isLocalGridSmoothing(LocalGridSmoothing smoothing) noexcept
{
    return static_cast<std::uint8_t>(smoothing) <= static_cast<std::uint8_t>(LocalGridSmoothing::Gaussian);
}

/// The smoothing's own name.
///
/// @param smoothing the smoothing to name. A value outside the enumeration is named
///        `"UnknownLocalGridSmoothing"`.
/// @return a null-terminated string literal compiled into this library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* localGridSmoothingName(LocalGridSmoothing smoothing) noexcept;

/// The geometry of one node grid over one frame.
///
/// Node values are stored channel-major, then node-row-major: the value of channel `c` at node
/// column `nx` and node row `ny` is at `(c * nodesPerAxis() + ny) * nodesPerAxis() + nx`, which
/// `localNormalizationNodeIndex` spells. A grid describes the frame it was fitted on, so two grids
/// are compatible only when every field agrees.
struct LocalNormalizationGridShape
{
    /// How many cells the grid has along each axis, within `[1, kMaxLocalNormalizationGridSize]`.
    std::uint32_t gridSize = 0u;

    /// How many pixels one row of the frame holds.
    std::uint32_t width = 0u;

    /// How many rows the frame holds.
    std::uint32_t height = 0u;

    /// How many channels the frame holds, within `[1, kMaxNormalizationChannels]`.
    std::uint32_t channelCount = 0u;

    /// How many nodes one axis holds, which is one more than the cell count.
    ///
    /// @return `gridSize + 1`.
    [[nodiscard]] constexpr std::uint32_t nodesPerAxis() const noexcept
    {
        return gridSize + 1u;
    }

    /// How many nodes one channel holds.
    ///
    /// @return `nodesPerAxis()` squared.
    [[nodiscard]] constexpr std::uint32_t nodeCount() const noexcept
    {
        return nodesPerAxis() * nodesPerAxis();
    }

    /// How many values a grid of this shape holds over every channel.
    ///
    /// @return `nodeCount() * channelCount`.
    [[nodiscard]] constexpr std::uint64_t valueCount() const noexcept
    {
        return static_cast<std::uint64_t>(nodeCount()) * channelCount;
    }

    /// Two shapes agree when every field agrees.
    [[nodiscard]] friend constexpr bool operator==(const LocalNormalizationGridShape& left,
                                                   const LocalNormalizationGridShape& right) noexcept = default;
};

/// Whether a shape describes a grid this build can hold.
///
/// Thread-safe and allocation-free.
///
/// @param shape the shape to inspect.
/// @return success; `ErrorCode::OutOfRange` for a grid size outside its range;
///         `ErrorCode::InvalidArgument` for a zero extent or a channel count outside its range.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkLocalNormalizationGridShape(const LocalNormalizationGridShape& shape);

/// Where one node's value sits in a grid of a shape.
///
/// @param shape the grid's geometry.
/// @param channel the channel, below the shape's channel count.
/// @param nodeX the node column, below `nodesPerAxis()`.
/// @param nodeY the node row, below `nodesPerAxis()`.
/// @return the index into the grid's values.
[[nodiscard]] constexpr std::uint64_t localNormalizationNodeIndex(const LocalNormalizationGridShape& shape,
                                                                  std::uint32_t channel, std::uint32_t nodeX,
                                                                  std::uint32_t nodeY) noexcept
{
    return (static_cast<std::uint64_t>(channel) * shape.nodesPerAxis() + nodeY) * shape.nodesPerAxis() + nodeX;
}

/// What a caller asks a local normalization for.
///
/// Every threshold is in the image's own units, as it is everywhere else in this engine; the
/// defaults are meaningful for a normalized floating frame. A value outside its stated interval is
/// refused by `planLocalNormalization`, not clamped into it.
struct LocalNormalizationRequest
{
    /// How many cells the grid has along each axis, within `[1, kMaxLocalNormalizationGridSize]`.
    ///
    /// One behaves like a global normalization with a gradient; larger values follow the sky more
    /// locally and, past the scale of the objects, start following them.
    std::uint32_t gridSize = 4u;

    /// How node values are interpolated between nodes, by the apply and by every estimator that
    /// reads a grid.
    LocalGridInterpolation interpolation = LocalGridInterpolation::Bicubic;

    /// Which robust centre describes one node's background.
    LocalBackgroundEstimator estimator = LocalBackgroundEstimator::Percentile;

    /// The percentile the `LocalBackgroundEstimator::Percentile` estimator reports, within `[0, 1]`.
    double backgroundPercentile = 0.25;

    /// How many robust deviations either side of the median the clipped estimators keep, within
    /// `[kMinLocalNormalizationSigmaClip, kMaxLocalNormalizationSigmaClip]`.
    double sigmaClip = 3.0;

    /// How many times the clipped estimators clip, within
    /// `[1, kMaxLocalNormalizationSigmaClipIterations]`.
    std::uint32_t sigmaClipIterations = 3u;

    /// The smallest side a node's sampling region is forced to, in pixels, within
    /// `[kMinLocalNormalizationRoiExtent, kMaxLocalNormalizationRoiExtent]`.
    std::uint32_t backgroundMinRoiExtent = 32u;

    /// How far past its own cell, on each side, a node's region reaches as a fraction of the cell,
    /// within `[0, kMaxLocalNormalizationRoiOverlap]`.
    double backgroundRoiOverlap = 0.5;

    /// The radius, in nodes, of the Gaussian smoothing the fitted background grid receives, or zero
    /// for none.
    std::uint32_t backgroundSmoothingRadius = 0u;

    /// The lowest sample value that takes part in any estimate. Must be finite and below
    /// `highClip`.
    double lowClip = 4.5e-5;

    /// The value at and above which a sample takes no part in any estimate. Must be finite.
    double highClip = 0.85;

    /// Whether a colour frame receives one scale or one per channel.
    LocalScaleMode scaleMode = LocalScaleMode::Linked;

    /// How many positions the residual-ratio scale estimator samples per channel, at least one.
    ///
    /// The scale-grid estimator divides the same number over its nodes.
    std::uint32_t scaleSamples = 8192u;

    /// The radius, in nodes, of the box smoothing a fitted scale grid receives, or zero for none.
    std::uint32_t localScaleSmoothingRadius = 1u;
};

/// A validated local-normalization request over one frame geometry, with everything the geometry
/// decides resolved.
///
/// Trivially copyable and allocation-free.
class LocalNormalizationPlan
{
public:
    /// A plan describing no normalization, which no execution accepts.
    LocalNormalizationPlan() noexcept = default;

    /// Whether this plan was built by a validated request.
    ///
    /// @return false only for a default-constructed plan.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_valid;
    }

    /// What was asked for.
    ///
    /// @return the request this plan was derived from.
    [[nodiscard]] const LocalNormalizationRequest& request() const noexcept
    {
        return m_request;
    }

    /// The geometry every grid of this plan has.
    ///
    /// @return the shape.
    [[nodiscard]] const LocalNormalizationGridShape& shape() const noexcept
    {
        return m_shape;
    }

    /// How many pixels wide one cell is, rounded up.
    ///
    /// @return the cell width, at least one.
    [[nodiscard]] std::uint32_t cellWidth() const noexcept
    {
        return m_cellWidth;
    }

    /// How many pixels tall one cell is, rounded up.
    ///
    /// @return the cell height, at least one.
    [[nodiscard]] std::uint32_t cellHeight() const noexcept
    {
        return m_cellHeight;
    }

    /// How many pixels wide one node's background sampling region is.
    ///
    /// @return the region width, at least one and at most the frame's.
    [[nodiscard]] std::uint32_t roiWidth() const noexcept
    {
        return m_roiWidth;
    }

    /// How many pixels tall one node's background sampling region is.
    ///
    /// @return the region height, at least one and at most the frame's.
    [[nodiscard]] std::uint32_t roiHeight() const noexcept
    {
        return m_roiHeight;
    }

    /// How many positions of its region one node is asked for.
    ///
    /// @return the count, at most the region's area.
    [[nodiscard]] std::uint32_t samplesPerNode() const noexcept
    {
        return m_samplesPerNode;
    }

    /// The most positions one node's expanded region is asked for by the low-sample fallback.
    ///
    /// @return the count, which bounds the per-worker scratch.
    [[nodiscard]] std::uint32_t maxExpandedSamplesPerNode() const noexcept
    {
        return m_maxExpandedSamples;
    }

    /// How many pixels wide one scale-grid node's sampling region is.
    ///
    /// @return the region width.
    [[nodiscard]] std::uint32_t scaleRoiWidth() const noexcept
    {
        return m_scaleRoiWidth;
    }

    /// How many pixels tall one scale-grid node's sampling region is.
    ///
    /// @return the region height.
    [[nodiscard]] std::uint32_t scaleRoiHeight() const noexcept
    {
        return m_scaleRoiHeight;
    }

    /// How many positions one scale-grid node is asked for.
    ///
    /// @return the count, at most the region's area.
    [[nodiscard]] std::uint32_t samplesPerScaleNode() const noexcept
    {
        return m_samplesPerScaleNode;
    }

    /// Which channel a linked scale is measured on.
    ///
    /// Green for a three-channel frame and the first channel otherwise; under
    /// `LocalScaleMode::PerChannel` every channel is its own.
    ///
    /// @return the channel index.
    [[nodiscard]] std::uint32_t scaleChannel() const noexcept
    {
        return m_scaleChannel;
    }

    /// The pixel column a node column sits on.
    ///
    /// Nodes are spread evenly from the first column to the last, so the corners of the grid are
    /// the corners of the frame.
    ///
    /// @param nodeX the node column, below the shape's `nodesPerAxis()`.
    /// @return the column, below the frame's width.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t nodeX(std::uint32_t nodeX) const noexcept;

    /// The pixel row a node row sits on.
    ///
    /// @param nodeY the node row, below the shape's `nodesPerAxis()`.
    /// @return the row, below the frame's height.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t nodeY(std::uint32_t nodeY) const noexcept;

    /// The first column of a node's background sampling region.
    ///
    /// The region is centred on the node and slid inward where it would leave the frame.
    ///
    /// @param nodeX the node column.
    /// @return the region's first column.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t roiOriginX(std::uint32_t nodeX) const noexcept;

    /// The first row of a node's background sampling region.
    ///
    /// @param nodeY the node row.
    /// @return the region's first row.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t roiOriginY(std::uint32_t nodeY) const noexcept;

    /// How many bytes of engine memory each worker of a background fit holds.
    ///
    /// The number a caller checks against a budget before starting. The fit takes this many per
    /// planned worker, plus one byte per node per channel and one Float64 per node of one channel,
    /// each rounded up to the buffer alignment, in one block it releases before smoothing; smoothing,
    /// when asked, then holds what `smoothLocalGrid` holds.
    ///
    /// @return the byte count.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t fitWorkingBytesPerWorker() const noexcept;

private:
    /// Builds the only validated shape this type may hold.
    friend AFTERNIGHTENGINE_API Result<LocalNormalizationPlan>
    planLocalNormalization(const LocalNormalizationRequest& request, std::uint32_t width, std::uint32_t height,
                           std::uint32_t channelCount);

    /// The request this plan records.
    LocalNormalizationRequest m_request{};

    /// The geometry every grid of this plan has.
    LocalNormalizationGridShape m_shape{};

    /// How many pixels wide one cell is.
    std::uint32_t m_cellWidth = 0u;

    /// How many pixels tall one cell is.
    std::uint32_t m_cellHeight = 0u;

    /// How many pixels wide a background sampling region is.
    std::uint32_t m_roiWidth = 0u;

    /// How many pixels tall a background sampling region is.
    std::uint32_t m_roiHeight = 0u;

    /// How many positions one node is asked for.
    std::uint32_t m_samplesPerNode = 0u;

    /// The most positions an expanded region is asked for.
    std::uint32_t m_maxExpandedSamples = 0u;

    /// How many pixels wide a scale-grid sampling region is.
    std::uint32_t m_scaleRoiWidth = 0u;

    /// How many pixels tall a scale-grid sampling region is.
    std::uint32_t m_scaleRoiHeight = 0u;

    /// How many positions one scale-grid node is asked for.
    std::uint32_t m_samplesPerScaleNode = 0u;

    /// Which channel a linked scale is measured on.
    std::uint32_t m_scaleChannel = 0u;

    /// Whether this plan was built by a validated request.
    bool m_valid = false;
};

/// Validates a request against one frame geometry.
///
/// Thread-safe and allocation-free. It reads no image and takes no execution.
///
/// @param request the grid, the estimators, the clip window and the scale policy.
/// @param width how many pixels one row of the frame holds. Must not be zero.
/// @param height how many rows the frame holds. Must not be zero.
/// @param channelCount how many channels, within `[1, kMaxNormalizationChannels]`.
/// @return the plan; `ErrorCode::InvalidArgument` for a field outside its enumeration, a clip
///         window with a non-finite bound or a low bound not below its high bound, a zero
///         `scaleSamples`, a zero extent or a channel count outside its range;
///         `ErrorCode::OutOfRange` for a grid size, percentile, sigma-clipping multiplier or
///         iteration count, region side or overlap outside its stated interval, a non-finite one
///         included. The grid size, iteration count and region side carry the value and the limit
///         it crossed; the three floating fields carry fixed integers in place of both.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalNormalizationPlan>
planLocalNormalization(const LocalNormalizationRequest& request, std::uint32_t width, std::uint32_t height,
                       std::uint32_t channelCount);

/// Gathers the samples one node's background is estimated from.
///
/// The sampling stage on its own. `plan.samplesPerNode()` positions of the node's region are
/// visited in a deterministic order that depends only on the geometry and the node, and every
/// admitted sample - one the frame's validity claim accepts and that lies in
/// `[lowClip, highClip)`, which a non-finite value never does - is written to the front of
/// `samples` in that order. It is how a caller sees what a node was estimated from: the fit gathers
/// exactly these samples, in this order, before any fallback.
///
/// Thread-safe with respect to `execution` and the frame, both of which are only read. Runs on the
/// calling thread and checks cancellation once.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked frame, whose extent and channel count must be the plan's.
/// @param plan the validated request.
/// @param nodeX the node column, below the shape's `nodesPerAxis()`.
/// @param nodeY the node row, below the shape's `nodesPerAxis()`.
/// @param channel the channel to sample, below the shape's channel count.
/// @param samples where the admitted samples go, of at least `plan.samplesPerNode()` entries.
/// @return how many samples were written; `ErrorCode::NotInitialized` for an empty execution or
///         an unvalidated plan; `ErrorCode::InvalidArgument` for an empty view or an index outside
///         the grid; `ErrorCode::IncompatibleArguments` when the frame is not the plan's geometry
///         or `samples` is too short; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::uint32_t>
sampleLocalBackgroundNode(const ExecutionContext& execution, const NormalizationFrameView& frame,
                          const LocalNormalizationPlan& plan, std::uint32_t nodeX, std::uint32_t nodeY,
                          std::uint32_t channel, std::span<double> samples);

/// Estimates one node's background from its samples.
///
/// The model stage on its own: the requested estimator over the samples exactly as
/// `fitLocalBackgroundGrid` applies it after the bright-tail rejection, with no fallback. The
/// percentile is the sample at rank `round(p * (n - 1))`, not interpolated. The clipped estimators
/// keep, per pass, the samples within `sigmaClip` robust deviations - median absolute deviations
/// scaled to a standard deviation - of the median, and stop early when a pass removes nothing,
/// would leave fewer than eight samples, or starts from fewer than thirty-two samples or from ones
/// with no deviation. The samples are
/// rearranged by the estimate and `scratch` is overwritten.
///
/// Thread-safe, allocation-free and takes no execution.
///
/// @param request the estimator, the percentile and the clipping policy.
/// @param samples the admitted samples. Must not be empty.
/// @param scratch working room at least as long as `samples`.
/// @return the estimate; `ErrorCode::InvalidArgument` for an unknown estimator, no samples, a
///         percentile or clipping field outside its range, or scratch that is too short;
///         `ErrorCode::InvalidData` when a sample is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double>
estimateLocalBackground(const LocalNormalizationRequest& request, std::span<double> samples, std::span<double> scratch);

/// What one background-grid fit did.
struct LocalBackgroundFitResult
{
    /// How many nodes were fitted per channel.
    std::uint32_t nodeCount = 0u;

    /// How many channels were fitted.
    std::uint32_t channelCount = 0u;

    /// How many frame samples the sampling visited, admitted or not, summed over nodes, channels
    /// and expanded regions.
    std::uint64_t samplesRead = 0u;

    /// Node estimates whose region admitted fewer than `kMinLocalNormalizationValidSamples`
    /// samples and went through the fallbacks.
    std::uint32_t lowSampleNodes = 0u;

    /// Node estimates whose samples had a bright tail rejected before the estimator ran.
    std::uint32_t sourceRejectionNodes = 0u;

    /// Node estimates recovered by growing the region.
    std::uint32_t expansionFallbacks = 0u;

    /// Node estimates recovered by a robust centre over the few samples the region had.
    std::uint32_t robustCentreFallbacks = 0u;

    /// Node estimates recovered from the median of already-fitted neighbours.
    std::uint32_t neighbourFallbacks = 0u;

    /// Node estimates that no fallback could recover and that took the channel's median node.
    ///
    /// Zero there would be the uncovered canvas's own value and would drag the interpolated background
    /// toward zero at the covered edge beside it.
    std::uint32_t unresolvedNodes = 0u;

    /// Whether the fitted grid was smoothed.
    bool smoothed = false;

    /// Context memory held during the call and released before it returned, in bytes.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;
};

/// Fits the background grid of one frame.
///
/// Every node of every channel is sampled and estimated independently on the operation's threads,
/// so the answer does not depend on how many there were; the neighbour fallback then runs once in
/// node order over the fitted values. A node that admits too few samples grows its region by a cell
/// at a time, then takes a robust centre of what it has, then the median of its fitted neighbours,
/// and finally the median of the channel's fitted nodes; each recovery is counted. The grid is
/// smoothed afterwards with the Gaussian of
/// `backgroundSmoothingRadius` when the request asks.
///
/// Before a node's estimator runs on at least 128 samples, they are sorted and, when the largest
/// gap between neighbours within the lowest two fifths reaches both a fifth of their whole range
/// and 1e-5, only the samples below that gap are kept, which is what stops a star's wing from
/// lifting the level.
/// The robust centre is a two-pass, three-sigma clipped median over at least
/// `kMinLocalNormalizationRobustCentreSamples` samples, whatever estimator was asked for.
///
/// Thread-safe with respect to `execution` and the frame, both of which are only read. Runs on the
/// threads the operation was admitted, checks cancellation at every node row, reports progress per
/// node row, and is not failure-atomic with respect to `values`.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked frame, whose extent and channel count must be the plan's.
/// @param plan the validated request.
/// @param values where the grid goes, of exactly the shape's `valueCount()` entries.
/// @return what the fit did; `ErrorCode::NotInitialized` for an empty execution or an unvalidated
///         plan; `ErrorCode::InvalidArgument` for an empty view; `ErrorCode::IncompatibleArguments`
///         when the frame is not the plan's geometry or `values` is not the grid's size;
///         `ErrorCode::EmptySelection` when a channel fits no node at all; `ErrorCode::InvalidData`
///         when an estimator produced a non-finite level; a memory-budget refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalBackgroundFitResult>
fitLocalBackgroundGrid(const ExecutionContext& execution, const NormalizationFrameView& frame,
                       const LocalNormalizationPlan& plan, std::span<double> values);

/// Smooths a grid in node space, in place.
///
/// The radius is clamped to the grid size. A window is truncated at the grid's edge and
/// renormalized over the nodes it covers. The Gaussian is separable, with a standard deviation of
/// three quarters of the radius in nodes and no less than one half.
///
/// Thread-safe with respect to `execution`. Runs on the calling thread and holds two channels'
/// worth of nodes and the Gaussian's taps in engine memory for the call. Checks cancellation
/// before work and at bounded node intervals, and reports progress after each channel is
/// committed. On cancellation, completed channels may have changed; the current and remaining
/// channels are unchanged. Temporary memory is released on every return.
///
/// @param execution the operation policy and engine-context lease.
/// @param shape the grid's geometry.
/// @param values the grid, of exactly the shape's `valueCount()` entries.
/// @param radius how many nodes either side of a node the window reaches. Zero smooths nothing.
/// @param smoothing which window.
/// @return success; `ErrorCode::NotInitialized` for an empty execution; whatever
///         `checkLocalNormalizationGridShape` refuses; `ErrorCode::InvalidArgument` for an
///         unknown smoothing; `ErrorCode::IncompatibleArguments` for values of the wrong size;
///         `ErrorCode::InvalidData` for a non-finite node; a memory-budget refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Status smoothLocalGrid(const ExecutionContext& execution,
                                                          const LocalNormalizationGridShape& shape,
                                                          std::span<double> values, std::uint32_t radius,
                                                          LocalGridSmoothing smoothing);

/// Combines grids of one shape by the per-node median.
///
/// What builds an integrated reference from several frames' backgrounds: each node of the result is
/// the median of that node over the members, with an even count averaging the two middle values.
///
/// Thread-safe, allocation-free and takes no execution.
///
/// @param shape the geometry every member has.
/// @param members the grids, each of exactly the shape's `valueCount()` entries; between one and
///        `kMaxLocalNormalizationGridMembers` of them.
/// @param combined where the result goes, of exactly the shape's `valueCount()` entries. It may be
///        one of the members.
/// @return success; whatever `checkLocalNormalizationGridShape` refuses; `ErrorCode::InvalidArgument`
///         for no members; `ErrorCode::LimitExceeded` for too many; `ErrorCode::IncompatibleArguments`
///         for a member or result of the wrong size; `ErrorCode::InvalidData` for a non-finite node.
[[nodiscard]] AFTERNIGHTENGINE_API Status combineLocalGrids(const LocalNormalizationGridShape& shape,
                                                            std::span<const std::span<const double>> members,
                                                            std::span<double> combined);

/// Interpolates a grid at one position.
///
/// The position is clamped to the frame, so a coordinate outside it reads the nearest edge. The
/// arithmetic is exactly the arithmetic the apply uses for that position, so a caller that
/// evaluates a grid by hand and a caller that applies it see the same number. Node rows are mixed
/// before node columns.
///
/// Thread-safe, allocation-free and takes no execution.
///
/// @param shape the grid's geometry.
/// @param values the grid, of exactly the shape's `valueCount()` entries.
/// @param interpolation how nodes are interpolated.
/// @param channel the channel, below the shape's channel count.
/// @param x the column in pixel indices, zero being the first. Node column `n` sits at
///        `n * (width - 1) / gridSize`, which `LocalNormalizationPlan::nodeX` rounds to the pixel it
///        samples around.
/// @param y the row, on the same convention.
/// @return the interpolated value; whatever `checkLocalNormalizationGridShape` refuses;
///         `ErrorCode::InvalidArgument` for an unknown interpolation, a channel outside the shape or
///         a non-finite position; `ErrorCode::IncompatibleArguments` for values of the wrong size.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> evaluateLocalGrid(const LocalNormalizationGridShape& shape,
                                                                    std::span<const double> values,
                                                                    LocalGridInterpolation interpolation,
                                                                    std::uint32_t channel, double x, double y);

/// One frame's scale against the reference, per channel.
struct LocalScaleEstimate
{
    /// How many leading entries of the arrays are meaningful.
    std::uint32_t channelCount = 0u;

    /// The factor each channel's residuals are multiplied by to match the reference's.
    std::array<double, kMaxNormalizationChannels> scale{};

    /// How many residual ratios each channel's estimate was the robust median of.
    std::array<std::uint32_t, kMaxNormalizationChannels> ratiosUsed{};

    /// Channels whose ratio count fell below `kMinLocalNormalizationScaleRatios` and reported unity.
    std::uint32_t unityFallbacks = 0u;

    /// Under `LocalScaleMode::Linked`, the channel the one scale was measured on.
    std::uint32_t measuredChannel = 0u;

    /// Context memory held during the call and released before it returned, in bytes.
    std::uint64_t workingBytes = 0u;
};

/// Estimates one frame's scale against the reference from residual ratios.
///
/// At each of `scaleSamples` deterministic positions per channel, capped at the pixel count, where
/// both frames admit the sample under their validity and both samples lie in
/// `[lowClip, highClip)`, both frames' residuals from their own background grids are formed; where
/// both exceed `kLocalNormalizationResidualEpsilon` and the ratio of the reference's to the
/// target's lies in `(0, kMaxLocalNormalizationRatio]`, the ratio is kept. The scale is the median
/// of the ratios within six median absolute deviations of their median, clamped to
/// `[kMinLocalNormalizationScale, kMaxLocalNormalizationScale]`; fewer than
/// `kMinLocalNormalizationScaleRatios` ratios report unity and are counted. Under
/// `LocalScaleMode::Linked` the plan's scale channel is measured and every channel receives it.
///
/// This operation offers the residual-ratio route alone; it estimates no photometric scale from
/// detected stars.
///
/// Thread-safe with respect to `execution` and the frames, which are only read. Runs on the calling
/// thread and checks cancellation before each channel it measures.
///
/// @param execution the operation policy and engine-context lease.
/// @param target the frame being normalized.
/// @param targetBackground the target's fitted grid, of the plan's shape.
/// @param reference the frame it is normalized to, of the same geometry.
/// @param referenceBackground the reference's fitted grid, of the plan's shape.
/// @param plan the validated request.
/// @return the estimate; `ErrorCode::NotInitialized` for an empty execution or an unvalidated
///         plan; `ErrorCode::InvalidArgument` for an empty view; `ErrorCode::IncompatibleArguments`
///         when a frame is not the plan's geometry or a grid is not its size;
///         `ErrorCode::InvalidData` for a non-finite node; a memory-budget refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalScaleEstimate>
estimateLocalScale(const ExecutionContext& execution, const NormalizationFrameView& target,
                   std::span<const double> targetBackground, const NormalizationFrameView& reference,
                   std::span<const double> referenceBackground, const LocalNormalizationPlan& plan);

/// What one scale-grid fit did.
struct LocalScaleGridResult
{
    /// How many nodes were fitted per channel.
    std::uint32_t nodeCount = 0u;

    /// How many channels were fitted.
    std::uint32_t channelCount = 0u;

    /// Node estimates that had too few residuals or ratios and took the channel's global scale.
    std::uint32_t confidenceFallbacks = 0u;

    /// Channels whose grid was rescaled so its median node equals the global scale.
    std::uint32_t normalizedChannels = 0u;

    /// Whether the fitted grid was smoothed.
    bool smoothed = false;

    /// Context memory held during the call and released before it returned, in bytes.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation could actually use.
    std::uint32_t plannedWorkers = 0u;
};

/// Fits a scale surface `k(x, y)` from residual ratios around each node.
///
/// Each node samples `plan.samplesPerScaleNode()` positions of its own region deterministically,
/// keeps the residual pairs that both clear `kLocalNormalizationScaleSigmaThreshold` robust
/// deviations of the larger of the two frames' residual spreads, and takes the robust median ratio
/// exactly as `estimateLocalScale` does. A node with fewer than
/// `kMinLocalNormalizationValidSamples` residuals, or fewer ratios than the larger of 48 and a
/// sixteenth of its residuals (never more than its sample count), takes the global scale and is
/// counted. The grid is then
/// box-smoothed by the request's radius, each channel is rescaled so its median node equals its
/// global scale and clamped to the scale range, and the fallback nodes are written back to the
/// global scale so smoothing cannot move them. Under `LocalScaleMode::Linked` a node is measured
/// once on the plan's scale channel and every channel receives that value, or its own global scale
/// where the node fell back.
///
/// Thread-safe with respect to `execution` and the frames, which are only read. Runs on the threads
/// the operation was admitted, checks cancellation at every node row, reports progress per node
/// row, and is not failure-atomic with respect to `values`.
///
/// @param execution the operation policy and engine-context lease.
/// @param target the frame being normalized.
/// @param targetBackground the target's fitted grid, of the plan's shape.
/// @param reference the frame it is normalized to, of the same geometry.
/// @param referenceBackground the reference's fitted grid, of the plan's shape.
/// @param plan the validated request.
/// @param globalScale one scale per channel, which is what a low-confidence node falls back to.
/// @param values where the grid goes, of exactly the shape's `valueCount()` entries.
/// @return what the fit did; `ErrorCode::NotInitialized` for an empty execution or an unvalidated
///         plan; `ErrorCode::InvalidArgument` for an empty view or a non-positive global scale;
///         `ErrorCode::IncompatibleArguments` when a frame is not the plan's geometry or a span is
///         not its size; `ErrorCode::InvalidData` for a non-finite node; a memory-budget refusal;
///         or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalScaleGridResult>
fitLocalScaleGrid(const ExecutionContext& execution, const NormalizationFrameView& target,
                  std::span<const double> targetBackground, const NormalizationFrameView& reference,
                  std::span<const double> referenceBackground, const LocalNormalizationPlan& plan,
                  std::span<const double> globalScale, std::span<double> values);

/// Everything one apply needs, borrowed from the caller.
struct LocalNormalizationApplyRequest
{
    /// The geometry every grid below has, and the frame's.
    LocalNormalizationGridShape shape{};

    /// How nodes are interpolated.
    LocalGridInterpolation interpolation = LocalGridInterpolation::Bicubic;

    /// The frame's own fitted background, of exactly the shape's `valueCount()` entries.
    std::span<const double> targetBackground{};

    /// The reference's fitted background, of the same size.
    std::span<const double> referenceBackground{};

    /// One finite positive scale per channel, of exactly the shape's channel count.
    std::span<const double> scale{};

    /// A scale surface of the shape's size with every node finite and positive, or empty to apply
    /// `scale` uniformly. When present it replaces `scale` in the arithmetic, and `scale` is still
    /// checked and is what the transition records.
    std::span<const double> scaleGrid{};
};

/// What one completed local apply reports.
struct LocalNormalizationApplyResult
{
    /// The implementation and instruction set that actually wrote the samples.
    BackendSelection backend{};

    /// Whether the scalar oracle or a compiled row ran.
    NormalizationExecutionPath path = NormalizationExecutionPath::Reference;

    /// Samples written, transformed or copied.
    std::uint64_t samplesWritten = 0u;

    /// Samples the validity rule admitted and the transform was applied to.
    std::uint64_t samplesTransformed = 0u;

    /// Samples the validity rule excluded and copied unchanged.
    std::uint64_t samplesPreserved = 0u;

    /// Transformed samples that landed exactly on the zero marker under
    /// `ValidityRule::ZeroIsNoData` and were moved to the smallest normal magnitude.
    std::uint64_t noDataCollisions = 0u;

    /// Workers the scheduler could use for the row tasks.
    std::uint32_t plannedWorkers = 0u;

    /// Full destination passes, always one.
    std::uint32_t imagePasses = 0u;

    /// Context memory held during the call and released before it returned, in bytes.
    std::uint64_t workingBytes = 0u;
};

/// The published effect of applying a local normalization.
///
/// It preserves geometry, astrometry, source cards and the frame's validity claim, and restates
/// the processing meaning as normalized.
inline constexpr OperationEffect kLocalNormalizationEffect{.samples = SampleEffect::Writes,
                                                           .semantics = SemanticEffect::Restates,
                                                           .validity =
                                                               ValidityTransition{.policy = ValidityPolicy::Preserve}};

/// Writes one frame through its grids.
///
/// Every admitted sample becomes `k * (sample - target) + reference`, with the two surfaces and,
/// when a scale grid is given, `k` interpolated at the sample's position; the arithmetic is Float64
/// and narrowed once to the resident type. Along each row the interpolation is evaluated a cell at a
/// time as a polynomial of the column fraction, so no sample gathers sixteen nodes; the per-column
/// fractions and per-row node mixes are the only working memory. Every sample the validity rule
/// excludes is copied unchanged, so a no-data marker survives, and a transformed sample that lands
/// exactly on zero under `ValidityRule::ZeroIsNoData` is moved to the smallest normal magnitude
/// and counted. The destination shares the source's planar or interleaved arrangement, and is
/// either the source itself with the source's own strides or storage disjoint from it. Rows under
/// a value rule run the selected CPU table's compiled row when that instruction set has one; a
/// packed mask, the reference backend or an instruction set without a compiled row runs the scalar
/// oracle. Both store the same bits, and the result names the path that ran.
///
/// Thread-safe with respect to `execution`, the request and the source, all of which are only read.
/// Runs on the threads the operation was admitted, checks cancellation at row-block boundaries,
/// reports progress per row block, and is not failure-atomic.
///
/// @param execution the operation policy and engine-context lease.
/// @param request the grids, the scale and the interpolation.
/// @param source the checked frame, of the request's geometry. `ValidityRule::DomainRange` is
///        refused, because the stated range describes the untransformed values.
/// @param destination where output storage starts, or `source.samples()` for an in-place write.
/// @param destinationBytes bytes available from `destination`, covering every byte named by
///        `destinationLayout`.
/// @param destinationLayout where the output samples sit, describing the same image as the source.
/// @return execution counters and the actual backend; `ErrorCode::NotInitialized` without a lease;
///         whatever `checkLocalNormalizationGridShape` refuses; `ErrorCode::InvalidArgument` for an
///         empty view, an unknown interpolation, a non-finite background node, a non-positive or
///         non-finite scale or scale-grid node, or null, misaligned or undersized destination
///         storage; `ErrorCode::IncompatibleArguments` when a span is not the shape's size or the
///         frame is not the shape's geometry, or the destination layout's extent, element size or
///         arrangement does not describe the frame; `ErrorCode::Unsupported` for a range rule;
///         `ErrorCode::Aliasing` for an overlap other than an exact in-place write;
///         `ErrorCode::UnsupportedBackend` when the execution requires the CPU backend with
///         fallback forbidden and only the scalar oracle can serve the call; a memory-budget
///         refusal; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the execution
///         stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalNormalizationApplyResult>
applyLocalNormalization(const ExecutionContext& execution, const LocalNormalizationApplyRequest& request,
                        const NormalizationFrameView& source, void* destination, std::uint64_t destinationBytes,
                        const ImageLayout& destinationLayout);

/// Builds the metadata transition for a locally normalized result.
///
/// The new meaning marks the frame as normalized and changes nothing else about it, and provenance
/// records the grid size, the interpolation, every channel's scale and whether a scale surface was
/// applied, so a later caller never infers them from the pixels.
///
/// @param request the grids and scale that were applied.
/// @param source the frame's current processing meaning.
/// @return the checked transition ready for `Image::transition`; whatever
///         `checkLocalNormalizationGridShape` refuses; `ErrorCode::InvalidArgument` for an unknown
///         interpolation or a scale span of the wrong size, or whatever rebuilding the semantics or
///         provenance reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
localNormalizationTransition(const LocalNormalizationApplyRequest& request, const ImageSemantics& source);

/// One frame's coefficients as a persisted document carries them.
///
/// The background grid is the frame's own; the scale is per channel; the scale grid is present
/// only when one was fitted; and the global match, when present, is the one estimated over the
/// locally normalized samples, which is how a combined local-plus-global normalization is persisted
/// as one record.
struct LocalNormalizationFrameCoefficients
{
    /// Which frame of the set these belong to.
    std::uint32_t frameIndex = 0u;

    /// One scale per channel, the rest left at one.
    std::array<double, kMaxNormalizationChannels> scale{1.0, 1.0, 1.0, 1.0};

    /// The frame's fitted background, of the document shape's `valueCount()` entries.
    std::vector<double> background{};

    /// The frame's fitted scale surface, of the same size, or empty.
    std::vector<double> scaleGrid{};

    /// The global match estimated after the local apply, or nothing under the local-only mode.
    std::optional<FrameNormalization> global{};
};

/// A set of frames' local-normalization coefficients, owned.
///
/// What `formatLocalNormalizationDocument` writes and `parseLocalNormalizationDocument` reads. The
/// text form is line-oriented `key=value` tokens, lossless for every number, and strict: every
/// field exactly once - a frame's scale grid at most once - every grid exactly its size, no value
/// that is not the shape its field takes. It is the engine's own form.
struct LocalNormalizationDocument
{
    /// The request every grid was fitted under.
    LocalNormalizationRequest request{};

    /// The geometry every grid has.
    LocalNormalizationGridShape shape{};

    /// The validity rule every frame stated when its grid was fitted, which is what a consumer
    /// checks the frame it applies to against.
    ValidityRule validity = ValidityRule::AllValid;

    /// The frame that is the reference and is not to be normalized, or nothing when every frame is.
    /// When present it is the `frameIndex` of one of `frames`.
    std::optional<std::uint32_t> referenceIndex{};

    /// The reference's fitted background, of the shape's `valueCount()` entries.
    std::vector<double> referenceBackground{};

    /// Each frame's coefficients, in strictly increasing `frameIndex` order; between one and
    /// `kMaxLocalNormalizationDocumentFrames` of them.
    std::vector<LocalNormalizationFrameCoefficients> frames{};
};

/// Writes a coefficient set as one lossless text document.
///
/// Every number is written through the shortest decimal that reads back as the same Float64, so a
/// document parsed and formatted again is byte-identical.
///
/// Thread-safe. Allocates the returned string and nothing else.
///
/// @param document the coefficients to write.
/// @return the text; whatever `checkLocalNormalizationGridShape` or `planLocalNormalization`
///         refuses for the shape and request; `ErrorCode::IncompatibleArguments` when the request
///         and the shape disagree in grid size, or for a grid or scale grid of the wrong size;
///         `ErrorCode::InvalidArgument` for an unknown validity rule, no frames, frame indices not
///         strictly increasing, a reference index naming no frame, or a global match naming an
///         unknown method or estimator or of the wrong channel count; `ErrorCode::InvalidData` for
///         a non-finite number, a scale or scale-grid node that is not positive, or a global scale
///         of zero; `ErrorCode::LimitExceeded` when the set has more than
///         `kMaxLocalNormalizationDocumentFrames` frames or the text would exceed
///         `kMaxLocalNormalizationDocumentBytes`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::string>
formatLocalNormalizationDocument(const LocalNormalizationDocument& document);

/// Reads a coefficient set from the text `formatLocalNormalizationDocument` writes.
///
/// A line may end in a carriage return and a line feed; an empty line, a global match that does
/// not follow its own frame's line, and a frame count other than the header's are refused.
///
/// Thread-safe. Allocates the returned document.
///
/// @param documentText the document, as the string a caller holds it in.
/// @return the coefficients; `ErrorCode::LimitExceeded` for text beyond
///         `kMaxLocalNormalizationDocumentBytes`; `ErrorCode::InvalidData` for text that is not this
///         form, another version, a field missing, repeated or unknown, a number that does not read
///         back finite, a grid that is not the shape's size, a scale that is not positive, or a
///         set the same checks as formatting would refuse. Each failure carries a line number in
///         its `ContextKey::Completed` field: the line at fault, or the last line for a check of the
///         whole set.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalNormalizationDocument>
parseLocalNormalizationDocument(const std::string& documentText);

} // namespace an
