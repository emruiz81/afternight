// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// One saved layout for every workspace and every floating image window: what a project keeps of the arrangement. The
// dock widgets are destroyed and made again under the same names, as images closed and opened again are, and the
// layout puts each where it was; a dock widget that is not made again is skipped; a task's window is not in it.
//
// A task's window is kept out by the dock widget itself, made to be skipped by a saved layout. Limiting the saver to
// an affinity name does not do it: the library then writes the windows that carry no affinity and none of the dock
// widgets in them.

#include "DockingTestSupport.h"

#include <QStackedWidget>

#include <kddockwidgets/LayoutSaver.h>

namespace an::tests::docking
{

namespace
{

// Every window is in a saved layout but the ones whose dock widgets were made to be skipped by it: a task's.
QByteArray saveLayout()
{
    const KDDockWidgets::LayoutSaver saver;
    return saver.serializeLayout();
}

// Restored without moving or resizing the window the workspaces are in, and with floating windows where they were.
bool restoreLayout(const QByteArray& blob)
{
    KDDockWidgets::LayoutSaver saver(KDDockWidgets::RestoreOptions(
        KDDockWidgets::RestoreOption_RelativeToMainWindow | KDDockWidgets::RestoreOption_AbsoluteFloatingDockWindows));
    return saver.restoreLayout(blob);
}

QStringList sorted(const KDDockWidgets::Vector<QString>& names)
{
    QStringList list(names.cbegin(), names.cend());
    list.sort();
    return list;
}

} // namespace

class LayoutBlob : public DockingTest
{
protected:
    /// Destroys a case's dock widgets, as closing their images does.
    static void destroy(std::initializer_list<DockWidget*> dockWidgets)
    {
        for (auto* dockWidget : dockWidgets)
        {
            delete dockWidget;
        }
        settle();
    }
};

TEST_F(LayoutBlob, OneBlobHoldsTwoWorkspacesAndAFloatingWindowAndPutsEachDockWidgetBack)
{
    const auto workspaces = makeWorkspaceStack();
    auto* left = makeView(QStringLiteral("left"));
    auto* right = makeView(QStringLiteral("right"));
    auto* tabbed = makeView(QStringLiteral("tabbed"));
    auto* second = makeView(QStringLiteral("second"));
    auto* floating = makeView(QStringLiteral("floating"));
    workspaces.first->addDockWidget(left, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(right, KDDockWidgets::Location_OnRight);
    right->addDockWidgetAsTab(tabbed);
    workspaces.second->addDockWidget(second, KDDockWidgets::Location_OnLeft);
    const QRect floatingGeometry(520, 60, 250, 210);
    floatAt(floating, floatingGeometry);
    ASSERT_EQ(windowOf(floating)->geometry(), floatingGeometry);

    const QByteArray blob = saveLayout();
    ASSERT_FALSE(blob.isEmpty());
    for (const char* name : {"\"an.workspace.1\"", "\"an.workspace.2\"", "\"left\"", "\"right\"", "\"tabbed\"",
                             "\"second\"", "\"floating\""})
    {
        EXPECT_TRUE(blob.contains(name)) << name << " is in the one blob";
    }

    destroy({left, right, tabbed, second, floating});
    ASSERT_TRUE(dockedIn(workspaces.first).isEmpty());
    ASSERT_TRUE(dockedIn(workspaces.second).isEmpty());

    // Made again under the same names, and shown nowhere yet.
    left = makeView(QStringLiteral("left"));
    right = makeView(QStringLiteral("right"));
    tabbed = makeView(QStringLiteral("tabbed"));
    second = makeView(QStringLiteral("second"));
    floating = makeView(QStringLiteral("floating"));

    ASSERT_TRUE(restoreLayout(blob));
    settle();

    EXPECT_EQ(dockedIn(workspaces.first),
              QStringList({QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("tabbed")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("second")}));
    EXPECT_EQ(right->group(), tabbed->group());
    EXPECT_NE(left->group(), right->group());
    EXPECT_LT(placeIn(left, workspaces.first).x(), placeIn(right, workspaces.first).x());
    ASSERT_TRUE(floating->isFloating());
    EXPECT_TRUE(floating->isVisible());
    EXPECT_EQ(windowOf(floating)->geometry(), floatingGeometry);
}

TEST_F(LayoutBlob, TheLibrarysListOfOpenedDockWidgetsNamesOnlyThoseThatWereOnScreen)
{
    const auto workspaces = makeWorkspaceStack();
    auto* shown = makeView(QStringLiteral("shown"));
    auto* behind = makeView(QStringLiteral("behind"));
    auto* elsewhere = makeView(QStringLiteral("elsewhere"));
    workspaces.first->addDockWidget(behind, KDDockWidgets::Location_OnLeft);
    behind->addDockWidgetAsTab(shown);
    workspaces.second->addDockWidget(elsewhere, KDDockWidgets::Location_OnLeft);
    settle();
    ASSERT_TRUE(shown->isVisible());
    ASSERT_FALSE(behind->isVisible());
    ASSERT_FALSE(elsewhere->isVisible());

    const QByteArray blob = saveLayout();

    // A tab behind another and a view of a workspace that is not on screen are in the layout, and are put back by
    // it; the library's list of what a layout has open leaves them out. Which images were open is the project's to
    // record, not this list's to tell.
    EXPECT_EQ(sorted(KDDockWidgets::LayoutSaver::openedDockWidgetsInLayout(blob)),
              QStringList({QStringLiteral("shown")}));
    EXPECT_TRUE(blob.contains("\"behind\""));
    EXPECT_TRUE(blob.contains("\"elsewhere\""));
}

TEST_F(LayoutBlob, ADockWidgetThatDeletesItselfWhenClosedIsDestroyedByABlobAppliedWhileItIsShown)
{
    const auto workspaces = makeWorkspaceStack();
    auto* left = makeView(QStringLiteral("left"));
    auto* right = makeView(QStringLiteral("right"));
    workspaces.first->addDockWidget(left, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(right, KDDockWidgets::Location_OnRight);
    const QByteArray blob = saveLayout();
    const QPointer<DockWidget> leftGuard(left);
    const QPointer<DockWidget> rightGuard(right);

    // Applying a layout closes every dock widget it names before it places them. An image view deletes itself when
    // closed, so the layout places it and it is gone a moment later.
    ASSERT_TRUE(restoreLayout(blob));
    settle();

    EXPECT_TRUE(leftGuard.isNull());
    EXPECT_TRUE(rightGuard.isNull());
    EXPECT_TRUE(dockedIn(workspaces.first).isEmpty())
        << "so a layout is applied to image views that are not shown yet, or that are told not to delete themselves "
           "while it is applied";
}

TEST_F(LayoutBlob, DockWidgetsShownWhenTheBlobIsAppliedMoveToTheirPlacesWhileToldNotToDeleteThemselves)
{
    const auto workspaces = makeWorkspaceStack();
    auto* left = makeView(QStringLiteral("left"));
    auto* right = makeView(QStringLiteral("right"));
    auto* second = makeView(QStringLiteral("second"));
    workspaces.first->addDockWidget(left, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(right, KDDockWidgets::Location_OnRight);
    workspaces.second->addDockWidget(second, KDDockWidgets::Location_OnLeft);
    const QByteArray blob = saveLayout();

    // The arrangement changes: everything as tabs of the first workspace, as opening images one by one docks them.
    left->addDockWidgetAsTab(right);
    left->addDockWidgetAsTab(second);
    settle();
    ASSERT_TRUE(dockedIn(workspaces.second).isEmpty());
    const QPointer<DockWidget> leftGuard(left);
    const QPointer<DockWidget> rightGuard(right);
    const QPointer<DockWidget> secondGuard(second);

    for (auto* dockWidget : {left, right, second})
    {
        dockWidget->setOptions(dockWidget->options() &
                               ~KDDockWidgets::DockWidgetOptions(KDDockWidgets::DockWidgetOption_DeleteOnClose));
    }
    ASSERT_TRUE(restoreLayout(blob));
    for (auto* dockWidget : {left, right, second})
    {
        dockWidget->setOptions(dockWidget->options() | KDDockWidgets::DockWidgetOption_DeleteOnClose);
    }
    settle();

    ASSERT_TRUE(leftGuard && rightGuard && secondGuard);
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("left"), QStringLiteral("right")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("second")}));
    EXPECT_NE(left->group(), right->group());
    EXPECT_LT(placeIn(left, workspaces.first).x(), placeIn(right, workspaces.first).x());
}

TEST_F(LayoutBlob, ADockWidgetThatIsNotMadeAgainIsSkipped)
{
    const auto workspaces = makeWorkspaceStack();
    auto* left = makeView(QStringLiteral("left"));
    auto* missing = makeView(QStringLiteral("missing"));
    auto* second = makeView(QStringLiteral("second"));
    workspaces.first->addDockWidget(left, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(missing, KDDockWidgets::Location_OnRight);
    workspaces.second->addDockWidget(second, KDDockWidgets::Location_OnLeft);
    const QByteArray blob = saveLayout();

    destroy({left, missing, second});
    left = makeView(QStringLiteral("left"));
    second = makeView(QStringLiteral("second"));

    EXPECT_TRUE(restoreLayout(blob)) << "a layout that names an image that could not be opened is still applied";
    settle();

    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("left")}));
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("second")}));
    EXPECT_TRUE(left->isVisible());
    EXPECT_GT(left->width(), workspaces.first->width() / 2) << "the place of the missing dock widget is not kept empty";
}

TEST_F(LayoutBlob, ATaskWindowIsNotInTheBlobAndIsLeftAloneByIt)
{
    const auto workspaces = makeWorkspaceStack();
    auto* view = makeView(QStringLiteral("view"));
    auto* task = makeTask(QStringLiteral("task"));
    workspaces.first->addDockWidget(view, KDDockWidgets::Location_OnLeft);
    const QRect taskGeometry(520, 60, 260, 220);
    floatAt(task, taskGeometry);

    const QByteArray blob = saveLayout();

    EXPECT_EQ(sorted(KDDockWidgets::LayoutSaver::openedDockWidgetsInLayout(blob)),
              QStringList({QStringLiteral("view")}));
    EXPECT_FALSE(blob.contains("\"task\"")) << "nothing of the task's window is written";

    // The task's window is moved, and the layout is applied: the window stays where it now is.
    const QRect movedGeometry(500, 300, 260, 220);
    windowOf(task)->setGeometry(movedGeometry);
    settle();
    destroy({view});
    view = makeView(QStringLiteral("view"));

    ASSERT_TRUE(restoreLayout(blob));
    settle();

    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("view")}));
    ASSERT_TRUE(task->isFloating());
    EXPECT_TRUE(task->isVisible());
    EXPECT_EQ(windowOf(task)->geometry(), movedGeometry);
}

TEST_F(LayoutBlob, ASaverLimitedToAnAffinityNamePutsBackNoDockWidgetThatCarriesNone)
{
    const auto workspaces = makeWorkspaceStack();
    auto* view = makeView(QStringLiteral("view"));
    workspaces.first->addDockWidget(view, KDDockWidgets::Location_OnLeft);
    settle();

    // The saver's own rule takes a window with no affinity whatever names the saver is limited to; the rule a
    // group's dock widgets are written by does not. So the workspace is written, and written empty.
    const KDDockWidgets::Vector<QString> names = {QStringLiteral("an.layout")};
    KDDockWidgets::LayoutSaver saver;
    saver.setAffinityNames(names);
    const QByteArray blob = saver.serializeLayout();
    EXPECT_TRUE(blob.contains("\"an.workspace.1\""));

    destroy({view});
    view = makeView(QStringLiteral("view"));
    KDDockWidgets::LayoutSaver restorer(KDDockWidgets::RestoreOption_RelativeToMainWindow);
    restorer.setAffinityNames(names);
    ASSERT_TRUE(restorer.restoreLayout(blob));
    settle();

    EXPECT_TRUE(dockedIn(workspaces.first).isEmpty())
        << "which is why a task is kept out of a layout by its dock widget, not by limiting the saver to a name";
    EXPECT_FALSE(view->isOpen());
}

TEST_F(LayoutBlob, ApplyingTheBlobDoesNotMoveTheWindowTheWorkspacesAreIn)
{
    const auto workspaces = makeWorkspaceStack();
    auto* view = makeView(QStringLiteral("view"));
    workspaces.first->addDockWidget(view, KDDockWidgets::Location_OnLeft);
    const QByteArray blob = saveLayout();

    const QRect moved(60, 80, 520, 420);
    workspaces.window->setGeometry(moved);
    settle();
    destroy({view});
    view = makeView(QStringLiteral("view"));

    ASSERT_TRUE(restoreLayout(blob));
    settle();

    EXPECT_EQ(workspaces.window->geometry(), moved)
        << "the window's place is the window's own setting, not the layout's";
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("view")}));
}

TEST_F(LayoutBlob, WithoutThatOptionTheBlobPutsTheWindowTheWorkspacesAreInBackWhereItWas)
{
    const auto workspaces = makeWorkspaceStack();
    auto* view = makeView(QStringLiteral("view"));
    workspaces.first->addDockWidget(view, KDDockWidgets::Location_OnLeft);
    const QRect saved = workspaces.window->geometry();
    const QByteArray blob = saveLayout();

    workspaces.window->setGeometry(60, 80, 520, 420);
    settle();
    destroy({view});
    view = makeView(QStringLiteral("view"));

    // To the library a main window's window is the main window's own, a page of a stack or not.
    KDDockWidgets::LayoutSaver saver;
    ASSERT_TRUE(saver.restoreLayout(blob));
    settle();

    EXPECT_EQ(workspaces.window->geometry(), saved);
}

} // namespace an::tests::docking
