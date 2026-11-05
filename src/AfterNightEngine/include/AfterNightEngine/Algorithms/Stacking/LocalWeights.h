// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The per-pixel weight surface a stack builds from each frame's local
/// signal-to-noise ratio.
///
/// Every frame is measured on a lattice of nodes - the mean, the variance and the mean gradient of
/// the luminance in a window around each node - the ratio becomes a quality, each node is judged
/// across the frames against the median level and the median quality, and the finished weights are
/// sampled bilinearly once per pixel per frame.
///
/// The lattice is a value the model fixes - `LocalWeightGridShape` from the input extent and the
/// request's spacing, never one coarsened to fit a memory estimate - so the planner counts the grids
/// and the measurement workspace before a frame is read, and the working set holds both.
/// `measureLocalWeightGrid` runs the measurement of one frame over that workspace into that frame's
/// grid, on the operation's threads, admitting pixels by the frame's own validity rule.
/// `finalizeLocalWeightGrids` judges every node across the frames in place, so the grids the
/// executor reads are the weights and nothing is allocated for the answer. `sampleLocalWeight` is
/// the bilinear read, and `fillLocalWeightBand` is the band of it that `StackFrameBand::localWeights`
/// carries into the accumulators and the column kernels. The luminance, the derivative and the
/// window products are computed in Float64.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlan.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// How many Float32 values one node of one frame holds: the level and the quality while the
/// grids are measured, the finished weight and the quality afterwards.
inline constexpr std::uint32_t kLocalWeightNodeValues = 2u;

static_assert(kLocalWeightNodeValues * sizeof(float) == kLocalWeightNodeBytes,
              "a grid node is what the planner counts it as");

/// How many integral images the measurement of one frame holds: the admitted count, the
/// luminance, its square and its gradient magnitude, each one Float64 per cell.
inline constexpr std::uint64_t kLocalWeightIntegralImages = 4u;

/// The bytes the finalization holds per frame while it judges one node: the levels, the
/// qualities, a ranked copy of each, the deviations, and an index.
inline constexpr std::uint64_t kLocalWeightFinalizeBytesPerFrame = 48u;

/// The fewest admitted pixels a window needs for its node to be measured.
inline constexpr std::uint64_t kMinimumLocalWeightNodeSamples = 3u;

/// The variance floor, added before the square root so a flat window has a finite ratio.
inline constexpr double kLocalWeightNoiseFloor = 1.0e-6;

/// The share of the mean gradient magnitude added to the mean level as the signal.
inline constexpr double kLocalWeightGradientShare = 0.5;

/// The lowest signal-to-noise ratio admitted before it is raised to the exponent.
inline constexpr double kLocalWeightRatioLowest = 1.0e-3;

/// The highest signal-to-noise ratio admitted before it is raised to the exponent.
inline constexpr double kLocalWeightRatioHighest = 1.0e6;

/// The lowest a node's quality may fall relative to the median quality across the frames.
inline constexpr double kLocalWeightQualityRatioLowest = 0.25;

/// The highest a node's quality may rise relative to the median quality across the frames.
inline constexpr double kLocalWeightQualityRatioHighest = 1.0;

/// The factor that scales the levels' median absolute deviation to a sigma: the normal consistency
/// constant to four places.
inline constexpr double kLocalWeightDeviationToSigma = 1.4826;

/// The width of the consistency term, in scaled median absolute deviations of the level.
inline constexpr double kLocalWeightConsistencyWidth = 2.0;

/// The absolute floor of the levels' scaled deviation.
inline constexpr double kLocalWeightSigmaFloor = 0.01;

/// The floor of the levels' scaled deviation as a fraction of the median level's magnitude.
inline constexpr double kLocalWeightSigmaFraction = 0.02;

/// The smallest finished weight, which keeps every frame in the stack at every pixel.
inline constexpr double kLocalWeightLowest = 0.1;

/// The largest finished weight.
inline constexpr double kLocalWeightHighest = 1.0;

/// The fewest frames a node needs measured before it is judged; below it every frame keeps one.
inline constexpr std::uint32_t kMinimumLocalWeightActiveFrames = 2u;

/// The lattice a frame is measured on.
///
/// Nodes sit at the first pixel and every `spacing` pixels after it, with one more at the far edge
/// when the extent does not land on a node, as `localWeightGridNodesAlong` counts them; a node's
/// pixel is the smaller of its index times the spacing and the last pixel. A plain aggregate, so a
/// shape can be stated in a constant expression and compared.
struct LocalWeightGridShape
{
    /// The frame's width, in pixels.
    std::uint32_t width = 0u;

    /// The frame's height, in pixels.
    std::uint32_t height = 0u;

    /// The node spacing, in pixels.
    std::uint32_t spacing = 0u;

    /// How many nodes lie along a row.
    std::uint32_t nodesX = 0u;

    /// How many nodes lie down a column.
    std::uint32_t nodesY = 0u;

    /// The lattice of a frame of the stated extent at the stated spacing.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param width the frame's width, at least one.
    /// @param height the frame's height, at least one.
    /// @param spacing the node spacing, at least `kMinimumLocalWeightGridSpacing`.
    /// @return the shape, or nothing for an empty extent or a spacing below the minimum.
    [[nodiscard]] static constexpr std::optional<LocalWeightGridShape> create(std::uint32_t width, std::uint32_t height,
                                                                              std::uint32_t spacing) noexcept
    {
        if (width == 0u || height == 0u || spacing < kMinimumLocalWeightGridSpacing)
        {
            return std::nullopt;
        }
        return LocalWeightGridShape{.width = width,
                                    .height = height,
                                    .spacing = spacing,
                                    .nodesX = static_cast<std::uint32_t>(localWeightGridNodesAlong(width, spacing)),
                                    .nodesY = static_cast<std::uint32_t>(localWeightGridNodesAlong(height, spacing))};
    }

    /// The lattice a checked stack measures its frames on.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param model the checked stack.
    /// @return the shape from the input extent and the request's spacing, or nothing when the
    ///         stack applies no local weighting.
    [[nodiscard]] static constexpr std::optional<LocalWeightGridShape> of(const StackModel& model) noexcept
    {
        if (!model.usesLocalWeights())
        {
            return std::nullopt;
        }
        const ImageExtent& extent = model.request().input.descriptor.extent();
        return create(extent.width(), extent.height(), model.request().localWeighting.gridSpacing);
    }

    /// How many nodes the lattice has.
    ///
    /// @return `nodesX * nodesY`.
    [[nodiscard]] constexpr std::uint64_t nodeCount() const noexcept
    {
        return static_cast<std::uint64_t>(nodesX) * nodesY;
    }

    /// How many Float32 values one frame's grid holds.
    ///
    /// @return `nodeCount() * kLocalWeightNodeValues`.
    [[nodiscard]] constexpr std::uint64_t valuesPerFrame() const noexcept
    {
        return nodeCount() * kLocalWeightNodeValues;
    }

    /// The pixel column of a node.
    ///
    /// @param nodeX the node's index along the row, below `nodesX`.
    /// @return the smaller of `nodeX * spacing` and the last column.
    [[nodiscard]] constexpr std::uint32_t nodeColumn(std::uint32_t nodeX) const noexcept
    {
        const std::uint64_t column = static_cast<std::uint64_t>(nodeX) * spacing;
        return column < width ? static_cast<std::uint32_t>(column) : width - 1u;
    }

    /// The pixel row of a node.
    ///
    /// @param nodeY the node's index down the column, below `nodesY`.
    /// @return the smaller of `nodeY * spacing` and the last row.
    [[nodiscard]] constexpr std::uint32_t nodeRow(std::uint32_t nodeY) const noexcept
    {
        const std::uint64_t row = static_cast<std::uint64_t>(nodeY) * spacing;
        return row < height ? static_cast<std::uint32_t>(row) : height - 1u;
    }

    /// Whether two shapes describe the same lattice.
    ///
    /// @param left one shape.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const LocalWeightGridShape& left,
                                                   const LocalWeightGridShape& right) noexcept = default;
};

/// The bytes the measurement pass needs beside one whole frame and the grids.
///
/// The larger of the four integral images over `(width + 1) * (height + 1)` cells and the
/// finalization's per-frame scratch, because the two are never held at once: every frame is
/// measured before any node is judged. This is what the planner reserves as the measurement
/// workspace of the `MeasureLocalWeights` pass and what the working set hands out during it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param shape the lattice.
/// @param frames how many frames the stack has.
/// @return the byte count.
[[nodiscard]] constexpr std::uint64_t localWeightMeasurementWorkspaceBytes(const LocalWeightGridShape& shape,
                                                                           std::uint32_t frames) noexcept
{
    const std::uint64_t integral = (static_cast<std::uint64_t>(shape.width) + 1u) *
                                   (static_cast<std::uint64_t>(shape.height) + 1u) * kLocalWeightIntegralImages *
                                   sizeof(double);
    const std::uint64_t finalize = static_cast<std::uint64_t>(frames) * kLocalWeightFinalizeBytesPerFrame;
    return integral > finalize ? integral : finalize;
}

/// One whole frame as the measurement reads it, borrowed for the call.
struct LocalWeightFrameSource
{
    /// What the samples are: the model's input representation, read as it is resident, or the
    /// tile width the source path widens it to, read as the combine will read it.
    SampleType sampleType = SampleType::Float32;

    /// The first byte of the frame's samples, never null.
    const void* samples = nullptr;

    /// Where the samples sit: the model's input extent, planar, interleaved or mixed, at the
    /// element width of `sampleType`. Never null.
    const ImageLayout* layout = nullptr;

    /// Which of the samples mean something: the frame's rule and, under the explicit-mask rule,
    /// its packed mask with one shared bit-plane or one per channel.
    StackBandValidity validity{};
};

/// What measuring one frame found.
struct LocalWeightMeasurement
{
    /// How many pixels the frame has.
    std::uint64_t totalPixels = 0u;

    /// How many of them the validity rule admitted on every channel the luminance reads.
    std::uint64_t admittedPixels = 0u;

    /// How many nodes the lattice has.
    std::uint64_t nodeCount = 0u;

    /// How many of them had at least `kMinimumLocalWeightNodeSamples` admitted pixels in their
    /// window and were measured; the others hold a quiet NaN level and a zero quality.
    std::uint64_t measuredNodes = 0u;
};

/// Measures one frame's level and quality at every node of the lattice.
///
/// The luminance is the Rec. 709 combination of the first three channels of a frame with at least
/// three, and the first channel otherwise; a pixel is admitted when the rule admits every channel
/// the luminance reads. The gradient is the three-tap derivative's magnitude over the luminance
/// with unadmitted pixels read as zero and the borders reflected. Four integral images over the
/// workspace then give, for the window around each node clipped to the frame, the admitted count,
/// the mean level, the variance and the mean gradient; the signal is the non-negative mean plus
/// `kLocalWeightGradientShare` of the mean gradient, the noise the square root of the variance
/// plus `kLocalWeightNoiseFloor`, and the quality the ratio, clamped to
/// `[kLocalWeightRatioLowest, kLocalWeightRatioHighest]`, raised to the request's exponent. A
/// quality that is not finite in double - an exponent large enough to carry the clamped ratio
/// past the range of a double, or a window sum that was not finite - is written as one. The grid
/// receives the level and the quality of each node, row-major, `kLocalWeightNodeValues` per node.
///
/// The integral images are built by rows and then by columns on the operation's threads and every
/// node from them independently, so the bits are the same at every thread count. Nothing is
/// allocated: the workspace is the caller's, at least `localWeightMeasurementWorkspaceBytes` of
/// the model's lattice and frame count, aligned for a double.
///
/// Thread-safe with respect to `execution` and the frame, which are only read; the workspace and
/// the grid are written and must not be shared. Allocates only a failure it has to describe.
///
/// Reports progress once, complete over the node count, when the grid is written.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack, which applies a local weighting.
/// @param frame the frame, at the model's input representation or its tile width, and at the
///        model's extent.
/// @param workspace the integral images' bytes.
/// @param grid receives the frame's `valuesPerFrame()` values.
/// @return what was measured; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint;
///         `ErrorCode::IncompatibleArguments` when the model applies no local weighting, the
///         frame's representation is neither the model's input representation nor its tile width,
///         its layout is not the model's input extent at that element width, its mask does not
///         cover the frame with one shared bit-plane or one per channel, the workspace is short, or
///         the grid is not one frame's, naming the parameter; `ErrorCode::InvalidArgument` for a
///         null pointer, misaligned samples or workspace, a mask under a rule that reads none, or
///         no mask under the rule that needs one; and `ErrorCode::InvalidData` when an admitted
///         sample is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalWeightMeasurement>
measureLocalWeightGrid(const ExecutionContext& execution, const StackModel& model, const LocalWeightFrameSource& frame,
                       std::span<std::byte> workspace, std::span<float> grid);

/// Judges every node across the frames and writes each frame's weight over its level, in place.
///
/// A frame is active at a node when its level is finite and its quality positive. At a node with
/// at least `kMinimumLocalWeightActiveFrames` active frames, the rule is: the median
/// level and the median quality across them; a sigma of `kLocalWeightDeviationToSigma` times
/// the median absolute deviation of the levels, floored at the larger of
/// `kLocalWeightSigmaFloor` and `kLocalWeightSigmaFraction` of the median level's magnitude; each
/// frame's quality over the median quality clamped to
/// `[kLocalWeightQualityRatioLowest, kLocalWeightQualityRatioHighest]`, times a Gaussian of the
/// level's residual over `kLocalWeightConsistencyWidth` sigmas, clamped to
/// `[kLocalWeightLowest, kLocalWeightHighest]`. Every other entry - an inactive frame at a judged
/// node, and every frame at a node with too few - is weight one. The quality stays in its slot.
///
/// Nodes are judged in row-major order on the calling thread, each from the workspace, which is
/// the caller's - at least `localWeightMeasurementWorkspaceBytes` of the model's lattice and frame
/// count, aligned for a double. Nothing is allocated. Cancellation is checked before the first node
/// and every 64 nodes after it; an informational diagnostic summarizing the weights and one
/// complete progress report follow the last node.
///
/// Thread-safe with respect to `execution`, which is only read; the grids and the workspace are
/// written and must not be shared. Allocates only a failure it has to describe.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack, which applies a local weighting.
/// @param grids every frame's grid, frame-major, as `StackWorkingSet::localWeightGrids` holds them.
/// @param workspace the per-frame scratch's bytes.
/// @return the summary; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint, after
///         which the grids hold a partial surface; `ErrorCode::IncompatibleArguments` when the
///         model applies no local weighting, the grids are not one per frame of the model, or the
///         workspace is short, naming the parameter; and `ErrorCode::InvalidArgument` for a
///         misaligned workspace.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalWeightingSummary>
finalizeLocalWeightGrids(const ExecutionContext& execution, const StackModel& model, std::span<float> grids,
                         std::span<std::byte> workspace);

/// The surface's weight at a pixel of one frame.
///
/// The bilinear interpolation of the four surrounding nodes' weights: the pixel is clamped to
/// the frame, the node pair along each axis chosen by the spacing, and the fraction taken
/// between the nodes' pixel positions so the edge node's clamped position counts.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param shape the lattice.
/// @param grid one frame's finalized grid, `shape.valuesPerFrame()` values; a grid of any other
///        length, or a shape with an empty extent, spacing or lattice, reads as one, the neutral
///        weight.
/// @param x the pixel's column, clamped to the frame.
/// @param y the pixel's row, clamped to the frame.
/// @return the weight.
[[nodiscard]] AFTERNIGHTENGINE_API float sampleLocalWeight(const LocalWeightGridShape& shape,
                                                           std::span<const float> grid, double x, double y) noexcept;

/// Writes the surface's weights over a band of rows, as `StackFrameBand::localWeights` carries them.
///
/// One `sampleLocalWeight` per pixel, row-major over the band, so a band handed to the
/// accumulators or the column kernels applies exactly the per-pixel read.
///
/// Thread-safe, reentrant and allocation-free except for a failure it has to describe.
///
/// @param shape the lattice.
/// @param grid one frame's finalized grid.
/// @param firstRow the band's first row.
/// @param rows how many rows the band has, at least one.
/// @param weights receives `rows * shape.width` values.
/// @return success; `ErrorCode::IncompatibleArguments` when the grid is not one frame's or the
///         weights are not the band's, naming the parameter; `ErrorCode::OutOfRange` when the
///         band ends past the frame or has no rows.
[[nodiscard]] AFTERNIGHTENGINE_API Status fillLocalWeightBand(const LocalWeightGridShape& shape,
                                                              std::span<const float> grid, std::uint32_t firstRow,
                                                              std::uint32_t rows, std::span<float> weights);

} // namespace an
