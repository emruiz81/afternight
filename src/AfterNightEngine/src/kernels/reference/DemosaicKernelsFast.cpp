// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DemosaicKernels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// The gradient-weighted estimate ("Fast").
//
// Derived from RawTherapee's fast demosaic as librtprocess carries it (`demosaic/bayerfast.cc`), which
// states `copyright (c) 2008-2010  Emil Martinec <ejmartin@uchicago.edu>` and is distributed under the
// GNU General Public License, version 3 or any later version. The arithmetic is the scalar dialect of
// that implementation: the weights are the reciprocal of a squared local gradient, the red and blue
// estimates are colour differences against the green plane, and the four-neighbour sum is clamped at
// the clip point before it is subtracted. The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// Three things changed in the port, and each is a defect rather than a preference.
//
// **The tile no longer has to start on an even column.** The original derives which columns of a
// row hold green filters from the colour of column two of the picture, and then indexes the tile
// with an offset that is only correct because every tile origin it generates happens to be odd. A
// tile whose origin moved would silently interpolate the wrong sites. Here each row asks the mosaic
// what covers its first two pixels and alternates from there, so the arithmetic is a function of
// where the pixels are rather than of how the picture was divided.
//
// **The clip point is the caller's.** The original clamps the four-neighbour sum at four times full
// scale of a sixteen-bit code, which is a number about the domain its shim converted into rather
// than about the samples. Here it is four times the clip point the demosaic plan states.
//
// **The weights are computed with division.** The vector dialect of the original uses a reciprocal
// approximation accurate to about twelve bits, so the shipped library already produces different
// pixels on the two compilers that compile the two dialects. The reference divides, and the vector
// kernel is held to the reference.

namespace an
{
namespace
{

/// The directional weight one gradient earns.
///
/// Sixteen over the square of four plus the gradient: flat in the direction of a small gradient and
/// falling away quadratically, which is what stops an edge being averaged across.
///
/// @param gradient the summed absolute differences along one direction.
/// @return the weight, which is at most one and always above zero for a finite gradient.
[[nodiscard]] inline float directionalWeight(float gradient) noexcept
{
    const float base = 4.0f + gradient;
    return 16.0f / (base * base);
}

} // namespace

void demosaicFastReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    constexpr std::uint32_t kPad = 2u;
    const std::uint32_t regionWidth = tile.xEnd - tile.xBegin;
    const std::uint32_t regionHeight = tile.yEnd - tile.yBegin;
    const std::uint64_t paddedWidth = static_cast<std::uint64_t>(regionWidth) + 2u * kPad;
    const std::uint64_t paddedHeight = static_cast<std::uint64_t>(regionHeight) + 2u * kPad;
    const std::uint64_t plane = paddedWidth * paddedHeight;
    if (regionWidth == 0u || regionHeight == 0u || scratchFloats < 3u * plane)
    {
        return;
    }
    float* const greenTile = scratch;
    float* const redTile = scratch + plane;
    float* const blueTile = scratch + 2u * plane;

    const std::uint32_t padBeginX = tile.xBegin - kPad;
    const std::uint32_t padBeginY = tile.yBegin - kPad;
    const CfaDescriptor& cfa = *tile.cfa;
    const float clipSum = 4.0f * tile.clipPoint;

    // Green everywhere, and the measured sample parked in both colour planes.
    for (std::uint32_t y = padBeginY; y < padBeginY + paddedHeight; ++y)
    {
        const std::uint64_t row = static_cast<std::uint64_t>(y) * tile.sourceStride;
        const std::uint64_t tileRow = static_cast<std::uint64_t>(y - padBeginY) * paddedWidth;
        const bool greenAtStart = cfa.colorAt(padBeginX, y) == CfaColor::Green;
        for (std::uint32_t x = padBeginX; x < padBeginX + paddedWidth; ++x)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            const float measured = tile.source[row + x];
            redTile[index] = measured;
            blueTile[index] = measured;
            const bool isGreen = (((x - padBeginX) & 1u) == 0u) == greenAtStart;
            if (isGreen)
            {
                greenTile[index] = measured;
                continue;
            }
            const float above = tile.source[row - tile.sourceStride + x];
            const float below = tile.source[row + tile.sourceStride + x];
            const float left = tile.source[row + x - 1u];
            const float right = tile.source[row + x + 1u];
            const float verticalSpan = std::fabs(below - above);
            const float horizontalSpan = std::fabs(right - left);
            const float upward =
                directionalWeight(verticalSpan + std::fabs(measured - tile.source[row - 2u * tile.sourceStride + x]) +
                                  std::fabs(above - tile.source[row - 3u * tile.sourceStride + x]));
            const float downward =
                directionalWeight(verticalSpan + std::fabs(measured - tile.source[row + 2u * tile.sourceStride + x]) +
                                  std::fabs(below - tile.source[row + 3u * tile.sourceStride + x]));
            const float leftward = directionalWeight(horizontalSpan + std::fabs(measured - tile.source[row + x - 2u]) +
                                                     std::fabs(left - tile.source[row + x - 3u]));
            const float rightward = directionalWeight(horizontalSpan + std::fabs(measured - tile.source[row + x + 2u]) +
                                                      std::fabs(right - tile.source[row + x + 3u]));
            greenTile[index] = (upward * above + downward * below + leftward * left + rightward * right) /
                               (upward + downward + leftward + rightward);
        }
    }

    // The opposite colour at every red and blue site, from the colour difference over its diagonals.
    for (std::uint32_t y = tile.yBegin - 1u; y < tile.yEnd + 1u; ++y)
    {
        const std::uint64_t row = static_cast<std::uint64_t>(y) * tile.sourceStride;
        const std::uint64_t tileRow = static_cast<std::uint64_t>(y - padBeginY) * paddedWidth;
        const std::uint32_t firstX = tile.xBegin - 1u;
        const bool greenAtStart = cfa.colorAt(firstX, y) == CfaColor::Green;
        const std::uint32_t offset = greenAtStart ? 1u : 0u;
        const bool writesBlue = cfa.colorAt(firstX + offset, y) == CfaColor::Red;
        float* const destinationPlane = writesBlue ? blueTile : redTile;
        for (std::uint32_t x = firstX + offset; x < tile.xEnd + 1u; x += 2u)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            const std::uint64_t above = index - paddedWidth;
            const std::uint64_t below = index + paddedWidth;
            const float greenCorners =
                greenTile[above - 1u] + greenTile[above + 1u] + greenTile[below + 1u] + greenTile[below - 1u];
            const float measuredCorners =
                tile.source[row - tile.sourceStride + x - 1u] + tile.source[row - tile.sourceStride + x + 1u] +
                tile.source[row + tile.sourceStride + x + 1u] + tile.source[row + tile.sourceStride + x - 1u];
            destinationPlane[index] = greenTile[index] - 0.25f * (greenCorners - (std::min)(clipSum, measuredCorners));
        }
    }

    // Both colours at every green site, from the colour differences of its four neighbours.
    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t tileRow = static_cast<std::uint64_t>(y - padBeginY) * paddedWidth;
        const bool greenAtStart = cfa.colorAt(tile.xBegin, y) == CfaColor::Green;
        const std::uint32_t offset = greenAtStart ? 0u : 1u;
        for (std::uint32_t x = tile.xBegin + offset; x < tile.xEnd; x += 2u)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            const std::uint64_t above = index - paddedWidth;
            const std::uint64_t below = index + paddedWidth;
            const float centre = greenTile[index];
            redTile[index] =
                centre -
                0.25f * ((greenTile[above] - redTile[above]) + (greenTile[below] - redTile[below]) +
                         (greenTile[index - 1u] - redTile[index - 1u]) + (greenTile[index + 1u] - redTile[index + 1u]));
            blueTile[index] =
                centre - 0.25f * ((greenTile[above] - blueTile[above]) + (greenTile[below] - blueTile[below]) +
                                  (greenTile[index - 1u] - blueTile[index - 1u]) +
                                  (greenTile[index + 1u] - blueTile[index + 1u]));
        }
    }

    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t tileRow = static_cast<std::uint64_t>(y - padBeginY) * paddedWidth;
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
        {
            const std::uint64_t index = tileRow + (x - padBeginX);
            tile.red[destinationRow + x] = redTile[index];
            tile.green[destinationRow + x] = greenTile[index];
            tile.blue[destinationRow + x] = blueTile[index];
        }
    }
}

} // namespace an
