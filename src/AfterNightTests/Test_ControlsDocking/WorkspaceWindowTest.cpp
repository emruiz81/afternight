// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A workspace taken out of the stack into a window of its own: it keeps its layout and its dock widgets, takes drops
// there, and keeps both when it is put back.

#include "DockingTestSupport.h"

#include <QStackedWidget>

namespace an::tests::docking
{

namespace
{

/// Where each dock widget of a workspace stands inside it, by name.
QStringList layoutOf(const MainWindow* workspace, const QList<DockWidget*>& dockWidgets)
{
    QStringList places;
    for (const auto* dockWidget : dockWidgets)
    {
        const QRect place(placeIn(dockWidget, workspace), dockWidget->size());
        places.append(QStringLiteral("%1 at %2,%3 %4x%5")
                          .arg(dockWidget->uniqueName())
                          .arg(place.x())
                          .arg(place.y())
                          .arg(place.width())
                          .arg(place.height()));
    }
    return places;
}

} // namespace

class WorkspaceWindow : public DockingTest
{
protected:
    /// Takes a workspace out of the stack and shows it as a window, at the size it has as a page.
    static void moveToItsOwnWindow(const WorkspaceStack& workspaces, MainWindow* workspace, QPoint topLeft)
    {
        // A page that has not been on screen yet has no size of its own: the stack's is the size it would have.
        const QSize size = workspaces.stack->size();
        workspaces.stack->removeWidget(workspace);
        workspace->setParent(nullptr);
        workspace->setGeometry(QRect(topLeft, size));
        workspace->show();
        settle();
    }

    static void putBack(const WorkspaceStack& workspaces, MainWindow* workspace)
    {
        workspaces.stack->addWidget(workspace);
        workspaces.show(workspace);
    }
};

TEST_F(WorkspaceWindow, AWorkspaceTakenOutOfTheStackKeepsItsLayoutAndItsDockWidgets)
{
    const auto workspaces = makeWorkspaceStack();
    auto* left = makeView(QStringLiteral("left"));
    auto* right = makeView(QStringLiteral("right"));
    auto* tabbed = makeView(QStringLiteral("tabbed"));
    auto* other = makeView(QStringLiteral("other"));
    workspaces.first->addDockWidget(other, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(left, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(right, KDDockWidgets::Location_OnRight);
    right->addDockWidgetAsTab(tabbed);
    right->setAsCurrentTab();
    workspaces.show(workspaces.second);
    const QStringList before = layoutOf(workspaces.second, {left, right});
    ASSERT_EQ(before.size(), 2);

    workspaces.show(workspaces.first);
    moveToItsOwnWindow(workspaces, workspaces.second, QPoint(20, 400));

    EXPECT_TRUE(workspaces.second->isWindow());
    EXPECT_TRUE(workspaces.second->isVisible());
    EXPECT_EQ(workspaces.stack->count(), 1);
    EXPECT_EQ(dockedIn(workspaces.second),
              QStringList({QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("tabbed")}));
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("other")}));
    EXPECT_EQ(layoutOf(workspaces.second, {left, right}), before);
    EXPECT_EQ(right->group(), tabbed->group());
    EXPECT_TRUE(left->isVisible());
    EXPECT_TRUE(other->isVisible()) << "the workspace left in the stack is on screen beside the one in its window";
}

TEST_F(WorkspaceWindow, AWorkspaceInItsOwnWindowTakesDropsAndSoDoesTheOneLeftInTheStack)
{
    const auto workspaces = makeWorkspaceStack();
    auto* inStack = makeView(QStringLiteral("inStack"));
    auto* inWindow = makeView(QStringLiteral("inWindow"));
    auto* first = makeView(QStringLiteral("first"));
    auto* second = makeView(QStringLiteral("second"));
    workspaces.first->addDockWidget(inStack, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(inWindow, KDDockWidgets::Location_OnLeft);
    moveToItsOwnWindow(workspaces, workspaces.second, QPoint(20, 400));
    floatAt(first, QRect(520, 40, 240, 160));
    floatAt(second, QRect(520, 240, 240, 160));

    auto intoTheWindow = SimulatedDrag::ofFloatingWindow(first);
    const QPoint overTheWindow = workspaces.second->mapToGlobal(workspaces.second->rect().center());
    ASSERT_TRUE(
        intoTheWindow.moveOnto(indicatorsOf(workspaces.second), overTheWindow, KDDockWidgets::DropLocation_Right));
    EXPECT_FALSE(indicatorsOf(workspaces.first)->isHovered());
    intoTheWindow.release();

    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("first"), QStringLiteral("inWindow")}));
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("inStack")}));

    auto intoTheStack = SimulatedDrag::ofFloatingWindow(second);
    const QPoint overTheStack = workspaces.stack->mapToGlobal(workspaces.stack->rect().center());
    ASSERT_TRUE(
        intoTheStack.moveOnto(indicatorsOf(workspaces.first), overTheStack, KDDockWidgets::DropLocation_Center));
    EXPECT_FALSE(indicatorsOf(workspaces.second)->isHovered());
    intoTheStack.release();

    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("inStack"), QStringLiteral("second")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("first"), QStringLiteral("inWindow")}));
}

TEST_F(WorkspaceWindow, AWorkspacePutBackInTheStackKeepsItsLayoutAndItsDockWidgets)
{
    const auto workspaces = makeWorkspaceStack();
    auto* left = makeView(QStringLiteral("left"));
    auto* right = makeView(QStringLiteral("right"));
    auto* dropped = makeView(QStringLiteral("dropped"));
    workspaces.second->addDockWidget(left, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(right, KDDockWidgets::Location_OnRight);
    moveToItsOwnWindow(workspaces, workspaces.second, QPoint(20, 400));

    // Its layout changes while it is a window: what is put back is the layout it has then.
    floatAt(dropped, QRect(520, 40, 240, 160));
    auto drag = SimulatedDrag::ofFloatingWindow(dropped);
    const QPoint overRight = right->mapToGlobal(right->rect().center());
    ASSERT_TRUE(drag.moveOnto(indicatorsOf(workspaces.second), overRight, KDDockWidgets::DropLocation_Bottom));
    drag.release();
    const QStringList inItsWindow = layoutOf(workspaces.second, {left, right, dropped});

    putBack(workspaces, workspaces.second);

    EXPECT_FALSE(workspaces.second->isWindow());
    EXPECT_EQ(workspaces.stack->count(), 2);
    EXPECT_EQ(workspaces.stack->currentWidget(), workspaces.second);
    EXPECT_EQ(dockedIn(workspaces.second),
              QStringList({QStringLiteral("dropped"), QStringLiteral("left"), QStringLiteral("right")}));
    EXPECT_EQ(layoutOf(workspaces.second, {left, right, dropped}), inItsWindow);

    // And it is a drop target again as a page.
    auto* again = makeView(QStringLiteral("again"));
    floatAt(again, QRect(520, 240, 240, 160));
    auto back = SimulatedDrag::ofFloatingWindow(again);
    const QPoint overLeft = left->mapToGlobal(left->rect().center());
    ASSERT_TRUE(back.moveOnto(indicatorsOf(workspaces.second), overLeft, KDDockWidgets::DropLocation_Center));
    back.release();
    EXPECT_EQ(again->group(), left->group());
}

} // namespace an::tests::docking
