// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - local normalization: the sampling stage.
//
// One node's background is estimated from a deterministic subset of the positions in its region.
// This file owns that subset: which positions, in which order, and which of the samples found there
// are admitted. The fit calls it for every node; a caller that wants to see what a node was
// estimated from calls it for one.

#include "algorithms/normalization/LocalNormalizationSampling.h"

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/normalization/LocalNormalizationInternal.h"

#include <cstdint>
#include <span>

namespace an
{

namespace
{

using local_normalization_detail::failure;
using local_normalization_detail::parameterFailure;

constexpr const char* kSampleOperation = "an::sampleLocalBackgroundNode";

} // namespace

namespace local_normalization_detail
{

std::uint64_t nodeSeed(const LocalNormalizationGridShape& shape, std::uint32_t nodeIndex) noexcept
{
    return geometrySeed(shape) ^ (static_cast<std::uint64_t>(nodeIndex) + 1u) * kNodeSeedMultiplier;
}

std::uint64_t expansionSeed(const LocalNormalizationGridShape& shape, std::uint32_t nodeIndex, std::uint32_t channel,
                            std::uint32_t step) noexcept
{
    return geometrySeed(shape) ^ (static_cast<std::uint64_t>(nodeIndex) + 1u) * kExpansionSeedMultiplier ^
           (static_cast<std::uint64_t>(channel) + 1u) * kSamplingSeed ^ static_cast<std::uint64_t>(step);
}

std::uint32_t sampleRegionChannel(const FrameReader& reader, const LocalNormalizationRequest& request,
                                  const Region& region, std::uint64_t count, std::uint64_t seed, std::uint32_t channel,
                                  double* samples, std::uint64_t& read) noexcept
{
    const std::uint64_t area = static_cast<std::uint64_t>(region.width) * region.height;
    std::uint32_t written = 0u;
    forEachRegionPosition(area, count, seed, [&](std::uint64_t index) {
        const auto x = static_cast<std::uint32_t>(region.originX + index % region.width);
        const auto y = static_cast<std::uint32_t>(region.originY + index / region.width);
        double value = 0.0;
        ++read;
        if (!reader.read(x, y, channel, value))
        {
            return;
        }
        // A positive range test, so a non-finite sample the rule admits is outside the window.
        if (!(value >= request.lowClip && value < request.highClip))
        {
            return;
        }
        samples[written++] = value;
    });
    return written;
}

void sampleRegionAllChannels(const FrameReader& reader, const LocalNormalizationRequest& request, const Region& region,
                             std::uint64_t count, std::uint64_t seed, std::uint32_t channelCount,
                             std::uint64_t channelStride, double* samples, std::uint32_t* written,
                             std::uint64_t& read) noexcept
{
    const std::uint64_t area = static_cast<std::uint64_t>(region.width) * region.height;
    for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
    {
        written[channel] = 0u;
    }
    forEachRegionPosition(area, count, seed, [&](std::uint64_t index) {
        const auto x = static_cast<std::uint32_t>(region.originX + index % region.width);
        const auto y = static_cast<std::uint32_t>(region.originY + index / region.width);
        for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
        {
            double value = 0.0;
            ++read;
            if (!reader.read(x, y, channel, value))
            {
                continue;
            }
            if (!(value >= request.lowClip && value < request.highClip))
            {
                continue;
            }
            samples[channel * channelStride + written[channel]] = value;
            ++written[channel];
        }
    });
}

Region nodeRegion(const LocalNormalizationPlan& plan, std::uint32_t nodeX, std::uint32_t nodeY) noexcept
{
    return Region{.originX = plan.roiOriginX(nodeX),
                  .originY = plan.roiOriginY(nodeY),
                  .width = plan.roiWidth(),
                  .height = plan.roiHeight()};
}

Region expandedNodeRegion(const LocalNormalizationPlan& plan, std::uint32_t nodeX, std::uint32_t nodeY,
                          std::uint32_t step) noexcept
{
    const LocalNormalizationGridShape& shape = plan.shape();
    const std::uint32_t width = (std::min)(shape.width, plan.roiWidth() + step * plan.cellWidth());
    const std::uint32_t height = (std::min)(shape.height, plan.roiHeight() + step * plan.cellHeight());
    return Region{.originX = regionOrigin(plan.nodeX(nodeX), width, shape.width),
                  .originY = regionOrigin(plan.nodeY(nodeY), height, shape.height),
                  .width = width,
                  .height = height};
}

std::uint64_t expandedSampleCount(const Region& region) noexcept
{
    const std::uint64_t area = static_cast<std::uint64_t>(region.width) * region.height;
    const std::uint64_t wanted =
        std::clamp<std::uint64_t>(area / kLocalNormalizationSampleDivisor, kMinLocalNormalizationNodeSamples,
                                  2ull * kMaxLocalNormalizationNodeSamples);
    return (std::min)(area, wanted);
}

} // namespace local_normalization_detail

Result<std::uint32_t> sampleLocalBackgroundNode(const ExecutionContext& execution, const NormalizationFrameView& frame,
                                                const LocalNormalizationPlan& plan, std::uint32_t nodeX,
                                                std::uint32_t nodeY, std::uint32_t channel, std::span<double> samples)
{
    if (!execution.valid())
    {
        return failure(kSampleOperation, ErrorCode::NotInitialized,
                       "node sampling needs an execution holding an engine-context lease");
    }
    if (!plan.valid())
    {
        return failure(kSampleOperation, ErrorCode::NotInitialized, "node sampling needs a validated plan");
    }
    if (!frame.valid())
    {
        return parameterFailure(kSampleOperation, ErrorCode::InvalidArgument, "node sampling needs a checked frame",
                                "frame");
    }
    const LocalNormalizationGridShape& shape = plan.shape();
    if (nodeX >= shape.nodesPerAxis() || nodeY >= shape.nodesPerAxis() || channel >= shape.channelCount)
    {
        return parameterFailure(kSampleOperation, ErrorCode::InvalidArgument, "the node or channel is outside the grid",
                                "nodeX");
    }
    if (!local_normalization_detail::frameMatchesPlan(plan, frame))
    {
        return failure(kSampleOperation, ErrorCode::IncompatibleArguments,
                       "the frame does not have the plan's extent and channel count");
    }
    if (samples.size() < plan.samplesPerNode())
    {
        return failure(kSampleOperation, ErrorCode::IncompatibleArguments,
                       "the sample span is shorter than the plan's samples per node");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    const local_normalization_detail::FrameReader reader(frame);
    const std::uint32_t nodeIndex = nodeY * shape.nodesPerAxis() + nodeX;
    const local_normalization_detail::Region region = local_normalization_detail::nodeRegion(plan, nodeX, nodeY);
    std::uint64_t read = 0u;
    return local_normalization_detail::sampleRegionChannel(reader, plan.request(), region, plan.samplesPerNode(),
                                                           local_normalization_detail::nodeSeed(shape, nodeIndex),
                                                           channel, samples.data(), read);
}

} // namespace an
