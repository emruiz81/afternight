// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnTag.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnTag)

namespace
{

/// The fill and the text colour of a tone, in the tokens in force.
struct ToneColours
{
    QColor fill;
    QColor text;
};

ToneColours toneColours(const an::ui::AnTag::Tone tone, const an::ui::DesignTokens& t)
{
    using Tone = an::ui::AnTag::Tone;
    switch (tone)
    {
    case Tone::Accent:
        return {t.colors.accentContainer, t.colors.textOnAccentContainer};
    case Tone::Success:
        return {t.colors.statusSuccessSoft, t.colors.statusSuccess};
    case Tone::Warning:
        return {t.colors.statusWarningSoft, t.colors.statusWarning};
    case Tone::Error:
        return {t.colors.statusErrorSoft, t.colors.statusError};
    case Tone::Info:
        return {t.colors.statusInfoSoft, t.colors.statusInfo};
    case Tone::Neutral:
    default:
        return {t.layers.surface4, t.colors.textSecondary};
    }
}

QFont tagFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontCaption, QFont::DemiBold);
}

} // namespace

namespace an::ui
{

AnTag::AnTag(const QString& text, const Tone tone, QWidget* parent) : QLabel(text, parent), m_tone(tone)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void AnTag::setTone(const Tone tone)
{
    if (m_tone == tone)
    {
        return;
    }
    m_tone = tone;
    update();
}

AnTag::Tone AnTag::tone() const noexcept
{
    return m_tone;
}

void AnTag::setIcon(const QIcon& icon)
{
    m_icon = icon;
    updateGeometry();
    update();
}

QIcon AnTag::icon() const
{
    return m_icon;
}

QSize AnTag::sizeHint() const
{
    const DesignTokens& t = tokens();
    int width = 2 * parts::kTagPadding + parts::textWidth(tagFont(t), text());
    if (!m_icon.isNull())
    {
        width += parts::kTagIcon + parts::kTagGap;
    }
    return QSize(width, t.metrics.tag);
}

QSize AnTag::minimumSizeHint() const
{
    return sizeHint();
}

void AnTag::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    const ToneColours colours = toneColours(m_tone, t);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds(rect());
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.fill);
    painter.drawRoundedRect(bounds, parts::kTagRadius, parts::kTagRadius);
    if (m_tone == Tone::Warning)
    {
        // A warning is outlined in its colour, so it is never told by its hue alone.
        painter.setPen(QPen(colours.text, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), parts::kTagRadius - 0.5,
                                parts::kTagRadius - 0.5);
    }

    qreal x = parts::kTagPadding;
    if (!m_icon.isNull())
    {
        const qreal top = (height() - parts::kTagIcon) / 2.0;
        parts::paintIcon(painter, m_icon, QRectF(x, top, parts::kTagIcon, parts::kTagIcon), colours.text);
        x += parts::kTagIcon + parts::kTagGap;
    }
    painter.setPen(colours.text);
    painter.setFont(tagFont(t));
    painter.drawText(QRectF(x, 0, width() - x - parts::kTagPadding, height()), Qt::AlignLeft | Qt::AlignVCenter,
                     text());
}

} // namespace an::ui
