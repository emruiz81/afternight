// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - measuring, finishing and reading the local weight surface.
//
// Measuring one frame is five passes over the caller's workspace, each a `parallelFor` that
// finishes before the next begins: every row's luminance and admission into the raw cells of the
// integral images; every row's gradient, which reads the raw luminance of the rows above and
// below; the running sum along every row, which overwrites those raw cells in place and so waits
// for every gradient; the running sum down every column; and every node's window from the
// finished images. Each cell is written by one worker and every sum runs in a fixed order, so the
// grid is the same at every worker count. The finalization judges the nodes in row-major order on
// the calling thread, from one set of per-frame arrays in the same workspace.

#include <AfterNightEngine/Algorithms/Stacking/LocalWeights.h>

#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/stacking/StackHistory.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>

namespace an
{
namespace
{

using stacking::failure;
using stacking::pointerIsAligned;
using stacking::refuse;

constexpr const char* kMeasure = "an::measureLocalWeightGrid";
constexpr const char* kFinalize = "an::finalizeLocalWeightGrids";
constexpr const char* kFill = "an::fillLocalWeightBand";
constexpr const char* kMeasureStage = "measuring local weights";
constexpr const char* kFinalizeStage = "finalizing local weights";

/// The Rec. 709 luminance coefficients three channels are combined with.
constexpr double kRec709Red = 0.2126;
constexpr double kRec709Green = 0.7152;
constexpr double kRec709Blue = 0.0722;

/// How many channels the luminance reads of a frame with at least that many.
constexpr std::uint32_t kLuminanceChannels = 3u;

/// How many nodes the finalization judges between cancellation checkpoints.
constexpr std::uint64_t kFinalizeCheckpointStride = 64u;

/// What `std::snprintf` wants a sixty-four-bit count as.
using Count = unsigned long long;

/// A frame checked against the model: its shape and how its mask is laid out.
struct FrameGeometry
{
    /// The frame's width, in pixels.
    std::uint32_t width = 0u;
    /// The frame's height, in pixels.
    std::uint32_t height = 0u;
    /// The frame's channel count.
    std::uint32_t channels = 0u;
    /// How many channels the luminance reads: three for a frame with at least three, else one.
    std::uint32_t luminanceChannels = 1u;
    /// The mask's bit-planes: zero under a rule that reads no mask, else one shared or one per
    /// channel.
    std::uint32_t maskPlanes = 0u;
};

/// The four integral images over the workspace, `(width + 1) * (height + 1)` cells each.
struct IntegralImages
{
    /// The admitted count; raw cells hold one or zero before the running sums.
    double* count = nullptr;
    /// The luminance, zero where not admitted.
    double* sum = nullptr;
    /// The squared luminance.
    double* sumSquares = nullptr;
    /// The gradient magnitude.
    double* gradient = nullptr;
    /// Cells per integral row: the frame's width plus one.
    std::uint64_t stride = 0u;

    /// The cell of one image at an integral row and column.
    [[nodiscard]] std::uint64_t cell(std::uint64_t row, std::uint64_t column) const noexcept
    {
        return row * stride + column;
    }
};

/// Checks that a model applies a local weighting and returns its lattice.
///
/// @param model the checked stack.
/// @param operation the name carried by a refusal.
/// @return the shape, or the refusal.
[[nodiscard]] Result<LocalWeightGridShape> shapeOf(const StackModel& model, const char* operation)
{
    const std::optional<LocalWeightGridShape> shape = LocalWeightGridShape::of(model);
    if (!shape.has_value())
    {
        return refuse(ErrorCode::IncompatibleArguments, operation, "model",
                      "model applies no local weighting, so there is no surface to measure or finish");
    }
    return *shape;
}

/// Checks a workspace for its size and alignment.
///
/// @param workspace the caller's bytes.
/// @param needed how many the operation needs.
/// @param operation the name carried by a refusal.
/// @return success, or the refusal.
[[nodiscard]] Status checkWorkspace(std::span<std::byte> workspace, std::uint64_t needed, const char* operation)
{
    if (workspace.size() < needed)
    {
        return failure(ErrorCode::IncompatibleArguments, operation,
                       "workspace holds fewer bytes than the measurement of this stack needs",
                       {StatusContextField::ofText(ContextKey::Parameter, "workspace"),
                        StatusContextField::ofCount(ContextKey::Requested, workspace.size()),
                        StatusContextField::ofCount(ContextKey::Limit, needed)});
    }
    if (!pointerIsAligned(workspace.data(), alignof(double)))
    {
        return refuse(ErrorCode::InvalidArgument, operation, "workspace",
                      "workspace does not begin on a boundary a double may be stored at");
    }
    return Status::success();
}

/// Checks one frame against the model it is measured for.
///
/// @param model the checked stack.
/// @param frame the frame.
/// @return the geometry, or the first refusal.
[[nodiscard]] Result<FrameGeometry> checkFrame(const StackModel& model, const LocalWeightFrameSource& frame)
{
    const ImageDescriptor& descriptor = model.request().input.descriptor;
    if (frame.samples == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kMeasure, "frame.samples", "frame.samples is null");
    }
    if (frame.layout == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kMeasure, "frame.layout", "frame.layout is null");
    }
    if (frame.sampleType != descriptor.sampleType() &&
        frame.sampleType != stackTileSampleTypeOf(descriptor.sampleType()))
    {
        return refuse(ErrorCode::IncompatibleArguments, kMeasure, "frame.sampleType",
                      "frame.sampleType is neither the representation the stack's input is described as nor the "
                      "tile width the source path widens it to");
    }
    const ImageExtent& extent = frame.layout->extent();
    const ImageExtent& expected = descriptor.extent();
    if (extent.width() != expected.width() || extent.height() != expected.height() ||
        extent.channelCount() != expected.channelCount())
    {
        return refuse(ErrorCode::IncompatibleArguments, kMeasure, "frame.layout",
                      "frame.layout does not describe the stack's input extent");
    }
    if (frame.layout->elementSizeBytes() != sampleTypeSizeBytes(frame.sampleType))
    {
        return refuse(ErrorCode::IncompatibleArguments, kMeasure, "frame.layout",
                      "frame.layout does not describe elements of the frame's representation");
    }
    if (!pointerIsAligned(frame.samples, sampleTypeSizeBytes(frame.sampleType)))
    {
        return refuse(ErrorCode::InvalidArgument, kMeasure, "frame.samples",
                      "frame.samples does not begin on a boundary its representation may be read at");
    }

    FrameGeometry geometry;
    geometry.width = extent.width();
    geometry.height = extent.height();
    geometry.channels = extent.channelCount();
    geometry.luminanceChannels = geometry.channels >= kLuminanceChannels ? kLuminanceChannels : 1u;
    const ValidityDescriptor& rule = frame.validity.descriptor;
    if (!rule.needsMask())
    {
        if (frame.validity.mask != nullptr)
        {
            return refuse(ErrorCode::InvalidArgument, kMeasure, "frame.validity.mask",
                          "frame.validity.mask was supplied under a rule that reads no mask");
        }
        return geometry;
    }
    if (frame.validity.mask == nullptr)
    {
        return refuse(ErrorCode::InvalidArgument, kMeasure, "frame.validity.mask",
                      "frame.validity.mask is required by the explicit-mask rule and none was given");
    }
    const ValidityMaskView<const std::uint8_t>& mask = *frame.validity.mask;
    if (mask.width() != geometry.width || mask.height() != geometry.height ||
        (mask.planeCount() != 1u && mask.planeCount() != geometry.channels))
    {
        return refuse(ErrorCode::IncompatibleArguments, kMeasure, "frame.validity.mask",
                      "frame.validity.mask does not cover the frame with one shared bit-plane or one per channel");
    }
    geometry.maskPlanes = mask.planeCount();
    return geometry;
}

/// Reflects an index into an extent at the borders, without repeating the edge pixel.
///
/// @param index the index, possibly one outside.
/// @param extent the axis length, at least one.
/// @return the reflected index.
[[nodiscard]] std::int64_t reflect101(std::int64_t index, std::int64_t extent) noexcept
{
    if (extent == 1)
    {
        return 0;
    }
    while (index < 0 || index >= extent)
    {
        index = index < 0 ? -index : 2 * extent - 2 - index;
    }
    return index;
}

/// Whether a pixel's mask bit admits it on one plane.
///
/// @param mask the packed bits.
/// @param plane which bit-plane.
/// @param x the column.
/// @param y the row.
/// @return true when the bit is set.
[[nodiscard]] bool maskAdmits(const ValidityMaskView<const std::uint8_t>& mask, std::uint32_t plane, std::uint32_t x,
                              std::uint32_t y) noexcept
{
    const std::uint8_t* const row = mask.data() + static_cast<std::uint64_t>(plane) * mask.planeStrideBytes() +
                                    static_cast<std::uint64_t>(y) * mask.rowStrideBytes();
    return ((row[x / 8u] >> (x % 8u)) & 1u) != 0u;
}

/// Reads the luminance of a row into the raw cells of the integral images.
///
/// The admitted flag goes to the count image and the luminance - zero where not admitted - to the
/// sum image, at the cell one row and one column in, so the prefix passes can run in place.
///
/// @tparam SampleT the frame's resident representation.
/// @param frame the frame.
/// @param geometry its checked shape.
/// @param images the integral images.
/// @param y the row.
/// @param admitted receives how many pixels of the row were admitted, added to.
/// @return false when an admitted pixel's luminance was not finite, as it is whenever a sample it
///         reads is not.
template <typename SampleT>
[[nodiscard]] bool readLuminanceRow(const LocalWeightFrameSource& frame, const FrameGeometry& geometry,
                                    const IntegralImages& images, std::uint32_t y, std::uint64_t& admitted) noexcept
{
    const ValidityDescriptor& rule = frame.validity.descriptor;
    const bool byValue = validityRuleIsDecidedByValue(rule.rule());
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(frame.samples);
    const SampleT* rows[kLuminanceChannels] = {nullptr, nullptr, nullptr};
    for (std::uint32_t channel = 0u; channel < geometry.luminanceChannels; ++channel)
    {
        rows[channel] =
            reinterpret_cast<const SampleT*>(bytes + plane_detail::channelRowByteOffset(*frame.layout, channel, y));
    }
    const std::uint32_t stride = frame.layout->samplesPerPixel();
    const std::uint64_t base = images.cell(static_cast<std::uint64_t>(y) + 1u, 1u);
    bool finite = true;
    for (std::uint32_t x = 0u; x < geometry.width; ++x)
    {
        bool valid = true;
        double values[kLuminanceChannels] = {0.0, 0.0, 0.0};
        for (std::uint32_t channel = 0u; channel < geometry.luminanceChannels; ++channel)
        {
            const SampleT sample = rows[channel][static_cast<std::uint64_t>(x) * stride];
            if (byValue && !sampleIsValidByValue(rule, sample))
            {
                valid = false;
            }
            if (geometry.maskPlanes != 0u)
            {
                const std::uint32_t plane = geometry.maskPlanes == 1u ? 0u : channel;
                valid = valid && maskAdmits(*frame.validity.mask, plane, x, y);
            }
            values[channel] = static_cast<double>(sample);
        }
        double luminance = 0.0;
        if (valid)
        {
            luminance = geometry.luminanceChannels == kLuminanceChannels
                            ? kRec709Red * values[0] + kRec709Green * values[1] + kRec709Blue * values[2]
                            : values[0];
            finite = finite && std::isfinite(luminance);
            ++admitted;
        }
        images.count[base + x] = valid ? 1.0 : 0.0;
        images.sum[base + x] = luminance;
    }
    return finite;
}

/// Computes one row's gradient magnitudes and squared luminance from the raw cells.
///
/// The gradient is the magnitude of the three-by-three Sobel derivative of the luminance, an
/// unadmitted pixel reading as zero and the borders reflected without repeating the edge pixel;
/// an unadmitted pixel's own gradient is zero.
///
/// @param geometry the frame's shape.
/// @param images the integral images, holding raw luminance in the sum image.
/// @param y the row.
void computeGradientRow(const FrameGeometry& geometry, const IntegralImages& images, std::uint32_t y) noexcept
{
    const std::int64_t width = geometry.width;
    const std::int64_t height = geometry.height;
    const auto luminance = [&](std::int64_t x, std::int64_t row) noexcept {
        return images.sum[images.cell(static_cast<std::uint64_t>(reflect101(row, height)) + 1u,
                                      static_cast<std::uint64_t>(reflect101(x, width)) + 1u)];
    };
    const std::int64_t row = y;
    const std::uint64_t base = images.cell(static_cast<std::uint64_t>(y) + 1u, 1u);
    for (std::int64_t x = 0; x < width; ++x)
    {
        const double gx = -luminance(x - 1, row - 1) + luminance(x + 1, row - 1) - 2.0 * luminance(x - 1, row) +
                          2.0 * luminance(x + 1, row) - luminance(x - 1, row + 1) + luminance(x + 1, row + 1);
        const double gy = -luminance(x - 1, row - 1) - 2.0 * luminance(x, row - 1) - luminance(x + 1, row - 1) +
                          luminance(x - 1, row + 1) + 2.0 * luminance(x, row + 1) + luminance(x + 1, row + 1);
        const std::uint64_t index = base + static_cast<std::uint64_t>(x);
        const double value = images.sum[index];
        images.sumSquares[index] = value * value;
        images.gradient[index] = images.count[index] != 0.0 ? std::sqrt(gx * gx + gy * gy) : 0.0;
    }
}

/// Turns one raw row of every image into its running sum along the row.
///
/// @param images the integral images.
/// @param y the frame row.
/// @param width the frame's width.
void prefixRow(const IntegralImages& images, std::uint32_t y, std::uint32_t width) noexcept
{
    const std::uint64_t base = images.cell(static_cast<std::uint64_t>(y) + 1u, 0u);
    for (double* image : {images.count, images.sum, images.sumSquares, images.gradient})
    {
        image[base] = 0.0;
        double running = 0.0;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            running += image[base + 1u + x];
            image[base + 1u + x] = running;
        }
    }
}

/// Adds each integral column's rows downwards, for a range of columns.
///
/// @param images the integral images.
/// @param first the first integral column.
/// @param last one past the last.
/// @param height the frame's height.
void prefixColumns(const IntegralImages& images, std::uint64_t first, std::uint64_t last, std::uint32_t height) noexcept
{
    for (double* image : {images.count, images.sum, images.sumSquares, images.gradient})
    {
        for (std::uint32_t y = 1u; y <= height; ++y)
        {
            const std::uint64_t above = images.cell(static_cast<std::uint64_t>(y) - 1u, 0u);
            const std::uint64_t row = images.cell(y, 0u);
            for (std::uint64_t column = first; column < last; ++column)
            {
                image[row + column] += image[above + column];
            }
        }
    }
}

/// The sum of one integral image over a window.
///
/// @param image the integral image.
/// @param images the geometry.
/// @param x0 the first column.
/// @param y0 the first row.
/// @param x1 one past the last column.
/// @param y1 one past the last row.
/// @return the window's sum.
[[nodiscard]] double windowSum(const double* image, const IntegralImages& images, std::uint64_t x0, std::uint64_t y0,
                               std::uint64_t x1, std::uint64_t y1) noexcept
{
    return image[images.cell(y1, x1)] - image[images.cell(y1, x0)] - image[images.cell(y0, x1)] +
           image[images.cell(y0, x0)];
}

/// Measures the frame of one representation.
///
/// @tparam SampleT the frame's resident representation.
/// @param execution the operation.
/// @param model the checked stack.
/// @param shape the lattice.
/// @param frame the frame.
/// @param geometry its checked shape.
/// @param images the integral images over the workspace.
/// @param grid the frame's grid.
/// @param measurement receives the counts.
/// @return success, or the first failure.
template <typename SampleT>
[[nodiscard]] Status measureAs(const ExecutionContext& execution, const StackModel& model,
                               const LocalWeightGridShape& shape, const LocalWeightFrameSource& frame,
                               const FrameGeometry& geometry, const IntegralImages& images, std::span<float> grid,
                               LocalWeightMeasurement& measurement)
{
    // The integral row above the frame is zero, so every window's subtraction reads a defined cell.
    for (double* image : {images.count, images.sum, images.sumSquares, images.gradient})
    {
        std::fill(image, image + images.stride, 0.0);
    }

    std::atomic<std::uint64_t> admitted{0u};
    const TaskCost rowCost{.bytesPerItem = static_cast<std::uint64_t>(geometry.width) * 40u,
                           .operationsPerItem = static_cast<std::uint64_t>(geometry.width) * 8u};
    Status status = parallelFor(execution, geometry.height, rowCost, [&](TaskRange range) -> Status {
        std::uint64_t local = 0u;
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            if (!readLuminanceRow<SampleT>(frame, geometry, images, static_cast<std::uint32_t>(y), local))
            {
                return failure(ErrorCode::InvalidData, kMeasure,
                               "an admitted sample is not finite, which a validity rule should exclude");
            }
        }
        admitted.fetch_add(local, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    const TaskCost gradientCost{.bytesPerItem = static_cast<std::uint64_t>(geometry.width) * 96u,
                                .operationsPerItem = static_cast<std::uint64_t>(geometry.width) * 24u};
    status = parallelFor(execution, geometry.height, gradientCost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            computeGradientRow(geometry, images, static_cast<std::uint32_t>(y));
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    const TaskCost prefixCost{.bytesPerItem = static_cast<std::uint64_t>(geometry.width) * 64u,
                              .operationsPerItem = static_cast<std::uint64_t>(geometry.width) * 4u};
    status = parallelFor(execution, geometry.height, prefixCost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            prefixRow(images, static_cast<std::uint32_t>(y), geometry.width);
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    const TaskCost columnCost{.bytesPerItem = static_cast<std::uint64_t>(geometry.height) * 64u,
                              .operationsPerItem = static_cast<std::uint64_t>(geometry.height) * 4u};
    status = parallelFor(execution, images.stride, columnCost, [&](TaskRange range) -> Status {
        prefixColumns(images, range.begin, range.end, geometry.height);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    const LocalWeightingRequest& request = model.request().localWeighting;
    const std::int64_t half = request.windowSize / 2u;
    const double exponent = request.snrExponent;
    std::atomic<std::uint64_t> measured{0u};
    const TaskCost nodeCost{.bytesPerItem = 128u, .operationsPerItem = 64u};
    status = parallelFor(execution, shape.nodeCount(), nodeCost, [&](TaskRange range) -> Status {
        std::uint64_t local = 0u;
        for (std::uint64_t node = range.begin; node < range.end; ++node)
        {
            const std::uint32_t nodeX = static_cast<std::uint32_t>(node % shape.nodesX);
            const std::uint32_t nodeY = static_cast<std::uint32_t>(node / shape.nodesX);
            const std::int64_t centreX = shape.nodeColumn(nodeX);
            const std::int64_t centreY = shape.nodeRow(nodeY);
            const std::uint64_t x0 = static_cast<std::uint64_t>((std::max<std::int64_t>)(0, centreX - half));
            const std::uint64_t y0 = static_cast<std::uint64_t>((std::max<std::int64_t>)(0, centreY - half));
            const std::uint64_t x1 =
                static_cast<std::uint64_t>((std::min<std::int64_t>)(geometry.width, centreX + half + 1));
            const std::uint64_t y1 =
                static_cast<std::uint64_t>((std::min<std::int64_t>)(geometry.height, centreY + half + 1));
            float* const values = grid.data() + node * kLocalWeightNodeValues;
            const double count = windowSum(images.count, images, x0, y0, x1, y1);
            if (count < static_cast<double>(kMinimumLocalWeightNodeSamples))
            {
                values[0] = std::numeric_limits<float>::quiet_NaN();
                values[1] = 0.0f;
                continue;
            }
            const double mean = windowSum(images.sum, images, x0, y0, x1, y1) / count;
            const double meanSquares = windowSum(images.sumSquares, images, x0, y0, x1, y1) / count;
            const double variance = (std::max)(0.0, meanSquares - mean * mean);
            const double gradient = windowSum(images.gradient, images, x0, y0, x1, y1) / count;
            const double signal = (std::max)(mean, 0.0) + kLocalWeightGradientShare * gradient;
            const double noise = std::sqrt(variance + kLocalWeightNoiseFloor);
            double quality = std::pow(std::clamp(signal / (noise + kLocalWeightNoiseFloor), kLocalWeightRatioLowest,
                                                 kLocalWeightRatioHighest),
                                      exponent);
            // A large enough exponent carries the clamped ratio past the range of a double; such a
            // quality, or one a non-finite window sum left undefined, is written as one.
            if (!std::isfinite(quality))
            {
                quality = 1.0;
            }
            values[0] = static_cast<float>(mean);
            values[1] = static_cast<float>(quality);
            ++local;
        }
        measured.fetch_add(local, std::memory_order_relaxed);
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }
    measurement.admittedPixels = admitted.load(std::memory_order_relaxed);
    measurement.measuredNodes = measured.load(std::memory_order_relaxed);
    return Status::success();
}

/// The exact median of a non-empty range, reordering it.
///
/// @param values the values.
/// @param count how many, at least one.
/// @return the middle value, or the mean of the two middle values for an even count.
[[nodiscard]] double medianOf(double* values, std::uint32_t count) noexcept
{
    const std::uint32_t middle = count / 2u;
    std::nth_element(values, values + middle, values + count);
    if ((count % 2u) != 0u)
    {
        return values[middle];
    }
    return (*(std::max_element)(values, values + middle) + values[middle]) * 0.5;
}

} // namespace

Result<LocalWeightMeasurement> measureLocalWeightGrid(const ExecutionContext& execution, const StackModel& model,
                                                      const LocalWeightFrameSource& frame,
                                                      std::span<std::byte> workspace, std::span<float> grid)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kMeasure,
                       "measuring local weights requires an execution with an engine-context lease");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const Result<LocalWeightGridShape> shape = shapeOf(model, kMeasure);
    if (!shape.hasValue())
    {
        return shape.status();
    }
    const Result<FrameGeometry> geometry = checkFrame(model, frame);
    if (!geometry.hasValue())
    {
        return geometry.status();
    }
    if (grid.size() != shape->valuesPerFrame())
    {
        return failure(ErrorCode::IncompatibleArguments, kMeasure,
                       "grid does not hold one frame's values of the stack's lattice",
                       {StatusContextField::ofText(ContextKey::Parameter, "grid"),
                        StatusContextField::ofCount(ContextKey::Requested, grid.size()),
                        StatusContextField::ofCount(ContextKey::Limit, shape->valuesPerFrame())});
    }
    status = checkWorkspace(workspace, localWeightMeasurementWorkspaceBytes(*shape, model.request().input.frameCount),
                            kMeasure);
    if (status.failed())
    {
        return status;
    }

    IntegralImages images;
    images.stride = static_cast<std::uint64_t>(geometry->width) + 1u;
    const std::uint64_t cells = images.stride * (static_cast<std::uint64_t>(geometry->height) + 1u);
    auto* const base = reinterpret_cast<double*>(workspace.data());
    images.count = base;
    images.sum = base + cells;
    images.sumSquares = base + 2u * cells;
    images.gradient = base + 3u * cells;

    LocalWeightMeasurement measurement;
    measurement.totalPixels = static_cast<std::uint64_t>(geometry->width) * geometry->height;
    measurement.nodeCount = shape->nodeCount();
    const bool dispatched = dispatchSampleType(frame.sampleType, [&]<typename SampleT>() {
        status = measureAs<SampleT>(execution, model, *shape, frame, *geometry, images, grid, measurement);
    });
    if (!dispatched)
    {
        return refuse(ErrorCode::InvalidArgument, kMeasure, "frame.sampleType",
                      "frame.sampleType is not one of the resident representations");
    }
    if (status.failed())
    {
        return status;
    }
    execution.reportProgress(measurement.nodeCount, measurement.nodeCount, kMeasureStage);
    return measurement;
}

Result<LocalWeightingSummary> finalizeLocalWeightGrids(const ExecutionContext& execution, const StackModel& model,
                                                       std::span<float> grids, std::span<std::byte> workspace)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kFinalize,
                       "finishing local weights requires an execution with an engine-context lease");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const Result<LocalWeightGridShape> shape = shapeOf(model, kFinalize);
    if (!shape.hasValue())
    {
        return shape.status();
    }
    const std::uint32_t frames = model.request().input.frameCount;
    const std::uint64_t values = shape->valuesPerFrame();
    if (grids.size() != static_cast<std::uint64_t>(frames) * values)
    {
        return failure(ErrorCode::IncompatibleArguments, kFinalize,
                       "grids does not hold one grid per frame of the stack's lattice",
                       {StatusContextField::ofText(ContextKey::Parameter, "grids"),
                        StatusContextField::ofCount(ContextKey::Requested, grids.size()),
                        StatusContextField::ofCount(ContextKey::Limit, static_cast<std::uint64_t>(frames) * values)});
    }
    status = checkWorkspace(workspace, localWeightMeasurementWorkspaceBytes(*shape, frames), kFinalize);
    if (status.failed())
    {
        return status;
    }

    // The per-frame scratch: five doubles and one index per frame, within the stated bytes.
    static_assert(5u * sizeof(double) + sizeof(std::uint32_t) <= kLocalWeightFinalizeBytesPerFrame);
    auto* const doubles = reinterpret_cast<double*>(workspace.data());
    double* const levels = doubles;
    double* const qualities = doubles + frames;
    double* const rankedLevels = doubles + 2u * static_cast<std::size_t>(frames);
    double* const rankedQualities = doubles + 3u * static_cast<std::size_t>(frames);
    double* const deviations = doubles + 4u * static_cast<std::size_t>(frames);
    auto* const active = reinterpret_cast<std::uint32_t*>(doubles + 5u * static_cast<std::size_t>(frames));

    const LocalWeightingRequest& request = model.request().localWeighting;
    LocalWeightingSummary summary;
    summary.windowSize = request.windowSize;
    summary.gridSpacing = request.gridSpacing;
    summary.snrExponent = request.snrExponent;
    summary.frameCount = frames;
    summary.nodeCount = shape->nodeCount();
    summary.minimumWeight = (std::numeric_limits<double>::max)();
    summary.maximumWeight = std::numeric_limits<double>::lowest();
    double sum = 0.0;
    double sumSquares = 0.0;

    for (std::uint64_t node = 0u; node < summary.nodeCount; ++node)
    {
        if (node % kFinalizeCheckpointStride == 0u)
        {
            status = execution.checkCancellation();
            if (status.failed())
            {
                return status;
            }
        }
        std::uint32_t activeCount = 0u;
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            const float* const entry =
                grids.data() + static_cast<std::size_t>(frame) * values + node * kLocalWeightNodeValues;
            const double level = entry[0];
            const double quality = entry[1];
            if (!std::isfinite(level) || !std::isfinite(quality) || quality <= 0.0)
            {
                continue;
            }
            levels[activeCount] = level;
            qualities[activeCount] = quality;
            rankedLevels[activeCount] = level;
            rankedQualities[activeCount] = quality;
            active[activeCount] = frame;
            ++activeCount;
        }

        // Every frame starts at the neutral weight; a judged node then overwrites its active ones.
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            grids[static_cast<std::size_t>(frame) * values + node * kLocalWeightNodeValues] = 1.0f;
        }
        if (activeCount >= kMinimumLocalWeightActiveFrames)
        {
            ++summary.activeNodes;
            const double medianLevel = medianOf(rankedLevels, activeCount);
            const double medianQuality = medianOf(rankedQualities, activeCount);
            for (std::uint32_t index = 0u; index < activeCount; ++index)
            {
                deviations[index] = std::abs(levels[index] - medianLevel);
            }
            double sigma = kLocalWeightDeviationToSigma * medianOf(deviations, activeCount);
            sigma = (std::max)(sigma,
                               (std::max)(kLocalWeightSigmaFloor, std::abs(medianLevel) * kLocalWeightSigmaFraction));
            const double qualityScale = (std::max)(medianQuality, kLocalWeightNoiseFloor);
            for (std::uint32_t index = 0u; index < activeCount; ++index)
            {
                const double ratio = std::clamp(qualities[index] / qualityScale, kLocalWeightQualityRatioLowest,
                                                kLocalWeightQualityRatioHighest);
                const double residual = (levels[index] - medianLevel) / (kLocalWeightConsistencyWidth * sigma);
                const double consistency = std::exp(-0.5 * residual * residual);
                const double weight = std::clamp(ratio * consistency, kLocalWeightLowest, kLocalWeightHighest);
                grids[static_cast<std::size_t>(active[index]) * values + node * kLocalWeightNodeValues] =
                    static_cast<float>(weight);
            }
            summary.weightedFrameNodes += activeCount;
        }
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            const double weight = grids[static_cast<std::size_t>(frame) * values + node * kLocalWeightNodeValues];
            sum += weight;
            sumSquares += weight * weight;
            summary.minimumWeight = (std::min)(summary.minimumWeight, weight);
            summary.maximumWeight = (std::max)(summary.maximumWeight, weight);
        }
    }

    const double entries = static_cast<double>(frames) * static_cast<double>(summary.nodeCount);
    if (entries > 0.0)
    {
        summary.meanWeight = sum / entries;
        const double variance = sumSquares / entries - summary.meanWeight * summary.meanWeight;
        summary.weightStandardDeviation = std::sqrt((std::max)(variance, 0.0));
    }
    else
    {
        summary.minimumWeight = 0.0;
        summary.maximumWeight = 0.0;
    }

    char message[256];
    std::snprintf(message, sizeof(message),
                  "Local weights finished over %llu frames and %llu nodes, %llu judged: weights in [%.4f, %.4f], "
                  "mean %.4f.",
                  static_cast<Count>(frames), static_cast<Count>(summary.nodeCount),
                  static_cast<Count>(summary.activeNodes), summary.minimumWeight, summary.maximumWeight,
                  summary.meanWeight);
    execution.reportDiagnostic(DiagnosticSeverity::Info, ErrorDomain::Algorithms, kFinalize, message);
    execution.reportProgress(summary.nodeCount, summary.nodeCount, kFinalizeStage);
    return summary;
}

float sampleLocalWeight(const LocalWeightGridShape& shape, std::span<const float> grid, double x, double y) noexcept
{
    if (shape.width == 0u || shape.height == 0u || shape.spacing == 0u || shape.nodesX == 0u || shape.nodesY == 0u ||
        grid.size() != shape.valuesPerFrame())
    {
        return 1.0f;
    }
    const double lastX = static_cast<double>(shape.width - 1u);
    const double lastY = static_cast<double>(shape.height - 1u);
    x = std::clamp(x, 0.0, lastX);
    y = std::clamp(y, 0.0, lastY);

    const double spacing = static_cast<double>(shape.spacing);
    const std::uint32_t x0 = (std::min)(static_cast<std::uint32_t>(std::floor(x / spacing)), shape.nodesX - 1u);
    const std::uint32_t y0 = (std::min)(static_cast<std::uint32_t>(std::floor(y / spacing)), shape.nodesY - 1u);
    const std::uint32_t x1 = (std::min)(x0 + 1u, shape.nodesX - 1u);
    const std::uint32_t y1 = (std::min)(y0 + 1u, shape.nodesY - 1u);

    const double px0 = shape.nodeColumn(x0);
    const double py0 = shape.nodeRow(y0);
    const double px1 = shape.nodeColumn(x1);
    const double py1 = shape.nodeRow(y1);
    const double tx = px1 > px0 ? (x - px0) / (px1 - px0) : 0.0;
    const double ty = py1 > py0 ? (y - py0) / (py1 - py0) : 0.0;

    const auto weightAt = [&](std::uint32_t nodeX, std::uint32_t nodeY) noexcept {
        return static_cast<double>(
            grid[(static_cast<std::size_t>(nodeY) * shape.nodesX + nodeX) * kLocalWeightNodeValues]);
    };
    const double top = (1.0 - tx) * weightAt(x0, y0) + tx * weightAt(x1, y0);
    const double bottom = (1.0 - tx) * weightAt(x0, y1) + tx * weightAt(x1, y1);
    return static_cast<float>((1.0 - ty) * top + ty * bottom);
}

Status fillLocalWeightBand(const LocalWeightGridShape& shape, std::span<const float> grid, std::uint32_t firstRow,
                           std::uint32_t rows, std::span<float> weights)
{
    if (grid.size() != shape.valuesPerFrame())
    {
        return failure(ErrorCode::IncompatibleArguments, kFill, "grid does not hold one frame's values of the lattice",
                       {StatusContextField::ofText(ContextKey::Parameter, "grid"),
                        StatusContextField::ofCount(ContextKey::Requested, grid.size()),
                        StatusContextField::ofCount(ContextKey::Limit, shape.valuesPerFrame())});
    }
    if (rows == 0u || static_cast<std::uint64_t>(firstRow) + rows > shape.height)
    {
        return failure(ErrorCode::OutOfRange, kFill, "the band has no rows or ends past the frame",
                       {StatusContextField::ofText(ContextKey::Parameter, "rows"),
                        StatusContextField::ofCount(ContextKey::Requested, static_cast<std::uint64_t>(firstRow) + rows),
                        StatusContextField::ofCount(ContextKey::Limit, shape.height)});
    }
    const std::uint64_t needed = static_cast<std::uint64_t>(rows) * shape.width;
    if (weights.size() != needed)
    {
        return failure(ErrorCode::IncompatibleArguments, kFill, "weights does not hold one value per pixel of the band",
                       {StatusContextField::ofText(ContextKey::Parameter, "weights"),
                        StatusContextField::ofCount(ContextKey::Requested, weights.size()),
                        StatusContextField::ofCount(ContextKey::Limit, needed)});
    }
    for (std::uint32_t y = 0u; y < rows; ++y)
    {
        float* const row = weights.data() + static_cast<std::size_t>(y) * shape.width;
        const double frameRow = static_cast<double>(firstRow) + y;
        for (std::uint32_t x = 0u; x < shape.width; ++x)
        {
            row[x] = sampleLocalWeight(shape, grid, static_cast<double>(x), frameRow);
        }
    }
    return Status::success();
}

} // namespace an
