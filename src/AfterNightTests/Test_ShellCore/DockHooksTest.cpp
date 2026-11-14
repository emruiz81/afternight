// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The one owner of the dock library's process-wide state: its configuration, set once before the first main window
/// is made, and its three hooks, which reach any number of subscribers through one dispatcher - a drag or an indicator
/// allowed only when every subscriber allows it, the end of a drag told to each with what the drag that ended named,
/// and a subscriber that leaves asked nothing more.

#include "ShellCoreTestSupport.h"

#include "core/ApplicationCore.h"
#include "views/DockHooks.h"

#include "controls/KDDockWidgetsHelper.h"

#include <gtest/gtest.h>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/Draggable_p.h>
#include <kddockwidgets/core/DropArea.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/MainWindow.h>
#include <kddockwidgets/core/Stack.h>
#include <kddockwidgets/core/TabBar.h>

#include <QCoreApplication>
#include <QLabel>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QWidget>

#include <memory>
#include <vector>

namespace
{

using an::tests::shellcore::ScopedStorageRoot;
using an::ui::shell::ApplicationCore;
using an::ui::shell::DockDrag;
using an::ui::shell::DockDrop;
using an::ui::shell::DockHooks;
using KDDockWidgets::Config;
using DockWidget = KDDockWidgets::QtWidgets::DockWidget;
using MainWindow = KDDockWidgets::QtWidgets::MainWindow;

void settleDocks()
{
    QCoreApplication::sendPostedEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/// Two main windows in a window of their own, each with a dock widget, and a dock widget floating beside them: what a
/// case asks the hooks about. Everything is the case's and goes with it.
class DockHooksTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // The application's core configures the library as it is made, before any window: the case makes one first,
        // as the application does.
        m_core = std::make_unique<ApplicationCore>();
        m_window = std::make_unique<QWidget>();
        m_window->resize(900, 600);
        m_first = new MainWindow(QStringLiteral("hooks.first"), {}, m_window.get());
        m_second = new MainWindow(QStringLiteral("hooks.second"), {}, m_window.get());
        m_first->setGeometry(QRect(0, 0, 450, 600));
        m_second->setGeometry(QRect(450, 0, 450, 600));
        m_inFirst = dockWidget(QStringLiteral("hooks.inFirst"));
        m_inSecond = dockWidget(QStringLiteral("hooks.inSecond"));
        m_floating = dockWidget(QStringLiteral("hooks.floating"));
        m_first->addDockWidget(m_inFirst, KDDockWidgets::Location_OnLeft);
        m_second->addDockWidget(m_inSecond, KDDockWidgets::Location_OnLeft);
        m_window->show();
        m_floating->open();
        settleDocks();
    }

    void TearDown() override
    {
        for (const QPointer<DockWidget>& dock : m_docks)
        {
            delete dock.data();
        }
        settleDocks();
        m_window.reset();
        settleDocks();
        m_core.reset();
    }

    DockWidget* dockWidget(const QString& name)
    {
        auto* dock = new DockWidget(name);
        dock->setWidget(new QLabel(name));
        m_docks.emplace_back(dock);
        return dock;
    }

    /// What a person pulls a docked dock widget by: the tab strip of its group.
    static KDDockWidgets::Core::Draggable* tabOf(DockWidget* dock)
    {
        KDDockWidgets::Core::Group* const group = dock->group();
        return group != nullptr && group->stack() != nullptr ? group->stack()->tabBar() : nullptr;
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    std::unique_ptr<ApplicationCore> m_core;
    std::unique_ptr<QWidget> m_window;
    MainWindow* m_first = nullptr;
    MainWindow* m_second = nullptr;
    DockWidget* m_inFirst = nullptr;
    DockWidget* m_inSecond = nullptr;
    DockWidget* m_floating = nullptr;
    std::vector<QPointer<DockWidget>> m_docks;
};

} // namespace

// The library is configured for the decorations once, before the first main window: the flags the decorations are
// drawn for, a dragged window at 0.7 of its opacity, no dock widget smaller than 100 x 100, and the decorations' own
// view factory. Asking again changes nothing.
TEST_F(DockHooksTest, ConfiguresTheLibraryOnceForTheDecorations)
{
    ASSERT_TRUE(DockHooks::isConfigured());

    const Config::Flags flags = Config::self().flags();
    for (const Config::Flag flag :
         {Config::Flag_HideTitleBarWhenTabsVisible, Config::Flag_AlwaysShowTabs, Config::Flag_AllowReorderTabs,
          Config::Flag_TabsHaveCloseButton, Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden,
          Config::Flag_AllowSwitchingTabsViaMenu, Config::Flag_TitleBarIsFocusable})
    {
        EXPECT_TRUE(flags.testFlag(flag)) << "flag " << static_cast<int>(flag);
    }
#if defined(Q_OS_WIN)
    // The library keeps the native title bar on Windows; on X11 it drops it, and on Wayland it forces it.
    EXPECT_TRUE(flags.testFlag(Config::Flag_NativeTitleBar));
#endif
    EXPECT_DOUBLE_EQ(Config::self().draggedWindowOpacity(), 0.7);
    EXPECT_EQ(Config::self().absoluteWidgetMinSize(), QSize(100, 100));
    EXPECT_NE(dynamic_cast<AN_ViewFactory*>(Config::self().viewFactory()), nullptr);

    // Set once: what a later call would set again is left as it is.
    Config::self().setDraggedWindowOpacity(0.5);
    DockHooks::configure();
    EXPECT_DOUBLE_EQ(Config::self().draggedWindowOpacity(), 0.5);
    Config::self().setDraggedWindowOpacity(0.7);
}

// The three hooks the library calls are the dispatcher's, and with no subscriber it allows every drag and indicator.
TEST_F(DockHooksTest, TheLibrarysHooksAreTheDispatchersAndAllowEverythingWithNoSubscriber)
{
    ASSERT_NE(Config::self().dragAboutToStartFunc(), nullptr);
    ASSERT_NE(Config::self().dropIndicatorAllowedFunc(), nullptr);
    ASSERT_NE(Config::self().dragEndedFunc(), nullptr);

    KDDockWidgets::Core::Draggable* const tab = tabOf(m_inFirst);
    ASSERT_NE(tab, nullptr);
    EXPECT_TRUE(Config::self().dragAboutToStartFunc()(tab));
    EXPECT_TRUE(DockHooks::allowsDrop(DockDrop{.location = KDDockWidgets::DropLocation_Left, .mainWindow = m_first}));
}

// A drag is allowed only when every subscriber allows it, and each is asked about the main window the dragged widget is
// in and the dock widget it carries: one subscriber refusing the first main window's drags refuses those alone.
TEST_F(DockHooksTest, ADragIsAllowedOnlyWhenEverySubscriberAllowsIt)
{
    std::vector<QString> asked;
    const DockHooks::Subscription allowing = DockHooks::subscribe({.dragAboutToStart = [&asked](const DockDrag& drag) {
        asked.push_back(drag.dockWidget != nullptr ? drag.dockWidget->uniqueName() : QStringLiteral("none"));
        return true;
    }});
    MainWindow* const first = m_first;
    const DockHooks::Subscription refusing =
        DockHooks::subscribe({.dragAboutToStart = [first](const DockDrag& drag) { return drag.mainWindow != first; }});

    KDDockWidgets::Core::Draggable* const inFirst = tabOf(m_inFirst);
    KDDockWidgets::Core::Draggable* const inSecond = tabOf(m_inSecond);
    ASSERT_NE(inFirst, nullptr);
    ASSERT_NE(inSecond, nullptr);

    EXPECT_FALSE(Config::self().dragAboutToStartFunc()(inFirst));
    EXPECT_TRUE(Config::self().dragAboutToStartFunc()(inSecond));
    EXPECT_EQ(asked, (std::vector<QString>{QStringLiteral("hooks.inFirst"), QStringLiteral("hooks.inSecond")}));
}

// An indicator is allowed only when every subscriber allows it; a floating window's dock area belongs to no main
// window.
TEST_F(DockHooksTest, AnIndicatorIsAllowedOnlyWhenEverySubscriberAllowsIt)
{
    MainWindow* const second = m_second;
    const DockHooks::Subscription allowing =
        DockHooks::subscribe({.dropIndicatorAllowed = [](const DockDrop&) { return true; }});
    const DockHooks::Subscription refusing = DockHooks::subscribe(
        {.dropIndicatorAllowed = [second](const DockDrop& drop) { return drop.mainWindow != second; }});

    const KDDockWidgets::Core::DockWidget::List none;
    KDDockWidgets::Core::DropArea* const firstArea = m_first->mainWindow()->dropArea();
    KDDockWidgets::Core::DropArea* const secondArea = m_second->mainWindow()->dropArea();
    EXPECT_TRUE(Config::self().dropIndicatorAllowedFunc()(KDDockWidgets::DropLocation_Left, none, none, firstArea));
    EXPECT_FALSE(Config::self().dropIndicatorAllowedFunc()(KDDockWidgets::DropLocation_Left, none, none, secondArea));
    EXPECT_TRUE(DockHooks::allowsDrop(DockDrop{.location = KDDockWidgets::DropLocation_Center, .mainWindow = nullptr}));
}

// The library tells the end of a drag with nothing: each subscriber is told what the drag the library last allowed
// named, once.
TEST_F(DockHooksTest, TheEndOfADragIsToldToEverySubscriberWithWhatTheDragNamed)
{
    std::vector<QString> ended;
    const auto note = [&ended](const DockDrag& drag) {
        ended.push_back((drag.mainWindow != nullptr ? drag.mainWindow->uniqueName() : QStringLiteral("none")) +
                        QLatin1Char('/') +
                        (drag.dockWidget != nullptr ? drag.dockWidget->uniqueName() : QStringLiteral("none")));
    };
    const DockHooks::Subscription one = DockHooks::subscribe({.dragEnded = note});
    const DockHooks::Subscription two = DockHooks::subscribe({.dragEnded = note});

    KDDockWidgets::Core::Draggable* const tab = tabOf(m_inSecond);
    ASSERT_NE(tab, nullptr);
    ASSERT_TRUE(Config::self().dragAboutToStartFunc()(tab));
    // The library asks at every move while the button is down; the end is told once.
    ASSERT_TRUE(Config::self().dragAboutToStartFunc()(tab));
    Config::self().dragEndedFunc()();

    EXPECT_EQ(ended, (std::vector<QString>{QStringLiteral("hooks.second/hooks.inSecond"),
                                           QStringLiteral("hooks.second/hooks.inSecond")}));
}

// A subscriber that leaves - its subscription reset or gone - is asked nothing more, and the others still are.
TEST_F(DockHooksTest, ASubscriberThatLeavesIsAskedNothingMore)
{
    int firstAsked = 0;
    int secondAsked = 0;
    DockHooks::Subscription first = DockHooks::subscribe({.dragAboutToStart = [&firstAsked](const DockDrag&) {
        ++firstAsked;
        return false;
    }});
    auto second = std::make_unique<DockHooks::Subscription>(
        DockHooks::subscribe({.dragAboutToStart = [&secondAsked](const DockDrag&) {
            ++secondAsked;
            return true;
        }}));

    EXPECT_FALSE(DockHooks::allowsDrag(DockDrag{.mainWindow = m_first}));
    EXPECT_EQ(firstAsked, 1);
    EXPECT_EQ(secondAsked, 1);

    first.reset();
    EXPECT_FALSE(first.active());
    EXPECT_TRUE(DockHooks::allowsDrag(DockDrag{.mainWindow = m_first}));
    EXPECT_EQ(firstAsked, 1);
    EXPECT_EQ(secondAsked, 2);

    second.reset();
    EXPECT_TRUE(DockHooks::allowsDrag(DockDrag{.mainWindow = m_first}));
    EXPECT_EQ(secondAsked, 2);
}

// The main window a widget is in is its first parent that is one, short of a window of its own: a floating window
// belongs to none, though the library parents it to a main window.
TEST_F(DockHooksTest, FindsTheMainWindowAWidgetIsInAndNoneForAFloatingWindow)
{
    EXPECT_EQ(DockHooks::mainWindowHolding(m_inFirst), m_first);
    EXPECT_EQ(DockHooks::mainWindowHolding(m_inSecond->widget()), m_second);
    ASSERT_TRUE(m_floating->isFloating());
    EXPECT_EQ(DockHooks::mainWindowHolding(m_floating), nullptr);
    EXPECT_EQ(DockHooks::mainWindowHolding(nullptr), nullptr);
}
