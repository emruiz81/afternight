// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory profile's drawing of the standard widgets: what AnProxyStyle draws under that profile, each element
// from the tokens in force. A control is its fill, its outline and its text from the colour tokens, its corners the
// controls' radius; hovered and pressed it lays the state colour over its fill at 8 and 12 %; disabled it is painted
// whole at 38 % opacity; the focus ring - two pixels of the focus colour - is drawn for focus that came from the
// keyboard, and a field shows its focus as a border whatever brought it. A raised part (a menu, a tooltip) stands on
// its elevation (theme/Elevation.h). What this file does not draw is drawn by the Classic code of AnProxyStyle.cpp.

#include "stdafx.hpp"

#include "theme/AnProxyStyle.h"

#include "theme/AnDesignTokens.h"
#include "theme/Elevation.h"
#include "theme/ObservatoryDock.h"
#include "theme/PainterGuard.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QStyleOption>
#include <QTabBar>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace
{

using an::ui::ColorTokens;
using an::ui::DesignTokens;
using an::ui::PainterGuard;

/// How many elements the Observatory painters have drawn; the suites read it.
std::atomic<std::uint64_t> g_observatoryPaints{0};

/// The opacity a disabled control is painted at.
constexpr qreal kDisabledOpacity = 0.38;

/// The state colour's share over a surface: hovered, pressed.
constexpr qreal kHoverShare = 0.08;
constexpr qreal kPressedShare = 0.12;

/// A soft status colour's share: a banner's, a toned progress bar's track.
constexpr qreal kSoftShare = 0.16;

/// The focus ring's width, and a control's border.
constexpr qreal kFocusRing = 2.0;
constexpr qreal kBorder = 1.0;

/// A check box's and a radio button's indicator: its side, its corner, its border; a radio button's dot.
constexpr qreal kIndicator = 16.0;
constexpr qreal kIndicatorRadius = 4.0;
constexpr qreal kIndicatorBorder = 2.0;
constexpr qreal kRadioDot = 8.0;

/// A slider's track and knob, and a thin progress bar's track.
constexpr qreal kTrack = 4.0;
constexpr qreal kKnob = 14.0;

/// The bar that marks a selected tab and a selected row, and how far a tab's bar keeps from the tab's ends.
constexpr qreal kMarkBar = 3.0;
constexpr qreal kTabBarInset = 10.0;

/// A scroll bar's handle: the tertiary text at these shares, at rest and hovered or dragged.
constexpr qreal kHandleShare = 0.40;
constexpr qreal kActiveHandleShare = 0.70;

/// A combo box's arrow column and a spin box's buttons' column; a field's text padding, past its border.
constexpr int kComboArrowColumn = 28;
constexpr int kSpinButtonColumn = 20;
constexpr int kFieldPadding = 10;

/// The inner margin a line edit keeps on each side of its text: a field's text field starts this much before its text.
constexpr int kLineEditMargin = 2;

/// A menu item's padding at each side and the gap between its parts.
constexpr qreal kMenuItemPadding = 8.0;
constexpr qreal kMenuItemGap = 10.0;

/// A process splitter's grip: its thickness and how far it keeps from the handle's ends.
constexpr qreal kGrip = 2.0;
constexpr qreal kGripInset = 6.0;

/// A chevron's half width and the pen it is drawn with.
constexpr qreal kChevron = 4.0;
constexpr qreal kChevronPen = 1.5;

/// A tool bar separator's longest extent.
constexpr qreal kSeparatorLength = 20.0;

bool enabled(const QStyleOption* option)
{
    return option->state & QStyle::State_Enabled;
}

bool hovered(const QStyleOption* option)
{
    return (option->state & QStyle::State_MouseOver) && enabled(option);
}

bool pressed(const QStyleOption* option)
{
    return (option->state & QStyle::State_Sunken) && enabled(option);
}

bool on(const QStyleOption* option)
{
    return option->state & QStyle::State_On;
}

bool focused(const QStyleOption* option)
{
    return (option->state & QStyle::State_HasFocus) && enabled(option);
}

/// Focus that came from the keyboard: the one a ring is drawn for.
bool keyboardFocus(const QStyleOption* option)
{
    return focused(option) && (option->state & QStyle::State_KeyboardFocusChange);
}

QString propertyText(const QWidget* widget, const char* name)
{
    return widget != nullptr ? widget->property(name).toString() : QString();
}

/// A colour at a share of its own opacity.
QColor atShare(QColor colour, const qreal share)
{
    colour.setAlphaF(colour.alphaF() * share);
    return colour;
}

/// The state colour a control lays over its fill, or nothing at rest.
QColor stateLayer(const ColorTokens& c, const QStyleOption* option)
{
    if (pressed(option))
    {
        return atShare(c.stateLayer, kPressedShare);
    }
    if (hovered(option))
    {
        return atShare(c.stateLayer, kHoverShare);
    }
    return {};
}

void fillRounded(QPainter* painter, const QRectF& rect, const qreal radius, const QColor& colour)
{
    if (!colour.isValid() || colour.alpha() == 0 || rect.isEmpty())
    {
        return;
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(colour);
    painter->drawRoundedRect(rect, radius, radius);
}

/// A ring of a width drawn inside a rounded rectangle, its outer corners of the radius.
void strokeRounded(QPainter* painter, const QRectF& rect, const qreal radius, const QColor& colour, const qreal width)
{
    if (!colour.isValid() || colour.alpha() == 0 || rect.isEmpty())
    {
        return;
    }
    const qreal half = width / 2.0;
    painter->setPen(QPen(colour, width));
    painter->setBrush(Qt::NoBrush);
    const qreal inner = (std::max)(0.0, radius - half);
    painter->drawRoundedRect(rect.adjusted(half, half, -half, -half), inner, inner);
}

void drawChevron(QPainter* painter, const QPointF& centre, const qreal half, const QColor& colour,
                 const Qt::ArrowType direction)
{
    painter->setPen(QPen(colour, kChevronPen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    const qreal depth = half / 2.0;
    QPointF points[3];
    switch (direction)
    {
    case Qt::UpArrow:
        points[0] = centre + QPointF(-half, depth);
        points[1] = centre + QPointF(0.0, -depth);
        points[2] = centre + QPointF(half, depth);
        break;
    case Qt::LeftArrow:
        points[0] = centre + QPointF(depth, -half);
        points[1] = centre + QPointF(-depth, 0.0);
        points[2] = centre + QPointF(depth, half);
        break;
    case Qt::RightArrow:
        points[0] = centre + QPointF(-depth, -half);
        points[1] = centre + QPointF(depth, 0.0);
        points[2] = centre + QPointF(-depth, half);
        break;
    default:
        points[0] = centre + QPointF(-half, -depth);
        points[1] = centre + QPointF(0.0, depth);
        points[2] = centre + QPointF(half, -depth);
        break;
    }
    painter->drawPolyline(points, 3);
}

/// Paints a pixmap in one colour, keeping its shape.
QPixmap tinted(const QPixmap& source, const QColor& colour)
{
    if (source.isNull())
    {
        return source;
    }
    QPixmap result(source.size());
    result.setDevicePixelRatio(source.devicePixelRatio());
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.drawPixmap(0, 0, source);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(result.rect(), colour);
    return result;
}

// ── Buttons ───────────────────────────────────────────────────────────────

/// The five roles of a button, named by its variant properties.
enum class ButtonRole
{
    Primary,
    Tonal,
    Outlined,
    Quiet,
    Destructive
};

ButtonRole buttonRole(const QWidget* widget, const QStyleOption* option)
{
    QString variant = propertyText(widget, "anButtonVariant");
    if (variant.isEmpty())
    {
        variant = propertyText(widget, "buttonVariant");
    }
    const QString role = propertyText(widget, "anProcessButtonRole");
    if (variant == QLatin1String("primary") || role == QLatin1String("primary"))
    {
        return ButtonRole::Primary;
    }
    if (variant == QLatin1String("tonal"))
    {
        return ButtonRole::Tonal;
    }
    if (variant == QLatin1String("quiet") || role == QLatin1String("neutral"))
    {
        return ButtonRole::Quiet;
    }
    if (variant == QLatin1String("destructive") || role == QLatin1String("danger"))
    {
        return ButtonRole::Destructive;
    }
    if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option))
    {
        if (button->features & QStyleOptionButton::Flat)
        {
            return ButtonRole::Quiet;
        }
    }
    return ButtonRole::Outlined;
}

struct ButtonLook
{
    QColor fill;
    QColor layer;
    QColor outline;
    QColor text;
    /// Whether the icon is painted in the text's colour: on a filled button, the icon manager's tint would not read.
    bool tintIcon = false;
};

ButtonLook buttonLook(const ButtonRole role, const QStyleOption* option, const ColorTokens& c)
{
    ButtonLook look;
    const bool checked = on(option);
    switch (role)
    {
    case ButtonRole::Primary:
        look.fill =
            (pressed(option) || checked) ? c.accentPressed : (hovered(option) ? c.accentHover : c.accentPrimary);
        look.text = c.textOnAccent;
        look.tintIcon = true;
        break;
    case ButtonRole::Destructive:
        look.fill = c.statusError;
        look.layer = stateLayer(c, option);
        look.text = an::ui::bestContrastTextColor(c.statusError, c.textOnAccent, c.textPrimary, c.bgPrimary, 4.5);
        look.tintIcon = true;
        break;
    case ButtonRole::Tonal:
        look.fill = c.accentContainer;
        look.layer = stateLayer(c, option);
        look.text = c.textOnAccentContainer;
        look.tintIcon = true;
        break;
    case ButtonRole::Quiet:
    case ButtonRole::Outlined:
        look.layer = stateLayer(c, option);
        if (checked)
        {
            look.fill = c.accentContainer;
            look.text = c.textOnAccentContainer;
            look.tintIcon = true;
        }
        else
        {
            look.outline = role == ButtonRole::Outlined ? c.borderPrimary : QColor();
            look.text = role == ButtonRole::Outlined ? c.textPrimary : c.textAccent;
        }
        break;
    }
    return look;
}

void drawButtonBevel(QPainter* painter, const QStyleOption* option, const QWidget* widget, const DesignTokens& t)
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (!enabled(option))
    {
        painter->setOpacity(kDisabledOpacity);
    }
    const ButtonLook look = buttonLook(buttonRole(widget, option), option, t.colors);
    const QRectF rect(option->rect);
    const qreal radius = t.radius.md;
    fillRounded(painter, rect, radius, look.fill);
    fillRounded(painter, rect, radius, look.layer);
    if (keyboardFocus(option))
    {
        strokeRounded(painter, rect, radius, t.colors.borderFocus, kFocusRing);
    }
    else
    {
        strokeRounded(painter, rect, radius, look.outline, kBorder);
    }
}

void drawButtonLabel(QPainter* painter, const QStyleOption* option, const QWidget* widget, const DesignTokens& t)
{
    const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
    if (button == nullptr)
    {
        return;
    }
    const PainterGuard guard(painter);
    if (!enabled(option))
    {
        painter->setOpacity(kDisabledOpacity);
    }
    const ButtonLook look = buttonLook(buttonRole(widget, option), option, t.colors);
    QFont font = painter->font();
    font.setWeight(QFont::DemiBold);
    painter->setFont(font);
    painter->setPen(look.text);

    QRect area = option->rect;
    if (button->features & QStyleOptionButton::HasMenu)
    {
        // A button that opens a menu says so with an arrow at its end.
        const int column = static_cast<int>(kIndicator) + t.spacing.xs;
        const QRectF arrow(area.right() - column - t.spacing.xs, area.top(), column, area.height());
        drawChevron(painter, arrow.center(), kChevron, look.text, Qt::DownArrow);
        area.setRight(area.right() - column);
    }
    const QString text = button->text;
    const int flags = Qt::AlignVCenter | Qt::TextHideMnemonic;
    if (button->icon.isNull())
    {
        painter->drawText(area, Qt::AlignHCenter | flags, text);
        return;
    }
    QPixmap pixmap = button->icon.pixmap(button->iconSize, painter->device()->devicePixelRatio(), QIcon::Normal);
    if (look.tintIcon)
    {
        pixmap = tinted(pixmap, look.text);
    }
    const QSizeF iconSize = pixmap.deviceIndependentSize();
    const qreal gap = text.isEmpty() ? 0.0 : t.spacing.xs + t.spacing.xs / 2.0;
    const qreal textWidth =
        text.isEmpty() ? 0.0 : painter->fontMetrics().horizontalAdvance(QString(text).remove(QLatin1Char('&')));
    const qreal total = iconSize.width() + gap + textWidth;
    const qreal left = area.left() + (area.width() - total) / 2.0;
    const QPointF iconAt(std::round(left), std::round(area.top() + (area.height() - iconSize.height()) / 2.0));
    painter->drawPixmap(iconAt, pixmap);
    if (!text.isEmpty())
    {
        const QRectF textRect(iconAt.x() + iconSize.width() + gap, area.top(), textWidth + 1.0, area.height());
        painter->drawText(textRect, Qt::AlignLeft | flags, text);
    }
}

// ── Fields ────────────────────────────────────────────────────────────────

/// A field's surface: the third surface with an outline, a two-pixel focus border when focused, the state colour over
/// it when hovered (a field that opens a list), nothing filled when it is read only.
void drawField(QPainter* painter, const QStyleOption* option, const DesignTokens& t, const bool readOnly,
               const QColor& layer)
{
    const QRectF rect(option->rect);
    const qreal radius = t.radius.md;
    fillRounded(painter, rect, radius, readOnly ? QColor() : t.colors.bgInput);
    fillRounded(painter, rect, radius, layer);
    if (focused(option) && !readOnly)
    {
        strokeRounded(painter, rect, radius, t.colors.borderFocus, kFocusRing);
    }
    else
    {
        strokeRounded(painter, rect, radius, readOnly ? t.colors.borderSubtle : t.colors.borderPrimary, kBorder);
    }
}

/// Whether a widget is the text of a spin box or a combo box, which draws the field around it.
bool insideAField(const QWidget* widget)
{
    const QWidget* parent = widget != nullptr ? widget->parentWidget() : nullptr;
    return parent != nullptr &&
           (qobject_cast<const QAbstractSpinBox*>(parent) || qobject_cast<const QComboBox*>(parent));
}

// ── Indicators ────────────────────────────────────────────────────────────

QRectF indicatorRect(const QRect& rect)
{
    const QPointF centre = QRectF(rect).center();
    return QRectF(std::round(centre.x() - kIndicator / 2.0), std::round(centre.y() - kIndicator / 2.0), kIndicator,
                  kIndicator);
}

void drawCheckIndicator(QPainter* painter, const QStyleOption* option, const DesignTokens& t)
{
    const ColorTokens& c = t.colors;
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (!enabled(option))
    {
        painter->setOpacity(kDisabledOpacity);
    }
    const QRectF box = indicatorRect(option->rect);
    const bool partial = option->state & QStyle::State_NoChange;
    if (!on(option) && !partial)
    {
        strokeRounded(painter, box, kIndicatorRadius, hovered(option) ? c.textPrimary : c.textSecondary,
                      kIndicatorBorder);
        return;
    }
    fillRounded(painter, box, kIndicatorRadius, hovered(option) ? c.accentHover : c.accentPrimary);
    painter->setPen(QPen(c.textOnAccent, kIndicatorBorder, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    const QPointF origin = box.topLeft();
    if (partial)
    {
        painter->drawLine(origin + QPointF(4.0, 8.0), origin + QPointF(12.0, 8.0));
        return;
    }
    const QPointF mark[3] = {origin + QPointF(4.0, 8.5), origin + QPointF(7.0, 11.5), origin + QPointF(12.0, 5.5)};
    painter->drawPolyline(mark, 3);
}

void drawRadioIndicator(QPainter* painter, const QStyleOption* option, const DesignTokens& t)
{
    const ColorTokens& c = t.colors;
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (!enabled(option))
    {
        painter->setOpacity(kDisabledOpacity);
    }
    const QRectF box = indicatorRect(option->rect);
    const qreal half = kIndicatorBorder / 2.0;
    const QColor ring = on(option) ? (hovered(option) ? c.accentHover : c.accentPrimary)
                                   : (hovered(option) ? c.textPrimary : c.textSecondary);
    painter->setPen(QPen(ring, kIndicatorBorder));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(box.adjusted(half, half, -half, -half));
    if (on(option))
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(ring);
        const QPointF centre = box.center();
        painter->drawEllipse(centre, kRadioDot / 2.0, kRadioDot / 2.0);
    }
}

// ── Raised parts ──────────────────────────────────────────────────────────

/// A raised part's surface: its window's rectangle without the margin its shadow is drawn in.
QRectF raisedSurface(const QStyleOption* option, const QWidget* widget)
{
    QRect rect = option->rect;
    if (widget != nullptr && widget->testAttribute(Qt::WA_TranslucentBackground))
    {
        rect = rect.marginsRemoved(widget->contentsMargins());
    }
    return QRectF(rect);
}

void drawRaisedPanel(QPainter* painter, const QStyleOption* option, const QWidget* widget, const QColor& fill,
                     const qreal radius, const DesignTokens& t)
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF surface = raisedSurface(option, widget);
    if (widget != nullptr && widget->testAttribute(Qt::WA_TranslucentBackground) && !widget->contentsMargins().isNull())
    {
        an::ui::paintElevation(*painter, surface, static_cast<int>(radius));
    }
    fillRounded(painter, surface, radius, fill);
    strokeRounded(painter, surface, radius, t.colors.borderSubtle, kBorder);
}

/// A menu's item: its padding, the column of its check or icon, its text, its shortcut in the monospace and the arrow
/// of a submenu; under the pointer, the accent's container in its text.
void drawObservatoryMenuItem(QPainter* painter, const QStyleOptionMenuItem* item, const DesignTokens& t,
                             const bool underline)
{
    const ColorTokens& c = t.colors;
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect(item->rect);
    if (item->menuItemType == QStyleOptionMenuItem::Separator)
    {
        const qreal y = std::floor(rect.center().y()) + 0.5;
        painter->setPen(QPen(c.borderSubtle, kBorder));
        painter->drawLine(QPointF(rect.left() + t.spacing.xs + t.spacing.xs / 2.0, y),
                          QPointF(rect.right() - t.spacing.xs - t.spacing.xs / 2.0, y));
        return;
    }
    if (item->menuItemType == QStyleOptionMenuItem::EmptyArea || item->menuItemType == QStyleOptionMenuItem::Margin)
    {
        return;
    }
    if (!enabled(item))
    {
        painter->setOpacity(kDisabledOpacity);
    }
    const bool selected = (item->state & QStyle::State_Selected) && enabled(item);
    if (selected)
    {
        fillRounded(painter, rect, t.radius.sm, c.accentContainer);
    }
    const QColor text = selected ? c.textOnAccentContainer : c.textPrimary;
    const bool rightToLeft = item->direction == Qt::RightToLeft;
    const auto visual = [&](const QRectF& logical) {
        return rightToLeft ? QRectF(rect.left() + rect.right() - logical.right(), logical.top(), logical.width(),
                                    logical.height())
                           : logical;
    };

    // The check or icon column.
    const qreal column = (std::max)(static_cast<qreal>(item->maxIconWidth), kIndicator);
    const QRectF columnRect(rect.left() + kMenuItemPadding, rect.top(), column, rect.height());
    const bool checkable = item->checkType != QStyleOptionMenuItem::NotCheckable;
    if (!item->icon.isNull())
    {
        if (checkable && item->checked)
        {
            fillRounded(painter, visual(columnRect.adjusted(-2.0, 2.0, 2.0, -2.0)), t.radius.sm,
                        selected ? c.bgInput : c.accentContainer);
        }
        const int extent = static_cast<int>(kIndicator);
        const QRectF iconRect(columnRect.center().x() - extent / 2.0, columnRect.center().y() - extent / 2.0, extent,
                              extent);
        item->icon.paint(painter, visual(iconRect).toRect(), Qt::AlignCenter, QIcon::Normal,
                         item->checked ? QIcon::On : QIcon::Off);
    }
    else if (checkable && item->checked)
    {
        const QPointF centre = visual(columnRect).center();
        painter->setPen(
            QPen(selected ? text : c.textAccent, kIndicatorBorder, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        if (item->checkType == QStyleOptionMenuItem::Exclusive)
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? text : c.textAccent);
            painter->drawEllipse(centre, kRadioDot / 2.0 - 1.0, kRadioDot / 2.0 - 1.0);
        }
        else
        {
            const QPointF mark[3] = {centre + QPointF(-4.0, 0.5), centre + QPointF(-1.0, 3.5),
                                     centre + QPointF(4.0, -2.5)};
            painter->drawPolyline(mark, 3);
        }
    }

    // The arrow of a submenu.
    qreal right = rect.right() - kMenuItemPadding;
    if (item->menuItemType == QStyleOptionMenuItem::SubMenu)
    {
        const QRectF arrow(right - kIndicator, rect.top(), kIndicator, rect.height());
        drawChevron(painter, visual(arrow).center(), kChevron / 1.5, selected ? text : c.textSecondary,
                    rightToLeft ? Qt::LeftArrow : Qt::RightArrow);
        right -= kIndicator;
    }

    // The text and the shortcut after a tab.
    const qsizetype tab = item->text.indexOf(QLatin1Char('\t'));
    const QString label = tab >= 0 ? item->text.left(tab) : item->text;
    const QString shortcut = tab >= 0 ? item->text.mid(tab + 1) : QString();
    const qreal textLeft = columnRect.right() + kMenuItemGap;
    const int alignment =
        Qt::AlignVCenter | Qt::TextSingleLine | (underline ? Qt::TextShowMnemonic : Qt::TextHideMnemonic);
    if (!shortcut.isEmpty())
    {
        QFont mono = t.typography.monospace;
        mono.setPointSizeF(t.metrics.fontCaption * 72.0 / 96.0);
        painter->setFont(mono);
        painter->setPen(selected ? text : c.textTertiary);
        const QRectF shortcutRect(textLeft, rect.top(), right - textLeft, rect.height());
        painter->drawText(visual(shortcutRect), (rightToLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
                          shortcut);
    }
    painter->setFont(item->font);
    painter->setPen(text);
    const QRectF textRect(textLeft, rect.top(), right - textLeft, rect.height());
    painter->drawText(visual(textRect), (rightToLeft ? Qt::AlignRight : Qt::AlignLeft) | alignment, label);
}

// ── Item views ────────────────────────────────────────────────────────────

/// Whether a widget is an item view or a view's viewport.
bool itemView(const QWidget* widget)
{
    return widget != nullptr &&
           (qobject_cast<const QAbstractItemView*>(widget) ||
            (widget->parentWidget() != nullptr && qobject_cast<const QAbstractItemView*>(widget->parentWidget())));
}

/// A selected row's fill: the accent's soft share laid over the view's base, opaque, so a row and its item painting it
/// both - as a table and a tree's branches do - give the same colour.
QColor selectionColour(const QStyleOption* option, const ColorTokens& c)
{
    return an::ui::compositeOver(c.accentSoft, option->palette.color(QPalette::Base));
}

/// A header's label: the caption in small capitals, semibold, a little spaced.
QFont headerFont(const DesignTokens& t)
{
    QFont font = t.typography.caption;
    font.setWeight(QFont::DemiBold);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::PercentageSpacing, 108.0);
    return font;
}

// ── Notes ─────────────────────────────────────────────────────────────────

/// A note's colour by its tone: info, warning, error; invalid for none.
QColor toneColour(const QString& tone, const ColorTokens& c)
{
    if (tone == QLatin1String("info"))
    {
        return c.statusInfo;
    }
    if (tone == QLatin1String("warning"))
    {
        return c.statusWarning;
    }
    if (tone == QLatin1String("error"))
    {
        return c.statusError;
    }
    if (tone == QLatin1String("success"))
    {
        return c.statusSuccess;
    }
    return {};
}

/// A progress bar's colour by the tone of the usage it shows: high, medium, low; invalid for none.
QColor usageToneColour(const QWidget* widget, const ColorTokens& c)
{
    const QString tone = propertyText(widget, "anUsageTone");
    if (tone == QLatin1String("high"))
    {
        return c.statusError;
    }
    if (tone == QLatin1String("medium"))
    {
        return c.statusWarning;
    }
    if (tone == QLatin1String("low"))
    {
        return c.statusSuccess;
    }
    return {};
}

/// A note: the tone's soft share with the controls' radius, a bar of the tone at its start; the third surface when it
/// has no tone.
void drawNote(QPainter* painter, const QRect& bounds, const QColor& tone, const DesignTokens& t,
              const Qt::LayoutDirection direction)
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect(bounds);
    const qreal radius = t.radius.md;
    if (!tone.isValid())
    {
        fillRounded(painter, rect, radius, t.colors.bgInput);
        return;
    }
    fillRounded(painter, rect, radius, atShare(tone, kSoftShare));
    QPainterPath shape;
    shape.addRoundedRect(rect, radius, radius);
    painter->setClipPath(shape);
    const QRectF bar = direction == Qt::RightToLeft
                           ? QRectF(rect.right() - kMarkBar, rect.top(), kMarkBar, rect.height())
                           : QRectF(rect.left(), rect.top(), kMarkBar, rect.height());
    painter->fillRect(bar, tone);
}

} // anonymous namespace

namespace an::ui
{

// ════════════════════════════════════════════════════════════════════════════
// Primitives
// ════════════════════════════════════════════════════════════════════════════

bool AnProxyStyle::drawObservatoryPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                                            const QWidget* widget) const
{
    const DesignTokens& t = *m_tokens;
    const ColorTokens& c = t.colors;
    switch (element)
    {
    case PE_PanelButtonCommand:
        drawButtonBevel(painter, option, widget, t);
        break;

    case PE_IndicatorTabClose:
        // A tab's close button, the dock strips' and every other: a cross over the state colour.
        an::ui::paintObservatoryDockTabClose(*painter, option->rect, hovered(option), pressed(option), enabled(option));
        break;

    case PE_PanelButtonTool: {
        // Quiet until hovered; the accent's container when checked, unless the button says its state another way.
        const bool quiet = propertyText(widget, "anButtonVariant") == QLatin1String("quiet");
        const QColor fill = (on(option) && !quiet) ? c.accentContainer : QColor();
        const QColor layer = stateLayer(c, option);
        if (!fill.isValid() && !layer.isValid())
        {
            break;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        fillRounded(painter, QRectF(option->rect), t.radius.md, fill);
        fillRounded(painter, QRectF(option->rect), t.radius.md, layer);
        break;
    }

    case PE_PanelLineEdit: {
        const auto* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
        if (insideAField(widget) || frame == nullptr || frame->lineWidth <= 0)
        {
            break;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        drawField(painter, option, t, option->state & State_ReadOnly, QColor());
        break;
    }

    case PE_FrameLineEdit:
        // The panel draws a field's border.
        break;

    case PE_IndicatorCheckBox:
    case PE_IndicatorItemViewItemCheck:
        drawCheckIndicator(painter, option, t);
        break;

    case PE_IndicatorRadioButton:
        drawRadioIndicator(painter, option, t);
        break;

    case PE_FrameFocusRect: {
        if (!(option->state & State_KeyboardFocusChange))
        {
            break;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (itemView(widget))
        {
            painter->setPen(QPen(c.accentLine, kBorder));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5));
            break;
        }
        strokeRounded(painter, QRectF(option->rect), t.radius.sm, c.borderFocus, kFocusRing);
        break;
    }

    case PE_PanelItemViewRow: {
        // A row's background: a selected row's, which a table and a tree's branches paint here as well as in the
        // item's panel, and an alternate row's.
        const auto* item = qstyleoption_cast<const QStyleOptionViewItem*>(option);
        const PainterGuard guard(painter);
        if (option->state & State_Selected)
        {
            painter->fillRect(option->rect, selectionColour(option, c));
        }
        else if (item != nullptr && (item->features & QStyleOptionViewItem::Alternate))
        {
            painter->fillRect(option->rect, option->palette.brush(QPalette::AlternateBase));
        }
        break;
    }

    case PE_PanelItemViewItem: {
        const auto* item = qstyleoption_cast<const QStyleOptionViewItem*>(option);
        const PainterGuard guard(painter);
        const QRectF rect(option->rect);
        if (option->state & State_Selected)
        {
            painter->fillRect(rect, selectionColour(option, c));
            const auto position = item != nullptr ? item->viewItemPosition : QStyleOptionViewItem::Invalid;
            if (position == QStyleOptionViewItem::Beginning || position == QStyleOptionViewItem::OnlyOne ||
                position == QStyleOptionViewItem::Invalid)
            {
                const bool rightToLeft = option->direction == Qt::RightToLeft;
                painter->fillRect(rightToLeft
                                      ? QRectF(rect.right() - kMarkBar + 1.0, rect.top(), kMarkBar, rect.height())
                                      : QRectF(rect.left(), rect.top(), kMarkBar, rect.height()),
                                  c.accentPrimary);
            }
        }
        else if (hovered(option))
        {
            painter->fillRect(rect, atShare(c.stateLayer, kHoverShare));
        }
        break;
    }

    case PE_PanelTipLabel:
        drawRaisedPanel(painter, option, widget, t.layers.surface4, t.radius.sm, t);
        break;

    case PE_PanelMenu:
        drawRaisedPanel(painter, option, widget, c.bgInput, t.radius.md, t);
        break;

    case PE_FrameMenu:
        // The panel draws a menu's hairline.
        break;

    case PE_PanelMenuBar:
    case PE_PanelStatusBar: {
        const PainterGuard guard(painter);
        const QRectF rect(option->rect);
        painter->fillRect(rect, t.materials.solidSurface);
        painter->setPen(QPen(c.borderSubtle, kBorder));
        const qreal y = element == PE_PanelStatusBar ? rect.top() + 0.5 : rect.bottom() - 0.5;
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        break;
    }

    case PE_PanelToolBar:
        // The tool bar's own control draws it, flat.
        break;

    case PE_IndicatorToolBarSeparator: {
        const PainterGuard guard(painter);
        painter->setPen(QPen(c.borderSubtle, kBorder));
        const QRectF rect(option->rect);
        if (option->state & State_Horizontal)
        {
            const qreal x = std::floor(rect.center().x()) + 0.5;
            const qreal half = (std::min)(kSeparatorLength, rect.height()) / 2.0;
            painter->drawLine(QPointF(x, rect.center().y() - half), QPointF(x, rect.center().y() + half));
        }
        else
        {
            const qreal y = std::floor(rect.center().y()) + 0.5;
            const qreal half = (std::min)(kSeparatorLength, rect.width()) / 2.0;
            painter->drawLine(QPointF(rect.center().x() - half, y), QPointF(rect.center().x() + half, y));
        }
        break;
    }

    case PE_IndicatorToolBarHandle:
        break;

    case PE_FrameTabBarBase: {
        const auto* base = qstyleoption_cast<const QStyleOptionTabBarBase*>(option);
        const PainterGuard guard(painter);
        painter->setPen(QPen(c.borderSubtle, kBorder));
        const QRectF rect(option->rect);
        const QTabBar::Shape shape = base != nullptr ? base->shape : QTabBar::RoundedNorth;
        switch (shape)
        {
        case QTabBar::RoundedSouth:
        case QTabBar::TriangularSouth:
            painter->drawLine(QPointF(rect.left(), rect.top() + 0.5), QPointF(rect.right(), rect.top() + 0.5));
            break;
        case QTabBar::RoundedWest:
        case QTabBar::TriangularWest:
            painter->drawLine(QPointF(rect.right() - 0.5, rect.top()), QPointF(rect.right() - 0.5, rect.bottom()));
            break;
        case QTabBar::RoundedEast:
        case QTabBar::TriangularEast:
            painter->drawLine(QPointF(rect.left() + 0.5, rect.top()), QPointF(rect.left() + 0.5, rect.bottom()));
            break;
        default:
            painter->drawLine(QPointF(rect.left(), rect.bottom() - 0.5), QPointF(rect.right(), rect.bottom() - 0.5));
            break;
        }
        break;
    }

    case PE_FrameTabWidget: {
        // No frame: the pages lie on the window; a hairline where the tabs meet them.
        const auto* frame = qstyleoption_cast<const QStyleOptionTabWidgetFrame*>(option);
        const PainterGuard guard(painter);
        painter->setPen(QPen(c.borderSubtle, kBorder));
        const QRectF rect(option->rect);
        const QTabBar::Shape shape = frame != nullptr ? frame->shape : QTabBar::RoundedNorth;
        switch (shape)
        {
        case QTabBar::RoundedSouth:
        case QTabBar::TriangularSouth:
            painter->drawLine(QPointF(rect.left(), rect.bottom() - 0.5), QPointF(rect.right(), rect.bottom() - 0.5));
            break;
        case QTabBar::RoundedWest:
        case QTabBar::TriangularWest:
            painter->drawLine(QPointF(rect.left() + 0.5, rect.top()), QPointF(rect.left() + 0.5, rect.bottom()));
            break;
        case QTabBar::RoundedEast:
        case QTabBar::TriangularEast:
            painter->drawLine(QPointF(rect.right() - 0.5, rect.top()), QPointF(rect.right() - 0.5, rect.bottom()));
            break;
        default:
            painter->drawLine(QPointF(rect.left(), rect.top() + 0.5), QPointF(rect.right(), rect.top() + 0.5));
            break;
        }
        break;
    }

    case PE_FrameGroupBox: {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        fillRounded(painter, QRectF(option->rect), t.radius.lg, c.bgCard);
        break;
    }

    case PE_Frame: {
        if (widget == nullptr)
        {
            return false;
        }
        const QString name = widget->objectName();
        if (name == QLatin1String("anProcessShellCard"))
        {
            const PainterGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);
            fillRounded(painter, QRectF(option->rect), t.radius.lg, c.bgCard);
            break;
        }
        if (name == QLatin1String("anProcessShellBanner"))
        {
            drawNote(painter, option->rect, toneColour(propertyText(widget, "anProcessMessageTone"), c), t,
                     option->direction);
            break;
        }
        if (name == QLatin1String("anContextMessage"))
        {
            drawNote(painter, option->rect, toneColour(propertyText(widget, "anContextMessageTone"), c), t,
                     option->direction);
            break;
        }
        if (itemView(widget))
        {
            const PainterGuard guard(painter);
            painter->setPen(QPen(c.borderSubtle, kBorder));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5));
            break;
        }
        return false;
    }

    default:
        return false;
    }
    g_observatoryPaints.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// Controls
// ════════════════════════════════════════════════════════════════════════════

bool AnProxyStyle::drawObservatoryControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                                          const QWidget* widget) const
{
    const DesignTokens& t = *m_tokens;
    const ColorTokens& c = t.colors;
    switch (element)
    {
    case CE_PushButton:
        // The bevel holds the focus ring at the button's edge, so no focus frame is drawn inside it.
        drawButtonBevel(painter, option, widget, t);
        drawButtonLabel(painter, option, widget, t);
        break;

    case CE_PushButtonBevel:
        drawButtonBevel(painter, option, widget, t);
        break;

    case CE_PushButtonLabel:
        drawButtonLabel(painter, option, widget, t);
        break;

    case CE_ToolButtonLabel: {
        const auto* tool = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        if (tool == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        QStyleOptionToolButton label(*tool);
        if (!enabled(option))
        {
            // Painted whole at the disabled opacity, in its own colours.
            painter->setOpacity(kDisabledOpacity);
            label.state |= State_Enabled;
        }
        const bool quiet = propertyText(widget, "anButtonVariant") == QLatin1String("quiet");
        if (on(option) && !quiet)
        {
            label.palette.setColor(QPalette::ButtonText, c.textOnAccentContainer);
            label.palette.setColor(QPalette::WindowText, c.textOnAccentContainer);
        }
        QProxyStyle::drawControl(element, &label, painter, widget);
        break;
    }

    case CE_CheckBoxLabel:
    case CE_RadioButtonLabel: {
        if (enabled(option))
        {
            return false;
        }
        const PainterGuard guard(painter);
        painter->setOpacity(kDisabledOpacity);
        if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option))
        {
            QStyleOptionButton label(*button);
            label.state |= State_Enabled;
            QProxyStyle::drawControl(element, &label, painter, widget);
        }
        break;
    }

    case CE_ComboBoxLabel: {
        const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
        if (combo == nullptr || combo->editable)
        {
            return false;
        }
        const PainterGuard guard(painter);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        QRect field = subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget);
        if (!combo->currentIcon.isNull())
        {
            const QSize iconSize = combo->iconSize.isValid() ? combo->iconSize : QSize(16, 16);
            const QRect iconRect(field.left(), field.top() + (field.height() - iconSize.height()) / 2, iconSize.width(),
                                 iconSize.height());
            combo->currentIcon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal);
            field.setLeft(iconRect.right() + 1 + t.spacing.xs + t.spacing.xs / 2);
        }
        painter->setClipRect(field);
        painter->setPen(c.textPrimary);
        const QString text =
            painter->fontMetrics().elidedText(combo->currentText, Qt::ElideMiddle, (std::max)(0, field.width()));
        painter->drawText(field, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, text);
        break;
    }

    case CE_ProgressBar: {
        const auto* bar = qstyleoption_cast<const QStyleOptionProgressBar*>(option);
        if (bar == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        const bool horizontal = bar->state & State_Horizontal;
        const QRectF rect(option->rect);
        // Without its text a bar is a thin track across its middle; with it, the bar's whole height.
        QRectF track = rect;
        qreal radius = (std::min)(static_cast<qreal>(t.radius.sm), (horizontal ? rect.height() : rect.width()) / 2.0);
        if (!bar->textVisible)
        {
            track = horizontal
                        ? QRectF(rect.left(), std::round(rect.center().y() - kTrack / 2.0), rect.width(), kTrack)
                        : QRectF(std::round(rect.center().x() - kTrack / 2.0), rect.top(), kTrack, rect.height());
            radius = kTrack / 2.0;
        }
        const QColor tone = usageToneColour(widget, c);
        const QColor fill = tone.isValid() ? tone : c.accentPrimary;
        fillRounded(painter, track, radius, tone.isValid() ? atShare(tone, kSoftShare) : c.accentTrack);

        QRectF chunk;
        const qint64 range = static_cast<qint64>(bar->maximum) - bar->minimum;
        if (range > 0)
        {
            const qreal share = std::clamp(static_cast<qreal>(static_cast<qint64>(bar->progress) - bar->minimum) /
                                               static_cast<qreal>(range),
                                           0.0, 1.0);
            const bool inverted = bar->invertedAppearance != (option->direction == Qt::RightToLeft && horizontal);
            if (horizontal)
            {
                const qreal width = track.width() * share;
                chunk = inverted ? QRectF(track.right() - width, track.top(), width, track.height())
                                 : QRectF(track.left(), track.top(), width, track.height());
            }
            else
            {
                const qreal height = track.height() * share;
                chunk = bar->invertedAppearance ? QRectF(track.left(), track.top(), track.width(), height)
                                                : QRectF(track.left(), track.bottom() - height, track.width(), height);
            }
            QPainterPath trackShape;
            trackShape.addRoundedRect(track, radius, radius);
            painter->save();
            painter->setClipPath(trackShape);
            fillRounded(painter, chunk, radius, fill);
            painter->restore();
        }
        if (bar->textVisible && !bar->text.isEmpty())
        {
            // The text in the primary text over the track and in the text on the fill over the fill.
            const QColor onFill = tone.isValid()
                                      ? bestContrastTextColor(fill, c.textOnAccent, c.textPrimary, c.bgPrimary, 4.5)
                                      : c.textOnAccent;
            QRegion outside(option->rect);
            if (!chunk.isEmpty())
            {
                outside -= QRegion(chunk.toAlignedRect());
            }
            painter->save();
            painter->setClipRegion(outside);
            painter->setPen(c.textPrimary);
            painter->drawText(option->rect, Qt::AlignCenter, bar->text);
            painter->restore();
            if (!chunk.isEmpty())
            {
                painter->save();
                painter->setClipRect(chunk);
                painter->setPen(onFill);
                painter->drawText(option->rect, Qt::AlignCenter, bar->text);
                painter->restore();
            }
        }
        break;
    }

    case CE_TabBarTabShape: {
        const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
        if (tab == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        const bool forceUnselected = widget != nullptr && widget->property("forceUnselected").toBool();
        const bool selected = !forceUnselected && (tab->state & State_Selected);
        const QRectF rect(option->rect);
        if (!selected)
        {
            fillRounded(painter, rect.adjusted(2.0, 4.0, -2.0, -4.0), t.radius.sm, stateLayer(c, option));
            break;
        }
        const bool top = propertyText(widget, "anTabAccentLinePosition") == QLatin1String("top");
        QRectF bar;
        switch (tab->shape)
        {
        case QTabBar::RoundedSouth:
        case QTabBar::TriangularSouth:
            bar = QRectF(rect.left() + kTabBarInset, rect.top(), rect.width() - 2 * kTabBarInset, kMarkBar);
            break;
        case QTabBar::RoundedWest:
        case QTabBar::TriangularWest:
            bar =
                QRectF(rect.right() - kMarkBar, rect.top() + kTabBarInset, kMarkBar, rect.height() - 2 * kTabBarInset);
            break;
        case QTabBar::RoundedEast:
        case QTabBar::TriangularEast:
            bar = QRectF(rect.left(), rect.top() + kTabBarInset, kMarkBar, rect.height() - 2 * kTabBarInset);
            break;
        default:
            bar = top ? QRectF(rect.left() + kTabBarInset, rect.top(), rect.width() - 2 * kTabBarInset, kMarkBar)
                      : QRectF(rect.left() + kTabBarInset, rect.bottom() - kMarkBar, rect.width() - 2 * kTabBarInset,
                               kMarkBar);
            break;
        }
        fillRounded(painter, bar, kMarkBar / 2.0, c.accentPrimary);
        break;
    }

    case CE_TabBarTabLabel: {
        const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
        if (tab == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        QStyleOptionTab label(*tab);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
            label.state |= State_Enabled;
        }
        const bool forceUnselected = widget != nullptr && widget->property("forceUnselected").toBool();
        const bool selected = !forceUnselected && (tab->state & State_Selected);
        const QColor text = selected ? c.textPrimary : c.textSecondary;
        label.palette.setColor(QPalette::WindowText, text);
        label.palette.setColor(QPalette::ButtonText, text);
        QFont font = painter->font();
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        QProxyStyle::drawControl(element, &label, painter, widget);
        break;
    }

    case CE_HeaderSection: {
        const PainterGuard guard(painter);
        const QRectF rect(option->rect);
        painter->fillRect(rect, c.bgInput);
        if (const QColor layer = stateLayer(c, option); layer.isValid())
        {
            painter->fillRect(rect, layer);
        }
        const auto* header = qstyleoption_cast<const QStyleOptionHeader*>(option);
        painter->setPen(QPen(c.borderSubtle, kBorder));
        if (header != nullptr && header->orientation == Qt::Vertical)
        {
            painter->drawLine(QPointF(rect.right() - 0.5, rect.top()), QPointF(rect.right() - 0.5, rect.bottom()));
        }
        else
        {
            painter->drawLine(QPointF(rect.left(), rect.bottom() - 0.5), QPointF(rect.right(), rect.bottom() - 0.5));
        }
        break;
    }

    case CE_HeaderEmptyArea: {
        const PainterGuard guard(painter);
        const QRectF rect(option->rect);
        painter->fillRect(rect, c.bgInput);
        painter->setPen(QPen(c.borderSubtle, kBorder));
        painter->drawLine(QPointF(rect.left(), rect.bottom() - 0.5), QPointF(rect.right(), rect.bottom() - 0.5));
        break;
    }

    case CE_HeaderLabel: {
        const auto* header = qstyleoption_cast<const QStyleOptionHeader*>(option);
        if (header == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        QRect rect = option->rect.adjusted(t.spacing.sm, 0, -t.spacing.sm, 0);
        if (!header->icon.isNull())
        {
            const int extent = pixelMetric(PM_SmallIconSize, option, widget);
            const QRect iconRect(rect.left(), rect.center().y() - extent / 2, extent, extent);
            header->icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal);
            rect.setLeft(iconRect.right() + 1 + t.spacing.xs);
        }
        const QFont font = headerFont(t);
        painter->setFont(font);
        painter->setPen(c.textSecondary);
        const QString text = QFontMetrics(font).elidedText(header->text, Qt::ElideRight, (std::max)(0, rect.width()));
        painter->drawText(rect, static_cast<int>(header->textAlignment) | Qt::AlignVCenter | Qt::TextSingleLine, text);
        break;
    }

    case CE_ItemViewItem: {
        const auto* item = qstyleoption_cast<const QStyleOptionViewItem*>(option);
        if (item == nullptr || !(option->state & State_Selected))
        {
            return false;
        }
        // The selection is the accent's soft share, so its text stays the primary text.
        QStyleOptionViewItem selected(*item);
        selected.palette.setColor(QPalette::HighlightedText, c.textPrimary);
        selected.palette.setColor(QPalette::Text, c.textPrimary);
        selected.palette.setColor(QPalette::WindowText, c.textPrimary);
        QProxyStyle::drawControl(element, &selected, painter, widget);
        break;
    }

    case CE_MenuItem: {
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (item == nullptr)
        {
            return false;
        }
        drawObservatoryMenuItem(painter, item, t, styleHint(SH_UnderlineShortcut, option, widget) != 0);
        break;
    }

    case CE_MenuEmptyArea:
    case CE_MenuHMargin:
    case CE_MenuVMargin:
        // The panel is the menu's background.
        break;

    case CE_MenuBarEmptyArea:
    case CE_MenuBarItem: {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF rect(option->rect);
        painter->fillRect(rect, t.materials.solidSurface);
        if (widget != nullptr)
        {
            // The bar's hairline runs under its items too.
            const qreal y = widget->rect().bottom() + 0.5;
            painter->setPen(QPen(c.borderSubtle, kBorder));
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right() + 1.0, y));
        }
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (element == CE_MenuBarEmptyArea || item == nullptr)
        {
            break;
        }
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        const bool open = option->state & State_Sunken;
        const bool active = (option->state & (State_Selected | State_MouseOver)) && enabled(option);
        if (active || open)
        {
            fillRounded(painter, rect.adjusted(2.0, 4.0, -2.0, -4.0), t.radius.sm,
                        atShare(c.stateLayer, open ? kPressedShare : kHoverShare));
        }
        painter->setPen(c.textPrimary);
        const bool underline = styleHint(SH_UnderlineShortcut, option, widget) != 0;
        painter->drawText(option->rect,
                          Qt::AlignCenter | Qt::TextSingleLine |
                              (underline ? Qt::TextShowMnemonic : Qt::TextHideMnemonic),
                          item->text);
        break;
    }

    case CE_ToolBar: {
        const PainterGuard guard(painter);
        const QRectF rect(option->rect);
        const bool card = widget != nullptr && widget->property("anUseCardBackground").toBool();
        painter->fillRect(rect, card ? c.bgCard : t.materials.solidSurface);
        painter->setPen(QPen(c.borderSubtle, kBorder));
        const auto* bar = qstyleoption_cast<const QStyleOptionToolBar*>(option);
        if (bar != nullptr && bar->toolBarArea == Qt::LeftToolBarArea)
        {
            painter->drawLine(QPointF(rect.right() - 0.5, rect.top()), QPointF(rect.right() - 0.5, rect.bottom()));
        }
        else if (bar != nullptr && bar->toolBarArea == Qt::RightToolBarArea)
        {
            painter->drawLine(QPointF(rect.left() + 0.5, rect.top()), QPointF(rect.left() + 0.5, rect.bottom()));
        }
        else
        {
            painter->drawLine(QPointF(rect.left(), rect.bottom() - 0.5), QPointF(rect.right(), rect.bottom() - 0.5));
        }
        break;
    }

    case CE_Splitter: {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        const bool active = hovered(option) || pressed(option);
        const bool process = propertyText(widget, "anSplitterVariant") == QLatin1String("process") ||
                             (widget != nullptr && widget->parentWidget() != nullptr &&
                              propertyText(widget->parentWidget(), "anSplitterVariant") == QLatin1String("process"));
        const QRectF rect(option->rect);
        if (!process)
        {
            painter->fillRect(rect, active ? c.accentLine : c.borderSubtle);
            break;
        }
        if (active)
        {
            fillRounded(painter, rect, t.radius.sm, atShare(c.stateLayer, kHoverShare));
        }
        const bool vertical = rect.height() > rect.width();
        const QRectF grip = vertical ? QRectF(rect.center().x() - kGrip / 2.0, rect.top() + kGripInset, kGrip,
                                              rect.height() - 2 * kGripInset)
                                     : QRectF(rect.left() + kGripInset, rect.center().y() - kGrip / 2.0,
                                              rect.width() - 2 * kGripInset, kGrip);
        fillRounded(painter, grip, kGrip / 2.0, active ? c.accentLine : c.borderPrimary);
        break;
    }

    case CE_SizeGrip: {
        const PainterGuard guard(painter);
        const QColor dot = atShare(c.textTertiary, 0.5);
        const bool rightToLeft = option->direction == Qt::RightToLeft;
        const QPoint centre = option->rect.center();
        const int step = 3;
        const int reach = 6;
        for (int i = -reach; i < 2 * reach; i += step)
        {
            for (int j = -reach; j < 2 * reach; j += step)
            {
                if ((!rightToLeft && 0 <= j + i) || (rightToLeft && 0 <= j - i))
                {
                    painter->fillRect(centre.x() + i, centre.y() + j, 2, 2, dot);
                }
            }
        }
        break;
    }

    case CE_RubberBand: {
        const PainterGuard guard(painter);
        painter->fillRect(option->rect, c.accentSoft);
        painter->setPen(QPen(c.accentPrimary, kBorder));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5));
        break;
    }

    default:
        return false;
    }
    g_observatoryPaints.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// Complex controls
// ════════════════════════════════════════════════════════════════════════════

bool AnProxyStyle::drawObservatoryComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                                                 QPainter* painter, const QWidget* widget) const
{
    const DesignTokens& t = *m_tokens;
    const ColorTokens& c = t.colors;
    switch (control)
    {
    case CC_ComboBox: {
        const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
        if (combo == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        // A list it opens is a button's: hovered and opened, the state colour.
        QColor layer;
        if (!combo->editable && enabled(option))
        {
            layer = (option->state & (State_On | State_Sunken)) ? atShare(c.stateLayer, kPressedShare)
                    : hovered(option)                           ? atShare(c.stateLayer, kHoverShare)
                                                                : QColor();
        }
        drawField(painter, option, t, false, layer);
        if (combo->subControls & SC_ComboBoxArrow)
        {
            const QRect arrow = subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
            drawChevron(painter, QRectF(arrow).center(), kChevron, c.textSecondary, Qt::DownArrow);
        }
        break;
    }

    case CC_SpinBox: {
        const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
        if (spin == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        drawField(painter, option, t, false, QColor());
        if (spin->buttonSymbols == QAbstractSpinBox::NoButtons)
        {
            break;
        }
        QPainterPath field;
        field.addRoundedRect(QRectF(option->rect), t.radius.md, t.radius.md);
        for (const SubControl button : {SC_SpinBoxUp, SC_SpinBoxDown})
        {
            const QRect rect = subControlRect(CC_SpinBox, spin, button, widget);
            const bool buttonEnabled = button == SC_SpinBoxUp ? (spin->stepEnabled & QAbstractSpinBox::StepUpEnabled)
                                                              : (spin->stepEnabled & QAbstractSpinBox::StepDownEnabled);
            if ((spin->activeSubControls & button) && enabled(option) && buttonEnabled)
            {
                const QColor layer = (option->state & State_Sunken)      ? atShare(c.stateLayer, kPressedShare)
                                     : (option->state & State_MouseOver) ? atShare(c.stateLayer, kHoverShare)
                                                                         : QColor();
                if (layer.isValid())
                {
                    painter->save();
                    painter->setClipPath(field);
                    painter->fillRect(rect, layer);
                    painter->restore();
                }
            }
            const QColor arrow = buttonEnabled ? c.textSecondary : c.textTertiary;
            const QPointF centre = QRectF(rect).center();
            drawChevron(painter, button == SC_SpinBoxUp ? centre + QPointF(0.0, 1.0) : centre - QPointF(0.0, 1.0),
                        kChevron * 0.75, arrow, button == SC_SpinBoxUp ? Qt::UpArrow : Qt::DownArrow);
        }
        break;
    }

    case CC_Slider: {
        const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (slider == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        const bool horizontal = slider->orientation == Qt::Horizontal;
        const QRectF groove(subControlRect(CC_Slider, slider, SC_SliderGroove, widget));
        const QRectF handle(subControlRect(CC_Slider, slider, SC_SliderHandle, widget));
        const QPointF knob = handle.center();
        const QRectF track = horizontal
                                 ? QRectF(groove.left(), std::round(knob.y() - kTrack / 2.0), groove.width(), kTrack)
                                 : QRectF(std::round(knob.x() - kTrack / 2.0), groove.top(), kTrack, groove.height());
        // The track is not painted under the knob: each part is painted once, so a disabled slider at its opacity is
        // the slider at that opacity.
        QPainterPath beside;
        beside.addRect(QRectF(option->rect));
        QPainterPath knobShape;
        knobShape.addEllipse(knob, kKnob / 2.0, kKnob / 2.0);
        painter->save();
        painter->setClipPath(beside.subtracted(knobShape));
        fillRounded(painter, track, kTrack / 2.0, c.accentTrack);
        // The filled part runs from the slider's minimum to its knob.
        QRectF filled;
        if (horizontal)
        {
            const bool fromRight = slider->upsideDown;
            filled = fromRight ? QRectF(knob.x(), track.top(), track.right() - knob.x(), kTrack)
                               : QRectF(track.left(), track.top(), knob.x() - track.left(), kTrack);
        }
        else
        {
            const bool fromTop = slider->upsideDown;
            filled = fromTop ? QRectF(track.left(), track.top(), kTrack, knob.y() - track.top())
                             : QRectF(track.left(), knob.y(), kTrack, track.bottom() - knob.y());
        }
        fillRounded(painter, filled, kTrack / 2.0, c.accentPrimary);
        painter->restore();

        if ((slider->subControls & SC_SliderTickmarks) && slider->tickPosition != QSlider::NoTicks &&
            slider->maximum > slider->minimum)
        {
            const int interval = slider->tickInterval > 0 ? slider->tickInterval
                                                          : (std::max)(1, slider->pageStep > 0 ? slider->pageStep : 1);
            const int length =
                static_cast<int>(horizontal ? groove.width() - handle.width() : groove.height() - handle.height());
            painter->setPen(QPen(c.textTertiary, kBorder));
            for (int value = slider->minimum; value <= slider->maximum; value += interval)
            {
                const int at =
                    sliderPositionFromValue(slider->minimum, slider->maximum, value, length, slider->upsideDown);
                const qreal centre =
                    (horizontal ? groove.left() + handle.width() / 2.0 : groove.top() + handle.height() / 2.0) + at +
                    0.5;
                const bool before = slider->tickPosition & QSlider::TicksAbove;
                const bool after = slider->tickPosition & QSlider::TicksBelow;
                if (horizontal)
                {
                    if (before)
                    {
                        painter->drawLine(QPointF(centre, groove.top()), QPointF(centre, groove.top() + 3.0));
                    }
                    if (after)
                    {
                        painter->drawLine(QPointF(centre, groove.bottom() - 3.0), QPointF(centre, groove.bottom()));
                    }
                }
                else
                {
                    if (before)
                    {
                        painter->drawLine(QPointF(groove.left(), centre), QPointF(groove.left() + 3.0, centre));
                    }
                    if (after)
                    {
                        painter->drawLine(QPointF(groove.right() - 3.0, centre), QPointF(groove.right(), centre));
                    }
                }
            }
        }

        const bool onKnob = (slider->activeSubControls & SC_SliderHandle) &&
                            ((option->state & State_MouseOver) || (option->state & State_Sunken)) && enabled(option);
        const qreal halo = (std::min)(handle.width(), handle.height()) / 2.0;
        if (onKnob)
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(c.accentSoft);
            painter->drawEllipse(knob, halo, halo);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(onKnob ? c.accentHover : c.accentPrimary);
        painter->drawEllipse(knob, kKnob / 2.0, kKnob / 2.0);
        if (keyboardFocus(option))
        {
            painter->setPen(QPen(c.borderFocus, kFocusRing));
            painter->setBrush(Qt::NoBrush);
            const qreal ring = kKnob / 2.0 + kFocusRing;
            painter->drawEllipse(knob, ring, ring);
        }
        break;
    }

    case CC_ScrollBar: {
        const auto* bar = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (bar == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled(option))
        {
            painter->setOpacity(kDisabledOpacity);
        }
        if (bar->maximum <= bar->minimum)
        {
            break;
        }
        const QRectF handle(subControlRect(CC_ScrollBar, bar, SC_ScrollBarSlider, widget));
        const bool active = (bar->activeSubControls & SC_ScrollBarSlider) &&
                            ((option->state & State_MouseOver) || (option->state & State_Sunken)) && enabled(option);
        const bool horizontal = bar->orientation == Qt::Horizontal;
        const QRectF shape = horizontal ? handle.adjusted(1.0, 0.0, -1.0, 0.0) : handle.adjusted(0.0, 1.0, 0.0, -1.0);
        const qreal radius = (std::min)(static_cast<qreal>(t.metrics.scrollBar) / 2.0,
                                        (horizontal ? shape.height() : shape.width()) / 2.0);
        fillRounded(painter, shape, radius, atShare(c.textTertiary, active ? kActiveHandleShare : kHandleShare));
        break;
    }

    case CC_GroupBox: {
        const auto* group = qstyleoption_cast<const QStyleOptionGroupBox*>(option);
        if (group == nullptr)
        {
            return false;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        const bool flat = propertyText(widget, "anGroupVariant") == QLatin1String("flat") ||
                          (group->features & QStyleOptionFrame::Flat);
        if (group->subControls & SC_GroupBoxFrame)
        {
            const QRectF frame(subControlRect(CC_GroupBox, group, SC_GroupBoxFrame, widget));
            if (flat)
            {
                painter->setPen(QPen(c.borderSubtle, kBorder));
                painter->drawLine(QPointF(frame.left(), frame.top() + 0.5), QPointF(frame.right(), frame.top() + 0.5));
            }
            else
            {
                fillRounded(painter, frame, t.radius.lg, c.bgCard);
            }
        }
        if ((group->subControls & SC_GroupBoxLabel) && !group->text.isEmpty())
        {
            const QRect title = subControlRect(CC_GroupBox, group, SC_GroupBoxLabel, widget);
            painter->save();
            if (!enabled(option))
            {
                painter->setOpacity(kDisabledOpacity);
            }
            QFont font = painter->font();
            font.setWeight(QFont::DemiBold);
            painter->setFont(font);
            painter->setPen(c.textSecondary);
            painter->drawText(title, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic, group->text);
            painter->restore();
        }
        if (group->subControls & SC_GroupBoxCheckBox)
        {
            QStyleOptionButton check;
            check.QStyleOption::operator=(*group);
            check.rect = subControlRect(CC_GroupBox, group, SC_GroupBoxCheckBox, widget);
            check.state = (check.state & ~(State_On | State_Off)) | ((group->state & State_On) ? State_On : State_Off);
            drawCheckIndicator(painter, &check, t);
        }
        break;
    }

    default:
        return false;
    }
    g_observatoryPaints.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// Geometry, hints, polish
// ════════════════════════════════════════════════════════════════════════════

std::optional<QRect> AnProxyStyle::observatorySubControlRect(ComplexControl control, const QStyleOptionComplex* option,
                                                             SubControl subControl, const QWidget* widget) const
{
    Q_UNUSED(widget);
    const QRect r = option->rect;
    switch (control)
    {
    case CC_ComboBox: {
        const QRect arrow(r.right() - kComboArrowColumn + 1, r.top(), kComboArrowColumn, r.height());
        switch (subControl)
        {
        case SC_ComboBoxArrow:
            return visualRect(option->direction, r, arrow);
        case SC_ComboBoxEditField: {
            const int left = r.left() + static_cast<int>(kBorder) + kFieldPadding;
            return visualRect(option->direction, r, QRect(left, r.top() + 1, arrow.left() - left, r.height() - 2));
        }
        case SC_ComboBoxFrame:
        case SC_ComboBoxListBoxPopup:
            return r;
        default:
            return std::nullopt;
        }
    }

    case CC_SpinBox: {
        const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
        const bool buttons = spin == nullptr || spin->buttonSymbols != QAbstractSpinBox::NoButtons;
        const int border = static_cast<int>(kBorder);
        const int column = buttons ? kSpinButtonColumn : 0;
        const int inner = r.height() - 2 * border;
        const QRect up(r.right() - border - column + 1, r.top() + border, column, inner / 2);
        const QRect down(up.left(), up.bottom() + 1, column, inner - inner / 2);
        const int left = r.left() + border + kFieldPadding - kLineEditMargin;
        const int right = buttons ? up.left() - 1 : r.right() - border - kFieldPadding + kLineEditMargin;
        switch (subControl)
        {
        case SC_SpinBoxUp:
            return buttons ? visualRect(option->direction, r, up) : QRect();
        case SC_SpinBoxDown:
            return buttons ? visualRect(option->direction, r, down) : QRect();
        case SC_SpinBoxEditField:
            return visualRect(option->direction, r, QRect(left, r.top() + border, right - left + 1, inner));
        case SC_SpinBoxFrame:
            return r;
        default:
            return std::nullopt;
        }
    }

    case CC_ScrollBar: {
        const auto* bar = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (bar == nullptr)
        {
            return std::nullopt;
        }
        // No arrows: the groove is the whole bar, and the handle and the pages share it.
        const bool horizontal = bar->orientation == Qt::Horizontal;
        const int length = horizontal ? r.width() : r.height();
        const qint64 range = static_cast<qint64>(bar->maximum) - bar->minimum;
        int handleLength = length;
        if (range > 0)
        {
            const qint64 proportional = (static_cast<qint64>(bar->pageStep) * length) / (range + bar->pageStep);
            handleLength = static_cast<int>(
                std::clamp<qint64>(proportional, pixelMetric(PM_ScrollBarSliderMin, bar, widget), length));
        }
        const int start = range > 0 ? sliderPositionFromValue(bar->minimum, bar->maximum, bar->sliderPosition,
                                                              length - handleLength, bar->upsideDown)
                                    : 0;
        const auto along = [&](const int from, const int extent) {
            return horizontal ? QRect(r.left() + from, r.top(), extent, r.height())
                              : QRect(r.left(), r.top() + from, r.width(), extent);
        };
        QRect rect;
        switch (subControl)
        {
        case SC_ScrollBarSubLine:
        case SC_ScrollBarAddLine:
        case SC_ScrollBarFirst:
        case SC_ScrollBarLast:
            return QRect();
        case SC_ScrollBarGroove:
            return r;
        case SC_ScrollBarSlider:
            rect = along(start, handleLength);
            break;
        case SC_ScrollBarSubPage:
            rect = along(0, start);
            break;
        case SC_ScrollBarAddPage:
            rect = along(start + handleLength, length - start - handleLength);
            break;
        default:
            return std::nullopt;
        }
        return horizontal ? visualRect(option->direction, r, rect) : rect;
    }

    default:
        return std::nullopt;
    }
}

std::optional<int> AnProxyStyle::observatoryStyleHint(StyleHint hint, const QStyleOption* option,
                                                      const QWidget* widget) const
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    switch (hint)
    {
    case SH_Table_GridLineColor:
        // Grid lines are hairlines.
        return static_cast<int>(m_tokens->colors.borderSubtle.rgba());
    case SH_ToolTipLabel_Opacity:
        return 255;
    case SH_Menu_Mask:
    case SH_ToolTip_Mask:
        // A raised part's rounded corners and its shadow come from its translucent window, not from a mask.
        return 0;
    default:
        return std::nullopt;
    }
}

void AnProxyStyle::polishObservatory(QWidget* widget)
{
    const DesignTokens& t = *m_tokens;
    const bool menu = qobject_cast<QMenu*>(widget) != nullptr;
    // A tooltip is a label shown as a tooltip window: Qt's own is one.
    const bool tip = widget->inherits("QTipLabel") ||
                     (qobject_cast<QLabel*>(widget) != nullptr && widget->windowType() == Qt::ToolTip);
    if (menu || tip)
    {
        // A raised window keeps the room its shadow is drawn in, from a translucent background.
        widget->setAttribute(Qt::WA_TranslucentBackground, true);
        widget->setContentsMargins(elevationWindowMargins());
    }
    if (menu)
    {
        widget->installEventFilter(this);
    }
    if (tip)
    {
        QFont font = t.typography.body;
        font.setPointSizeF(t.metrics.fontSmall * 72.0 / 96.0);
        widget->setFont(font);
        QPalette palette = widget->palette();
        palette.setColor(QPalette::ToolTipText, t.colors.textPrimary);
        palette.setColor(QPalette::WindowText, t.colors.textPrimary);
        palette.setColor(QPalette::ToolTipBase, t.layers.surface4);
        widget->setPalette(palette);
    }
    if (widget->inherits("QComboBoxPrivateContainer"))
    {
        // A combo box's list lies on the menu's surface.
        for (QAbstractItemView* view : widget->findChildren<QAbstractItemView*>())
        {
            QPalette palette = view->palette();
            palette.setColor(QPalette::Base, t.colors.bgInput);
            palette.setColor(QPalette::Window, t.colors.bgInput);
            view->setPalette(palette);
        }
    }
}

bool AnProxyStyle::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Show && styleProfile() == StyleProfile::Observatory)
    {
        if (auto* menu = qobject_cast<QMenu*>(watched);
            menu != nullptr && menu->isWindow() && menu->testAttribute(Qt::WA_TranslucentBackground))
        {
            // The menu was placed by its window; its surface lies inside the margin its shadow is drawn in, so the
            // window moves by that margin to put the surface where the menu was placed.
            const QMargins margins = menu->contentsMargins();
            menu->move(menu->pos() - QPoint(margins.left(), margins.top()));
        }
    }
    return QProxyStyle::eventFilter(watched, event);
}

namespace testing
{

std::uint64_t observatoryPaints()
{
    return g_observatoryPaints.load(std::memory_order_relaxed);
}

} // namespace testing

} // namespace an::ui
