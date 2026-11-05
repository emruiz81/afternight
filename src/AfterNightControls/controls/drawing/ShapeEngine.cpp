// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/drawing/ShapeEngine.h"

#include "controls/drawing/CoveragePlane.h"

#include <AfterNightEngine/Algorithms/Pointwise/LayerBlend.h>
#include <AfterNightEngine/Algorithms/Spatial/DrawingPrimitives.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QImage>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace
{

using an::ui::drawing::clampUnit;

/// How far beyond a path's bounds the toolkit's antialiasing can lay coverage, in pixels.
constexpr double kAntialiasMargin = 2.0;

/// How far a feather's blur can carry coverage, in pixels, counted once more so that no sample the blur of the whole
/// image would make non-zero lies near a layer's edge: each pass's kernel reaches four of its standard deviations,
/// rounded up, and the passes add their reaches. No value for a width the engine refuses.
[[nodiscard]] std::optional<int> featherReach(const float feather)
{
    const an::Result<an::CoverageFeatherPlan> plan =
        an::planCoverageFeather(an::CoverageFeatherRequest{.feather = feather});
    if (!plan.hasValue())
    {
        return std::nullopt;
    }
    if (plan->passes() == 0u)
    {
        return 0;
    }
    const int kernelReach = static_cast<int>(std::ceil(4.0 * plan->passSigma())) + 1;
    return static_cast<int>(plan->passes() + 1u) * kernelReach;
}

[[nodiscard]] float outlineWidthForShape(const an::ui::drawing::ShapeObject& shape)
{
    if (const auto* line = std::get_if<an::ui::drawing::ShapeObject::LineData>(&shape.data))
    {
        return (std::max)(line->width, 1.0f);
    }
    return 1.0f;
}

[[nodiscard]] QTransform shapeRotationTransform(const an::ui::drawing::ShapeObject& shape)
{
    QTransform transform;
    if (std::abs(shape.rotation) <= std::numeric_limits<float>::epsilon())
    {
        return transform;
    }

    transform.translate(shape.center.x(), shape.center.y());
    transform.rotate(shape.rotation);
    transform.translate(-shape.center.x(), -shape.center.y());
    return transform;
}

[[nodiscard]] QPointF inverseRotatePoint(const an::ui::drawing::ShapeObject& shape, const QPointF& point)
{
    const QTransform transform = shapeRotationTransform(shape);
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    return invertible ? inverse.map(point) : point;
}

[[nodiscard]] QRectF centeredRect(const QPointF& center, const QSizeF& size)
{
    return QRectF(center.x() - (size.width() * 0.5), center.y() - (size.height() * 0.5), size.width(), size.height());
}

[[nodiscard]] QRectF pointsBounds(const std::vector<QPointF>& points)
{
    if (points.empty())
    {
        return {};
    }

    qreal minX = points.front().x();
    qreal maxX = points.front().x();
    qreal minY = points.front().y();
    qreal maxY = points.front().y();
    for (const QPointF& point : points)
    {
        minX = (std::min)(minX, point.x());
        maxX = (std::max)(maxX, point.x());
        minY = (std::min)(minY, point.y());
        maxY = (std::max)(maxY, point.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

[[nodiscard]] float normalizeRotation(const float degrees)
{
    return static_cast<float>(std::remainder(static_cast<double>(degrees), 360.0));
}

void updateCenterFromGeometry(an::ui::drawing::ShapeObject& shape)
{
    switch (shape.type)
    {
    case an::ui::drawing::ShapeObject::Type::Polygon:
        if (const auto* poly = std::get_if<an::ui::drawing::ShapeObject::PolyData>(&shape.data))
        {
            shape.center = pointsBounds(poly->vertices).center();
        }
        break;
    case an::ui::drawing::ShapeObject::Type::Line:
        if (const auto* line = std::get_if<an::ui::drawing::ShapeObject::LineData>(&shape.data))
        {
            shape.center = pointsBounds(line->points).center();
        }
        break;
    default:
        break;
    }
}

[[nodiscard]] qreal distanceToSegment(const QPointF& point, const QPointF& start, const QPointF& end)
{
    const qreal dx = end.x() - start.x();
    const qreal dy = end.y() - start.y();
    const qreal lenSquared = (dx * dx) + (dy * dy);
    if (lenSquared <= std::numeric_limits<qreal>::epsilon())
    {
        return std::hypot(point.x() - start.x(), point.y() - start.y());
    }

    const qreal t = std::clamp(((point.x() - start.x()) * dx + (point.y() - start.y()) * dy) / lenSquared, 0.0, 1.0);
    const QPointF projection(start.x() + (dx * t), start.y() + (dy * t));
    return std::hypot(point.x() - projection.x(), point.y() - projection.y());
}

} // namespace

namespace an::ui::drawing
{

QRectF ShapeEngine::boundingRect(const ShapeObject& shape)
{
    return buildPainterPath(shape).boundingRect();
}

bool ShapeEngine::hitTest(const ShapeObject& shape, const QPointF& imagePos, const double tolerance)
{
    if (shape.type == ShapeObject::Type::Line)
    {
        const auto* line = std::get_if<ShapeObject::LineData>(&shape.data);
        if (!line || line->points.size() < 2)
        {
            return false;
        }

        const double hitDistance = (std::max)(0.5, static_cast<double>(line->width) * 0.5) + tolerance;
        for (size_t index = 1; index < line->points.size(); ++index)
        {
            if (distanceToSegment(imagePos, line->points[index - 1], line->points[index]) <= hitDistance)
            {
                return true;
            }
        }
        return false;
    }

    QPainterPath path = buildPainterPath(shape);
    if (!shape.filled)
    {
        QPainterPathStroker stroker;
        stroker.setWidth((std::max)(1.0, outlineWidthForShape(shape) + (2.0 * tolerance)));
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        path = stroker.createStroke(path);
    }
    else if (tolerance > 0.0)
    {
        QPainterPathStroker stroker;
        stroker.setWidth(2.0 * tolerance);
        path = path.united(stroker.createStroke(path));
    }

    return path.contains(imagePos);
}

int ShapeEngine::hitTestHandle(const ShapeObject& shape, const QPointF& imagePos, const double tolerance)
{
    if (shape.type == ShapeObject::Type::Rectangle)
    {
        if (const auto* rect = std::get_if<ShapeObject::RectData>(&shape.data))
        {
            const QRectF localRect = centeredRect(shape.center, rect->size);
            const QPointF localPoint = inverseRotatePoint(shape, imagePos);
            const double clampedTolerance = (std::max)(tolerance, 0.0);
            struct EdgeHit
            {
                int handleIndex = -1;
                double distance = 0.0;
            };

            const std::array<EdgeHit, 4> edgeHits = {
                EdgeHit{0, distanceToSegment(localPoint, localRect.topRight(), localRect.bottomRight())},
                EdgeHit{1, distanceToSegment(localPoint, localRect.topLeft(), localRect.topRight())},
                EdgeHit{2, distanceToSegment(localPoint, localRect.topLeft(), localRect.bottomLeft())},
                EdgeHit{3, distanceToSegment(localPoint, localRect.bottomLeft(), localRect.bottomRight())}};

            const auto edgeIt =
                std::min_element(edgeHits.begin(), edgeHits.end(),
                                 [](const EdgeHit& lhs, const EdgeHit& rhs) { return lhs.distance < rhs.distance; });
            if (edgeIt != edgeHits.end() && edgeIt->distance <= clampedTolerance)
            {
                return edgeIt->handleIndex;
            }
        }
    }

    const auto handles = handlePoints(shape);
    for (size_t index = 0; index < handles.size(); ++index)
    {
        const QPointF delta = handles[index] - imagePos;
        if (std::hypot(delta.x(), delta.y()) <= tolerance)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::vector<QPointF> ShapeEngine::handlePoints(const ShapeObject& shape)
{
    const QTransform transform = shapeRotationTransform(shape);

    switch (shape.type)
    {
    case ShapeObject::Type::Circle:
        if (const auto* circle = std::get_if<ShapeObject::CircleData>(&shape.data))
        {
            return {transform.map(QPointF(shape.center.x() + circle->radius, shape.center.y())),
                    transform.map(QPointF(shape.center.x(), shape.center.y() - circle->radius)),
                    transform.map(QPointF(shape.center.x() - circle->radius, shape.center.y())),
                    transform.map(QPointF(shape.center.x(), shape.center.y() + circle->radius))};
        }
        break;
    case ShapeObject::Type::Ellipse:
        if (const auto* ellipse = std::get_if<ShapeObject::EllipseData>(&shape.data))
        {
            return {transform.map(QPointF(shape.center.x() + ellipse->rx, shape.center.y())),
                    transform.map(QPointF(shape.center.x(), shape.center.y() - ellipse->ry)),
                    transform.map(QPointF(shape.center.x() - ellipse->rx, shape.center.y())),
                    transform.map(QPointF(shape.center.x(), shape.center.y() + ellipse->ry))};
        }
        break;
    case ShapeObject::Type::Rectangle:
        if (const auto* rect = std::get_if<ShapeObject::RectData>(&shape.data))
        {
            const QRectF localRect = centeredRect(shape.center, rect->size);
            return {transform.map(QPointF(localRect.right(), shape.center.y())),
                    transform.map(QPointF(shape.center.x(), localRect.top())),
                    transform.map(QPointF(localRect.left(), shape.center.y())),
                    transform.map(QPointF(shape.center.x(), localRect.bottom()))};
        }
        break;
    case ShapeObject::Type::Polygon:
        if (const auto* poly = std::get_if<ShapeObject::PolyData>(&shape.data))
        {
            std::vector<QPointF> handles;
            handles.reserve(poly->vertices.size());
            for (const QPointF& vertex : poly->vertices)
            {
                handles.push_back(transform.map(vertex));
            }
            return handles;
        }
        break;
    case ShapeObject::Type::Line:
        if (const auto* line = std::get_if<ShapeObject::LineData>(&shape.data))
        {
            std::vector<QPointF> handles;
            handles.reserve(line->points.size());
            for (const QPointF& point : line->points)
            {
                handles.push_back(transform.map(point));
            }
            return handles;
        }
        break;
    }

    return {};
}

void ShapeEngine::translate(ShapeObject& shape, const QPointF& delta)
{
    shape.center += delta;
    if (auto* poly = std::get_if<ShapeObject::PolyData>(&shape.data))
    {
        for (QPointF& vertex : poly->vertices)
        {
            vertex += delta;
        }
    }
    else if (auto* line = std::get_if<ShapeObject::LineData>(&shape.data))
    {
        for (QPointF& point : line->points)
        {
            point += delta;
        }
    }
}

void ShapeEngine::rotate(ShapeObject& shape, const float deltaDegrees)
{
    shape.rotation = normalizeRotation(shape.rotation + deltaDegrees);
}

void ShapeEngine::moveHandle(ShapeObject& shape, const int handleIndex, const QPointF& imagePos)
{
    const QPointF localPoint = inverseRotatePoint(shape, imagePos);

    switch (shape.type)
    {
    case ShapeObject::Type::Circle:
        if (auto* circle = std::get_if<ShapeObject::CircleData>(&shape.data))
        {
            circle->radius = (std::max)(1.0f, static_cast<float>(std::hypot(localPoint.x() - shape.center.x(),
                                                                            localPoint.y() - shape.center.y())));
        }
        break;
    case ShapeObject::Type::Ellipse:
        if (auto* ellipse = std::get_if<ShapeObject::EllipseData>(&shape.data))
        {
            if (handleIndex == 0 || handleIndex == 2)
            {
                ellipse->rx = (std::max)(1.0f, static_cast<float>(std::abs(localPoint.x() - shape.center.x())));
            }
            else if (handleIndex == 1 || handleIndex == 3)
            {
                ellipse->ry = (std::max)(1.0f, static_cast<float>(std::abs(localPoint.y() - shape.center.y())));
            }
        }
        break;
    case ShapeObject::Type::Rectangle:
        if (auto* rect = std::get_if<ShapeObject::RectData>(&shape.data))
        {
            QRectF nextRect = centeredRect(shape.center, rect->size);
            switch (handleIndex)
            {
            case 0:
                nextRect.setRight((std::max)(localPoint.x(), nextRect.left() + 1.0));
                break;
            case 1:
                nextRect.setTop((std::min)(localPoint.y(), nextRect.bottom() - 1.0));
                break;
            case 2:
                nextRect.setLeft((std::min)(localPoint.x(), nextRect.right() - 1.0));
                break;
            case 3:
                nextRect.setBottom((std::max)(localPoint.y(), nextRect.top() + 1.0));
                break;
            default:
                return;
            }
            rect->size = QSizeF((std::max)(nextRect.width(), 1.0), (std::max)(nextRect.height(), 1.0));
            shape.center = nextRect.center();
        }
        break;
    case ShapeObject::Type::Polygon:
        if (auto* poly = std::get_if<ShapeObject::PolyData>(&shape.data))
        {
            if (handleIndex >= 0 && static_cast<size_t>(handleIndex) < poly->vertices.size())
            {
                poly->vertices[static_cast<size_t>(handleIndex)] = imagePos;
                updateCenterFromGeometry(shape);
            }
        }
        break;
    case ShapeObject::Type::Line:
        if (auto* line = std::get_if<ShapeObject::LineData>(&shape.data))
        {
            if (handleIndex >= 0 && static_cast<size_t>(handleIndex) < line->points.size())
            {
                line->points[static_cast<size_t>(handleIndex)] = imagePos;
                updateCenterFromGeometry(shape);
            }
        }
        break;
    }
}

ShapeObject ShapeEngine::makeDragShape(const ShapeObject::Type type, const QPointF& anchor, const QPointF& current,
                                       const float opacity, const float feather, const bool filled,
                                       const float lineWidth)
{
    ShapeObject shape;
    shape.type = type;
    shape.opacity = clampUnit(opacity);
    shape.feather = (std::max)(feather, 0.0f);
    shape.filled = filled;

    switch (type)
    {
    case ShapeObject::Type::Circle: {
        shape.center = anchor;
        shape.data = ShapeObject::CircleData{
            static_cast<float>((std::max)(1.0, std::hypot(current.x() - anchor.x(), current.y() - anchor.y())))};
        break;
    }
    case ShapeObject::Type::Ellipse: {
        const QRectF rect(anchor, current);
        const QRectF normalized = rect.normalized();
        shape.center = normalized.center();
        shape.data = ShapeObject::EllipseData{static_cast<float>((std::max)(normalized.width() * 0.5, 0.5)),
                                              static_cast<float>((std::max)(normalized.height() * 0.5, 0.5))};
        break;
    }
    case ShapeObject::Type::Rectangle: {
        const QRectF rect(anchor, current);
        const QRectF normalized = rect.normalized();
        shape.center = normalized.center();
        shape.data = ShapeObject::RectData{
            QSizeF((std::max)(normalized.width(), 1.0), (std::max)(normalized.height(), 1.0)), 0.0f};
        break;
    }
    case ShapeObject::Type::Polygon:
    case ShapeObject::Type::Line:
        return makePathShape(type, {anchor, current}, opacity, feather, filled, lineWidth);
    }

    return shape;
}

ShapeObject ShapeEngine::makePathShape(const ShapeObject::Type type, const std::vector<QPointF>& points,
                                       const float opacity, const float feather, const bool filled,
                                       const float lineWidth)
{
    ShapeObject shape;
    shape.type = type;
    shape.opacity = clampUnit(opacity);
    shape.feather = (std::max)(feather, 0.0f);
    shape.filled = filled;

    if (type == ShapeObject::Type::Polygon)
    {
        shape.data = ShapeObject::PolyData{points};
    }
    else
    {
        shape.data = ShapeObject::LineData{points, (std::max)(lineWidth, 1.0f)};
        shape.filled = false;
    }

    updateCenterFromGeometry(shape);
    return shape;
}

QPainterPath ShapeEngine::buildPainterPath(const ShapeObject& shape)
{
    QPainterPath path;

    switch (shape.type)
    {
    case ShapeObject::Type::Circle:
        if (const auto* circle = std::get_if<ShapeObject::CircleData>(&shape.data))
        {
            path.addEllipse(shape.center, circle->radius, circle->radius);
        }
        break;
    case ShapeObject::Type::Ellipse:
        if (const auto* ellipse = std::get_if<ShapeObject::EllipseData>(&shape.data))
        {
            path.addEllipse(shape.center, ellipse->rx, ellipse->ry);
        }
        break;
    case ShapeObject::Type::Rectangle:
        if (const auto* rect = std::get_if<ShapeObject::RectData>(&shape.data))
        {
            path.addRect(centeredRect(shape.center, rect->size));
        }
        break;
    case ShapeObject::Type::Polygon:
        if (const auto* poly = std::get_if<ShapeObject::PolyData>(&shape.data))
        {
            if (!poly->vertices.empty())
            {
                QPolygonF polygon;
                polygon.reserve(static_cast<qsizetype>(poly->vertices.size()));
                for (const QPointF& vertex : poly->vertices)
                {
                    polygon << vertex;
                }
                path.addPolygon(polygon);
                path.closeSubpath();
            }
        }
        break;
    case ShapeObject::Type::Line:
        if (const auto* line = std::get_if<ShapeObject::LineData>(&shape.data))
        {
            if (!line->points.empty())
            {
                path.moveTo(line->points.front());
                for (size_t index = 1; index < line->points.size(); ++index)
                {
                    path.lineTo(line->points[index]);
                }
            }
        }
        break;
    }

    return shapeRotationTransform(shape).map(path);
}

float ShapeLayer::at(const int x, const int y) const noexcept
{
    if (!bounds.contains(x, y) || coverage.empty())
    {
        return 0.0f;
    }
    return coverage[static_cast<std::size_t>(y - bounds.top()) * static_cast<std::size_t>(bounds.width()) +
                    static_cast<std::size_t>(x - bounds.left())];
}

std::optional<an::PlaneView<const float>> ShapeLayer::plane() const noexcept
{
    if (bounds.isEmpty())
    {
        return std::nullopt;
    }
    return packedPlane(coverage, static_cast<std::uint32_t>(bounds.width()),
                       static_cast<std::uint32_t>(bounds.height()));
}

std::uint64_t ShapeLayer::bytes() const noexcept
{
    return static_cast<std::uint64_t>(coverage.size()) * sizeof(float);
}

QRect ShapeEngine::layerBounds(const ShapeObject& shape, const int width, const int height)
{
    const QRectF outline = buildPainterPath(shape).boundingRect();
    if (width <= 0 || height <= 0 || !std::isfinite(outline.left()) || !std::isfinite(outline.top()) ||
        !std::isfinite(outline.right()) || !std::isfinite(outline.bottom()))
    {
        return {};
    }

    // The pen of an outline reaches half its width beyond the path, and the toolkit's antialiasing a pixel beyond
    // that.
    double margin = kAntialiasMargin;
    if (shape.type == ShapeObject::Type::Line || !shape.filled)
    {
        margin += std::ceil(static_cast<double>(outlineWidthForShape(shape)) * 0.5);
    }
    if (const std::optional<int> reach = featherReach(shape.feather); reach.has_value())
    {
        margin += static_cast<double>(*reach);
    }
    else
    {
        return {};
    }

    const QRectF widened = outline.adjusted(-margin, -margin, margin, margin);
    const double left = (std::max)(std::floor(widened.left()), 0.0);
    const double top = (std::max)(std::floor(widened.top()), 0.0);
    const double right = (std::min)(std::ceil(widened.right()), static_cast<double>(width - 1));
    const double bottom = (std::min)(std::ceil(widened.bottom()), static_cast<double>(height - 1));
    if (right < left || bottom < top)
    {
        return {};
    }
    return QRect(QPoint(static_cast<int>(left), static_cast<int>(top)),
                 QPoint(static_cast<int>(right), static_cast<int>(bottom)));
}

ShapeLayer ShapeEngine::rasterizeShapeCoverage(const ShapeObject& shape, const QRect& bounds)
{
    ShapeLayer layer;
    if (bounds.isEmpty())
    {
        return layer;
    }
    layer.bounds = bounds;

    QImage raster(bounds.width(), bounds.height(), QImage::Format_ARGB32_Premultiplied);
    raster.fill(Qt::transparent);
    {
        QPainter painter(&raster);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(-bounds.left(), -bounds.top());
        painter.setBrush(Qt::NoBrush);
        painter.setPen(Qt::NoPen);

        const QPainterPath path = buildPainterPath(shape);
        if (shape.type == ShapeObject::Type::Line || !shape.filled)
        {
            QPen pen(Qt::white);
            pen.setWidthF(outlineWidthForShape(shape));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawPath(path);
        }
        else
        {
            painter.fillPath(path, Qt::white);
        }
    }

    layer.coverage.resize(static_cast<std::size_t>(bounds.width()) * static_cast<std::size_t>(bounds.height()));
    for (int y = 0; y < bounds.height(); ++y)
    {
        const auto* line = reinterpret_cast<const QRgb*>(raster.constScanLine(y));
        float* row = layer.coverage.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(bounds.width());
        for (int x = 0; x < bounds.width(); ++x)
        {
            row[x] = static_cast<float>(qAlpha(line[x])) / 255.0f;
        }
    }
    return layer;
}

an::Status ShapeEngine::featherShapeLayer(const an::ExecutionContext& execution, ShapeLayer& layer, const float feather)
{
    const an::Result<an::CoverageFeatherPlan> plan =
        an::planCoverageFeather(an::CoverageFeatherRequest{.feather = feather});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    if (plan->passes() == 0u || layer.bounds.isEmpty())
    {
        return an::Status::success();
    }
    const auto width = static_cast<std::uint32_t>(layer.bounds.width());
    const auto height = static_cast<std::uint32_t>(layer.bounds.height());
    std::vector<float> feathered(layer.coverage.size(), 0.0f);
    const std::optional<an::PlaneView<const float>> source = layer.plane();
    const std::optional<an::PlaneView<float>> destination = packedPlane(feathered, width, height);
    if (!source.has_value() || !destination.has_value())
    {
        return an::Status::failure(an::ErrorCode::Internal, "a shape's layer cannot be viewed as a plane");
    }
    const an::Result<an::CoverageFeatherResult> blurred = an::featherCoverage(execution, *plan, *source, *destination);
    if (!blurred.hasValue())
    {
        return blurred.status();
    }
    layer.coverage = std::move(feathered);
    return an::Status::success();
}

an::Result<ShapeLayer> ShapeEngine::renderShapeLayer(const an::ExecutionContext& execution, const ShapeObject& shape,
                                                     const int width, const int height)
{
    ShapeLayer layer = rasterizeShapeCoverage(shape, layerBounds(shape, width, height));
    const an::Status feathered = featherShapeLayer(execution, layer, shape.feather);
    if (feathered.failed())
    {
        return feathered;
    }
    return layer;
}

an::Status ShapeEngine::screenShapeLayer(const an::ExecutionContext& execution, const ShapeLayer& layer,
                                         const float opacity, const an::PlaneView<float> composite, const QRect& clip)
{
    const QRect plane(0, 0, static_cast<int>(composite.width()), static_cast<int>(composite.height()));
    const QRect region = layer.bounds.intersected(clip).intersected(plane);
    if (region.isEmpty())
    {
        return an::Status::success();
    }
    const an::Result<an::LayerBlendPlan> blend = an::planLayerBlend(
        an::LayerBlendRequest{.mode = an::LayerBlendMode::Screen,
                              .opacity = std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 0.0f});
    if (!blend.hasValue())
    {
        return blend.status();
    }
    const std::optional<an::PlaneView<const float>> coverage = layer.plane();
    const std::optional<an::PlaneView<float>> destination = planeRegion(composite, region);
    const std::optional<an::PlaneView<const float>> source =
        coverage.has_value() ? planeRegion(*coverage, region.translated(-layer.bounds.topLeft()))
                             : std::optional<an::PlaneView<const float>>{};
    if (!destination.has_value() || !source.has_value())
    {
        return an::Status::failure(an::ErrorCode::Internal, "a shape's layer cannot be screened over the mask");
    }
    const an::Result<an::LayerBlendResult> blended =
        an::blendLayerPlane(execution, *blend, destination->constView(), *source, *destination);
    return blended.hasValue() ? an::Status::success() : blended.status();
}

QRectF ShapeObject::boundingRect() const
{
    return ShapeEngine::boundingRect(*this);
}

bool ShapeObject::hitTest(const QPointF& imagePos, const double tolerance) const
{
    return ShapeEngine::hitTest(*this, imagePos, tolerance);
}

int ShapeObject::hitTestHandle(const QPointF& imagePos, const double tolerance) const
{
    return ShapeEngine::hitTestHandle(*this, imagePos, tolerance);
}

} // namespace an::ui::drawing
