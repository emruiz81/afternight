// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DemosaicKernels.h"

#include <array>
#include <cstdint>
#include <limits>

// The shared half of the demosaic kernels: how much scratch a tile of each method needs, how big a
// tile is, which implementation answers for a method, and the two kernels short enough to live
// beside that bookkeeping.
//
// **The border rule is one rule.** Every method leaves an outer band it cannot reach, and the band
// is filled here rather than by each method, so that changing the method changes the middle of a
// frame and not its edges. It is one kernel for both shapes of mosaic: the weights are the shape's,
// the guard against a neighbourhood holding no site of a colour is unconditional, and a corner of a
// tabulated mosaic and a picture two pixels wide take the same path.
//
// The rule is adapted from the border fills of librtprocess's copy of RawTherapee (`demosaic/border.cc`,
// `bayerborder_demosaic` and `xtransborder_demosaic`), which states
// `Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>` and is distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. Upstream's are the like-filtered average of the clipped
// three-by-three neighbourhood, the measured sample kept for its own colour, and the tabulated mosaic's
// weights - one half for an edge-adjacent neighbour, one quarter for a diagonal one. The engine's are
// one kernel for both mosaics and the per-colour fallback to the measured sample where a colour has no
// site. The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// **Bilinear is the measurement of the others.** It has no directional decision, so a difference
// between it and any other method is the other method's decision rather than the input, which is
// what makes it worth having as a method rather than only as a first draft.
//
// It is written as the four cases it is - the mean of the four orthogonal greens and of the four
// diagonals at a colour site, and of the two horizontal and the two vertical neighbours at a green
// one - rather than as the mean of the like-filtered samples of a three-by-three neighbourhood,
// which is the same arithmetic in a different summation order. The reason is the vector kernel: a
// case written out lane by lane is bit-exact against a case written out sample by sample, and a
// generically accumulated sum is not.

namespace an
{
namespace
{

/// How much padding around a tile a method's working planes need, and how many of them there are.
///
/// Two numbers rather than a byte count, so that each method states the shape of its scratch where
/// the shape is decided and the arithmetic that turns it into a size lives once.
struct ScratchShape
{
    /// How many pixels beyond the written region the working planes cover.
    std::uint32_t pad = 0u;

    /// How many working planes of that padded region the method holds at once.
    std::uint32_t planes = 0u;
};

/// The scratch shape of one method.
///
/// @param method the estimate.
/// @return its padding and plane count; zeroes for a method that needs no scratch and for a value
///         outside the enumeration.
[[nodiscard]] constexpr ScratchShape scratchShapeOf(DemosaicMethod method) noexcept
{
    switch (method)
    {
    case DemosaicMethod::Bilinear:
    case DemosaicMethod::XTransFast:
        return ScratchShape{};
    case DemosaicMethod::Fast:
        // Three colour planes over a region padded by two, which is what the colour-difference
        // passes read beyond the pixels they write.
        return ScratchShape{.pad = 2u, .planes = 3u};
    case DemosaicMethod::Rcd:
        // The mosaic copy, three colour planes, the direction discrimination, four half-resolution
        // planes and four rolling rows. The padding is the whole reach, because this estimate reads
        // its own earlier passes and therefore works over the padded tile rather than beside it -
        // which is also what makes the padded origin even, and the half-plane addressing correct.
        // The size is not derived from this padding: `demosaicTileScratchFloats` sizes the planes
        // at the dialect's fixed working extent.
        return ScratchShape{.pad = 9u, .planes = 8u};
    case DemosaicMethod::Amaze:
        // Twelve full working planes - the tile, the green estimate, the gradient sum and the two
        // directional weights, the four colour-difference interpolations, the squared difference
        // between the two chosen ones and the two spreads - and sixteen half-resolution ones, which
        // together fit inside twenty-one full planes. The padding is the whole reach,
        // for the same two reasons the ratio-corrected estimate has it: this method works over its
        // padded tile, and an even padded origin is what makes the half-plane addressing correct.
        return ScratchShape{.pad = 16u, .planes = 21u};
    case DemosaicMethod::MarkesteijnOnePass:
        // Four directions of three colours, the perceptual triple, one bend measure per direction,
        // the homogeneity map, its five-by-five sum and that sum's per-pixel best as bytes, and the
        // two half-width green bounds.
        return ScratchShape{.pad = 14u, .planes = 23u};
    case DemosaicMethod::MarkesteijnThreePass:
        // The same planes for eight directions rather than four.
        return ScratchShape{.pad = 14u, .planes = 41u};
    case DemosaicMethod::Lmmse:
        // The horizontal and vertical colour differences, their two low passes and the green
        // estimate.
        return ScratchShape{.pad = 12u, .planes = 5u};
    case DemosaicMethod::Vng4:
        // Four entries per pixel in the initial working image plus the final green plane.
        return ScratchShape{.pad = 4u, .planes = 5u};
    case DemosaicMethod::LuminanceAhd:
        // Horizontal and vertical RGB candidates, their two lightness planes and two homogeneity
        // maps.
        return ScratchShape{.pad = 5u, .planes = 10u};
    }
    return ScratchShape{};
}

/// The like-filtered weighted average of a clipped three-by-three neighbourhood.
///
/// The border rule, in one place, for one pixel. It never divides by zero: a colour with no site in
/// the neighbourhood answers with the pixel's own measurement, which is the only value present that
/// was measured at all.
///
/// @param tile the picture and its mosaic.
/// @param x the picture column.
/// @param y the picture row.
/// @param cornerWeight what a diagonal neighbour counts for.
/// @param edgeWeight what an edge-adjacent neighbour counts for.
/// @param red set to the red estimate.
/// @param green set to the green estimate.
/// @param blue set to the blue estimate.
void borderPixel(const DemosaicTile& tile, std::uint32_t x, std::uint32_t y, float cornerWeight, float edgeWeight,
                 float& red, float& green, float& blue) noexcept
{
    std::array<float, 3> sum{};
    std::array<float, 3> count{};
    const std::uint32_t lowestX = x == 0u ? 0u : x - 1u;
    const std::uint32_t highestX = x + 1u < tile.width ? x + 1u : tile.width - 1u;
    const std::uint32_t lowestY = y == 0u ? 0u : y - 1u;
    const std::uint32_t highestY = y + 1u < tile.height ? y + 1u : tile.height - 1u;
    for (std::uint32_t neighbourY = lowestY; neighbourY <= highestY; ++neighbourY)
    {
        for (std::uint32_t neighbourX = lowestX; neighbourX <= highestX; ++neighbourX)
        {
            if (neighbourX == x && neighbourY == y)
            {
                continue;
            }
            const bool onEdge = neighbourX == x || neighbourY == y;
            const float weight = onEdge ? edgeWeight : cornerWeight;
            const std::size_t color = static_cast<std::size_t>(tile.cfa->colorAt(neighbourX, neighbourY));
            sum[color] += tile.source[static_cast<std::uint64_t>(neighbourY) * tile.sourceStride + neighbourX] * weight;
            count[color] += weight;
        }
    }
    const float measured = tile.source[static_cast<std::uint64_t>(y) * tile.sourceStride + x];
    const std::size_t own = static_cast<std::size_t>(tile.cfa->colorAt(x, y));
    std::array<float, 3> estimate{};
    for (std::size_t color = 0u; color < 3u; ++color)
    {
        estimate[color] = (color == own || count[color] <= 0.0f) ? measured : sum[color] / count[color];
    }
    red = estimate[0];
    green = estimate[1];
    blue = estimate[2];
}

} // namespace

std::uint64_t demosaicTileScratchFloats(DemosaicMethod method, std::uint32_t regionWidth,
                                        std::uint32_t regionHeight) noexcept
{
    const ScratchShape shape = scratchShapeOf(method);
    if (shape.planes == 0u || regionWidth == 0u || regionHeight == 0u)
    {
        return 0u;
    }
    if (method == DemosaicMethod::Rcd)
    {
        // The adopted RCD dialect uses fixed 194 by 194 working arrays even for a partial edge
        // tile. Its half-resolution indexing is defined against that fixed row stride.
        constexpr std::uint64_t kRcdTileExtent = 194u;
        return kRcdTileExtent * kRcdTileExtent * shape.planes;
    }
    const std::uint64_t paddedWidth = static_cast<std::uint64_t>(regionWidth) + 2u * shape.pad;
    const std::uint64_t paddedHeight = static_cast<std::uint64_t>(regionHeight) + 2u * shape.pad;
    const std::uint64_t area = paddedWidth * paddedHeight;
    if (paddedHeight != 0u && area / paddedHeight != paddedWidth)
    {
        return (std::numeric_limits<std::uint64_t>::max)();
    }
    const std::uint64_t total = area * shape.planes;
    if (area != 0u && total / area != shape.planes)
    {
        return (std::numeric_limits<std::uint64_t>::max)();
    }
    return total;
}

void demosaicTileShape(DemosaicMethod method, std::uint32_t& width, std::uint32_t& height) noexcept
{
    switch (method)
    {
    case DemosaicMethod::Bilinear:
    case DemosaicMethod::XTransFast:
        width = 256u;
        height = 64u;
        return;
    case DemosaicMethod::Fast:
        width = 224u;
        height = 64u;
        return;
    case DemosaicMethod::Rcd:
        // RCD's rolling buffers and discard ring are defined by rtprocess' 194-sample
        // padded tile (176 writable samples plus a nine-sample halo on each side).
        // Retaining that geometry is part of the adopted scalar dialect: changing either
        // writable extent changes the floating-point evaluation at internal tile seams.
        width = 176u;
        height = 176u;
        return;
    case DemosaicMethod::Amaze:
        width = 96u;
        height = 96u;
        return;
    case DemosaicMethod::MarkesteijnOnePass:
    case DemosaicMethod::MarkesteijnThreePass:
        width = 96u;
        height = 64u;
        return;
    case DemosaicMethod::Lmmse:
        width = 128u;
        height = 64u;
        return;
    case DemosaicMethod::Vng4:
        width = 192u;
        height = 64u;
        return;
    case DemosaicMethod::LuminanceAhd:
        width = 128u;
        height = 64u;
        return;
    }
    width = 128u;
    height = 32u;
}

DemosaicRunFn referenceDemosaicRun(DemosaicMethod method) noexcept
{
    switch (method)
    {
    case DemosaicMethod::Bilinear:
        return &demosaicBilinearReference;
    case DemosaicMethod::Fast:
        return &demosaicFastReference;
    case DemosaicMethod::Rcd:
        return &demosaicRcdReference;
    case DemosaicMethod::Amaze:
        return &demosaicAmazeReference;
    case DemosaicMethod::XTransFast:
        return &demosaicXTransFastReference;
    case DemosaicMethod::MarkesteijnOnePass:
        return &demosaicMarkesteijnOneReference;
    case DemosaicMethod::MarkesteijnThreePass:
        return &demosaicMarkesteijnThreeReference;
    case DemosaicMethod::Lmmse:
        return &demosaicLmmseReference;
    case DemosaicMethod::Vng4:
        return &demosaicVng4Reference;
    case DemosaicMethod::LuminanceAhd:
        return &demosaicLuminanceAhdReference;
    }
    return nullptr;
}

void demosaicBorderRun(const DemosaicTile& tile) noexcept
{
    const bool tabulated = cfaPatternIsTabulated(tile.cfa->pattern());
    const float cornerWeight = tabulated ? 0.25f : 1.0f;
    const float edgeWeight = tabulated ? 0.5f : 1.0f;
    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
        {
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;
            borderPixel(tile, x, y, cornerWeight, edgeWeight, red, green, blue);
            tile.red[destinationRow + x] = red;
            tile.green[destinationRow + x] = green;
            tile.blue[destinationRow + x] = blue;
        }
    }
}

void demosaicBilinearReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    static_cast<void>(scratch);
    static_cast<void>(scratchFloats);
    const CfaDescriptor& cfa = *tile.cfa;
    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        const std::uint64_t centreRow = static_cast<std::uint64_t>(y) * tile.sourceStride;
        const std::uint64_t aboveRow = centreRow - tile.sourceStride;
        const std::uint64_t belowRow = centreRow + tile.sourceStride;
        const bool greenAtBegin = cfa.colorAt(tile.xBegin, y) == CfaColor::Green;
        const std::uint32_t colourColumn = greenAtBegin ? tile.xBegin + 1u : tile.xBegin;
        const bool rowIsRed = cfa.colorAt(colourColumn, y) == CfaColor::Red;
        float* const rowColour = rowIsRed ? tile.red : tile.blue;
        float* const otherColour = rowIsRed ? tile.blue : tile.red;
        for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
        {
            const float centre = tile.source[centreRow + x];
            const float above = tile.source[aboveRow + x];
            const float below = tile.source[belowRow + x];
            const float left = tile.source[centreRow + x - 1u];
            const float right = tile.source[centreRow + x + 1u];
            const float orthogonal = ((above + below) + (left + right)) * 0.25f;
            const float diagonal = ((tile.source[aboveRow + x - 1u] + tile.source[aboveRow + x + 1u]) +
                                    (tile.source[belowRow + x - 1u] + tile.source[belowRow + x + 1u])) *
                                   0.25f;
            const float horizontal = (left + right) * 0.5f;
            const float vertical = (above + below) * 0.5f;
            const bool onGreen = (((x - tile.xBegin) & 1u) == 0u) == greenAtBegin;
            tile.green[destinationRow + x] = onGreen ? centre : orthogonal;
            rowColour[destinationRow + x] = onGreen ? horizontal : centre;
            otherColour[destinationRow + x] = onGreen ? vertical : diagonal;
        }
    }
}

} // namespace an
