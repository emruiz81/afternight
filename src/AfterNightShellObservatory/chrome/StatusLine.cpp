// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/StatusLine.h"

#include "theme/AnDesignTokens.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>

namespace an::ui::observatory
{

StatusLine::StatusLine(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("observatoryStatusLine"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(0, 1, 0, 0);
    m_row->setSpacing(0);
}

QHBoxLayout* StatusLine::row() const noexcept
{
    return m_row;
}

QSize StatusLine::sizeHint() const
{
    return {QWidget::sizeHint().width(), an::ui::tokens().metrics.statusLine};
}

QSize StatusLine::minimumSizeHint() const
{
    return {0, an::ui::tokens().metrics.statusLine};
}

void StatusLine::paintEvent(QPaintEvent* /*event*/)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    painter.fillRect(rect(), t.layers.surface1);
    // The hairline above the line, in its first row of pixels.
    painter.fillRect(QRect(0, 0, width(), 1), t.colors.borderSubtle);
}

void StatusLine::changeEvent(QEvent* event)
{
    // A change of density resizes the line: its height is the density's.
    if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
    {
        updateGeometry();
        update();
    }
    QWidget::changeEvent(event);
}

} // namespace an::ui::observatory
