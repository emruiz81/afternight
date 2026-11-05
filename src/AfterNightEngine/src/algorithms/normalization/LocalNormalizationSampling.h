// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the region samplers the local-normalization fit shares with its public
// sampling entry point.
//
// Both samplers visit a region's positions in the order `forEachRegionPosition` derives from a
// seed, and admit a sample only when the frame's validity accepts it and it lies in the request's
// clip window. The all-channel walk reads every channel at each position, so each channel's run
// holds exactly the samples, in exactly the order, that the one-channel walk gives it: that is what
// lets the fit read a node's channels in one pass while `sampleLocalBackgroundNode` still shows a
// caller what one channel was estimated from.

#pragma once

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>

#include "algorithms/normalization/LocalNormalizationInternal.h"

#include <cstdint>

namespace an::local_normalization_detail
{

/// A rectangle of frame positions.
struct Region
{
    /// The first column.
    std::uint32_t originX = 0u;

    /// The first row.
    std::uint32_t originY = 0u;

    /// How many columns.
    std::uint32_t width = 0u;

    /// How many rows.
    std::uint32_t height = 0u;
};

/// The seed one node's ordinary sampling order is derived from.
///
/// @param shape the grid geometry.
/// @param nodeIndex the node's row-major index.
/// @return the seed.
[[nodiscard]] std::uint64_t nodeSeed(const LocalNormalizationGridShape& shape, std::uint32_t nodeIndex) noexcept;

/// The seed one node's expanded sampling order is derived from.
///
/// @param shape the grid geometry.
/// @param nodeIndex the node's row-major index.
/// @param channel the channel being recovered.
/// @param step which expansion step.
/// @return the seed.
[[nodiscard]] std::uint64_t expansionSeed(const LocalNormalizationGridShape& shape, std::uint32_t nodeIndex,
                                          std::uint32_t channel, std::uint32_t step) noexcept;

/// Gathers one channel's admitted samples over a region.
///
/// @param reader the frame.
/// @param request the clip window.
/// @param region where to sample.
/// @param count how many positions to visit.
/// @param seed the order.
/// @param channel which channel.
/// @param samples where the admitted samples go, of at least `count` entries.
/// @param read incremented once per position visited.
/// @return how many samples were written.
[[nodiscard]] std::uint32_t sampleRegionChannel(const FrameReader& reader, const LocalNormalizationRequest& request,
                                                const Region& region, std::uint64_t count, std::uint64_t seed,
                                                std::uint32_t channel, double* samples, std::uint64_t& read) noexcept;

/// Gathers every channel's admitted samples over a region in one walk.
///
/// @param reader the frame.
/// @param request the clip window.
/// @param region where to sample.
/// @param count how many positions to visit.
/// @param seed the order.
/// @param channelCount how many channels.
/// @param channelStride how many entries separate one channel's run from the next in `samples`.
/// @param samples where the admitted samples go, `channelCount * channelStride` entries.
/// @param written receives how many samples each channel got.
/// @param read incremented once per channel at every position visited.
void sampleRegionAllChannels(const FrameReader& reader, const LocalNormalizationRequest& request, const Region& region,
                             std::uint64_t count, std::uint64_t seed, std::uint32_t channelCount,
                             std::uint64_t channelStride, double* samples, std::uint32_t* written,
                             std::uint64_t& read) noexcept;

/// The ordinary sampling region of one node.
///
/// @param plan the validated plan.
/// @param nodeX the node column.
/// @param nodeY the node row.
/// @return the region.
[[nodiscard]] Region nodeRegion(const LocalNormalizationPlan& plan, std::uint32_t nodeX, std::uint32_t nodeY) noexcept;

/// The region of one node grown by a number of cells in each direction and kept inside the frame.
///
/// @param plan the validated plan.
/// @param nodeX the node column.
/// @param nodeY the node row.
/// @param step how many cells to grow by.
/// @return the region.
[[nodiscard]] Region expandedNodeRegion(const LocalNormalizationPlan& plan, std::uint32_t nodeX, std::uint32_t nodeY,
                                        std::uint32_t step) noexcept;

/// How many positions an expanded region is asked for.
///
/// @param region the expanded region.
/// @return the count, at most the region's area.
[[nodiscard]] std::uint64_t expandedSampleCount(const Region& region) noexcept;

} // namespace an::local_normalization_detail
