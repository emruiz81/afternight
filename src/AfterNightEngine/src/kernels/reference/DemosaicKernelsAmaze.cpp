// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DemosaicKernels.h"
#include "kernels/reference/DemosaicKernelVectorOps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Aliasing-minimised and zipper-eliminated demosaicing (AMaZE).
//
// Derived from RawTherapee's AMaZE as librtprocess carries it (`demosaic/amaze.cc`), which states
// `copyright (c) 2008-2010  Emil Martinec <ejmartin@uchicago.edu>` and `optimized for speed by Ingo
// Weyrich`, and is distributed under the GNU General Public License, version 3 or any later version.
// The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
// The arithmetic is the scalar dialect of that implementation, pass for pass: directional weights
// from the local gradients, colour differences interpolated four ways and chosen by their own
// variance, a Nyquist-texture test that switches to area interpolation where the mosaic cannot
// resolve the detail, a diagonal reconstruction of red and blue, and a final chrominance smoothing.
//
// The body takes optional lane-wise passes through `DemosaicAmazeVectorOps`: the reference passes
// none, and a compiled CPU target replaces the normalisation, the gradients, the colour differences,
// both halves of choosing and bounding them and the direction weights with lane-wise spellings of the
// same arithmetic, while every other pass stays this scalar text. The passes that visit every second
// column count their sites and address the half planes by offsets fixed for the row, and those whose
// sites of a row do not read one another's results say so to the compiler, so that it may take several
// at once; that changes no operation and no order of evaluation.
//
// Five things changed from the original.
//
// **The clip thresholds are fractions of the caller's white level.** The original takes an input
// scale and compares against `1.0 / initGain` and `0.8 / initGain` *after* dividing the tile by that
// scale, which is correct only while the scale and the samples agree: a caller that rescales its
// source into sixteen-bit code values and passes an input scale of one leaves a tile of numbers up to
// 65535 against highlight thresholds of 1.0 and 0.8, so every adaptive-ratio branch and every
// highlight branch fires on almost every pixel, and what runs is Hamilton-Adams with medians rather
// than AMaZE. Here the thresholds are derived from the same white level the tile is normalised by, so
// the method runs as described for every representation.
//
// **The edge of the picture is the family's border rule, not this method's mirrored padding.** The
// original reflects the image into a sixteen-pixel margin around every edge tile and then leaves only
// three pixels to the shared border rule. This family gives every method the same treatment: the
// outer band is the shared three-by-three rule and the tiles cover what is left, so a frame does not
// change at its edges when the method changes in the middle. The band is therefore sixteen pixels
// wide rather than the original's three.
//
// **Halving and doubling are arithmetic.** The original halves and doubles by adding to the exponent
// field of the float, which is exact for a normal number and corrupts a denormal, an infinity and a
// not-a-number. Here they are multiplications by a power of two, which agree on every normal value
// and are right on the rest.
//
// **The tile no longer has to start on an even column.** As in the ratio-corrected estimate, the
// half-resolution buffers are addressed by an explicit half-plane index and the executor guarantees
// the padded tile origin is even, rather than that happening to be true of the tiling the original
// generates.
//
// **Every pass reads what was written for this tile.** The scratch is cleared per tile, and the
// green-minus-blue split is widened by two pixels to cover everything the chrominance smoothing
// after it reads; each is explained where it happens.

namespace an
{
namespace
{

/// How far this estimate reads from the pixel it writes.
constexpr std::uint32_t kPad = 16u;

/// The most padded rows a tile may have for the lane-wise passes, which read a per-row table of this size.
constexpr std::size_t kMaxLaneRows = 256u;

/// Tolerance added to a denominator so that a flat neighbourhood does not divide by zero.
constexpr float kEpsilon = 1e-5f;

/// The same tolerance for a squared measure.
constexpr float kSquaredEpsilon = 1e-10f;

/// How far a colour ratio may stray from one before the Hamilton-Adams estimate is used instead.
constexpr float kAdaptiveRatioThreshold = 0.75f;

/// Threshold of the Nyquist texture test.
constexpr float kNyquistThreshold = 0.5f;

/// A five-by-five quincunx Gaussian, for the colour-difference term of the Nyquist test.
constexpr float kGaussOdd[4] = {0.14659727707323927f, 0.103592713382435f, 0.0732036125103057f, 0.0365543548389495f};

/// A five-by-five Gaussian scaled by the Nyquist threshold, for the gradient term of that test.
constexpr float kGaussGrad[6] = {kNyquistThreshold * 0.07384411893421103f, kNyquistThreshold * 0.06207511968171489f,
                                 kNyquistThreshold * 0.0521818194747806f,  kNyquistThreshold * 0.03687419286733595f,
                                 kNyquistThreshold * 0.03099732204057846f, kNyquistThreshold * 0.018413194161458882f};

/// A five-by-five alternate-quincunx Gaussian, for the diagonal colour-difference variance.
constexpr float kGaussEven[2] = {0.13719494435797422f, 0.05640252782101291f};

/// A Gaussian on the quincunx grid, for the refined green curvature.
constexpr float kGaussQuincunx[4] = {0.169917f, 0.108947f, 0.069855f, 0.0287182f};

/// The square of a value.
///
/// @param value the value.
/// @return its square.
[[nodiscard]] inline float squared(float value) noexcept
{
    return value * value;
}

/// The middle of three values.
///
/// @param first one value.
/// @param second another.
/// @param third the third.
/// @return the median.
[[nodiscard]] inline float median3(float first, float second, float third) noexcept
{
    return (std::max)((std::min)(first, second), (std::min)(third, (std::max)(first, second)));
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

} // namespace

void demosaicAmazeBody(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                       const DemosaicAmazeVectorOps* vectorOps) noexcept
{
    const std::uint32_t regionWidth = tile.xEnd - tile.xBegin;
    const std::uint32_t regionHeight = tile.yEnd - tile.yBegin;
    if (regionWidth == 0u || regionHeight == 0u)
    {
        return;
    }
    const std::uint64_t rows = static_cast<std::uint64_t>(regionHeight) + 2u * kPad;
    const std::uint64_t columns = static_cast<std::uint64_t>(regionWidth) + 2u * kPad;
    const std::uint64_t halfColumns = (columns + 1u) / 2u;
    const std::uint64_t plane = rows * columns;
    const std::uint64_t halfPlane = rows * halfColumns;
    const std::uint64_t needed = 12u * plane + 16u * halfPlane;
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
    float* const rgbGreen = cfa + plane;
    float* const gradientSum = rgbGreen + plane;
    float* const verticalWeights = gradientSum + plane;
    float* const horizontalWeights = verticalWeights + plane;
    float* const verticalDifference = horizontalWeights + plane;
    float* const horizontalDifference = verticalDifference + plane;
    float* const verticalAlternate = horizontalDifference + plane;
    float* const horizontalAlternate = verticalAlternate + plane;
    float* const differenceSquared = horizontalAlternate + plane;
    float* const verticalSpread = differenceSquared + plane;
    float* const horizontalSpread = verticalSpread + plane;
    float* const directionWeight = horizontalSpread + plane;
    float* const greenRed = directionWeight + halfPlane;
    float* const greenBlue = greenRed + halfPlane;
    float* const plusGradient = greenBlue + halfPlane;
    float* const minusGradient = plusGradient + halfPlane;
    float* const diagonalSum = minusGradient + halfPlane;
    float* const curvatureH = diagonalSum + halfPlane;
    float* const curvatureV = curvatureH + halfPlane;
    float* const minusSquared = curvatureV + halfPlane;
    float* const plusSquared = minusSquared + halfPlane;
    float* const diagonalWeight = plusSquared + halfPlane;
    float* const minusInterpolation = diagonalWeight + halfPlane;
    float* const plusInterpolation = minusInterpolation + halfPlane;
    float* const nyquistTest = plusInterpolation + halfPlane;
    float* const nyquist = nyquistTest + halfPlane;
    float* const nyquistRefined = nyquist + halfPlane;

    const std::uint32_t padBeginX = tile.xBegin - kPad;
    const std::uint32_t padBeginY = tile.yBegin - kPad;
    const CfaDescriptor& cfaDescriptor = *tile.cfa;
    const float whiteLevel = tile.whiteLevel;
    const float clipPoint = tile.clipPoint / whiteLevel;
    const float highlightPoint = 0.8f * clipPoint;

    const std::uint64_t v1 = columns;
    const std::uint64_t v2 = 2u * columns;
    const std::uint64_t v3 = 3u * columns;
    const std::int64_t p1 = -static_cast<std::int64_t>(columns) + 1;
    const std::int64_t p2 = 2 * p1;
    const std::int64_t p3 = 3 * p1;
    const std::int64_t m1 = static_cast<std::int64_t>(columns) + 1;
    const std::int64_t m2 = 2 * m1;
    const std::int64_t m3 = 3 * m1;

    /// Whether a padded row holds an even number of pixels, as every tile but an odd-width picture's last does.
    const bool evenColumns = columns % 2u == 0u;
    /// Whether the pixel at this padded-tile position is a green site.
    const auto isGreen = [&cfaDescriptor, padBeginX, padBeginY](std::uint64_t row, std::uint64_t column) noexcept {
        return cfaDescriptor.colorAt(padBeginX + static_cast<std::uint32_t>(column),
                                     padBeginY + static_cast<std::uint32_t>(row)) == CfaColor::Green;
    };
    /// The first column at or after `base` that is not a green site.
    const auto firstColourColumn = [&isGreen](std::uint64_t row, std::uint64_t base) noexcept {
        return isGreen(row, base) ? base + 1u : base;
    };
    /// How many sites a row holds from `first`, every second column, before `end`.
    const auto colourSites = [](std::uint64_t first, std::uint64_t end) noexcept -> std::uint64_t {
        return first < end ? (end - first + 1u) / 2u : 0u;
    };
    const auto signedHalfColumns = static_cast<std::int64_t>(halfColumns);
    /// The half-plane offset from a site to the site `rowOffset` rows and `columnOffset` columns away, in a
    /// row whose sites start on a column of parity `parity`.
    ///
    /// A pixel's half-plane index is its row times `halfColumns` plus its column halved, whatever the width
    /// of the row, so that offset is the same for every site of the row. The passes that visit every second
    /// column count their sites and read their neighbours through these offsets, which leaves the compiler
    /// an address it can step rather than a halving it cannot see through.
    const auto halfStep = [signedHalfColumns](std::uint64_t parity, std::int64_t rowOffset,
                                              std::int64_t columnOffset) noexcept -> std::int64_t {
        return rowOffset * signedHalfColumns + ((static_cast<std::int64_t>(parity) + columnOffset) >> 1);
    };

    // The tile, normalised by the stated white level.
    if (vectorOps != nullptr && vectorOps->initialize != nullptr)
    {
        vectorOps->initialize(tile, padBeginX, padBeginY, rows, columns, whiteLevel, cfa, rgbGreen);
    }
    else
    {
        for (std::uint64_t row = 0u; row < rows; ++row)
        {
            const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
            const float* const sourceRow = tile.source + static_cast<std::uint64_t>(y) * tile.sourceStride + padBeginX;
            for (std::uint64_t column = 0u; column < columns; ++column)
            {
                const float measured = sourceRow[column] / whiteLevel;
                cfa[row * columns + column] = measured;
                rgbGreen[row * columns + column] = measured;
            }
        }
    }

    // Horizontal and vertical gradients.
    if (vectorOps != nullptr && vectorOps->gradients != nullptr)
    {
        vectorOps->gradients(cfa, rows, columns, gradientSum, verticalWeights, horizontalWeights);
    }
    else
    {
        for (std::uint64_t row = 2u; row < rows - 2u; ++row)
        {
            for (std::uint64_t column = 2u; column < columns - 2u; ++column)
            {
                const std::uint64_t index = row * columns + column;
                const float horizontalSpan = std::fabs(cfa[index + 1u] - cfa[index - 1u]);
                const float verticalSpan = std::fabs(cfa[index + v1] - cfa[index - v1]);
                verticalWeights[index] = kEpsilon + std::fabs(cfa[index + v2] - cfa[index]) +
                                         std::fabs(cfa[index] - cfa[index - v2]) + verticalSpan;
                horizontalWeights[index] = kEpsilon + std::fabs(cfa[index + 2u] - cfa[index]) +
                                           std::fabs(cfa[index] - cfa[index - 2u]) + horizontalSpan;
                gradientSum[index] = squared(horizontalSpan) + squared(verticalSpan);
            }
        }
    }

    // Which rows start on a green site, for the lane-wise passes, which cannot ask the mosaic lane by lane.
    std::array<std::uint8_t, kMaxLaneRows> rowStartsGreen{};
    const bool laneWise = vectorOps != nullptr && rows <= rowStartsGreen.size();
    if (laneWise)
    {
        for (std::uint64_t row = 4u; row < rows - 4u; ++row)
        {
            rowStartsGreen[row] = isGreen(row, 4u) ? 1u : 0u;
        }
    }

    // Colour differences interpolated along each axis, by adaptive ratio and by Hamilton-Adams.
    if (laneWise && vectorOps->colourDifferences != nullptr)
    {
        vectorOps->colourDifferences(rowStartsGreen.data(), rows, columns, highlightPoint, cfa, verticalWeights,
                                     horizontalWeights, verticalDifference, horizontalDifference, verticalAlternate,
                                     horizontalAlternate, verticalSpread, horizontalSpread);
    }
    else
    {
        for (std::uint64_t row = 4u; row < rows - 4u; ++row)
        {
            bool greenSite = isGreen(row, 4u);
            for (std::uint64_t column = 4u; column < columns - 4u; ++column)
            {
                const std::uint64_t index = row * columns + column;
                const float centre = cfa[index];
                const float upRatio = cfa[index - v1] * (verticalWeights[index - v2] + verticalWeights[index]) /
                                      (verticalWeights[index - v2] * (kEpsilon + centre) +
                                       verticalWeights[index] * (kEpsilon + cfa[index - v2]));
                const float downRatio = cfa[index + v1] * (verticalWeights[index + v2] + verticalWeights[index]) /
                                        (verticalWeights[index + v2] * (kEpsilon + centre) +
                                         verticalWeights[index] * (kEpsilon + cfa[index + v2]));
                const float leftRatio = cfa[index - 1u] * (horizontalWeights[index - 2u] + horizontalWeights[index]) /
                                        (horizontalWeights[index - 2u] * (kEpsilon + centre) +
                                         horizontalWeights[index] * (kEpsilon + cfa[index - 2u]));
                const float rightRatio = cfa[index + 1u] * (horizontalWeights[index + 2u] + horizontalWeights[index]) /
                                         (horizontalWeights[index + 2u] * (kEpsilon + centre) +
                                          horizontalWeights[index] * (kEpsilon + cfa[index + 2u]));

                const float upAdams = cfa[index - v1] + 0.5f * (centre - cfa[index - v2]);
                const float downAdams = cfa[index + v1] + 0.5f * (centre - cfa[index + v2]);
                const float leftAdams = cfa[index - 1u] + 0.5f * (centre - cfa[index - 2u]);
                const float rightAdams = cfa[index + 1u] + 0.5f * (centre - cfa[index + 2u]);

                float upRatioEstimate =
                    std::fabs(1.0f - upRatio) < kAdaptiveRatioThreshold ? centre * upRatio : upAdams;
                float downRatioEstimate =
                    std::fabs(1.0f - downRatio) < kAdaptiveRatioThreshold ? centre * downRatio : downAdams;
                float leftRatioEstimate =
                    std::fabs(1.0f - leftRatio) < kAdaptiveRatioThreshold ? centre * leftRatio : leftAdams;
                float rightRatioEstimate =
                    std::fabs(1.0f - rightRatio) < kAdaptiveRatioThreshold ? centre * rightRatio : rightAdams;

                const float horizontalWeight =
                    horizontalWeights[index - 1u] / (horizontalWeights[index - 1u] + horizontalWeights[index + 1u]);
                const float verticalWeight =
                    verticalWeights[index - v1] / (verticalWeights[index + v1] + verticalWeights[index - v1]);

                const float verticalAdams = verticalWeight * downAdams + (1.0f - verticalWeight) * upAdams;
                const float horizontalAdams = horizontalWeight * rightAdams + (1.0f - horizontalWeight) * leftAdams;

                if (greenSite)
                {
                    verticalDifference[index] =
                        centre - (verticalWeight * downRatioEstimate + (1.0f - verticalWeight) * upRatioEstimate);
                    horizontalDifference[index] = centre - (horizontalWeight * rightRatioEstimate +
                                                            (1.0f - horizontalWeight) * leftRatioEstimate);
                    verticalAlternate[index] = centre - verticalAdams;
                    horizontalAlternate[index] = centre - horizontalAdams;
                }
                else
                {
                    verticalDifference[index] =
                        (verticalWeight * downRatioEstimate + (1.0f - verticalWeight) * upRatioEstimate) - centre;
                    horizontalDifference[index] =
                        (horizontalWeight * rightRatioEstimate + (1.0f - horizontalWeight) * leftRatioEstimate) -
                        centre;
                    verticalAlternate[index] = verticalAdams - centre;
                    horizontalAlternate[index] = horizontalAdams - centre;
                }
                greenSite = !greenSite;

                if (centre > highlightPoint || verticalAdams > highlightPoint || horizontalAdams > highlightPoint)
                {
                    upRatioEstimate = upAdams;
                    downRatioEstimate = downAdams;
                    leftRatioEstimate = leftAdams;
                    rightRatioEstimate = rightAdams;
                    verticalDifference[index] = verticalAlternate[index];
                    horizontalDifference[index] = horizontalAlternate[index];
                }

                verticalSpread[index] =
                    (std::min)(squared(upAdams - downAdams), squared(upRatioEstimate - downRatioEstimate));
                horizontalSpread[index] =
                    (std::min)(squared(leftAdams - rightAdams), squared(leftRatioEstimate - rightRatioEstimate));
            }
        }
    }

    // Choose the smoother of the two interpolations, and bound both where the colour is saturated.
    //
    // The vertical and the horizontal difference are chosen and bounded apart - every row's vertical
    // difference, then every row's horizontal one - and each is the same arithmetic in the same order as
    // when the two were interleaved. A vertical difference reads only the vertical differences two rows
    // away - the rows above already final, the rows below not yet touched - and a horizontal one only the
    // horizontal differences two columns away in its own row, the one to its left already final. Neither
    // reads the other until their squared difference, which waits for both. The horizontal half is a
    // recurrence along each row and the vertical half is not, but no row's horizontal half reads another
    // row, so a lane-wise pass takes the rows of a block together.
    for (std::uint64_t row = 4u; row < rows - 4u; ++row)
    {
        const bool startsGreen = isGreen(row, 4u);
        if (vectorOps != nullptr && vectorOps->boundVertical != nullptr)
        {
            vectorOps->boundVertical(row, columns, startsGreen, clipPoint, cfa, verticalAlternate, verticalDifference);
        }
        else
        {
            bool greenSite = startsGreen;
            for (std::uint64_t column = 4u; column < columns - 4u; ++column)
            {
                const std::uint64_t index = row * columns + column;
                const float verticalVariance =
                    3.0f * (squared(verticalDifference[index - v2]) + squared(verticalDifference[index]) +
                            squared(verticalDifference[index + v2])) -
                    squared(verticalDifference[index - v2] + verticalDifference[index] +
                            verticalDifference[index + v2]);
                const float verticalAlternateVariance =
                    3.0f * (squared(verticalAlternate[index - v2]) + squared(verticalAlternate[index]) +
                            squared(verticalAlternate[index + v2])) -
                    squared(verticalAlternate[index - v2] + verticalAlternate[index] + verticalAlternate[index + v2]);
                if (verticalAlternateVariance < verticalVariance)
                {
                    verticalDifference[index] = verticalAlternate[index];
                }

                const float centre = cfa[index];
                if (greenSite)
                {
                    const float verticalGreen = -verticalDifference[index] + centre;
                    if (verticalDifference[index] > 0.0f)
                    {
                        const float bounded = -median3(verticalGreen, cfa[index - v1], cfa[index + v1]) + centre;
                        if (3.0f * verticalDifference[index] > (verticalGreen + centre))
                        {
                            verticalDifference[index] = bounded;
                        }
                        else
                        {
                            const float weight =
                                1.0f - 3.0f * verticalDifference[index] / (kEpsilon + verticalGreen + centre);
                            verticalDifference[index] = weight * verticalDifference[index] + (1.0f - weight) * bounded;
                        }
                    }
                    if (verticalGreen > clipPoint)
                    {
                        verticalDifference[index] = -median3(verticalGreen, cfa[index - v1], cfa[index + v1]) + centre;
                    }
                }
                else
                {
                    const float verticalGreen = verticalDifference[index] + centre;
                    if (verticalDifference[index] < 0.0f)
                    {
                        const float bounded = median3(verticalGreen, cfa[index - v1], cfa[index + v1]) - centre;
                        if (3.0f * verticalDifference[index] < -(verticalGreen + centre))
                        {
                            verticalDifference[index] = bounded;
                        }
                        else
                        {
                            const float weight =
                                1.0f + 3.0f * verticalDifference[index] / (kEpsilon + verticalGreen + centre);
                            verticalDifference[index] = weight * verticalDifference[index] + (1.0f - weight) * bounded;
                        }
                    }
                    if (verticalGreen > clipPoint)
                    {
                        verticalDifference[index] = median3(verticalGreen, cfa[index - v1], cfa[index + v1]) - centre;
                    }
                }
                greenSite = !greenSite;
            }
        }
    }

    if (laneWise && vectorOps->boundHorizontal != nullptr && columns <= kDemosaicAmazeLaneColumns)
    {
        vectorOps->boundHorizontal(rowStartsGreen.data(), rows, columns, clipPoint, cfa, horizontalAlternate,
                                   verticalDifference, horizontalDifference, differenceSquared);
    }
    else
    {
        for (std::uint64_t row = 4u; row < rows - 4u; ++row)
        {
            // Each horizontal bound is computed and the one its tests choose is kept, which writes what the
            // tests' branches write. The tests follow the data, and as branches they cost this recurrence more
            // than the arithmetic they save; the kind of site alternates, and stays a branch.
            bool greenSite = isGreen(row, 4u);
            for (std::uint64_t column = 4u; column < columns - 4u; ++column)
            {
                const std::uint64_t index = row * columns + column;
                const float horizontalVariance =
                    3.0f * (squared(horizontalDifference[index - 2u]) + squared(horizontalDifference[index]) +
                            squared(horizontalDifference[index + 2u])) -
                    squared(horizontalDifference[index - 2u] + horizontalDifference[index] +
                            horizontalDifference[index + 2u]);
                const float horizontalAlternateVariance =
                    3.0f * (squared(horizontalAlternate[index - 2u]) + squared(horizontalAlternate[index]) +
                            squared(horizontalAlternate[index + 2u])) -
                    squared(horizontalAlternate[index - 2u] + horizontalAlternate[index] +
                            horizontalAlternate[index + 2u]);
                const float chosen = horizontalAlternateVariance < horizontalVariance ? horizontalAlternate[index]
                                                                                      : horizontalDifference[index];

                const float centre = cfa[index];
                if (greenSite)
                {
                    const float horizontalGreen = -chosen + centre;
                    const float bounded = -median3(horizontalGreen, cfa[index - 1u], cfa[index + 1u]) + centre;
                    const float weight = 1.0f - 3.0f * chosen / (kEpsilon + horizontalGreen + centre);
                    const float softened = 3.0f * chosen > (horizontalGreen + centre)
                                               ? bounded
                                               : weight * chosen + (1.0f - weight) * bounded;
                    const float limited = chosen > 0.0f ? softened : chosen;
                    horizontalDifference[index] = horizontalGreen > clipPoint ? bounded : limited;
                }
                else
                {
                    const float horizontalGreen = chosen + centre;
                    const float bounded = median3(horizontalGreen, cfa[index - 1u], cfa[index + 1u]) - centre;
                    const float weight = 1.0f + 3.0f * chosen / (kEpsilon + horizontalGreen + centre);
                    const float softened = 3.0f * chosen < -(horizontalGreen + centre)
                                               ? bounded
                                               : weight * chosen + (1.0f - weight) * bounded;
                    const float limited = chosen < 0.0f ? softened : chosen;
                    const float final = horizontalGreen > clipPoint ? bounded : limited;
                    horizontalDifference[index] = final;
                    differenceSquared[index] = squared(verticalDifference[index] - final);
                }
                greenSite = !greenSite;
            }
        }
    }

    // How much of the vertical estimate to keep, at every red and blue site.
    if (laneWise && vectorOps->directionWeights != nullptr && evenColumns)
    {
        vectorOps->directionWeights(rowStartsGreen.data(), rows, columns, verticalDifference, horizontalDifference,
                                    verticalWeights, horizontalWeights, verticalSpread, horizontalSpread,
                                    directionWeight);
    }
    else
    {
        for (std::uint64_t row = 6u; row < rows - 6u; ++row)
        {
            for (std::uint64_t column = firstColourColumn(row, 6u); column < columns - 6u; column += 2u)
            {
                const std::uint64_t index = row * columns + column;
                const float upAverage = verticalDifference[index] + verticalDifference[index - v1] +
                                        verticalDifference[index - v2] + verticalDifference[index - v3];
                const float downAverage = verticalDifference[index] + verticalDifference[index + v1] +
                                          verticalDifference[index + v2] + verticalDifference[index + v3];
                const float leftAverage = horizontalDifference[index] + horizontalDifference[index - 1u] +
                                          horizontalDifference[index - 2u] + horizontalDifference[index - 3u];
                const float rightAverage = horizontalDifference[index] + horizontalDifference[index + 1u] +
                                           horizontalDifference[index + 2u] + horizontalDifference[index + 3u];

                const float upVariance = squared(verticalDifference[index] - upAverage) +
                                         squared(verticalDifference[index - v1] - upAverage) +
                                         squared(verticalDifference[index - v2] - upAverage) +
                                         squared(verticalDifference[index - v3] - upAverage);
                const float downVariance = squared(verticalDifference[index] - downAverage) +
                                           squared(verticalDifference[index + v1] - downAverage) +
                                           squared(verticalDifference[index + v2] - downAverage) +
                                           squared(verticalDifference[index + v3] - downAverage);
                const float leftVariance = squared(horizontalDifference[index] - leftAverage) +
                                           squared(horizontalDifference[index - 1u] - leftAverage) +
                                           squared(horizontalDifference[index - 2u] - leftAverage) +
                                           squared(horizontalDifference[index - 3u] - leftAverage);
                const float rightVariance = squared(horizontalDifference[index] - rightAverage) +
                                            squared(horizontalDifference[index + 1u] - rightAverage) +
                                            squared(horizontalDifference[index + 2u] - rightAverage) +
                                            squared(horizontalDifference[index + 3u] - rightAverage);

                const float horizontalWeight =
                    horizontalWeights[index - 1u] / (horizontalWeights[index - 1u] + horizontalWeights[index + 1u]);
                const float verticalWeight =
                    verticalWeights[index - v1] / (verticalWeights[index + v1] + verticalWeights[index - v1]);

                const float verticalVariance =
                    kSquaredEpsilon + verticalWeight * downVariance + (1.0f - verticalWeight) * upVariance;
                const float horizontalVariance =
                    kSquaredEpsilon + horizontalWeight * rightVariance + (1.0f - horizontalWeight) * leftVariance;

                const float upSpread = verticalSpread[index] + verticalSpread[index - v1] + verticalSpread[index - v2];
                const float downSpread =
                    verticalSpread[index] + verticalSpread[index + v1] + verticalSpread[index + v2];
                const float leftSpread =
                    horizontalSpread[index] + horizontalSpread[index - 1u] + horizontalSpread[index - 2u];
                const float rightSpread =
                    horizontalSpread[index] + horizontalSpread[index + 1u] + horizontalSpread[index + 2u];

                const float verticalSpreadVariance =
                    kSquaredEpsilon + verticalWeight * downSpread + (1.0f - verticalWeight) * upSpread;
                const float horizontalSpreadVariance =
                    kSquaredEpsilon + horizontalWeight * rightSpread + (1.0f - horizontalWeight) * leftSpread;

                const float varianceWeight = horizontalVariance / (verticalVariance + horizontalVariance);
                const float spreadWeight =
                    horizontalSpreadVariance / (verticalSpreadVariance + horizontalSpreadVariance);

                const bool agree = (0.5f - varianceWeight) * (0.5f - spreadWeight) > 0.0f &&
                                   std::fabs(0.5f - spreadWeight) < std::fabs(0.5f - varianceWeight);
                directionWeight[row * halfColumns + column / 2u] = agree ? varianceWeight : spreadWeight;
            }
        }
    }

    // Where the mosaic cannot resolve the detail it is looking at.
    //
    // A site's test reads only the squared differences and the gradient sums, so the sites of a row can be
    // taken together.
    for (std::uint64_t row = 6u; row < rows - 6u; ++row)
    {
        const std::uint64_t first = firstColourColumn(row, 6u);
        const std::uint64_t sites = colourSites(first, columns - 6u);
        const auto firstIndex = static_cast<std::int64_t>(row * columns + first);
        float* const test = nyquistTest + row * halfColumns + first / 2u;
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            const std::int64_t index = firstIndex + 2 * static_cast<std::int64_t>(site);
            const float colourTerm = kGaussOdd[0] * differenceSquared[index] +
                                     kGaussOdd[1] * (differenceSquared[index - m1] + differenceSquared[index + p1] +
                                                     differenceSquared[index - p1] + differenceSquared[index + m1]) +
                                     kGaussOdd[2] * (differenceSquared[index - static_cast<std::int64_t>(v2)] +
                                                     differenceSquared[index - 2] + differenceSquared[index + 2] +
                                                     differenceSquared[index + static_cast<std::int64_t>(v2)]) +
                                     kGaussOdd[3] * (differenceSquared[index - m2] + differenceSquared[index + p2] +
                                                     differenceSquared[index - p2] + differenceSquared[index + m2]);
            const auto stride = static_cast<std::int64_t>(columns);
            const float gradientTerm =
                kGaussGrad[0] * gradientSum[index] +
                kGaussGrad[1] * (gradientSum[index - stride] + gradientSum[index + 1] + gradientSum[index - 1] +
                                 gradientSum[index + stride]) +
                kGaussGrad[2] * (gradientSum[index - m1] + gradientSum[index + p1] + gradientSum[index - p1] +
                                 gradientSum[index + m1]) +
                kGaussGrad[3] * (gradientSum[index - 2 * stride] + gradientSum[index - 2] + gradientSum[index + 2] +
                                 gradientSum[index + 2 * stride]) +
                kGaussGrad[4] * (gradientSum[index - 2 * stride - 1] + gradientSum[index - 2 * stride + 1] +
                                 gradientSum[index - stride - 2] + gradientSum[index - stride + 2] +
                                 gradientSum[index + stride - 2] + gradientSum[index + stride + 2] +
                                 gradientSum[index + 2 * stride - 1] + gradientSum[index + 2 * stride + 1]) +
                kGaussGrad[5] * (gradientSum[index - m2] + gradientSum[index + p2] + gradientSum[index - p2] +
                                 gradientSum[index + m2]);
            test[site] = colourTerm - gradientTerm;
        }
    }

    std::uint64_t nyquistFirstRow = 0u;
    std::uint64_t nyquistLastRow = 0u;
    std::uint64_t nyquistFirstColumn = columns + 1u;
    std::uint64_t nyquistLastColumn = 0u;
    bool sawNyquist = false;
    // Each row marks its sites and finds its first and last marked column, and then extends the box the
    // marks span; that is the box the sites extend one at a time.
    for (std::uint64_t row = 6u; row < rows - 6u; ++row)
    {
        const std::uint64_t first = firstColourColumn(row, 6u);
        const std::uint64_t sites = colourSites(first, columns - 6u);
        const float* const test = nyquistTest + row * halfColumns + first / 2u;
        float* const marks = nyquist + row * halfColumns + first / 2u;
        std::uint64_t rowFirstColumn = columns + 1u;
        std::uint64_t rowLastColumn = 0u;
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            const bool marked = test[site] > 0.0f;
            const std::uint64_t column = first + 2u * site;
            marks[site] = marked ? 1.0f : marks[site];
            rowFirstColumn = marked ? (std::min)(rowFirstColumn, column) : rowFirstColumn;
            rowLastColumn = marked ? (std::max)(rowLastColumn, column) : rowLastColumn;
        }
        if (rowLastColumn != 0u)
        {
            nyquistFirstRow = sawNyquist ? nyquistFirstRow : row;
            sawNyquist = true;
            nyquistLastRow = row;
            nyquistFirstColumn = (std::min)(nyquistFirstColumn, rowFirstColumn);
            nyquistLastColumn = (std::max)(nyquistLastColumn, rowLastColumn);
        }
    }

    const bool refineNyquist = nyquistFirstRow != nyquistLastRow && nyquistFirstColumn != nyquistLastColumn;
    if (refineNyquist)
    {
        nyquistLastRow += 1u;
        nyquistLastColumn += 1u;
        nyquistFirstColumn -= (nyquistFirstColumn & 1u);
        nyquistFirstRow = (std::max)(std::uint64_t{8}, nyquistFirstRow);
        nyquistLastRow = (std::min)(rows - 8u, nyquistLastRow);
        nyquistFirstColumn = (std::max)(std::uint64_t{8}, nyquistFirstColumn);
        nyquistLastColumn = (std::min)(columns - 8u, nyquistLastColumn);

        // A refined site reads the unrefined marks only.
        for (std::uint64_t row = nyquistFirstRow; row < nyquistLastRow; ++row)
        {
            const std::uint64_t first = firstColourColumn(row, nyquistFirstColumn);
            const std::uint64_t parity = first & 1u;
            const std::uint64_t sites = colourSites(first, nyquistLastColumn);
            const float* const marks = nyquist + row * halfColumns + first / 2u;
            float* const refined = nyquistRefined + row * halfColumns + first / 2u;
            const std::int64_t twoUp = halfStep(parity, -2, 0);
            const std::int64_t upLeft = halfStep(parity, -1, -1);
            const std::int64_t upRight = halfStep(parity, -1, 1);
            const std::int64_t twoLeft = halfStep(parity, 0, -2);
            const std::int64_t twoRight = halfStep(parity, 0, 2);
            const std::int64_t downLeft = halfStep(parity, 1, -1);
            const std::int64_t downRight = halfStep(parity, 1, 1);
            const std::int64_t twoDown = halfStep(parity, 2, 0);
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
            for (std::uint64_t site = 0u; site < sites; ++site)
            {
                const auto at = static_cast<std::int64_t>(site);
                const float neighbours = marks[at + twoUp] + marks[at + upLeft] + marks[at + upRight] +
                                         marks[at + twoLeft] + marks[at + twoRight] + marks[at + downLeft] +
                                         marks[at + downRight] + marks[at + twoDown];
                refined[site] = neighbours > 4.0f ? 1.0f : (neighbours < 4.0f ? 0.0f : marks[at]);
            }
        }

        for (std::uint64_t row = nyquistFirstRow; row < nyquistLastRow; ++row)
        {
            for (std::uint64_t column = firstColourColumn(row, nyquistFirstColumn); column < nyquistLastColumn;
                 column += 2u)
            {
                const auto index = static_cast<std::int64_t>(row * columns + column);
                const auto centreHalf = static_cast<std::int64_t>(row * halfColumns + column / 2u);
                if (nyquistRefined[centreHalf] == 0.0f)
                {
                    continue;
                }
                float sumCfa = 0.0f;
                float sumHorizontal = 0.0f;
                float sumVertical = 0.0f;
                float sumSquaredHorizontal = 0.0f;
                float sumSquaredVertical = 0.0f;
                float area = 0.0f;
                for (std::int64_t dy = -6; dy < 7; dy += 2)
                {
                    std::int64_t neighbour = index + dy * static_cast<std::int64_t>(columns) - 6;
                    std::int64_t neighbourHalf = centreHalf + dy * signedHalfColumns - 3;
                    for (std::int64_t dx = -6; dx < 7; dx += 2, neighbour += 2, ++neighbourHalf)
                    {
                        if (nyquistRefined[neighbourHalf] == 0.0f)
                        {
                            continue;
                        }
                        const float centre = cfa[neighbour];
                        sumCfa += centre;
                        sumHorizontal += cfa[neighbour - 1] + cfa[neighbour + 1];
                        sumVertical += cfa[neighbour - static_cast<std::int64_t>(v1)] +
                                       cfa[neighbour + static_cast<std::int64_t>(v1)];
                        sumSquaredHorizontal +=
                            squared(centre - cfa[neighbour - 1]) + squared(centre - cfa[neighbour + 1]);
                        sumSquaredVertical += squared(centre - cfa[neighbour - static_cast<std::int64_t>(v1)]) +
                                              squared(centre - cfa[neighbour + static_cast<std::int64_t>(v1)]);
                        area += 1.0f;
                    }
                }
                sumHorizontal = sumCfa - 0.5f * sumHorizontal;
                sumVertical = sumCfa - 0.5f * sumVertical;
                area = 0.5f * area;
                const float horizontalVariance =
                    kSquaredEpsilon + std::fabs(area * sumSquaredHorizontal - sumHorizontal * sumHorizontal);
                const float verticalVariance =
                    kSquaredEpsilon + std::fabs(area * sumSquaredVertical - sumVertical * sumVertical);
                directionWeight[centreHalf] = horizontalVariance / (verticalVariance + horizontalVariance);
            }
        }
    }

    // Green where red and blue were recorded.
    //
    // A site's weight is adjusted from the sites above and below it only, and its green and curvatures read
    // the green sites beside it, which this pass never writes, so the sites of a row can be taken together.
    for (std::uint64_t row = 8u; row < rows - 8u; ++row)
    {
        const std::uint64_t first = firstColourColumn(row, 8u);
        const std::uint64_t parity = first & 1u;
        const std::uint64_t sites = colourSites(first, columns - 8u);
        const auto firstIndex = static_cast<std::int64_t>(row * columns + first);
        const auto firstHalf = static_cast<std::int64_t>(row * halfColumns + first / 2u);
        const std::int64_t upLeft = halfStep(parity, -1, -1);
        const std::int64_t upRight = halfStep(parity, -1, 1);
        const std::int64_t downLeft = halfStep(parity, 1, -1);
        const std::int64_t downRight = halfStep(parity, 1, 1);
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            const std::int64_t index = firstIndex + 2 * static_cast<std::int64_t>(site);
            const std::int64_t centreHalf = firstHalf + static_cast<std::int64_t>(site);
            const float neighbourWeight =
                0.25f * (directionWeight[centreHalf + upLeft] + directionWeight[centreHalf + upRight] +
                         directionWeight[centreHalf + downLeft] + directionWeight[centreHalf + downRight]);
            const float ownWeight = directionWeight[centreHalf];
            const float weight =
                std::fabs(0.5f - ownWeight) < std::fabs(0.5f - neighbourWeight) ? neighbourWeight : ownWeight;
            directionWeight[centreHalf] = weight;
            const float difference = mix(weight, verticalDifference[index], horizontalDifference[index]);
            greenRed[centreHalf] = difference;
            const float green = cfa[index] + difference;
            rgbGreen[index] = green;

            const bool textured = nyquistRefined[centreHalf] != 0.0f;
            curvatureH[centreHalf] =
                textured ? squared(green - 0.5f * (rgbGreen[index - 1] + rgbGreen[index + 1])) : 0.0f;
            curvatureV[centreHalf] = textured
                                         ? squared(green - 0.5f * (rgbGreen[index - static_cast<std::int64_t>(v1)] +
                                                                   rgbGreen[index + static_cast<std::int64_t>(v1)]))
                                         : 0.0f;
        }
    }

    if (refineNyquist)
    {
        for (std::uint64_t row = nyquistFirstRow; row < nyquistLastRow; ++row)
        {
            const std::uint64_t first = firstColourColumn(row, nyquistFirstColumn);
            const std::uint64_t parity = first & 1u;
            const std::int64_t upLeft = halfStep(parity, -1, -1);
            const std::int64_t upRight = halfStep(parity, -1, 1);
            const std::int64_t downLeft = halfStep(parity, 1, -1);
            const std::int64_t downRight = halfStep(parity, 1, 1);
            const std::int64_t twoUp = halfStep(parity, -2, 0);
            const std::int64_t twoLeft = halfStep(parity, 0, -2);
            const std::int64_t twoRight = halfStep(parity, 0, 2);
            const std::int64_t twoDown = halfStep(parity, 2, 0);
            const std::int64_t twoUpLeft = halfStep(parity, -2, -2);
            const std::int64_t twoUpRight = halfStep(parity, -2, 2);
            const std::int64_t twoDownLeft = halfStep(parity, 2, -2);
            const std::int64_t twoDownRight = halfStep(parity, 2, 2);
            for (std::uint64_t column = first; column < nyquistLastColumn; column += 2u)
            {
                const auto index = static_cast<std::int64_t>(row * columns + column);
                const auto centreHalf = static_cast<std::int64_t>(row * halfColumns + column / 2u);
                if (nyquistRefined[centreHalf] == 0.0f)
                {
                    continue;
                }
                const float horizontalCurvature =
                    kSquaredEpsilon +
                    (kGaussQuincunx[0] * curvatureH[centreHalf] +
                     kGaussQuincunx[1] * (curvatureH[centreHalf + upLeft] + curvatureH[centreHalf + upRight] +
                                          curvatureH[centreHalf + downLeft] + curvatureH[centreHalf + downRight]) +
                     kGaussQuincunx[2] * (curvatureH[centreHalf + twoUp] + curvatureH[centreHalf + twoLeft] +
                                          curvatureH[centreHalf + twoRight] + curvatureH[centreHalf + twoDown]) +
                     kGaussQuincunx[3] *
                         (curvatureH[centreHalf + twoUpLeft] + curvatureH[centreHalf + twoUpRight] +
                          curvatureH[centreHalf + twoDownLeft] + curvatureH[centreHalf + twoDownRight]));
                const float verticalCurvature =
                    kSquaredEpsilon +
                    (kGaussQuincunx[0] * curvatureV[centreHalf] +
                     kGaussQuincunx[1] * (curvatureV[centreHalf + upLeft] + curvatureV[centreHalf + upRight] +
                                          curvatureV[centreHalf + downLeft] + curvatureV[centreHalf + downRight]) +
                     kGaussQuincunx[2] * (curvatureV[centreHalf + twoUp] + curvatureV[centreHalf + twoLeft] +
                                          curvatureV[centreHalf + twoRight] + curvatureV[centreHalf + twoDown]) +
                     kGaussQuincunx[3] *
                         (curvatureV[centreHalf + twoUpLeft] + curvatureV[centreHalf + twoUpRight] +
                          curvatureV[centreHalf + twoDownLeft] + curvatureV[centreHalf + twoDownRight]));
                greenRed[centreHalf] = (horizontalDifference[index] * verticalCurvature +
                                        verticalDifference[index] * horizontalCurvature) /
                                       (verticalCurvature + horizontalCurvature);
                rgbGreen[index] = cfa[index] + greenRed[centreHalf];
            }
        }
    }

    // Diagonal gradients and squared diagonal colour differences.
    for (std::uint64_t row = 6u; row < rows - 6u; ++row)
    {
        const bool colourAtEven = !isGreen(row, 6u);
        const std::int64_t colourOffset = colourAtEven ? 0 : 1;
        const std::int64_t greenOffset = colourAtEven ? 1 : 0;
        const std::uint64_t sites = colourSites(6u, columns - 6u);
        const auto firstIndex = static_cast<std::int64_t>(row * columns + 6u);
        const std::uint64_t firstHalf = row * halfColumns + 3u;
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            const std::int64_t index = firstIndex + 2 * static_cast<std::int64_t>(site);
            const std::uint64_t centreHalf = firstHalf + site;
            const std::int64_t colourSite = index + colourOffset;
            const std::int64_t greenSite = index + greenOffset;
            plusGradient[centreHalf] = std::fabs(cfa[colourSite + p1] - cfa[colourSite - p1]);
            minusGradient[centreHalf] = std::fabs(cfa[colourSite + m1] - cfa[colourSite - m1]);
            plusSquared[centreHalf] =
                squared(cfa[greenSite] - cfa[greenSite - p1]) + squared(cfa[greenSite] - cfa[greenSite + p1]);
            minusSquared[centreHalf] =
                squared(cfa[greenSite] - cfa[greenSite - m1]) + squared(cfa[greenSite] - cfa[greenSite + m1]);
        }
    }

    // Red at blue sites and blue at red sites, from the two diagonals.
    //
    // A site's two interpolations are bounded where they are formed, and only this site reads them in this
    // pass, so the sites of a row can be taken together.
    for (std::uint64_t row = 8u; row < rows - 8u; ++row)
    {
        const std::uint64_t first = firstColourColumn(row, 8u);
        const std::uint64_t parity = first & 1u;
        const std::uint64_t sites = colourSites(first, columns - 8u);
        const auto firstIndex = static_cast<std::int64_t>(row * columns + first);
        const auto firstHalf = static_cast<std::int64_t>(row * halfColumns + first / 2u);
        const std::int64_t downRight = halfStep(parity, 1, 1);
        const std::int64_t twoDownRight = halfStep(parity, 2, 2);
        const std::int64_t upLeft = halfStep(parity, -1, -1);
        const std::int64_t twoUpLeft = halfStep(parity, -2, -2);
        const std::int64_t upRight = halfStep(parity, -1, 1);
        const std::int64_t twoUpRight = halfStep(parity, -2, 2);
        const std::int64_t downLeft = halfStep(parity, 1, -1);
        const std::int64_t twoDownLeft = halfStep(parity, 2, -2);
        const std::int64_t up = halfStep(parity, -1, 0);
        const std::int64_t left = halfStep(parity, 0, -1);
        const std::int64_t right = halfStep(parity, 0, 1);
        const std::int64_t down = halfStep(parity, 1, 0);
        const std::int64_t twoUpOneLeft = halfStep(parity, -2, -1);
        const std::int64_t twoUpOneRight = halfStep(parity, -2, 1);
        const std::int64_t upTwoLeft = halfStep(parity, -1, -2);
        const std::int64_t upTwoRight = halfStep(parity, -1, 2);
        const std::int64_t downTwoLeft = halfStep(parity, 1, -2);
        const std::int64_t downTwoRight = halfStep(parity, 1, 2);
        const std::int64_t twoDownOneLeft = halfStep(parity, 2, -1);
        const std::int64_t twoDownOneRight = halfStep(parity, 2, 1);
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            const std::int64_t index = firstIndex + 2 * static_cast<std::int64_t>(site);
            const std::int64_t centreHalf = firstHalf + static_cast<std::int64_t>(site);
            const float centre = cfa[index];
            const float southEastRatio = 2.0f * cfa[index + m1] / (kEpsilon + centre + cfa[index + m2]);
            const float northWestRatio = 2.0f * cfa[index - m1] / (kEpsilon + centre + cfa[index - m2]);
            const float northEastRatio = 2.0f * cfa[index + p1] / (kEpsilon + centre + cfa[index + p2]);
            const float southWestRatio = 2.0f * cfa[index - p1] / (kEpsilon + centre + cfa[index - p2]);

            const float southEast = std::fabs(1.0f - southEastRatio) < kAdaptiveRatioThreshold
                                        ? centre * southEastRatio
                                        : cfa[index + m1] + 0.5f * (centre - cfa[index + m2]);
            const float northWest = std::fabs(1.0f - northWestRatio) < kAdaptiveRatioThreshold
                                        ? centre * northWestRatio
                                        : cfa[index - m1] + 0.5f * (centre - cfa[index - m2]);
            const float northEast = std::fabs(1.0f - northEastRatio) < kAdaptiveRatioThreshold
                                        ? centre * northEastRatio
                                        : cfa[index + p1] + 0.5f * (centre - cfa[index + p2]);
            const float southWest = std::fabs(1.0f - southWestRatio) < kAdaptiveRatioThreshold
                                        ? centre * southWestRatio
                                        : cfa[index - p1] + 0.5f * (centre - cfa[index - p2]);

            const float southEastWeight = kEpsilon + minusGradient[centreHalf] + minusGradient[centreHalf + downRight] +
                                          minusGradient[centreHalf + twoDownRight];
            const float northWestWeight = kEpsilon + minusGradient[centreHalf] + minusGradient[centreHalf + upLeft] +
                                          minusGradient[centreHalf + twoUpLeft];
            const float northEastWeight = kEpsilon + plusGradient[centreHalf] + plusGradient[centreHalf + upRight] +
                                          plusGradient[centreHalf + twoUpRight];
            const float southWestWeight = kEpsilon + plusGradient[centreHalf] + plusGradient[centreHalf + downLeft] +
                                          plusGradient[centreHalf + twoDownLeft];

            float minus =
                (southEastWeight * northWest + northWestWeight * southEast) / (southEastWeight + northWestWeight);
            float plus =
                (northEastWeight * southWest + southWestWeight * northEast) / (northEastWeight + southWestWeight);

            const float minusVariance =
                kSquaredEpsilon +
                (kGaussEven[0] * (minusSquared[centreHalf + up] + minusSquared[centreHalf + left] +
                                  minusSquared[centreHalf + right] + minusSquared[centreHalf + down]) +
                 kGaussEven[1] *
                     (minusSquared[centreHalf + twoUpOneLeft] + minusSquared[centreHalf + twoUpOneRight] +
                      minusSquared[centreHalf + upTwoLeft] + minusSquared[centreHalf + upTwoRight] +
                      minusSquared[centreHalf + downTwoLeft] + minusSquared[centreHalf + downTwoRight] +
                      minusSquared[centreHalf + twoDownOneLeft] + minusSquared[centreHalf + twoDownOneRight]));
            const float plusVariance =
                kSquaredEpsilon +
                (kGaussEven[0] * (plusSquared[centreHalf + up] + plusSquared[centreHalf + left] +
                                  plusSquared[centreHalf + right] + plusSquared[centreHalf + down]) +
                 kGaussEven[1] *
                     (plusSquared[centreHalf + twoUpOneLeft] + plusSquared[centreHalf + twoUpOneRight] +
                      plusSquared[centreHalf + upTwoLeft] + plusSquared[centreHalf + upTwoRight] +
                      plusSquared[centreHalf + downTwoLeft] + plusSquared[centreHalf + downTwoRight] +
                      plusSquared[centreHalf + twoDownOneLeft] + plusSquared[centreHalf + twoDownOneRight]));
            diagonalWeight[centreHalf] = minusVariance / (plusVariance + minusVariance);

            if (plus < centre)
            {
                const float bounded = median3(plus, cfa[index - p1], cfa[index + p1]);
                if (2.0f * plus < centre)
                {
                    plus = bounded;
                }
                else
                {
                    const float weight = 2.0f * (centre - plus) / (kEpsilon + plus + centre);
                    plus = weight * plus + (1.0f - weight) * bounded;
                }
            }
            if (minus < centre)
            {
                const float bounded = median3(minus, cfa[index - m1], cfa[index + m1]);
                if (2.0f * minus < centre)
                {
                    minus = bounded;
                }
                else
                {
                    const float weight = 2.0f * (centre - minus) / (kEpsilon + minus + centre);
                    minus = weight * minus + (1.0f - weight) * bounded;
                }
            }
            if (plus > clipPoint)
            {
                plus = median3(plus, cfa[index - p1], cfa[index + p1]);
            }
            if (minus > clipPoint)
            {
                minus = median3(minus, cfa[index - m1], cfa[index + m1]);
            }
            minusInterpolation[centreHalf] = minus;
            plusInterpolation[centreHalf] = plus;
        }
    }

    // The interpolated sum of red and blue at every red and blue site.
    //
    // A site's weight is adjusted from the sites above and below it only.
    for (std::uint64_t row = 10u; row < rows - 10u; ++row)
    {
        const std::uint64_t first = firstColourColumn(row, 10u);
        const std::uint64_t parity = first & 1u;
        const std::uint64_t sites = colourSites(first, columns - 10u);
        const auto firstIndex = static_cast<std::int64_t>(row * columns + first);
        const auto firstHalf = static_cast<std::int64_t>(row * halfColumns + first / 2u);
        const std::int64_t upLeft = halfStep(parity, -1, -1);
        const std::int64_t upRight = halfStep(parity, -1, 1);
        const std::int64_t downLeft = halfStep(parity, 1, -1);
        const std::int64_t downRight = halfStep(parity, 1, 1);
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            const std::int64_t index = firstIndex + 2 * static_cast<std::int64_t>(site);
            const std::int64_t centreHalf = firstHalf + static_cast<std::int64_t>(site);
            const float neighbourWeight =
                0.25f * (diagonalWeight[centreHalf + upLeft] + diagonalWeight[centreHalf + upRight] +
                         diagonalWeight[centreHalf + downLeft] + diagonalWeight[centreHalf + downRight]);
            const float ownWeight = diagonalWeight[centreHalf];
            const float weight =
                std::fabs(0.5f - ownWeight) < std::fabs(0.5f - neighbourWeight) ? neighbourWeight : ownWeight;
            diagonalWeight[centreHalf] = weight;
            diagonalSum[centreHalf] = 0.5f * (cfa[index] + minusInterpolation[centreHalf] * (1.0f - weight) +
                                              plusInterpolation[centreHalf] * weight);
        }
    }

    // Where the diagonal discrimination is stronger than the cardinal one, refine green from it.
    //
    // Every site computes the refinement and keeps its own green where the cardinal discrimination is the
    // stronger, which writes what skipping it writes.
    for (std::uint64_t row = 12u; row < rows - 12u; ++row)
    {
        const std::uint64_t first = firstColourColumn(row, 12u);
        const std::uint64_t parity = first & 1u;
        const std::uint64_t sites = colourSites(first, columns - 12u);
        const auto firstIndex = static_cast<std::int64_t>(row * columns + first);
        const auto firstHalf = static_cast<std::int64_t>(row * halfColumns + first / 2u);
        const std::int64_t twoUp = halfStep(parity, -2, 0);
        const std::int64_t twoDown = halfStep(parity, 2, 0);
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            const std::int64_t index = firstIndex + 2 * static_cast<std::int64_t>(site);
            const std::int64_t centreHalf = firstHalf + static_cast<std::int64_t>(site);
            const bool cardinal =
                std::fabs(0.5f - diagonalWeight[centreHalf]) < std::fabs(0.5f - directionWeight[centreHalf]);
            const std::int64_t aboveHalf = centreHalf + twoUp;
            const std::int64_t belowHalf = centreHalf + twoDown;
            const float sum = diagonalSum[centreHalf];
            const float upRatio =
                cfa[index - static_cast<std::int64_t>(v1)] * 2.0f / (kEpsilon + sum + diagonalSum[aboveHalf]);
            const float downRatio =
                cfa[index + static_cast<std::int64_t>(v1)] * 2.0f / (kEpsilon + sum + diagonalSum[belowHalf]);
            const float leftRatio = cfa[index - 1] * 2.0f / (kEpsilon + sum + diagonalSum[centreHalf - 1]);
            const float rightRatio = cfa[index + 1] * 2.0f / (kEpsilon + sum + diagonalSum[centreHalf + 1]);

            const float up = std::fabs(1.0f - upRatio) < kAdaptiveRatioThreshold
                                 ? sum * upRatio
                                 : cfa[index - static_cast<std::int64_t>(v1)] + 0.5f * (sum - diagonalSum[aboveHalf]);
            const float down = std::fabs(1.0f - downRatio) < kAdaptiveRatioThreshold
                                   ? sum * downRatio
                                   : cfa[index + static_cast<std::int64_t>(v1)] + 0.5f * (sum - diagonalSum[belowHalf]);
            const float left = std::fabs(1.0f - leftRatio) < kAdaptiveRatioThreshold
                                   ? sum * leftRatio
                                   : cfa[index - 1] + 0.5f * (sum - diagonalSum[centreHalf - 1]);
            const float right = std::fabs(1.0f - rightRatio) < kAdaptiveRatioThreshold
                                    ? sum * rightRatio
                                    : cfa[index + 1] + 0.5f * (sum - diagonalSum[centreHalf + 1]);

            float vertical = (verticalWeights[index - static_cast<std::int64_t>(v1)] * down +
                              verticalWeights[index + static_cast<std::int64_t>(v1)] * up) /
                             (verticalWeights[index + static_cast<std::int64_t>(v1)] +
                              verticalWeights[index - static_cast<std::int64_t>(v1)]);
            float horizontal = (horizontalWeights[index - 1] * right + horizontalWeights[index + 1] * left) /
                               (horizontalWeights[index - 1] + horizontalWeights[index + 1]);

            if (vertical < sum)
            {
                const float bounded = median3(vertical, cfa[index - static_cast<std::int64_t>(v1)],
                                              cfa[index + static_cast<std::int64_t>(v1)]);
                if (2.0f * vertical < sum)
                {
                    vertical = bounded;
                }
                else
                {
                    const float weight = 2.0f * (sum - vertical) / (kEpsilon + vertical + sum);
                    vertical = weight * vertical + (1.0f - weight) * bounded;
                }
            }
            if (horizontal < sum)
            {
                const float bounded = median3(horizontal, cfa[index - 1], cfa[index + 1]);
                if (2.0f * horizontal < sum)
                {
                    horizontal = bounded;
                }
                else
                {
                    const float weight = 2.0f * (sum - horizontal) / (kEpsilon + horizontal + sum);
                    horizontal = weight * horizontal + (1.0f - weight) * bounded;
                }
            }
            if (horizontal > clipPoint)
            {
                horizontal = median3(horizontal, cfa[index - 1], cfa[index + 1]);
            }
            if (vertical > clipPoint)
            {
                vertical = median3(vertical, cfa[index - static_cast<std::int64_t>(v1)],
                                   cfa[index + static_cast<std::int64_t>(v1)]);
            }

            const float green =
                horizontal * (1.0f - directionWeight[centreHalf]) + vertical * directionWeight[centreHalf];
            rgbGreen[index] = cardinal ? rgbGreen[index] : green;
            greenRed[centreHalf] = cardinal ? greenRed[centreHalf] : green - cfa[index];
        }
    }

    // Split the green-minus-blue difference out of the green-minus-red one, at the blue sites.
    //
    // Two pixels wider than the original on every side, and that is a defect fix: the smoothing pass
    // below reads three rows and columns out from the pixels it writes, so the original reads a
    // green-minus-blue difference that was never split in the outermost two rings of every tile. In
    // its own arrangement those cells hold a stale interpolation from an aliased buffer, and the
    // value propagates inwards because the smoothing writes in place.
    for (std::uint64_t row = 10u; row < rows - 10u; ++row)
    {
        const std::uint64_t first = firstColourColumn(row, 10u);
        if (cfaDescriptor.colorAt(padBeginX + static_cast<std::uint32_t>(first),
                                  padBeginY + static_cast<std::uint32_t>(row)) != CfaColor::Blue)
        {
            continue;
        }
        const std::uint64_t sites = colourSites(first, columns - 10u);
        const std::uint64_t firstHalf = row * halfColumns + first / 2u;
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            greenBlue[firstHalf + site] = greenRed[firstHalf + site];
            greenRed[firstHalf + site] = 0.0f;
        }
    }

    // Smooth each chrominance difference along the diagonal it varies least in.
    //
    // The colour sites of a Bayer row are all red or all blue, so a row smooths one difference, and a site
    // reads that difference only in the rows one and three above and below it, so the sites of a row can
    // be taken together even though the smoothing writes in place.
    for (std::uint64_t row = 14u; row < rows - 14u; ++row)
    {
        const std::uint64_t first = firstColourColumn(row, 14u);
        const std::uint64_t parity = first & 1u;
        const std::uint64_t sites = colourSites(first, columns - 14u);
        const auto firstHalf = static_cast<std::int64_t>(row * halfColumns + first / 2u);
        float* const difference = cfaDescriptor.colorAt(padBeginX + static_cast<std::uint32_t>(first),
                                                        padBeginY + static_cast<std::uint32_t>(row)) == CfaColor::Red
                                      ? greenBlue
                                      : greenRed;
        const std::int64_t upLeft = halfStep(parity, -1, -1);
        const std::int64_t downRight = halfStep(parity, 1, 1);
        const std::int64_t threeUpLeft = halfStep(parity, -3, -3);
        const std::int64_t upRight = halfStep(parity, -1, 1);
        const std::int64_t downLeft = halfStep(parity, 1, -1);
        const std::int64_t threeUpRight = halfStep(parity, -3, 3);
        const std::int64_t threeDownRight = halfStep(parity, 3, 3);
        const std::int64_t threeDownLeft = halfStep(parity, 3, -3);
        const std::int64_t upThreeLeft = halfStep(parity, -1, -3);
        const std::int64_t threeUpOneLeft = halfStep(parity, -3, -1);
        const std::int64_t upThreeRight = halfStep(parity, -1, 3);
        const std::int64_t downOneRight = halfStep(parity, 1, 1);
        const std::int64_t downThreeLeft = halfStep(parity, 1, -3);
        const std::int64_t upOneLeft = halfStep(parity, -1, -1);
        const std::int64_t downThreeRight = halfStep(parity, 1, 3);
        const std::int64_t threeDownOneRight = halfStep(parity, 3, 1);
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t site = 0u; site < sites; ++site)
        {
            const std::int64_t centreHalf = firstHalf + static_cast<std::int64_t>(site);
            const float northWestWeight =
                1.0f / (kEpsilon + std::fabs(difference[centreHalf + upLeft] - difference[centreHalf + downRight]) +
                        std::fabs(difference[centreHalf + upLeft] - difference[centreHalf + threeUpLeft]) +
                        std::fabs(difference[centreHalf + downRight] - difference[centreHalf + threeUpLeft]));
            const float northEastWeight =
                1.0f / (kEpsilon + std::fabs(difference[centreHalf + upRight] - difference[centreHalf + downLeft]) +
                        std::fabs(difference[centreHalf + upRight] - difference[centreHalf + threeUpRight]) +
                        std::fabs(difference[centreHalf + downLeft] - difference[centreHalf + threeUpRight]));
            const float southWestWeight =
                1.0f / (kEpsilon + std::fabs(difference[centreHalf + downLeft] - difference[centreHalf + upRight]) +
                        std::fabs(difference[centreHalf + downLeft] - difference[centreHalf + threeDownRight]) +
                        std::fabs(difference[centreHalf + upRight] - difference[centreHalf + threeDownLeft]));
            const float southEastWeight =
                1.0f / (kEpsilon + std::fabs(difference[centreHalf + downRight] - difference[centreHalf + upLeft]) +
                        std::fabs(difference[centreHalf + downRight] - difference[centreHalf + threeDownLeft]) +
                        std::fabs(difference[centreHalf + upLeft] - difference[centreHalf + threeDownRight]));
            difference[centreHalf] =
                (northWestWeight *
                     (1.325f * difference[centreHalf + upLeft] - 0.175f * difference[centreHalf + threeUpLeft] -
                      0.075f * difference[centreHalf + upThreeLeft] -
                      0.075f * difference[centreHalf + threeUpOneLeft]) +
                 northEastWeight *
                     (1.325f * difference[centreHalf + upRight] - 0.175f * difference[centreHalf + threeUpRight] -
                      0.075f * difference[centreHalf + upThreeRight] - 0.075f * difference[centreHalf + downOneRight]) +
                 southWestWeight *
                     (1.325f * difference[centreHalf + downLeft] - 0.175f * difference[centreHalf + threeDownLeft] -
                      0.075f * difference[centreHalf + downThreeLeft] - 0.075f * difference[centreHalf + upOneLeft]) +
                 southEastWeight *
                     (1.325f * difference[centreHalf + downRight] - 0.175f * difference[centreHalf + threeDownRight] -
                      0.075f * difference[centreHalf + downThreeRight] -
                      0.075f * difference[centreHalf + threeDownOneRight])) /
                (northWestWeight + northEastWeight + southWestWeight + southEastWeight);
        }
    }

    // Red, green and blue everywhere the tile was asked for.
    //
    // A row alternates green and colour sites, so it is written a pair of columns at a time - one site of
    // each kind - and a region of odd width ends on one site, written alone. Each site's arithmetic is the
    // arithmetic of its kind in the original order.
    for (std::uint64_t row = kPad; row < rows - kPad; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride + padBeginX;
        const bool greenFirst = isGreen(row, kPad);
        const std::uint64_t greenColumn = kPad + (greenFirst ? 0u : 1u);
        const std::uint64_t colourColumn = kPad + (greenFirst ? 1u : 0u);
        const std::uint64_t pairs = (columns - 2u * kPad) / 2u;
        const std::uint64_t colourHalf = row * halfColumns + colourColumn / 2u;
        const std::uint64_t aboveHalf = (row - 1u) * halfColumns + greenColumn / 2u;
        const std::uint64_t belowHalf = (row + 1u) * halfColumns + greenColumn / 2u;
        const std::uint64_t leftHalf = row * halfColumns + (greenColumn - 1u) / 2u;
        const std::uint64_t rightHalf = row * halfColumns + (greenColumn + 1u) / 2u;
        const std::uint64_t greenIndex = row * columns + greenColumn;
        const std::uint64_t colourIndex = row * columns + colourColumn;
#if defined(__clang__) && !defined(_MSC_VER)
#pragma clang loop vectorize(assume_safety)
#endif
        for (std::uint64_t pair = 0u; pair < pairs; ++pair)
        {
            const float colourGreen = rgbGreen[colourIndex + 2u * pair];
            const std::uint64_t colourDestination = destinationRow + colourColumn + 2u * pair;
            tile.green[colourDestination] = whiteLevel * colourGreen;
            tile.red[colourDestination] = whiteLevel * (colourGreen - greenRed[colourHalf + pair]);
            tile.blue[colourDestination] = whiteLevel * (colourGreen - greenBlue[colourHalf + pair]);

            const float green = rgbGreen[greenIndex + 2u * pair];
            const std::uint64_t greenDestination = destinationRow + greenColumn + 2u * pair;
            tile.green[greenDestination] = whiteLevel * green;
            const float above = directionWeight[aboveHalf + pair];
            const float right = directionWeight[rightHalf + pair];
            const float left = directionWeight[leftHalf + pair];
            const float below = directionWeight[belowHalf + pair];
            const float scale = 1.0f / (above + 2.0f - right - left + below);
            const float redSum = above * greenRed[aboveHalf + pair] + (1.0f - right) * greenRed[rightHalf + pair] +
                                 (1.0f - left) * greenRed[leftHalf + pair] + below * greenRed[belowHalf + pair];
            const float blueSum = above * greenBlue[aboveHalf + pair] + (1.0f - right) * greenBlue[rightHalf + pair] +
                                  (1.0f - left) * greenBlue[leftHalf + pair] + below * greenBlue[belowHalf + pair];
            tile.red[greenDestination] = whiteLevel * (green - redSum * scale);
            tile.blue[greenDestination] = whiteLevel * (green - blueSum * scale);
        }
        if ((columns - 2u * kPad) % 2u == 0u)
        {
            continue;
        }
        const std::uint64_t column = columns - kPad - 1u;
        const auto index = static_cast<std::int64_t>(row * columns + column);
        const std::uint64_t centreHalf = row * halfColumns + column / 2u;
        const std::uint64_t destination = destinationRow + column;
        const float green = rgbGreen[index];
        tile.green[destination] = whiteLevel * green;
        if (!isGreen(row, column))
        {
            tile.red[destination] = whiteLevel * (green - greenRed[centreHalf]);
            tile.blue[destination] = whiteLevel * (green - greenBlue[centreHalf]);
            continue;
        }
        const std::uint64_t lastAbove = (row - 1u) * halfColumns + column / 2u;
        const std::uint64_t lastBelow = (row + 1u) * halfColumns + column / 2u;
        const std::uint64_t lastLeft = row * halfColumns + (column - 1u) / 2u;
        const std::uint64_t lastRight = row * halfColumns + (column + 1u) / 2u;
        const float scale = 1.0f / (directionWeight[lastAbove] + 2.0f - directionWeight[lastRight] -
                                    directionWeight[lastLeft] + directionWeight[lastBelow]);
        const float redSum = directionWeight[lastAbove] * greenRed[lastAbove] +
                             (1.0f - directionWeight[lastRight]) * greenRed[lastRight] +
                             (1.0f - directionWeight[lastLeft]) * greenRed[lastLeft] +
                             directionWeight[lastBelow] * greenRed[lastBelow];
        const float blueSum = directionWeight[lastAbove] * greenBlue[lastAbove] +
                              (1.0f - directionWeight[lastRight]) * greenBlue[lastRight] +
                              (1.0f - directionWeight[lastLeft]) * greenBlue[lastLeft] +
                              directionWeight[lastBelow] * greenBlue[lastBelow];
        tile.red[destination] = whiteLevel * (green - redSum * scale);
        tile.blue[destination] = whiteLevel * (green - blueSum * scale);
    }
}

void demosaicAmazeReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    demosaicAmazeBody(tile, scratch, scratchFloats, nullptr);
}

} // namespace an
