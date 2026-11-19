// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The shape overlay, and how the image view makes it through its companion seam (`controls/ImageViewCompanions.h`).

#include "stdafx.hpp"

#include "controls/ShapeOverlayDisplayModule.h"

#include "controls/AstrometryOverlayUtils.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ViewShapes.h"
#include "controls/annotation/AnnotationSettings.h"
#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"
#include "theme/PainterGuard.h"
#include "widgets/ImageViewWidget.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>

#include <cmath>
#include <utility>

namespace an::ui
{

namespace
{

/// The part of the view's widget a paint reaches, in the widget's coordinates: the part the paint under way paints - a
/// pan paints the strip it uncovers - or the whole widget when none is said, or when the painter is turned, as the
/// image is in its rotation's preview.
[[nodiscard]] QRectF reachedByPaint(const QPainter& painter, const ImageViewWidget& widget)
{
    const QRect painted = widget.rectBeingPainted();
    if (painted.isEmpty() || !painter.worldTransform().isIdentity())
    {
        return QRectF(widget.rect());
    }
    return QRectF(painted);
}

} // namespace

void ShapeOverlayDisplayModule::setShapes(std::shared_ptr<const ViewShapes> shapes)
{
    m_Shapes = std::move(shapes);
    m_Labels = LaidOutLabels{};
}

std::size_t ShapeOverlayDisplayModule::shapeCount() const
{
    return m_Shapes ? m_Shapes->shapes.size() : 0u;
}

std::size_t ShapeOverlayDisplayModule::shapesDrawnByLastPaint() const noexcept
{
    return m_DrawnByLastPaint;
}

void ShapeOverlayDisplayModule::onDraw(QPainter& painter)
{
    m_DrawnByLastPaint = 0u;
    if (!isDisplayModuleEnabled() || !m_Shapes || m_Shapes->shapes.empty() || !m_ViewWidget)
    {
        return;
    }
    const double scale = m_ViewWidget->getCurrentScaleFactor();
    if (!std::isfinite(scale) || scale <= 0.0)
    {
        return;
    }

    const QRectF reached = reachedByPaint(painter, *m_ViewWidget);
    const an::ui::DesignTokens& tokens = an::ui::tokens();
    // A label stands the smallest spacing from its shape; a shape of one point is a dot of half that radius.
    const double labelGap = tokens.spacing.xs;
    const double pointRadius = tokens.spacing.xs / 2.0;
    QFont font = tokens.typography.caption;
    font.setPixelSize((std::max)(1, static_cast<int>(std::lround(
                                        detail::overlayLabelPixels(*m_ViewWidget, AnnotationViewTextSize::Medium)))));
    if (m_Labels.pixelSize != font.pixelSize() || m_Labels.texts.size() != m_Shapes->shapes.size())
    {
        m_Labels = LaidOutLabels{};
        m_Labels.pixelSize = font.pixelSize();
        m_Labels.texts.reserve(m_Shapes->shapes.size());
        m_Labels.widths.reserve(m_Shapes->shapes.size());
        for (const ViewShape& shape : m_Shapes->shapes)
        {
            QStaticText text(shape.label);
            text.setTextFormat(Qt::PlainText);
            text.setPerformanceHint(QStaticText::AggressiveCaching);
            if (!shape.label.isEmpty())
            {
                text.prepare(QTransform(), font);
            }
            m_Labels.widths.push_back(shape.label.isEmpty() ? 0.0 : text.size().width());
            m_Labels.texts.push_back(std::move(text));
        }
    }
    const QFontMetricsF metrics(font);

    an::ui::PainterGuard guard(&painter);
    painter.setFont(font);
    QPen pen;
    pen.setWidthF(kLineWidth);
    pen.setCosmetic(true);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);

    QPolygonF scaled;
    for (std::size_t shapeIndex = 0; shapeIndex < m_Shapes->shapes.size(); ++shapeIndex)
    {
        const ViewShape& shape = m_Shapes->shapes[shapeIndex];
        const QRectF bounds(shape.bounds.x() * scale, shape.bounds.y() * scale, shape.bounds.width() * scale,
                            shape.bounds.height() * scale);
        // A shape and its label beside it: drawn only where the paint reaches them.
        QRectF covered = bounds.adjusted(-pointRadius - kLineWidth, -pointRadius - kLineWidth, pointRadius + kLineWidth,
                                         pointRadius + kLineWidth);
        if (!shape.label.isEmpty())
        {
            covered = covered.united(QRectF(bounds.right() + labelGap, bounds.top(),
                                            m_Labels.widths[shapeIndex] + labelGap, metrics.height()));
        }
        if (!covered.intersects(reached))
        {
            continue;
        }
        ++m_DrawnByLastPaint;

        const QColor color = an::ui::colorAsShown(shape.color);
        pen.setColor(color);
        painter.setPen(pen);
        scaled.resize(static_cast<qsizetype>(shape.points.size()));
        for (std::size_t index = 0; index < shape.points.size(); ++index)
        {
            scaled[static_cast<qsizetype>(index)] =
                QPointF(shape.points[index].x() * scale, shape.points[index].y() * scale);
        }

        if (scaled.size() == 1)
        {
            painter.setBrush(color);
            painter.drawEllipse(scaled.front(), pointRadius, pointRadius);
        }
        else if (shape.closed)
        {
            painter.setBrush(shape.filled ? QBrush(color) : QBrush(Qt::NoBrush));
            painter.drawPolygon(scaled);
        }
        else
        {
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(scaled);
        }

        if (!shape.label.isEmpty())
        {
            painter.drawStaticText(QPointF(bounds.right() + labelGap, bounds.top()), m_Labels.texts[shapeIndex]);
        }
    }
}

std::unique_ptr<::ImageViewWidgetDisplayModule> createShapeOverlay(::ImageViewControl&)
{
    return std::make_unique<ShapeOverlayDisplayModule>();
}

void setShapeOverlayShapes(::ImageViewWidgetDisplayModule& overlay, std::shared_ptr<const ViewShapes> shapes)
{
    if (auto* shapeOverlay = dynamic_cast<ShapeOverlayDisplayModule*>(&overlay); shapeOverlay != nullptr)
    {
        shapeOverlay->setShapes(std::move(shapes));
    }
}

} // namespace an::ui
