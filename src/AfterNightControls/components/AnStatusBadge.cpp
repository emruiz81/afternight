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

/// A soft status colour's share, under the Observatory profile.
constexpr float kSoftShare = 0.16f;
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
    const bool observatory = styleProfile() == StyleProfile::Observatory;
    m_toneColor = toneColor;
    m_bgColor = toneColor;
    m_borderColor = toneColor;
    if (observatory)
    {
        // A tag: the status colour's soft share and no border.
        m_bgColor.setAlphaF(toneColor.alphaF() * kSoftShare);
        m_borderColor = QColor();
    }
    else
    {
        m_bgColor.setAlpha(46);
        m_borderColor.setAlpha(170);
    }
    m_radius = tokenSet.radius.sm;

    QFont font = tokenSet.typography.caption;
    font.setWeight(QFont::DemiBold);
    setFont(font);

    // Text color via palette
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, toneColor);
    setPalette(pal);

    // Padding via content margins (vertical=xs, horizontal=sm); a tag's height is its token's.
    setContentsMargins(tokenSet.spacing.sm, observatory ? 0 : tokenSet.spacing.xs, tokenSet.spacing.sm,
                       observatory ? 0 : tokenSet.spacing.xs);
    updateGeometry();
    update();
}

QSize AnStatusBadge::sizeHint() const
{
    QSize size = QLabel::sizeHint();
    if (styleProfile() == StyleProfile::Observatory)
    {
        size.setHeight(tokens().metrics.tag);
    }
    return size;
}

QSize AnStatusBadge::minimumSizeHint() const
{
    QSize size = QLabel::minimumSizeHint();
    if (styleProfile() == StyleProfile::Observatory)
    {
        size.setHeight(tokens().metrics.tag);
    }
    return size;
}

void AnStatusBadge::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setBrush(m_bgColor);
    if (m_borderColor.isValid())
    {
        painter.setPen(QPen(m_borderColor, 1));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
    }
    else
    {
        // A tag with no border fills its whole rectangle.
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(rect()), m_radius, m_radius);
    }

    // Draw text
    painter.setPen(m_toneColor);
    painter.setFont(font());
    painter.drawText(contentsRect(), alignment(), text());
}

} // namespace an::ui
