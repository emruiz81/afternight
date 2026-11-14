// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/settings/ThemePresetTile.h"

#include <QPainter>
#include <QPainterPath>

#include <optional>

namespace an::ui
{

namespace
{

/// The tokens a preset resolves to in the theme, contrast profile, density and Night in force: what the tile shows.
DesignTokens presetTokens(const ThemePreset preset)
{
    ThemePresetParts parts{
        .accent = themeAccentFamily(), .tint = themeSurfaceTintFamily(), .strength = themeTintStrength()};
    if (const std::optional<ThemePresetParts> named = themePresetParts(preset); named.has_value())
    {
        parts = *named;
    }
    return resolveDesignTokens(ThemeChoices{.theme = isDarkTheme() ? ThemeVariant::Dark : ThemeVariant::Light,
                                            .contrast = themeContrastProfile(),
                                            .accent = parts.accent,
                                            .tint = parts.tint,
                                            .tintStrength = parts.strength,
                                            .profile = StyleProfile::Observatory,
                                            .density = themeDensity(),
                                            .night = isNightMode()});
}

} // namespace

ThemePresetTile::ThemePresetTile(const ThemePreset preset, const QString& name, QWidget* parent)
    : QAbstractButton(parent), m_preset(preset)
{
    setText(name);
    setCheckable(true);
    setAccessibleName(name);
    setToolTip(name);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
}

QSize ThemePresetTile::sizeHint() const
{
    // A miniature two controls high and three wide, with the name's row under it.
    const MetricTokens& metrics = tokens().metrics;
    return {metrics.control * 3, metrics.control * 2 + metrics.rowDense};
}

QSize ThemePresetTile::minimumSizeHint() const
{
    return sizeHint();
}

void ThemePresetTile::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& shown = tokens();
    const DesignTokens preset = presetTokens(m_preset);
    const MetricTokens& metrics = shown.metrics;
    const SpacingTokens& spacing = shown.spacing;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The miniature: the window's surface, a rail down its side, a card with an accent pill and two lines of text.
    const QRectF miniature(1.5, 1.5, width() - 3.0, height() - metrics.rowDense - 3.0);
    const qreal radius = shown.radius.md;
    QPainterPath frame;
    frame.addRoundedRect(miniature, radius, radius);
    painter.fillPath(frame, preset.layers.surface0);
    painter.save();
    painter.setClipPath(frame);
    const qreal rail = metrics.controlXSmall * 0.75;
    painter.fillRect(QRectF(miniature.left(), miniature.top(), rail, miniature.height()), preset.layers.surface1);
    const QRectF card = miniature.adjusted(rail + spacing.sm, spacing.sm, -spacing.sm, -spacing.sm);
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, shown.radius.sm, shown.radius.sm);
    painter.fillPath(cardPath, preset.layers.surface2);
    const qreal line = spacing.xs;
    const QRectF pill(card.left() + spacing.sm, card.bottom() - spacing.sm - line * 2.0, card.width() * 0.4,
                      line * 2.0);
    QPainterPath pillPath;
    pillPath.addRoundedRect(pill, line, line);
    painter.fillPath(pillPath, preset.colors.accentPrimary);
    painter.fillRect(QRectF(card.left() + spacing.sm, card.top() + spacing.sm, card.width() * 0.6, line),
                     preset.colors.textPrimary);
    painter.fillRect(QRectF(card.left() + spacing.sm, card.top() + spacing.sm + line * 2.0, card.width() * 0.45, line),
                     preset.colors.textSecondary);
    painter.restore();

    // The tile's border: the accent in force when it is the preset chosen, the outline when hovered, the hairline
    // otherwise; the focus ring when it has the keyboard's focus.
    QColor border = shown.colors.borderSubtle;
    qreal borderWidth = 1.0;
    if (isChecked())
    {
        border = shown.colors.accentPrimary;
        borderWidth = 2.0;
    }
    else if (underMouse())
    {
        border = shown.colors.borderPrimary;
    }
    if (hasFocus())
    {
        border = shown.colors.borderFocus;
        borderWidth = 2.0;
    }
    painter.setPen(QPen(border, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(frame);

    // The name, under the miniature.
    QFont font = shown.typography.body;
    font.setWeight(isChecked() ? QFont::DemiBold : QFont::Normal);
    painter.setFont(font);
    painter.setPen(isEnabled() ? shown.colors.textPrimary : shown.colors.textTertiary);
    painter.drawText(QRectF(0.0, height() - metrics.rowDense, width(), metrics.rowDense), Qt::AlignCenter,
                     QFontMetrics(font).elidedText(text(), Qt::ElideRight, width()));
}

} // namespace an::ui
