// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnIconButton.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QIcon>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnIconButton)

namespace an::ui
{

AnIconButton::AnIconButton(const QString& iconKey, const QString& text, QWidget* parent) : QAbstractButton(parent)
{
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover, true);
    setIconKey(iconKey);
    setDescription(text);
}

QString AnIconButton::iconKey() const
{
    return m_iconKey;
}

void AnIconButton::setIconKey(const QString& key)
{
    m_iconKey = key;
    setIcon(key.isEmpty() ? QIcon() : QIcon(QStringLiteral(":/AfterNight/icons/SVG/%1.svg").arg(key)));
    update();
}

QSize AnIconButton::sizeHint() const
{
    const int side = tokens().metrics.controlXSmall;
    return QSize(side, side);
}

QSize AnIconButton::minimumSizeHint() const
{
    return sizeHint();
}

void AnIconButton::setDescription(const QString& text)
{
    setText(text);
    setToolTip(text);
    setAccessibleName(text);
}

void AnIconButton::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled())
    {
        painter.setOpacity(parts::kDisabledOpacity);
    }

    const QRectF bounds(rect());
    const bool on = isCheckable() && isChecked();
    if (on)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.colors.accentContainer);
        painter.drawRoundedRect(bounds, parts::kIconButtonRadius, parts::kIconButtonRadius);
    }
    parts::paintStateLayer(painter, bounds, parts::kIconButtonRadius, m_hovered && isEnabled(), isDown());
    if (m_keyboardFocus && isEnabled())
    {
        parts::paintFocusRing(painter, bounds, parts::kIconButtonRadius);
    }

    if (!icon().isNull())
    {
        const QColor colour =
            on ? t.colors.textOnAccentContainer : (m_hovered ? t.colors.textPrimary : t.colors.textSecondary);
        const qreal side = parts::kIconButtonIcon;
        parts::paintIcon(painter, icon(), QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side), colour);
    }
}

void AnIconButton::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    QAbstractButton::focusInEvent(event);
    update();
}

void AnIconButton::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QAbstractButton::focusOutEvent(event);
    update();
}

void AnIconButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    QAbstractButton::enterEvent(event);
    update();
}

void AnIconButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    QAbstractButton::leaveEvent(event);
    update();
}

} // namespace an::ui
