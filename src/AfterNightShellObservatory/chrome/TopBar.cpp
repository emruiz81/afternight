// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/TopBar.h"

#include "theme/AnDesignTokens.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>

namespace an::ui::observatory
{

TopBar::TopBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("observatoryTopBar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(0, 0, 0, 1);
    m_row->setSpacing(0);
}

QHBoxLayout* TopBar::row() const noexcept
{
    return m_row;
}

QSize TopBar::sizeHint() const
{
    return {QWidget::sizeHint().width(), an::ui::tokens().metrics.barTop};
}

QSize TopBar::minimumSizeHint() const
{
    return {0, an::ui::tokens().metrics.barTop};
}

void TopBar::paintEvent(QPaintEvent* /*event*/)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    painter.fillRect(rect(), t.layers.surface1);
    // The hairline under the bar, in its last row of pixels.
    painter.fillRect(QRect(0, height() - 1, width(), 1), t.colors.borderSubtle);
}

void TopBar::changeEvent(QEvent* event)
{
    // A change of density resizes the bar: its height is the density's.
    if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
    {
        updateGeometry();
        update();
    }
    QWidget::changeEvent(event);
}

} // namespace an::ui::observatory
