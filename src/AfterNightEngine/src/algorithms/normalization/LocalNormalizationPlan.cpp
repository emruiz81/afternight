// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - local normalization: names, grid geometry, the validated plan, point
// evaluation, smoothing and combination.
//
// The plan resolves once what the geometry decides: a cell is the frame divided by the grid size,
// rounded up; a node's region is the cell widened by the overlap on each side, no smaller than the
// requested minimum and no larger than the frame; and a node asks for one sample per sixteen region
// pixels within its stated bounds. The expanded-region and scale-grid counts are derived the same
// way, so every stage reads one number rather than re-deriving it. A point evaluation includes the
// apply's own row body, so the two cannot compute a surface differently.

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "algorithms/normalization/LocalNormalizationInternal.h"
#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace an
{

namespace
{

// The shared row body, so a point evaluation and a row evaluation cannot disagree.
#include "kernels/reference/LocalNormalizationRow.inl"

using local_normalization_detail::failure;
using local_normalization_detail::parameterFailure;
using local_normalization_detail::rangeFailure;

/// A floating parameter's actual value and the bound it violated.
[[nodiscard]] Status realRangeFailure(const char* operation, const char* message, const char* parameter, double value,
                                      double low, double high)
{
    return Status::failure(ErrorCode::OutOfRange,
                           {.message = message,
                            .operation = operation,
                            .domain = ErrorDomain::Algorithms,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter),
                                        StatusContextField::ofReal(ContextKey::Requested, value),
                                        StatusContextField::ofReal(ContextKey::Limit, value < low ? low : high)}});
}

constexpr const char* kPlanOperation = "an::planLocalNormalization";
constexpr const char* kShapeOperation = "an::checkLocalNormalizationGridShape";
constexpr const char* kEvaluateOperation = "an::evaluateLocalGrid";
constexpr const char* kSmoothOperation = "an::smoothLocalGrid";
constexpr const char* kCombineOperation = "an::combineLocalGrids";

} // namespace

namespace local_normalization_detail
{

double evaluateGridUnchecked(const LocalNormalizationGridShape& shape, std::span<const double> values, bool bicubic,
                             std::uint32_t channel, double x, double y) noexcept
{
    const double maxX = static_cast<double>(shape.width) - 1.0;
    const double maxY = static_cast<double>(shape.height) - 1.0;
    x = std::clamp(x, 0.0, maxX);
    y = std::clamp(y, 0.0, maxY);
    double columnFraction = 0.0;
    double rowFraction = 0.0;
    const std::uint32_t columnCell = localNormalizationCellOfCoordinate(x, shape.width, shape.gridSize, columnFraction);
    const std::uint32_t rowCell = localNormalizationCellOfCoordinate(y, shape.height, shape.gridSize, rowFraction);
    // The row body indexes the mixed arrays by absolute node column, so the whole axis is mixed
    // here too; a point evaluation is not a hot path and a four-wide window would need its own
    // index arithmetic that the row does not have.
    std::array<double, kMaxLocalNormalizationGridSize + 1u> mixed{};
    std::array<double, kMaxLocalNormalizationGridSize + 1u> lower{};
    std::array<double, kMaxLocalNormalizationGridSize + 1u> upper{};
    const double* const plane = values.data() + static_cast<std::uint64_t>(channel) * shape.nodeCount();
    localNormalizationMixRows(plane, shape.nodesPerAxis(), shape.gridSize, rowCell, rowFraction, bicubic, mixed.data(),
                              lower.data(), upper.data());
    const LocalNormalizationRunGrid grid{.mixed = mixed.data(), .lower = lower.data(), .upper = upper.data()};
    const LocalNormalizationCell cell = localNormalizationCellOf(grid, columnCell, shape.gridSize, bicubic);
    return localNormalizationEvaluateCell(cell, columnFraction, bicubic);
}

} // namespace local_normalization_detail

const char* localGridInterpolationName(LocalGridInterpolation interpolation) noexcept
{
    switch (interpolation)
    {
    case LocalGridInterpolation::Bilinear:
        return "Bilinear";
    case LocalGridInterpolation::Bicubic:
        return "Bicubic";
    }
    return "UnknownLocalGridInterpolation";
}

const char* localBackgroundEstimatorName(LocalBackgroundEstimator estimator) noexcept
{
    switch (estimator)
    {
    case LocalBackgroundEstimator::Percentile:
        return "Percentile";
    case LocalBackgroundEstimator::SigmaClippedMedian:
        return "SigmaClippedMedian";
    case LocalBackgroundEstimator::SigmaClippedMode:
        return "SigmaClippedMode";
    }
    return "UnknownLocalBackgroundEstimator";
}

const char* localScaleModeName(LocalScaleMode mode) noexcept
{
    switch (mode)
    {
    case LocalScaleMode::Linked:
        return "Linked";
    case LocalScaleMode::PerChannel:
        return "PerChannel";
    }
    return "UnknownLocalScaleMode";
}

const char* localGridSmoothingName(LocalGridSmoothing smoothing) noexcept
{
    switch (smoothing)
    {
    case LocalGridSmoothing::Box:
        return "Box";
    case LocalGridSmoothing::Gaussian:
        return "Gaussian";
    }
    return "UnknownLocalGridSmoothing";
}

Status checkLocalNormalizationGridShape(const LocalNormalizationGridShape& shape)
{
    if (shape.gridSize == 0u || shape.gridSize > kMaxLocalNormalizationGridSize)
    {
        return rangeFailure(kShapeOperation, "a local-normalization grid has between one and the maximum cells a side",
                            "gridSize", shape.gridSize, kMaxLocalNormalizationGridSize);
    }
    if (shape.width == 0u || shape.height == 0u)
    {
        return parameterFailure(kShapeOperation, ErrorCode::InvalidArgument,
                                "a local-normalization grid describes a frame with a non-zero extent", "extent");
    }
    if (shape.channelCount == 0u || shape.channelCount > kMaxNormalizationChannels)
    {
        return parameterFailure(kShapeOperation, ErrorCode::InvalidArgument,
                                "a local-normalization grid describes between one and four channels", "channelCount");
    }
    return Status::success();
}

std::uint32_t LocalNormalizationPlan::nodeX(std::uint32_t nodeX) const noexcept
{
    return local_normalization_detail::nodePosition(nodeX, m_shape.gridSize, m_shape.width);
}

std::uint32_t LocalNormalizationPlan::nodeY(std::uint32_t nodeY) const noexcept
{
    return local_normalization_detail::nodePosition(nodeY, m_shape.gridSize, m_shape.height);
}

std::uint32_t LocalNormalizationPlan::roiOriginX(std::uint32_t nodeX) const noexcept
{
    return local_normalization_detail::regionOrigin(this->nodeX(nodeX), m_roiWidth, m_shape.width);
}

std::uint32_t LocalNormalizationPlan::roiOriginY(std::uint32_t nodeY) const noexcept
{
    return local_normalization_detail::regionOrigin(this->nodeY(nodeY), m_roiHeight, m_shape.height);
}

std::uint64_t LocalNormalizationPlan::fitWorkingBytesPerWorker() const noexcept
{
    // One sample run per channel for the node's region, plus two runs the size of the largest
    // expanded region for the estimator's samples and its scratch.
    const std::uint64_t nodeRuns = static_cast<std::uint64_t>(m_shape.channelCount) * m_samplesPerNode;
    const std::uint64_t estimatorRuns = 2ull * (std::max)(m_samplesPerNode, m_maxExpandedSamples);
    return (nodeRuns + estimatorRuns) * sizeof(double);
}

Result<LocalNormalizationPlan> planLocalNormalization(const LocalNormalizationRequest& request, std::uint32_t width,
                                                      std::uint32_t height, std::uint32_t channelCount)
{
    if (!isLocalGridInterpolation(request.interpolation))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the interpolation is not one this build implements", "interpolation");
    }
    if (!isLocalBackgroundEstimator(request.estimator))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the background estimator is not one this build implements", "estimator");
    }
    if (!isLocalScaleMode(request.scaleMode))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the scale mode is not one this build implements", "scaleMode");
    }
    if (request.gridSize == 0u || request.gridSize > kMaxLocalNormalizationGridSize)
    {
        return rangeFailure(kPlanOperation, "the grid has between one and the maximum cells a side", "gridSize",
                            request.gridSize, kMaxLocalNormalizationGridSize);
    }
    if (!std::isfinite(request.backgroundPercentile) || request.backgroundPercentile < 0.0 ||
        request.backgroundPercentile > 1.0)
    {
        return realRangeFailure(kPlanOperation, "the background percentile lies within the unit interval",
                                "backgroundPercentile", request.backgroundPercentile, 0.0, 1.0);
    }
    if (!std::isfinite(request.sigmaClip) || request.sigmaClip < kMinLocalNormalizationSigmaClip ||
        request.sigmaClip > kMaxLocalNormalizationSigmaClip)
    {
        return realRangeFailure(kPlanOperation, "the sigma-clipping multiplier lies within its stated interval",
                                "sigmaClip", request.sigmaClip, kMinLocalNormalizationSigmaClip,
                                kMaxLocalNormalizationSigmaClip);
    }
    if (request.sigmaClipIterations == 0u || request.sigmaClipIterations > kMaxLocalNormalizationSigmaClipIterations)
    {
        return rangeFailure(kPlanOperation, "the sigma-clipping iteration count lies within its stated interval",
                            "sigmaClipIterations", request.sigmaClipIterations,
                            kMaxLocalNormalizationSigmaClipIterations);
    }
    if (request.backgroundMinRoiExtent < kMinLocalNormalizationRoiExtent ||
        request.backgroundMinRoiExtent > kMaxLocalNormalizationRoiExtent)
    {
        return rangeFailure(kPlanOperation, "the minimum region side lies within its stated interval",
                            "backgroundMinRoiExtent", request.backgroundMinRoiExtent, kMaxLocalNormalizationRoiExtent);
    }
    if (!std::isfinite(request.backgroundRoiOverlap) || request.backgroundRoiOverlap < 0.0 ||
        request.backgroundRoiOverlap > kMaxLocalNormalizationRoiOverlap)
    {
        return realRangeFailure(kPlanOperation, "the region overlap lies within its stated interval",
                                "backgroundRoiOverlap", request.backgroundRoiOverlap, 0.0,
                                kMaxLocalNormalizationRoiOverlap);
    }
    if (!std::isfinite(request.lowClip) || !std::isfinite(request.highClip) || !(request.lowClip < request.highClip))
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the clip window is two finite bounds with the low one below the high one", "lowClip");
    }
    if (request.scaleSamples == 0u)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "the scale estimator samples at least one position", "scaleSamples");
    }
    if (width == 0u || height == 0u)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "a local normalization plans over a non-zero extent", "extent");
    }
    if (channelCount == 0u || channelCount > kMaxNormalizationChannels)
    {
        return parameterFailure(kPlanOperation, ErrorCode::InvalidArgument,
                                "a local normalization plans over between one and four channels", "channelCount");
    }

    LocalNormalizationPlan plan;
    plan.m_request = request;
    plan.m_shape = LocalNormalizationGridShape{
        .gridSize = request.gridSize, .width = width, .height = height, .channelCount = channelCount};
    const std::uint32_t g = request.gridSize;
    plan.m_cellWidth = (std::max)(1u, (width + g - 1u) / g);
    plan.m_cellHeight = (std::max)(1u, (height + g - 1u) / g);

    const double regionScale = 1.0 + 2.0 * request.backgroundRoiOverlap;
    const auto scaledWidth =
        static_cast<std::uint32_t>((std::max)(1.0, std::round(static_cast<double>(plan.m_cellWidth) * regionScale)));
    const auto scaledHeight =
        static_cast<std::uint32_t>((std::max)(1.0, std::round(static_cast<double>(plan.m_cellHeight) * regionScale)));
    plan.m_roiWidth = (std::min)(width, (std::max)(request.backgroundMinRoiExtent, scaledWidth));
    plan.m_roiHeight = (std::min)(height, (std::max)(request.backgroundMinRoiExtent, scaledHeight));
    const std::uint64_t regionArea = static_cast<std::uint64_t>(plan.m_roiWidth) * plan.m_roiHeight;
    const std::uint64_t wanted =
        std::clamp<std::uint64_t>(regionArea / kLocalNormalizationSampleDivisor, kMinLocalNormalizationNodeSamples,
                                  kMaxLocalNormalizationNodeSamples);
    plan.m_samplesPerNode = static_cast<std::uint32_t>((std::min)(regionArea, wanted));
    // The largest expanded region is the region grown by every step's cell in each direction,
    // bounded by the frame, sampled at up to twice the ordinary ceiling.
    const std::uint64_t expandedWidth =
        (std::min)(static_cast<std::uint64_t>(width),
                   static_cast<std::uint64_t>(plan.m_roiWidth) +
                       static_cast<std::uint64_t>(kLocalNormalizationExpansionSteps) * plan.m_cellWidth);
    const std::uint64_t expandedHeight =
        (std::min)(static_cast<std::uint64_t>(height),
                   static_cast<std::uint64_t>(plan.m_roiHeight) +
                       static_cast<std::uint64_t>(kLocalNormalizationExpansionSteps) * plan.m_cellHeight);
    const std::uint64_t expandedArea = expandedWidth * expandedHeight;
    const std::uint64_t expandedWanted =
        std::clamp<std::uint64_t>(expandedArea / kLocalNormalizationSampleDivisor, kMinLocalNormalizationNodeSamples,
                                  2ull * kMaxLocalNormalizationNodeSamples);
    plan.m_maxExpandedSamples = static_cast<std::uint32_t>((std::min)(expandedArea, expandedWanted));

    plan.m_scaleRoiWidth = (std::min)(width, (std::max)(kMinLocalNormalizationScaleRoiExtent, plan.m_cellWidth));
    plan.m_scaleRoiHeight = (std::min)(height, (std::max)(kMinLocalNormalizationScaleRoiExtent, plan.m_cellHeight));
    const std::uint64_t scaleArea = static_cast<std::uint64_t>(plan.m_scaleRoiWidth) * plan.m_scaleRoiHeight;
    const std::uint64_t perNode = std::clamp<std::uint64_t>(
        static_cast<std::uint64_t>(request.scaleSamples) / (std::max)(1u, plan.m_shape.nodeCount()),
        kMinLocalNormalizationScaleNodeSamples, kMaxLocalNormalizationScaleNodeSamples);
    plan.m_samplesPerScaleNode = static_cast<std::uint32_t>((std::min)(scaleArea, perNode));
    plan.m_scaleChannel = channelCount == 3u ? 1u : 0u;
    plan.m_valid = true;
    return plan;
}

Result<double> evaluateLocalGrid(const LocalNormalizationGridShape& shape, std::span<const double> values,
                                 LocalGridInterpolation interpolation, std::uint32_t channel, double x, double y)
{
    const Status shapeReady = checkLocalNormalizationGridShape(shape);
    if (shapeReady.failed())
    {
        return shapeReady;
    }
    if (!isLocalGridInterpolation(interpolation))
    {
        return parameterFailure(kEvaluateOperation, ErrorCode::InvalidArgument,
                                "the interpolation is not one this build implements", "interpolation");
    }
    if (channel >= shape.channelCount)
    {
        return parameterFailure(kEvaluateOperation, ErrorCode::InvalidArgument, "the channel is outside the grid",
                                "channel");
    }
    if (!std::isfinite(x) || !std::isfinite(y))
    {
        return parameterFailure(kEvaluateOperation, ErrorCode::InvalidArgument, "the position is not finite", "x");
    }
    if (values.size() != shape.valueCount())
    {
        return failure(kEvaluateOperation, ErrorCode::IncompatibleArguments,
                       "the grid does not hold exactly the shape's values");
    }
    return local_normalization_detail::evaluateGridUnchecked(
        shape, values, interpolation == LocalGridInterpolation::Bicubic, channel, x, y);
}

Status smoothLocalGrid(const ExecutionContext& execution, const LocalNormalizationGridShape& shape,
                       std::span<double> values, std::uint32_t radius, LocalGridSmoothing smoothing)
{
    if (!execution.valid())
    {
        return failure(kSmoothOperation, ErrorCode::NotInitialized,
                       "grid smoothing needs an execution holding an engine-context lease");
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    const Status shapeReady = checkLocalNormalizationGridShape(shape);
    if (shapeReady.failed())
    {
        return shapeReady;
    }
    if (!isLocalGridSmoothing(smoothing))
    {
        return parameterFailure(kSmoothOperation, ErrorCode::InvalidArgument,
                                "the smoothing is not one this build implements", "smoothing");
    }
    if (values.size() != shape.valueCount())
    {
        return failure(kSmoothOperation, ErrorCode::IncompatibleArguments,
                       "the grid does not hold exactly the shape's values");
    }
    if (!local_normalization_detail::allFinite(values))
    {
        return failure(kSmoothOperation, ErrorCode::InvalidData, "a grid node is not finite");
    }
    const std::uint32_t g = shape.gridSize;
    const std::uint32_t r = (std::min)(radius, g);
    if (r == 0u)
    {
        return Status::success();
    }
    const std::uint32_t nodes = shape.nodesPerAxis();
    // One channel's nodes for the result and, for the separable Gaussian, one more for the pass
    // between, plus the taps.
    const std::uint64_t bytes = (2ull * shape.nodeCount() + (2ull * r + 1ull)) * sizeof(double);
    auto taken =
        plane_detail::ScopedContextMemory::take(execution, bytes, kDefaultBufferAlignmentBytes, kSmoothOperation);
    if (!taken.hasValue())
    {
        return taken.status();
    }
    auto* const out = static_cast<double*>(taken->data());
    double* const between = out + shape.nodeCount();
    double* const taps = between + shape.nodeCount();

    if (smoothing == LocalGridSmoothing::Gaussian)
    {
        const double sigma = (std::max)(0.5, static_cast<double>(r) * 0.75);
        const double inverseTwoSigmaSquared = 1.0 / (2.0 * sigma * sigma);
        double tapSum = 0.0;
        for (std::uint32_t tap = 0u; tap <= 2u * r; ++tap)
        {
            const double offset = static_cast<double>(tap) - static_cast<double>(r);
            const double weight = std::exp(-(offset * offset) * inverseTwoSigmaSquared);
            taps[tap] = weight;
            tapSum += weight;
        }
        for (std::uint32_t tap = 0u; tap <= 2u * r; ++tap)
        {
            taps[tap] /= tapSum;
        }
    }

    for (std::uint32_t channel = 0u; channel < shape.channelCount; ++channel)
    {
        double* const plane = values.data() + static_cast<std::uint64_t>(channel) * shape.nodeCount();
        if (smoothing == LocalGridSmoothing::Box)
        {
            for (std::uint32_t ny = 0u; ny < nodes; ++ny)
            {
                if (const Status stopped = execution.checkCancellation(); stopped.failed())
                {
                    return stopped;
                }
                const std::uint32_t y0 = ny > r ? ny - r : 0u;
                const std::uint32_t y1 = (std::min)(g, ny + r);
                for (std::uint32_t nx = 0u; nx < nodes; ++nx)
                {
                    if ((nx % 32u) == 0u)
                    {
                        if (const Status stopped = execution.checkCancellation(); stopped.failed())
                        {
                            return stopped;
                        }
                    }
                    const std::uint32_t x0 = nx > r ? nx - r : 0u;
                    const std::uint32_t x1 = (std::min)(g, nx + r);
                    double sum = 0.0;
                    std::uint32_t count = 0u;
                    for (std::uint32_t yy = y0; yy <= y1; ++yy)
                    {
                        for (std::uint32_t xx = x0; xx <= x1; ++xx)
                        {
                            sum += plane[static_cast<std::uint64_t>(yy) * nodes + xx];
                            ++count;
                        }
                    }
                    out[static_cast<std::uint64_t>(ny) * nodes + nx] = sum / static_cast<double>(count);
                }
            }
        }
        else
        {
            for (std::uint32_t ny = 0u; ny < nodes; ++ny)
            {
                if (const Status stopped = execution.checkCancellation(); stopped.failed())
                {
                    return stopped;
                }
                for (std::uint32_t nx = 0u; nx < nodes; ++nx)
                {
                    if ((nx % 32u) == 0u)
                    {
                        if (const Status stopped = execution.checkCancellation(); stopped.failed())
                        {
                            return stopped;
                        }
                    }
                    double sum = 0.0;
                    double weightSum = 0.0;
                    for (std::uint32_t tap = 0u; tap <= 2u * r; ++tap)
                    {
                        const std::int64_t xx = static_cast<std::int64_t>(nx) + static_cast<std::int64_t>(tap) -
                                                static_cast<std::int64_t>(r);
                        if (xx < 0 || xx >= static_cast<std::int64_t>(nodes))
                        {
                            continue;
                        }
                        sum +=
                            plane[static_cast<std::uint64_t>(ny) * nodes + static_cast<std::uint64_t>(xx)] * taps[tap];
                        weightSum += taps[tap];
                    }
                    between[static_cast<std::uint64_t>(ny) * nodes + nx] = weightSum > 0.0 ? sum / weightSum : 0.0;
                }
            }
            for (std::uint32_t ny = 0u; ny < nodes; ++ny)
            {
                if (const Status stopped = execution.checkCancellation(); stopped.failed())
                {
                    return stopped;
                }
                for (std::uint32_t nx = 0u; nx < nodes; ++nx)
                {
                    if ((nx % 32u) == 0u)
                    {
                        if (const Status stopped = execution.checkCancellation(); stopped.failed())
                        {
                            return stopped;
                        }
                    }
                    double sum = 0.0;
                    double weightSum = 0.0;
                    for (std::uint32_t tap = 0u; tap <= 2u * r; ++tap)
                    {
                        const std::int64_t yy = static_cast<std::int64_t>(ny) + static_cast<std::int64_t>(tap) -
                                                static_cast<std::int64_t>(r);
                        if (yy < 0 || yy >= static_cast<std::int64_t>(nodes))
                        {
                            continue;
                        }
                        sum += between[static_cast<std::uint64_t>(yy) * nodes + nx] * taps[tap];
                        weightSum += taps[tap];
                    }
                    out[static_cast<std::uint64_t>(ny) * nodes + nx] = weightSum > 0.0 ? sum / weightSum : 0.0;
                }
            }
        }
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
        {
            return stopped;
        }
        std::copy(out, out + shape.nodeCount(), plane);
        execution.reportProgress(channel + 1u, shape.channelCount, "smoothing local normalization grid");
    }
    return execution.checkCancellation();
}

Status combineLocalGrids(const LocalNormalizationGridShape& shape, std::span<const std::span<const double>> members,
                         std::span<double> combined)
{
    const Status shapeReady = checkLocalNormalizationGridShape(shape);
    if (shapeReady.failed())
    {
        return shapeReady;
    }
    if (members.empty())
    {
        return parameterFailure(kCombineOperation, ErrorCode::InvalidArgument, "a combination needs at least one grid",
                                "members");
    }
    if (members.size() > kMaxLocalNormalizationGridMembers)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{
                .message = "a combination takes at most the stated number of grids",
                .operation = kCombineOperation,
                .domain = ErrorDomain::Algorithms,
                .context = {StatusContextField::ofCount(ContextKey::Requested, members.size()),
                            StatusContextField::ofCount(ContextKey::Limit, kMaxLocalNormalizationGridMembers)}});
    }
    if (combined.size() != shape.valueCount())
    {
        return failure(kCombineOperation, ErrorCode::IncompatibleArguments,
                       "the combined grid does not hold exactly the shape's values");
    }
    for (const std::span<const double>& member : members)
    {
        if (member.size() != shape.valueCount())
        {
            return failure(kCombineOperation, ErrorCode::IncompatibleArguments,
                           "a member grid does not hold exactly the shape's values");
        }
        if (!local_normalization_detail::allFinite(member))
        {
            return failure(kCombineOperation, ErrorCode::InvalidData, "a member grid node is not finite");
        }
    }
    std::array<double, kMaxLocalNormalizationGridMembers> scratch{};
    for (std::uint64_t index = 0u; index < shape.valueCount(); ++index)
    {
        for (std::size_t member = 0u; member < members.size(); ++member)
        {
            scratch[member] = members[member][index];
        }
        combined[index] = local_normalization_detail::medianInPlace(scratch.data(), members.size());
    }
    return Status::success();
}

} // namespace an
