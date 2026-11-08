// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/annotation/AnnotationRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <vector>

namespace an::ui
{
namespace
{

[[nodiscard]] QColor withOpacity(QColor color, const double opacity)
{
    color.setAlphaF(static_cast<float>(std::clamp(color.alphaF() * opacity, 0.0, 1.0)));
    return color;
}

/// A colour part-way from one to another, each channel and the opacity alike.
[[nodiscard]] QColor blended(const QColor& from, const QColor& to, const double amount)
{
    const auto mix = [amount](const float a, const float b) {
        return static_cast<float>(std::clamp(a + (b - a) * amount, 0.0, 1.0));
    };
    QColor color;
    color.setRgbF(mix(from.redF(), to.redF()), mix(from.greenF(), to.greenF()), mix(from.blueF(), to.blueF()),
                  mix(from.alphaF(), to.alphaF()));
    return color;
}

/// Whether a mark's outline spans much of the frame: it is drawn dashed and softer, a region rather than an object.
[[nodiscard]] bool spansFrame(const AnnotationMark& mark, const QSize frame)
{
    if (mark.coversFrame)
    {
        return true;
    }
    if (mark.outline.size() < 3)
    {
        return false;
    }
    const QRectF box = mark.outline.boundingRect();
    return (std::max)(box.width(), box.height()) > 0.5 * (std::min)(frame.width(), frame.height());
}

[[nodiscard]] QPen roundPen(const QColor& color, const double width)
{
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCosmetic(false);
    return pen;
}

/// A mark's shape as the style draws it: its outline or its circle, four brackets about where it lies, or a star's
/// ring or reticle.
[[nodiscard]] QPainterPath markShape(const AnnotationMark& mark, const AnnotationStyle& style,
                                     const AnnotationMetrics& metrics)
{
    QPainterPath path;
    if (mark.kind == AnnotationMarkKind::Star && style.starMarker == AnnotationStarMarker::Reticle)
    {
        // Four ticks from the star's radius outward, toward the frame's edges: the star itself is left clear.
        const double inner = mark.radius;
        const double outer = kAnnotationReticleReach * mark.radius;
        for (const QPointF direction : {QPointF(1.0, 0.0), QPointF(-1.0, 0.0), QPointF(0.0, 1.0), QPointF(0.0, -1.0)})
        {
            path.moveTo(mark.center + direction * inner);
            path.lineTo(mark.center + direction * outer);
        }
        return path;
    }
    const bool outlined = mark.outline.size() >= 3;
    const bool brackets =
        style.marker == AnnotationMarker::Brackets && mark.kind != AnnotationMarkKind::Star && !mark.coversFrame;
    if (brackets)
    {
        const QRectF box = outlined ? mark.outline.boundingRect()
                                    : QRectF(mark.center.x() - mark.radius, mark.center.y() - mark.radius,
                                             2.0 * mark.radius, 2.0 * mark.radius);
        const double side = (std::min)(box.width(), box.height());
        const double arm = std::clamp(0.28 * side, (std::min)(0.5 * metrics.fontSize, 0.5 * side), 0.5 * side);
        const auto corner = [&path, arm](const QPointF at, const double dx, const double dy) {
            path.moveTo(at + QPointF(dx * arm, 0.0));
            path.lineTo(at);
            path.lineTo(at + QPointF(0.0, dy * arm));
        };
        corner(box.topLeft(), 1.0, 1.0);
        corner(box.topRight(), -1.0, 1.0);
        corner(box.bottomLeft(), 1.0, -1.0);
        corner(box.bottomRight(), -1.0, -1.0);
        return path;
    }
    if (outlined)
    {
        path.addPolygon(mark.outline);
        path.closeSubpath();
        return path;
    }
    path.addEllipse(mark.center, mark.radius, mark.radius);
    return path;
}

/// An arrow's head at a tip, pointing away from where the arrow starts.
[[nodiscard]] QPolygonF arrowHead(const QPointF from, const QPointF tip, const double length, const double halfWidth)
{
    const QPointF delta = tip - from;
    const double norm = std::hypot(delta.x(), delta.y());
    if (!(norm > 0.0))
    {
        return {};
    }
    const QPointF along = delta / norm;
    const QPointF across(-along.y(), along.x());
    const QPointF base = tip - along * length;
    return QPolygonF{tip, base + across * halfWidth, base - across * halfWidth};
}

void drawText(QPainter& painter, const QPainterPath& path, const QColor& color, const AnnotationStyle& style,
              const AnnotationMetrics& metrics)
{
    if (path.isEmpty())
    {
        return;
    }
    if (style.labelBacking == AnnotationLabelBacking::Halo)
    {
        painter.strokePath(path, roundPen(style.colors.halo, metrics.haloWidth));
    }
    painter.fillPath(path, color);
}

} // namespace

QColor annotationMarkColor(const AnnotationColors& colors, const AnnotationMarkKind kind)
{
    switch (kind)
    {
    case AnnotationMarkKind::Galaxy:
        return colors.galaxy;
    case AnnotationMarkKind::Nebula:
        return colors.nebula;
    case AnnotationMarkKind::PlanetaryNebula:
        return colors.planetaryNebula;
    case AnnotationMarkKind::OpenCluster:
        return colors.openCluster;
    case AnnotationMarkKind::GlobularCluster:
        return colors.globularCluster;
    case AnnotationMarkKind::Star:
        return colors.star;
    case AnnotationMarkKind::OtherObject:
        break;
    }
    return colors.otherObject;
}

void paintAnnotations(QPainter& painter, const AnnotationScene& scene, const AnnotationLayout& layout,
                      const AnnotationStyle& style)
{
    if (scene.imageSize.isEmpty() || !(layout.metrics.fontSize > 0.0))
    {
        return;
    }
    const AnnotationMetrics& metrics = layout.metrics;
    const AnnotationColors& colors = style.colors;
    // The text's halo and the lines' dark edge are chosen apart.
    const bool lineHalo = style.lineHalo;
    const double lineOpacity =
        std::clamp(std::isfinite(style.lineOpacity) ? style.lineOpacity : 1.0, kAnnotationMinimumLineOpacity, 1.0);
    const QColor softHalo = withOpacity(colors.halo, 0.55);
    const QColor markHalo = withOpacity(softHalo, lineOpacity);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(QRectF(0.0, 0.0, scene.imageSize.width(), scene.imageSize.height()), Qt::IntersectClip);
    painter.setBrush(Qt::NoBrush);

    // The grid, under everything else.
    const double gridOpacity = std::clamp(style.gridOpacity, 0.0, 1.0);
    if (!scene.grid.empty() && gridOpacity > 0.0)
    {
        painter.setPen(roundPen(withOpacity(colors.grid, gridOpacity), metrics.gridWidth));
        for (const AnnotationGridLine& line : scene.grid)
        {
            for (const QPolygonF& segment : line.segments)
            {
                painter.drawPolyline(segment);
            }
        }
    }

    // The marks: every fill first, then every halo, so no halo covers another mark's line, then the lines.
    std::vector<QPainterPath> shapes(scene.marks.size());
    std::vector<bool> drawn(scene.marks.size(), false);
    for (std::size_t index = 0u; index < scene.marks.size(); ++index)
    {
        // A mark drawn without a label by choice is always drawn; another only when its label was placed or the
        // style marks the objects whose labels found no room.
        const bool labelled = index < layout.labelled.size() && layout.labelled[index];
        if (!labelled && !style.unlabelledMarks && !scene.marks[index].markOnly)
        {
            continue;
        }
        shapes[index] = markShape(scene.marks[index], style, metrics);
        drawn[index] = true;
    }
    const double fillOpacity = std::clamp(std::isfinite(style.fillOpacity) ? style.fillOpacity : 0.0, 0.0, 1.0);
    if (style.fillObjects && style.marker == AnnotationMarker::Outline && fillOpacity > 0.0)
    {
        // An object's area in its colour; a region that spans the frame is left unfilled, or it would tint the image.
        for (std::size_t index = 0u; index < scene.marks.size(); ++index)
        {
            const AnnotationMark& mark = scene.marks[index];
            if (drawn[index] && mark.kind != AnnotationMarkKind::Star && !spansFrame(mark, scene.imageSize))
            {
                painter.fillPath(shapes[index], withOpacity(annotationMarkColor(colors, mark.kind), fillOpacity));
            }
        }
    }
    const auto markWidth = [&metrics](const AnnotationMark& mark) {
        return mark.kind == AnnotationMarkKind::Star ? 0.8 * metrics.lineWidth : metrics.lineWidth;
    };
    if (lineHalo)
    {
        for (std::size_t index = 0u; index < scene.marks.size(); ++index)
        {
            if (drawn[index])
            {
                painter.strokePath(shapes[index], roundPen(markHalo, markWidth(scene.marks[index]) * 2.6));
            }
        }
    }
    for (std::size_t index = 0u; index < scene.marks.size(); ++index)
    {
        if (!drawn[index])
        {
            continue;
        }
        const AnnotationMark& mark = scene.marks[index];
        QPen pen = roundPen(withOpacity(annotationMarkColor(colors, mark.kind), lineOpacity), markWidth(mark));
        if (spansFrame(mark, scene.imageSize))
        {
            // A region rather than an object: its extent in long, soft dashes.
            pen.setColor(withOpacity(pen.color(), 0.7));
            pen.setDashPattern({8.0, 5.0});
            pen.setCapStyle(Qt::FlatCap);
        }
        painter.strokePath(shapes[index], pen);
    }

    // The labels, each with its leader line under it.
    const auto labelColor = [&](const AnnotationLabel& label) {
        if (label.role == AnnotationLabelRole::Grid)
        {
            // The grid's labels are read, its lines only seen: half-way to the text colour and nearly opaque.
            return withOpacity(blended(colors.grid, colors.text, 0.5), (std::min)(1.0, gridOpacity + 0.5));
        }
        const AnnotationMark& mark = scene.marks[label.source];
        return style.labelsInObjectColor ? annotationMarkColor(colors, mark.kind) : colors.text;
    };
    for (const AnnotationLabel& label : layout.labels)
    {
        if (label.role == AnnotationLabelRole::Object && label.source >= scene.marks.size())
        {
            continue;
        }
        const QColor color = labelColor(label);
        if (label.leader.has_value())
        {
            QPainterPath leader;
            leader.moveTo(label.leader->p1());
            leader.lineTo(label.leader->p2());
            if (lineHalo)
            {
                painter.strokePath(leader, roundPen(markHalo, metrics.lineWidth * 2.4));
            }
            painter.strokePath(leader, roundPen(withOpacity(color, 0.8 * lineOpacity), 0.75 * metrics.lineWidth));
        }
        if (style.labelBacking == AnnotationLabelBacking::Badge)
        {
            const double radius = 0.3 * metrics.fontSize;
            painter.setPen(roundPen(withOpacity(color, 0.45), 0.06 * metrics.fontSize));
            painter.setBrush(colors.panel);
            painter.drawRoundedRect(label.box, radius, radius);
            painter.setBrush(Qt::NoBrush);
        }
        drawText(painter, label.primary, color, style, metrics);
        drawText(painter, label.secondary, withOpacity(color, 0.86), style, metrics);
    }

    // The compass: two arrows from one point, north the longer, with their letters.
    if (layout.compass.has_value())
    {
        const AnnotationCompassLayout& compass = *layout.compass;
        const double shaft = 1.3 * metrics.lineWidth;
        const double headLength = 0.55 * metrics.fontSize;
        const double headHalf = 0.24 * metrics.fontSize;
        QPainterPath arrows;
        for (const QPointF tip : {compass.northTip, compass.eastTip})
        {
            const QPolygonF head = arrowHead(compass.origin, tip, headLength, headHalf);
            const QPointF delta = tip - compass.origin;
            const double norm = std::hypot(delta.x(), delta.y());
            const QPointF shaftEnd = norm > headLength ? tip - delta * (headLength * 0.6 / norm) : tip;
            arrows.moveTo(compass.origin);
            arrows.lineTo(shaftEnd);
            if (!head.isEmpty())
            {
                arrows.addPolygon(head);
                arrows.closeSubpath();
            }
        }
        if (lineHalo)
        {
            painter.strokePath(arrows, roundPen(softHalo, 2.6 * shaft));
        }
        painter.strokePath(arrows, roundPen(colors.decoration, shaft));
        for (const QPointF tip : {compass.northTip, compass.eastTip})
        {
            const QPolygonF head = arrowHead(compass.origin, tip, headLength, headHalf);
            if (!head.isEmpty())
            {
                QPainterPath filled;
                filled.addPolygon(head);
                filled.closeSubpath();
                painter.fillPath(filled, colors.decoration);
            }
        }
        QPainterPath hub;
        hub.addEllipse(compass.origin, 1.4 * shaft, 1.4 * shaft);
        painter.fillPath(hub, colors.decoration);
        drawText(painter, compass.northLetter, colors.decoration, style, metrics);
        drawText(painter, compass.eastLetter, colors.decoration, style, metrics);
    }

    // The scale bar: a bar with a tick at each end, its length above it.
    if (layout.scaleBar.has_value())
    {
        const AnnotationScaleBarLayout& bar = *layout.scaleBar;
        const double half = bar.tickLength * 0.5;
        QPainterPath path;
        path.moveTo(bar.bar.p1());
        path.lineTo(bar.bar.p2());
        path.moveTo(bar.bar.p1() + QPointF(0.0, -half));
        path.lineTo(bar.bar.p1() + QPointF(0.0, half));
        path.moveTo(bar.bar.p2() + QPointF(0.0, -half));
        path.lineTo(bar.bar.p2() + QPointF(0.0, half));
        const double width = 1.5 * metrics.lineWidth;
        if (lineHalo)
        {
            painter.strokePath(path, roundPen(softHalo, 2.4 * width));
        }
        QPen pen(colors.decoration, width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.strokePath(path, pen);
        drawText(painter, bar.label, colors.decoration, style, metrics);
    }

    // The panel, over everything: its box, its title and rule in the accent, its facts in two columns.
    if (layout.panel.has_value())
    {
        const AnnotationPanelLayout& panel = *layout.panel;
        painter.setPen(roundPen(withOpacity(colors.panelText, 0.12), 0.04 * metrics.fontSize));
        painter.setBrush(colors.panel);
        painter.drawRoundedRect(panel.box, panel.radius, panel.radius);
        painter.setBrush(Qt::NoBrush);
        painter.fillPath(panel.title, colors.accent);
        if (panel.rule.has_value())
        {
            painter.setPen(roundPen(withOpacity(colors.accent, 0.55), 0.05 * metrics.fontSize));
            painter.drawLine(*panel.rule);
        }
        painter.fillPath(panel.credit, withOpacity(colors.panelText, 0.85));
        painter.fillPath(panel.factLabels, withOpacity(colors.panelText, 0.6));
        painter.fillPath(panel.factValues, colors.panelText);
    }

    painter.restore();
}

} // namespace an::ui
