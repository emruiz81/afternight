// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/settings/ThemeSwatch.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QPainter>
#include <QPainterPath>

#include <utility>

namespace an::ui
{

namespace
{

/// The swatch's outline inside a rectangle: a disc, or a square with the small parts' corners.
QPainterPath outlineOf(const ThemeSwatch::Shape shape, const QRectF& box)
{
    QPainterPath path;
    if (shape == ThemeSwatch::Shape::Disc)
    {
        path.addEllipse(box);
    }
    else
    {
        const qreal radius = tokens().radius.sm;
        path.addRoundedRect(box, radius, radius);
    }
    return path;
}

} // namespace

ThemeSwatch::ThemeSwatch(const Shape shape, const QString& name, std::function<QColor()> colour, QWidget* parent)
    : QAbstractButton(parent), m_shape(shape), m_colour(std::move(colour))
{
    setText(name);
    setCheckable(true);
    setAccessibleName(name);
    setToolTip(name);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QColor ThemeSwatch::colour() const
{
    return m_colour ? m_colour() : tokens().layers.surface2;
}

QSize ThemeSwatch::sizeHint() const
{
    return {parts::kSwatchSize, parts::kSwatchSize};
}

QSize ThemeSwatch::minimumSizeHint() const
{
    return sizeHint();
}

bool ThemeSwatch::hitButton(const QPoint& position) const
{
    return rect().contains(position);
}

void ThemeSwatch::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& shown = tokens();
    const QColor fill = colour();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled())
    {
        painter.setOpacity(parts::kDisabledOpacity);
    }

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QPainterPath outline = outlineOf(m_shape, box);
    painter.fillPath(outline, fill);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(underMouse() ? shown.colors.borderPrimary : shown.colors.borderSubtle, 1.0));
    painter.drawPath(outline);

    const qreal ring = parts::kSwatchRing;
    if (hasFocus())
    {
        painter.setPen(QPen(shown.colors.borderFocus, ring));
        painter.drawPath(outlineOf(m_shape, box.adjusted(ring / 2.0, ring / 2.0, -ring / 2.0, -ring / 2.0)));
    }
    if (!isChecked())
    {
        return;
    }

    // Chosen: a ring of the primary text inside the edge, and a check in the colour that reads on the fill.
    const qreal inset = ring * 1.5 + (hasFocus() ? ring : 0.0);
    painter.setPen(QPen(shown.colors.textPrimary, ring));
    painter.drawPath(outlineOf(m_shape, box.adjusted(inset, inset, -inset, -inset)));

    const QColor mark =
        bestContrastTextColor(fill, shown.colors.textPrimary, shown.colors.textOnAccent, shown.layers.surface0);
    const qreal side = parts::kSwatchCheck;
    const QRectF check(QPointF(width() / 2.0 - side / 2.0, height() / 2.0 - side / 2.0), QSizeF(side, side));
    QPen pen(mark, ring);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    const QPointF points[] = {
        {check.left() + side * 0.18, check.top() + side * 0.52},
        {check.left() + side * 0.42, check.top() + side * 0.76},
        {check.left() + side * 0.84, check.top() + side * 0.28},
    };
    painter.drawPolyline(points, 3);
}

} // namespace an::ui
