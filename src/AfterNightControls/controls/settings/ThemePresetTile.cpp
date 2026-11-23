// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/settings/ThemePresetTile.h"

#include <QPainter>
#include <QPainterPath>

#include <functional>
#include <optional>

namespace an::ui
{

namespace
{

/// The tokens a preset's parts resolve to in a theme, with the contrast profile, density and Night in force.
DesignTokens partsTokens(const ThemePresetParts& parts, const ThemeVariant theme)
{
    return resolveDesignTokens(ThemeChoices{.theme = theme,
                                            .contrast = themeContrastProfile(),
                                            .accent = parts.accent,
                                            .tint = parts.tint,
                                            .tintStrength = parts.strength,
                                            .density = themeDensity(),
                                            .night = isNightMode()});
}

/// The parts in force.
ThemePresetParts partsInForce()
{
    return ThemePresetParts{
        .accent = themeAccentFamily(), .tint = themeSurfaceTintFamily(), .strength = themeTintStrength()};
}

/// The tokens a preset resolves to in the theme, contrast profile, density and Night in force: what its tile shows.
DesignTokens presetTokens(const ThemePreset preset)
{
    const std::optional<ThemePresetParts> named = themePresetParts(preset);
    return partsTokens(named.has_value() ? *named : partsInForce(),
                       isDarkTheme() ? ThemeVariant::Dark : ThemeVariant::Light);
}

/// A tile's miniature and its frame: the room under the name's row.
QRectF miniatureOf(const QAbstractButton& tile)
{
    return QRectF(1.5, 1.5, tile.width() - 3.0, tile.height() - tokens().metrics.rowDense - 3.0);
}

/// The miniature of a window in a set of tokens, inside a rectangle the painter is clipped to: the window's surface, a
/// rail down its side, a card with an accent pill and two lines of text.
void paintWindow(QPainter& painter, const QRectF& miniature, const DesignTokens& painted)
{
    const DesignTokens& shown = tokens();
    const MetricTokens& metrics = shown.metrics;
    const SpacingTokens& spacing = shown.spacing;
    painter.fillRect(miniature, painted.layers.surface0);
    const qreal rail = metrics.controlXSmall * 0.75;
    painter.fillRect(QRectF(miniature.left(), miniature.top(), rail, miniature.height()), painted.layers.surface1);
    const QRectF card = miniature.adjusted(rail + spacing.sm, spacing.sm, -spacing.sm, -spacing.sm);
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, shown.radius.sm, shown.radius.sm);
    painter.fillPath(cardPath, painted.layers.surface2);
    const qreal line = spacing.xs;
    const QRectF pill(card.left() + spacing.sm, card.bottom() - spacing.sm - line * 2.0, card.width() * 0.4,
                      line * 2.0);
    QPainterPath pillPath;
    pillPath.addRoundedRect(pill, line, line);
    painter.fillPath(pillPath, painted.colors.accentPrimary);
    painter.fillRect(QRectF(card.left() + spacing.sm, card.top() + spacing.sm, card.width() * 0.6, line),
                     painted.colors.textPrimary);
    painter.fillRect(QRectF(card.left() + spacing.sm, card.top() + spacing.sm + line * 2.0, card.width() * 0.45, line),
                     painted.colors.textSecondary);
}

/// Paints a tile: its miniature, clipped to the frame's rounded corners, by `paintMiniature`; the frame - the accent in
/// force when the tile is checked, the outline when hovered, the hairline otherwise, the focus ring when it has the
/// keyboard's focus; and the name under it.
void paintTile(QAbstractButton& tile, const std::function<void(QPainter&, const QRectF&)>& paintMiniature)
{
    const DesignTokens& shown = tokens();
    const MetricTokens& metrics = shown.metrics;

    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF miniature = miniatureOf(tile);
    const qreal radius = shown.radius.md;
    QPainterPath frame;
    frame.addRoundedRect(miniature, radius, radius);
    painter.save();
    painter.setClipPath(frame);
    paintMiniature(painter, miniature);
    painter.restore();

    QColor border = shown.colors.borderSubtle;
    qreal borderWidth = 1.0;
    if (tile.isChecked())
    {
        border = shown.colors.accentPrimary;
        borderWidth = 2.0;
    }
    else if (tile.underMouse())
    {
        border = shown.colors.borderPrimary;
    }
    if (tile.hasFocus())
    {
        border = shown.colors.borderFocus;
        borderWidth = 2.0;
    }
    painter.setPen(QPen(border, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(frame);

    QFont font = shown.typography.body;
    font.setWeight(tile.isChecked() ? QFont::DemiBold : QFont::Normal);
    painter.setFont(font);
    painter.setPen(tile.isEnabled() ? shown.colors.textPrimary : shown.colors.textTertiary);
    painter.drawText(QRectF(0.0, tile.height() - metrics.rowDense, tile.width(), metrics.rowDense), Qt::AlignCenter,
                     QFontMetrics(font).elidedText(tile.text(), Qt::ElideRight, tile.width()));
}

/// A tile's size: a miniature two controls high and three wide, with the name's row under it.
QSize tileSize()
{
    const MetricTokens& metrics = tokens().metrics;
    return {metrics.control * 3, metrics.control * 2 + metrics.rowDense};
}

/// What a tile is to the pointer, the keyboard and assistive technology: a checkable button named by its text.
void prepareTile(QAbstractButton& tile, const QString& name)
{
    tile.setText(name);
    tile.setCheckable(true);
    tile.setAccessibleName(name);
    tile.setToolTip(name);
    tile.setFocusPolicy(Qt::StrongFocus);
    tile.setCursor(Qt::PointingHandCursor);
}

} // namespace

ThemePresetTile::ThemePresetTile(const ThemePreset preset, const QString& name, QWidget* parent)
    : QAbstractButton(parent), m_preset(preset)
{
    prepareTile(*this, name);
}

QSize ThemePresetTile::sizeHint() const
{
    return tileSize();
}

QSize ThemePresetTile::minimumSizeHint() const
{
    return sizeHint();
}

void ThemePresetTile::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens preset = presetTokens(m_preset);
    paintTile(*this,
              [&preset](QPainter& painter, const QRectF& miniature) { paintWindow(painter, miniature, preset); });
}

ThemeModeTile::ThemeModeTile(const Mode mode, const QString& name, QWidget* parent)
    : QAbstractButton(parent), m_mode(mode)
{
    prepareTile(*this, name);
}

QSize ThemeModeTile::sizeHint() const
{
    return tileSize();
}

QSize ThemeModeTile::minimumSizeHint() const
{
    return sizeHint();
}

void ThemeModeTile::paintEvent(QPaintEvent* /*event*/)
{
    const ThemePresetParts parts = partsInForce();
    const Mode mode = m_mode;
    paintTile(*this, [&parts, mode](QPainter& painter, const QRectF& miniature) {
        if (mode != Mode::System)
        {
            paintWindow(painter, miniature,
                        partsTokens(parts, mode == Mode::Light ? ThemeVariant::Light : ThemeVariant::Dark));
            return;
        }
        // The system's choice: the dark theme's window and the light one's, each a half of the miniature.
        const QRectF left(miniature.left(), miniature.top(), miniature.width() / 2.0, miniature.height());
        const QRectF right(left.right(), miniature.top(), miniature.width() - left.width(), miniature.height());
        painter.save();
        painter.setClipRect(left, Qt::IntersectClip);
        paintWindow(painter, miniature, partsTokens(parts, ThemeVariant::Dark));
        painter.restore();
        painter.save();
        painter.setClipRect(right, Qt::IntersectClip);
        paintWindow(painter, miniature, partsTokens(parts, ThemeVariant::Light));
        painter.restore();
    });
}

} // namespace an::ui
