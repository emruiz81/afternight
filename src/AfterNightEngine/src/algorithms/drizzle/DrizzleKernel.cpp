// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzle kernels' arithmetic: the Lanczos table, the square kernel's
// two independent overlap calculations, a drop's support, and the per-cell coefficients.
//
// The Lanczos table is the windowed sinc evaluated in Float32, with a 0.01 spacing and nearest-entry
// rounding. The two overlap calculations are independent: the reference path clips the footprint
// against the unit cell's four edges and measures the polygon, and the optimized path integrates the
// area under each footprint edge over the cell, so each overlap is a test of the other. Both work relative to
// the destination cell, and the area check relative to the footprint's first corner, so large
// coordinates do not swamp the half-pixel edges.
//
// A drop's support is clipped to the canvas before any cell is visited. A drop that reaches no cell,
// or a square drop whose corners are missing or enclose no area, contributes nothing rather than
// failing the band.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>

#include "algorithms/drizzle/DrizzleAccumulationInternal.h"
#include "algorithms/drizzle/DrizzleGeometryInternal.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace an
{

Result<DrizzleKernelTable> DrizzleKernelTable::create(DrizzleKernel kernel)
{
    if (kernel != DrizzleKernel::Lanczos2 && kernel != DrizzleKernel::Lanczos3)
        return drizzle_detail::accumulationFailure(ErrorCode::InvalidArgument,
                                                   "a Lanczos table requires order two or three");
    DrizzleKernelTable table(kernel);
    const float order = kernel == DrizzleKernel::Lanczos2 ? 2.0f : 3.0f;
    for (std::size_t i = 0u; i < table.m_values.size(); ++i)
    {
        const float x = static_cast<float>(i) * 0.01f;
        const float argument = std::numbers::pi_v<float> * x;
        const float window = argument / order;
        table.m_values[i] =
            x >= order ? 0.0f : (i == 0u ? 1.0f : (std::sin(argument) / argument) * (std::sin(window) / window));
    }
    return table;
}

Result<float> DrizzleKernelTable::lookup(float distance) const
{
    if (!std::isfinite(distance))
        return drizzle_detail::accumulationFailure(ErrorCode::InvalidArgument, "Lanczos distance must be finite");
    distance = std::abs(distance);
    if (distance >= 5.11f)
        return 0.0f;
    return m_values[static_cast<std::size_t>(distance / 0.01f + 0.5f)];
}

Result<double> drizzleSquareOverlap(const DrizzleFootprint& footprint, std::uint32_t x, std::uint32_t y)
{
    if (drizzle_detail::squareArea(footprint) == 0.0)
        return drizzle_detail::accumulationFailure(ErrorCode::InvalidData,
                                                   "drop corners must form a finite convex quadrilateral");
    return drizzle_detail::clippedSquareArea(footprint, x, y);
}

namespace drizzle_detail
{
namespace
{

struct Point
{
    double x = 0.0, y = 0.0;
};

double cross(Point a, Point b, Point c) noexcept
{
    return (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
}

bool bounds(double low, double high, std::uint32_t size, std::uint32_t& begin, std::uint32_t& end) noexcept
{
    low = std::floor(low);
    high = std::ceil(high);
    if (!std::isfinite(low) || !std::isfinite(high) || high < 0.0 || low >= size)
        return false;
    begin = static_cast<std::uint32_t>((std::max)(0.0, low));
    end = static_cast<std::uint32_t>((std::min)(static_cast<double>(size - 1u), high)) + 1u;
    return begin < end;
}

double tableValue(const DrizzleKernelTable& table, double distance) noexcept
{
    const float value = std::abs(static_cast<float>(distance));
    if (!(value < 5.11f))
        return 0.0;
    return table.values()[static_cast<std::size_t>(value / 0.01f + 0.5f)];
}

// Integrate clamped line height over a clipped x interval, in the cell's own [0,1] coordinates.
// Splitting where the height crosses zero or one keeps each piece linear, and working in cell
// coordinates avoids the large determinants an edge formula in image coordinates takes. The sign
// follows the edge's direction, so the four edges of either orientation sum to plus or minus the
// overlap.
double edgeIntegral(Point a, Point b) noexcept
{
    if (a.x == b.x)
        return 0.0;
    const double sign = a.x < b.x ? 1.0 : -1.0;
    if (a.x > b.x)
        std::swap(a, b);
    const double low = (std::max)(a.x, 0.0), high = (std::min)(b.x, 1.0);
    if (low >= high)
        return 0.0;
    const double slope = (b.y - a.y) / (b.x - a.x);
    const auto height = [&](double x) { return std::clamp(a.y + (x - a.x) * slope, 0.0, 1.0); };
    std::array<double, 4u> cuts{low, high, high, high};
    if (slope != 0.0)
    {
        cuts[2] = std::clamp(a.x - a.y / slope, low, high);
        cuts[3] = std::clamp(a.x + (1.0 - a.y) / slope, low, high);
    }
    std::sort(cuts.begin(), cuts.end());
    double sum = 0.0;
    for (std::size_t i = 1u; i < cuts.size(); ++i)
        sum += (cuts[i] - cuts[i - 1u]) * (height(cuts[i]) + height(cuts[i - 1u])) * 0.5;
    return sum * sign;
}

} // namespace

double squareArea(const DrizzleFootprint& footprint) noexcept
{
    std::array<Point, 4u> points;
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        if (!std::isfinite(footprint[i].x) || !std::isfinite(footprint[i].y))
            return 0.0;
        points[i] = {static_cast<double>(footprint[i].x) - footprint[0].x,
                     static_cast<double>(footprint[i].y) - footprint[0].y};
    }
    double orientation = 0.0;
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        const double turn = cross(points[i], points[(i + 1u) % 4u], points[(i + 2u) % 4u]);
        if (turn == 0.0 || (orientation != 0.0 && std::signbit(turn) != std::signbit(orientation)))
            return 0.0;
        orientation = turn;
    }
    return std::abs(points[1].x * points[2].y - points[1].y * points[2].x + points[2].x * points[3].y -
                    points[2].y * points[3].x) *
           0.5;
}

double clippedSquareArea(const DrizzleFootprint& footprint, std::uint32_t x, std::uint32_t y) noexcept
{
    std::array<Point, 12u> polygon{}, buffer{};
    std::size_t count = 4u;
    for (std::size_t i = 0u; i < count; ++i)
        polygon[i] = {static_cast<double>(footprint[i].x) - x, static_cast<double>(footprint[i].y) - y};
    for (std::uint32_t side = 0u; side < 4u && count != 0u; ++side)
    {
        const auto coordinate = [side](Point p) { return side < 2u ? p.x : p.y; };
        const double edge = side % 2u == 0u ? -0.5 : 0.5;
        const auto inside = [&](Point p) { return side % 2u == 0u ? coordinate(p) >= edge : coordinate(p) <= edge; };
        std::size_t nextCount = 0u;
        Point previous = polygon[count - 1u];
        for (std::size_t i = 0u; i < count; ++i)
        {
            const Point current = polygon[i];
            if (inside(previous) != inside(current))
            {
                const double t = (edge - coordinate(previous)) / (coordinate(current) - coordinate(previous));
                Point intersection{previous.x + t * (current.x - previous.x),
                                   previous.y + t * (current.y - previous.y)};
                // The clipped coordinate is known exactly. Reconstructing it by interpolation
                // loses the half-pixel edge when a finite footprint spans very large coordinates.
                if (side < 2u)
                    intersection.x = edge;
                else
                    intersection.y = edge;
                buffer[nextCount++] = intersection;
            }
            if (inside(current))
                buffer[nextCount++] = current;
            previous = current;
        }
        polygon = buffer;
        count = nextCount;
    }
    double area = 0.0;
    for (std::size_t i = 0u; i < count; ++i)
        area += polygon[i].x * polygon[(i + 1u) % count].y - polygon[i].y * polygon[(i + 1u) % count].x;
    return std::clamp(std::abs(area) * 0.5, 0.0, 1.0);
}

double integratedSquareArea(const DrizzleFootprint& footprint, std::uint32_t x, std::uint32_t y) noexcept
{
    double area = 0.0;
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        const auto& a = footprint[i];
        const auto& b = footprint[(i + 1u) % 4u];
        area += edgeIntegral({static_cast<double>(a.x) - x + 0.5, static_cast<double>(a.y) - y + 0.5},
                             {static_cast<double>(b.x) - x + 0.5, static_cast<double>(b.y) - y + 0.5});
    }
    return std::clamp(std::abs(area), 0.0, 1.0);
}

bool prepareDrizzleDrop(const DrizzleModel& model, const DrizzlePixmapView& map, const DrizzleCoordinateRow& row,
                        std::uint32_t x, std::uint32_t y, DrizzleDrop& drop)
{
    const auto& parameters = model.request().parameters;
    const auto& extent = model.outputDescriptor().extent();
    drop.x = row.x[x];
    drop.y = row.y[x];
    if (!std::isfinite(drop.x) || !std::isfinite(drop.y))
        return false;
    double xmin = drop.x, xmax = drop.x, ymin = drop.y, ymax = drop.y;
    const double width = parameters.pixelFraction * parameters.scale;
    drop.inverseWidth = 1.0 / width;
    switch (parameters.kernel)
    {
    case DrizzleKernel::Point:
        xmin = xmax = std::round(drop.x);
        ymin = ymax = std::round(drop.y);
        break;
    case DrizzleKernel::Square: {
        if (!DrizzleGeometryAccess::cornersUnchecked(map, x, y, parameters.pixelFraction * 0.5, drop.corners))
            return false;
        drop.rectangle = drop.corners[0].x == drop.corners[3].x && drop.corners[1].x == drop.corners[2].x &&
                         drop.corners[0].y == drop.corners[1].y && drop.corners[2].y == drop.corners[3].y;
        drop.area = drop.rectangle ? std::abs((static_cast<double>(drop.corners[1].x) - drop.corners[0].x) *
                                              (static_cast<double>(drop.corners[2].y) - drop.corners[0].y))
                                   : squareArea(drop.corners);
        if (drop.area == 0.0)
            return false;
        xmin = xmax = drop.corners[0].x;
        ymin = ymax = drop.corners[0].y;
        for (const auto& corner : drop.corners)
        {
            xmin = (std::min)(xmin, static_cast<double>(corner.x));
            xmax = (std::max)(xmax, static_cast<double>(corner.x));
            ymin = (std::min)(ymin, static_cast<double>(corner.y));
            ymax = (std::max)(ymax, static_cast<double>(corner.y));
        }
        break;
    }
    case DrizzleKernel::Turbo:
        drop.radius = width * 0.5;
        drop.area = width * width;
        break;
    case DrizzleKernel::Gaussian: {
        const double sigma = width / 2.3548;
        drop.radius = 3.0 * sigma;
        drop.gaussianExponent = -0.5 / (sigma * sigma);
        drop.gaussianNormalization = 1.0 / (2.0 * std::numbers::pi * sigma * sigma);
        break;
    }
    case DrizzleKernel::Lanczos2:
        drop.radius = 2.0 * width + 0.5;
        break;
    case DrizzleKernel::Lanczos3:
        drop.radius = 3.0 * width + 0.5;
        break;
    }
    double inset = 0.0;
    if (parameters.kernel == DrizzleKernel::Square || parameters.kernel == DrizzleKernel::Turbo)
        inset = 0.5;
    if (parameters.kernel == DrizzleKernel::Lanczos2 || parameters.kernel == DrizzleKernel::Lanczos3)
        inset = 1.5;
    return bounds(xmin - drop.radius + inset, xmax + drop.radius - inset, extent.width(), drop.xBegin, drop.xEnd) &&
           bounds(ymin - drop.radius + inset, ymax + drop.radius - inset, extent.height(), drop.yBegin, drop.yEnd);
}

double drizzleAxisCoefficient(const DrizzleModel& model, const DrizzleDrop& drop, const DrizzleKernelTable* table,
                              double distance) noexcept
{
    switch (model.request().parameters.kernel)
    {
    case DrizzleKernel::Turbo:
        return (std::max)(0.0, (std::min)(drop.radius, distance + 0.5) - (std::max)(-drop.radius, distance - 0.5));
    case DrizzleKernel::Gaussian:
        return std::exp(drop.gaussianExponent * distance * distance);
    case DrizzleKernel::Lanczos2:
    case DrizzleKernel::Lanczos3:
        return tableValue(*table, distance * drop.inverseWidth);
    default:
        return 1.0;
    }
}

double drizzleCoefficient(const DrizzleModel& model, const DrizzleDrop& drop, const DrizzleKernelTable* table,
                          std::uint32_t x, std::uint32_t y, bool reference) noexcept
{
    if (model.request().parameters.kernel == DrizzleKernel::Square && !reference && drop.rectangle)
    {
        const double xmin = (std::min)(drop.corners[0].x, drop.corners[1].x),
                     xmax = (std::max)(drop.corners[0].x, drop.corners[1].x);
        const double ymin = (std::min)(drop.corners[0].y, drop.corners[2].y),
                     ymax = (std::max)(drop.corners[0].y, drop.corners[2].y);
        return (std::max)(0.0, (std::min)(xmax, x + 0.5) - (std::max)(xmin, x - 0.5)) *
               (std::max)(0.0, (std::min)(ymax, y + 0.5) - (std::max)(ymin, y - 0.5)) / drop.area;
    }
    if (model.request().parameters.kernel == DrizzleKernel::Square)
        return (reference ? clippedSquareArea(drop.corners, x, y) : integratedSquareArea(drop.corners, x, y)) /
               drop.area;
    double weight =
        drizzleAxisCoefficient(model, drop, table, x - drop.x) * drizzleAxisCoefficient(model, drop, table, y - drop.y);
    switch (model.request().parameters.kernel)
    {
    case DrizzleKernel::Turbo:
        weight /= drop.area;
        break;
    case DrizzleKernel::Gaussian:
        weight *= drop.gaussianNormalization;
        break;
    default:
        break;
    }
    if ((model.request().parameters.kernel == DrizzleKernel::Gaussian || table != nullptr) && std::abs(weight) < 1e-10)
        return 0.0;
    return weight;
}

} // namespace drizzle_detail
} // namespace an
