// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Several main windows as the pages of one stack: what a workspace is. A dock widget is added to either, moves from
// one to the other by code and by a drag, and each main window holds only its own.

#include "DockingTestSupport.h"

#include <QStackedWidget>

namespace an::tests::docking
{

class SeveralMainWindows : public DockingTest
{
};

TEST_F(SeveralMainWindows, ADockWidgetIsAddedToEitherAndEachHoldsOnlyItsOwn)
{
    const auto workspaces = makeWorkspaceStack();
    auto* first = makeView(QStringLiteral("first"));
    auto* second = makeView(QStringLiteral("second"));
    auto* third = makeView(QStringLiteral("third"));

    workspaces.first->addDockWidget(first, KDDockWidgets::Location_OnLeft);
    // The second workspace is not on screen: a dock widget is added to it all the same.
    workspaces.second->addDockWidget(second, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(third, KDDockWidgets::Location_OnRight);
    settle();

    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("first")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("second"), QStringLiteral("third")}));
    EXPECT_EQ(mainWindowHolding(first), workspaces.first);
    EXPECT_EQ(mainWindowHolding(second), workspaces.second);
    EXPECT_EQ(mainWindowHolding(third), workspaces.second);

    EXPECT_TRUE(first->isVisible());
    EXPECT_FALSE(second->isVisible()) << "a workspace that is not on screen shows nothing of its own";

    workspaces.show(workspaces.second);
    EXPECT_FALSE(first->isVisible());
    EXPECT_TRUE(second->isVisible());
    EXPECT_TRUE(third->isVisible());
}

TEST_F(SeveralMainWindows, ADockWidgetMovesToTheOtherByCode)
{
    const auto workspaces = makeWorkspaceStack();
    auto* moved = makeView(QStringLiteral("moved"));
    auto* stays = makeView(QStringLiteral("stays"));
    auto* there = makeView(QStringLiteral("there"));
    workspaces.first->addDockWidget(moved, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(stays, KDDockWidgets::Location_OnRight);
    workspaces.second->addDockWidget(there, KDDockWidgets::Location_OnLeft);
    settle();

    // As a tab of a dock widget of the other workspace, with neither workspace change on screen.
    there->addDockWidgetAsTab(moved);
    settle();

    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("stays")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("moved"), QStringLiteral("there")}));
    EXPECT_EQ(mainWindowHolding(moved), workspaces.second);
    EXPECT_EQ(moved->group(), there->group()) << "the moved dock widget is a tab beside the one it was added to";

    // And back, beside the dock area's other dock widget.
    workspaces.first->addDockWidget(moved, KDDockWidgets::Location_OnBottom);
    settle();

    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("moved"), QStringLiteral("stays")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("there")}));
}

TEST_F(SeveralMainWindows, ADockWidgetMovesToTheOtherByADrag)
{
    const auto workspaces = makeWorkspaceStack();
    auto* moved = makeView(QStringLiteral("moved"));
    auto* stays = makeView(QStringLiteral("stays"));
    auto* there = makeView(QStringLiteral("there"));
    workspaces.first->addDockWidget(moved, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(stays, KDDockWidgets::Location_OnRight);
    workspaces.second->addDockWidget(there, KDDockWidgets::Location_OnLeft);
    settle();

    // Pulled out of the first workspace by its tab and released away from every window: it floats.
    auto pull = SimulatedDrag::ofTab(moved);
    pull.moveTo(QPoint(650, 650));
    pull.release();

    ASSERT_TRUE(moved->isFloating());
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("stays")}));

    // The other workspace is put on screen and the floating window is dropped beside its dock widget.
    workspaces.show(workspaces.second);
    auto drop = SimulatedDrag::ofFloatingWindow(moved);
    const QPoint overThere = there->mapToGlobal(there->rect().center());
    ASSERT_TRUE(drop.moveOnto(indicatorsOf(workspaces.second), overThere, KDDockWidgets::DropLocation_Left));
    drop.release();

    EXPECT_FALSE(moved->isFloating());
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("stays")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("moved"), QStringLiteral("there")}));
    EXPECT_LT(moved->mapToGlobal(QPoint(0, 0)).x(), there->mapToGlobal(QPoint(0, 0)).x())
        << "dropped on the left indicator, it stands to the left";
}

TEST_F(SeveralMainWindows, AFloatingWindowIsParentedToTheFirstMainWindowThatSharesItsAffinity)
{
    const auto workspaces = makeWorkspaceStack();
    auto* view = makeView(QStringLiteral("view"));
    auto* task = makeTask(QStringLiteral("task"));
    floatAt(view, QRect(520, 40, 240, 200));
    floatAt(task, QRect(520, 280, 240, 200));

    // With several main windows the library parents a floating window to the first one made that shares its
    // affinity, whichever is on screen: an image's window to the first workspace.
    EXPECT_EQ(windowOf(view)->parentWidget(), workspaces.first);

    // No workspace shares a task's affinity, so its window has no parent: nothing keeps it above the main window,
    // and nothing owns it.
    EXPECT_EQ(windowOf(task)->parentWidget(), nullptr);
}

TEST_F(SeveralMainWindows, AFloatingWindowIsDestroyedWithTheMainWindowItIsParentedTo)
{
    const auto workspaces = makeWorkspaceStack();
    auto* view = makeView(QStringLiteral("view"));
    floatAt(view, QRect(520, 40, 240, 200));
    ASSERT_EQ(windowOf(view)->parentWidget(), workspaces.first);
    const QPointer<DockWidget> guard(view);

    // Closing the first workspace: a floating image, which belongs to no workspace, goes with it all the same.
    delete workspaces.first;
    settle();

    EXPECT_TRUE(guard.isNull()) << "so floating windows are parented to a main window that outlives every workspace";
}

TEST_F(SeveralMainWindows, MainWindowsThatAreNeverShownParentTheFloatingWindowsAndTakeNoDrop)
{
    // Made before any workspace, inside the window that will hold them, and never shown: one with no affinity, for
    // the images' floating windows, and one with the tasks' affinity.
    auto* shell = own(new QWidget);
    shell->setGeometry(20, 400, 300, 200);
    auto* imageAnchor =
        own(new MainWindow(QStringLiteral("an.anchor.images"), KDDockWidgets::MainWindowOption_None, shell));
    auto* taskAnchor =
        own(new MainWindow(QStringLiteral("an.anchor.tasks"), KDDockWidgets::MainWindowOption_None, shell));
    taskAnchor->setAffinities({taskAffinity()});
    for (auto* anchor : {imageAnchor, taskAnchor})
    {
        anchor->setWindowFlags(Qt::Widget);
        anchor->hide();
    }
    shell->show();
    settle();
    ASSERT_FALSE(imageAnchor->isVisible());
    ASSERT_FALSE(taskAnchor->isVisible());

    const auto workspaces = makeWorkspaceStack();
    auto* docked = makeView(QStringLiteral("docked"));
    auto* view = makeView(QStringLiteral("view"));
    auto* task = makeTask(QStringLiteral("task"));
    workspaces.first->addDockWidget(docked, KDDockWidgets::Location_OnLeft);
    floatAt(view, QRect(520, 40, 240, 200));
    floatAt(task, QRect(520, 280, 240, 200));

    EXPECT_EQ(windowOf(view)->parentWidget(), imageAnchor);
    EXPECT_EQ(windowOf(task)->parentWidget(), taskAnchor);
    EXPECT_EQ(windowOf(view)->parentWidget()->window(), shell) << "so the window they are kept above is the shell's";
    EXPECT_TRUE(view->isVisible());
    EXPECT_TRUE(task->isVisible());

    // Neither is ever a drop target: over the window they are in, a dragged image is offered nothing.
    auto over = SimulatedDrag::ofFloatingWindow(view);
    over.moveTo(shell->mapToGlobal(shell->rect().center()));
    EXPECT_FALSE(indicatorsOf(imageAnchor)->isHovered());
    EXPECT_EQ(indicatorWindowsOnScreen(), 0);

    // And the workspaces take the drop as before.
    const QPoint overDocked = docked->mapToGlobal(docked->rect().center());
    ASSERT_TRUE(over.moveOnto(indicatorsOf(workspaces.first), overDocked, KDDockWidgets::DropLocation_Center));
    over.release();
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("docked"), QStringLiteral("view")}));
    EXPECT_TRUE(dockedIn(imageAnchor).isEmpty());
}

} // namespace an::tests::docking
