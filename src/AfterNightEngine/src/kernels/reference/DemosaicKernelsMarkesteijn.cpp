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
#include <limits>

// Markesteijn interpolation over a tabulated mosaic.
//
// Derived from Frank Markesteijn's X-Trans algorithm, itself from dcraw, as adapted to RawTherapee
// by Ingo Weyrich in 2014 and carried by librtprocess (`demosaic/markesteijn.cc`, distributed
// under the GNU General Public License, version 3 or any later version). The structure is
// unchanged: a hexagon of like-filtered neighbours is mapped around every pixel, green is
// interpolated along four directions (eight after the second pass), red and blue are filled from
// the green plane, each direction is measured in a perceptual space, and the most homogeneous
// directions are averaged. The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// What changed, and why.
//
// **A colour is a colour, not a table index.** The original threads the filter of a pixel through as
// a small integer and indexes colour planes with it. Here the mosaic is asked what covers a pixel and
// the answer is a named colour; the integer only survives where it is genuinely an index into the
// three colour planes of one pixel.
//
// **The perceptual space is computed rather than looked up.** The original builds a
// sixty-five-thousand-entry cube-root table indexed by the *integer part* of a sixteen-bit code
// value, which both fixes the numeric domain at sixteen bits and quantises the conversion. Here the
// cube root is computed from the normalised sample, so the method works in whatever domain the
// demosaic plan states and the perceptual measure is not quantised. The measure only ranks four or
// eight directions against each other, so the difference does not move a pixel unless two
// directions were within the quantisation of each other.
//
// **The camera matrix is the identity, and that is stated rather than hidden.** The original takes a
// camera-to-RGB matrix. This method is reached before any colour transform, so the perceptual space
// is computed from the working RGB directly and the option that selects it says so. The cheaper
// luminance-chrominance space remains the default.
//
// **The edge of the picture is the family's border rule.** The original leaves eight pixels to its
// own border kernel and relies on the picture having three pixels of margin outside every tile. Here
// the outer band is the shared three-by-three rule at the family's stated reach, and every tile has
// its whole halo inside the picture - so there is no bounds test in this file.
//
// **Nothing is read outside the region that was written.** Two of the original's inner passes read
// four pixels beyond the tile they were given, into whatever the neighbouring direction plane held.
// Here each pass is narrowed to the region whose reads land inside the region the pass before it
// wrote, and every tile starts from cleared scratch.
//
// The edge rule, the narrowed passes and the cleared scratch make the output differ from the
// library's own answer.
//
// The body takes optional lane-wise passes through `DemosaicMarkesteijnVectorOps`: the reference
// passes none, and a compiled CPU target replaces the luminance-chrominance conversion and the bend
// measure with lane-wise spellings of the same arithmetic.

namespace an
{
namespace
{

/// How far this estimate reads from the pixel it writes.
constexpr std::uint32_t kPad = 14u;

/// The two orthogonal bases the hexagon table is generated over.
constexpr short kOrthogonal[12] = {1, 0, 0, 1, -1, 0, 0, -1, 1, 0, 0, 1};

/// The hexagon offsets of a green pixel and of a non-green one, in the generating basis.
constexpr short kPattern[2][16] = {{0, 1, 0, -1, 2, 0, -1, 0, 1, 1, 1, -1, 0, 0, 0, 0},
                                   {0, 1, 0, -2, 1, 0, -2, 0, 1, 1, -2, -2, 1, -1, -1, 1}};

/// The linear-RGB to CIE XYZ matrix, and the D65 white it is normalised by.
constexpr float kXyzFromRgb[3][3] = {
    {0.412453f, 0.357580f, 0.180423f}, {0.212671f, 0.715160f, 0.072169f}, {0.019334f, 0.119193f, 0.950227f}};

/// The D65 white point the perceptual conversion divides each row by.
constexpr float kD65White[3] = {0.950456f, 1.0f, 1.088754f};

/// The square of a value.
///
/// @param value the value.
/// @return its square.
[[nodiscard]] inline float squared(float value) noexcept
{
    return value * value;
}

/// The value clamped to a closed interval.
///
/// @param value the value.
/// @param lowest the lower bound.
/// @param highest the upper bound.
/// @return the clamped value.
[[nodiscard]] inline float clampTo(float value, float lowest, float highest) noexcept
{
    return (std::max)(lowest, (std::min)(value, highest));
}

/// The lightness companding function of the perceptual space.
///
/// The cube root above the sRGB epsilon and the linear segment below it, computed rather than looked
/// up in a table indexed by a sixteen-bit code.
///
/// @param value the normalised tristimulus value.
/// @return its companded value.
[[nodiscard]] inline float compand(float value) noexcept
{
    constexpr float kEpsilon = 216.0f / 24389.0f;
    constexpr float kKappa = 24389.0f / 27.0f;
    if (value <= 0.0f)
    {
        return (kKappa * value + 16.0f) / 116.0f;
    }
    return value > kEpsilon ? std::cbrt(value) : (kKappa * value + 16.0f) / 116.0f;
}

/// The hexagon tables and the solitary-green offset one mosaic generates.
struct HexagonTables
{
    /// Offsets into the source plane, by pixel position within the three-by-three block.
    std::array<std::array<std::array<std::int64_t, 8>, 3>, 3> source{};

    /// The same offsets into a padded tile.
    std::array<std::array<std::array<std::int64_t, 8>, 3>, 3> tile{};

    /// Row of the solitary green pixel within the three-by-three block.
    std::uint32_t solitaryRow = 0u;

    /// Column of the solitary green pixel within the three-by-three block.
    std::uint32_t solitaryColumn = 0u;

    /// Whether a row of the block holds two green pixels in its first three columns.
    std::array<bool, 3> rightShift{};
};

/// Builds the hexagon tables for one mosaic.
///
/// @param cfa the mosaic, whose phase is already applied by `colorAt`.
/// @param sourceStride floats between two rows of the source plane.
/// @param tileStride floats between two rows of a padded tile.
/// @return the tables.
[[nodiscard]] HexagonTables buildHexagons(const CfaDescriptor& cfa, std::int64_t sourceStride,
                                          std::int64_t tileStride) noexcept
{
    const auto isGreen = [&cfa](std::int64_t row, std::int64_t column) noexcept {
        return cfa.colorAt(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row)) == CfaColor::Green;
    };
    HexagonTables tables{};
    for (std::int64_t row = 0; row < 3; ++row)
    {
        std::uint32_t greens = 0u;
        for (std::int64_t column = 0; column < 3; ++column)
        {
            greens += isGreen(row, column) ? 1u : 0u;
        }
        tables.rightShift[static_cast<std::size_t>(row)] = greens == 2u;
    }
    for (std::int64_t row = 0; row < 3; ++row)
    {
        for (std::int64_t column = 0; column < 3; ++column)
        {
            const int green = isGreen(row, column) ? 1 : 0;
            int run = 0;
            for (int step = 0; step < 10; step += 2)
            {
                if (isGreen(row + kOrthogonal[step] + 6, column + kOrthogonal[step + 2] + 6))
                {
                    run = 0;
                }
                else
                {
                    ++run;
                }
                if (run == 4)
                {
                    tables.solitaryRow = static_cast<std::uint32_t>(row);
                    tables.solitaryColumn = static_cast<std::uint32_t>(column);
                }
                if (run != green + 1)
                {
                    continue;
                }
                for (int entry = 0; entry < 8; ++entry)
                {
                    const int vertical = kOrthogonal[step] * kPattern[green][entry * 2] +
                                         kOrthogonal[step + 1] * kPattern[green][entry * 2 + 1];
                    const int horizontal = kOrthogonal[step + 2] * kPattern[green][entry * 2] +
                                           kOrthogonal[step + 3] * kPattern[green][entry * 2 + 1];
                    const std::size_t slot = static_cast<std::size_t>(entry ^ ((green * 2) & step));
                    tables.source[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)][slot] =
                        horizontal + vertical * sourceStride;
                    tables.tile[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)][slot] =
                        horizontal + vertical * tileStride;
                }
            }
        }
    }
    return tables;
}

} // namespace

void demosaicMarkesteijnBodyWithOps(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                                    std::uint32_t passes, const DemosaicMarkesteijnVectorOps* vectorOps) noexcept
{
    const std::uint32_t regionWidth = tile.xEnd - tile.xBegin;
    const std::uint32_t regionHeight = tile.yEnd - tile.yBegin;
    if (regionWidth == 0u || regionHeight == 0u)
    {
        return;
    }
    const std::uint64_t rows = static_cast<std::uint64_t>(regionHeight) + 2u * kPad;
    const std::uint64_t columns = static_cast<std::uint64_t>(regionWidth) + 2u * kPad;
    const std::uint64_t pixels = rows * columns;
    const std::uint32_t directions = passes > 1u ? 8u : 4u;
    const std::uint64_t halfColumns = (columns + 1u) / 2u;

    // rgb: directions * 3 floats per pixel. lab: 3. derivative: directions.
    // homogeneity, its five-by-five sum and that sum's maximum, in bytes. Then the two green bounds,
    // as half-width float planes.
    const std::uint64_t rgbFloats = static_cast<std::uint64_t>(directions) * 3u * pixels;
    const std::uint64_t labFloats = 3u * pixels;
    const std::uint64_t derivativeFloats = static_cast<std::uint64_t>(directions) * pixels;
    const std::uint64_t byteFloats = (static_cast<std::uint64_t>(2u * directions + 1u) * pixels + 3u) / 4u;
    const std::uint64_t boundsFloats = 2u * rows * halfColumns;
    const std::uint64_t needed = rgbFloats + labFloats + derivativeFloats + byteFloats + boundsFloats;
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

    float* const rgbBase = scratch;
    float* const lab = rgbBase + rgbFloats;
    float* const derivative = lab + labFloats;
    auto* const homogeneity = reinterpret_cast<std::uint8_t*>(derivative + derivativeFloats);
    std::uint8_t* const homogeneitySum = homogeneity + static_cast<std::uint64_t>(directions) * pixels;
    std::uint8_t* const homogeneityBest = homogeneitySum + static_cast<std::uint64_t>(directions) * pixels;
    float* const greenLowest = derivative + derivativeFloats + byteFloats;
    float* const greenHighest = greenLowest + rows * halfColumns;

    const std::uint32_t padBeginX = tile.xBegin - kPad;
    const std::uint32_t padBeginY = tile.yBegin - kPad;
    const CfaDescriptor& cfa = *tile.cfa;
    const auto sourceStride = static_cast<std::int64_t>(tile.sourceStride);
    const auto tileStride = static_cast<std::int64_t>(columns);
    const HexagonTables hexagons = buildHexagons(cfa, sourceStride, tileStride);

    /// Which filter covers a pixel of the padded tile.
    const auto colourAt = [&cfa, padBeginX, padBeginY](std::uint64_t row, std::uint64_t column) noexcept {
        return cfa.colorAt(padBeginX + static_cast<std::uint32_t>(column), padBeginY + static_cast<std::uint32_t>(row));
    };
    /// Whether a pixel of the padded tile is a green site.
    const auto isGreen = [&colourAt](std::uint64_t row, std::uint64_t column) noexcept {
        return colourAt(row, column) == CfaColor::Green;
    };
    /// The channel index of the filter covering a pixel of the padded tile.
    const auto channelAt = [&colourAt](std::uint64_t row, std::uint64_t column) noexcept {
        return static_cast<std::size_t>(colourAt(row, column));
    };
    /// The hexagon table entry for a pixel of the padded tile.
    const auto hexagonOf = [&hexagons, padBeginX, padBeginY](bool tileSpace, std::uint64_t row,
                                                             std::uint64_t column) noexcept -> const std::int64_t* {
        const std::size_t blockRow = static_cast<std::size_t>((padBeginY + row) % 3u);
        const std::size_t blockColumn = static_cast<std::size_t>((padBeginX + column) % 3u);
        return tileSpace ? hexagons.tile[blockRow][blockColumn].data() : hexagons.source[blockRow][blockColumn].data();
    };
    /// The three colour samples of one direction at one pixel of the padded tile.
    const auto pixelOf = [rgbBase, pixels](std::uint32_t direction, std::int64_t index) noexcept -> float* {
        return rgbBase + (static_cast<std::int64_t>(direction) * static_cast<std::int64_t>(pixels) + index) * 3;
    };
    /// One sample of the source plane at a padded-tile position.
    const auto sourceAt = [&tile, padBeginX, padBeginY](std::uint64_t row, std::uint64_t column) noexcept {
        return tile.source + static_cast<std::uint64_t>(padBeginY + row) * tile.sourceStride + (padBeginX + column);
    };
    /// The first column at or after `base` that is not a green site.
    const auto firstColourColumn = [&isGreen, columns](std::uint64_t row, std::uint64_t base) noexcept {
        std::uint64_t column = base;
        while (column < columns && isGreen(row, column))
        {
            ++column;
        }
        return column;
    };

    const float whiteLevel = tile.whiteLevel;

    /// The extremes of one pixel's hexagon of green neighbours, in the source plane.
    const auto hexagonBounds = [&](std::uint64_t row, std::uint64_t column, const std::int64_t* hexagon, float& lowest,
                                   float& highest) noexcept {
        const float* const centre = sourceAt(row, column);
        lowest = (std::numeric_limits<float>::max)();
        highest = 0.0f;
        for (int entry = 0; entry < 6; ++entry)
        {
            const float value = centre[hexagon[entry]];
            lowest = (std::min)(lowest, value);
            highest = (std::max)(highest, value);
        }
    };

    // The bounds a green estimate is clamped to: the extremes of the hexagon of green neighbours.
    //
    // Two pixels inside the padded tile on every side, because the hexagon reaches that far and this
    // family gives a tile exactly its stated halo rather than a picture margin to run off into.
    const std::uint64_t boundsEnd = columns - 2u;
    for (std::uint64_t row = 2u; row < rows - 2u; ++row)
    {
        const std::uint64_t start = firstColourColumn(row, 2u);
        if (start >= boundsEnd)
        {
            continue;
        }
        const bool shifted = hexagons.rightShift[(padBeginY + row) % 3u];
        float lowest = 0.0f;
        float highest = 0.0f;
        if (shifted)
        {
            const std::int64_t* const hexagon = hexagonOf(false, row, start);
            for (std::uint64_t column = start; column < boundsEnd; column += 3u)
            {
                hexagonBounds(row, column, hexagon, lowest, highest);
                greenLowest[row * halfColumns + column / 2u] = lowest;
                greenHighest[row * halfColumns + column / 2u] = highest;
            }
            continue;
        }
        std::uint64_t column = start;
        if (!isGreen(row, start + 1u))
        {
            // The pair starts one column early, so its first entry is written on its own.
            const std::int64_t* const hexagon = hexagonOf(false, row, column);
            hexagonBounds(row, column, hexagon, lowest, highest);
            greenLowest[row * halfColumns + column / 2u] = lowest;
            greenHighest[row * halfColumns + column / 2u] = highest;
            column += 2u;
        }
        if (column >= boundsEnd)
        {
            continue;
        }
        const std::int64_t* const hexagon = hexagonOf(false, row, column);
        for (; column + 1u < boundsEnd; column += 3u)
        {
            hexagonBounds(row, column, hexagon, lowest, highest);
            greenLowest[row * halfColumns + column / 2u] = lowest;
            greenHighest[row * halfColumns + column / 2u] = highest;
            greenLowest[row * halfColumns + (column + 1u) / 2u] = lowest;
            greenHighest[row * halfColumns + (column + 1u) / 2u] = highest;
        }
        if (column < boundsEnd)
        {
            hexagonBounds(row, column, hexagon, lowest, highest);
            greenLowest[row * halfColumns + column / 2u] = lowest;
            greenHighest[row * halfColumns + column / 2u] = highest;
        }
    }

    // The mosaic, placed in the channel its filter measured, for every direction.
    for (std::uint64_t row = 0u; row < rows; ++row)
    {
        for (std::uint64_t column = 0u; column < columns; ++column)
        {
            const auto index = static_cast<std::int64_t>(row * columns + column);
            pixelOf(0u, index)[channelAt(row, column)] = *sourceAt(row, column);
        }
    }
    for (std::uint32_t direction = 1u; direction < directions; ++direction)
    {
        std::memcpy(pixelOf(direction, 0), pixelOf(0u, 0), static_cast<std::size_t>(pixels) * 3u * sizeof(float));
    }

    // Green along the four directions, from the hexagon of green neighbours.
    for (std::uint64_t row = 6u; row < rows - 6u; ++row)
    {
        const std::uint64_t start = firstColourColumn(row, 6u);
        if (start >= columns - 6u)
        {
            continue;
        }
        const bool shifted = hexagons.rightShift[(padBeginY + row) % 3u];
        std::uint64_t step = shifted ? 3u : (isGreen(row, start + 1u) ? 2u : 1u);
        std::uint64_t hexagonIndex = 0u;
        const std::int64_t* const hexagons0 = hexagonOf(false, row, start);
        const std::int64_t* const hexagons1 = hexagonOf(false, row, start + step);
        for (std::uint64_t column = start; column < columns - 6u;)
        {
            const std::int64_t* const hexagon = shifted ? hexagons0 : (hexagonIndex == 0u ? hexagons0 : hexagons1);
            const float* const centre = sourceAt(row, column);
            std::array<float, 4> colour{};
            colour[0] = 0.6796875f * (centre[hexagon[1]] + centre[hexagon[0]]) -
                        0.1796875f * (centre[2 * hexagon[1]] + centre[2 * hexagon[0]]);
            colour[1] = 0.87109375f * centre[hexagon[3]] + centre[hexagon[2]] * 0.12890625f +
                        0.359375f * (centre[0] - centre[-hexagon[2]]);
            for (int index = 0; index < 2; ++index)
            {
                colour[static_cast<std::size_t>(2 + index)] =
                    0.640625f * centre[hexagon[4 + index]] + 0.359375f * centre[-2 * hexagon[4 + index]] +
                    0.12890625f * (2.0f * centre[0] - centre[3 * hexagon[4 + index]] - centre[-3 * hexagon[4 + index]]);
            }
            const std::uint64_t bound = row * halfColumns + column / 2u;
            const auto index = static_cast<std::int64_t>(row * columns + column);
            for (std::uint32_t direction = 0u; direction < 4u; ++direction)
            {
                const std::uint32_t slot = shifted ? direction : (direction ^ 1u);
                pixelOf(slot, index)[1] = clampTo(colour[direction], greenLowest[bound], greenHighest[bound]);
            }
            column += step;
            if (!shifted)
            {
                step ^= 3u;
                hexagonIndex ^= 1u;
            }
        }
    }

    for (std::uint32_t pass = 0u; pass < passes; ++pass)
    {
        const std::uint32_t base = pass == 0u ? 0u : 4u;
        if (pass == 1u)
        {
            std::memcpy(pixelOf(4u, 0), pixelOf(0u, 0), 4u * static_cast<std::size_t>(pixels) * 3u * sizeof(float));
        }

        // Green again, from the interpolated values of the closer pixels.
        if (pass != 0u)
        {
            for (std::uint64_t row = 10u; row < rows - 10u; ++row)
            {
                const std::uint64_t start = firstColourColumn(row, 10u);
                if (start >= columns - 10u)
                {
                    continue;
                }
                const bool shifted = hexagons.rightShift[(padBeginY + row) % 3u];
                std::uint64_t step = shifted ? 3u : (isGreen(row, start + 1u) ? 2u : 1u);
                std::uint64_t hexagonIndex = 0u;
                const std::int64_t* const hexagons0 = hexagonOf(true, row, start);
                const std::int64_t* const hexagons1 = hexagonOf(true, row, start + step);
                std::size_t channel = channelAt(row, start);
                for (std::uint64_t column = start; column < columns - 10u;)
                {
                    const std::int64_t* const hexagon =
                        shifted ? hexagons0 : (hexagonIndex == 0u ? hexagons0 : hexagons1);
                    const auto index = static_cast<std::int64_t>(row * columns + column);
                    const std::uint64_t bound = row * halfColumns + column / 2u;
                    for (int direction = 3; direction < 6; ++direction)
                    {
                        const std::uint32_t slot =
                            base + static_cast<std::uint32_t>(shifted ? direction - 2 : ((direction - 2) ^ 1));
                        float* const centre = pixelOf(slot, index);
                        const std::int64_t offset = hexagon[direction];
                        const float value =
                            0.33333333f * (centre[-2 * offset * 3 + 1] +
                                           2.0f * (centre[offset * 3 + 1] -
                                                   centre[offset * 3 + static_cast<std::int64_t>(channel)]) -
                                           centre[-2 * offset * 3 + static_cast<std::int64_t>(channel)]) +
                            centre[channel];
                        centre[1] = clampTo(value, greenLowest[bound], greenHighest[bound]);
                    }
                    column += step;
                    if (!shifted)
                    {
                        step ^= 3u;
                        hexagonIndex ^= 1u;
                        channel = channel ^ (step & 2u);
                    }
                    else
                    {
                        channel ^= 2u;
                    }
                }
            }
        }

        // Red and blue at the solitary green pixels.
        {
            // The solitary green sites sit on every third row and every third column of the mosaic,
            // and which third is the mosaic's own answer rather than the tile's: a residue computed
            // in tile-local coordinates is the right residue only when the padded tile happens to
            // start on a multiple of three.
            std::uint64_t firstRow = 10u;
            while (firstRow < rows - 10u && (padBeginY + firstRow) % 3u != hexagons.solitaryRow)
            {
                ++firstRow;
            }
            for (std::uint64_t row = firstRow; row < rows - 10u; row += 3u)
            {
                std::uint64_t column = 10u;
                while (column < columns - 10u && (padBeginX + column) % 3u != hexagons.solitaryColumn)
                {
                    ++column;
                }
                for (; column < columns - 10u; column += 3u)
                {
                    const auto index = static_cast<std::int64_t>(row * columns + column);
                    std::array<std::array<float, 6>, 3> diagonalColour{};
                    std::array<float, 6> difference{};
                    std::size_t channel = channelAt(row, column + 1u);
                    float* centre = pixelOf(base, index);
                    std::int64_t reach = 1;
                    for (int direction = 0; direction < 6; ++direction)
                    {
                        for (int scale = 0; scale < 2; ++scale)
                        {
                            const std::int64_t offset = (reach << scale) * 3;
                            const float green = centre[1] + centre[1] - centre[offset + 1] - centre[-offset + 1];
                            diagonalColour[channel][static_cast<std::size_t>(direction)] =
                                green + centre[offset + static_cast<std::int64_t>(channel)] +
                                centre[-offset + static_cast<std::int64_t>(channel)];
                            if (direction > 1)
                            {
                                difference[static_cast<std::size_t>(direction)] +=
                                    squared(centre[offset + 1] - centre[-offset + 1] -
                                            centre[offset + static_cast<std::int64_t>(channel)] +
                                            centre[-offset + static_cast<std::int64_t>(channel)]) +
                                    squared(green);
                            }
                            channel ^= 2u;
                        }
                        if (direction > 2 && (direction & 1) != 0 &&
                            difference[static_cast<std::size_t>(direction - 1)] <
                                difference[static_cast<std::size_t>(direction)])
                        {
                            diagonalColour[0][static_cast<std::size_t>(direction)] =
                                diagonalColour[0][static_cast<std::size_t>(direction - 1)];
                            diagonalColour[2][static_cast<std::size_t>(direction)] =
                                diagonalColour[2][static_cast<std::size_t>(direction - 1)];
                        }
                        if ((direction & 1) != 0 || direction < 2)
                        {
                            centre[0] = 0.5f * diagonalColour[0][static_cast<std::size_t>(direction)];
                            centre[2] = 0.5f * diagonalColour[2][static_cast<std::size_t>(direction)];
                            centre += static_cast<std::int64_t>(pixels) * 3;
                        }
                        reach ^= tileStride ^ 1;
                        channel ^= 2u;
                    }
                }
            }
        }

        // Red where blue was measured, and the other way round.
        for (std::uint64_t row = 10u; row < rows - 10u; ++row)
        {
            const std::uint64_t start = firstColourColumn(row, 10u);
            if (start >= columns - 10u)
            {
                continue;
            }
            const bool shifted = hexagons.rightShift[(padBeginY + row) % 3u];
            const std::int64_t near = ((padBeginY + row) % 3u != hexagons.solitaryRow) ? tileStride : 1;
            const std::int64_t far = 3 * (near == 1 ? tileStride : 1);
            std::uint64_t step = shifted ? 3u : (isGreen(row, start + 1u) ? 2u : 1u);
            std::size_t channel = 2u - channelAt(row, start);
            for (std::uint64_t column = start; column < columns - 10u;)
            {
                const auto index = static_cast<std::int64_t>(row * columns + column);
                float* centre = pixelOf(base, index);
                for (std::uint32_t direction = 0u; direction < 4u; ++direction)
                {
                    const bool preferNear =
                        direction > 1u || ((direction ^ static_cast<std::uint32_t>(near)) & 1u) != 0u ||
                        ((std::fabs(centre[1] - centre[near * 3 + 1]) + std::fabs(centre[1] - centre[-near * 3 + 1])) <
                         2.0f * (std::fabs(centre[1] - centre[far * 3 + 1]) +
                                 std::fabs(centre[1] - centre[-far * 3 + 1])));
                    const std::int64_t offset = (preferNear ? near : far) * 3;
                    centre[channel] = centre[1] + 0.5f * (centre[offset + static_cast<std::int64_t>(channel)] +
                                                          centre[-offset + static_cast<std::int64_t>(channel)] -
                                                          centre[offset + 1] - centre[-offset + 1]);
                    centre += static_cast<std::int64_t>(pixels) * 3;
                }
                column += step;
                if (!shifted)
                {
                    step ^= 3u;
                    channel = channel ^ (step & 2u);
                }
                else
                {
                    channel ^= 2u;
                }
            }
        }

        // Red and blue in the two-by-two blocks of green.
        for (std::uint64_t row = 10u; row < rows - 10u; ++row)
        {
            if ((padBeginY + row) % 3u == hexagons.solitaryRow)
            {
                continue;
            }
            std::uint64_t start = 10u;
            while (start < columns - 10u && (padBeginX + start) % 3u == hexagons.solitaryColumn)
            {
                ++start;
            }
            if (start >= columns - 10u)
            {
                continue;
            }
            std::uint64_t step = isGreen(row, start + 1u) ? 1u : 2u;
            std::uint64_t hexagonIndex = 0u;
            const std::int64_t* const hexagons0 = hexagonOf(true, row, start);
            const std::int64_t* const hexagons1 = hexagonOf(true, row, start + step);
            for (std::uint64_t column = start; column < columns - 10u;)
            {
                const std::int64_t* const hexagon = hexagonIndex == 0u ? hexagons0 : hexagons1;
                const auto index = static_cast<std::int64_t>(row * columns + column);
                float* centre = pixelOf(base, index);
                for (std::uint32_t pair = 0u; pair < directions / 2u; ++pair)
                {
                    const std::int64_t first = hexagon[2u * pair] * 3;
                    const std::int64_t second = hexagon[2u * pair + 1u] * 3;
                    if (hexagon[2u * pair] + hexagon[2u * pair + 1u] != 0)
                    {
                        const float green = 3.0f * centre[1] - 2.0f * centre[first + 1] - centre[second + 1];
                        centre[0] = (green + 2.0f * centre[first] + centre[second]) * 0.33333333f;
                        centre[2] = (green + 2.0f * centre[first + 2] + centre[second + 2]) * 0.33333333f;
                    }
                    else
                    {
                        const float green = 2.0f * centre[1] - centre[first + 1] - centre[second + 1];
                        centre[0] = (green + centre[first] + centre[second]) * 0.5f;
                        centre[2] = (green + centre[first + 2] + centre[second + 2]) * 0.5f;
                    }
                    centre += static_cast<std::int64_t>(pixels) * 3;
                }
                column += step;
                step ^= 3u;
                hexagonIndex ^= 1u;
            }
        }
    }

    // How much each direction bends, measured in a space where bending is visible.
    const std::array<std::int64_t, 4> directionOffset = {1, tileStride, tileStride + 1, tileStride - 1};
    for (std::uint32_t direction = 0u; direction < directions; ++direction)
    {
        if (!tile.useCieLab && vectorOps != nullptr && vectorOps->lumaChroma != nullptr)
        {
            vectorOps->lumaChroma(pixelOf(direction, 0), pixels, rows, columns, lab);
        }
        else
        {
            for (std::uint64_t row = 10u; row < rows - 10u; ++row)
            {
                for (std::uint64_t column = 10u; column < columns - 10u; ++column)
                {
                    const auto index = static_cast<std::int64_t>(row * columns + column);
                    const float* const centre = pixelOf(direction, index);
                    if (tile.useCieLab)
                    {
                        std::array<float, 3> xyz{};
                        for (std::size_t channel = 0u; channel < 3u; ++channel)
                        {
                            const float value = centre[channel] / whiteLevel;
                            xyz[0] += kXyzFromRgb[0][channel] * value;
                            xyz[1] += kXyzFromRgb[1][channel] * value;
                            xyz[2] += kXyzFromRgb[2][channel] * value;
                        }
                        for (std::size_t component = 0u; component < 3u; ++component)
                        {
                            xyz[component] = compand(xyz[component] / kD65White[component]);
                        }
                        lab[static_cast<std::uint64_t>(index)] = 116.0f * xyz[1] - 16.0f;
                        lab[pixels + static_cast<std::uint64_t>(index)] = 500.0f * (xyz[0] - xyz[1]);
                        lab[2u * pixels + static_cast<std::uint64_t>(index)] = 200.0f * (xyz[1] - xyz[2]);
                    }
                    else
                    {
                        const float luma = 0.2627f * centre[0] + 0.6780f * centre[1] + 0.0593f * centre[2];
                        lab[static_cast<std::uint64_t>(index)] = luma;
                        lab[pixels + static_cast<std::uint64_t>(index)] = (centre[2] - luma) * 0.56433f;
                        lab[2u * pixels + static_cast<std::uint64_t>(index)] = (centre[0] - luma) * 0.67815f;
                    }
                }
            }
        }
        const std::int64_t offset = directionOffset[direction & 3u];
        if (vectorOps != nullptr && vectorOps->derivative != nullptr)
        {
            vectorOps->derivative(lab, pixels, rows, columns, offset, tile.useCieLab, derivative + direction * pixels);
        }
        else
        {
            for (std::uint64_t row = 11u; row < rows - 11u; ++row)
            {
                for (std::uint64_t column = 11u; column < columns - 11u; ++column)
                {
                    const auto index = static_cast<std::int64_t>(row * columns + column);
                    const float* const lightness = lab + index;
                    const float* const first = lab + pixels + index;
                    const float* const second = lab + 2u * pixels + index;
                    const float bend = 2.0f * lightness[0] - lightness[offset] - lightness[-offset];
                    if (tile.useCieLab)
                    {
                        derivative[direction * pixels + static_cast<std::uint64_t>(index)] =
                            squared(bend) +
                            squared(2.0f * first[0] - first[offset] - first[-offset] + bend * 2.1551724f) +
                            squared(2.0f * second[0] - second[offset] - second[-offset] - bend * 0.86206896f);
                    }
                    else
                    {
                        derivative[direction * pixels + static_cast<std::uint64_t>(index)] =
                            squared(bend) + squared(2.0f * first[0] - first[offset] - first[-offset]) +
                            squared(2.0f * second[0] - second[offset] - second[-offset]);
                    }
                }
            }
        }
    }

    // How many of a pixel's neighbours agree that a direction bends least.
    for (std::uint64_t row = 12u; row < rows - 12u; ++row)
    {
        for (std::uint64_t column = 12u; column < columns - 12u; ++column)
        {
            const std::uint64_t index = row * columns + column;
            float least = derivative[index];
            for (std::uint32_t direction = 1u; direction < directions; ++direction)
            {
                least = (std::min)(least, derivative[direction * pixels + index]);
            }
            least *= 8.0f;
            for (std::uint32_t direction = 0u; direction < directions; ++direction)
            {
                std::uint8_t agreeing = 0u;
                for (std::int64_t dy = -1; dy <= 1; ++dy)
                {
                    for (std::int64_t dx = -1; dx <= 1; ++dx)
                    {
                        const std::uint64_t neighbour =
                            static_cast<std::uint64_t>(static_cast<std::int64_t>(index) + dy * tileStride + dx);
                        agreeing = static_cast<std::uint8_t>(
                            agreeing + (derivative[direction * pixels + neighbour] <= least ? 1u : 0u));
                    }
                }
                homogeneity[direction * pixels + index] = agreeing;
            }
        }
    }

    for (std::uint32_t direction = 0u; direction < directions; ++direction)
    {
        for (std::uint64_t row = kPad; row < rows - kPad; ++row)
        {
            for (std::uint64_t column = kPad; column < columns - kPad; ++column)
            {
                const std::uint64_t index = row * columns + column;
                std::uint32_t block = 0u;
                for (std::int64_t dy = -2; dy <= 2; ++dy)
                {
                    for (std::int64_t dx = -2; dx <= 2; ++dx)
                    {
                        block += homogeneity[direction * pixels +
                                             static_cast<std::uint64_t>(static_cast<std::int64_t>(index) +
                                                                        dy * tileStride + dx)];
                    }
                }
                homogeneitySum[direction * pixels + index] = static_cast<std::uint8_t>((std::min)(block, 255u));
            }
        }
    }

    for (std::uint64_t row = kPad; row < rows - kPad; ++row)
    {
        for (std::uint64_t column = kPad; column < columns - kPad; ++column)
        {
            const std::uint64_t index = row * columns + column;
            std::uint8_t best = homogeneitySum[index];
            for (std::uint32_t direction = 1u; direction < directions; ++direction)
            {
                best = (std::max)(best, homogeneitySum[direction * pixels + index]);
            }
            homogeneityBest[index] = static_cast<std::uint8_t>(best - (best >> 3));
        }
    }

    // Average the directions that bend least, and write the picture.
    for (std::uint64_t row = kPad; row < rows - kPad; ++row)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(row);
        const std::uint64_t destinationRow = static_cast<std::uint64_t>(y) * tile.destinationStride;
        for (std::uint64_t column = kPad; column < columns - kPad; ++column)
        {
            const std::uint64_t index = row * columns + column;
            std::array<std::uint8_t, 8> weight{};
            for (std::uint32_t direction = 0u; direction < 4u; ++direction)
            {
                weight[direction] = homogeneitySum[direction * pixels + index];
            }
            for (std::uint32_t direction = 4u; direction < directions; ++direction)
            {
                weight[direction] = homogeneitySum[direction * pixels + index];
                if (weight[direction - 4u] < weight[direction])
                {
                    weight[direction - 4u] = 0u;
                }
                else if (weight[direction - 4u] > weight[direction])
                {
                    weight[direction] = 0u;
                }
            }
            const std::uint8_t best = homogeneityBest[index];
            std::array<float, 3> total{};
            float count = 0.0f;
            for (std::uint32_t direction = 0u; direction < directions; ++direction)
            {
                if (weight[direction] < best)
                {
                    continue;
                }
                const float* const centre = pixelOf(direction, static_cast<std::int64_t>(index));
                total[0] += centre[0];
                total[1] += centre[1];
                total[2] += centre[2];
                count += 1.0f;
            }
            const std::uint64_t destination = destinationRow + padBeginX + column;
            const float scale = count > 0.0f ? 1.0f / count : 1.0f;
            tile.red[destination] = total[0] * scale;
            tile.green[destination] = total[1] * scale;
            tile.blue[destination] = total[2] * scale;
        }
    }
}

void demosaicMarkesteijnBody(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats,
                             std::uint32_t passes) noexcept
{
    demosaicMarkesteijnBodyWithOps(tile, scratch, scratchFloats, passes, nullptr);
}

void demosaicMarkesteijnOneReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    demosaicMarkesteijnBody(tile, scratch, scratchFloats, 1u);
}

void demosaicMarkesteijnThreeReference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
{
    demosaicMarkesteijnBody(tile, scratch, scratchFloats, 3u);
}

} // namespace an
