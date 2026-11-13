// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The library's three hooks, each a plain function for the whole process: a drag can be refused by the main window
// its draggable belongs to, an indicator by the dock area it would drop into, and the end of a drag is told after a
// floating window's drag and after a tab's, with the cursor's place readable and the dock widgets already where the
// drag left them.

#include "DockingTestSupport.h"

#include <QCursor>
#include <QStackedWidget>

#include <kddockwidgets/core/Draggable_p.h>
#include <kddockwidgets/core/TabBar.h>
#include <kddockwidgets/core/View.h>

namespace an::tests::docking
{

namespace
{

// What the hooks, which can capture nothing, read and write.
struct HookState
{
    MainWindow* lockedWorkspace = nullptr;
    KDDockWidgets::Core::DropArea* refusedDropArea = nullptr;
    MainWindow* watchedWorkspace = nullptr;

    int dragsAsked = 0;
    int dragsRefused = 0;
    QStringList draggedDockWidgets;
    int indicatorsAsked = 0;
    int dragsEnded = 0;
    QPoint cursorAtTheEnd;
    QStringList dockedAtTheEnd;
};

HookState g_hooks;

bool refuseDragsOutOfTheLockedWorkspace(KDDockWidgets::Core::Draggable* draggable)
{
    ++g_hooks.dragsAsked;

    // A draggable names its dock widget when it carries one alone. A strip of several tabs carries none by that
    // name: the tab that was pressed is the strip's current one by the time the drag is asked about.
    const auto* dockWidget = draggable->singleDockWidget();
    if (!dockWidget)
    {
        if (const auto* tabBar = draggable->asView()->asTabBarController())
        {
            dockWidget = tabBar->currentDockWidget();
        }
    }
    const QString name = dockWidget ? dockWidget->uniqueName() : QStringLiteral("(several)");
    if (g_hooks.draggedDockWidgets.isEmpty() || g_hooks.draggedDockWidgets.constLast() != name)
    {
        g_hooks.draggedDockWidgets.append(name);
    }

    auto* workspace = mainWindowHolding(widgetOf(draggable->asView()));
    if (workspace && workspace == g_hooks.lockedWorkspace)
    {
        ++g_hooks.dragsRefused;
        return false;
    }
    return true;
}

bool refuseIndicatorsOfTheRefusedDropArea(KDDockWidgets::DropLocation, const KDDockWidgets::Core::DockWidget::List&,
                                          const KDDockWidgets::Core::DockWidget::List&,
                                          KDDockWidgets::Core::DropArea* dropArea)
{
    ++g_hooks.indicatorsAsked;
    return dropArea != g_hooks.refusedDropArea;
}

void noteTheEndOfADrag()
{
    ++g_hooks.dragsEnded;
    g_hooks.cursorAtTheEnd = QCursor::pos();
    if (g_hooks.watchedWorkspace)
    {
        g_hooks.dockedAtTheEnd = dockedIn(g_hooks.watchedWorkspace);
    }
}

} // namespace

class Hooks : public DockingTest
{
protected:
    void SetUp() override
    {
        DockingTest::SetUp();
        g_hooks = HookState();
    }

    void TearDown() override
    {
        g_hooks = HookState();
        DockingTest::TearDown();
    }
};

TEST_F(Hooks, ADragIsRefusedByTheMainWindowItsDraggableBelongsTo)
{
    const auto workspaces = makeWorkspaceStack();
    auto* locked = makeView(QStringLiteral("locked"));
    auto* beside = makeView(QStringLiteral("beside"));
    auto* free = makeView(QStringLiteral("free"));
    auto* other = makeView(QStringLiteral("other"));
    workspaces.first->addDockWidget(locked, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(beside, KDDockWidgets::Location_OnRight);
    workspaces.second->addDockWidget(free, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(other, KDDockWidgets::Location_OnRight);
    settle();

    g_hooks.lockedWorkspace = workspaces.first;
    KDDockWidgets::Config::self().setDragAboutToStartFunc(refuseDragsOutOfTheLockedWorkspace);

    auto refused = SimulatedDrag::ofTab(locked);
    refused.moveTo(QPoint(650, 650));
    refused.release();

    // The library asks again at every move for as long as the button is down, so the hook is a question, not an
    // event: it answers the same each time, and what it refuses stays where it was.
    const int refusals = g_hooks.dragsRefused;
    EXPECT_GE(refusals, 1);
    EXPECT_EQ(refusals, g_hooks.dragsAsked);
    EXPECT_FALSE(locked->isFloating());
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("beside"), QStringLiteral("locked")}));

    // The other workspace is not locked: the same pull takes a tab out of it.
    workspaces.show(workspaces.second);
    auto allowed = SimulatedDrag::ofTab(free);
    allowed.moveTo(QPoint(650, 650));
    allowed.release();

    EXPECT_EQ(g_hooks.dragsRefused, refusals);
    EXPECT_TRUE(free->isFloating());
    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("other")}));

    // A floating window belongs to no workspace, and is dragged whichever is locked.
    const int askedBefore = g_hooks.dragsAsked;
    auto floating = SimulatedDrag::ofFloatingWindow(free);
    floating.moveTo(QPoint(600, 600));
    floating.release();

    EXPECT_EQ(g_hooks.dragsAsked, askedBefore + 1);
    EXPECT_EQ(g_hooks.dragsRefused, refusals);
}

TEST_F(Hooks, TheDraggableNamesTheDockWidgetItCarries)
{
    const auto workspaces = makeWorkspaceStack();
    auto* tab = makeView(QStringLiteral("tab"));
    auto* beside = makeView(QStringLiteral("beside"));
    workspaces.first->addDockWidget(tab, KDDockWidgets::Location_OnLeft);
    tab->addDockWidgetAsTab(beside);
    tab->setAsCurrentTab();
    settle();

    KDDockWidgets::Config::self().setDragAboutToStartFunc(refuseDragsOutOfTheLockedWorkspace);

    // One of two tabs, pulled out of the strip.
    auto pull = SimulatedDrag::ofTab(tab);
    pull.moveTo(QPoint(650, 650));
    pull.release();
    ASSERT_TRUE(tab->isFloating());
    EXPECT_EQ(g_hooks.draggedDockWidgets, QStringList({QStringLiteral("tab")}));

    // The floating window it became.
    g_hooks.draggedDockWidgets.clear();
    auto floating = SimulatedDrag::ofFloatingWindow(tab);
    floating.moveTo(QPoint(600, 600));
    floating.release();

    EXPECT_EQ(g_hooks.draggedDockWidgets, QStringList({QStringLiteral("tab")}))
        << "the hook that is asked about a drag is where a dispatcher learns which dock widget is dragged: the hook "
           "that hears the drag end is told nothing";
}

TEST_F(Hooks, AnIndicatorIsRefusedByTheDockAreaItWouldDropInto)
{
    const auto workspaces = makeWorkspaceStack();
    auto* here = makeView(QStringLiteral("here"));
    auto* there = makeView(QStringLiteral("there"));
    auto* floating = makeView(QStringLiteral("floating"));
    workspaces.first->addDockWidget(here, KDDockWidgets::Location_OnLeft);
    workspaces.second->addDockWidget(there, KDDockWidgets::Location_OnLeft);
    floatAt(floating, QRect(520, 40, 240, 200));

    g_hooks.refusedDropArea = workspaces.first->mainWindow()->dropArea();
    KDDockWidgets::Config::self().setDropIndicatorAllowedFunc(refuseIndicatorsOfTheRefusedDropArea);

    const QPoint overTheStack = workspaces.stack->mapToGlobal(workspaces.stack->rect().center());
    auto refused = SimulatedDrag::ofFloatingWindow(floating);
    EXPECT_FALSE(refused.moveOnto(indicatorsOf(workspaces.first), overTheStack, KDDockWidgets::DropLocation_Center));
    EXPECT_GT(g_hooks.indicatorsAsked, 0);
    for (const auto location :
         {KDDockWidgets::DropLocation_Center, KDDockWidgets::DropLocation_Left, KDDockWidgets::DropLocation_Right,
          KDDockWidgets::DropLocation_Top, KDDockWidgets::DropLocation_Bottom, KDDockWidgets::DropLocation_OutterLeft,
          KDDockWidgets::DropLocation_OutterRight, KDDockWidgets::DropLocation_OutterTop,
          KDDockWidgets::DropLocation_OutterBottom})
    {
        EXPECT_FALSE(indicatorsOf(workspaces.first)->dropIndicatorVisible(location)) << "location " << location;
    }
    refused.release();

    EXPECT_TRUE(floating->isFloating());
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("here")}));

    // The other workspace's dock area is not refused.
    workspaces.show(workspaces.second);
    auto allowed = SimulatedDrag::ofFloatingWindow(floating);
    ASSERT_TRUE(allowed.moveOnto(indicatorsOf(workspaces.second), overTheStack, KDDockWidgets::DropLocation_Center));
    allowed.release();

    EXPECT_EQ(dockedIn(workspaces.second), QStringList({QStringLiteral("floating"), QStringLiteral("there")}));
}

TEST_F(Hooks, TheEndOfADragIsToldAfterAFloatingWindowsDragAndAfterATabs)
{
    const auto workspaces = makeWorkspaceStack();
    auto* docked = makeView(QStringLiteral("docked"));
    auto* tab = makeView(QStringLiteral("tab"));
    auto* floating = makeView(QStringLiteral("floating"));
    workspaces.first->addDockWidget(docked, KDDockWidgets::Location_OnLeft);
    workspaces.first->addDockWidget(tab, KDDockWidgets::Location_OnRight);
    floatAt(floating, QRect(520, 40, 240, 200));

    g_hooks.watchedWorkspace = workspaces.first;
    KDDockWidgets::Config::self().setDragEndedFunc(noteTheEndOfADrag);

    // A floating window, released over nothing.
    auto moved = SimulatedDrag::ofFloatingWindow(floating);
    moved.moveTo(QPoint(610, 620));
    EXPECT_EQ(g_hooks.dragsEnded, 0) << "nothing is told while the button is down";
    moved.release();

    EXPECT_EQ(g_hooks.dragsEnded, 1);
    EXPECT_EQ(g_hooks.cursorAtTheEnd, QPoint(610, 620));

    // A tab, pulled out and released over nothing.
    auto pulled = SimulatedDrag::ofTab(tab);
    pulled.moveTo(QPoint(640, 660));
    pulled.release();

    EXPECT_EQ(g_hooks.dragsEnded, 2);
    EXPECT_EQ(g_hooks.cursorAtTheEnd, QPoint(640, 660));
    EXPECT_EQ(g_hooks.dockedAtTheEnd, QStringList({QStringLiteral("docked")}))
        << "when the end is told, the tab has already left its workspace";

    // A drop: when the end is told, the dropped dock widget is already in its new place.
    auto dropped = SimulatedDrag::ofFloatingWindow(floating);
    const QPoint overDocked = docked->mapToGlobal(docked->rect().center());
    ASSERT_TRUE(dropped.moveOnto(indicatorsOf(workspaces.first), overDocked, KDDockWidgets::DropLocation_Center));
    dropped.release();

    EXPECT_EQ(g_hooks.dragsEnded, 3);
    EXPECT_EQ(g_hooks.dockedAtTheEnd, QStringList({QStringLiteral("docked"), QStringLiteral("floating")}));
}

TEST_F(Hooks, ARefusedDragTellsNoEnd)
{
    const auto workspaces = makeWorkspaceStack();
    auto* locked = makeView(QStringLiteral("locked"));
    workspaces.first->addDockWidget(locked, KDDockWidgets::Location_OnLeft);
    settle();

    g_hooks.lockedWorkspace = workspaces.first;
    KDDockWidgets::Config::self().setDragAboutToStartFunc(refuseDragsOutOfTheLockedWorkspace);
    KDDockWidgets::Config::self().setDragEndedFunc(noteTheEndOfADrag);

    auto refused = SimulatedDrag::ofTab(locked);
    refused.moveTo(QPoint(650, 650));
    refused.release();

    EXPECT_GE(g_hooks.dragsRefused, 1);
    EXPECT_EQ(g_hooks.dragsEnded, 0);
}

} // namespace an::tests::docking
