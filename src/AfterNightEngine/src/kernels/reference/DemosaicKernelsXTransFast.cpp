// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DemosaicKernels.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The three-by-three weighted average over a tabulated mosaic.
//
// Derived from RawTherapee's fast X-Trans demosaic as librtprocess carries it
// (`demosaic/xtransfast.cc`), distributed under the GNU General Public
// License, version 3 or any later version; the file names no copyright holder. The weights are
// unchanged - the four edge-adjacent neighbours count double the corners - and so is the rule that
// a site keeps its own measurement for the filter that covered it. A colour with no site in the
// neighbourhood falls back to the pixel's own measurement, as it does in the family's border rule.
// The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// The arithmetic is written differently, and the difference is a simplification rather than a
// change of intent. The original divides its weighted sums by constants chosen per case: a half for
// green at a red or blue site, one for the opposite colour there, and either one or four thirds for
// red and blue at a green site depending on whether the green is solitary - which it decides by
// comparing the filters two columns apart. Every one of those constants is the total weight the
// neighbourhood happened to put on that colour, so this kernel accumulates that total and divides by
// it. The two agree on every configuration the original decides correctly, and this one also decides
// the configuration the original's test cannot see: a green whose one red and one blue neighbour are
// both vertical, which the table validation permits and the column comparison would call solitary.
//
// One consequence is worth stating rather than discovering. The original spells four thirds as the
// literal `1.3333333f`, which is a float about one part in ten million below the quotient. Dividing
// by the accumulated weight gives the correctly rounded quotient instead, so this kernel and the
// library differ in the last bits at every non-solitary green site.

namespace an
{

void demosaicXTransFastReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    static_cast<void>(scratch);
    static_cast<void>(scratchFloats);
    constexpr std::array<float, 9> kWeights = {0.25f, 0.5f, 0.25f, 0.5f, 0.0f, 0.5f, 0.25f, 0.5f, 0.25f};
    const CfaDescriptor& cfa = *tile.cfa;
    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::uint64_t centreRow = static_cast<std::uint64_t>(y) * tile.sourceStride;
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
        {
            std::array<float, 3> sum{};
            std::array<float, 3> weight{};
            for (std::uint32_t row = 0u; row < 3u; ++row)
            {
                const std::uint32_t neighbourY = y + row - 1u;
                const std::uint64_t sourceRow = static_cast<std::uint64_t>(neighbourY) * tile.sourceStride;
                for (std::uint32_t column = 0u; column < 3u; ++column)
                {
                    const std::uint32_t neighbourX = x + column - 1u;
                    const std::size_t color = static_cast<std::size_t>(cfa.colorAt(neighbourX, neighbourY));
                    const float share = kWeights[static_cast<std::size_t>(row) * 3u + column];
                    sum[color] += tile.source[sourceRow + neighbourX] * share;
                    weight[color] += share;
                }
            }
            const float measured = tile.source[centreRow + x];
            const std::size_t own = static_cast<std::size_t>(cfa.colorAt(x, y));
            std::array<float, 3> estimate{};
            for (std::size_t color = 0u; color < 3u; ++color)
            {
                estimate[color] = (color == own || weight[color] <= 0.0f) ? measured : sum[color] / weight[color];
            }
            tile.red[destinationRow + x] = estimate[0];
            tile.green[destinationRow + x] = estimate[1];
            tile.blue[destinationRow + x] = estimate[2];
        }
    }
}

} // namespace an
