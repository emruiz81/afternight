// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnSwitch.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnSwitch)

namespace
{

QFont labelFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontBody);
}

} // namespace

namespace an::ui
{

AnSwitch::AnSwitch(const QString& text, QWidget* parent) : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover, true);
}

QSize AnSwitch::sizeHint() const
{
    const DesignTokens& t = tokens();
    int width = parts::kSwitchWidth;
    if (!text().isEmpty())
    {
        width += parts::kSwitchLabelGap + parts::textWidth(labelFont(t), text());
    }
    return QSize(width, t.metrics.controlXSmall);
}

QSize AnSwitch::minimumSizeHint() const
{
    return sizeHint();
}

void AnSwitch::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled())
    {
        painter.setOpacity(parts::kDisabledOpacity);
    }
    const qreal top = (height() - parts::kSwitchHeight) / 2.0;
    const QRectF track(0, top, parts::kSwitchWidth, parts::kSwitchHeight);
    const qreal radius = track.height() / 2.0;
    const bool on = isChecked();

    painter.setPen(Qt::NoPen);
    painter.setBrush(on ? t.colors.accentPrimary : t.layers.surface4);
    painter.drawRoundedRect(track, radius, radius);
    if (!on)
    {
        const qreal half = parts::kSwitchBorder / 2.0;
        painter.setPen(QPen(t.colors.borderPrimary, parts::kSwitchBorder));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(half, half, -half, -half), radius - half, radius - half);
    }
    parts::paintStateLayer(painter, track, radius, m_hovered && isEnabled(), isDown());
    if (m_keyboardFocus && isEnabled())
    {
        parts::paintFocusRing(painter, track, radius);
    }

    // The knob sits at the end its state names, its centre a half track's height in from that end.
    const qreal knob = on ? parts::kSwitchKnobOn : parts::kSwitchKnobOff;
    const QPointF centre(on ? track.right() - radius : track.left() + radius, track.center().y());
    painter.setPen(Qt::NoPen);
    painter.setBrush(on ? t.colors.textOnAccent : t.colors.textSecondary);
    painter.drawEllipse(centre, knob / 2.0, knob / 2.0);
    if (on)
    {
        const qreal check = knob - 2 * parts::kSwitchBorder;
        parts::paintCheckMark(painter, QRectF(centre.x() - check / 2.0, centre.y() - check / 2.0, check, check),
                              t.colors.accentPrimary, parts::kGlyphStroke);
    }

    if (!text().isEmpty())
    {
        const qreal x = parts::kSwitchWidth + parts::kSwitchLabelGap;
        painter.setPen(t.colors.textPrimary);
        painter.setFont(labelFont(t));
        painter.drawText(QRectF(x, 0, width() - x, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
    }
}

void AnSwitch::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->isAutoRepeat())
    {
        click();
        event->accept();
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

void AnSwitch::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    QAbstractButton::focusInEvent(event);
    update();
}

void AnSwitch::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QAbstractButton::focusOutEvent(event);
    update();
}

void AnSwitch::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    QAbstractButton::enterEvent(event);
    update();
}

void AnSwitch::leaveEvent(QEvent* event)
{
    m_hovered = false;
    QAbstractButton::leaveEvent(event);
    update();
}

bool AnSwitch::hitButton(const QPoint& position) const
{
    return rect().contains(position);
}

} // namespace an::ui
