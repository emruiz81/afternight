// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/KddwProxyStyle.h"

#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "theme/ObservatoryDock.h"
#include "theme/PainterGuard.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QStyleOptionDockWidget>
#include <QStyleOptionTab>
#include <QStyleOptionToolButton>
#include <QTabBar>
#include <QToolButton>

namespace
{

using an::ui::PainterGuard;

/// Global raw pointer — the dock main window owns the style via setStyle().
an::ui::KddwProxyStyle* g_installedKddwStyle = nullptr;

// ── State helpers ──────────────────────────────────────────────────────────

bool isHovered(const QStyleOption* opt)
{
    return (opt->state & QStyle::State_MouseOver) && (opt->state & QStyle::State_Enabled);
}

bool isPressed(const QStyleOption* opt)
{
    return (opt->state & QStyle::State_Sunken) && (opt->state & QStyle::State_Enabled);
}

bool isEnabled(const QStyleOption* opt)
{
    return opt->state & QStyle::State_Enabled;
}

bool isSelected(const QStyleOption* opt)
{
    return opt->state & QStyle::State_Selected;
}

bool isChecked(const QStyleOption* opt)
{
    return opt->state & QStyle::State_On;
}

// ── Widget ancestry check ──────────────────────────────────────────────────

bool isKddwWidget(const QWidget* widget)
{
    if (!widget)
        return false;

    // Walk up ancestry so child controls (for example tab close buttons) are
    // recognized as part of the KDDW subtree.
    for (const QWidget* current = widget; current; current = current->parentWidget())
    {
        const QString objName = current->objectName();
        if (objName == QLatin1String("DockTabBar") || objName == QLatin1String("DockTabCornerButton"))
        {
            return true;
        }

        if (current->inherits("KDDockWidgets::QtWidgets::Button"))
            return true;

        if (current->inherits("KDDockWidgets::QtWidgets::TitleBar"))
            return true;
    }

    return false;
}

} // anonymous namespace

namespace an::ui
{

// ════════════════════════════════════════════════════════════════════════════
// Construction
// ════════════════════════════════════════════════════════════════════════════

KddwProxyStyle::KddwProxyStyle(QStyle* baseStyle)
    : QProxyStyle(baseStyle ? baseStyle : new AnProxyStyle()), m_tokens(&tokens())
{
}

KddwProxyStyle::~KddwProxyStyle()
{
    if (g_installedKddwStyle == this)
    {
        g_installedKddwStyle = nullptr;
    }
}

void KddwProxyStyle::refreshThemeTokens()
{
    m_tokens = &tokens();

    // Re-polish the dock main window to cascade updated palettes.
    if (auto* dockWindow = qobject_cast<QWidget*>(parent()))
    {
        QPalette pal = dockWindow->palette();
        setupDockPalette(pal);
        dockWindow->setPalette(pal);

        for (auto* child : dockWindow->findChildren<QWidget*>())
        {
            polish(child);
            child->update();
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Polish
// ════════════════════════════════════════════════════════════════════════════

void KddwProxyStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);

    if (!widget)
        return;

    // Enable hover tracking for interactive KDDW widgets
    if (qobject_cast<QTabBar*>(widget) || qobject_cast<QToolButton*>(widget) || qobject_cast<QPushButton*>(widget))
    {
        widget->setAttribute(Qt::WA_Hover, true);
    }
}

void KddwProxyStyle::polish(QPalette& palette)
{
    setupDockPalette(palette);
}

void KddwProxyStyle::setupDockPalette(QPalette& palette) const
{
    const auto& c = m_tokens->colors;
    const auto& layers = m_tokens->layers;
    const auto& materials = m_tokens->materials;

    palette.setColor(QPalette::Window, materials.micaSurface);
    palette.setColor(QPalette::WindowText, c.textPrimary);
    palette.setColor(QPalette::Base, materials.solidSurface);
    palette.setColor(QPalette::AlternateBase, materials.acrylicSurface);
    palette.setColor(QPalette::Text, c.textPrimary);
    palette.setColor(QPalette::BrightText, c.textAccent);
    palette.setColor(QPalette::Button, layers.surface2);
    palette.setColor(QPalette::ButtonText, c.textPrimary);
    palette.setColor(QPalette::Highlight, c.accentPrimary);
    palette.setColor(QPalette::HighlightedText, c.textOnAccent);
    palette.setColor(QPalette::Mid, c.borderSubtle); // used by Group frame border
    palette.setColor(QPalette::Light, layers.surface4);
    palette.setColor(QPalette::Dark, c.borderSubtle);
    palette.setColor(QPalette::Shadow, c.shadowOverlay);
}

// ════════════════════════════════════════════════════════════════════════════
// drawPrimitive
// ════════════════════════════════════════════════════════════════════════════

void KddwProxyStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                                   const QWidget* widget) const
{
    if (observatoryDockDecorations() && drawObservatoryPrimitive(element, option, widget))
    {
        return;
    }
    switch (element)
    {
    case PE_PanelButtonTool:
        if (isKddwWidget(widget))
        {
            drawDockButtonPanel(option, painter, widget);
            return;
        }
        break;

    case PE_Frame:
    case PE_FrameGroupBox:
        if (isKddwWidget(widget))
        {
            drawDockGroupFrame(option, painter, widget);
            return;
        }
        break;

    case PE_Widget:
        // KDDW title bar calls PE_Widget before CE_DockWidgetTitle.
        // Paint the title bar background here.
        if (widget && widget->inherits("KDDockWidgets::QtWidgets::TitleBar"))
        {
            const PainterGuard guard(painter);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_tokens->layers.surface2);
            painter->drawRect(option->rect);
            return;
        }
        break;

    case PE_IndicatorTabClose:
        if (isKddwWidget(widget))
        {
            const PainterGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);

            const auto& c = m_tokens->colors;
            const QRectF rect = option->rect;

            QColor xColor = c.textTertiary;
            if (isPressed(option))
                xColor = c.statusError;
            else if (isHovered(option))
                xColor = c.textPrimary;

            if (isHovered(option) || isPressed(option))
            {
                const QColor bgColor = isPressed(option) ? c.bgCardSecondary : c.bgHover;
                painter->setPen(Qt::NoPen);
                painter->setBrush(bgColor);
                painter->drawEllipse(rect.adjusted(1.0, 1.0, -1.0, -1.0));
            }

            const qreal inset = qMin(rect.width(), rect.height()) * 0.22;
            const QRectF crossRect = rect.adjusted(inset, inset, -inset, -inset);
            painter->setPen(QPen(xColor, 1.5, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(crossRect.topLeft(), crossRect.bottomRight());
            painter->drawLine(crossRect.topRight(), crossRect.bottomLeft());
            return;
        }
        break;

    default:
        break;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

// ════════════════════════════════════════════════════════════════════════════
// drawControl
// ════════════════════════════════════════════════════════════════════════════

void KddwProxyStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                                 const QWidget* widget) const
{
    if (observatoryDockDecorations() && drawObservatoryControl(element, option, painter, widget))
    {
        return;
    }
    switch (element)
    {
    case CE_DockWidgetTitle:
        drawDockTitleBar(option, painter, widget);
        return;

    case CE_Splitter:
        drawDockSeparator(option, painter, widget);
        return;

    case CE_TabBarTabShape:
        drawDockTabShape(option, painter, widget);
        return;

    case CE_TabBarTabLabel:
        drawDockTabLabel(option, painter, widget);
        return;

    default:
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// drawComplexControl
// ════════════════════════════════════════════════════════════════════════════

void KddwProxyStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter,
                                        const QWidget* widget) const
{
    switch (control)
    {
    case CC_ToolButton:
        if (isKddwWidget(widget))
        {
            drawDockToolButton(option, painter, widget);
            return;
        }
        break;

    default:
        break;
    }

    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

// ════════════════════════════════════════════════════════════════════════════
// pixelMetric
// ════════════════════════════════════════════════════════════════════════════

int KddwProxyStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    if (const std::optional<int> answer =
            observatoryDockDecorations() ? observatoryPixelMetric(metric, widget) : std::nullopt)
    {
        return *answer;
    }
    switch (metric)
    {
    case PM_DockWidgetTitleBarButtonMargin:
        return m_tokens->spacing.xs;

    case PM_DockWidgetTitleMargin:
        return m_tokens->spacing.sm;

    case PM_SplitterWidth:
        return 3; // thin separator – accent glow on hover makes it easy to grab

    case PM_TabBarTabHSpace:
        return m_tokens->spacing.lg;

    case PM_TabBarTabVSpace:
        return m_tokens->spacing.sm;

    case PM_TabCloseIndicatorWidth:
    case PM_TabCloseIndicatorHeight:
        return 14;

    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

QSize KddwProxyStyle::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize,
                                       const QWidget* widget) const
{
    switch (type)
    {
    case CT_TabBarTab: {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);

        double heightScale = 1.0;
        if (widget)
        {
            bool ok = false;
            const double propertyScale = widget->property("anTabHeightScale").toDouble(&ok);
            if (ok && propertyScale > 0.0)
            {
                heightScale = propertyScale;
            }
        }

        if (heightScale != 1.0)
        {
            size.setHeight(qMax(16, qRound(size.height() * heightScale)));
        }

        const int minWidth = widget ? widget->property("anTabMinWidth").toInt() : 0;
        const int minHeight = widget ? widget->property("anTabMinHeight").toInt() : 0;
        if (minWidth > 0)
        {
            size.setWidth(qMax(size.width(), minWidth));
        }
        if (minHeight > 0)
        {
            size.setHeight(qMax(size.height(), minHeight));
        }

        return size;
    }

    default:
        return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    }
}

QRect KddwProxyStyle::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
{
    if (const std::optional<QRect> answer =
            observatoryDockDecorations() ? observatorySubElementRect(element, option, widget) : std::nullopt)
    {
        return *answer;
    }
    return QProxyStyle::subElementRect(element, option, widget);
}

int KddwProxyStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                              QStyleHintReturn* returnData) const
{
    if (const std::optional<int> answer =
            observatoryDockDecorations() ? observatoryStyleHint(hint, widget) : std::nullopt)
    {
        return *answer;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

// ════════════════════════════════════════════════════════════════════════════
// The Observatory profile: a dock tab strip
// ════════════════════════════════════════════════════════════════════════════

namespace
{

/// The tab strip the style paints, when it tells what its tabs show.
const ObservatoryDockTabSource* tabSourceOf(const QWidget* widget)
{
    return dynamic_cast<const ObservatoryDockTabSource*>(widget);
}

/// The index of the tab an option is for: the one it names, or the strip's tab with its rectangle.
int tabIndexOf(const QStyleOptionTab& tab, const QWidget* widget)
{
    if (tab.tabIndex >= 0)
    {
        return tab.tabIndex;
    }
    if (const auto* strip = qobject_cast<const QTabBar*>(widget))
    {
        for (int index = 0; index < strip->count(); ++index)
        {
            if (strip->tabRect(index) == tab.rect)
            {
                return index;
            }
        }
    }
    return -1;
}

} // namespace

bool KddwProxyStyle::drawObservatoryPrimitive(PrimitiveElement element, const QStyleOption* /*option*/,
                                              const QWidget* widget) const
{
    if (tabSourceOf(widget) == nullptr)
    {
        return false;
    }
    switch (element)
    {
    case PE_FrameTabBarBase:
    case PE_IndicatorTabTear:
    case PE_IndicatorTabTearRight:
        // The strip's ground and hairline are its stack's; a cut tab shows no tear.
        return true;
    default:
        return false;
    }
}

bool KddwProxyStyle::drawObservatoryControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                                            const QWidget* widget) const
{
    const ObservatoryDockTabSource* source = tabSourceOf(widget);
    const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (source == nullptr || tab == nullptr)
    {
        return false;
    }
    switch (element)
    {
    case CE_TabBarTab: {
        const int index = tabIndexOf(*tab, widget);
        if (index < 0)
        {
            return true;
        }
        ObservatoryDockTabState state;
        state.enabled = (tab->state & State_Enabled) != 0;
        state.current = (tab->state & State_Selected) != 0;
        state.hovered = state.enabled && (tab->state & State_MouseOver) != 0;
        state.activeGroup = !widget->property("forceUnselected").toBool();
        paintObservatoryDockTab(*painter, tab->rect, source->observatoryDockTab(index), state);
        return true;
    }
    case CE_TabBarTabShape:
    case CE_TabBarTabLabel:
        // Drawn whole as the tab.
        return true;
    default:
        return false;
    }
}

std::optional<int> KddwProxyStyle::observatoryPixelMetric(PixelMetric metric, const QWidget* widget) const
{
    if (tabSourceOf(widget) == nullptr)
    {
        return std::nullopt;
    }
    switch (metric)
    {
    case PM_TabBarTabOverlap:
    case PM_TabBarTabShiftHorizontal:
    case PM_TabBarTabShiftVertical:
    case PM_TabBarBaseOverlap:
    case PM_TabBarBaseHeight:
        // Tabs side by side, none raised over another, no base under them.
        return 0;
    default:
        return std::nullopt;
    }
}

std::optional<QRect> KddwProxyStyle::observatorySubElementRect(SubElement element, const QStyleOption* option,
                                                               const QWidget* widget) const
{
    const ObservatoryDockTabSource* source = tabSourceOf(widget);
    const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (source == nullptr || tab == nullptr)
    {
        return std::nullopt;
    }
    const int index = tabIndexOf(*tab, widget);
    if (index < 0)
    {
        return std::nullopt;
    }
    const ObservatoryDockTabParts parts = observatoryDockTabParts(tab->rect, source->observatoryDockTab(index));
    switch (element)
    {
    case SE_TabBarTabRightButton:
        return parts.close;
    case SE_TabBarTabLeftButton:
        return QRect();
    case SE_TabBarTabText:
        return parts.name;
    default:
        return std::nullopt;
    }
}

std::optional<int> KddwProxyStyle::observatoryStyleHint(StyleHint hint, const QWidget* widget) const
{
    if (tabSourceOf(widget) == nullptr)
    {
        return std::nullopt;
    }
    switch (hint)
    {
    case SH_Widget_Animation_Duration:
        // The dock library sets a style of its own on its strips for this: a tab removed while an animation runs would
        // be painted after it is gone.
        return 0;
    case SH_TabBar_CloseButtonPosition:
        return static_cast<int>(QTabBar::RightSide);
    case SH_TabBar_Alignment:
        return static_cast<int>(Qt::AlignLeft);
    case SH_TabBar_ElideMode:
        return static_cast<int>(Qt::ElideMiddle);
    default:
        return std::nullopt;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Private drawing helpers
// ════════════════════════════════════════════════════════════════════════════

// ── Title Bar ──────────────────────────────────────────────────────────────

void KddwProxyStyle::drawDockTitleBar(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const auto* dwOpt = qstyleoption_cast<const QStyleOptionDockWidget*>(option);
    if (!dwOpt)
    {
        QProxyStyle::drawControl(CE_DockWidgetTitle, option, painter, widget);
        return;
    }

    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto& layers = m_tokens->layers;

    // ── Background: subtle gradient from elevated to base surface ───────
    {
        QLinearGradient gradient(option->rect.topLeft(), option->rect.bottomLeft());
        gradient.setColorAt(0.0, layers.surface2);
        gradient.setColorAt(1.0, layers.surface1);
        painter->setPen(Qt::NoPen);
        painter->setBrush(gradient);
        painter->drawRect(option->rect);
    }

    // ── Focus underline at bottom edge ─────────────────────────────────
    {
        QColor focus = c.borderFocus;
        focus.setAlpha(68);
        painter->setPen(QPen(focus, 1));
        const int y = option->rect.bottom();
        painter->drawLine(option->rect.left(), y, option->rect.right(), y);
    }

    // ── Bottom border ──────────────────────────────────────────────────
    painter->setPen(QPen(c.borderSubtle, 1));
    painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());

    // ── Title text ─────────────────────────────────────────────────────
    painter->setPen(c.textPrimary);
    auto titleFont = painter->font();
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    const QRect textRect = option->rect.adjusted(m_tokens->spacing.sm, 0, -m_tokens->spacing.sm, 0);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, dwOpt->title);
}

// ── Separator ──────────────────────────────────────────────────────────────

void KddwProxyStyle::drawDockSeparator(const QStyleOption* option, QPainter* painter, const QWidget* /*widget*/) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const bool hovered = isHovered(option);
    const bool horizontal = option->state & QStyle::State_Horizontal;

    // Hover glow
    if (hovered)
    {
        QColor glow = c.borderFocus;
        glow.setAlpha(22);
        painter->setPen(Qt::NoPen);
        painter->setBrush(glow);
        painter->drawRect(option->rect);
    }

    // Central groove line
    const QColor grooveColor = hovered ? c.borderFocus : c.borderSubtle;
    painter->setPen(Qt::NoPen);
    painter->setBrush(grooveColor);

    constexpr int grooveThickness = 2;
    if (horizontal)
    {
        const int y = option->rect.center().y() - (grooveThickness / 2);
        painter->drawRoundedRect(QRectF(option->rect.left() + 6, y, option->rect.width() - 12, grooveThickness), 1.0,
                                 1.0);
    }
    else
    {
        const int x = option->rect.center().x() - (grooveThickness / 2);
        painter->drawRoundedRect(QRectF(x, option->rect.top() + 6, grooveThickness, option->rect.height() - 12), 1.0,
                                 1.0);
    }
}

// ── Tab Shape ──────────────────────────────────────────────────────────────

void KddwProxyStyle::drawDockTabShape(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const auto* tabOpt = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (!tabOpt)
    {
        QProxyStyle::drawControl(CE_TabBarTabShape, option, painter, widget);
        return;
    }

    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto r = m_tokens->radius.sm;

    const bool forceUnselected = widget && widget->property("forceUnselected").toBool();
    const bool selected = !forceUnselected && (tabOpt->state & QStyle::State_Selected);
    const bool hovered = isHovered(option) && !selected;

    QColor bgColor;
    QColor borderColor = c.borderSubtle;

    if (selected)
    {
        bgColor = c.bgCard;
        borderColor = c.borderPrimary;
    }
    else if (hovered)
    {
        bgColor = c.bgHover;
    }
    else
    {
        bgColor = c.bgCardSecondary;
    }

    painter->setPen(QPen(borderColor, 1));
    painter->setBrush(bgColor);

    // Top-rounded rect (tab shape)
    QRectF tabRect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.moveTo(tabRect.bottomLeft());
    path.lineTo(tabRect.left(), tabRect.top() + r);
    path.arcTo(tabRect.left(), tabRect.top(), 2 * r, 2 * r, 180, -90);
    path.lineTo(tabRect.right() - r, tabRect.top());
    path.arcTo(tabRect.right() - 2 * r, tabRect.top(), 2 * r, 2 * r, 90, -90);
    path.lineTo(tabRect.bottomRight());

    if (selected)
    {
        // Close the bottom to erase the base line for the selected tab
        path.lineTo(tabRect.bottomLeft());
    }

    painter->drawPath(path);

    // Selected-tab accent marker
    if (selected)
    {
        const QString accentLinePosition = widget ? widget->property("anTabAccentLinePosition").toString() : QString();
        const bool drawTopAccentLine = accentLinePosition == QLatin1String("top");

        painter->setPen(Qt::NoPen);
        painter->setBrush(c.accentPrimary);

        if (drawTopAccentLine)
        {
            constexpr qreal barHeight = 3.0;
            painter->drawRect(QRectF(tabRect.left(), tabRect.top(), tabRect.width(), barHeight));
        }
        else
        {
            const qreal dotRadius = 2.0;
            const QPointF center(tabRect.center().x(), tabRect.top() + m_tokens->spacing.xs + dotRadius);
            painter->drawEllipse(center, dotRadius, dotRadius);
        }
    }
}

// ── Tab Label ──────────────────────────────────────────────────────────────

void KddwProxyStyle::drawDockTabLabel(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const auto* tabOpt = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (!tabOpt)
    {
        QProxyStyle::drawControl(CE_TabBarTabLabel, option, painter, widget);
        return;
    }

    const PainterGuard guard(painter);
    const auto& c = m_tokens->colors;

    const bool forceUnselected = widget && widget->property("forceUnselected").toBool();
    const bool selected = !forceUnselected && (tabOpt->state & QStyle::State_Selected);

    painter->setPen(selected ? c.textAccent : c.textSecondary);

    if (selected)
    {
        auto font = painter->font();
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
    }

    QProxyStyle::drawControl(CE_TabBarTabLabel, option, painter, widget);
}

// ── Tool Button (title bar buttons) ────────────────────────────────────────

void KddwProxyStyle::drawDockToolButton(const QStyleOptionComplex* option, QPainter* painter,
                                        const QWidget* widget) const
{
    // Background panel is handled by PE_PanelButtonTool → drawDockButtonPanel.
    // Render the icon label through the base proxy.
    QProxyStyle::drawComplexControl(CC_ToolButton, option, painter, widget);
}

// ── Button panel (PE_PanelButtonTool for KDDW buttons) ─────────────────────

void KddwProxyStyle::drawDockButtonPanel(const QStyleOption* option, QPainter* painter, const QWidget* /*widget*/) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto r = m_tokens->radius.sm;

    QColor bgColor = Qt::transparent;
    QColor borderColor = Qt::transparent;

    if (isPressed(option))
    {
        bgColor = c.bgCardSecondary;
        borderColor = c.borderPrimary;
    }
    else if (isChecked(option))
    {
        bgColor = c.bgCardSecondary;
        borderColor = c.borderPrimary;
    }
    else if (isHovered(option))
    {
        bgColor = c.bgHover;
        borderColor = c.borderSubtle;
    }

    if (bgColor != Qt::transparent)
    {
        painter->setPen(QPen(borderColor, 1));
        painter->setBrush(bgColor);
        painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
    }
}

// ── Group frame ────────────────────────────────────────────────────────────

void KddwProxyStyle::drawDockGroupFrame(const QStyleOption* option, QPainter* painter, const QWidget* /*widget*/) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;

    const qreal penWidth = 1.0;
    const qreal halfPen = penWidth / 2.0;
    const QRectF rectf = QRectF(option->rect).adjusted(halfPen, halfPen, -halfPen, -halfPen);

    painter->setPen(QPen(c.borderSubtle, penWidth));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(rectf, m_tokens->radius.sm, m_tokens->radius.sm);
}

// ════════════════════════════════════════════════════════════════════════════
// Install / query
// ════════════════════════════════════════════════════════════════════════════

KddwProxyStyle* installKddwProxyStyle(QWidget* dockMainWindow)
{
    if (!dockMainWindow)
        return nullptr;

    auto* style = new KddwProxyStyle();
    style->setParent(dockMainWindow); // ensure lifetime management
    dockMainWindow->setStyle(style);
    g_installedKddwStyle = style;
    return style;
}

KddwProxyStyle* installedKddwProxyStyle()
{
    return g_installedKddwStyle;
}

} // namespace an::ui
