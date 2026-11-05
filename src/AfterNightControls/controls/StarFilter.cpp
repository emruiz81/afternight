// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/StarFilter.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <random>
#include <unordered_map>
#include <utility>

namespace an::ui
{
namespace
{

/// A star's box: two full widths times the scale from its centre along each axis, a width that is not positive taken
/// as one pixel.
struct StarBox
{
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

[[nodiscard]] StarBox boxOf(const an::DetectedStar& star, const double scaleFactor) noexcept
{
    const double width = star.psf.fullWidthX > 0.0 ? star.psf.fullWidthX : 1.0;
    const double height = star.psf.fullWidthY > 0.0 ? star.psf.fullWidthY : 1.0;
    const double halfWidth = width * 2.0 * scaleFactor;
    const double halfHeight = height * 2.0 * scaleFactor;
    return StarBox{.left = star.positionX - halfWidth,
                   .top = star.positionY - halfHeight,
                   .right = star.positionX + halfWidth,
                   .bottom = star.positionY + halfHeight};
}

/// Whether two boxes share an area: touching edges share none.
[[nodiscard]] bool overlaps(const StarBox& a, const StarBox& b) noexcept
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

/// A uniform draw below a bound from a generator whose every output is specified, by rejection rather than by a
/// standard distribution, whose algorithm each library chooses for itself.
[[nodiscard]] std::uint64_t drawBelow(std::mt19937_64& generator, const std::uint64_t bound)
{
    const std::uint64_t threshold = (std::numeric_limits<std::uint64_t>::max() - bound + 1u) % bound;
    std::uint64_t value = generator();
    while (value < threshold)
    {
        value = generator();
    }
    return value % bound;
}

/// Keeps a share of the stars still kept, chosen from the seed: a partial Fisher-Yates shuffle of their indices.
void keepRandomShare(std::vector<char>& kept, const double keepPercent, const std::uint64_t seed)
{
    std::vector<std::size_t> candidates;
    for (std::size_t index = 0u; index < kept.size(); ++index)
    {
        if (kept[index] != 0)
        {
            candidates.push_back(index);
        }
    }
    const double share = std::clamp(keepPercent, 0.0, 100.0) / 100.0;
    const auto keepCount = static_cast<std::size_t>(share * static_cast<double>(candidates.size()));
    std::mt19937_64 generator(seed);
    for (std::size_t position = 0u; position < keepCount; ++position)
    {
        const std::size_t chosen =
            position + static_cast<std::size_t>(drawBelow(generator, candidates.size() - position));
        std::swap(candidates[position], candidates[chosen]);
    }
    for (std::size_t position = keepCount; position < candidates.size(); ++position)
    {
        kept[candidates[position]] = 0;
    }
}

/// Keeps every star left out whose box overlaps a kept star's, and so on from the stars kept that way.
void keepNeighbours(std::span<const an::DetectedStar> stars, std::vector<char>& kept, const double scaleFactor)
{
    std::vector<StarBox> boxes;
    boxes.reserve(stars.size());
    std::vector<double> extents;
    extents.reserve(stars.size());
    double largest = 0.0;
    for (const an::DetectedStar& star : stars)
    {
        const StarBox box = boxOf(star, scaleFactor);
        boxes.push_back(box);
        const double extent = (std::max)(box.right - box.left, box.bottom - box.top);
        extents.push_back(extent);
        largest = (std::max)(largest, extent);
    }
    if (boxes.empty())
    {
        return;
    }
    // A cell about as wide as a typical box, and never so narrow that one box spans more than a few dozen cells.
    std::nth_element(extents.begin(), extents.begin() + static_cast<std::ptrdiff_t>(extents.size() / 2u),
                     extents.end());
    const double cell = (std::max)({1.0, extents[extents.size() / 2u], largest / 32.0});

    const auto cellRange = [cell](const double low, const double high) {
        return std::pair<std::int64_t, std::int64_t>{static_cast<std::int64_t>(std::floor(low / cell)),
                                                     static_cast<std::int64_t>(std::floor(high / cell))};
    };
    const auto key = [](const std::int64_t column, const std::int64_t row) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(column)) << 32u) |
               static_cast<std::uint64_t>(static_cast<std::uint32_t>(row));
    };
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> grid;
    for (std::size_t index = 0u; index < boxes.size(); ++index)
    {
        const auto [firstColumn, lastColumn] = cellRange(boxes[index].left, boxes[index].right);
        const auto [firstRow, lastRow] = cellRange(boxes[index].top, boxes[index].bottom);
        for (std::int64_t row = firstRow; row <= lastRow; ++row)
        {
            for (std::int64_t column = firstColumn; column <= lastColumn; ++column)
            {
                grid[key(column, row)].push_back(index);
            }
        }
    }

    std::deque<std::size_t> reached;
    for (std::size_t index = 0u; index < kept.size(); ++index)
    {
        if (kept[index] != 0)
        {
            reached.push_back(index);
        }
    }
    while (!reached.empty())
    {
        const std::size_t current = reached.front();
        reached.pop_front();
        const StarBox& box = boxes[current];
        const auto [firstColumn, lastColumn] = cellRange(box.left, box.right);
        const auto [firstRow, lastRow] = cellRange(box.top, box.bottom);
        for (std::int64_t row = firstRow; row <= lastRow; ++row)
        {
            for (std::int64_t column = firstColumn; column <= lastColumn; ++column)
            {
                const auto found = grid.find(key(column, row));
                if (found == grid.end())
                {
                    continue;
                }
                for (const std::size_t other : found->second)
                {
                    if (kept[other] == 0 && overlaps(box, boxes[other]))
                    {
                        kept[other] = 1;
                        reached.push_back(other);
                    }
                }
            }
        }
    }
}

} // namespace

std::optional<double> starFilterValue(const an::DetectedStar& star, const StarFilterField field) noexcept
{
    switch (field)
    {
    case StarFilterField::Amplitude:
        return star.psf.amplitude;
    case StarFilterField::Circularity:
        return star.metrics.circularity;
    case StarFilterField::Radius:
        return star.metrics.meanFullWidth / 2.0;
    case StarFilterField::Magnitude:
        return star.instrumentalMagnitude;
    case StarFilterField::FullWidthX:
        return star.psf.fullWidthX;
    case StarFilterField::FullWidthY:
        return star.psf.fullWidthY;
    }
    return std::nullopt;
}

std::optional<StarFilterLimits> starFilterLimits(std::span<const an::DetectedStar> stars,
                                                 const StarFilterField field) noexcept
{
    std::optional<StarFilterLimits> limits;
    for (const an::DetectedStar& star : stars)
    {
        const std::optional<double> value = starFilterValue(star, field);
        if (!value.has_value() || !std::isfinite(*value))
        {
            continue;
        }
        if (!limits.has_value())
        {
            limits = StarFilterLimits{.minimum = *value, .maximum = *value};
            continue;
        }
        limits->minimum = (std::min)(limits->minimum, *value);
        limits->maximum = (std::max)(limits->maximum, *value);
    }
    return limits;
}

bool starFilterApplies(const StarFilterRequest& request) noexcept
{
    return !request.ranges.empty() || request.keepPercent < 100.0;
}

std::vector<std::size_t> filterStars(std::span<const an::DetectedStar> stars, const StarFilterRequest& request)
{
    std::vector<char> kept(stars.size(), 1);
    for (std::size_t index = 0u; index < stars.size(); ++index)
    {
        for (const StarFilterRange& range : request.ranges)
        {
            const std::optional<double> value = starFilterValue(stars[index], range.field);
            if (!value.has_value() || !(*value >= range.lowest && *value <= range.highest))
            {
                kept[index] = 0;
                break;
            }
        }
    }
    if (request.keepPercent < 100.0)
    {
        keepRandomShare(kept, request.keepPercent, request.seed);
    }
    if (request.includeNeighbours && starFilterApplies(request))
    {
        keepNeighbours(stars, kept, request.scaleFactor);
    }

    std::vector<std::size_t> indices;
    for (std::size_t index = 0u; index < kept.size(); ++index)
    {
        if (kept[index] != 0)
        {
            indices.push_back(index);
        }
    }
    return indices;
}

StarListPtr filteredStarList(const StarListPtr& stars, const StarFilterRequest& request)
{
    if (!stars)
    {
        return std::make_shared<const StarList>();
    }
    if (!starFilterApplies(request))
    {
        return stars;
    }
    auto kept = std::make_shared<StarList>();
    const std::vector<std::size_t> indices = filterStars(*stars, request);
    kept->reserve(indices.size());
    for (const std::size_t index : indices)
    {
        kept->push_back((*stars)[index]);
    }
    return kept;
}

} // namespace an::ui
