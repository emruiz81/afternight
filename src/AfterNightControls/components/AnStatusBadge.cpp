// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnStatusBadge.h"

#include "theme/AnDesignTokens.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QSizePolicy>

namespace
{

QColor toneForStatus(const an::ui::DesignTokens& tokenSet, const an::ui::AnStatusBadge::Status status)
{
    switch (status)
    {
    case an::ui::AnStatusBadge::Status::Success:
        return tokenSet.colors.statusSuccess;
    case an::ui::AnStatusBadge::Status::Warning:
        return tokenSet.colors.statusWarning;
    case an::ui::AnStatusBadge::Status::Error:
        return tokenSet.colors.statusError;
    case an::ui::AnStatusBadge::Status::Info:
    default:
        return tokenSet.colors.statusInfo;
    }
}

} // anonymous namespace

namespace an::ui
{

AnStatusBadge::AnStatusBadge(const QString& text, const Status status, QWidget* parent)
    : QLabel(text, parent), m_status(status)
{
    setObjectName(QStringLiteral("anStatusBadge"));
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    refreshTheme();
}

void AnStatusBadge::setStatus(const Status status)
{
    if (m_status == status)
    {
        return;
    }

    m_status = status;
    refreshTheme();
}

AnStatusBadge::Status AnStatusBadge::status() const
{
    return m_status;
}

void AnStatusBadge::refreshTheme()
{
    refreshTokens();
    const auto& tokenSet = tokens();
    const QColor toneColor = toneForStatus(tokenSet, m_status);

    m_toneColor = toneColor;

    m_bgColor = toneColor;
    m_bgColor.setAlpha(46);

    m_borderColor = toneColor;
    m_borderColor.setAlpha(170);

    m_radius = tokenSet.radius.sm;

    QFont font = tokenSet.typography.caption;
    font.setWeight(QFont::DemiBold);
    setFont(font);

    // Text color via palette
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, toneColor);
    setPalette(pal);

    // Padding via content margins (vertical=xs, horizontal=sm)
    setContentsMargins(tokenSet.spacing.sm, tokenSet.spacing.xs, tokenSet.spacing.sm, tokenSet.spacing.xs);

    update();
}

void AnStatusBadge::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(QPen(m_borderColor, 1));
    painter.setBrush(m_bgColor);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);

    // Draw text
    painter.setPen(m_toneColor);
    painter.setFont(font());
    painter.drawText(contentsRect(), alignment(), text());
}

} // namespace an::ui
