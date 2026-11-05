// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "DemosaicOracle.h"

#include "Digest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace an::bench
{
namespace
{

/// The four-colour index of VNG4's working image: red, the green on red's rows, blue, the green on blue's rows.
using WorkingColours = std::array<std::array<std::uint32_t, 2u>, 2u>;

/// dcraw's gradient program: per term the first site's row and column offsets, the second site's, the
/// weight as a shift and the directions it scores.
constexpr std::int32_t kVng4Terms[64][6] = {
    {-2, -2, +0, -1, 0, 0x01}, {-2, -2, +0, +0, 1, 0x01}, {-2, -1, -1, +0, 0, 0x01}, {-2, -1, +0, -1, 0, 0x02},
    {-2, -1, +0, +0, 0, 0x03}, {-2, -1, +0, +1, 1, 0x01}, {-2, +0, +0, -1, 0, 0x06}, {-2, +0, +0, +0, 1, 0x02},
    {-2, +0, +0, +1, 0, 0x03}, {-2, +1, -1, +0, 0, 0x04}, {-2, +1, +0, -1, 1, 0x04}, {-2, +1, +0, +0, 0, 0x06},
    {-2, +1, +0, +1, 0, 0x02}, {-2, +2, +0, +0, 1, 0x04}, {-2, +2, +0, +1, 0, 0x04}, {-1, -2, -1, +0, 0, 0x80},
    {-1, -2, +0, -1, 0, 0x01}, {-1, -2, +1, -1, 0, 0x01}, {-1, -2, +1, +0, 1, 0x01}, {-1, -1, -1, +1, 0, 0x88},
    {-1, -1, +1, -2, 0, 0x40}, {-1, -1, +1, -1, 0, 0x22}, {-1, -1, +1, +0, 0, 0x33}, {-1, -1, +1, +1, 1, 0x11},
    {-1, +0, -1, +2, 0, 0x08}, {-1, +0, +0, -1, 0, 0x44}, {-1, +0, +0, +1, 0, 0x11}, {-1, +0, +1, -2, 1, 0x40},
    {-1, +0, +1, -1, 0, 0x66}, {-1, +0, +1, +0, 1, 0x22}, {-1, +0, +1, +1, 0, 0x33}, {-1, +0, +1, +2, 1, 0x10},
    {-1, +1, +1, -1, 1, 0x44}, {-1, +1, +1, +0, 0, 0x66}, {-1, +1, +1, +1, 0, 0x22}, {-1, +1, +1, +2, 0, 0x10},
    {-1, +2, +0, +1, 0, 0x04}, {-1, +2, +1, +0, 1, 0x04}, {-1, +2, +1, +1, 0, 0x04}, {+0, -2, +0, +0, 1, 0x80},
    {+0, -1, +0, +1, 1, 0x88}, {+0, -1, +1, -2, 0, 0x40}, {+0, -1, +1, +0, 0, 0x11}, {+0, -1, +2, -2, 0, 0x40},
    {+0, -1, +2, -1, 0, 0x20}, {+0, -1, +2, +0, 0, 0x30}, {+0, -1, +2, +1, 1, 0x10}, {+0, +0, +0, +2, 1, 0x08},
    {+0, +0, +2, -2, 1, 0x40}, {+0, +0, +2, -1, 0, 0x60}, {+0, +0, +2, +0, 1, 0x20}, {+0, +0, +2, +1, 0, 0x30},
    {+0, +0, +2, +2, 1, 0x10}, {+0, +1, +1, +0, 0, 0x44}, {+0, +1, +1, +2, 0, 0x10}, {+0, +1, +2, -1, 1, 0x40},
    {+0, +1, +2, +0, 0, 0x60}, {+0, +1, +2, +1, 0, 0x20}, {+0, +1, +2, +2, 0, 0x10}, {+1, -2, +1, +0, 0, 0x80},
    {+1, -1, +1, +1, 0, 0x88}, {+1, +0, +1, +2, 0, 0x08}, {+1, +0, +2, -1, 0, 0x40}, {+1, +0, +2, +1, 0, 0x10}};

/// The eight directions, as row and column steps, in the order the gradient program's bits name them.
constexpr std::int32_t kVng4Directions[8][2] = {{-1, -1}, {-1, +0}, {-1, +1}, {+0, +1},
                                                {+1, +1}, {+1, +0}, {+1, -1}, {+0, -1}};

/// Output rows the definition evaluates at once; its working image covers them and a halo of three.
constexpr std::uint32_t kVng4BandRows = 128u;

/// One gradient term that applies at a site of one parity.
struct Vng4Term
{
    std::uint32_t colour = 0u;
    std::int32_t firstRow = 0;
    std::int32_t firstColumn = 0;
    std::int32_t secondRow = 0;
    std::int32_t secondColumn = 0;
    double factor = 1.0;
    std::uint32_t directions = 0u;
};

/// The four-colour index at each parity of a Bayer block, or nothing for a block that is not one.
std::optional<WorkingColours> workingColours(const MosaicBlock& block)
{
    if (block.size != 2u || block.entries.size() < 4u)
    {
        return std::nullopt;
    }
    std::array<std::uint32_t, 3u> counts{};
    std::uint32_t redRow = 2u;
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u; ++x)
        {
            const std::uint8_t colour = block.colourAt(x, y);
            if (colour > 2u)
            {
                return std::nullopt;
            }
            ++counts[colour];
            if (colour == 0u)
            {
                redRow = y;
            }
        }
    }
    if (counts[0] != 1u || counts[1] != 2u || counts[2] != 1u)
    {
        return std::nullopt;
    }
    WorkingColours colours{};
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u; ++x)
        {
            const std::uint8_t colour = block.colourAt(x, y);
            colours[y][x] = colour == 0u ? 0u : (colour == 2u ? 2u : (y == redRow ? 1u : 3u));
        }
    }
    return colours;
}

/// The four-colour index of a site displaced from a picture position.
std::uint32_t colourNear(const WorkingColours& colours, std::int64_t row, std::int64_t column)
{
    return colours[static_cast<std::size_t>(row & 1)][static_cast<std::size_t>(column & 1)];
}

/// The value one held to the unit interval.
double unitInterval(double value)
{
    return (std::min)((std::max)(value, 0.0), 1.0);
}

/// Evaluates VNG4's definition over the interior of a Bayer picture, a band of rows at a time, and hands
/// each interior pixel's three expected values and whether each is decided to `visit`.
///
/// @param mosaic the samples the method evaluated.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param colour the four-colour index at each parity.
/// @param border how many pixels of the edge to leave out; at least four, and less than half of each side.
/// @param visit called as (row, column, four-colour index, expected red-green-blue, decided per colour, own
///     choice tied) for every interior pixel, row by row.
template <typename Visit>
void evaluateVng4(std::span<const float> mosaic, std::uint32_t width, std::uint32_t height,
                  const WorkingColours& colour, std::uint32_t border, Visit&& visit)
{

    // The terms that apply at each parity, in program order: both sites on one colour, and not a
    // diagonal pair of the colour that also sits beside the centre in both axes.
    std::array<std::array<std::vector<Vng4Term>, 2u>, 2u> terms;
    for (std::int64_t py = 0; py < 2; ++py)
    {
        for (std::int64_t px = 0; px < 2; ++px)
        {
            for (const auto& term : kVng4Terms)
            {
                const std::uint32_t termColour = colourNear(colour, py + term[0], px + term[1]);
                if (colourNear(colour, py + term[2], px + term[3]) != termColour)
                {
                    continue;
                }
                const std::int32_t diagonal =
                    colourNear(colour, py, px + 1) == termColour && colourNear(colour, py + 1, px) == termColour ? 2
                                                                                                                 : 1;
                if (std::abs(term[0] - term[2]) == diagonal && std::abs(term[1] - term[3]) == diagonal)
                {
                    continue;
                }
                terms[static_cast<std::size_t>(py)][static_cast<std::size_t>(px)].push_back(
                    Vng4Term{termColour, term[0], term[1], term[2], term[3], term[4] == 0 ? 1.0 : 2.0,
                             static_cast<std::uint32_t>(term[5])});
            }
        }
    }

    const auto sample = [&](std::int64_t row, std::int64_t column) {
        return static_cast<double>(mosaic[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(column)]);
    };

    const std::uint32_t last = height - border;
    for (std::uint32_t bandBegin = border; bandBegin < last; bandBegin += kVng4BandRows)
    {
        const std::uint32_t bandEnd = (std::min)(bandBegin + kVng4BandRows, last);
        // The working image covers the band and three rows each side; green covers one row each side.
        const std::int64_t workBegin = static_cast<std::int64_t>(bandBegin) - 3;
        const std::size_t workRows = static_cast<std::size_t>(bandEnd - bandBegin) + 6u;
        std::vector<double> work(4u * workRows * width, 0.0);
        const auto workAt = [&](std::uint32_t c, std::int64_t row, std::int64_t column) -> double& {
            return work[(static_cast<std::size_t>(c) * workRows + static_cast<std::size_t>(row - workBegin)) * width +
                        static_cast<std::size_t>(column)];
        };
        for (std::int64_t row = workBegin; row < workBegin + static_cast<std::int64_t>(workRows); ++row)
        {
            for (std::int64_t column = 1; column + 1 < static_cast<std::int64_t>(width); ++column)
            {
                const std::uint32_t own = colourNear(colour, row, column);
                std::array<double, 4u> sums{};
                std::array<double, 4u> weights{};
                for (std::int64_t dy = -1; dy <= 1; ++dy)
                {
                    for (std::int64_t dx = -1; dx <= 1; ++dx)
                    {
                        if (dy == 0 && dx == 0)
                        {
                            continue;
                        }
                        const std::uint32_t c = colourNear(colour, row + dy, column + dx);
                        const double weight = dy == 0 || dx == 0 ? 2.0 : 1.0;
                        sums[c] += weight * sample(row + dy, column + dx);
                        weights[c] += weight;
                    }
                }
                for (std::uint32_t c = 0u; c < 4u; ++c)
                {
                    workAt(c, row, column) = c == own ? sample(row, column) : sums[c] / weights[c];
                }
            }
        }

        const std::int64_t greenBegin = static_cast<std::int64_t>(bandBegin) - 1;
        const std::size_t greenRows = static_cast<std::size_t>(bandEnd - bandBegin) + 2u;
        std::vector<double> referenceGreen(greenRows * width, 0.0);
        std::vector<std::uint8_t> tied(greenRows * width, 0u);
        for (std::int64_t row = greenBegin; row < greenBegin + static_cast<std::int64_t>(greenRows); ++row)
        {
            for (std::int64_t column = static_cast<std::int64_t>(border) - 1;
                 column <= static_cast<std::int64_t>(width - border); ++column)
            {
                const std::uint32_t own = colourNear(colour, row, column);
                std::array<double, 8u> gradients{};
                for (const Vng4Term& term :
                     terms[static_cast<std::size_t>(row & 1)][static_cast<std::size_t>(column & 1)])
                {
                    const double difference =
                        std::fabs(workAt(term.colour, row + term.firstRow, column + term.firstColumn) -
                                  workAt(term.colour, row + term.secondRow, column + term.secondColumn)) *
                        term.factor;
                    for (std::uint32_t direction = 0u; direction < 8u; ++direction)
                    {
                        if ((term.directions & (1u << direction)) != 0u)
                        {
                            gradients[direction] += difference;
                        }
                    }
                }
                const auto [smallest, largest] = std::minmax_element(gradients.begin(), gradients.end());
                const double threshold = *smallest + 0.5 * *largest;
                bool tie = false;
                for (const double gradient : gradients)
                {
                    tie = tie || (*largest > 0.0 && std::fabs(gradient - threshold) <= kVng4TieMargin * *largest);
                }
                // dcraw's four estimates: a direction whose neighbour is another colour and whose site two
                // steps out is the centre's averages those two for the centre's own colour.
                const double centre = sample(row, column);
                std::array<double, 4u> sums{};
                std::uint32_t chosen = 0u;
                for (std::uint32_t direction = 0u; direction < 8u; ++direction)
                {
                    if (gradients[direction] > threshold)
                    {
                        continue;
                    }
                    const std::int64_t ny = row + kVng4Directions[direction][0];
                    const std::int64_t nx = column + kVng4Directions[direction][1];
                    const std::int64_t fy = row + 2 * kVng4Directions[direction][0];
                    const std::int64_t fx = column + 2 * kVng4Directions[direction][1];
                    const bool averaged = colourNear(colour, ny, nx) != own && colourNear(colour, fy, fx) == own;
                    for (std::uint32_t c = 0u; c < 4u; ++c)
                    {
                        sums[c] += c == own && averaged ? 0.5 * (centre + workAt(own, fy, fx)) : workAt(c, ny, nx);
                    }
                    ++chosen;
                }
                std::array<double, 4u> estimates{};
                for (std::uint32_t c = 0u; c < 4u; ++c)
                {
                    estimates[c] = c == own ? centre : centre + (sums[c] - sums[own]) / static_cast<double>(chosen);
                }
                const std::size_t index =
                    static_cast<std::size_t>(row - greenBegin) * width + static_cast<std::size_t>(column);
                // The two green estimates are averaged, so a green site reports a mix of both phases.
                referenceGreen[index] = 0.5 * (estimates[1] + estimates[3]);
                tied[index] = tie ? 1u : 0u;
            }
        }

        const auto greenAt = [&](std::int64_t row, std::int64_t column) {
            return referenceGreen[static_cast<std::size_t>(row - greenBegin) * width +
                                  static_cast<std::size_t>(column)];
        };
        const auto tiedAt = [&](std::int64_t row, std::int64_t column) {
            return tied[static_cast<std::size_t>(row - greenBegin) * width + static_cast<std::size_t>(column)] != 0u;
        };
        for (std::int64_t row = bandBegin; row < static_cast<std::int64_t>(bandEnd); ++row)
        {
            for (std::int64_t column = border; column < static_cast<std::int64_t>(width - border); ++column)
            {
                const std::uint32_t own = colourNear(colour, row, column);
                const double g = greenAt(row, column);
                const auto difference = [&](std::int64_t dy, std::int64_t dx) {
                    return sample(row + dy, column + dx) - greenAt(row + dy, column + dx);
                };
                double r = 0.0;
                double b = 0.0;
                bool neighbourhoodTied = false;
                for (std::int64_t dy = -1; dy <= 1; ++dy)
                {
                    for (std::int64_t dx = -1; dx <= 1; ++dx)
                    {
                        neighbourhoodTied = neighbourhoodTied || tiedAt(row + dy, column + dx);
                    }
                }
                if (own == 1u || own == 3u)
                {
                    const double horizontal = g + 0.5 * (difference(0, -1) + difference(0, 1));
                    const double vertical = g + 0.5 * (difference(-1, 0) + difference(1, 0));
                    const bool redHorizontal = colourNear(colour, row, column + 1) == 0u;
                    r = redHorizontal ? horizontal : vertical;
                    b = redHorizontal ? vertical : horizontal;
                }
                else
                {
                    const double opposite =
                        g + 0.25 * (difference(-1, -1) + difference(-1, 1) + difference(1, -1) + difference(1, 1));
                    r = own == 0u ? sample(row, column) : opposite;
                    b = own == 2u ? sample(row, column) : opposite;
                }
                const bool ownTied = tiedAt(row, column);
                // The colour a red or blue site measured is copied and decided whatever any choice was.
                visit(static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column), own,
                      std::array<double, 3u>{r, g, b},
                      std::array<bool, 3u>{own == 0u || !neighbourhoodTied, !ownTied, own == 2u || !neighbourhoodTied},
                      ownTied);
            }
        }
    }
}

/// The four-colour indices of a picture the definition can judge, or nothing.
std::optional<WorkingColours> judgeable(std::span<const float> mosaic, std::uint32_t width, std::uint32_t height,
                                        const MosaicBlock& block, std::uint32_t border)
{
    const std::size_t total = static_cast<std::size_t>(width) * height;
    if (border < 4u || width <= 2u * border || height <= 2u * border || mosaic.size() < total)
    {
        return std::nullopt;
    }
    return workingColours(block);
}

} // namespace

KeptMeasurements keptMeasurements(std::string_view method) noexcept
{
    return method == "Vng4" ? KeptMeasurements::RedAndBlue : KeptMeasurements::Every;
}

std::uint8_t MosaicBlock::colourAt(std::uint32_t x, std::uint32_t y) const noexcept
{
    if (size == 0u || entries.size() < static_cast<std::size_t>(size) * size)
    {
        return 0u;
    }
    const std::size_t row = static_cast<std::size_t>((y + originY) % size);
    const std::size_t column = static_cast<std::size_t>((x + originX) % size);
    return entries[row * size + column];
}

double demosaicResidual(std::span<const float> red, std::span<const float> green, std::span<const float> blue,
                        std::span<const float> mosaic, std::uint32_t width, std::uint32_t height,
                        const MosaicBlock& block, std::uint32_t border, KeptMeasurements kept)
{
    const std::size_t total = static_cast<std::size_t>(width) * height;
    if (red.size() < total || green.size() < total || blue.size() < total || mosaic.size() < total ||
        width <= 2u * border || height <= 2u * border)
    {
        return 0.0;
    }
    double sum = 0.0;
    std::size_t counted = 0u;
    for (std::uint32_t y = border; y < height - border; ++y)
    {
        for (std::uint32_t x = border; x < width - border; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const std::uint8_t colour = block.colourAt(x, y);
            if (kept == KeptMeasurements::RedAndBlue && colour == 1u)
            {
                continue;
            }
            const float produced = colour == 0u ? red[index] : (colour == 1u ? green[index] : blue[index]);
            const double difference = static_cast<double>(produced) - static_cast<double>(mosaic[index]);
            sum += difference * difference;
            ++counted;
        }
    }
    return counted == 0u ? 0.0 : std::sqrt(sum / static_cast<double>(counted));
}

std::optional<Vng4Definition> vng4Definition(std::span<const float> mosaic, std::uint32_t width, std::uint32_t height,
                                             const MosaicBlock& block, std::uint32_t border)
{
    const std::optional<WorkingColours> colours = judgeable(mosaic, width, height, block, border);
    if (!colours.has_value())
    {
        return std::nullopt;
    }
    const std::size_t total = static_cast<std::size_t>(width) * height;
    Vng4Definition definition{std::vector<double>(total, 0.0), std::vector<double>(total, 0.0),
                              std::vector<double>(total, 0.0), std::vector<std::uint8_t>(total, 0u)};
    evaluateVng4(mosaic, width, height, *colours, border,
                 [&](std::uint32_t row, std::uint32_t column, std::uint32_t, const std::array<double, 3u>& expected,
                     const std::array<bool, 3u>& decided, bool) {
                     const std::size_t index = static_cast<std::size_t>(row) * width + column;
                     definition.red[index] = expected[0];
                     definition.green[index] = expected[1];
                     definition.blue[index] = expected[2];
                     definition.decided[index] = static_cast<std::uint8_t>(
                         (decided[0] ? 1u : 0u) | (decided[1] ? 2u : 0u) | (decided[2] ? 4u : 0u));
                 });
    return definition;
}

std::optional<Vng4DefinitionEvidence> vng4DefinitionEvidence(std::span<const float> red, std::span<const float> green,
                                                             std::span<const float> blue, std::span<const float> mosaic,
                                                             std::uint32_t width, std::uint32_t height,
                                                             const MosaicBlock& block, std::uint32_t border)
{
    const std::optional<WorkingColours> colours = judgeable(mosaic, width, height, block, border);
    const std::size_t total = static_cast<std::size_t>(width) * height;
    if (!colours.has_value() || red.size() < total || green.size() < total || blue.size() < total)
    {
        return std::nullopt;
    }
    Vng4DefinitionEvidence evidence;
    double adjustment = 0.0;
    std::uint64_t greenSites = 0u;
    evaluateVng4(mosaic, width, height, *colours, border,
                 [&](std::uint32_t row, std::uint32_t column, std::uint32_t own, const std::array<double, 3u>& expected,
                     const std::array<bool, 3u>& decided, bool ownTied) {
                     const std::size_t index = static_cast<std::size_t>(row) * width + column;
                     const std::array<double, 3u> produced = {static_cast<double>(red[index]),
                                                              static_cast<double>(green[index]),
                                                              static_cast<double>(blue[index])};
                     for (std::size_t channel = 0u; channel < 3u; ++channel)
                     {
                         ++evidence.interiorSamples;
                         if (!decided[channel])
                         {
                             continue;
                         }
                         ++evidence.decidedSamples;
                         evidence.largestDecidedError =
                             (std::max)(evidence.largestDecidedError,
                                        std::fabs(unitInterval(produced[channel]) - unitInterval(expected[channel])));
                     }
                     evidence.tiedPixels += ownTied ? 1u : 0u;
                     if (own == 1u || own == 3u)
                     {
                         const double moved = produced[1] - static_cast<double>(mosaic[index]);
                         adjustment += moved * moved;
                         ++greenSites;
                     }
                 });
    evidence.greenSiteAdjustmentRms = greenSites == 0u ? 0.0 : std::sqrt(adjustment / static_cast<double>(greenSites));
    return evidence;
}

void recordDemosaicEvidence(const ScenarioSpec& scenario, std::span<const float> red, std::span<const float> green,
                            std::span<const float> blue, std::span<const float> mosaic, std::uint32_t width,
                            std::uint32_t height, const MosaicBlock& block, ResultRecord& out,
                            std::span<const float> evaluated)
{
    const auto method = scenario.parameters.find("method");
    const KeptMeasurements kept =
        method == scenario.parameters.end() ? KeptMeasurements::Every : keptMeasurements(method->second);
    out.correctnessMetric =
        demosaicResidual(red, green, blue, mosaic, width, height, block, kDemosaicResidualBorder, kept);
    const auto oracle = scenario.parameters.find(kDemosaicQualityOracleParameter);
    if (oracle == scenario.parameters.end() || oracle->second != kVng4DefinitionOracle)
    {
        return;
    }
    const std::optional<Vng4DefinitionEvidence> evidence = vng4DefinitionEvidence(
        red, green, blue, evaluated.empty() ? mosaic : evaluated, width, height, block, kDemosaicResidualBorder);
    if (!evidence.has_value())
    {
        return;
    }
    out.counters["vng4LargestDecidedError"] = evidence->largestDecidedError;
    out.counters["vng4DecidedSamples"] = static_cast<double>(evidence->decidedSamples);
    out.counters["vng4InteriorSamples"] = static_cast<double>(evidence->interiorSamples);
    out.counters["vng4TiedPixels"] = static_cast<double>(evidence->tiedPixels);
    out.counters["vng4GreenSiteAdjustmentRms"] = evidence->greenSiteAdjustmentRms;
}

bool requiresDemosaicEvidence(const ScenarioSpec& scenario)
{
    return scenario.group == "demosaic" && scenario.parameters.contains("method");
}

bool demosaicEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& record)
{
    if (!record.correctnessMetric.has_value() || !std::isfinite(*record.correctnessMetric) ||
        *record.correctnessMetric > scenario.tolerance)
    {
        return false;
    }
    const auto oracle = scenario.parameters.find(kDemosaicQualityOracleParameter);
    if (oracle == scenario.parameters.end())
    {
        return true;
    }
    if (oracle->second != kVng4DefinitionOracle)
    {
        return false;
    }
    const auto counter = [&](const char* name) -> std::optional<double> {
        const auto found = record.counters.find(name);
        return found == record.counters.end() ? std::nullopt : std::optional<double>(found->second);
    };
    const std::optional<double> largest = counter("vng4LargestDecidedError");
    const std::optional<double> decided = counter("vng4DecidedSamples");
    const std::optional<double> interior = counter("vng4InteriorSamples");
    return largest.has_value() && decided.has_value() && interior.has_value() && std::isfinite(*largest) &&
           *largest <= kVng4DecidedTolerance && *interior > 0.0 && 2.0 * *decided >= *interior;
}

std::string demosaicProbeText(std::span<const float> red, std::span<const float> green, std::span<const float> blue,
                              std::uint32_t width, std::uint32_t height, std::uint32_t stride)
{
    const std::size_t total = static_cast<std::size_t>(width) * height;
    if (red.size() < total || green.size() < total || blue.size() < total || stride == 0u)
    {
        return {};
    }
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(9) << std::scientific;
    for (std::size_t index = 0u; index < total; index += stride)
    {
        text << index << ' ' << red[index] << ' ' << green[index] << ' ' << blue[index] << '\n';
    }
    return text.str();
}

std::string demosaicExactDigest(std::span<const float> red, std::span<const float> green, std::span<const float> blue)
{
    if (red.size() != green.size() || red.size() != blue.size())
    {
        return {};
    }
    Sha256 digest;
    digest.update(red.data(), red.size_bytes());
    digest.update(green.data(), green.size_bytes());
    digest.update(blue.data(), blue.size_bytes());
    return digest.hex();
}

} // namespace an::bench
