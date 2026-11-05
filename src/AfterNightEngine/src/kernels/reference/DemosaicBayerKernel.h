// AfterNightEngine - shared scalar arithmetic for Bayer demosaic kernels.
//
// The Bayer methods that estimate a complete green plane first - LMMSE, VNG4 and the luminance-only
// adaptive homogeneity-directed estimate - finish the same way: red and blue are rebuilt from their
// colour difference against that green. That step and the highlight-limited fifth-order green
// estimate two of them share are written here once, so the three methods differ only in how they
// arrive at green. The luminance-only estimate's per-pixel steps follow, written once for its scalar
// authority and for the columns its compiled passes leave to scalar text.
//
// Everything here reads a neighbourhood without a bounds test. The caller's tile halo is what keeps
// every read inside the picture and inside its padded green plane, as it is for every kernel of the
// family.
//
// The fifth-order green estimate and its median limiter, and the luminance-only estimate's per-pixel
// steps, are adapted from librtprocess's copy of RawTherapee: `demosaic/lmmse.cc`, which states
// `Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>`, and `demosaic/ahd.cc`, which states
// `Copyright (c) 2018 Ingo Weyrich (heckflosse67@gmx.de)`, both distributed under the GNU General Public
// License, version 3 or any later version; this file is part of that derived work and is distributed
// under the same licence. The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "kernels/reference/DemosaicKernels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

/// Scalar arithmetic shared by the Bayer demosaic kernels that reconstruct red and blue from a
/// complete green plane.
namespace an::demosaic_bayer_detail
{

/// Reads one mosaic sample at picture coordinates.
///
/// @param tile the picture.
/// @param x the column.
/// @param y the row.
/// @return the stored sample.
[[nodiscard]] inline float sourceAt(const DemosaicTile& tile, std::uint32_t x, std::uint32_t y) noexcept
{
    return tile.source[static_cast<std::uint64_t>(y) * tile.sourceStride + x];
}

/// The middle of three values.
///
/// @param first one value.
/// @param second another value.
/// @param third the final value.
/// @return the median.
[[nodiscard]] inline float median3(float first, float second, float third) noexcept
{
    return (std::max)((std::min)(first, second), (std::min)(third, (std::max)(first, second)));
}

/// A fifth-order green estimate along one axis.
///
/// The two adjacent samples are green at a red or blue Bayer site, and the samples two sites away
/// have the centre's colour. Taking the median with the adjacent measurements is the highlight-safe
/// limiter used by both the retained LMMSE and luminance-AHD dialects.
///
/// @param centre the measured red or blue sample.
/// @param beforeTwo the like-coloured sample two positions before it.
/// @param beforeOne the adjacent green sample.
/// @param afterOne the adjacent green sample.
/// @param afterTwo the like-coloured sample two positions after it.
/// @return the limited green estimate.
[[nodiscard]] inline float fifthOrderGreen(float centre, float beforeTwo, float beforeOne, float afterOne,
                                           float afterTwo) noexcept
{
    const float estimate = 0.25f * (2.0f * (beforeOne + centre + afterOne) - beforeTwo - afterTwo);
    return median3(estimate, beforeOne, afterOne);
}

/// Reconstructs red and blue from a complete green plane and the measured mosaic.
///
/// At a green site the two colour differences are interpolated along the axis on which that colour
/// was measured. At a red or blue site the opposite colour difference comes from the four diagonal
/// sites. The measured colour and the supplied green are copied exactly.
///
/// It reads the pixel's eight neighbours in both the mosaic and the green plane, so the pixel must
/// lie at least one sample inside both.
///
/// @param tile the picture and filter array.
/// @param green the padded green plane.
/// @param columns floats between rows of the green plane.
/// @param localX the pixel's column in that plane.
/// @param localY the pixel's row in that plane.
/// @param x the picture column.
/// @param y the picture row.
/// @param red set to the red estimate.
/// @param greenValue set to the green estimate.
/// @param blue set to the blue estimate.
inline void reconstruct(const DemosaicTile& tile, const float* green, std::uint64_t columns, std::uint64_t localX,
                        std::uint64_t localY, std::uint32_t x, std::uint32_t y, float& red, float& greenValue,
                        float& blue) noexcept
{
    const std::uint64_t index = localY * columns + localX;
    const float measured = sourceAt(tile, x, y);
    greenValue = green[index];
    const CfaColor own = tile.cfa->colorAt(x, y);
    if (own == CfaColor::Green)
    {
        const bool redIsHorizontal = tile.cfa->colorAt(x - 1u, y) == CfaColor::Red;
        const float horizontal = greenValue + 0.5f * ((sourceAt(tile, x - 1u, y) - green[index - 1u]) +
                                                      (sourceAt(tile, x + 1u, y) - green[index + 1u]));
        const float vertical = greenValue + 0.5f * ((sourceAt(tile, x, y - 1u) - green[index - columns]) +
                                                    (sourceAt(tile, x, y + 1u) - green[index + columns]));
        red = redIsHorizontal ? horizontal : vertical;
        blue = redIsHorizontal ? vertical : horizontal;
        return;
    }

    const float opposite = greenValue + 0.25f * ((sourceAt(tile, x - 1u, y - 1u) - green[index - columns - 1u]) +
                                                 (sourceAt(tile, x + 1u, y - 1u) - green[index - columns + 1u]) +
                                                 (sourceAt(tile, x - 1u, y + 1u) - green[index + columns - 1u]) +
                                                 (sourceAt(tile, x + 1u, y + 1u) - green[index + columns + 1u]));
    if (own == CfaColor::Red)
    {
        red = measured;
        blue = opposite;
    }
    else
    {
        red = opposite;
        blue = measured;
    }
}

/// Keeps an interpolated channel of the luminance-only AHD estimate in the stated sensor range.
///
/// @param value the estimate.
/// @param whiteLevel the stated fully exposed value.
/// @return the bounded estimate, as `std::min` and `std::max` order the comparisons.
[[nodiscard]] inline float luminanceAhdClamp(float value, float whiteLevel) noexcept
{
    return (std::min)(whiteLevel, (std::max)(0.0f, value));
}

/// The perceptual lightness the luminance-only AHD estimate decides its direction by.
///
/// @param red red estimate.
/// @param green green estimate.
/// @param blue blue estimate.
/// @param whiteLevel the stated fully exposed value.
/// @return CIE lightness of the all-ones-camera-matrix channel sum, scaled without a lookup table.
[[nodiscard]] inline float luminanceAhdLightness(float red, float green, float blue, float whiteLevel) noexcept
{
    const double relative = (static_cast<double>(red) + static_cast<double>(green) + static_cast<double>(blue)) /
                            static_cast<double>(whiteLevel);
    constexpr double kTransition = 0.008856;
    const double mapped = relative > kTransition ? std::cbrt(relative) : 7.787 * relative + 16.0 / 116.0;
    return static_cast<float>(116.0 * mapped - 16.0);
}

/// Both green candidates of the luminance-only AHD estimate at one pixel: the measured sample at a
/// green site, and elsewhere the limited fifth-order estimate along each axis.
///
/// @param tile the picture and filter array.
/// @param x the picture column, at least two samples inside the tile's halo.
/// @param y the picture row, at least two samples inside the tile's halo.
/// @param horizontal set to the horizontal candidate.
/// @param vertical set to the vertical candidate.
inline void luminanceAhdGreens(const DemosaicTile& tile, std::uint32_t x, std::uint32_t y, float& horizontal,
                               float& vertical) noexcept
{
    const float measured = sourceAt(tile, x, y);
    if (tile.cfa->colorAt(x, y) == CfaColor::Green)
    {
        horizontal = measured;
        vertical = measured;
        return;
    }
    horizontal = fifthOrderGreen(measured, sourceAt(tile, x - 2u, y), sourceAt(tile, x - 1u, y),
                                 sourceAt(tile, x + 1u, y), sourceAt(tile, x + 2u, y));
    vertical = fifthOrderGreen(measured, sourceAt(tile, x, y - 2u), sourceAt(tile, x, y - 1u),
                               sourceAt(tile, x, y + 1u), sourceAt(tile, x, y + 2u));
}

/// Red and blue against both green candidates of the luminance-only AHD estimate at one pixel,
/// every estimated colour held to the sensor range.
///
/// @param tile the picture and filter array.
/// @param horizontalGreen the horizontal green candidate plane.
/// @param verticalGreen the vertical green candidate plane.
/// @param columns floats between rows of the candidate planes.
/// @param localX the pixel's column in the planes.
/// @param localY the pixel's row in the planes.
/// @param x the picture column.
/// @param y the picture row.
/// @param horizontalRed the horizontal red plane, written at the pixel.
/// @param horizontalBlue the horizontal blue plane, written at the pixel.
/// @param verticalRed the vertical red plane, written at the pixel.
/// @param verticalBlue the vertical blue plane, written at the pixel.
inline void luminanceAhdColours(const DemosaicTile& tile, const float* horizontalGreen, const float* verticalGreen,
                                std::uint64_t columns, std::uint64_t localX, std::uint64_t localY, std::uint32_t x,
                                std::uint32_t y, float* horizontalRed, float* horizontalBlue, float* verticalRed,
                                float* verticalBlue) noexcept
{
    const std::uint64_t index = localY * columns + localX;
    // The green a reconstruction reports is the candidate's own sample, which the plane already holds.
    float unchanged = 0.0f;
    reconstruct(tile, horizontalGreen, columns, localX, localY, x, y, horizontalRed[index], unchanged,
                horizontalBlue[index]);
    reconstruct(tile, verticalGreen, columns, localX, localY, x, y, verticalRed[index], unchanged, verticalBlue[index]);
    const CfaColor own = tile.cfa->colorAt(x, y);
    if (own != CfaColor::Red)
    {
        horizontalRed[index] = luminanceAhdClamp(horizontalRed[index], tile.whiteLevel);
        verticalRed[index] = luminanceAhdClamp(verticalRed[index], tile.whiteLevel);
    }
    if (own != CfaColor::Blue)
    {
        horizontalBlue[index] = luminanceAhdClamp(horizontalBlue[index], tile.whiteLevel);
        verticalBlue[index] = luminanceAhdClamp(verticalBlue[index], tile.whiteLevel);
    }
}

/// Homogeneous-neighbour counts of both luminance-only AHD candidates at one pixel.
///
/// @param horizontalLightness horizontal candidate lightness plane.
/// @param verticalLightness vertical candidate lightness plane.
/// @param index centre index.
/// @param columns floats between rows.
/// @param horizontal set to the horizontal candidate's count.
/// @param vertical set to the vertical candidate's count.
inline void luminanceAhdHomogeneity(const float* horizontalLightness, const float* verticalLightness,
                                    std::uint64_t index, std::uint64_t columns, float& horizontal,
                                    float& vertical) noexcept
{
    const std::uint64_t neighbours[4] = {index - 1u, index + 1u, index - columns, index + columns};
    float horizontalDifference[4] = {};
    float verticalDifference[4] = {};
    for (std::uint32_t direction = 0u; direction < 4u; ++direction)
    {
        horizontalDifference[direction] =
            std::fabs(horizontalLightness[index] - horizontalLightness[neighbours[direction]]);
        verticalDifference[direction] = std::fabs(verticalLightness[index] - verticalLightness[neighbours[direction]]);
    }
    const float threshold = (std::min)((std::max)(horizontalDifference[0], horizontalDifference[1]),
                                       (std::max)(verticalDifference[2], verticalDifference[3]));
    horizontal = 0.0f;
    vertical = 0.0f;
    for (std::uint32_t direction = 0u; direction < 4u; ++direction)
    {
        horizontal += horizontalDifference[direction] <= threshold ? 1.0f : 0.0f;
        vertical += verticalDifference[direction] <= threshold ? 1.0f : 0.0f;
    }
}

/// The luminance-only AHD estimate's choice at one pixel: the candidate more of whose three-by-three
/// neighbourhood is homogeneous, or the two candidates' average where they tie.
///
/// @param planes the six candidate colour planes - horizontal red, green and blue, then vertical.
/// @param horizontalHomogeneity the horizontal candidate's counts.
/// @param verticalHomogeneity the vertical candidate's counts.
/// @param index the pixel's index in the planes.
/// @param columns floats between rows of the planes.
/// @param red set to the chosen red.
/// @param green set to the chosen green.
/// @param blue set to the chosen blue.
inline void luminanceAhdChoose(const float* const* planes, const float* horizontalHomogeneity,
                               const float* verticalHomogeneity, std::uint64_t index, std::uint64_t columns, float& red,
                               float& green, float& blue) noexcept
{
    float horizontalAgreement = 0.0f;
    float verticalAgreement = 0.0f;
    for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY)
    {
        for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX)
        {
            const std::int64_t neighbour =
                static_cast<std::int64_t>(index) + offsetY * static_cast<std::int64_t>(columns) + offsetX;
            const std::uint64_t neighbourIndex = static_cast<std::uint64_t>(neighbour);
            horizontalAgreement += horizontalHomogeneity[neighbourIndex];
            verticalAgreement += verticalHomogeneity[neighbourIndex];
        }
    }
    if (horizontalAgreement > verticalAgreement)
    {
        red = planes[0][index];
        green = planes[1][index];
        blue = planes[2][index];
    }
    else if (verticalAgreement > horizontalAgreement)
    {
        red = planes[3][index];
        green = planes[4][index];
        blue = planes[5][index];
    }
    else
    {
        red = 0.5f * (planes[0][index] + planes[3][index]);
        green = 0.5f * (planes[1][index] + planes[4][index]);
        blue = 0.5f * (planes[2][index] + planes[5][index]);
    }
}

} // namespace an::demosaic_bayer_detail
