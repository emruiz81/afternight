// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DemosaicBayerKernel.h"
#include "kernels/reference/DemosaicKernelVectorOps.h"

#include <cstdint>

// Luminance-only adaptive homogeneity-directed Bayer interpolation.
//
// Derived from the AHD implementation in librtprocess's copy of RawTherapee (`demosaic/ahd.cc`), which
// states `Copyright (c) 2018 Ingo Weyrich (heckflosse67@gmx.de)` and is distributed under the GNU General
// Public License, version 3 or any later version, and from the method of Hirakawa, Parks and Lee it
// cites. The directional choice is made on perceptual lightness alone, with no chrominance criterion,
// and the method is named for that rather than promising chrominance-aware AHD. The provenance note is
// `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// Horizontal and vertical full-colour candidates, their perceptual lightness and their homogeneity
// maps are held in ten bounded tile planes. The original threshold and three-by-three agreement
// decision are retained, but the 65,536-entry lookup is replaced by direct domain-scaled arithmetic,
// so every tile is independent and no table can be indexed beyond the caller's stated numeric domain.
// Where the two candidates' agreement ties, the pixel is their average.
//
// The body takes optional lane-wise passes through `DemosaicLuminanceAhdVectorOps`: the reference passes
// none, and a compiled CPU target replaces the green candidates, the colour candidates, the homogeneity
// counts and the choice with lane-wise spellings of the same arithmetic. The lightness, a
// double-precision cube root, stays this scalar text on every path.

namespace an
{
namespace
{

/// Full reach of the original AHD's border.
constexpr std::uint32_t kPad = 5u;

} // namespace

void demosaicLuminanceAhdBody(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                              const DemosaicLuminanceAhdVectorOps* vectorOps) noexcept
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
    const std::uint64_t needed = 10u * plane;
    if (scratch == nullptr || scratchFloats < needed)
    {
        return;
    }

    float* const horizontalRed = scratch;
    float* const horizontalGreen = horizontalRed + plane;
    float* const horizontalBlue = horizontalGreen + plane;
    float* const verticalRed = horizontalBlue + plane;
    float* const verticalGreen = verticalRed + plane;
    float* const verticalBlue = verticalGreen + plane;
    float* const horizontalLightness = verticalBlue + plane;
    float* const verticalLightness = horizontalLightness + plane;
    float* const horizontalHomogeneity = verticalLightness + plane;
    float* const verticalHomogeneity = horizontalHomogeneity + plane;
    const std::uint32_t padBeginX = tile.xBegin - kPad;
    const std::uint32_t padBeginY = tile.yBegin - kPad;

    if (vectorOps != nullptr && vectorOps->greens != nullptr)
    {
        vectorOps->greens(tile, padBeginX, padBeginY, rows, columns, horizontalGreen, verticalGreen);
    }
    else
    {
        for (std::uint64_t localY = 2u; localY + 2u < rows; ++localY)
        {
            const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
            for (std::uint64_t localX = 2u; localX + 2u < columns; ++localX)
            {
                const std::uint32_t x = padBeginX + static_cast<std::uint32_t>(localX);
                const std::uint64_t index = localY * columns + localX;
                demosaic_bayer_detail::luminanceAhdGreens(tile, x, y, horizontalGreen[index], verticalGreen[index]);
            }
        }
    }

    if (vectorOps != nullptr && vectorOps->colours != nullptr)
    {
        vectorOps->colours(tile, padBeginX, padBeginY, rows, columns, horizontalGreen, verticalGreen, horizontalRed,
                           horizontalBlue, verticalRed, verticalBlue);
    }
    else
    {
        for (std::uint64_t localY = 3u; localY + 3u < rows; ++localY)
        {
            const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
            for (std::uint64_t localX = 3u; localX + 3u < columns; ++localX)
            {
                const std::uint32_t x = padBeginX + static_cast<std::uint32_t>(localX);
                demosaic_bayer_detail::luminanceAhdColours(tile, horizontalGreen, verticalGreen, columns, localX,
                                                           localY, x, y, horizontalRed, horizontalBlue, verticalRed,
                                                           verticalBlue);
            }
        }
    }
    for (std::uint64_t localY = 3u; localY + 3u < rows; ++localY)
    {
        for (std::uint64_t localX = 3u; localX + 3u < columns; ++localX)
        {
            const std::uint64_t index = localY * columns + localX;
            horizontalLightness[index] = demosaic_bayer_detail::luminanceAhdLightness(
                horizontalRed[index], horizontalGreen[index], horizontalBlue[index], tile.whiteLevel);
            verticalLightness[index] = demosaic_bayer_detail::luminanceAhdLightness(
                verticalRed[index], verticalGreen[index], verticalBlue[index], tile.whiteLevel);
        }
    }

    if (vectorOps != nullptr && vectorOps->homogeneity != nullptr)
    {
        vectorOps->homogeneity(rows, columns, horizontalLightness, verticalLightness, horizontalHomogeneity,
                               verticalHomogeneity);
    }
    else
    {
        for (std::uint64_t localY = 4u; localY + 4u < rows; ++localY)
        {
            for (std::uint64_t localX = 4u; localX + 4u < columns; ++localX)
            {
                const std::uint64_t index = localY * columns + localX;
                demosaic_bayer_detail::luminanceAhdHomogeneity(horizontalLightness, verticalLightness, index, columns,
                                                               horizontalHomogeneity[index],
                                                               verticalHomogeneity[index]);
            }
        }
    }

    const float* const candidates[6] = {horizontalRed, horizontalGreen, horizontalBlue,
                                        verticalRed,   verticalGreen,   verticalBlue};
    if (vectorOps != nullptr && vectorOps->choose != nullptr)
    {
        vectorOps->choose(tile, padBeginX, padBeginY, columns, candidates, horizontalHomogeneity, verticalHomogeneity);
        return;
    }
    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t localY = y - padBeginY;
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
        {
            const std::uint64_t localX = x - padBeginX;
            demosaic_bayer_detail::luminanceAhdChoose(candidates, horizontalHomogeneity, verticalHomogeneity,
                                                      localY * columns + localX, columns, tile.red[destinationRow + x],
                                                      tile.green[destinationRow + x], tile.blue[destinationRow + x]);
        }
    }
}

void demosaicLuminanceAhdReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    demosaicLuminanceAhdBody(tile, scratch, scratchFloats, nullptr);
}

} // namespace an
