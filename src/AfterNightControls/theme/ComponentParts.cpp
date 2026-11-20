// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/ComponentParts.h"

#include "theme/AnDesignTokens.h"
#include "theme/PainterGuard.h"

#include <QFontMetricsF>
#include <QIcon>
#include <QImage>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QString>

#include <algorithm>
#include <cmath>

namespace
{

/// The points a logical pixel is laid out at: Qt 6 presents 96 logical dots per inch on every screen.
constexpr qreal kPointsPerPixel = 72.0 / 96.0;

QPen strokePen(const QColor& colour, const qreal width)
{
    QPen pen(colour, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

} // namespace

namespace an::ui::parts
{

QFont sizedFont(const QFont& base, const int logicalPixels, const QFont::Weight weight)
{
    QFont font = base;
    font.setPointSizeF(logicalPixels * kPointsPerPixel);
    font.setWeight(weight);
    return font;
}

QFont capsFont(const QFont& base, const int logicalPixels)
{
    QFont font = sizedFont(base, logicalPixels, QFont::DemiBold);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, kReadoutCapsTracking * logicalPixels);
    return font;
}

QFont windowHeadingEyebrowFont(const DesignTokens& tokens)
{
    return capsFont(tokens.typography.caption, tokens.metrics.fontCaption);
}

QFont windowHeadingTitleFont(const DesignTokens& tokens)
{
    return sizedFont(tokens.typography.body, kWindowHeadingTitleFont, QFont::DemiBold);
}

QFont windowHeadingDescriptionFont(const DesignTokens& tokens)
{
    return sizedFont(tokens.typography.body, tokens.metrics.fontSmall);
}

int textWidth(const QFont& font, const QString& text)
{
    return static_cast<int>(std::ceil(QFontMetricsF(font).horizontalAdvance(text)));
}

QColor atShare(const QColor& colour, const qreal share)
{
    QColor shared = colour;
    shared.setAlphaF(static_cast<float>(colour.alphaF() * share));
    return shared;
}

QColor contrastingTextColor(const QColor& background, const QColor& preferred, const double minimumRatio)
{
    // The extremes a text colour falls back to when the preferred one does not stand out.
    return bestContrastTextColor(background, preferred, QColor(255, 255, 255), QColor(0, 0, 0), minimumRatio);
}

void paintStateLayer(QPainter& painter, const QRectF& part, const qreal radius, const bool hovered, const bool pressed)
{
    if (!hovered && !pressed)
    {
        return;
    }
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(atShare(tokens().colors.stateLayer, pressed ? kPressedLayer : kHoverLayer));
    painter.drawRoundedRect(part, radius, radius);
}

void paintFocusRing(QPainter& painter, const QRectF& part, const qreal radius)
{
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(tokens().colors.borderFocus, kFocusRingWidth));
    const qreal half = kFocusRingWidth / 2.0;
    const qreal inner = (std::max)(0.0, radius - half);
    painter.drawRoundedRect(part.adjusted(half, half, -half, -half), inner, inner);
}

bool isKeyboardFocusReason(const Qt::FocusReason reason)
{
    return reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason;
}

void paintIcon(QPainter& painter, const QIcon& icon, const QRectF& square, const QColor& colour)
{
    if (icon.isNull() || square.isEmpty())
    {
        return;
    }
    const qreal ratio = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
    const QSize logical(static_cast<int>(std::ceil(square.width())), static_cast<int>(std::ceil(square.height())));
    QImage shape = icon.pixmap(logical, ratio).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter tint(&shape);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(shape.rect(), colour);
    }
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(square, shape);
}

void paintCheckMark(QPainter& painter, const QRectF& square, const QColor& colour, const qreal width)
{
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(strokePen(colour, width));
    const qreal s = square.width();
    QPainterPath path;
    path.moveTo(square.left() + s * 0.18, square.top() + s * 0.52);
    path.lineTo(square.left() + s * 0.41, square.top() + s * 0.75);
    path.lineTo(square.left() + s * 0.84, square.top() + s * 0.28);
    painter.drawPath(path);
}

void paintCrossMark(QPainter& painter, const QRectF& square, const QColor& colour, const qreal width)
{
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(strokePen(colour, width));
    const QRectF inner =
        square.adjusted(square.width() * 0.25, square.height() * 0.25, -square.width() * 0.25, -square.height() * 0.25);
    QPainterPath path;
    path.moveTo(inner.topLeft());
    path.lineTo(inner.bottomRight());
    path.moveTo(inner.topRight());
    path.lineTo(inner.bottomLeft());
    painter.drawPath(path);
}

void paintExclamationMark(QPainter& painter, const QRectF& square, const QColor& colour, const qreal width)
{
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(strokePen(colour, width));
    const qreal x = square.center().x();
    painter.drawLine(QPointF(x, square.top() + square.height() * 0.2),
                     QPointF(x, square.top() + square.height() * 0.58));
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawEllipse(QPointF(x, square.top() + square.height() * 0.8), width * 0.6, width * 0.6);
}

void paintChevronRight(QPainter& painter, const QRectF& square, const QColor& colour, const qreal width)
{
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(strokePen(colour, width));
    const qreal s = square.width();
    QPainterPath path;
    path.moveTo(square.left() + s * 0.38, square.top() + s * 0.22);
    path.lineTo(square.left() + s * 0.66, square.top() + s * 0.5);
    path.lineTo(square.left() + s * 0.38, square.top() + s * 0.78);
    painter.drawPath(path);
}

} // namespace an::ui::parts
