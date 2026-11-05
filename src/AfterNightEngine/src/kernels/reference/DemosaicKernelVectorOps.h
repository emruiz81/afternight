// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - vector seams shared by the demosaic references and CPU targets.
//
// The scalar implementations remain the authority.  A compiled CPU target calls the same method
// body and replaces only passes whose lane arithmetic can preserve the reference's association and
// branch decisions exactly.  Keeping those seams here avoids copying the large RCD, AMaZE and
// Markesteijn bodies into every instruction-set translation unit.

#pragma once

#include "kernels/reference/DemosaicKernels.h"

#include <cstdint>

namespace an
{

/// Lane-wise passes used by the ratio-corrected estimate.
///
/// Both passes work over the padded tile - the written region plus the nine-pixel reach on every
/// side, `rows` by `columns` - addressed at the dialect's fixed `scratchStride`. A null member runs
/// that pass's scalar text instead.
struct DemosaicRcdVectorOps
{
    /// Divides every sample of the padded tile by `whiteLevel`, clamps it to the unit interval, and
    /// stores the result in `cfa` and in all three colour planes.
    void (*initialize)(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
                       std::uint64_t columns, std::uint64_t scratchStride, float whiteLevel, float* cfa, float* red,
                       float* green, float* blue) noexcept = nullptr;

    /// Writes the region inside the reach to the tile's destination planes, each sample multiplied
    /// by `whiteLevel` and raised to zero when negative.
    void (*write)(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
                  std::uint64_t columns, std::uint64_t scratchStride, float whiteLevel, const float* red,
                  const float* green, const float* blue) noexcept = nullptr;
};

/// The widest padded tile whose horizontal colour differences the aliasing-minimised estimate bounds
/// lane-wise; a wider one runs that pass's scalar text.
inline constexpr std::uint64_t kDemosaicAmazeLaneColumns = 256u;

/// Lane-wise passes used by the aliasing-minimised estimate.
///
/// Both passes work over the padded tile - the written region plus the sixteen-pixel reach on
/// every side, `rows` by `columns`, with `columns` as the row stride. A null member runs that
/// pass's scalar text instead.
struct DemosaicAmazeVectorOps
{
    /// Divides every sample of the padded tile by `whiteLevel` and stores the result in `cfa` and in
    /// the green working plane `rgbGreen`, without clamping.
    void (*initialize)(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
                       std::uint64_t columns, float whiteLevel, float* cfa, float* rgbGreen) noexcept = nullptr;

    /// Computes, two samples in from every edge, the vertical and horizontal gradient weights and
    /// the sum of the squared horizontal and vertical spans across each sample, which the Nyquist
    /// test reads.
    void (*gradients)(const float* cfa, std::uint64_t rows, std::uint64_t columns, float* gradientSum,
                      float* verticalWeights, float* horizontalWeights) noexcept = nullptr;

    /// Computes, four samples in from every edge, the vertical and horizontal colour differences by
    /// adaptive ratio and by Hamilton-Adams, replaces the first by the second wherever the sample or
    /// either Hamilton-Adams estimate is above `highlightPoint`, and stores the two spreads the next
    /// pass chooses between them by. `rowStartsGreen` says, for each row, whether its fourth sample is
    /// a green site; the sites alternate from there.
    void (*colourDifferences)(const std::uint8_t* rowStartsGreen, std::uint64_t rows, std::uint64_t columns,
                              float highlightPoint, const float* cfa, const float* verticalWeights,
                              const float* horizontalWeights, float* verticalDifference, float* horizontalDifference,
                              float* verticalAlternate, float* horizontalAlternate, float* verticalSpread,
                              float* horizontalSpread) noexcept = nullptr;

    /// Chooses, on one row four samples in from every edge, the vertical colour difference or its
    /// Hamilton-Adams alternative by which varies less over the rows two above and two below, and
    /// bounds the choice where the colour is saturated or the green it implies is above `clipPoint`.
    /// Reads the final differences two rows above and the untouched ones two rows below, so a row
    /// may be written before the next is begun. `rowStartsGreen` says whether the row's fourth sample
    /// is a green site.
    void (*boundVertical)(std::uint64_t row, std::uint64_t columns, bool rowStartsGreen, float clipPoint,
                          const float* cfa, const float* verticalAlternate,
                          float* verticalDifference) noexcept = nullptr;

    /// Chooses, on every row four samples in from every edge, the horizontal colour difference or its
    /// Hamilton-Adams alternative by which varies less over the columns two to either side, bounds the
    /// choice where the colour is saturated or the green it implies is above `clipPoint`, and stores the
    /// squared difference between the final vertical and horizontal differences at every red and blue
    /// site. A row is a recurrence - each choice reads the final difference two columns to its left - but
    /// no row reads another, so rows can be taken together. Reads the final vertical differences, so it
    /// runs once every row's vertical bound is done, and only for a padded tile at most
    /// `kDemosaicAmazeLaneColumns` wide. `rowStartsGreen` says, for each row, whether its fourth sample is
    /// a green site.
    void (*boundHorizontal)(const std::uint8_t* rowStartsGreen, std::uint64_t rows, std::uint64_t columns,
                            float clipPoint, const float* cfa, const float* horizontalAlternate,
                            const float* verticalDifference, float* horizontalDifference,
                            float* differenceSquared) noexcept = nullptr;

    /// Weighs, at every red and blue site six samples in from every edge, the vertical estimate
    /// against the horizontal one by the variance and by the spread of each, and stores the weight in
    /// `directionWeight` at the site's half-resolution index. Called only for an even row width, where
    /// that index is the full index halved. `rowStartsGreen` says, for each row, whether its fourth
    /// sample is a green site.
    void (*directionWeights)(const std::uint8_t* rowStartsGreen, std::uint64_t rows, std::uint64_t columns,
                             const float* verticalDifference, const float* horizontalDifference,
                             const float* verticalWeights, const float* horizontalWeights, const float* verticalSpread,
                             const float* horizontalSpread, float* directionWeight) noexcept = nullptr;
};

/// Lane-wise passes shared by the one- and three-pass Markesteijn estimates.
///
/// Both work on planes of `pixels` samples laid out `rows` by `columns`, with `columns` as the row
/// stride. A null member runs that pass's scalar text instead.
struct DemosaicMarkesteijnVectorOps
{
    /// Converts one direction's interleaved red, green and blue into the default luminance and two
    /// scaled colour differences, ten samples in from every edge, writing the three planes of `lab`.
    /// Called only when the tile does not ask for the perceptual space.
    void (*lumaChroma)(const float* rgb, std::uint64_t pixels, std::uint64_t rows, std::uint64_t columns,
                       float* lab) noexcept = nullptr;

    /// Measures how much one direction bends - the summed squared second differences of the three
    /// planes of `lab` along `offset` - eleven samples in from every edge, into `destination`.
    /// `cieLab` says which space `lab` holds, which changes how the two colour terms are combined.
    void (*derivative)(const float* lab, std::uint64_t pixels, std::uint64_t rows, std::uint64_t columns,
                       std::int64_t offset, bool cieLab, float* destination) noexcept = nullptr;
};

/// Lane-wise passes used by the luminance-only adaptive homogeneity-directed estimate.
///
/// Every pass works over the padded tile - the written region plus the five-pixel reach on every side,
/// `rows` by `columns`, with `columns` as the row stride of the ten working planes - and runs the
/// reference's arithmetic in its order, lane by lane, its comparisons as `std::min` and `std::max` make
/// them. The lightness between the colour and the homogeneity passes stays scalar text. A null member
/// runs that pass's scalar text instead.
struct DemosaicLuminanceAhdVectorOps
{
    /// Writes both green candidates two samples in from every edge: the measured sample at a green
    /// site, and elsewhere the limited fifth-order estimate along each axis.
    void (*greens)(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
                   std::uint64_t columns, float* horizontalGreen, float* verticalGreen) noexcept = nullptr;
    /// Rebuilds red and blue against each green candidate three samples in from every edge, every
    /// estimated colour held to the sensor range.
    void (*colours)(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t rows,
                    std::uint64_t columns, const float* horizontalGreen, const float* verticalGreen,
                    float* horizontalRed, float* horizontalBlue, float* verticalRed,
                    float* verticalBlue) noexcept = nullptr;
    /// Counts each candidate's homogeneous neighbours four samples in from every edge.
    void (*homogeneity)(std::uint64_t rows, std::uint64_t columns, const float* horizontalLightness,
                        const float* verticalLightness, float* horizontalHomogeneity,
                        float* verticalHomogeneity) noexcept = nullptr;
    /// Writes the tile's region to its destination planes: at each pixel the candidate more of whose
    /// three-by-three neighbourhood is homogeneous, or the two candidates' average where they tie.
    /// `candidates` are the six colour planes, horizontal red, green and blue, then vertical.
    void (*choose)(const DemosaicTile& tile, std::uint32_t padBeginX, std::uint32_t padBeginY, std::uint64_t columns,
                   const float* const* candidates, const float* horizontalHomogeneity,
                   const float* verticalHomogeneity) noexcept = nullptr;
};

/// Runs the RCD authority with optional lane-wise passes.
///
/// @param tile the region to write, at most 176 by 176 pixels.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds; a shortfall writes nothing.
/// @param vectorOps the passes to run lane-wise, or null to run every pass as scalar text.
void demosaicRcdBody(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                     const DemosaicRcdVectorOps* vectorOps) noexcept;

/// Runs the AMaZE authority with optional lane-wise passes.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds; a shortfall writes nothing.
/// @param vectorOps the passes to run lane-wise, or null to run every pass as scalar text.
void demosaicAmazeBody(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                       const DemosaicAmazeVectorOps* vectorOps) noexcept;

/// Runs the shared Markesteijn authority with optional lane-wise passes.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds; a shortfall writes nothing.
/// @param passes one or three.
/// @param vectorOps the passes to run lane-wise, or null to run every pass as scalar text.
void demosaicMarkesteijnBodyWithOps(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                                    std::uint32_t passes, const DemosaicMarkesteijnVectorOps* vectorOps) noexcept;

/// Runs the luminance-only adaptive homogeneity-directed authority with optional lane-wise passes.
///
/// @param tile the region to write.
/// @param scratch the tile working planes.
/// @param scratchFloats how many floats the span holds; a shortfall writes nothing.
/// @param vectorOps the passes to run lane-wise, or null to run every pass as scalar text.
void demosaicLuminanceAhdBody(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                              const DemosaicLuminanceAhdVectorOps* vectorOps) noexcept;

} // namespace an
