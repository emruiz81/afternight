// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Affinities: a task's dock widget carries one that no workspace and no image view carries, so a task never lands in
// a workspace or in an image's floating window and no image lands in a task's window; image views, which carry none,
// dock into every workspace and into each other's floating windows.

#include "DockingTestSupport.h"

#include <QStackedWidget>

namespace an::tests::docking
{

class Affinities : public DockingTest
{
};

TEST_F(Affinities, ATaskIsRefusedByAWorkspaceAndByAViewInCode)
{
    const auto workspaces = makeWorkspaceStack();
    auto* view = makeView(QStringLiteral("view"));
    auto* task = makeTask(QStringLiteral("task"));
    workspaces.first->addDockWidget(view, KDDockWidgets::Location_OnLeft);

    workspaces.first->addDockWidget(task, KDDockWidgets::Location_OnRight);
    workspaces.first->addDockWidgetAsTab(task);
    view->addDockWidgetAsTab(task);
    settle();

    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("view")}));
    EXPECT_EQ(task->dockWidget()->mainWindow(), nullptr);
    EXPECT_NE(task->group(), view->group());

    // Nor is a view taken by a task's window.
    floatAt(task, QRect(520, 40, 240, 200));
    auto* other = makeView(QStringLiteral("other"));
    task->addDockWidgetAsTab(other);
    settle();

    EXPECT_NE(other->group(), task->group());
    ASSERT_NE(task->dockWidget()->floatingWindow(), nullptr);
    EXPECT_EQ(task->dockWidget()->floatingWindow()->dockWidgets().size(), 1);
}

TEST_F(Affinities, ATaskDraggedOverAWorkspaceShowsNoIndicatorAndStaysAWindow)
{
    const auto workspaces = makeWorkspaceStack();
    auto* view = makeView(QStringLiteral("view"));
    auto* task = makeTask(QStringLiteral("task"));
    workspaces.first->addDockWidget(view, KDDockWidgets::Location_OnLeft);
    floatAt(task, QRect(520, 40, 240, 200));

    auto drag = SimulatedDrag::ofFloatingWindow(task);
    const QPoint overTheStack = workspaces.stack->mapToGlobal(workspaces.stack->rect().center());
    drag.moveTo(overTheStack);

    EXPECT_FALSE(indicatorsOf(workspaces.first)->isHovered());
    EXPECT_EQ(indicatorWindowsOnScreen(), 0);
    EXPECT_EQ(indicatorsOf(workspaces.first)->currentDropLocation(), KDDockWidgets::DropLocation_None);

    drag.release();

    EXPECT_TRUE(task->isFloating());
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("view")}));
}

TEST_F(Affinities, AViewIsNeverOfferedATasksWindow)
{
    auto* view = makeView(QStringLiteral("view"));
    auto* task = makeTask(QStringLiteral("task"));
    floatAt(task, QRect(40, 40, 300, 260));
    floatAt(view, QRect(420, 40, 240, 200));

    auto drag = SimulatedDrag::ofFloatingWindow(view);
    drag.moveTo(task->mapToGlobal(task->rect().center()));

    EXPECT_FALSE(indicatorsOfWindowOf(task)->isHovered());
    EXPECT_EQ(indicatorWindowsOnScreen(), 0);

    drag.release();

    EXPECT_TRUE(view->isFloating());
    EXPECT_NE(windowOf(view), windowOf(task));
    EXPECT_EQ(task->dockWidget()->floatingWindow()->dockWidgets().size(), 1);
}

TEST_F(Affinities, ATaskIsNeverOfferedAViewsWindow)
{
    auto* view = makeView(QStringLiteral("view"));
    auto* task = makeTask(QStringLiteral("task"));
    floatAt(view, QRect(40, 40, 300, 260));
    floatAt(task, QRect(420, 40, 240, 200));

    auto drag = SimulatedDrag::ofFloatingWindow(task);
    drag.moveTo(view->mapToGlobal(view->rect().center()));

    EXPECT_FALSE(indicatorsOfWindowOf(view)->isHovered());
    EXPECT_EQ(indicatorWindowsOnScreen(), 0);

    drag.release();

    EXPECT_TRUE(task->isFloating());
    EXPECT_NE(windowOf(view), windowOf(task));
    EXPECT_EQ(view->dockWidget()->floatingWindow()->dockWidgets().size(), 1);
}

TEST_F(Affinities, AViewDocksIntoAnotherViewsFloatingWindow)
{
    auto* target = makeView(QStringLiteral("target"));
    auto* dragged = makeView(QStringLiteral("dragged"));
    floatAt(target, QRect(40, 40, 320, 280));
    floatAt(dragged, QRect(420, 40, 240, 200));

    auto drag = SimulatedDrag::ofFloatingWindow(dragged);
    const QPoint overTarget = target->mapToGlobal(target->rect().center());
    ASSERT_TRUE(drag.moveOnto(indicatorsOfWindowOf(target), overTarget, KDDockWidgets::DropLocation_Center));
    drag.release();

    EXPECT_EQ(windowOf(dragged), windowOf(target));
    EXPECT_EQ(dragged->group(), target->group());
}

TEST_F(Affinities, AViewDocksIntoEveryWorkspace)
{
    const auto workspaces = makeWorkspaceStack();
    const QPoint overTheStack = workspaces.stack->mapToGlobal(workspaces.stack->rect().center());

    int number = 0;
    for (auto* workspace : {workspaces.first, workspaces.second})
    {
        workspaces.show(workspace);
        auto* view = makeView(QStringLiteral("view%1").arg(++number));
        floatAt(view, QRect(520, 40, 240, 200));

        // An empty dock area offers its edges; any of them takes the whole area.
        auto drag = SimulatedDrag::ofFloatingWindow(view);
        ASSERT_TRUE(drag.moveOnto(indicatorsOf(workspace), overTheStack, KDDockWidgets::DropLocation_OutterLeft))
            << "workspace " << number;
        drag.release();

        EXPECT_EQ(dockedIn(workspace), QStringList({view->uniqueName()}));
    }
}

} // namespace an::tests::docking
