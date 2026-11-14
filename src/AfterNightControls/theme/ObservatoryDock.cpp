// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The dock decorations under the Observatory profile. A tab strip is the first surface with a hairline under it; a tab
// is its icon, its name in the small text, semibold, elided in the middle, a star in the accent's text colour when the
// image has unsaved changes, an adornment and a close button, and the current tab stands over a three-pixel bar with
// rounded top corners, ten pixels in from each end: the accent in the active view's group, the tertiary text in the
// others. The buttons at a strip's right are quiet. A floating window is the first surface inside a one-pixel frame,
// the accent while it is active; groups lie two pixels apart on the dock area's ground; a drop indicator is a raised
// square whose filled half says where a view would land, and a band of the accent marks that place.

#include "stdafx.hpp"

#include "theme/ObservatoryDock.h"

#include "theme/AnDesignTokens.h"
#include "theme/Elevation.h"
#include "theme/PainterGuard.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>
#include <atomic>

namespace
{

using an::ui::ObservatoryDockGlyph;
using an::ui::ObservatoryDropSide;
using an::ui::PainterGuard;

/// How many dock parts the Observatory painters have drawn; the suites read it.
std::atomic<std::uint64_t> g_observatoryDockPaints{0};

void counted()
{
    g_observatoryDockPaints.fetch_add(1u, std::memory_order_relaxed);
}

/// The opacity a disabled part is painted at, and the state colour's share over a surface hovered and pressed.
constexpr qreal kDisabledOpacity = 0.38;
constexpr qreal kHoverShare = 0.08;
constexpr qreal kPressedShare = 0.12;

/// How much lower than a docked strip the strip of a floating image window is, and the room a strip keeps after its
/// last button.
constexpr int kSmallerStrip = 4;
constexpr int kStripEndPadding = 6;

/// A tab's padding at its start, at its end without a close button, and after its close button; the gap between its
/// parts; its icon; its adornment and the adornment's icon and corners.
constexpr int kTabPadding = 12;
constexpr int kTabCloseInset = 10;
constexpr int kTabGap = 8;
constexpr int kTabIcon = 14;
constexpr int kAdornment = 18;
constexpr int kAdornmentIcon = 12;
constexpr qreal kAdornmentRadius = 5.0;

/// The fewest characters a tab's name is cut to.
constexpr int kNameFloor = 12;

/// What follows the name of an image with unsaved changes.
constexpr char16_t kUnsavedStar[] = u" *";

/// The bar under the current tab, and how far it keeps from the tab's ends; how far a hovered tab's state colour keeps
/// from the tab's sides and from the strip's top and hairline.
constexpr qreal kMarkBar = 3.0;
constexpr qreal kMarkBarInset = 10.0;
constexpr qreal kHoverInsetX = 2.0;
constexpr qreal kHoverInsetY = 4.0;

/// A glyph's box inside a strip button, the cross inside a tab's close button, and the pen both are drawn with.
constexpr qreal kButtonGlyph = 10.0;
constexpr qreal kCloseCross = 7.0;
constexpr qreal kGlyphPen = 1.5;
constexpr qreal kCloseRadius = 4.0;

/// A title bar's text keeps this much from the bar's start.
constexpr int kTitlePadding = 10;

/// A drop indicator: its side, the gap between the cross's targets, its distance from the dock area's edge, its
/// corners, how far its filled half keeps from its border, and that half's corners.
constexpr int kDropTarget = 32;
constexpr int kDropTargetGap = 4;
constexpr int kDropTargetMargin = 12;
constexpr qreal kDropTargetRadius = 6.0;
constexpr int kDropTargetInset = 6;
constexpr qreal kDropHalfRadius = 2.0;

QColor atShare(QColor colour, const qreal share)
{
    colour.setAlphaF(static_cast<float>(colour.alphaF() * share));
    return colour;
}

void fillRounded(QPainter& painter, const QRectF& rect, const qreal radius, const QColor& colour)
{
    if (!colour.isValid() || colour.alpha() == 0 || rect.isEmpty())
    {
        return;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(rect, radius, radius);
}

/// A one-pixel line inside a rectangle's edge.
void strokeInside(QPainter& painter, const QRectF& rect, const qreal radius, const QColor& colour)
{
    painter.setPen(QPen(colour, 1.0));
    painter.setBrush(Qt::NoBrush);
    const QRectF inside = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    if (radius > 0.0)
    {
        painter.drawRoundedRect(inside, radius - 0.5, radius - 0.5);
    }
    else
    {
        painter.drawRect(inside);
    }
}

/// An icon in one colour, keeping its shape, drawn centred in a box at the painter's pixel ratio.
void paintTintedIcon(QPainter& painter, const QIcon& icon, const QRect& box, const int side, const QColor& colour)
{
    if (icon.isNull() || box.isEmpty())
    {
        return;
    }
    const qreal ratio = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
    QPixmap source = icon.pixmap(QSize(side, side), ratio);
    if (source.isNull())
    {
        return;
    }
    QPixmap shape(source.size());
    shape.setDevicePixelRatio(source.devicePixelRatio());
    shape.fill(Qt::transparent);
    {
        QPainter tint(&shape);
        tint.drawPixmap(0, 0, source);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(shape.rect(), colour);
    }
    const QRect target(box.left() + (box.width() - side) / 2, box.top() + (box.height() - side) / 2, side, side);
    painter.drawPixmap(target, shape);
}

/// A rectangle of a side centred in a box.
QRectF centred(const QRectF& box, const qreal side)
{
    return {box.center().x() - side / 2.0, box.center().y() - side / 2.0, side, side};
}

/// A cross corner to corner of a box.
QPainterPath crossIn(const QRectF& box)
{
    QPainterPath path;
    path.moveTo(box.topLeft());
    path.lineTo(box.bottomRight());
    path.moveTo(box.topRight());
    path.lineTo(box.bottomLeft());
    return path;
}

/// A glyph's strokes as one path, stroked once: where they cross is painted once, so a glyph painted at an opacity is
/// at that opacity throughout.
void drawGlyph(QPainter& painter, const QRectF& box, const ObservatoryDockGlyph glyph, const QColor& colour)
{
    const qreal w = box.width();
    const qreal h = box.height();
    QPainterPath path;
    switch (glyph)
    {
    case ObservatoryDockGlyph::Close:
        path = crossIn(centred(box, w * 0.9));
        break;
    case ObservatoryDockGlyph::Float: {
        // Two windows, the front one lower and to the left.
        const qreal offset = w * 0.3;
        path.addRoundedRect(QRectF(box.left() + offset, box.top(), w - offset, h - offset), 1.0, 1.0);
        path.addRoundedRect(QRectF(box.left(), box.top() + offset, w - offset, h - offset), 1.0, 1.0);
        break;
    }
    case ObservatoryDockGlyph::Dock: {
        // A window with a panel at its left: back into the workspace.
        path.addRoundedRect(box, 1.0, 1.0);
        const qreal x = box.left() + w * 0.38;
        path.moveTo(x, box.top());
        path.lineTo(x, box.bottom());
        break;
    }
    case ObservatoryDockGlyph::TabList: {
        const qreal half = w / 2.0;
        const QPointF centre = box.center();
        path.moveTo(centre + QPointF(-half, -half / 2.0));
        path.lineTo(centre + QPointF(0.0, half / 2.0));
        path.lineTo(centre + QPointF(half, -half / 2.0));
        break;
    }
    case ObservatoryDockGlyph::Maximize:
        path.addRoundedRect(box, 1.0, 1.0);
        break;
    case ObservatoryDockGlyph::Minimize:
        path.moveTo(box.left(), box.center().y());
        path.lineTo(box.right(), box.center().y());
        break;
    case ObservatoryDockGlyph::Pin: {
        const qreal x = box.center().x();
        path.addEllipse(QPointF(x, box.top() + h * 0.25), w * 0.25, w * 0.25);
        path.moveTo(x, box.top() + h * 0.5);
        path.lineTo(x, box.bottom());
        break;
    }
    }
    QPainterPathStroker stroker;
    stroker.setWidth(kGlyphPen);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawPath(stroker.createStroke(path).simplified());
}

QFontMetrics tabMetrics()
{
    return QFontMetrics(an::ui::observatoryDockTabFont());
}

} // anonymous namespace

namespace an::ui
{

bool observatoryDockDecorations()
{
    return styleProfile() == StyleProfile::Observatory;
}

int observatoryDockStripHeight(const bool floatingWindow)
{
    const int strip = tokens().metrics.strip;
    return floatingWindow ? strip - kSmallerStrip : strip;
}

int observatoryDockButtonSide()
{
    return tokens().metrics.controlSmall;
}

ObservatoryDockTabSource::~ObservatoryDockTabSource() = default;

QMargins observatoryDockCornerMargins(const int cornerHeight)
{
    const int room = (std::max)(0, cornerHeight - observatoryDockButtonSide());
    const int top = room / 2;
    return {0, top, kStripEndPadding, room - top};
}

QFont observatoryDockTabFont()
{
    const DesignTokens& t = tokens();
    QFont font = t.typography.body;
    font.setPointSizeF(t.metrics.fontSmall * 72.0 / 96.0);
    font.setWeight(QFont::DemiBold);
    return font;
}

int observatoryDockTabWidth(const ObservatoryDockTab& tab, const bool minimum)
{
    const QFontMetrics metrics = tabMetrics();
    int name = metrics.horizontalAdvance(tab.name);
    if (minimum)
    {
        name = (std::min)(name, metrics.averageCharWidth() * kNameFloor);
    }
    int width = kTabPadding + name;
    if (!tab.icon.isNull())
    {
        width += kTabIcon + kTabGap;
    }
    if (tab.modified)
    {
        width += metrics.horizontalAdvance(QString::fromUtf16(kUnsavedStar));
    }
    if (tab.adorned)
    {
        width += kTabGap + kAdornment;
    }
    width += tab.closable ? kTabGap + kObservatoryDockTabClose + kTabCloseInset : kTabPadding;
    return width;
}

ObservatoryDockTabParts observatoryDockTabParts(const QRect& rect, const ObservatoryDockTab& tab)
{
    ObservatoryDockTabParts parts;
    // The parts stand over the strip's hairline, which is the tab's last row.
    const int top = rect.top();
    const int height = rect.height() - 1;
    const auto centredBox = [top, height](const int x, const int side) {
        return QRect(x, top + (height - side) / 2, side, side);
    };

    int x = rect.left() + kTabPadding;
    if (!tab.icon.isNull())
    {
        parts.icon = centredBox(x, kTabIcon);
        x += kTabIcon + kTabGap;
    }

    int right = rect.left() + rect.width();
    if (tab.closable)
    {
        right -= kTabCloseInset;
        parts.close = centredBox(right - kObservatoryDockTabClose, kObservatoryDockTabClose);
        right -= kObservatoryDockTabClose + kTabGap;
    }
    else
    {
        right -= kTabPadding;
    }
    if (tab.adorned)
    {
        parts.adornment = centredBox(right - kAdornment, kAdornment);
        right -= kAdornment + kTabGap;
    }
    if (tab.modified)
    {
        const int star = tabMetrics().horizontalAdvance(QString::fromUtf16(kUnsavedStar));
        parts.star = QRect(right - star, top, star, height);
        right -= star;
    }
    parts.name = QRect(x, top, (std::max)(0, right - x), height);
    return parts;
}

QString observatoryDockTabText(const QString& name, const int width)
{
    return tabMetrics().elidedText(name, Qt::ElideMiddle, width);
}

void paintObservatoryDockStrip(QPainter& painter, const QRect& strip)
{
    const DesignTokens& t = tokens();
    const PainterGuard guard(&painter);
    painter.fillRect(strip, t.layers.surface1);
    painter.fillRect(QRect(strip.left(), strip.bottom(), strip.width(), 1), t.colors.borderSubtle);
    counted();
}

void paintObservatoryDockTab(QPainter& painter, const QRect& rect, const ObservatoryDockTab& tab,
                             const ObservatoryDockTabState& state)
{
    const DesignTokens& t = tokens();
    const ColorTokens& c = t.colors;
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!state.enabled)
    {
        painter.setOpacity(kDisabledOpacity);
    }

    const QRectF content = QRectF(rect).adjusted(0.0, 0.0, 0.0, -1.0);
    if (state.hovered && !state.current)
    {
        fillRounded(painter, content.adjusted(kHoverInsetX, kHoverInsetY, -kHoverInsetX, -kHoverInsetY), t.radius.sm,
                    atShare(c.stateLayer, kHoverShare));
    }

    const ObservatoryDockTabParts parts = observatoryDockTabParts(rect, tab);
    paintTintedIcon(painter, tab.icon, parts.icon, kTabIcon, c.textTertiary);

    painter.setFont(observatoryDockTabFont());
    painter.setPen(state.current && state.activeGroup ? c.textPrimary : c.textSecondary);
    painter.drawText(parts.name, Qt::AlignLeft | Qt::AlignVCenter,
                     observatoryDockTabText(tab.name, parts.name.width()));
    if (tab.modified)
    {
        painter.setPen(c.textAccent);
        painter.drawText(parts.star, Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf16(kUnsavedStar));
    }

    if (tab.adorned)
    {
        fillRounded(painter, QRectF(parts.adornment), kAdornmentRadius, c.accentContainer);
        paintTintedIcon(painter, tab.adornmentIcon, parts.adornment, kAdornmentIcon, c.textOnAccentContainer);
    }

    if (state.current)
    {
        const QRectF bar(rect.left() + kMarkBarInset, content.bottom() - kMarkBar, rect.width() - 2 * kMarkBarInset,
                         kMarkBar);
        painter.setClipRect(bar);
        // Rounded at its top only: the corners below are clipped away.
        fillRounded(painter, bar.adjusted(0.0, 0.0, 0.0, kMarkBar), kMarkBar,
                    state.activeGroup ? c.accentPrimary : c.textTertiary);
    }
    counted();
}

void paintObservatoryDockButton(QPainter& painter, const QRect& rect, const ObservatoryDockGlyph glyph,
                                const bool hovered, const bool pressed, const bool enabled)
{
    const DesignTokens& t = tokens();
    const ColorTokens& c = t.colors;
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!enabled)
    {
        painter.setOpacity(kDisabledOpacity);
    }
    const bool active = enabled && (hovered || pressed);
    if (active)
    {
        fillRounded(painter, QRectF(rect), t.radius.sm, atShare(c.stateLayer, pressed ? kPressedShare : kHoverShare));
    }
    drawGlyph(painter, centred(QRectF(rect), kButtonGlyph), glyph, active ? c.textPrimary : c.textSecondary);
    counted();
}

void paintObservatoryDockTabClose(QPainter& painter, const QRect& rect, const bool hovered, const bool pressed,
                                  const bool enabled)
{
    const DesignTokens& t = tokens();
    const ColorTokens& c = t.colors;
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!enabled)
    {
        painter.setOpacity(kDisabledOpacity);
    }
    const bool active = enabled && (hovered || pressed);
    if (active)
    {
        fillRounded(painter, QRectF(rect), kCloseRadius, atShare(c.stateLayer, pressed ? kPressedShare : kHoverShare));
    }
    QPainterPathStroker stroker;
    stroker.setWidth(kGlyphPen);
    stroker.setCapStyle(Qt::RoundCap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(active ? c.textPrimary : c.textTertiary);
    painter.drawPath(stroker.createStroke(crossIn(centred(QRectF(rect), kCloseCross))).simplified());
    counted();
}

void paintObservatoryDockTitleBar(QPainter& painter, const QRect& rect, const QString& title, const QRect& titleRect)
{
    const DesignTokens& t = tokens();
    const PainterGuard guard(&painter);
    painter.fillRect(rect, t.layers.surface1);
    painter.fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), t.colors.borderSubtle);
    const QRect text = titleRect.adjusted(kTitlePadding, 0, 0, -1);
    painter.setFont(observatoryDockTabFont());
    painter.setPen(t.colors.textPrimary);
    painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, observatoryDockTabText(title, text.width()));
    counted();
}

void paintObservatoryDockFrame(QPainter& painter, const QRect& rect, const bool active)
{
    const DesignTokens& t = tokens();
    const PainterGuard guard(&painter);
    painter.fillRect(rect, t.layers.surface1);
    strokeInside(painter, QRectF(rect), 0.0, active ? t.colors.accentPrimary : t.colors.borderPrimary);
    counted();
}

void paintObservatoryDockPane(QPainter& painter, const QRect& rect)
{
    painter.fillRect(rect, tokens().layers.surface1);
    counted();
}

void paintObservatoryDockSeparator(QPainter& painter, const QRect& rect, const bool active)
{
    const DesignTokens& t = tokens();
    painter.fillRect(rect, t.layers.surface0);
    if (active)
    {
        painter.fillRect(rect, t.colors.accentLine);
    }
    counted();
}

int observatoryDropTargetSide()
{
    return kDropTarget;
}

int observatoryDropTargetPitch()
{
    return kDropTarget + kDropTargetGap;
}

int observatoryDropTargetMargin()
{
    return kDropTargetMargin;
}

QRect observatoryDropTargetHalf(const QRect& target, const ObservatoryDropSide side)
{
    const QRect inside = target.adjusted(kDropTargetInset, kDropTargetInset, -kDropTargetInset, -kDropTargetInset);
    const int halfWidth = inside.width() / 2;
    const int halfHeight = inside.height() / 2;
    switch (side)
    {
    case ObservatoryDropSide::Left:
        return {inside.left(), inside.top(), halfWidth, inside.height()};
    case ObservatoryDropSide::Right:
        return {inside.left() + inside.width() - halfWidth, inside.top(), halfWidth, inside.height()};
    case ObservatoryDropSide::Top:
        return {inside.left(), inside.top(), inside.width(), halfHeight};
    case ObservatoryDropSide::Bottom:
        return {inside.left(), inside.top() + inside.height() - halfHeight, inside.width(), halfHeight};
    case ObservatoryDropSide::Centre:
        break;
    }
    return inside;
}

void paintObservatoryDropTarget(QPainter& painter, const QRect& rect, const ObservatoryDropSide side, const bool hot)
{
    const DesignTokens& t = tokens();
    const ColorTokens& c = t.colors;
    const PainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintElevation(painter, QRectF(rect), static_cast<int>(kDropTargetRadius));
    fillRounded(painter, QRectF(rect), kDropTargetRadius, t.layers.surface3);
    strokeInside(painter, QRectF(rect), kDropTargetRadius, c.borderPrimary);
    const QRect inside = rect.adjusted(kDropTargetInset, kDropTargetInset, -kDropTargetInset, -kDropTargetInset);
    strokeInside(painter, QRectF(inside), kDropHalfRadius, c.textTertiary);
    fillRounded(painter, QRectF(observatoryDropTargetHalf(rect, side)), kDropHalfRadius,
                hot ? c.accentPrimary : c.accentLine);
    counted();
}

void paintObservatoryDropBand(QPainter& painter, const QRect& rect)
{
    const DesignTokens& t = tokens();
    const PainterGuard guard(&painter);
    painter.fillRect(rect, t.colors.accentSoft);
    const QColor border = t.colors.accentPrimary;
    const int b = kObservatoryDropBandBorder;
    painter.fillRect(QRect(rect.left(), rect.top(), rect.width(), b), border);
    painter.fillRect(QRect(rect.left(), rect.bottom() - b + 1, rect.width(), b), border);
    painter.fillRect(QRect(rect.left(), rect.top() + b, b, rect.height() - 2 * b), border);
    painter.fillRect(QRect(rect.right() - b + 1, rect.top() + b, b, rect.height() - 2 * b), border);
    counted();
}

namespace testing
{

std::uint64_t observatoryDockPaints()
{
    return g_observatoryDockPaints.load(std::memory_order_relaxed);
}

} // namespace testing

} // namespace an::ui
