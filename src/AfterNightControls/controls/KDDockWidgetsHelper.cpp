// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "KDDockWidgetsHelper.h"
#include "common.h"
#include "controls/DockDropIndicators.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"
#include "theme/KddwProxyStyle.h"
#include "theme/ObservatoryDock.h"

#include <QMouseEvent>
#include <QHelpEvent>
#include <QApplication>
#include <QAbstractButton>
#include <QBoxLayout>
#include <QMenu>
#include <QToolButton>
#include <QPalette>
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtwidgets/DockWidget.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/MainWindow.h>
#include <QPainter>
#include <QHash>
#include <QStyle>
#include <QToolTip>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include <verdigris/wobjectimpl.h>

namespace
{

/// The dock library's own separator: what it lays groups apart by unless told otherwise, and the Classic profile's.
constexpr int kLibraryDockSeparator = 5;

/// What follows a view's name in its title while its image has unsaved changes.
constexpr QStringView kUnsavedMark = u" *";

/// Whether a widget stands in a floating window of the dock library: its strip is then the smaller one, and its float
/// button docks.
bool inFloatingWindowOf(const QWidget* widget)
{
    return widget != nullptr && dynamic_cast<const AN_FloatingWindow*>(widget->QWidget::window()) != nullptr;
}

/// The dock widget of the tab at an index of a strip, or null.
KDDockWidgets::QtWidgets::DockWidget* dockWidgetViewAt(KDDockWidgets::Core::TabBar* tabBar, const int index)
{
    if (tabBar == nullptr || index < 0)
    {
        return nullptr;
    }
    auto* dockWidget = tabBar->dockWidgetAt(index);
    return dockWidget != nullptr ? dynamic_cast<KDDockWidgets::QtWidgets::DockWidget*>(dockWidget->view()) : nullptr;
}

QIcon iconFromContext(const QString& iconName);

/// What a button of the strip or of a title bar shows under the Observatory profile.
an::ui::ObservatoryDockGlyph observatoryGlyph(const KDDockWidgets::TitleBarButtonType type, const bool tabList,
                                              const QWidget* button)
{
    using an::ui::ObservatoryDockGlyph;
    using KDDockWidgets::TitleBarButtonType;
    if (tabList)
    {
        return ObservatoryDockGlyph::TabList;
    }
    switch (type)
    {
    case TitleBarButtonType::Close:
        return ObservatoryDockGlyph::Close;
    case TitleBarButtonType::Float:
    case TitleBarButtonType::Normal:
        return inFloatingWindowOf(button) ? ObservatoryDockGlyph::Dock : ObservatoryDockGlyph::Float;
    case TitleBarButtonType::Maximize:
        return ObservatoryDockGlyph::Maximize;
    case TitleBarButtonType::Minimize:
        return ObservatoryDockGlyph::Minimize;
    default:
        return ObservatoryDockGlyph::Pin;
    }
}

/// The adornment a dock widget keeps, as a child the widget deletes with itself.
class TabAdornmentHolder final : public QObject
{
public:
    explicit TabAdornmentHolder(QObject* parent) : QObject(parent)
    {
        setObjectName(QStringLiteral("anTabAdornment"));
    }

    an::ui::TabAdornment adornment;
};

TabAdornmentHolder* adornmentHolderOf(const KDDockWidgets::QtWidgets::DockWidget* dockWidget)
{
    if (dockWidget == nullptr)
    {
        return nullptr;
    }
    return dynamic_cast<TabAdornmentHolder*>(
        dockWidget->findChild<QObject*>(QStringLiteral("anTabAdornment"), Qt::FindDirectChildrenOnly));
}

/// The strip a dock widget's tab is in: lays it out again and applies its locks, after an adornment changed.
void refreshStripOf(KDDockWidgets::QtWidgets::DockWidget* dockWidget)
{
    for (QWidget* current = dockWidget != nullptr ? dockWidget->parentWidget() : nullptr; current != nullptr;
         current = current->parentWidget())
    {
        if (auto* stack = dynamic_cast<AN_Stack*>(current))
        {
            if (auto* tabBar = dynamic_cast<AN_TabBar*>(stack->tabBar()))
            {
                tabBar->refreshTabs();
            }
            stack->applyTabLocks();
            return;
        }
    }
}

} // namespace

namespace an::ui
{

void setTabAdornment(KDDockWidgets::QtWidgets::DockWidget* dockWidget, TabAdornment adornment)
{
    if (dockWidget == nullptr)
    {
        return;
    }
    TabAdornmentHolder* holder = adornmentHolderOf(dockWidget);
    if (holder == nullptr)
    {
        holder = new TabAdornmentHolder(dockWidget);
    }
    holder->adornment = std::move(adornment);
    refreshStripOf(dockWidget);
}

void clearTabAdornment(KDDockWidgets::QtWidgets::DockWidget* dockWidget)
{
    TabAdornmentHolder* holder = adornmentHolderOf(dockWidget);
    if (holder == nullptr)
    {
        return;
    }
    delete holder;
    refreshStripOf(dockWidget);
}

const TabAdornment* tabAdornment(const KDDockWidgets::QtWidgets::DockWidget* dockWidget)
{
    const TabAdornmentHolder* holder = adornmentHolderOf(dockWidget);
    return holder != nullptr ? &holder->adornment : nullptr;
}

void applyDockDecorationsProfile()
{
    const int thickness = observatoryDockDecorations() ? kObservatoryDockSeparator : kLibraryDockSeparator;
    KDDockWidgets::Config& config = KDDockWidgets::Config::self();
    if (config.separatorThickness() == thickness)
    {
        return;
    }
    if (!KDDockWidgets::DockRegistry::self()->isEmpty(/*excludeBeingDeleted=*/true))
    {
        LOG_WARNING("The dock library kept its separator: dock widgets or main windows already exist");
        return;
    }
    config.setSeparatorThickness(thickness);
}

} // namespace an::ui

// ════════════════════════════════════════════════════════════════════════════
// AN_DockButton — Visual-Studio-style flat button
// ════════════════════════════════════════════════════════════════════════════

AN_DockButton::AN_DockButton(KDDockWidgets::TitleBarButtonType type, QWidget* parent)
    : QAbstractButton(parent), m_type(type)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

AN_DockButton* AN_DockButton::makeTabList(QWidget* parent)
{
    auto* button = new AN_DockButton(KDDockWidgets::TitleBarButtonType::Normal, parent);
    button->m_tabList = true;
    button->setObjectName(QStringLiteral("anDockTabList"));
    return button;
}

QSize AN_DockButton::sizeHint() const
{
    if (an::ui::observatoryDockDecorations())
    {
        return {an::ui::observatoryDockButtonSide(), an::ui::observatoryDockButtonSide()};
    }
    // VS-style: compact square. Scale with logical DPI.
    const qreal scale = logicalDpiX() / 96.0;
    const int side = qRound(16 * scale);
    return QSize(side, side);
}

QSize AN_DockButton::minimumSizeHint() const
{
    return sizeHint();
}

void AN_DockButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void AN_DockButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

bool AN_DockButton::event(QEvent* ev)
{
    // Guard against use-after-free: clicking close/float can destroy the parent
    // widget tree (including this button) while the click handler is still on the
    // call stack.  Mirror the same QPointer guard that KDDW's own Button uses.
    switch (ev->type())
    {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        QPointer<QObject> guard(this);
        const bool result = QAbstractButton::event(ev);
        // If we were deleted during the handler, just return.
        if (!guard)
            return result;
        return result;
    }
    default:
        break;
    }
    return QAbstractButton::event(ev);
}

void AN_DockButton::paintEvent(QPaintEvent* /*event*/)
{
    if (an::ui::observatoryDockDecorations())
    {
        QPainter painter(this);
        an::ui::paintObservatoryDockButton(painter, rect(), observatoryGlyph(m_type, m_tabList, this), m_hovered,
                                           isDown(), isEnabled());
        return;
    }
    using KDDockWidgets::TitleBarButtonType;
    const auto& c = an::ui::tokens().colors;
    const bool pressed = isDown();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect r = rect();

    // ── Background ─────────────────────────────────────────────────
    if (pressed)
    {
        p.fillRect(r, c.bgCardSecondary);
    }
    else if (m_hovered)
    {
        p.fillRect(r, c.bgHover);
    }

    // ── Glyph ──────────────────────────────────────────────────────
    QColor glyphColor;
    if (m_hovered || pressed)
        glyphColor = c.textPrimary;
    else
        glyphColor = c.textSecondary;

    QPen pen(glyphColor, 1.4, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Draw a larger centered glyph so tab-corner controls are easier to hit/read.
    const qreal glyphSide = qMin<qreal>(r.width(), r.height()) * 0.58;
    const QRectF glyph(r.center().x() - (glyphSide / 2.0), r.center().y() - (glyphSide / 2.0), glyphSide, glyphSide);

    switch (m_type)
    {
    case TitleBarButtonType::Close: {
        // × glyph: two diagonal lines
        p.drawLine(glyph.topLeft(), glyph.bottomRight());
        p.drawLine(glyph.topRight(), glyph.bottomLeft());
        break;
    }
    case TitleBarButtonType::Float:
    case TitleBarButtonType::Normal: {
        // Detach / float glyph: two overlapping small rectangles (VS-style)
        const qreal w = glyph.width();
        const qreal h = glyph.height();
        const qreal off = w * 0.22; // offset for the back rectangle
        // Back rectangle (upper-right, slightly offset)
        QRectF back(glyph.left() + off, glyph.top(), w - off, h - off);
        // Front rectangle (lower-left)
        QRectF front(glyph.left(), glyph.top() + off, w - off, h - off);
        p.drawRect(back);
        p.drawRect(front);
        break;
    }
    case TitleBarButtonType::Maximize: {
        // Single rectangle (maximize)
        p.drawRect(glyph);
        break;
    }
    case TitleBarButtonType::Minimize: {
        // Horizontal line at vertical center
        const qreal cy = glyph.center().y();
        p.drawLine(QPointF(glyph.left(), cy), QPointF(glyph.right(), cy));
        break;
    }
    case TitleBarButtonType::AutoHide: {
        // Pin glyph — draw a simple pushpin: vertical line with a circle
        const qreal cx = glyph.center().x();
        p.drawLine(QPointF(cx, glyph.top()), QPointF(cx, glyph.bottom()));
        p.drawEllipse(QPointF(cx, glyph.top() + glyph.height() * 0.35), glyph.width() * 0.3, glyph.width() * 0.3);
        break;
    }
    default:
        // Fallback: draw ×
        p.drawLine(glyph.topLeft(), glyph.bottomRight());
        p.drawLine(glyph.topRight(), glyph.bottomLeft());
        break;
    }
}

namespace
{
ImageViewControl* getImageViewControl(KDDockWidgets::Core::DockWidget* dockWidgetController)
{
    if (!dockWidgetController)
    {
        return nullptr;
    }

    auto* dockWidgetView = dockWidgetController->view();
    if (!dockWidgetView)
    {
        return nullptr;
    }

    auto* dockWidget = dynamic_cast<KDDockWidgets::QtWidgets::DockWidget*>(dockWidgetView);
    if (!dockWidget)
    {
        return nullptr;
    }

    return dynamic_cast<ImageViewControl*>(dockWidget->widget());
}

[[nodiscard]] bool layoutChangesLocked()
{
    return KDDockWidgets::Config::self().dropIndicatorsInhibited();
}

void setActiveDockWidgetView(KDDockWidgets::Core::DockWidget* dockWidgetController)
{
    auto* imageViewControl = getImageViewControl(dockWidgetController);
    if (!imageViewControl)
    {
        return;
    }

    if (auto* context = an::ui::appContext())
    {
        if (auto* viewRegistry = context->getViewRegistry())
        {
            viewRegistry->setActiveImageViewControl(imageViewControl);
        }
    }
}

QString buildImageSummaryTooltip(ImageViewControl* imageViewControl)
{
    if (!imageViewControl || !imageViewControl->hasImage())
    {
        return {};
    }

    const an::ui::ImageDocument& document = imageViewControl->document();
    if (const std::optional<an::ui::DocumentSequence> sequence = document.sequence(); sequence.has_value())
    {
        // The summary the view's own window shows, from what the document recorded when the sequence was opened.
        return QString::fromUtf8(getSequenceFullReport(
            sequence->descriptor, reportFileFacts(sequence->sourcePath.toUtf8().toStdString()),
            imageViewControl->getUniqueName(), sequence->recorded, sequence->firstFrame, sequence->namedFilesBytes));
    }

    // A tooltip never waits for a lease: an image a writer holds shows its summary the next time the tooltip is asked.
    an::Result<an::consumer::AssetReadLease> lease = document.tryRead();
    if (!lease.hasValue())
    {
        return {};
    }
    const ReportFileFacts file = reportFileFacts(document.names().sourcePath.toStdString());
    return lease->visit([&](const auto& typed) -> QString {
        const std::optional<an::ImageDescriptor> described = typed.descriptor();
        if (!described.has_value())
        {
            return {};
        }
        return QString::fromUtf8(
            getImageFullReport(*described, typed.metadata(), file, imageViewControl->getUniqueName()));
    });
}

QString buildImageSummaryTooltip(KDDockWidgets::Core::DockWidget* dockWidgetController)
{
    return buildImageSummaryTooltip(getImageViewControl(dockWidgetController));
}
} // namespace
// ---------------- AN_TabBar ----------------

W_OBJECT_IMPL(AN_TabBar)

AN_TabBar::AN_TabBar(KDDockWidgets::Core::TabBar* controller, KDDockWidgets::Core::View* parent)
    : KDDockWidgets::QtWidgets::TabBar(controller, const_cast<QWidget*>(static_cast<const QWidget*>(parent->handle())))
{
    setObjectName("DockTabBar");
    setDocumentMode(true);
    setTabsClosable(true);
    setMovable(true);
    setElideMode(Qt::ElideRight);
    setExpanding(false);
    setUsesScrollButtons(false);
    setIconSize(QSize(12, 12));
    setDrawBase(false);
    applyThemeColors();
    if (an::ui::observatoryDockDecorations())
    {
        // The library gives its strips a style of its own; the dock style draws them under this profile and keeps the
        // library's reason for its own (no animation of the tabs).
        auto* style = new an::ui::KddwProxyStyle();
        style->setParent(this);
        setStyle(style);
    }
}

an::ui::ObservatoryDockTab AN_TabBar::observatoryDockTab(int index) const
{
    an::ui::ObservatoryDockTab tab;
    QString title = tabText(index);
    if (title.endsWith(kUnsavedMark))
    {
        tab.modified = true;
        title.chop(kUnsavedMark.size());
    }
    tab.name = title;
    tab.closable = tabsClosable();

    KDDockWidgets::Core::DockWidget* dockWidget = tabBar() != nullptr ? tabBar()->dockWidgetAt(index) : nullptr;
    tab.icon = tabIcon(index);
    if (tab.icon.isNull())
    {
        if (ImageViewControl* view = getImageViewControl(dockWidget))
        {
            tab.icon = iconFromContext(view->document().sequence().has_value() ? QStringLiteral("film")
                                                                               : QStringLiteral("picture"));
        }
    }
    if (const an::ui::TabAdornment* adornment = an::ui::tabAdornment(dockWidgetViewAt(tabBar(), index)))
    {
        tab.adorned = true;
        tab.adornmentIcon = iconFromContext(adornment->iconKey);
    }
    return tab;
}

QRect AN_TabBar::adornmentRect(int index) const
{
    if (!an::ui::observatoryDockDecorations() || index < 0 || index >= count())
    {
        return {};
    }
    const an::ui::ObservatoryDockTab tab = observatoryDockTab(index);
    return tab.adorned ? an::ui::observatoryDockTabParts(tabRect(index), tab).adornment : QRect();
}

QString AN_TabBar::toolTipAt(QPoint position) const
{
    const int index = an::ui::observatoryDockDecorations() ? tabAt(position) : -1;
    if (index < 0)
    {
        return {};
    }
    if (adornmentRect(index).contains(position))
    {
        if (const an::ui::TabAdornment* adornment = an::ui::tabAdornment(dockWidgetViewAt(tabBar(), index)))
        {
            return adornment->toolTip;
        }
    }
    QString text = observatoryDockTab(index).name;
    if (const QString summary = buildImageSummaryTooltip(tabBar()->dockWidgetAt(index)); !summary.isEmpty())
    {
        text += QStringLiteral("\n\n") + summary;
    }
    return text;
}

void AN_TabBar::refreshTabs()
{
    // Setting the elision again is what makes the strip measure its tabs anew.
    setElideMode(elideMode());
    updateGeometry();
    update();
}

bool AN_TabBar::inFloatingWindow() const
{
    return inFloatingWindowOf(this);
}

QSize AN_TabBar::minimumTabSizeHint(int index) const
{
    if (an::ui::observatoryDockDecorations())
    {
        return {an::ui::observatoryDockTabWidth(observatoryDockTab(index), true),
                an::ui::observatoryDockStripHeight(inFloatingWindow())};
    }
    return KDDockWidgets::QtWidgets::TabBar::minimumTabSizeHint(index);
}

QSize AN_TabBar::tabSizeHint(int index) const
{
    if (an::ui::observatoryDockDecorations())
    {
        return {an::ui::observatoryDockTabWidth(observatoryDockTab(index), false),
                an::ui::observatoryDockStripHeight(inFloatingWindow())};
    }
    QSize hint = KDDockWidgets::QtWidgets::TabBar::tabSizeHint(index);

    const auto& spacing = an::ui::tokens().spacing;
    const int textWidth = fontMetrics().horizontalAdvance(tabText(index));
    const int iconWidth = tabIcon(index).isNull() ? 0 : iconSize().width() + spacing.xs;
    const int closeIndicatorWidth =
        tabsClosable() && style() ? style()->pixelMetric(QStyle::PM_TabCloseIndicatorWidth, nullptr, this) : 0;
    const int closeWidth = closeIndicatorWidth > 0 ? closeIndicatorWidth + spacing.sm : 0;
    const int minimumReadableWidth = textWidth + iconWidth + closeWidth + (spacing.sm * 2) + spacing.xs;
    hint.setWidth(qMax(hint.width(), minimumReadableWidth));

    constexpr qreal kTabHeightScale = 0.85;
    hint.setHeight(qMax(18, qRound(hint.height() * kTabHeightScale)));
    return hint;
}

void AN_TabBar::applyThemeColors()
{
    if (an::ui::observatoryDockDecorations())
    {
        refreshTabs();
    }
    // All tab shape/color drawing is handled by KddwProxyStyle / AnProxyStyle.
    // We only need to set palette colors so the tab label text renders correctly,
    // and configure close button icons per theme.
    const auto& c = an::ui::tokens().colors;

    QPalette pal = palette();
    pal.setColor(QPalette::Window, c.bgPrimary);
    pal.setColor(QPalette::WindowText, c.textPrimary);
    pal.setColor(QPalette::BrightText, c.textAccent);
    setPalette(pal);
    update();
}

void AN_TabBar::setForceUnselected(bool enabled)
{
    if (isForceUnselected() == enabled)
        return;

    setProperty("forceUnselected", enabled);
    if (auto* s = style())
    {
        s->unpolish(this);
        s->polish(this);
    }
    update();
}

bool AN_TabBar::isForceUnselected() const
{
    return property("forceUnselected").toBool();
}

void AN_TabBar::paintEvent(QPaintEvent* event)
{
    if (an::ui::observatoryDockDecorations())
    {
        // The tabs, through the dock style; the strip's ground is the stack's.
        KDDockWidgets::QtWidgets::TabBar::paintEvent(event);
        return;
    }
    // Let the base QTabBar do all its normal painting through the style system.
    KDDockWidgets::QtWidgets::TabBar::paintEvent(event);

    // Overlay a bold accent line on the currently-selected tab when this tab bar
    // belongs to the active view-control group (i.e. forceUnselected is false).
    const int idx = currentIndex();
    if (idx < 0 || isForceUnselected())
        return;

    const QRect tr = tabRect(idx);
    if (tr.isEmpty())
        return;

    const auto& c = an::ui::tokens().colors;
    constexpr int barHeight = 3;

    QPainter p(this);
    p.setPen(Qt::NoPen);
    p.setBrush(c.accentPrimary);
    p.drawRect(tr.left(), tr.top(), tr.width(), barHeight);
}

void AN_TabBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (layoutChangesLocked())
    {
        event->accept();
    }
    else
    {
        KDDockWidgets::QtWidgets::TabBar::mouseDoubleClickEvent(event);
    }
}

void AN_TabBar::mousePressEvent(QMouseEvent* event)
{
    if (const int index = tabAt(event->pos());
        event->button() == Qt::LeftButton && index >= 0 && adornmentRect(index).contains(event->pos()))
    {
        // A press on an adornment is the adornment's: it neither selects nor drags the tab.
        m_pressedAdornment = index;
        event->accept();
        return;
    }
    // Core DockWidget from the controller (preferred, no extra includes required)
    KDDockWidgets::Core::DockWidget* dwCore = nullptr;
    if (auto* coreTabBar = this->tabBar()) // controller
    {
        const int idx = tabAt(event->pos());
        if (idx >= 0)
            dwCore = coreTabBar->dockWidgetAt(idx);
        else
            dwCore = coreTabBar->currentDockWidget();
    }

    if (dwCore)
    {
        setActiveDockWidgetView(dwCore);
    }

    KDDockWidgets::QtWidgets::TabBar::mousePressEvent(event);
}

void AN_TabBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_pressedAdornment >= 0)
    {
        const int index = m_pressedAdornment;
        m_pressedAdornment = -1;
        event->accept();
        if (!adornmentRect(index).contains(event->pos()))
        {
            return;
        }
        if (const an::ui::TabAdornment* adornment = an::ui::tabAdornment(dockWidgetViewAt(tabBar(), index));
            adornment != nullptr && adornment->activated)
        {
            // A copy: what it runs may take the tab, and the adornment with it, away.
            const std::function<void()> activated = adornment->activated;
            activated();
        }
        return;
    }
    KDDockWidgets::QtWidgets::TabBar::mouseReleaseEvent(event);
}

bool AN_TabBar::event(QEvent* event)
{
    if (an::ui::observatoryDockDecorations())
    {
        if (event->type() == QEvent::Show)
        {
            // Shown again after its group moved between a floating window and a dock area: its height may differ.
            refreshTabs();
        }
        else if (event->type() == QEvent::ToolTip)
        {
            auto* helpEvent = static_cast<QHelpEvent*>(event);
            if (const QString text = toolTipAt(helpEvent->pos()); !text.isEmpty())
            {
                QToolTip::showText(helpEvent->globalPos(), text, this);
                return true;
            }
            QToolTip::hideText();
            event->ignore();
            return true;
        }
    }
    if (event->type() == QEvent::ToolTip)
    {
        auto* helpEvent = static_cast<QHelpEvent*>(event);
        KDDockWidgets::Core::DockWidget* dwCore = nullptr;
        if (auto* coreTabBar = this->tabBar())
        {
            const int idx = tabAt(helpEvent->pos());
            if (idx < 0)
            {
                QToolTip::hideText();
                event->ignore();
                return KDDockWidgets::QtWidgets::TabBar::event(event);
            }
            dwCore = coreTabBar->dockWidgetAt(idx);
        }

        const auto tooltipText = buildImageSummaryTooltip(dwCore);
        if (!tooltipText.isEmpty())
        {
            QToolTip::showText(helpEvent->globalPos(), tooltipText, this);
            return true;
        }

        QToolTip::hideText();
        event->ignore();
        return KDDockWidgets::QtWidgets::TabBar::event(event);
    }

    return KDDockWidgets::QtWidgets::TabBar::event(event);
}

// ---------------- AN_TitleBar ----------------

namespace
{
KDDockWidgets::Core::DockWidget* getActiveDockWidget(KDDockWidgets::Core::TitleBar* controller)
{
    if (!controller)
    {
        return nullptr;
    }

    if (auto* tabBar = controller->tabBar())
    {
        if (auto* currentDock = tabBar->currentDockWidget())
        {
            return currentDock;
        }
    }

    const auto dockWidgets = controller->dockWidgets();
    if (!dockWidgets.empty())
    {
        return dockWidgets.front();
    }

    return nullptr;
}
} // namespace

W_OBJECT_IMPL(AN_TitleBar)
AN_TitleBar::AN_TitleBar(KDDockWidgets::Core::TitleBar* controller, KDDockWidgets::Core::View* parent)
    : KDDockWidgets::QtWidgets::TitleBar(controller, parent)
{
    // setObjectName("DockTitleBar");
    //  DO NOT touch buttons here - they are created in init()
}

QSize AN_TitleBar::sizeHint() const
{
    QSize hint = KDDockWidgets::QtWidgets::TitleBar::sizeHint();
    if (an::ui::observatoryDockDecorations())
    {
        // A title bar heads a floating window: it is the smaller strip.
        hint.setHeight(an::ui::observatoryDockStripHeight(true));
    }
    return hint;
}

void AN_TitleBar::paintEvent(QPaintEvent* event)
{
    if (an::ui::observatoryDockDecorations())
    {
        QPainter painter(this);
        const QRect title = rect().adjusted(iconRect().isEmpty() ? 0 : iconRect().right(), 0, -buttonAreaWidth(), 0);
        an::ui::paintObservatoryDockTitleBar(painter, rect(), titleBar()->title(), title);
        return;
    }
    KDDockWidgets::QtWidgets::TitleBar::paintEvent(event);
}

bool AN_TitleBar::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        const auto tooltipText = buildImageSummaryTooltip(getActiveDockWidget(titleBar()));
        if (!tooltipText.isEmpty())
        {
            auto* helpEvent = static_cast<QHelpEvent*>(event);
            QToolTip::showText(helpEvent->globalPos(), tooltipText, this);
            return true;
        }

        QToolTip::hideText();
        event->ignore();
        return KDDockWidgets::QtWidgets::TitleBar::event(event);
    }

    return KDDockWidgets::QtWidgets::TitleBar::event(event);
}

void AN_TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (layoutChangesLocked())
    {
        event->accept();
    }
    else
    {
        KDDockWidgets::QtWidgets::TitleBar::mouseDoubleClickEvent(event);
    }
}

// ---------------- AN_Stack ----------------

W_OBJECT_IMPL(AN_Stack)
AN_Stack::AN_Stack(KDDockWidgets::Core::Stack* controller, KDDockWidgets::Core::View* parent)
    : KDDockWidgets::QtWidgets::Stack(controller, const_cast<QWidget*>(static_cast<const QWidget*>(parent->handle())))
{
}

void AN_Stack::init()
{
    // Let base create tab bar, corner widget & buttons (if flags enabled)
    KDDockWidgets::QtWidgets::Stack::init();
    applyThemeColors();
    styleCornerButtons();

    // Centre the corner-widget buttons vertically within the tab bar.
    if (auto* cw = cornerWidget(Qt::TopRightCorner))
    {
        cw->setContentsMargins(0, 0, 0, 0);
        if (auto* lay = cw->layout())
        {
            lay->setContentsMargins(0, 0, 2, 0);
            lay->setSpacing(0);
            lay->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            for (int i = 0; i < lay->count(); ++i)
            {
                if (auto* item = lay->itemAt(i))
                {
                    if (auto* childWidget = item->widget())
                        lay->setAlignment(childWidget, Qt::AlignVCenter);
                }
            }
        }
    }

    // --- Per-tab close button ---
    // The base Stack::init() connects tabCloseRequested → dw->view()->close()
    // synchronously, which can destroy the entire group/stack while QTabBar's
    // internal click handler is still on the call stack, causing a crash.
    // Disconnect that and reconnect with a deferred close via QTimer::singleShot.
    disconnect(this, &QTabWidget::tabCloseRequested, nullptr, nullptr);
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* coreStack = stack();
        if (!coreStack)
            return;
        auto* dw = coreStack->tabBar()->dockWidgetAt(index);
        if (!dw)
            return;
        if (dw->options() & KDDockWidgets::DockWidgetOption_NotClosable)
            return;

        // Prevent use-after-free: the QTabBar close-button click handler is
        // still on the call stack. Deferring the close lets it unwind safely.
        auto* dwView = dynamic_cast<KDDockWidgets::QtWidgets::DockWidget*>(dw->view());
        if (!dwView)
            return;
        QPointer<KDDockWidgets::QtWidgets::DockWidget> guard(dwView);
        QTimer::singleShot(0, this, [guard]() {
            if (guard)
                guard->close();
        });
    });

    // --- Corner close button ("Close Tab Group") ---
    // KDDW's default handler calls m_group->view()->close() which merely hides
    // the Group QWidget without properly closing individual dock widgets through
    // their closeRequested() chain.  Override to iterate every dock widget in
    // the group and close each one, honouring pending-change prompts.
    if (auto* closeBtn = button(KDDockWidgets::TitleBarButtonType::Close))
    {
        closeBtn->disconnect(SIGNAL(clicked()));
        connect(closeBtn, &QAbstractButton::clicked, this, [this]() {
            auto* coreStack = stack();
            if (!coreStack)
                return;
            auto* group = coreStack->group();
            if (!group)
                return;

            // Snapshot guarded views before closing; close() can destroy the
            // underlying KDDW controller, so raw DockWidget* must not be
            // inspected afterwards.
            const auto docks = group->dockWidgets();
            QVector<QPointer<KDDockWidgets::QtWidgets::DockWidget>> dockViews;
            dockViews.reserve(static_cast<qsizetype>(docks.size()));
            for (auto* dw : docks)
            {
                if (dw->options() & KDDockWidgets::DockWidgetOption_NotClosable)
                    continue;

                auto* dockView = dynamic_cast<KDDockWidgets::QtWidgets::DockWidget*>(dw->view());
                if (dockView)
                    dockViews.push_back(dockView);
            }

            for (const auto& dockView : dockViews)
            {
                if (!dockView)
                    continue;

                const bool wasOpen = dockView->isOpen();
                dockView->close(); // triggers CustomDockWidget::closeEvent → closeRequested chain

                // If the dock widget is still open the user cancelled the
                // pending-changes dialog — stop closing further tabs.
                if (wasOpen && dockView && dockView->isOpen())
                    break;
            }
        });
    }

    if (an::ui::observatoryDockDecorations())
    {
        // The list of the strip's tabs, before its other buttons.
        if (auto* corner = cornerWidget(Qt::TopRightCorner))
        {
            m_tabList = AN_DockButton::makeTabList(corner);
            if (auto* layout = qobject_cast<QBoxLayout*>(corner->layout()))
            {
                layout->insertWidget(0, m_tabList, 0, Qt::AlignVCenter);
            }
            connect(m_tabList, &QAbstractButton::clicked, this, [this]() { showTabList(); });
        }
        // After the library's own answers to a change of tabs, which enable the strip's buttons.
        if (auto* strip = qobject_cast<KDDockWidgets::QtWidgets::TabBar*>(tabBar()))
        {
            connect(strip, &KDDockWidgets::QtWidgets::TabBar::countChanged, this,
                    [this]() { applyObservatoryStrip(); });
        }
        connect(this, &QTabWidget::currentChanged, this, [this](int) { applyTabLocks(); });
        applyObservatoryStrip();
    }
}

bool AN_Stack::event(QEvent* event)
{
    if (an::ui::observatoryDockDecorations() && event->type() == QEvent::Show)
    {
        // Shown again after its group moved between a floating window and a dock area: its strip's height may differ.
        applyObservatoryStrip();
    }
    return KDDockWidgets::QtWidgets::Stack::event(event);
}

void AN_Stack::paintEvent(QPaintEvent* event)
{
    if (an::ui::observatoryDockDecorations())
    {
        // The strip's ground under the tabs and the buttons; the pages below lie on the group's.
        if (const QTabBar* strip = tabBar(); strip != nullptr && strip->isVisible())
        {
            QPainter painter(this);
            an::ui::paintObservatoryDockStrip(painter, QRect(0, strip->y(), width(), strip->height()));
        }
        return;
    }
    KDDockWidgets::QtWidgets::Stack::paintEvent(event);
}

void AN_Stack::applyObservatoryStrip()
{
    if (!an::ui::observatoryDockDecorations())
    {
        return;
    }
    const bool floating = inFloatingWindowOf(this);
    if (auto* corner = cornerWidget(Qt::TopRightCorner); corner != nullptr && corner->layout() != nullptr)
    {
        // The tab widget gives its corner the strip's height less its tabs' base.
        const int cornerHeight = an::ui::observatoryDockStripHeight(floating) -
                                 style()->pixelMetric(QStyle::PM_TabBarBaseHeight, nullptr, this);
        corner->layout()->setContentsMargins(an::ui::observatoryDockCornerMargins(cornerHeight));
        corner->layout()->setSpacing(0);
        for (auto* button : corner->findChildren<QAbstractButton*>(Qt::FindDirectChildrenOnly))
        {
            button->updateGeometry();
        }
        corner->updateGeometry();
    }
    if (auto* floatButton = button(KDDockWidgets::TitleBarButtonType::Float))
    {
        floatButton->setToolTip(floating ? QObject::tr("Dock") : QObject::tr("Float / Detach"));
    }
    if (m_tabList != nullptr)
    {
        m_tabList->setToolTip(QObject::tr("Tab List"));
        m_tabList->setVisible(count() > 1);
    }
    applyTabLocks();
    updateGeometry();
    update();
}

void AN_Stack::applyTabLocks()
{
    if (!an::ui::observatoryDockDecorations() || stack() == nullptr)
    {
        return;
    }
    QTabBar* strip = tabBar();
    const auto closeSide = static_cast<QTabBar::ButtonPosition>(
        strip->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, strip));
    bool currentLocked = false;
    for (int index = 0; index < strip->count(); ++index)
    {
        const an::ui::TabAdornment* adornment = an::ui::tabAdornment(dockWidgetViewAt(stack()->tabBar(), index));
        const bool locked = adornment != nullptr && adornment->locked;
        if (QWidget* close = strip->tabButton(index, closeSide))
        {
            close->setEnabled(!locked);
        }
        if (index == strip->currentIndex())
        {
            currentLocked = locked;
        }
    }
    if (auto* floatButton = button(KDDockWidgets::TitleBarButtonType::Float))
    {
        floatButton->setEnabled(!currentLocked);
    }
    if (auto* closeButton = button(KDDockWidgets::TitleBarButtonType::Close))
    {
        const bool closable = stack()->group() == nullptr || !stack()->group()->anyNonClosable();
        closeButton->setEnabled(!currentLocked && closable);
    }
}

void AN_Stack::showTabList()
{
    if (m_tabList == nullptr)
    {
        return;
    }
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("anDockTabListMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (int index = 0; index < count(); ++index)
    {
        QAction* action = menu->addAction(tabText(index));
        action->setEnabled(index != currentIndex());
        connect(action, &QAction::triggered, this, [this, index]() {
            if (index < count())
            {
                setCurrentIndex(index);
            }
        });
    }
    menu->popup(m_tabList->mapToGlobal(QPoint(0, m_tabList->height())));
}

void AN_Stack::applyThemeColors()
{
    const auto& c = an::ui::tokens().colors;

    // Set palette for the stack pane background/border — KddwProxyStyle handles the rest.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, c.bgPrimary);
    pal.setColor(QPalette::Mid, c.borderSubtle); // border
    setPalette(pal);
    update();
    applyObservatoryStrip();
}

namespace
{
QIcon iconFromContext(const QString& iconName)
{
    if (iconName.isEmpty())
    {
        return QIcon();
    }

    if (auto* context = an::ui::appContext())
    {
        if (auto iconProvider = context->getIconProvider())
        {
            return iconProvider->getIcon(iconName);
        }
    }

    return QIcon();
}
} // namespace

void AN_Stack::styleCornerButtons()
{
    using KDDockWidgets::TitleBarButtonType;

    auto* closeBtn = button(TitleBarButtonType::Close);
    auto* floatBtn = button(TitleBarButtonType::Float);

    // Tooltips only — AN_DockButton handles all visual styling.
    const bool closeOnlyCurrent =
        (KDDockWidgets::Config::self().flags() & KDDockWidgets::Config::Flag_CloseOnlyCurrentTab);

    if (closeBtn)
    {
        closeBtn->setToolTip(closeOnlyCurrent ? QObject::tr("Close Active Image") : QObject::tr("Close Tab Group"));
    }

    if (floatBtn)
    {
        floatBtn->setToolTip(QObject::tr("Float / Detach"));
    }
    applyObservatoryStrip();
}

void AN_Stack::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (layoutChangesLocked())
    {
        event->accept();
    }
    else
    {
        KDDockWidgets::QtWidgets::Stack::mouseDoubleClickEvent(event);
    }
}

void AN_Stack::mousePressEvent(QMouseEvent* event)
{
    // Resolve the Core DockWidget under the click (or the current one if not on a tab)
    KDDockWidgets::Core::DockWidget* dwCore = nullptr;

    // Controller stack -> controller tab bar
    if (auto* coreStack = this->stack())
    {
        if (auto* coreTabBar = coreStack->tabBar())
        {
            int idx = -1;

            // Try to map the click to the QTabBar to get the specific tab index
            if (auto* qtb = this->tabBar())
            {
                const QPoint posInTabBar = qtb->mapFrom(this, event->pos());
                idx = qtb->tabAt(posInTabBar);
            }

            if (idx >= 0)
                dwCore = coreTabBar->dockWidgetAt(idx);
            else
                dwCore = coreTabBar->currentDockWidget();
        }
    }

    if (dwCore)
    {
        setActiveDockWidgetView(dwCore);
    }

    KDDockWidgets::QtWidgets::Stack::mousePressEvent(event);
}

// ---------------- AN_Group ----------------

AN_Group::AN_Group(KDDockWidgets::Core::Group* controller, QWidget* parent)
    : KDDockWidgets::QtWidgets::Group(controller, parent)
{
}

void AN_Group::paintEvent(QPaintEvent*)
{
    if (an::ui::observatoryDockDecorations())
    {
        // A docked group's ground; a floating window paints its own.
        if (!m_group->isFloating())
        {
            QPainter painter(this);
            an::ui::paintObservatoryDockPane(painter, QWidget::rect());
        }
        return;
    }
    if (m_group->isFloating())
        return; // floating windows have their own border painting

    const auto& c = an::ui::tokens().colors;
    const auto r = an::ui::tokens().radius.sm;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal penWidth = 1.0;
    const qreal halfPen = penWidth / 2.0;
    const QRectF rectf = QWidget::rect();

    const bool isOverlayed = m_group->isOverlayed();
    const QColor penColor = isOverlayed ? c.borderPrimary : c.borderSubtle;
    QPen pen(penColor);
    pen.setWidthF(penWidth);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (isOverlayed)
    {
        pen.setJoinStyle(Qt::MiterJoin);
        p.setPen(pen);
        p.drawRect(rectf.adjusted(halfPen, penWidth, -halfPen, -halfPen));
    }
    else
    {
        p.drawRoundedRect(rectf.adjusted(halfPen, halfPen, -halfPen, -halfPen), r, r);
    }
}

// ---------------- AN_FloatingWindow ----------------

AN_FloatingWindow::AN_FloatingWindow(KDDockWidgets::Core::FloatingWindow* controller, QMainWindow* parent,
                                     Qt::WindowFlags windowFlags)
    : KDDockWidgets::QtWidgets::FloatingWindow(controller, parent, windowFlags)
{
}

bool AN_FloatingWindow::event(QEvent* event)
{
    if (an::ui::observatoryDockDecorations() && event->type() == QEvent::ActivationChange)
    {
        // The frame is the accent while the window is active.
        update();
    }
    return KDDockWidgets::QtWidgets::FloatingWindow::event(event);
}

void AN_FloatingWindow::paintEvent(QPaintEvent*)
{
    if (an::ui::observatoryDockDecorations())
    {
        QPainter painter(this);
        an::ui::paintObservatoryDockFrame(painter, QWidget::rect(), isActiveWindow());
        return;
    }
    const auto& c = an::ui::tokens().colors;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPen pen(c.borderPrimary);
    pen.setWidth(1);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal halfPen = pen.widthF() / 2.0;
    const QRectF rectf = rect();
    p.drawRect(rectf.adjusted(halfPen, halfPen, -halfPen, -halfPen));
}

// ---------------- AN_Separator ----------------

AN_Separator::AN_Separator(KDDockWidgets::Core::Separator* controller, KDDockWidgets::Core::View* parent)
    : KDDockWidgets::QtWidgets::Separator(controller, parent)
{
}

void AN_Separator::paintEvent(QPaintEvent* event)
{
    if (an::ui::observatoryDockDecorations())
    {
        QPainter painter(this);
        an::ui::paintObservatoryDockSeparator(painter, QWidget::rect(), m_hovered);
        return;
    }
    KDDockWidgets::QtWidgets::Separator::paintEvent(event);
}

void AN_Separator::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    KDDockWidgets::QtWidgets::Separator::enterEvent(event);
}

void AN_Separator::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    KDDockWidgets::QtWidgets::Separator::leaveEvent(event);
}

// ---------------- Factory ----------------

QIcon AN_ViewFactory::iconForButtonType(KDDockWidgets::TitleBarButtonType type, qreal dpr) const
{
    auto key = std::make_pair(type, dpr);
    auto it = m_anCachedIcons.constFind(key);
    if (it != m_anCachedIcons.cend())
        return *it;

    using KDDockWidgets::TitleBarButtonType;
    QString iconName;
    switch (type)
    {
    case TitleBarButtonType::Close:
        iconName = QStringLiteral("close");
        break;
    case TitleBarButtonType::Float:
    case TitleBarButtonType::Normal:
        iconName = QStringLiteral("detach");
        break;
    case TitleBarButtonType::Maximize:
        iconName = QStringLiteral("fullscreen");
        break;
    case TitleBarButtonType::Minimize:
        iconName = QStringLiteral("fullscreen_exit");
        break;
    case TitleBarButtonType::AutoHide:
    case TitleBarButtonType::UnautoHide:
        iconName = QStringLiteral("lock");
        break;
    default:
        // Fallback to KDDW's default
        return KDDockWidgets::QtWidgets::ViewFactory::iconForButtonType(type, dpr);
    }

    QIcon icon = iconFromContext(iconName);
    if (icon.isNull())
    {
        return KDDockWidgets::QtWidgets::ViewFactory::iconForButtonType(type, dpr);
    }

    m_anCachedIcons.insert(key, icon);
    return icon;
}

void AN_ViewFactory::clearIconCache()
{
    m_anCachedIcons.clear();
}

QAbstractButton* AN_ViewFactory::createTitleBarButton(QWidget* parent, KDDockWidgets::TitleBarButtonType type) const
{
    // Only use AN_DockButton for Stack corner-widget buttons.
    // TitleBar buttons MUST remain KDDW's Button subclass, because
    // TitleBar::~TitleBar() does qobject_cast<Button*> and dereferences
    // the result — using AN_DockButton here would cause a nullptr crash.
    if (qobject_cast<KDDockWidgets::QtWidgets::Stack*>(parent))
        return new AN_DockButton(type, parent);

    return KDDockWidgets::QtWidgets::ViewFactory::createTitleBarButton(parent, type);
}

KDDockWidgets::Core::View* AN_ViewFactory::createTabBar(KDDockWidgets::Core::TabBar* controller,
                                                        KDDockWidgets::Core::View* parent) const
{
    return new AN_TabBar(controller, parent);
}

KDDockWidgets::Core::View* AN_ViewFactory::createTitleBar(KDDockWidgets::Core::TitleBar* controller,
                                                          KDDockWidgets::Core::View* parent) const
{
    return new AN_TitleBar(controller, parent);
}

KDDockWidgets::Core::View* AN_ViewFactory::createStack(KDDockWidgets::Core::Stack* controller,
                                                       KDDockWidgets::Core::View* parent) const
{
    return new AN_Stack(controller, parent);
}

KDDockWidgets::Core::View* AN_ViewFactory::createGroup(KDDockWidgets::Core::Group* controller,
                                                       KDDockWidgets::Core::View* parent) const
{
    return new AN_Group(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent));
}

KDDockWidgets::Core::View* AN_ViewFactory::createFloatingWindow(KDDockWidgets::Core::FloatingWindow* controller,
                                                                KDDockWidgets::Core::MainWindow* parent,
                                                                Qt::WindowFlags windowFlags) const
{
    auto* mainwindow =
        qobject_cast<QMainWindow*>(KDDockWidgets::QtCommon::View_qt::asQWidget(parent ? parent->view() : nullptr));
    return new AN_FloatingWindow(controller, mainwindow, windowFlags);
}

KDDockWidgets::Core::View* AN_ViewFactory::createSeparator(KDDockWidgets::Core::Separator* controller,
                                                           KDDockWidgets::Core::View* parent) const
{
    return new AN_Separator(controller, parent);
}

KDDockWidgets::Core::ClassicIndicatorWindowViewInterface*
AN_ViewFactory::createClassicIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators,
                                             KDDockWidgets::Core::View* parent) const
{
    if (an::ui::observatoryDockDecorations())
    {
        return new AN_DropIndicators(indicators);
    }
    return KDDockWidgets::QtWidgets::ViewFactory::createClassicIndicatorWindow(indicators, parent);
}

KDDockWidgets::Core::View* AN_ViewFactory::createRubberBand(KDDockWidgets::Core::View* parent) const
{
    if (an::ui::observatoryDockDecorations())
    {
        return new AN_DropBand(KDDockWidgets::QtCommon::View_qt::asQWidget(parent));
    }
    return KDDockWidgets::QtWidgets::ViewFactory::createRubberBand(parent);
}
