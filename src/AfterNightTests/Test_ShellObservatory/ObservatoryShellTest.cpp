// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory shell behind the shell contract, through its real window: where a view goes, how it is brought
// forward, maximized and restored, the dock area the core's operations act on, and the two places the window shows.

#include "ObservatoryWindowCase.h"

#include "pages/HomePage.h"
#include "pages/WorkspacePage.h"
#include "workspaces/WorkspaceHost.h"

#include "controls/ImageViewControl.h"
#include "core/ApplicationCore.h"
#include "core/IShell.h"
#include "views/DockHooks.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QCoreApplication>
#include <QStackedWidget>

#include <gtest/gtest.h>

namespace an::tests::observatory
{
namespace
{

using ObservatoryShell = ObservatoryWindowCase;

/// Whether a view is docked in the dock area: its dock widget is in the area's main window.
[[nodiscard]] bool dockedIn(ImageViewControl& view, KDDockWidgets::QtWidgets::MainWindow* area)
{
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    return dock != nullptr && area != nullptr && an::ui::shell::DockHooks::mainWindowHolding(dock) == area;
}

// The shell answers the contract through its window: the window it shows, no process presenter while every process
// is a window, a view placed in the workspace's dock area and the workspace shown, a view brought forward as a tab,
// a view maximized over the whole page and restored to its dock with the layout as it was, the dock area the core's
// operations act on, and Home and the views shown when asked.
TEST_F(ObservatoryShell, ImplementsTheContract)
{
    an::ui::observatory::ObservatoryShell& contract = shell();
    ASSERT_EQ(contract.mainWindow(), &window());
    EXPECT_EQ(contract.processPresenter(), nullptr);
    EXPECT_EQ(&contract.viewHost(), static_cast<an::ui::shell::IViewHost*>(&window()));
    EXPECT_EQ(&contract.statusDisplay(), static_cast<an::ui::shell::IStatusDisplay*>(&window()));
    EXPECT_EQ(core().shell(), &contract);

    // The core's operations act on the workspace's dock area, which names no workspace while it is the only one.
    const an::ui::shell::DockTarget target = contract.viewHost().dockTarget();
    EXPECT_EQ(target.mainWindow, window().workspaces().mainWindow());
    EXPECT_TRUE(target.workspaceId.isEmpty());

    // Place: the image a person opens is docked in the workspace, which is shown.
    ImageViewControl* const first = openView(QStringLiteral("contract-first"));
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(dockedIn(*first, target.mainWindow));
    EXPECT_TRUE(window().isWorkspaceShown());

    // A second as its tab: activating the first brings its tab forward.
    ImageViewControl* const second = openView(QStringLiteral("contract-second"));
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(dockedIn(*second, target.mainWindow));
    contract.viewHost().activate(*first);
    QCoreApplication::processEvents();
    ASSERT_NE(first->getDockWidget(), nullptr);
    EXPECT_TRUE(first->getDockWidget()->dockWidget()->isCurrentTab());
    EXPECT_TRUE(first->isVisible());

    // Maximize: the view covers the workspace's page, its dock keeps its place with a stand-in, and the layout is
    // locked while it is.
    ASSERT_FALSE(contract.viewHost().isMaximizedViewActive());
    first->switchMaximized();
    QCoreApplication::processEvents();
    EXPECT_TRUE(contract.viewHost().isMaximizedViewActive());
    EXPECT_EQ(window().workspacePage()->maximizedView(), first);
    EXPECT_TRUE(window().workspacePage()->isAncestorOf(first));
    EXPECT_NE(first->getDockWidget()->widget(), first);
    EXPECT_TRUE(core().dockLayout().isLocked(target));

    // Restore: the view is back in its dock and the layout as it was.
    first->switchMaximized();
    QCoreApplication::processEvents();
    EXPECT_FALSE(contract.viewHost().isMaximizedViewActive());
    EXPECT_EQ(window().workspacePage()->maximizedView(), nullptr);
    EXPECT_EQ(first->getDockWidget()->widget(), first);
    EXPECT_FALSE(core().dockLayout().isLocked(target));

    // Home and the views, when asked.
    contract.viewHost().showHome();
    EXPECT_TRUE(window().isHomeShown());
    contract.viewHost().showViews();
    EXPECT_TRUE(window().isWorkspaceShown());
}

// A layout the person locked stays locked when a maximized view is restored: the maximize locks only a layout that
// was free.
TEST_F(ObservatoryShell, ARestoreKeepsTheLockThePersonSet)
{
    ImageViewControl* const view = openView(QStringLiteral("restore-keeps-the-lock"));
    ASSERT_NE(view, nullptr);
    const an::ui::shell::DockTarget target = shell().viewHost().dockTarget();
    core().dockLayout().lock(target);
    view->switchMaximized();
    QCoreApplication::processEvents();
    view->switchMaximized();
    QCoreApplication::processEvents();
    EXPECT_TRUE(core().dockLayout().isLocked(target));
    core().dockLayout().unlock(target);
}

} // namespace
} // namespace an::tests::observatory
