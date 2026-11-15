// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnActionButton.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QAction>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnActionButton)

namespace an::ui
{

AnActionButton::AnActionButton(QAction* action, QWidget* parent) : QAbstractButton(parent)
{
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover, true);
    QObject::connect(this, &QAbstractButton::clicked, this, [this]() {
        if (m_action)
        {
            m_action->trigger();
            // A checkable action the trigger left as it was - refused - keeps the button as the action is.
            follow();
        }
    });
    setAction(action);
}

AnActionButton::~AnActionButton() = default;

void AnActionButton::setAction(QAction* action)
{
    QObject::disconnect(m_changed);
    QObject::disconnect(m_toggled);
    QObject::disconnect(m_gone);
    m_action = action;
    if (action != nullptr)
    {
        m_changed = QObject::connect(action, &QAction::changed, this, [this]() { follow(); });
        m_toggled = QObject::connect(action, &QAction::toggled, this, [this]() { follow(); });
        m_gone = QObject::connect(action, &QObject::destroyed, this, [this]() { follow(); });
    }
    follow();
}

QAction* AnActionButton::action() const
{
    return m_action.data();
}

void AnActionButton::setTone(const Tone tone)
{
    m_tone = tone;
    update();
}

AnActionButton::Tone AnActionButton::tone() const noexcept
{
    return m_tone;
}

void AnActionButton::setSides(const int side, const int iconSide)
{
    m_side = side;
    m_iconSide = iconSide;
    updateGeometry();
    update();
}

QSize AnActionButton::sizeHint() const
{
    const int side = m_side > 0 ? m_side : tokens().metrics.controlSmall;
    return QSize(side, side);
}

QSize AnActionButton::minimumSizeHint() const
{
    return sizeHint();
}

void AnActionButton::follow()
{
    QAction* action = m_action.data();
    if (action == nullptr)
    {
        setVisible(false);
        return;
    }
    setText(action->text());
    setToolTip(action->toolTip());
    setAccessibleName(action->text());
    setIcon(action->icon());
    setCheckable(action->isCheckable());
    if (action->isCheckable())
    {
        setChecked(action->isChecked());
    }
    setEnabled(action->isEnabled());
    setVisible(action->isVisible());
    update();
}

void AnActionButton::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!isEnabled())
    {
        painter.setOpacity(parts::kDisabledOpacity);
    }

    const bool overImage = m_tone == Tone::OnImage;
    const QRectF bounds(rect());
    const qreal radius = overImage ? parts::kIconButtonRadius : t.radius.sm;
    const bool on = isCheckable() && isChecked();
    const bool hovered = m_hovered && isEnabled();
    if (on)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(overImage ? t.colors.accentOnImage : t.colors.accentContainer);
        painter.drawRoundedRect(bounds, radius, radius);
    }
    if (overImage)
    {
        // Over a scrim the state is the text over images, laid at the state layer's shares.
        if (hovered || isDown())
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(
                parts::atShare(t.colors.textOnImage, isDown() ? parts::kPressedLayer : parts::kHoverLayer));
            painter.drawRoundedRect(bounds, radius, radius);
        }
    }
    else
    {
        parts::paintStateLayer(painter, bounds, radius, hovered, isDown());
    }
    if (m_keyboardFocus && isEnabled())
    {
        parts::paintFocusRing(painter, bounds, radius);
    }

    if (!icon().isNull())
    {
        QColor colour;
        if (overImage)
        {
            colour = on ? t.colors.textOnAccentImage : (hovered ? t.colors.textOnImage : t.colors.textOnImageSecondary);
        }
        else
        {
            colour = on ? t.colors.textOnAccentContainer : (hovered ? t.colors.textPrimary : t.colors.textSecondary);
        }
        const qreal side = m_iconSide;
        parts::paintIcon(painter, icon(), QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side), colour);
    }
}

void AnActionButton::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    QAbstractButton::focusInEvent(event);
    update();
}

void AnActionButton::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QAbstractButton::focusOutEvent(event);
    update();
}

void AnActionButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    QAbstractButton::enterEvent(event);
    update();
}

void AnActionButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    QAbstractButton::leaveEvent(event);
    update();
}

} // namespace an::ui
