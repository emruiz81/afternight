// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// What the docking suite's cases share: the dock library configured as the application configures it, the windows and
// dock widgets a case makes and the fixture that takes them away again, and a drag driven by events.
//
// The suite holds the dock library to the behaviour the interface is built on. The library's drag controller is not
// part of its installed interface, so a case cannot ask it for its state or end a drag through it: a drag is driven
// the way a user drives one - a press, moves and a release delivered to the widget the library drags by - and its
// state is read from what the library shows: the hooks it calls, the dragged window's opacity, the drop indicators.

#include <gtest/gtest.h>

#include <QEvent>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <ostream>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/indicators/ClassicDropIndicatorOverlay.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/DropArea.h>
#include <kddockwidgets/core/DropIndicatorOverlay.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/MainWindow.h>
#include <kddockwidgets/core/views/ClassicIndicatorWindowViewInterface.h>

#include "controls/KDDockWidgetsHelper.h"

class QMouseEvent;
class QPaintEvent;
class QStackedWidget;

// How a failed comparison prints the values the cases compare.
inline void PrintTo(const QString& text, std::ostream* out)
{
    *out << '"' << text.toStdString() << '"';
}

inline void PrintTo(const QStringList& list, std::ostream* out)
{
    *out << "{" << list.join(QStringLiteral(", ")).toStdString() << "}";
}

inline void PrintTo(const QPoint& point, std::ostream* out)
{
    *out << "(" << point.x() << ", " << point.y() << ")";
}

inline void PrintTo(const QRect& rect, std::ostream* out)
{
    *out << "(" << rect.x() << ", " << rect.y() << " " << rect.width() << "x" << rect.height() << ")";
}

namespace an::tests::docking
{

using DockWidget = KDDockWidgets::QtWidgets::DockWidget;
using MainWindow = KDDockWidgets::QtWidgets::MainWindow;

/// The affinity that keeps a task's dock widget out of every workspace.
inline QString taskAffinity()
{
    return QStringLiteral("an.task");
}

/// Configures the dock library once for the process, with the flags, the dragged-window opacity, the minimum widget
/// size and the decorations the application's own dock area is made with.
void configureDockLibrary();

/// Whether floating windows are dragged by the system's title bar. The library keeps the application's request for
/// one on Windows and drops it on X11, where a window is dragged by the library's own title bar or tab strip.
[[nodiscard]] bool floatingWindowsUseTheNativeTitleBar();

/// Delivers what is posted, deletes what asked to be deleted later, and lets layouts settle.
void settle();

/// The widget of a library view, or null.
[[nodiscard]] QWidget* widgetOf(KDDockWidgets::Core::View* view);

/// The window a widget is in, and where the widget stands inside a widget above it. The library's views give both
/// names another meaning, so a case asks here.
[[nodiscard]] QWidget* windowOf(const QWidget* widget);
[[nodiscard]] QPoint placeIn(const QWidget* widget, const QWidget* ancestor);

/// The main window a widget is inside of, read from the widgets above it; null for a widget in a floating window.
[[nodiscard]] MainWindow* mainWindowHolding(const QWidget* widget);

/// The dock widgets docked in a main window, by their unique names, sorted.
[[nodiscard]] QStringList dockedIn(const MainWindow* mainWindow);

/// The drop indicators of a main window's dock area, and of the floating window a dock widget is in.
[[nodiscard]] KDDockWidgets::Core::DropIndicatorOverlay* indicatorsOf(const MainWindow* mainWindow);
[[nodiscard]] KDDockWidgets::Core::DropIndicatorOverlay* indicatorsOfWindowOf(const DockWidget* dockWidget);

/// How many of the library's indicator windows are on screen.
[[nodiscard]] int indicatorWindowsOnScreen();

/// An indicator window made by the suite instead of the library's, and the band it marks the landing place with.
/// A decoration of the product's own is made the same way: a widget that implements the library's interface.
class ProbeIndicatorWindow : public QWidget, public KDDockWidgets::Core::ClassicIndicatorWindowViewInterface
{
public:
    explicit ProbeIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators);

    KDDockWidgets::DropLocation hover(QPoint globalPos) override;
    QPoint posForIndicator(KDDockWidgets::DropLocation location) const override;
    void updatePositions() override;
    void raise() override;
    void setVisible(bool visible) override;
    void resize(QSize size) override;
    void setGeometry(QRect geometry) override;
    bool isWindow() const override;
    void setObjectName(const QString& name) override;
    void updateIndicatorVisibility() override;

    [[nodiscard]] int hoverCount() const
    {
        return m_hoverCount;
    }

private:
    [[nodiscard]] QRect rectFor(KDDockWidgets::DropLocation location) const;

    KDDockWidgets::Core::ClassicDropIndicatorOverlay* m_indicators = nullptr;
    int m_hoverCount = 0;
};

class ProbeRubberBand;

/// The application's view factory, with the two hooks a decoration of the product's own replaces. A case that sets
/// replaceIndicators() before it makes a window gets the suite's indicator window and band in that window.
class ProbeViewFactory : public AN_ViewFactory
{
public:
    KDDockWidgets::Core::ClassicIndicatorWindowViewInterface*
    createClassicIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay* indicators,
                                 KDDockWidgets::Core::View* parent) const override;
    KDDockWidgets::Core::View* createRubberBand(KDDockWidgets::Core::View* parent) const override;

    static void setReplaceIndicators(bool replace);
};

/// Whether a widget is the band the suite's factory makes.
[[nodiscard]] bool isProbeRubberBand(const QWidget* widget);

/// Delivers a mouse event to a widget as a user's would arrive, with the cursor placed where the event says it is:
/// the library asks the platform for the cursor when it looks for what is under a dragged window.
void deliverMouse(QWidget* receiver, QEvent::Type type, QPoint globalPos);

/// Shows a task's dock widget in a floating window of its own that has one title bar and no tab strip.
void showAsTaskWindow(DockWidget* task, QRect geometry);

/// What stands for a task's thumbnail in the window the task belongs to. Pressed and pulled past that window's frame,
/// it shows its task in a floating window under the cursor and hands the press to the dock library as a drag.
class TaskThumbnail : public QWidget
{
public:
    TaskThumbnail(DockWidget* task, QWidget* parent);

    /// How often the task was pulled out, and whether the library took the last pull as a drag.
    [[nodiscard]] int pulls() const
    {
        return m_pulls;
    }
    [[nodiscard]] bool dragStarted() const
    {
        return m_dragStarted;
    }
    [[nodiscard]] int releases() const
    {
        return m_releases;
    }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QPointer<DockWidget> m_task;
    bool m_pressed = false;
    bool m_dragStarted = false;
    int m_pulls = 0;
    int m_releases = 0;
};

/// A window that holds two workspaces as the pages of one stack.
struct WorkspaceStack
{
    QWidget* window = nullptr;
    QStackedWidget* stack = nullptr;
    MainWindow* first = nullptr;
    MainWindow* second = nullptr;

    /// Puts a workspace on screen, as choosing its place does.
    void show(MainWindow* workspace) const;
};

/// A drag driven by events, from the press to the release.
class SimulatedDrag
{
public:
    /// Presses on the floating window a dock widget is in - on its native title bar where the library uses one, on
    /// the library's title bar or tab strip elsewhere - and moves far enough for the drag to start.
    static SimulatedDrag ofFloatingWindow(DockWidget* dockWidget);

    /// Presses on a docked widget's tab and pulls it out of its strip.
    static SimulatedDrag ofTab(DockWidget* dockWidget);

    /// Takes over a drag the library started by code on a floating dock widget: the moves and the release that
    /// follow are delivered as a user's would be.
    static SimulatedDrag joining(DockWidget* dockWidget);

    /// Moves the cursor, and what is dragged, to a place on the screen.
    void moveTo(QPoint globalPos);

    /// Moves over a dock area and then onto one of its indicators. False when that indicator is not offered.
    bool moveOnto(KDDockWidgets::Core::DropIndicatorOverlay* indicators, QPoint over,
                  KDDockWidgets::DropLocation location);

    /// Releases the button where the cursor is.
    void release();

private:
    explicit SimulatedDrag(DockWidget* dockWidget);
    void press(QWidget* target, QPoint localPos, bool nonClient);
    void send(QEvent::Type type, QPoint globalPos);
    [[nodiscard]] QWidget* target() const;

    QPointer<DockWidget> m_dockWidget;
    QPointer<QWidget> m_pressed;
    QPoint m_position;
    bool m_nonClient = false;
};

/// Makes a case's windows and dock widgets and takes them away when it ends, with the library's hooks cleared, so a
/// case starts from an empty registry whatever the one before it did.
class DockingTest : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    /// Two workspaces in one window, the first on screen.
    WorkspaceStack makeWorkspaceStack(const QString& firstName = QStringLiteral("an.workspace.1"),
                                      const QString& secondName = QStringLiteral("an.workspace.2"));

    /// A dock widget with no affinity, as an image view's is, deleted when closed.
    DockWidget* makeView(const QString& name);

    /// A dock widget as a task's is made: with the task affinity, and skipped by a saved layout.
    DockWidget* makeTask(const QString& name);

    /// Shows a dock widget in a floating window of its own at a place on the screen.
    void floatAt(DockWidget* dockWidget, QRect geometry);

    /// A widget the fixture deletes with the case.
    template <typename T> T* own(T* widget)
    {
        m_owned.append(QPointer<QWidget>(widget));
        return widget;
    }

private:
    QList<QPointer<QWidget>> m_owned;
    QList<QPointer<DockWidget>> m_dockWidgets;
};

} // namespace an::tests::docking
