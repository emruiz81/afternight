// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent definitions of the comet composite and rejection-map statistics rows: their derived inputs and the
// outputs a run is held to.
#pragma once

#include "Results.h"
#include "Scenario.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// True for the comet composite and rejection-map statistics rows.
[[nodiscard]] bool requiresStackingNumericsOracle(const ScenarioSpec& scenario);

/// The comet row's frames, the comet's step between them, its first and fixed positions and its aperture radius, in
/// engine pixel coordinates (the first pixel's centre at one half); the centres are dyadic, so integer-centred
/// distances and the engine's agree to the bit.
inline constexpr std::uint32_t kCometFrames = 8u;
inline constexpr double kCometStep = 16.0;
inline constexpr double kCometStartX = 384.25;
inline constexpr double kCometY = 512.75;
inline constexpr double kCometRadius = 24.0;

/// The comet's profile: `peak / (1 + r^2 / width^2)^2` at distance `r` from its centre.
inline constexpr double kCometPeak = 0.45;
inline constexpr double kCometWidth = 6.0;

/// Where the comet is on frame `frame` of the stars' registration: `kCometStartX + frame * kCometStep`.
[[nodiscard]] double cometStarAlignedX(std::uint32_t frame) noexcept;

/// Frame `frame` as the stars register it: the field with the comet added at `cometStarAlignedX(frame)`, rounded once
/// to Float32. One tightly packed plane.
[[nodiscard]] std::vector<float> cometStarAlignedFrame(std::span<const float> field, std::uint32_t width,
                                                       std::uint32_t height, std::uint32_t frame);

/// Frame `frame` as the comet registers it: the field moved left by `frame * kCometStep` whole pixels - a column past
/// the right edge repeats the last one - with the comet added at `kCometStartX`, rounded once to Float32.
[[nodiscard]] std::vector<float> cometCometAlignedFrame(std::span<const float> field, std::uint32_t width,
                                                        std::uint32_t height, std::uint32_t frame);

/// Holds the comet row's composite to its definition, computed in double from the frames: each frame split at its
/// aperture - the sky the annulus median, the weight one inside the radius and falling linearly across the larger of
/// one pixel and 0.35 radii - into the layer its composition keeps, rounded to Float32; each layer's frames averaged;
/// the mask the largest of the eight apertures' weights over the larger of two pixels and 0.45 radii; and the
/// composite the star stack blended with the comet stack by the mask, the registrations the identity. The engine
/// accumulates its stacks in another order than this, so the tolerance is 1.0e-5. Sets the correctness metric to the
/// largest difference.
void checkCometCompositeOutput(std::span<const float> field, std::uint32_t width, std::uint32_t height,
                               std::span<const float> composite, ResultRecord& result);

/// The rejection row's stack depth, by which a fraction map divides its counts.
inline constexpr std::uint32_t kRejectionFrames = 8u;

/// The rejection row's cluster analysis: the fewest pixels a cluster holds and the most each map reports.
inline constexpr std::uint32_t kRejectionMinimumClusterPixels = 8u;
inline constexpr std::uint32_t kRejectionMaximumClusters = 32u;

/// The rejection row's low map from its field: scattered single rejections where a hash of the element falls under
/// one in sixty-four, and three-by-three blocks rejected twice on a coarse grid. The field is read only for its
/// extent. Packed planar counts.
[[nodiscard]] std::vector<std::uint32_t> rejectionLowCounts(std::span<const float> field, std::uint32_t width,
                                                            std::uint32_t height, std::uint32_t channels);

/// The rejection row's high map from its field: every element above 0.12 - the stars - rejected once for every
/// further 0.04, at most `kRejectionFrames` times. Packed planar counts.
[[nodiscard]] std::vector<std::uint32_t> rejectionHighCounts(std::span<const float> field, std::uint32_t width,
                                                             std::uint32_t height, std::uint32_t channels);

/// A map as a fraction: each count over `kRejectionFrames`, rounded to Float32.
[[nodiscard]] std::vector<float> rejectionFractionMap(std::span<const std::uint32_t> counts);

/// What the workflow's spatial statistics read from the two maps: the elements either reached, that count over the
/// map's elements, the clusters both maps report - at most `kRejectionMaximumClusters` each - and the largest
/// cluster's pixels and fraction of the map.
struct RejectionMapStatistics
{
    double eitherElements = 0.0;
    double eitherFraction = 0.0;
    double clusters = 0.0;
    double largestClusterPixels = 0.0;
    double largestClusterFraction = 0.0;
};

/// Holds the rejection row's statistics to their definition, computed here from the counts: the union of the
/// elements a rejection reached, and each map's eight-connected clusters of at least
/// `kRejectionMinimumClusterPixels` elements per channel plane. Every figure is a count or a count's ratio, so the
/// tolerance is 1.0e-9. Sets the correctness metric to the largest difference.
void checkRejectionMapStatistics(std::span<const std::uint32_t> low, std::span<const std::uint32_t> high,
                                 std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                 const RejectionMapStatistics& produced, ResultRecord& result);

} // namespace an::bench
