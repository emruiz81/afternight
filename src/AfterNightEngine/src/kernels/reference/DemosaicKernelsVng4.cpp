// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/DemosaicBayerKernel.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

// Variable-number-of-gradients interpolation over a four-colour Bayer working image.
//
// Derived from the dcraw-lineage VNG4 implementation in librtprocess's copy of RawTherapee
// (`demosaic/vng4.cc`), optimised for speed by Ingo Weyrich and distributed under the GNU General
// Public License, version 3 or any later version; the file names no copyright holder. The
// four-colour first interpolation, its 64 gradient terms, the eight-direction threshold and the
// two distinct green positions are retained. The frame-sized working image and row-block fix-up
// are replaced by five bounded tile planes: four initial colour planes and one final green plane.
// Red and blue use the family's shared colour-difference reconstruction.
// The provenance note is `docs/dev/DEMOSAIC_PROVENANCE.md`.
//
// Every value needed by an output tile is rebuilt from its four-pixel source halo, so no output row
// depends on which worker received the preceding row and a task-arena partition cannot change the
// pixels.
//
// Which filter covers a Bayer site is a function of its row and column parity alone, so every
// colour question the program asks - which plane a term reads, whether a direction's neighbour
// shares the centre's filter - has one answer per parity, resolved once per tile into four phase
// tables. The gradient terms a four-colour site keeps are the 32 whose sites are an even number of
// rows and columns apart, the same at every site, so they are resolved when the engine is compiled
// and evaluated unrolled in program order, each adding to the one or two directions it names. That
// changes where a value comes from and not one arithmetic operation or the order it runs in.

namespace an
{
namespace
{

/// Full source reach, including the initial interpolation and final chrominance reconstruction.
constexpr std::uint32_t kPad = 4u;

/// Number of channels in the VNG working image: red, two greens and blue.
constexpr std::uint32_t kWorkingColors = 4u;

/// Number of directed neighbourhoods judged by the VNG threshold.
constexpr std::uint32_t kDirections = 8u;

/// Marks the second direction of a term that scores only one.
constexpr std::uint32_t kNoDirection = kDirections;

/// Six values per gradient term: two offsets, a weight exponent and a direction mask.
constexpr std::size_t kGradientTermWidth = 6u;

/// The dcraw-lineage gradient program retained by the scalar authority.
constexpr std::int16_t kGradientTerms[] = {
    -2, -2, 0,  -1, 0, 0x01, -2, -2, 0,  0,  1, 0x01, -2, -1, -1, 0,  0, 0x01, -2, -1, 0,  -1, 0, 0x02,
    -2, -1, 0,  0,  0, 0x03, -2, -1, 0,  1,  1, 0x01, -2, 0,  0,  -1, 0, 0x06, -2, 0,  0,  0,  1, 0x02,
    -2, 0,  0,  1,  0, 0x03, -2, 1,  -1, 0,  0, 0x04, -2, 1,  0,  -1, 1, 0x04, -2, 1,  0,  0,  0, 0x06,
    -2, 1,  0,  1,  0, 0x02, -2, 2,  0,  0,  1, 0x04, -2, 2,  0,  1,  0, 0x04, -1, -2, -1, 0,  0, 0x80,
    -1, -2, 0,  -1, 0, 0x01, -1, -2, 1,  -1, 0, 0x01, -1, -2, 1,  0,  1, 0x01, -1, -1, -1, 1,  0, 0x88,
    -1, -1, 1,  -2, 0, 0x40, -1, -1, 1,  -1, 0, 0x22, -1, -1, 1,  0,  0, 0x33, -1, -1, 1,  1,  1, 0x11,
    -1, 0,  -1, 2,  0, 0x08, -1, 0,  0,  -1, 0, 0x44, -1, 0,  0,  1,  0, 0x11, -1, 0,  1,  -2, 1, 0x40,
    -1, 0,  1,  -1, 0, 0x66, -1, 0,  1,  0,  1, 0x22, -1, 0,  1,  1,  0, 0x33, -1, 0,  1,  2,  1, 0x10,
    -1, 1,  1,  -1, 1, 0x44, -1, 1,  1,  0,  0, 0x66, -1, 1,  1,  1,  0, 0x22, -1, 1,  1,  2,  0, 0x10,
    -1, 2,  0,  1,  0, 0x04, -1, 2,  1,  0,  1, 0x04, -1, 2,  1,  1,  0, 0x04, 0,  -2, 0,  0,  1, 0x80,
    0,  -1, 0,  1,  1, 0x88, 0,  -1, 1,  -2, 0, 0x40, 0,  -1, 1,  0,  0, 0x11, 0,  -1, 2,  -2, 0, 0x40,
    0,  -1, 2,  -1, 0, 0x20, 0,  -1, 2,  0,  0, 0x30, 0,  -1, 2,  1,  1, 0x10, 0,  0,  0,  2,  1, 0x08,
    0,  0,  2,  -2, 1, 0x40, 0,  0,  2,  -1, 0, 0x60, 0,  0,  2,  0,  1, 0x20, 0,  0,  2,  1,  0, 0x30,
    0,  0,  2,  2,  1, 0x10, 0,  1,  1,  0,  0, 0x44, 0,  1,  1,  2,  0, 0x10, 0,  1,  2,  -1, 1, 0x40,
    0,  1,  2,  0,  0, 0x60, 0,  1,  2,  1,  0, 0x20, 0,  1,  2,  2,  0, 0x10, 1,  -2, 1,  0,  0, 0x80,
    1,  -1, 1,  1,  0, 0x88, 1,  0,  1,  2,  0, 0x08, 1,  0,  2,  -1, 0, 0x40, 1,  0,  2,  1,  0, 0x10};

/// Number of retained gradient terms.
constexpr std::size_t kGradientTermCount = sizeof(kGradientTerms) / sizeof(kGradientTerms[0]) / kGradientTermWidth;

static_assert(kGradientTermCount == 64u);

/// A gradient term a four-colour Bayer site keeps.
///
/// Each of the four working colours sits on one row parity and one column parity, so a term survives
/// the program's colour test only when its two sites are an even number of rows and of columns apart,
/// and none of those is the diagonal pair the program's second test drops. That leaves the same 32
/// terms, in program order, at every site of every phase; only the plane they read depends on the
/// phase. Each scores one or two of the eight directions.
struct KeptGradientTerm
{
    /// Row of the first site, from the centre.
    std::int32_t firstRow = 0;

    /// Column of the first site, from the centre.
    std::int32_t firstColumn = 0;

    /// Row of the second site, from the centre.
    std::int32_t secondRow = 0;

    /// Column of the second site, from the centre.
    std::int32_t secondColumn = 0;

    /// The term's power-of-two weight as a factor.
    float factor = 1.0f;

    /// The first of the directions the term scores.
    std::uint32_t firstDirection = 0u;

    /// The second direction it scores, or `kNoDirection` for a term that scores one.
    std::uint32_t secondDirection = kNoDirection;
};

/// Whether a term of the program compares two sites of one four-colour plane.
///
/// @param term the term's index in the program.
/// @return true when both its distances are even.
[[nodiscard]] constexpr bool keepsTerm(std::size_t term) noexcept
{
    const std::int16_t* const values = kGradientTerms + term * kGradientTermWidth;
    return (values[0] - values[2]) % 2 == 0 && (values[1] - values[3]) % 2 == 0;
}

/// How many terms of the program a four-colour site keeps.
///
/// @return the count.
[[nodiscard]] constexpr std::size_t keptTermCount() noexcept
{
    std::size_t count = 0u;
    for (std::size_t term = 0u; term < kGradientTermCount; ++term)
    {
        count += keepsTerm(term) ? 1u : 0u;
    }
    return count;
}

/// Number of terms a four-colour site keeps.
constexpr std::size_t kKeptTermCount = keptTermCount();

static_assert(kKeptTermCount == 32u);

/// The terms a four-colour site keeps, in program order.
///
/// @return the terms; one whose mask named no direction or more than two would leave a direction
///     unset, which the assertion below refuses.
[[nodiscard]] constexpr std::array<KeptGradientTerm, kKeptTermCount> keptTerms() noexcept
{
    std::array<KeptGradientTerm, kKeptTermCount> kept{};
    std::size_t next = 0u;
    for (std::size_t term = 0u; term < kGradientTermCount; ++term)
    {
        if (!keepsTerm(term))
        {
            continue;
        }
        const std::int16_t* const values = kGradientTerms + term * kGradientTermWidth;
        KeptGradientTerm& resolved = kept[next];
        resolved.firstRow = values[0];
        resolved.firstColumn = values[1];
        resolved.secondRow = values[2];
        resolved.secondColumn = values[3];
        resolved.factor = values[4] == 0 ? 1.0f : 2.0f;
        const auto mask = static_cast<std::uint32_t>(values[5]);
        resolved.firstDirection = kNoDirection;
        resolved.secondDirection = kNoDirection;
        if (std::popcount(mask) == 1 || std::popcount(mask) == 2)
        {
            resolved.firstDirection = static_cast<std::uint32_t>(std::countr_zero(mask));
            if (std::popcount(mask) == 2)
            {
                resolved.secondDirection = static_cast<std::uint32_t>(31 - std::countl_zero(mask));
            }
        }
        ++next;
    }
    return kept;
}

/// The terms a four-colour site keeps.
constexpr std::array<KeptGradientTerm, kKeptTermCount> kKeptTerms = keptTerms();

/// Whether every kept term names a first direction.
///
/// @return true when none was left unset.
[[nodiscard]] constexpr bool everyKeptTermScoresADirection() noexcept
{
    for (const KeptGradientTerm& term : kKeptTerms)
    {
        if (term.firstDirection >= kDirections)
        {
            return false;
        }
    }
    return true;
}

static_assert(everyKeptTermScoresADirection());

/// The eight directed neighbours in the order encoded by the gradient masks.
constexpr std::array<std::array<std::int32_t, 2u>, kDirections> kNeighbourDirections = {
    std::array<std::int32_t, 2u>{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};

/// Four-colour index at one Bayer position.
///
/// Green one is the filter on row zero of the uncropped repeating block and green two is the one
/// on row one. Calibration has already equalised their transmission, but retaining their
/// positions is part of VNG4's directional arithmetic.
///
/// @param tile the picture and phased filter array.
/// @param x the picture column.
/// @param y the picture row.
/// @return red as zero, first green as one, blue as two or second green as three.
[[nodiscard]] std::uint32_t workingColorAt(const DemosaicTile& tile, std::uint32_t x, std::uint32_t y) noexcept
{
    const CfaColor color = tile.cfa->colorAt(x, y);
    if (color == CfaColor::Red)
    {
        return 0u;
    }
    if (color == CfaColor::Blue)
    {
        return 2u;
    }
    return ((y + tile.cfa->originY()) & 1u) == 0u ? 1u : 3u;
}

/// Index displaced from one tile-plane point.
///
/// @param centre centre index.
/// @param columns floats between rows.
/// @param offsetX signed column displacement.
/// @param offsetY signed row displacement.
/// @return the displaced index, which the caller's halo guarantees is non-negative and in range.
[[nodiscard]] std::uint64_t displacedIndex(std::uint64_t centre, std::uint64_t columns, std::int32_t offsetX,
                                           std::int32_t offsetY) noexcept
{
    const std::int64_t displaced = static_cast<std::int64_t>(centre) +
                                   static_cast<std::int64_t>(offsetY) * static_cast<std::int64_t>(columns) + offsetX;
    return static_cast<std::uint64_t>(displaced);
}

/// Everything the gradient program asks about a Bayer site, answered once for its parity.
struct Vng4Phase
{
    /// The site's own four-colour index.
    std::uint32_t own = 0u;

    /// Four-colour index of each of the eight neighbours, in the mask's direction order.
    std::array<std::uint32_t, kDirections> neighbourColor{};

    /// Whether each direction's neighbour lies on a different filter, so the centre and the site
    /// two steps out are averaged into the centre sum.
    std::array<bool, kDirections> centreContributes{};

    /// Four-colour index of each three-by-three neighbour, row-major without the centre.
    std::array<std::uint32_t, 8u> ringColor{};

    /// Four-colour index of the sites an even or odd number of rows and columns from the centre,
    /// indexed by row parity times two plus column parity: the plane a kept gradient term reads.
    std::array<std::uint32_t, 4u> siteColor{};
};

/// The row or column parity of a picture coordinate under the filter array's phase.
///
/// @param coordinate the picture column or row.
/// @param origin the filter array's origin along that axis.
/// @return zero or one.
[[nodiscard]] std::uint32_t parityOf(std::int64_t coordinate, std::uint32_t origin) noexcept
{
    return static_cast<std::uint32_t>((coordinate + static_cast<std::int64_t>(origin)) & 1);
}

/// The four phase tables of one tile, resolved from the descriptor's own colour answers.
///
/// @param tile the picture and phased filter array.
/// @param phases set to the table for each (row parity, column parity) pair, row-major.
void resolvePhases(const DemosaicTile& tile, std::array<Vng4Phase, 4u>& phases) noexcept
{
    // The descriptor answers for any coordinate, so the first two rows and columns of the picture
    // are enough to read every parity's colour through the one function that defines it.
    std::array<std::array<std::uint32_t, 2u>, 2u> colorOfParity{};
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u; ++x)
        {
            colorOfParity[parityOf(y, tile.cfa->originY())][parityOf(x, tile.cfa->originX())] =
                workingColorAt(tile, x, y);
        }
    }
    const auto colorAt = [&](std::uint32_t rowParity, std::uint32_t columnParity, std::int32_t offsetX,
                             std::int32_t offsetY) {
        return colorOfParity[static_cast<std::uint32_t>((static_cast<std::int32_t>(rowParity) + offsetY) & 1)]
                            [static_cast<std::uint32_t>((static_cast<std::int32_t>(columnParity) + offsetX) & 1)];
    };
    for (std::uint32_t rowParity = 0u; rowParity < 2u; ++rowParity)
    {
        for (std::uint32_t columnParity = 0u; columnParity < 2u; ++columnParity)
        {
            Vng4Phase& phase = phases[rowParity * 2u + columnParity];
            phase.own = colorAt(rowParity, columnParity, 0, 0);
            for (std::uint32_t direction = 0u; direction < kDirections; ++direction)
            {
                const std::int32_t offsetX = kNeighbourDirections[direction][0];
                const std::int32_t offsetY = kNeighbourDirections[direction][1];
                phase.neighbourColor[direction] = colorAt(rowParity, columnParity, offsetX, offsetY);
                // Two steps out has the centre's parity and therefore the centre's filter, so the
                // original's second test is always true and only the first decides.
                phase.centreContributes[direction] = phase.neighbourColor[direction] != phase.own;
            }
            std::uint32_t ring = 0u;
            for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    if (offsetX == 0 && offsetY == 0)
                    {
                        continue;
                    }
                    phase.ringColor[ring] = colorAt(rowParity, columnParity, offsetX, offsetY);
                    ++ring;
                }
            }
            for (std::uint32_t rowStep = 0u; rowStep < 2u; ++rowStep)
            {
                for (std::uint32_t columnStep = 0u; columnStep < 2u; ++columnStep)
                {
                    phase.siteColor[rowStep * 2u + columnStep] =
                        colorAt(rowParity, columnParity, static_cast<std::int32_t>(columnStep),
                                static_cast<std::int32_t>(rowStep));
                }
            }
        }
    }
}

/// One value from the planar four-colour working image.
///
/// @param colors first working plane.
/// @param plane floats in one plane.
/// @param color four-colour index.
/// @param index point in that plane.
/// @return the interpolated value.
[[nodiscard]] float workingValue(const float* colors, std::uint64_t plane, std::uint32_t color,
                                 std::uint64_t index) noexcept
{
    return colors[static_cast<std::uint64_t>(color) * plane + index];
}

/// Adds one kept gradient term to the direction scores.
///
/// @tparam Term the term's index among the kept terms.
/// @param sites the centre in the plane each parity of offset reads, indexed as `Vng4Phase::siteColor`.
/// @param columns floats between rows.
/// @param gradients the eight scores.
template <std::size_t Term>
void addKeptTerm(const std::array<const float*, 4u>& sites, std::int64_t columns,
                 std::array<float, kDirections>& gradients) noexcept
{
    constexpr KeptGradientTerm kTerm = kKeptTerms[Term];
    // Both sites have the first one's parity, so both are read from its plane.
    const float* const plane = sites[static_cast<std::size_t>(((kTerm.firstRow & 1) << 1) | (kTerm.firstColumn & 1))];
    const float difference = std::fabs(plane[kTerm.firstRow * columns + kTerm.firstColumn] -
                                       plane[kTerm.secondRow * columns + kTerm.secondColumn]) *
                             kTerm.factor;
    gradients[kTerm.firstDirection] += difference;
    if constexpr (kTerm.secondDirection != kNoDirection)
    {
        gradients[kTerm.secondDirection] += difference;
    }
}

/// Accumulates the kept gradient terms into eight direction scores.
///
/// The terms run in program order and each adds its difference to the one or two directions it
/// names, so every score is summed term by term exactly as the unresolved program sums it.
///
/// @tparam Terms every kept term's index, in order.
/// @param sites the centre in the plane each parity of offset reads.
/// @param columns floats between rows.
/// @param gradients set to the eight scores.
template <std::size_t... Terms>
void accumulateKeptTerms(const std::array<const float*, 4u>& sites, std::int64_t columns,
                         std::array<float, kDirections>& gradients, std::index_sequence<Terms...>) noexcept
{
    (addKeptTerm<Terms>(sites, columns, gradients), ...);
}

/// The eight direction scores of one site.
///
/// @param phase the centre's resolved parity.
/// @param colors four interpolated working planes.
/// @param plane floats in one plane.
/// @param columns floats between rows.
/// @param index centre point in each plane.
/// @param gradients set to the eight scores.
void calculateGradients(const Vng4Phase& phase, const float* colors, std::uint64_t plane, std::uint64_t columns,
                        std::uint64_t index, std::array<float, kDirections>& gradients) noexcept
{
    const std::array<const float*, 4u> sites = {
        colors + phase.siteColor[0] * plane + index, colors + phase.siteColor[1] * plane + index,
        colors + phase.siteColor[2] * plane + index, colors + phase.siteColor[3] * plane + index};
    accumulateKeptTerms(sites, static_cast<std::int64_t>(columns), gradients,
                        std::make_index_sequence<kKeptTermCount>{});
}

} // namespace

void demosaicVng4Reference(const DemosaicTile& tile, float* scratch, std::uint64_t scratchFloats) noexcept
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
    const std::uint64_t needed = (kWorkingColors + 1u) * plane;
    if (scratch == nullptr || scratchFloats < needed)
    {
        return;
    }
    std::memset(scratch, 0, static_cast<std::size_t>(needed) * sizeof(float));

    float* const colors = scratch;
    float* const green = colors + kWorkingColors * plane;
    const std::uint32_t padBeginX = tile.xBegin - kPad;
    const std::uint32_t padBeginY = tile.yBegin - kPad;
    std::array<Vng4Phase, 4u> phases{};
    resolvePhases(tile, phases);
    const auto phaseAt = [&](std::uint32_t x, std::uint32_t y) -> const Vng4Phase& {
        return phases[parityOf(y, tile.cfa->originY()) * 2u + parityOf(x, tile.cfa->originX())];
    };

    for (std::uint64_t localY = 0u; localY < rows; ++localY)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
        for (std::uint64_t localX = 0u; localX < columns; ++localX)
        {
            const std::uint32_t x = padBeginX + static_cast<std::uint32_t>(localX);
            const std::uint64_t index = localY * columns + localX;
            const std::uint32_t color = phaseAt(x, y).own;
            colors[static_cast<std::uint64_t>(color) * plane + index] = demosaic_bayer_detail::sourceAt(tile, x, y);
        }
    }

    // First interpolate all three missing entries of the four-colour working image. Edge-adjacent
    // samples weigh two and diagonal samples one, exactly as in the original's generated table.
    for (std::uint64_t localY = 1u; localY + 1u < rows; ++localY)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
        for (std::uint64_t localX = 1u; localX + 1u < columns; ++localX)
        {
            const std::uint32_t x = padBeginX + static_cast<std::uint32_t>(localX);
            const std::uint64_t index = localY * columns + localX;
            const Vng4Phase& phase = phaseAt(x, y);
            const std::uint32_t own = phase.own;
            float sums[kWorkingColors] = {};
            float weights[kWorkingColors] = {};
            std::uint32_t ring = 0u;
            for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    if (offsetX == 0 && offsetY == 0)
                    {
                        continue;
                    }
                    const std::uint32_t neighbourX = static_cast<std::uint32_t>(static_cast<std::int64_t>(x) + offsetX);
                    const std::uint32_t neighbourY = static_cast<std::uint32_t>(static_cast<std::int64_t>(y) + offsetY);
                    const std::uint32_t color = phase.ringColor[ring];
                    ++ring;
                    const float weight = offsetX == 0 || offsetY == 0 ? 2.0f : 1.0f;
                    sums[color] += demosaic_bayer_detail::sourceAt(tile, neighbourX, neighbourY) * weight;
                    weights[color] += weight;
                }
            }
            for (std::uint32_t color = 0u; color < kWorkingColors; ++color)
            {
                if (color != own)
                {
                    colors[static_cast<std::uint64_t>(color) * plane + index] = sums[color] / weights[color];
                }
            }
        }
    }

    for (std::uint64_t localY = 3u; localY + 3u < rows; ++localY)
    {
        const std::uint32_t y = padBeginY + static_cast<std::uint32_t>(localY);
        for (std::uint64_t localX = 3u; localX + 3u < columns; ++localX)
        {
            const std::uint32_t x = padBeginX + static_cast<std::uint32_t>(localX);
            const std::uint64_t index = localY * columns + localX;
            const Vng4Phase& phase = phaseAt(x, y);
            std::array<float, kDirections> gradients{};
            calculateGradients(phase, colors, plane, columns, index, gradients);
            const auto limits = std::minmax_element(gradients.begin(), gradients.end());
            const float threshold = *limits.first + 0.5f * *limits.second;

            const std::uint32_t own = phase.own;
            const float measured = workingValue(colors, plane, own, index);
            float centreSum = 0.0f;
            float neighbourSum = 0.0f;
            std::uint32_t count = 0u;
            for (std::uint32_t direction = 0u; direction < kDirections; ++direction)
            {
                if (gradients[direction] > threshold)
                {
                    continue;
                }
                const std::int32_t offsetX = kNeighbourDirections[direction][0];
                const std::int32_t offsetY = kNeighbourDirections[direction][1];
                const std::uint64_t neighbour = displacedIndex(index, columns, offsetX, offsetY);
                const std::uint64_t distant = displacedIndex(index, columns, 2 * offsetX, 2 * offsetY);
                if (phase.centreContributes[direction])
                {
                    centreSum += measured + workingValue(colors, plane, own, distant);
                }
                if ((own & 1u) != 0u)
                {
                    neighbourSum += workingValue(colors, plane, own ^ 2u, neighbour);
                }
                else
                {
                    neighbourSum +=
                        workingValue(colors, plane, 1u, neighbour) + workingValue(colors, plane, 3u, neighbour);
                }
                ++count;
            }
            const float comparedCentre = (own & 1u) != 0u ? 0.5f * centreSum : centreSum;
            green[index] = count == 0u
                               ? measured
                               : measured + (neighbourSum - comparedCentre) / (2.0f * static_cast<float>(count));
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
