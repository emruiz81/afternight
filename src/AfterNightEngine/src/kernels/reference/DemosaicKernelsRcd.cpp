// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DemosaicKernels.h"
#include "kernels/reference/DemosaicKernelVectorOps.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Ratio-corrected demosaicing (RCD), ported from release 2.3 of the algorithm.
//
// Derived from RawTherapee's RCD as librtprocess carries it (`demosaic/rcd.cc`), whose copyright line is
// `Copyright (c) 2017-2018 Luis Sanz Rodriguez (luis.sanz.rodriguez(at)gmail(dot)com) and Ingo Weyrich
// (heckflosse67@gmx.de)` - one line upstream, copyright and addresses together - tiled by Ingo Weyrich
// and distributed under the GNU General Public License, version 3 or any later version. The arithmetic
// is unchanged: the same directional discrimination from a squared colour-difference high-pass filter,
// the same low-pass
// ratio correction of the four cardinal green estimates, the same diagonal reconstruction of red
// and blue at the opposite sites, and the same cardinal reconstruction at the green sites.
// The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// **The tile geometry is the original's.** Every working plane has the original's fixed 194-sample
// row stride, even for a partial tile at the edge of the interior, because its half-resolution
// indexing is defined against that stride and a different tile extent changes the floating-point
// evaluation at tile seams. The body therefore accepts a region of at most 176 by 176 pixels, which
// is the shape `demosaicTileShape` reports.
//
// The body takes optional lane-wise passes through `DemosaicRcdVectorOps`: the reference passes
// none, and a compiled CPU target replaces the normalisation and the final write with lane-wise
// spellings of the same arithmetic. The passes whose sites of a row do not read what the pass writes
// say so to the compiler, so that it may take several sites at once; that changes no operation and no
// order of evaluation.
//
// Five things changed, and each is a defect or an unstated assumption rather than a preference.
//
// **The normalising scale is the caller's white level.** The original divides by 65536 on the way in
// and multiplies by 65536 on the way out, which is a statement about a sixteen-bit code rather than
// about the samples it was handed; combined with a shim that rescaled every source into that range
// it worked, and it is the reason no other numeric domain could reach the method. Here the
// demosaic plan states what a fully exposed photosite reads as, and that is what the method
// normalises by. The clamp to the unit interval is kept, because the ratio correction is only
// stable there - so a sample above the stated white level is clipped, deliberately and where a
// reader can see it.
//
// **Nothing is read before it is written.** The original leaves the third colour plane of each row
// uninitialised and reads it in the outer rings of every tile. Those rings are discarded before the
// output is written, so no pixel of a picture ever carried a garbage value - but the read happens,
// it is undefined, and a sanitizer finds it. Here all three planes start from the measured sample.
//
// **The tile no longer has to start on an even column.** The original addresses its half-resolution
// buffers by halving a full-resolution index, which is only correct while the tile origin is even -
// true of every tile the original generates, and an invariant nothing states or checks. Here the
// halving is written as an explicit half-plane index, and the executor is what guarantees the
// padded tile origin is even, because the parity of a picture column is what decides which filter
// covers it.
//
// **The estimate reads only inside its own padded tile.** The reach is nine pixels, which is the
// border the original uses, and the executor gives every tile that much halo inside the picture -
// so there is no bounds test here and no clamped read.
//
// **Every tile starts from cleared scratch.** The original allocates its working planes once per
// thread and reuses them, so a tile inherits whatever the previous tile left where a pass reads
// beyond what the pass before it wrote.

namespace an
{
namespace
{

/// Tolerance added to a gradient so that a flat neighbourhood does not divide by zero.
constexpr float kEpsilon = 1e-5f;

/// The same tolerance for a squared measure.
constexpr float kSquaredEpsilon = 1e-10f;

/// How far this estimate reads from the pixel it writes.
constexpr std::uint32_t kPad = 9u;

/// Fixed working extent of the adopted rtprocess RCD dialect.
constexpr std::uint64_t kTileExtent = 194u;

/// The square of a value.
///
/// @param value the value.
/// @return its square.
[[nodiscard]] inline float squared(float value) noexcept
{
    return value * value;
}

/// Linear interpolation between two values.
///
/// @param weight how much of the first value survives.
/// @param first the value at weight one.
/// @param second the value at weight zero.
/// @return the interpolated value.
[[nodiscard]] inline float mix(float weight, float first, float second) noexcept
{
    return weight * (first - second) + second;
}

/// The value clamped to the unit interval.
///
/// @param value the value.
/// @return zero below zero, one above one, the value between.
[[nodiscard]] inline float clampToUnit(float value) noexcept
{
    return (std::max)(0.0f, (std::min)(value, 1.0f));
}

} // namespace

void demosaicRcdBody(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                     const DemosaicRcdVectorOps* vectorOps) noexcept
{
    const std::uint32_t regionWidth = tile.xEnd - tile.xBegin;
    const std::uint32_t regionHeight = tile.yEnd - tile.yBegin;
    if (regionWidth == 0u || regionHeight == 0u)
    {
        return;
    }
    const std::uint64_t rows = static_cast<std::uint64_t>(regionHeight) + 2u * kPad;
    const std::uint64_t columns = static_cast<std::uint64_t>(regionWidth) + 2u * kPad;
    const std::uint64_t scratchStride = kTileExtent;
    const std::uint64_t halfColumns = scratchStride / 2u;
    const std::uint64_t plane = kTileExtent * kTileExtent;
    const std::uint64_t halfPlane = plane / 2u;
    const std::uint64_t needed = 5u * plane + 4u * halfPlane + 4u * scratchStride;
    if (scratchFloats < needed)
    {
        return;
    }

    std::memset(scratch, 0, static_cast<std::size_t>(needed) * sizeof(float));

    // The whole scratch starts from zero, and that is a correctness requirement rather than tidiness.
    // Several of the passes below read one ring beyond the region the pass before them wrote - the
    // original does the same, and gets away with it because its working buffers come from `calloc`.
    // A worker-private scratch partition is reused by every tile that worker takes, so the same read
    // would otherwise carry the previous tile's numbers into this one and make the answer depend on
    // how the tiles were scheduled.
    float* const cfa = scratch;
    float* const red = cfa + plane;
    float* const green = red + plane;
    float* const blue = green + plane;
    float* const vhDir = blue + plane;
    float* const lowPass = vhDir + plane;
    float* const pqDir = lowPass + halfPlane;
    float* const pHighPass = pqDir + halfPlane;
    float* const qHighPass = pHighPass + halfPlane;
    float* const rowBuffers = qHighPass + halfPlane;
    float* verticalOne = rowBuffers;
    float* verticalTwo = rowBuffers + scratchStride;
    float* verticalThree = rowBuffers + 2u * scratchStride;
    float* const horizontal = rowBuffers + 3u * scratchStride;
    float* const channels[3] = {red, green, blue};

    const std::uint32_t padBeginX = tile.xBegin - kPad;
    const std::uint32_t padBeginY = tile.yBegin - kPad;
    const CfaDescriptor& cfaDescriptor = *tile.cfa;
    const float whiteLevel = tile.whiteLevel;
    const std::uint64_t w1 = scratchStride;
    const std::uint64_t w2 = 2u * scratchStride;
    const std::uint64_t w3 = 3u * scratchStride;
    const std::uint64_t w4 = 4u * scratchStride;

    // Normalised measurements, and the two colours each row records parked in their planes.
    if (vectorOps != nullptr && vectorOps->initialize != nullptr)
    {
        vectorOps->initialize(tile, padBeginX, padBeginY, rows, columns, scratchStride, whiteLevel, cfa, red, green,
                              blue);
    }
    else
    {
        for (std::uint64_t row = 0u; row < rows; ++row)
        {
            const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
            const float* const sourceRow = tile.source + static_cast<std::uint64_t>(y) * tile.sourceStride + padBeginX;
            for (std::uint64_t column = 0u; column < columns; ++column)
            {
                const float measured = clampToUnit(sourceRow[column] / whiteLevel);
                const std::uint64_t index = row * scratchStride + column;
                cfa[index] = measured;
                red[index] = measured;
                green[index] = measured;
                blue[index] = measured;
            }
        }
    }

    // Which of the two cardinal directions the local detail runs along.
    for (std::uint64_t row = 3u; row < (std::min)(rows - 3u, std::uint64_t{5}); ++row)
    {
        for (std::uint64_t column = 4u; column < columns - 4u; ++column)
        {
            const std::uint64_t index = row * scratchStride + column;
            verticalOne[column] = squared((cfa[index - w3] - cfa[index - w1] - cfa[index + w1] + cfa[index + w3]) -
                                          3.0f * (cfa[index - w2] + cfa[index + w2]) + 6.0f * cfa[index]);
        }
        std::swap(verticalOne, verticalTwo);
    }

    for (std::uint64_t row = 4u; row < rows - 4u; ++row)
    {
        for (std::uint64_t column = 3u; column < columns - 3u; ++column)
        {
            const std::uint64_t index = row * scratchStride + column;
            horizontal[column] = squared((cfa[index - 3u] - cfa[index - 1u] - cfa[index + 1u] + cfa[index + 3u]) -
                                         3.0f * (cfa[index - 2u] + cfa[index + 2u]) + 6.0f * cfa[index]);
        }
        for (std::uint64_t column = 4u; column < columns - 4u; ++column)
        {
            const std::uint64_t index = (row + 1u) * scratchStride + column;
            verticalThree[column] = squared((cfa[index - w3] - cfa[index - w1] - cfa[index + w1] + cfa[index + w3]) -
                                            3.0f * (cfa[index - w2] + cfa[index + w2]) + 6.0f * cfa[index]);
        }
        for (std::uint64_t column = 4u; column < columns - 4u; ++column)
        {
            const float verticalStrength =
                (std::max)(kSquaredEpsilon, verticalOne[column] + verticalTwo[column] + verticalThree[column]);
            const float horizontalStrength =
                (std::max)(kSquaredEpsilon, horizontal[column - 1u] + horizontal[column] + horizontal[column + 1u]);
            vhDir[row * scratchStride + column] = verticalStrength / (verticalStrength + horizontalStrength);
        }
        std::swap(verticalOne, verticalThree);
        std::swap(verticalOne, verticalTwo);
    }

    // A low pass over each recorded colour, at the sites that recorded red and blue.
    for (std::uint64_t row = 2u; row < rows - 2u; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const std::uint64_t firstColumn = cfaDescriptor.colorAt(padBeginX, y) == CfaColor::Green ? 3u : 2u;
        // The low pass reads the mosaic and writes its own plane.
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t column = firstColumn; column < columns - 2u; column += 2u)
        {
            const std::uint64_t index = row * scratchStride + column;
            const std::uint64_t half = row * halfColumns + column / 2u;
            lowPass[half] =
                cfa[index] + 0.5f * (cfa[index - w1] + cfa[index + w1] + cfa[index - 1u] + cfa[index + 1u]) +
                0.25f * (cfa[index - w1 - 1u] + cfa[index - w1 + 1u] + cfa[index + w1 - 1u] + cfa[index + w1 + 1u]);
        }
    }

    // Green where red and blue were recorded.
    for (std::uint64_t row = 4u; row < rows - 4u; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const std::uint64_t firstColumn = cfaDescriptor.colorAt(padBeginX, y) == CfaColor::Green ? 5u : 4u;
        // Green is written at the red and blue sites and read nowhere in this pass.
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t column = firstColumn; column < columns - 4u; column += 2u)
        {
            const std::uint64_t index = row * scratchStride + column;
            const std::uint64_t half = row * halfColumns + column / 2u;
            const float centre = cfa[index];
            const float verticalSpan = std::fabs(cfa[index - w1] - cfa[index + w1]);
            const float horizontalSpan = std::fabs(cfa[index - 1u] - cfa[index + 1u]);
            const float northGradient =
                kEpsilon + (verticalSpan + std::fabs(centre - cfa[index - w2])) +
                (std::fabs(cfa[index - w1] - cfa[index - w3]) + std::fabs(cfa[index - w2] - cfa[index - w4]));
            const float southGradient =
                kEpsilon + (verticalSpan + std::fabs(centre - cfa[index + w2])) +
                (std::fabs(cfa[index + w1] - cfa[index + w3]) + std::fabs(cfa[index + w2] - cfa[index + w4]));
            const float westGradient =
                kEpsilon + (horizontalSpan + std::fabs(centre - cfa[index - 2u])) +
                (std::fabs(cfa[index - 1u] - cfa[index - 3u]) + std::fabs(cfa[index - 2u] - cfa[index - 4u]));
            const float eastGradient =
                kEpsilon + (horizontalSpan + std::fabs(centre - cfa[index + 2u])) +
                (std::fabs(cfa[index + 1u] - cfa[index + 3u]) + std::fabs(cfa[index + 2u] - cfa[index + 4u]));

            const float centreLowPass = lowPass[half];
            const float northEstimate = cfa[index - w1] * (centreLowPass + centreLowPass) /
                                        (kEpsilon + centreLowPass + lowPass[half - 2u * halfColumns]);
            const float southEstimate = cfa[index + w1] * (centreLowPass + centreLowPass) /
                                        (kEpsilon + centreLowPass + lowPass[half + 2u * halfColumns]);
            const float westEstimate =
                cfa[index - 1u] * (centreLowPass + centreLowPass) / (kEpsilon + centreLowPass + lowPass[half - 1u]);
            const float eastEstimate =
                cfa[index + 1u] * (centreLowPass + centreLowPass) / (kEpsilon + centreLowPass + lowPass[half + 1u]);

            const float verticalEstimate =
                (southGradient * northEstimate + northGradient * southEstimate) / (northGradient + southGradient);
            const float horizontalEstimate =
                (westGradient * eastEstimate + eastGradient * westEstimate) / (eastGradient + westGradient);

            const float centreDirection = vhDir[index];
            const float neighbourDirection = 0.25f * ((vhDir[index - w1 - 1u] + vhDir[index - w1 + 1u]) +
                                                      (vhDir[index + w1 - 1u] + vhDir[index + w1 + 1u]));
            const float direction = std::fabs(0.5f - centreDirection) < std::fabs(0.5f - neighbourDirection)
                                        ? neighbourDirection
                                        : centreDirection;
            green[index] = mix(direction, horizontalEstimate, verticalEstimate);
        }
    }

    // Which diagonal the local detail runs along.
    for (std::uint64_t row = 3u; row < rows - 3u; ++row)
    {
        // Both high passes read the mosaic and write their own planes.
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t column = 3u; column < columns - 3u; column += 2u)
        {
            const std::uint64_t index = row * scratchStride + column;
            const std::uint64_t half = row * halfColumns + column / 2u;
            pHighPass[half] =
                squared((cfa[index - w3 - 3u] - cfa[index - w1 - 1u] - cfa[index + w1 + 1u] + cfa[index + w3 + 3u]) -
                        3.0f * (cfa[index - w2 - 2u] + cfa[index + w2 + 2u]) + 6.0f * cfa[index]);
            qHighPass[half] =
                squared((cfa[index - w3 + 3u] - cfa[index - w1 + 1u] - cfa[index + w1 - 1u] + cfa[index + w3 - 3u]) -
                        3.0f * (cfa[index - w2 + 2u] + cfa[index + w2 - 2u]) + 6.0f * cfa[index]);
        }
    }

    for (std::uint64_t row = 4u; row < rows - 4u; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const std::uint64_t firstColumn = cfaDescriptor.colorAt(padBeginX, y) == CfaColor::Green ? 5u : 4u;
        for (std::uint64_t column = firstColumn; column < columns - 4u; column += 2u)
        {
            const std::uint64_t half = row * halfColumns + column / 2u;
            const std::uint64_t above = (row - 1u) * halfColumns + (column - 1u) / 2u;
            const std::uint64_t below = (row + 1u) * halfColumns + (column - 1u) / 2u;
            const float pStrength =
                (std::max)(kSquaredEpsilon, pHighPass[above] + pHighPass[half] + pHighPass[below + 1u]);
            const float qStrength =
                (std::max)(kSquaredEpsilon, qHighPass[above + 1u] + qHighPass[half] + qHighPass[below]);
            pqDir[half] = pStrength / (pStrength + qStrength);
        }
    }

    // Red where blue was recorded, and blue where red was, from the two diagonals.
    for (std::uint64_t row = 4u; row < rows - 4u; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const std::uint64_t firstColumn = cfaDescriptor.colorAt(padBeginX, y) == CfaColor::Green ? 5u : 4u;
        const std::size_t opposite =
            cfaDescriptor.colorAt(padBeginX + static_cast<std::uint32_t>(firstColumn), y) == CfaColor::Red ? 2u : 0u;
        float* const target = channels[opposite];
        for (std::uint64_t column = firstColumn; column < columns - 4u; column += 2u)
        {
            const std::uint64_t index = row * scratchStride + column;
            const std::uint64_t half = row * halfColumns + column / 2u;
            const std::uint64_t above = (row - 1u) * halfColumns + (column - 1u) / 2u;
            const std::uint64_t below = (row + 1u) * halfColumns + (column - 1u) / 2u;
            const float centreDirection = pqDir[half];
            const float neighbourDirection =
                0.25f * (pqDir[above] + pqDir[above + 1u] + pqDir[below] + pqDir[below + 1u]);
            const float direction = std::fabs(0.5f - centreDirection) < std::fabs(0.5f - neighbourDirection)
                                        ? neighbourDirection
                                        : centreDirection;

            const float northWest = target[index - w1 - 1u];
            const float northEast = target[index - w1 + 1u];
            const float southWest = target[index + w1 - 1u];
            const float southEast = target[index + w1 + 1u];
            const float northWestGradient = kEpsilon + std::fabs(northWest - southEast) +
                                            std::fabs(northWest - target[index - w3 - 3u]) +
                                            std::fabs(green[index] - green[index - w2 - 2u]);
            const float northEastGradient = kEpsilon + std::fabs(northEast - southWest) +
                                            std::fabs(northEast - target[index - w3 + 3u]) +
                                            std::fabs(green[index] - green[index - w2 + 2u]);
            const float southWestGradient = kEpsilon + std::fabs(northEast - southWest) +
                                            std::fabs(southWest - target[index + w3 - 3u]) +
                                            std::fabs(green[index] - green[index + w2 - 2u]);
            const float southEastGradient = kEpsilon + std::fabs(northWest - southEast) +
                                            std::fabs(southEast - target[index + w3 + 3u]) +
                                            std::fabs(green[index] - green[index + w2 + 2u]);

            const float northWestEstimate = northWest - green[index - w1 - 1u];
            const float northEastEstimate = northEast - green[index - w1 + 1u];
            const float southWestEstimate = southWest - green[index + w1 - 1u];
            const float southEastEstimate = southEast - green[index + w1 + 1u];

            const float pEstimate = (northWestGradient * southEastEstimate + southEastGradient * northWestEstimate) /
                                    (northWestGradient + southEastGradient);
            const float qEstimate = (northEastGradient * southWestEstimate + southWestGradient * northEastEstimate) /
                                    (northEastGradient + southWestGradient);
            target[index] = green[index] + mix(direction, qEstimate, pEstimate);
        }
    }

    // Red and blue where green was recorded, from the two cardinal directions.
    for (std::uint64_t row = 4u; row < rows - 4u; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const std::uint64_t firstColumn = cfaDescriptor.colorAt(padBeginX + 1u, y) == CfaColor::Green ? 5u : 4u;
        // Red and blue are written at the green sites and read at the red and blue ones only.
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t column = firstColumn; column < columns - 4u; column += 2u)
        {
            const std::uint64_t index = row * scratchStride + column;
            const float centreDirection = vhDir[index];
            const float neighbourDirection = 0.25f * ((vhDir[index - w1 - 1u] + vhDir[index - w1 + 1u]) +
                                                      (vhDir[index + w1 - 1u] + vhDir[index + w1 + 1u]));
            const float direction = std::fabs(0.5f - centreDirection) < std::fabs(0.5f - neighbourDirection)
                                        ? neighbourDirection
                                        : centreDirection;
            const float centreGreen = green[index];
            const float northBase = kEpsilon + std::fabs(centreGreen - green[index - w2]);
            const float southBase = kEpsilon + std::fabs(centreGreen - green[index + w2]);
            const float westBase = kEpsilon + std::fabs(centreGreen - green[index - 2u]);
            const float eastBase = kEpsilon + std::fabs(centreGreen - green[index + 2u]);
            const float greenNorth = green[index - w1];
            const float greenSouth = green[index + w1];
            const float greenWest = green[index - 1u];
            const float greenEast = green[index + 1u];
            for (std::size_t colour = 0u; colour <= 2u; colour += 2u)
            {
                float* const target = channels[colour];
                const float verticalSpan = std::fabs(target[index - w1] - target[index + w1]);
                const float horizontalSpan = std::fabs(target[index - 1u] - target[index + 1u]);
                const float northGradient =
                    northBase + verticalSpan + std::fabs(target[index - w1] - target[index - w3]);
                const float southGradient =
                    southBase + verticalSpan + std::fabs(target[index + w1] - target[index + w3]);
                const float westGradient =
                    westBase + horizontalSpan + std::fabs(target[index - 1u] - target[index - 3u]);
                const float eastGradient =
                    eastBase + horizontalSpan + std::fabs(target[index + 1u] - target[index + 3u]);

                const float northEstimate = target[index - w1] - greenNorth;
                const float southEstimate = target[index + w1] - greenSouth;
                const float westEstimate = target[index - 1u] - greenWest;
                const float eastEstimate = target[index + 1u] - greenEast;

                const float verticalEstimate =
                    (northGradient * southEstimate + southGradient * northEstimate) / (northGradient + southGradient);
                const float horizontalEstimate =
                    (eastGradient * westEstimate + westGradient * eastEstimate) / (eastGradient + westGradient);
                target[index] = centreGreen + mix(direction, horizontalEstimate, verticalEstimate);
            }
        }
    }

    if (vectorOps != nullptr && vectorOps->write != nullptr)
    {
        vectorOps->write(tile, padBeginX, padBeginY, rows, columns, scratchStride, whiteLevel, red, green, blue);
    }
    else
    {
        for (std::uint64_t row = kPad; row < rows - kPad; ++row)
        {
            const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
            const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
            for (std::uint64_t column = kPad; column < columns - kPad; ++column)
            {
                const std::uint64_t index = row * scratchStride + column;
                const std::uint64_t destination = destinationRow + padBeginX + column;
                tile.red[destination] = (std::max)(0.0f, red[index] * whiteLevel);
                tile.green[destination] = (std::max)(0.0f, green[index] * whiteLevel);
                tile.blue[destination] = (std::max)(0.0f, blue[index] * whiteLevel);
            }
        }
    }
}

void demosaicRcdReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    demosaicRcdBody(tile, scratch, scratchFloats, nullptr);
}

} // namespace an
