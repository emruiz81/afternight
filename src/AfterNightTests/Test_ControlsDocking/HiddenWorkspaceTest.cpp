// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Of two workspaces that are pages of one stack, the one that is not on screen is never where a dragged window lands:
// a floating image window docks into the workspace the user is looking at.

#include "DockingTestSupport.h"

#include <QStackedWidget>

namespace an::tests::docking
{

class HiddenWorkspace : public DockingTest
{
};

TEST_F(HiddenWorkspace, OnlyTheWorkspaceOnScreenShowsIndicatorsAndTakesTheDrop)
{
    const auto workspaces = makeWorkspaceStack();
    auto* here = makeView(QStringLiteral("here"));
    auto* there = makeView(QStringLiteral("there"));
    auto* floating = makeView(QStringLiteral("floating"));
    workspaces.first->addDockWidget(here, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(there, KDDockWidgets::Location_OnLeft);
    floatAt(floating, QRect(520, 40, 240, 200));

    auto drag = SimulatedDrag::ofFloatingWindow(floating);
    const QPoint overTheStack = workspaces.stack->mapToGlobal(workspaces.stack->rect().center());
    drag.moveTo(overTheStack);

    EXPECT_TRUE(indicatorsOf(workspaces.first)->isHovered());
    EXPECT_FALSE(indicatorsOf(workspaces.second)->isHovered())
        << "the workspace that is not on screen is under the cursor's coordinates, and is not a drop target";
    EXPECT_EQ(indicatorWindowsOnScreen(), 1);

    ASSERT_TRUE(drag.moveOnto(indicatorsOf(workspaces.first), overTheStack, KDDockWidgets::DropLocation_Center));
    drag.release();

    EXPECT_FALSE(floating->isFloating());
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("floating"), QStringLiteral("here")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("there")}));
    EXPECT_EQ(indicatorWindowsOnScreen(), 0);
}

TEST_F(HiddenWorkspace, TheDropFollowsTheWorkspaceThatIsPutOnScreen)
{
    const auto workspaces = makeWorkspaceStack();
    auto* here = makeView(QStringLiteral("here"));
    auto* there = makeView(QStringLiteral("there"));
    auto* floating = makeView(QStringLiteral("floating"));
    workspaces.first->addDockWidget(here, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(there, KDDockWidgets::Location_OnLeft);
    floatAt(floating, QRect(520, 40, 240, 200));

    workspaces.show(workspaces.second);

    auto drag = SimulatedDrag::ofFloatingWindow(floating);
    const QPoint overTheStack = workspaces.stack->mapToGlobal(workspaces.stack->rect().center());
    drag.moveTo(overTheStack);

    EXPECT_FALSE(indicatorsOf(workspaces.first)->isHovered());
    EXPECT_TRUE(indicatorsOf(workspaces.second)->isHovered());

    ASSERT_TRUE(drag.moveOnto(indicatorsOf(workspaces.second), overTheStack, KDDockWidgets::DropLocation_Center));
    drag.release();

    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("here")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("floating"), QStringLiteral("there")}));
}

TEST_F(HiddenWorkspace, AFloatingWindowStaysOnScreenWhicheverWorkspaceIsShown)
{
    const auto workspaces = makeWorkspaceStack();
    auto* here = makeView(QStringLiteral("here"));
    auto* floating = makeView(QStringLiteral("floating"));
    workspaces.first->addDockWidget(here, KDDockWidgets::Location_OnLeft);
    floatAt(floating, QRect(520, 40, 240, 200));
    ASSERT_TRUE(floating->isVisible());

    workspaces.show(workspaces.second);
    EXPECT_TRUE(floating->isVisible()) << "a floating image window belongs to no workspace";
    EXPECT_TRUE(windowOf(floating)->isVisible());

    workspaces.show(workspaces.first);
    EXPECT_TRUE(floating->isVisible());
}

} // namespace an::tests::docking
