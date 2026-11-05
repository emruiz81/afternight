// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DemosaicBayerKernel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Directional linear minimum mean-square-error Bayer interpolation.
//
// Derived from the LMMSE implementation in librtprocess's copy of RawTherapee (`demosaic/lmmse.cc`),
// which states `Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>`, was adapted to
// RawTherapee by Jacques Desmis in 2013 and made faster and smaller by Ingo Weyrich in 2015, and is
// distributed under the GNU General Public License, version 3 or any later version; it is itself based
// on Zhang and Wu, "Color Demosaicking via Directional Linear Minimum Mean-Square-Error Estimation"
// (2005). The core is retained: fifth-order horizontal and vertical green-minus-colour estimates,
// radius-four Gaussian low passes, local signal and residual variances, and inverse-uncertainty
// combination. The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// Four integration choices are deliberate. The hidden gamma encode and decode pass is not retained:
// this engine's request says the samples are linear and a demosaic does not silently change that
// domain. The optional post-passes - three-by-three median passes over the colour differences after
// the estimate - are not retained either. Five bounded tile planes replace the original's frame-sized
// buffers, and red and blue use the family's shared colour-difference reconstruction. Those choices
// make the result intentionally different from the original while preserving the method's
// noise-aware estimator.
//
// The variance floor is `1e-7` of the squared white level, with `1e-12` as its lower bound, so it
// scales with the stated numeric domain rather than assuming a sixteen-bit one.

namespace an
{
namespace
{

/// Full reach of the estimate, including final chrominance reconstruction.
constexpr std::uint32_t kPad = 12u;

/// Statistics of one directional estimate.
struct DirectionEstimate
{
    /// Estimated green-minus-colour difference.
    double value = 0.0;

    /// Local uncertainty; lower is more trustworthy.
    double uncertainty = 0.0;
};

/// Computes the LMMSE estimate at one point of one direction.
///
/// @param raw the unfiltered directional differences.
/// @param smooth their Gaussian low pass.
/// @param centre index of the point.
/// @param stride distance between samples along this direction.
/// @param epsilon scale-aware positive floor for both variances.
/// @return the estimated difference and its uncertainty.
[[nodiscard]] DirectionEstimate estimateDirection(const float* raw, const float* smooth, std::uint64_t centre,
                                                  std::uint64_t stride, double epsilon) noexcept
{
    double mean = 0.0;
    for (std::int32_t offset = -4; offset <= 4; ++offset)
    {
        const std::int64_t signedIndex = static_cast<std::int64_t>(centre) + offset * static_cast<std::int64_t>(stride);
        mean += smooth[static_cast<std::uint64_t>(signedIndex)];
    }
    mean /= 9.0;

    double signalVariance = epsilon;
    double residualVariance = epsilon;
    for (std::int32_t offset = -4; offset <= 4; ++offset)
    {
        const std::int64_t signedIndex = static_cast<std::int64_t>(centre) + offset * static_cast<std::int64_t>(stride);
        const std::uint64_t index = static_cast<std::uint64_t>(signedIndex);
        const double centred = static_cast<double>(smooth[index]) - mean;
        const double residual = static_cast<double>(smooth[index]) - static_cast<double>(raw[index]);
        signalVariance += centred * centred;
        residualVariance += residual * residual;
    }
    const double total = signalVariance + residualVariance;
    return DirectionEstimate{.value = (static_cast<double>(raw[centre]) * signalVariance +
                                       static_cast<double>(smooth[centre]) * residualVariance) /
                                      total,
                             .uncertainty = signalVariance * residualVariance / total};
}

} // namespace

void demosaicLmmseReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    const std::uint32_t regionWidth = tile.xEnd - tile.xBegin;
    const std::uint32_t regionHeight = tile.yEnd - tile.yBegin;
    if (regionWidth == 0u || regionHeight == 0u)
    {
        return;
    }
    const std::uint64_t rows = static_cast<std::uint64_t>(regionHeight) + 2u * kPad;
    const std::uint64_t columns = static_cast<std::uint64_t>(regionWidth) + 2u * kPad;
    const std::uint64_t plane = rows * columns;
    const std::uint64_t needed = 5u * plane;
    if (scratch == nullptr || scratchFloats < needed)
    {
        return;
    }
    std::memset(scratch, 0, static_cast<std::size_t>(needed) * sizeof(float));
    float* const horizontalRaw = scratch;
    float* const verticalRaw = horizontalRaw + plane;
    float* const horizontalSmooth = verticalRaw + plane;
    float* const verticalSmooth = horizontalSmooth + plane;
    float* const green = verticalSmooth + plane;

    const std::uint32_t padBeginX = tile.xBegin - kPad;
    const std::uint32_t padBeginY = tile.yBegin - kPad;

    // Directional green-minus-colour estimates at red and blue sites.
    for (std::uint64_t localY = 0u; localY < rows; ++localY)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
        for (std::uint64_t localX = 0u; localX < columns; ++localX)
        {
            const std::uint32_t x = padBeginX + static_cast<std::uint32_t>(localX);
            if (tile.cfa->colorAt(x, y) == CfaColor::Green)
            {
                continue;
            }
            const std::uint64_t index = localY * columns + localX;
            const float centre = demosaic_bayer_detail::sourceAt(tile, x, y);
            if (x >= 2u && x + 2u < tile.width)
            {
                horizontalRaw[index] =
                    demosaic_bayer_detail::fifthOrderGreen(centre, demosaic_bayer_detail::sourceAt(tile, x - 2u, y),
                                                           demosaic_bayer_detail::sourceAt(tile, x - 1u, y),
                                                           demosaic_bayer_detail::sourceAt(tile, x + 1u, y),
                                                           demosaic_bayer_detail::sourceAt(tile, x + 2u, y)) -
                    centre;
            }
            if (y >= 2u && y + 2u < tile.height)
            {
                verticalRaw[index] =
                    demosaic_bayer_detail::fifthOrderGreen(centre, demosaic_bayer_detail::sourceAt(tile, x, y - 2u),
                                                           demosaic_bayer_detail::sourceAt(tile, x, y - 1u),
                                                           demosaic_bayer_detail::sourceAt(tile, x, y + 1u),
                                                           demosaic_bayer_detail::sourceAt(tile, x, y + 2u)) -
                    centre;
            }
        }
    }

    // At green sites, carry the neighbouring colour-difference estimates across the direction in
    // which that same red or blue component was measured. This is the four-colour part of the
    // original construction without a separate fourth output channel.
    for (std::uint64_t localY = 1u; localY + 1u < rows; ++localY)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
        for (std::uint64_t localX = 1u; localX + 1u < columns; ++localX)
        {
            const std::uint32_t x = padBeginX + static_cast<std::uint32_t>(localX);
            if (tile.cfa->colorAt(x, y) != CfaColor::Green)
            {
                continue;
            }
            const std::uint64_t index = localY * columns + localX;
            horizontalRaw[index] = 0.5f * (horizontalRaw[index - 1u] + horizontalRaw[index + 1u]);
            verticalRaw[index] = 0.5f * (verticalRaw[index - columns] + verticalRaw[index + columns]);
        }
    }

    const float rawWeights[5] = {1.0f, std::exp(-1.0f / 8.0f), std::exp(-4.0f / 8.0f), std::exp(-9.0f / 8.0f),
                                 std::exp(-16.0f / 8.0f)};
    const float weightSum = rawWeights[0] + 2.0f * (rawWeights[1] + rawWeights[2] + rawWeights[3] + rawWeights[4]);
    float weights[5] = {};
    for (std::size_t index = 0u; index < 5u; ++index)
    {
        weights[index] = rawWeights[index] / weightSum;
    }
    for (std::uint64_t localY = 4u; localY + 4u < rows; ++localY)
    {
        for (std::uint64_t localX = 4u; localX + 4u < columns; ++localX)
        {
            const std::uint64_t index = localY * columns + localX;
            float horizontal = weights[0] * horizontalRaw[index];
            float vertical = weights[0] * verticalRaw[index];
            for (std::uint64_t offset = 1u; offset <= 4u; ++offset)
            {
                horizontal += weights[offset] * (horizontalRaw[index - offset] + horizontalRaw[index + offset]);
                vertical +=
                    weights[offset] * (verticalRaw[index - offset * columns] + verticalRaw[index + offset * columns]);
            }
            horizontalSmooth[index] = horizontal;
            verticalSmooth[index] = vertical;
        }
    }

    const double whiteLevel = static_cast<double>(tile.whiteLevel);
    const double epsilon = (std::max)(1.0e-12, whiteLevel * whiteLevel * 1.0e-7);
    for (std::uint64_t localY = kPad - 1u; localY <= rows - kPad; ++localY)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
        for (std::uint64_t localX = kPad - 1u; localX <= columns - kPad; ++localX)
        {
            const std::uint32_t x = padBeginX + static_cast<std::uint32_t>(localX);
            const std::uint64_t index = localY * columns + localX;
            const float centre = demosaic_bayer_detail::sourceAt(tile, x, y);
            if (tile.cfa->colorAt(x, y) == CfaColor::Green)
            {
                green[index] = centre;
                continue;
            }
            const DirectionEstimate horizontal = estimateDirection(horizontalRaw, horizontalSmooth, index, 1u, epsilon);
            const DirectionEstimate vertical = estimateDirection(verticalRaw, verticalSmooth, index, columns, epsilon);
            const double uncertainty = horizontal.uncertainty + vertical.uncertainty;
            const double difference =
                (horizontal.value * vertical.uncertainty + vertical.value * horizontal.uncertainty) / uncertainty;
            green[index] = static_cast<float>(static_cast<double>(centre) + difference);
        }
    }

    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t localY = y - padBeginY;
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
        {
            const std::uint64_t localX = x - padBeginX;
            float red = 0.0f;
            float greenValue = 0.0f;
            float blue = 0.0f;
            demosaic_bayer_detail::reconstruct(tile, green, columns, localX, localY, x, y, red, greenValue, blue);
            tile.red[destinationRow + x] = red;
            tile.green[destinationRow + x] = greenValue;
            tile.blue[destinationRow + x] = blue;
        }
    }
}

} // namespace an
