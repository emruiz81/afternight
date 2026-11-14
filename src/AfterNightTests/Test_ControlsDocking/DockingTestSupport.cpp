// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "DockingTestSupport.h"

#include <QApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/Stack.h>
#include <kddockwidgets/core/TitleBar.h>
#include <kddockwidgets/qtcommon/View.h>
#include <kddockwidgets/qtwidgets/views/RubberBand.h>
#include <kddockwidgets/qtwidgets/views/Stack.h>

#include <algorithm>
#include <array>

namespace an::tests::docking
{

namespace
{

using KDDockWidgets::Config;
using KDDockWidgets::DropLocation;

// How far a press travels before the library takes it for a drag: well past the platform's drag distance, and far
// enough down for a tab to leave its strip instead of being reordered in it.
constexpr int kDragStartTravel = 60;

// The side of one of the suite's own indicators, and its distance from the edge or the centre it stands for.
constexpr int kProbeIndicatorSide = 32;
constexpr int kProbeIndicatorGap = 40;

constexpr std::array<DropLocation, 9> kIndicatorLocations = {
    KDDockWidgets::DropLocation_Center,       KDDockWidgets::DropLocation_Left,
    KDDockWidgets::DropLocation_Top,          KDDockWidgets::DropLocation_Right,
    KDDockWidgets::DropLocation_Bottom,       KDDockWidgets::DropLocation_OutterLeft,
    KDDockWidgets::DropLocation_OutterTop,    KDDockWidgets::DropLocation_OutterRight,
    KDDockWidgets::DropLocation_OutterBottom,
};

bool g_replaceIndicators = false;

} // namespace

/// The band the suite's factory makes in place of the library's.
class ProbeRubberBand : public KDDockWidgets::QtWidgets::RubberBand
{
public:
    using KDDockWidgets::QtWidgets::RubberBand::RubberBand;
};

void configureDockLibrary()
{
    static bool configured = false;
    if (configured)
    {
        return;
    }
    configured = true;

    // The configuration the application's shell core sets (its DockHooks), from the decorations' own declaration,
    // with the suite's factory in place of the decorations'.
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    Config::Flags flags = Config::self().flags();
    for (const Config::Flag flag : kDockLibraryFlags)
    {
        flags |= flag;
    }
    Config::self().setFlags(flags);
    Config::self().setDraggedWindowOpacity(kDraggedDockWindowOpacity);
    Config::self().setAbsoluteWidgetMinSize(QSize(kDockWidgetMinimumSide, kDockWidgetMinimumSide));
    Config::self().setViewFactory(new ProbeViewFactory);
}

bool floatingWindowsUseTheNativeTitleBar()
{
    return Config::self().flags().testFlag(Config::Flag_NativeTitleBar);
}

void settle()
{
    QCoreApplication::sendPostedEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QWidget* widgetOf(KDDockWidgets::Core::View* view)
{
    return view ? KDDockWidgets::QtCommon::View_qt::asQWidget(view) : nullptr;
}

QWidget* windowOf(const QWidget* widget)
{
    return widget->QWidget::window();
}

QPoint placeIn(const QWidget* widget, const QWidget* ancestor)
{
    return widget->QWidget::mapTo(ancestor, QPoint(0, 0));
}

MainWindow* mainWindowHolding(const QWidget* widget)
{
    for (auto* current = const_cast<QWidget*>(widget); current; current = current->parentWidget())
    {
        if (auto* mainWindow = qobject_cast<MainWindow*>(current))
        {
            return mainWindow;
        }
        if (current->isWindow())
        {
            // A floating window is a window of its own: the main window it is parented to does not hold it.
            return nullptr;
        }
    }
    return nullptr;
}

QStringList dockedIn(const MainWindow* mainWindow)
{
    QStringList names;
    const auto dockWidgets = KDDockWidgets::DockRegistry::self()->dockwidgets();
    for (const auto* dockWidget : dockWidgets)
    {
        if (dockWidget->isOpen() && dockWidget->mainWindow() == mainWindow->mainWindow())
        {
            names.append(dockWidget->uniqueName());
        }
    }
    names.sort();
    return names;
}

KDDockWidgets::Core::DropIndicatorOverlay* indicatorsOf(const MainWindow* mainWindow)
{
    return mainWindow->mainWindow()->dropArea()->dropIndicatorOverlay();
}

KDDockWidgets::Core::DropIndicatorOverlay* indicatorsOfWindowOf(const DockWidget* dockWidget)
{
    auto* floatingWindow = dockWidget->dockWidget()->floatingWindow();
    return floatingWindow ? floatingWindow->dropArea()->dropIndicatorOverlay() : nullptr;
}

int indicatorWindowsOnScreen()
{
    int count = 0;
    const auto topLevels = QApplication::topLevelWidgets();
    for (const auto* widget : topLevels)
    {
        if (widget->isVisible() && widget->objectName() == QLatin1String("_docks_IndicatorWindow_Overlay"))
        {
            ++count;
        }
    }
    return count;
}

// ── The suite's indicator window ─────────────────────────────────────────────────────────────────────────────────

ProbeIndicatorWindow::ProbeIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint), m_indicators(indicators)
{
    setAttribute(Qt::WA_TranslucentBackground);
}

QRect ProbeIndicatorWindow::rectFor(DropLocation location) const
{
    const QRect outer = rect();
    const QPoint centre = m_indicators->hoveredGroupRect().center();
    QPoint at;
    switch (location)
    {
    case KDDockWidgets::DropLocation_Center:
        at = centre;
        break;
    case KDDockWidgets::DropLocation_Left:
        at = centre - QPoint(kProbeIndicatorGap, 0);
        break;
    case KDDockWidgets::DropLocation_Right:
        at = centre + QPoint(kProbeIndicatorGap, 0);
        break;
    case KDDockWidgets::DropLocation_Top:
        at = centre - QPoint(0, kProbeIndicatorGap);
        break;
    case KDDockWidgets::DropLocation_Bottom:
        at = centre + QPoint(0, kProbeIndicatorGap);
        break;
    case KDDockWidgets::DropLocation_OutterLeft:
        at = QPoint(outer.left() + kProbeIndicatorGap, outer.center().y());
        break;
    case KDDockWidgets::DropLocation_OutterRight:
        at = QPoint(outer.right() - kProbeIndicatorGap, outer.center().y());
        break;
    case KDDockWidgets::DropLocation_OutterTop:
        at = QPoint(outer.center().x(), outer.top() + kProbeIndicatorGap);
        break;
    case KDDockWidgets::DropLocation_OutterBottom:
        at = QPoint(outer.center().x(), outer.bottom() - kProbeIndicatorGap);
        break;
    default:
        return {};
    }

    QRect indicator(0, 0, kProbeIndicatorSide, kProbeIndicatorSide);
    indicator.moveCenter(at);
    return indicator;
}

DropLocation ProbeIndicatorWindow::hover(QPoint globalPos)
{
    ++m_hoverCount;
    const QPoint local = mapFromGlobal(globalPos);
    for (const auto location : kIndicatorLocations)
    {
        if (m_indicators->dropIndicatorVisible(location) && rectFor(location).contains(local))
        {
            return location;
        }
    }
    return KDDockWidgets::DropLocation_None;
}

QPoint ProbeIndicatorWindow::posForIndicator(DropLocation location) const
{
    return mapToGlobal(rectFor(location).center());
}

void ProbeIndicatorWindow::updatePositions()
{
    update();
}

void ProbeIndicatorWindow::raise()
{
    QWidget::raise();
}

void ProbeIndicatorWindow::setVisible(bool visible)
{
    QWidget::setVisible(visible);
}

void ProbeIndicatorWindow::resize(QSize size)
{
    QWidget::resize(size);
}

void ProbeIndicatorWindow::setGeometry(QRect geometry)
{
    QWidget::setGeometry(geometry);
}

bool ProbeIndicatorWindow::isWindow() const
{
    return QWidget::isWindow();
}

void ProbeIndicatorWindow::setObjectName(const QString& name)
{
    QWidget::setObjectName(name);
}

void ProbeIndicatorWindow::updateIndicatorVisibility()
{
    update();
}

// ── The suite's view factory ─────────────────────────────────────────────────────────────────────────────────────

KDDockWidgets::Core::ClassicIndicatorWindowViewInterface*
ProbeViewFactory::createClassicIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators,
                                               KDDockWidgets::Core::View* parent) const
{
    if (g_replaceIndicators)
    {
        return new ProbeIndicatorWindow(indicators);
    }
    return AN_ViewFactory::createClassicIndicatorWindow(indicators, parent);
}

KDDockWidgets::Core::View* ProbeViewFactory::createRubberBand(KDDockWidgets::Core::View* parent) const
{
    if (g_replaceIndicators)
    {
        return new ProbeRubberBand(widgetOf(parent));
    }
    return AN_ViewFactory::createRubberBand(parent);
}

void ProbeViewFactory::setReplaceIndicators(bool replace)
{
    g_replaceIndicators = replace;
}

bool isProbeRubberBand(const QWidget* widget)
{
    return dynamic_cast<const ProbeRubberBand*>(widget) != nullptr;
}

// ── Events, and a task shown as a window ─────────────────────────────────────────────────────────────────────────

void deliverMouse(QWidget* receiver, QEvent::Type type, QPoint globalPos)
{
    QCursor::setPos(globalPos);

    const bool release = type == QEvent::MouseButtonRelease || type == QEvent::NonClientAreaMouseButtonRelease;
    const bool move = type == QEvent::MouseMove || type == QEvent::NonClientAreaMouseMove;
    QMouseEvent event(type, QPointF(receiver->mapFromGlobal(globalPos)), QPointF(globalPos),
                      move ? Qt::NoButton : Qt::LeftButton, release ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &event);
}

void showAsTaskWindow(DockWidget* task, QRect geometry)
{
    // Asked of the dock widget before its window exists. The window's title bar is the system's where the library
    // uses one and the library's everywhere else, whatever the tab strip would have hidden.
    KDDockWidgets::FloatingWindowFlags flags = KDDockWidgets::FloatingWindowFlag::AlwaysTitleBarWhenFloating;
    if (floatingWindowsUseTheNativeTitleBar())
    {
        flags |= KDDockWidgets::FloatingWindowFlag::NativeTitleBar;
    }
    task->dockWidget()->setFloatingWindowFlags(flags);
    task->dockWidget()->setFloatingGeometry(geometry);
    task->open();
    settle();
    if (auto* window = windowOf(task); window && window->geometry() != geometry)
    {
        window->setGeometry(geometry);
        settle();
    }

    // And of the group the window made for it: a strip with one tab hides.
    if (auto* group = task->group())
    {
        group->stack()->setTabBarAutoHide(true);
    }
    settle();
}

TaskThumbnail::TaskThumbnail(DockWidget* task, QWidget* parent) : QWidget(parent), m_task(task)
{
    setMouseTracking(false);
}

void TaskThumbnail::mousePressEvent(QMouseEvent* event)
{
    m_pressed = true;
    event->accept();
}

void TaskThumbnail::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    if (!m_pressed || !m_task || m_task->isOpen())
    {
        return;
    }

    const QPoint cursor = event->globalPosition().toPoint();
    if (windowOf(this)->frameGeometry().contains(cursor))
    {
        // Still over the window the task belongs to: nothing leaves it yet.
        return;
    }

    // Past the frame: the task becomes a window with its top under the cursor, and the press becomes a drag of it.
    const QSize size(360, 260);
    showAsTaskWindow(m_task, QRect(cursor - QPoint(size.width() / 2, 14), size));
    QCursor::setPos(cursor);
    ++m_pulls;
    m_dragStarted = m_task->dockWidget()->startDragging();
}

void TaskThumbnail::mouseReleaseEvent(QMouseEvent* event)
{
    m_pressed = false;
    ++m_releases;
    event->accept();
}

void TaskThumbnail::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().highlight());
    painter.setPen(palette().highlightedText().color());
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("task"));
}

// ── Workspaces ───────────────────────────────────────────────────────────────────────────────────────────────────

void WorkspaceStack::show(MainWindow* workspace) const
{
    stack->setCurrentWidget(workspace);
    settle();
}

// ── A drag driven by events ──────────────────────────────────────────────────────────────────────────────────────

SimulatedDrag::SimulatedDrag(DockWidget* dockWidget) : m_dockWidget(dockWidget) {}

namespace
{

// The widget the library drags a floating window by, and a place on it to press.
struct Grip
{
    QWidget* widget = nullptr;
    QPoint at;
    bool nonClient = false;
};

Grip gripOf(KDDockWidgets::Core::FloatingWindow* floatingWindow)
{
    if (floatingWindowsUseTheNativeTitleBar())
    {
        // The system's title bar: the library hears of the press as a press outside the window's client area.
        return {widgetOf(floatingWindow->view()), QPoint(24, 8), true};
    }

    if (auto* titleBar = floatingWindow->titleBar(); titleBar && titleBar->isVisible())
    {
        auto* widget = widgetOf(titleBar->view());
        return {widget, QPoint(24, widget->height() / 2), false};
    }

    // No title bar on screen: the window is dragged by the empty part of its tab strip.
    auto* stack = qobject_cast<KDDockWidgets::QtWidgets::Stack*>(
        widgetOf(floatingWindow->groups().constFirst()->stack()->view()));
    if (!stack)
    {
        return {};
    }
    const QRect tabs = stack->tabBar()->geometry();
    return {stack, QPoint((std::min)(tabs.right() + 12, stack->width() - 2), tabs.height() / 2), false};
}

} // namespace

SimulatedDrag SimulatedDrag::ofFloatingWindow(DockWidget* dockWidget)
{
    SimulatedDrag drag(dockWidget);
    auto* floatingWindow = dockWidget->dockWidget()->floatingWindow();
    const Grip grip = floatingWindow ? gripOf(floatingWindow) : Grip();
    if (!grip.widget)
    {
        ADD_FAILURE() << dockWidget->uniqueName().toStdString() << " is in no floating window that can be dragged";
        return drag;
    }

    drag.press(grip.widget, grip.at, grip.nonClient);
    drag.moveTo(drag.m_position + QPoint(kDragStartTravel, kDragStartTravel));
    return drag;
}

SimulatedDrag SimulatedDrag::joining(DockWidget* dockWidget)
{
    SimulatedDrag drag(dockWidget);
    auto* floatingWindow = dockWidget->dockWidget()->floatingWindow();
    const Grip grip = floatingWindow ? gripOf(floatingWindow) : Grip();
    if (!grip.widget)
    {
        ADD_FAILURE() << dockWidget->uniqueName().toStdString() << " is in no floating window that can be dragged";
        return drag;
    }

    drag.m_pressed = grip.widget;
    drag.m_nonClient = grip.nonClient;
    drag.m_position = QCursor::pos();
    return drag;
}

SimulatedDrag SimulatedDrag::ofTab(DockWidget* dockWidget)
{
    SimulatedDrag drag(dockWidget);
    auto* group = dockWidget->group();
    auto* stack = group ? qobject_cast<KDDockWidgets::QtWidgets::Stack*>(widgetOf(group->stack()->view())) : nullptr;
    if (!stack)
    {
        ADD_FAILURE() << dockWidget->uniqueName().toStdString() << " has no tab";
        return drag;
    }

    auto* tabBar = stack->tabBar();
    const int index = stack->indexOf(dockWidget);
    drag.press(tabBar, tabBar->tabRect(index).center(), false);
    drag.moveTo(drag.m_position + QPoint(0, kDragStartTravel));
    return drag;
}

void SimulatedDrag::press(QWidget* target, QPoint localPos, bool nonClient)
{
    m_pressed = target;
    m_nonClient = nonClient;
    m_position = target->mapToGlobal(localPos);
    send(nonClient ? QEvent::NonClientAreaMouseButtonPress : QEvent::MouseButtonPress, m_position);
}

QWidget* SimulatedDrag::target() const
{
    // Once a tab has left its strip under a native title bar, the library drags the new floating window by that
    // title bar and listens on the window; everywhere else it goes on listening on the widget that was pressed.
    if (m_dockWidget && floatingWindowsUseTheNativeTitleBar())
    {
        if (auto* floatingWindow = m_dockWidget->dockWidget()->floatingWindow())
        {
            return widgetOf(floatingWindow->view());
        }
    }
    return m_pressed;
}

void SimulatedDrag::send(QEvent::Type type, QPoint globalPos)
{
    auto* receiver = target();
    if (!receiver)
    {
        ADD_FAILURE() << "the dragged widget is gone";
        return;
    }
    deliverMouse(receiver, type, globalPos);
}

void SimulatedDrag::moveTo(QPoint globalPos)
{
    m_position = globalPos;
    const bool nonClient = floatingWindowsUseTheNativeTitleBar() && target() != m_pressed ? true : m_nonClient;
    send(nonClient ? QEvent::NonClientAreaMouseMove : QEvent::MouseMove, globalPos);
}

bool SimulatedDrag::moveOnto(KDDockWidgets::Core::DropIndicatorOverlay* indicators, QPoint over, DropLocation location)
{
    // The indicators take their places when the dragged window first comes over the dock area.
    moveTo(over);
    if (!indicators->isHovered())
    {
        return false;
    }
    moveTo(indicators->posForIndicator(location));
    return indicators->currentDropLocation() == location;
}

void SimulatedDrag::release()
{
    const bool nonClient = floatingWindowsUseTheNativeTitleBar() && target() != m_pressed ? true : m_nonClient;
    send(nonClient ? QEvent::NonClientAreaMouseButtonRelease : QEvent::MouseButtonRelease, m_position);
    settle();
}

// ── The fixture ──────────────────────────────────────────────────────────────────────────────────────────────────

void DockingTest::SetUp()
{
    configureDockLibrary();
    ASSERT_TRUE(KDDockWidgets::DockRegistry::self()->isEmpty())
        << "a case starts with no main window, dock widget or floating window left by another";
}

void DockingTest::TearDown()
{
    Config::self().setDragAboutToStartFunc(nullptr);
    Config::self().setDragEndedFunc(nullptr);
    Config::self().setDropIndicatorAllowedFunc(nullptr);
    Config::self().setDropIndicatorsInhibited(false);
    ProbeViewFactory::setReplaceIndicators(false);

    // Dock widgets first, so no window is deleted with a dock widget of the case still inside it.
    for (const auto& dockWidget : std::as_const(m_dockWidgets))
    {
        delete dockWidget.data();
    }
    m_dockWidgets.clear();
    settle();

    for (const auto& widget : std::as_const(m_owned))
    {
        delete widget.data();
    }
    m_owned.clear();
    settle();

    EXPECT_TRUE(KDDockWidgets::DockRegistry::self()->isEmpty(true)) << "the case left something in the dock registry";
}

WorkspaceStack DockingTest::makeWorkspaceStack(const QString& firstName, const QString& secondName)
{
    WorkspaceStack workspaces;
    workspaces.window = own(new QWidget);
    workspaces.window->setObjectName(QStringLiteral("workspaceWindow"));

    auto* layout = new QVBoxLayout(workspaces.window);
    layout->setContentsMargins(0, 0, 0, 0);
    workspaces.stack = new QStackedWidget(workspaces.window);
    layout->addWidget(workspaces.stack);

    // Made with the window as parent and handed to the stack, as the application makes its dock area: the stack
    // takes the main window as a child widget, where a main window made with the stack as parent stays a window.
    // Each is owned by the case as well as by the window: a case may take one out of the stack and leave it there.
    workspaces.first = own(new MainWindow(firstName, KDDockWidgets::MainWindowOption_None, workspaces.window));
    workspaces.second = own(new MainWindow(secondName, KDDockWidgets::MainWindowOption_None, workspaces.window));
    for (auto* workspace : {workspaces.first, workspaces.second})
    {
        workspace->setCenterWidgetMargins(QMargins(0, 0, 0, 0));
        workspaces.stack->addWidget(workspace);
    }

    workspaces.window->setGeometry(20, 20, 480, 360);
    workspaces.window->show();
    settle();
    return workspaces;
}

DockWidget* DockingTest::makeView(const QString& name)
{
    auto* dockWidget = new DockWidget(name, KDDockWidgets::DockWidgetOption_DeleteOnClose);
    dockWidget->setTitle(name);
    dockWidget->setWidget(new QWidget);
    m_dockWidgets.append(QPointer<DockWidget>(dockWidget));
    return dockWidget;
}

DockWidget* DockingTest::makeTask(const QString& name)
{
    auto* dockWidget =
        new DockWidget(name, KDDockWidgets::DockWidgetOption_None, KDDockWidgets::LayoutSaverOption::Skip);
    dockWidget->setTitle(name);
    dockWidget->setAffinities({taskAffinity()});
    dockWidget->setWidget(new QWidget);
    m_dockWidgets.append(QPointer<DockWidget>(dockWidget));
    return dockWidget;
}

void DockingTest::floatAt(DockWidget* dockWidget, QRect geometry)
{
    dockWidget->dockWidget()->setFloatingGeometry(geometry);
    dockWidget->open();
    settle();
    if (auto* window = windowOf(dockWidget); window && window->geometry() != geometry)
    {
        window->setGeometry(geometry);
        settle();
    }
}

} // namespace an::tests::docking
