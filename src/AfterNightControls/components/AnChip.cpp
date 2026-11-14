// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnChip.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnChip)

namespace
{

QFont textFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontCaption, QFont::DemiBold);
}

QFont countFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
}

} // namespace

namespace an::ui
{

AnChip::AnChip(const QString& text, QWidget* parent) : QAbstractButton(parent)
{
    setText(text);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover, true);
}

void AnChip::setCountText(const QString& count)
{
    if (m_count == count)
    {
        return;
    }
    m_count = count;
    setAccessibleDescription(count);
    updateGeometry();
    update();
}

QString AnChip::countText() const
{
    return m_count;
}

QSize AnChip::sizeHint() const
{
    const DesignTokens& t = tokens();
    const bool withIcon = !icon().isNull();
    int width = (withIcon ? parts::kChipPaddingBeforeIcon + parts::kChipIcon + parts::kChipGap : parts::kChipPadding) +
                parts::textWidth(textFont(t), text()) + parts::kChipPadding;
    if (!m_count.isEmpty())
    {
        width += parts::kChipGap + parts::textWidth(countFont(t), m_count);
    }
    return QSize(width, t.metrics.chip);
}

QSize AnChip::minimumSizeHint() const
{
    return sizeHint();
}

void AnChip::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled())
    {
        painter.setOpacity(parts::kDisabledOpacity);
    }
    const QRectF bounds(rect());
    const qreal radius = bounds.height() / 2.0;
    const bool on = isCheckable() && isChecked();
    if (on)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.colors.accentContainer);
        painter.drawRoundedRect(bounds, radius, radius);
    }
    parts::paintStateLayer(painter, bounds, radius, m_hovered && isEnabled(), isDown());
    if (m_keyboardFocus && isEnabled())
    {
        parts::paintFocusRing(painter, bounds, radius);
    }
    else if (!on)
    {
        painter.setPen(QPen(t.colors.borderPrimary, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    }

    const QColor textColour = on ? t.colors.textOnAccentContainer : t.colors.textPrimary;
    qreal x = parts::kChipPadding;
    if (!icon().isNull())
    {
        x = parts::kChipPaddingBeforeIcon;
        const qreal top = (height() - parts::kChipIcon) / 2.0;
        parts::paintIcon(painter, icon(), QRectF(x, top, parts::kChipIcon, parts::kChipIcon), textColour);
        x += parts::kChipIcon + parts::kChipGap;
    }
    const QFont font = textFont(t);
    const int textWidth = parts::textWidth(font, text());
    painter.setPen(textColour);
    painter.setFont(font);
    painter.drawText(QRectF(x, 0, textWidth, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
    if (!m_count.isEmpty())
    {
        x += textWidth + parts::kChipGap;
        const QFont count = countFont(t);
        painter.setPen(on ? t.colors.textOnAccentContainer : t.colors.textSecondary);
        painter.setFont(count);
        painter.drawText(QRectF(x, 0, parts::textWidth(count, m_count), height()), Qt::AlignLeft | Qt::AlignVCenter,
                         m_count);
    }
}

void AnChip::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->isAutoRepeat())
    {
        click();
        event->accept();
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

void AnChip::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    QAbstractButton::focusInEvent(event);
    update();
}

void AnChip::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QAbstractButton::focusOutEvent(event);
    update();
}

void AnChip::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    QAbstractButton::enterEvent(event);
    update();
}

void AnChip::leaveEvent(QEvent* event)
{
    m_hovered = false;
    QAbstractButton::leaveEvent(event);
    update();
}

} // namespace an::ui
