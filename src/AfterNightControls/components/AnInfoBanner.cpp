// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnInfoBanner.h"

#include "theme/AnDesignTokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QSizePolicy>

namespace
{

/// Under the Observatory profile: the tone's soft share, and the width of the tone's bar at the note's start.
constexpr float kSoftShare = 0.16f;
constexpr qreal kToneBar = 3.0;
QColor toneColorFor(const an::ui::AnInfoBanner::Tone tone, const an::ui::DesignTokens& tokenSet)
{
    switch (tone)
    {
    case an::ui::AnInfoBanner::Tone::Success:
        return tokenSet.colors.statusSuccess;
    case an::ui::AnInfoBanner::Tone::Warning:
        return tokenSet.colors.statusWarning;
    case an::ui::AnInfoBanner::Tone::Error:
        return tokenSet.colors.statusError;
    case an::ui::AnInfoBanner::Tone::Info:
    default:
        return tokenSet.colors.statusInfo;
    }
}

} // anonymous namespace

namespace an::ui
{

AnInfoBanner::AnInfoBanner(const QString& message, const Tone tone, QWidget* parent) : QFrame(parent), m_tone(tone)
{
    setObjectName(QStringLiteral("anInfoBanner"));
    setFrameShape(QFrame::NoFrame);
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setObjectName(QStringLiteral("anInfoBannerMessage"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setMinimumWidth(0);
    m_messageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    rootLayout->addWidget(m_messageLabel, 1);

    setMessage(message);
    refreshTheme();
}

void AnInfoBanner::setMessage(const QString& message)
{
    if (!m_messageLabel)
    {
        return;
    }

    m_messageLabel->setText(message.trimmed());
}

QString AnInfoBanner::message() const
{
    return m_messageLabel ? m_messageLabel->text() : QString();
}

QLabel* AnInfoBanner::messageLabel() const
{
    return m_messageLabel;
}

void AnInfoBanner::setTone(const Tone tone)
{
    if (m_tone == tone)
    {
        return;
    }

    m_tone = tone;
    refreshTheme();
}

AnInfoBanner::Tone AnInfoBanner::tone() const
{
    return m_tone;
}

void AnInfoBanner::refreshTheme()
{
    if (!m_messageLabel)
    {
        return;
    }

    refreshTokens();
    const auto& tokenSet = tokens();
    const QColor toneColor = toneColorFor(m_tone, tokenSet);
    const bool observatory = styleProfile() == StyleProfile::Observatory;
    m_bgColor = toneColor;
    m_borderColor = toneColor;
    if (observatory)
    {
        // A note: the tone's soft share, no border, a bar of the tone at its start; its message in the primary text.
        m_bgColor.setAlphaF(toneColor.alphaF() * kSoftShare);
        m_borderColor = QColor();
    }
    else
    {
        m_bgColor.setAlpha(40);
        m_borderColor.setAlpha(180);
    }
    m_radius = tokenSet.radius.md;

    if (auto* rootLayout = qobject_cast<QHBoxLayout*>(layout()))
    {
        if (observatory)
        {
            rootLayout->setContentsMargins(tokenSet.metrics.pad, tokenSet.metrics.gap, tokenSet.metrics.pad,
                                           tokenSet.metrics.gap);
        }
        else
        {
            rootLayout->setContentsMargins(tokenSet.spacing.sm, tokenSet.spacing.sm, tokenSet.spacing.sm,
                                           tokenSet.spacing.sm);
        }
    }

    m_messageLabel->setFont(tokenSet.typography.body);

    // Message color via palette
    QPalette msgPal = m_messageLabel->palette();
    msgPal.setColor(QPalette::WindowText, observatory ? tokenSet.colors.textPrimary : toneColor);
    m_messageLabel->setPalette(msgPal);

    update();
}

void AnInfoBanner::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto& tokenSet = tokens();
    const QColor bg = m_bgColor.isValid() ? m_bgColor : tokenSet.colors.statusInfo;
    const QColor border = m_borderColor.isValid() ? m_borderColor : tokenSet.colors.statusInfo;
    const int r = m_radius > 0 ? m_radius : tokenSet.radius.md;

    if (!m_borderColor.isValid())
    {
        // The Observatory profile's note: the fill and, clipped to its shape, the tone's bar at its start.
        const QRectF bounds(rect());
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(bounds, r, r);
        QPainterPath shape;
        shape.addRoundedRect(bounds, r, r);
        painter.setClipPath(shape);
        const QRectF bar = layoutDirection() == Qt::RightToLeft
                               ? QRectF(bounds.right() - kToneBar, bounds.top(), kToneBar, bounds.height())
                               : QRectF(bounds.left(), bounds.top(), kToneBar, bounds.height());
        painter.fillRect(bar, toneColorFor(m_tone, tokenSet));
        return;
    }
    painter.setPen(QPen(border, 1));
    painter.setBrush(bg);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
}

} // namespace an::ui
