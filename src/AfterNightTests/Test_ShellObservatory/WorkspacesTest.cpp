// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Several workspaces as a person uses them: a second one made and shown in the rail with the digits, a view moved
// between them by its tab's menu, an emptied one closed once it is left, the floating images and Home, a lock and the
// layout's operations of one workspace that leave the other as it is, a workspace moved to a window of its own and
// back, closing a workspace and its unsaved images, the name it is given, where a new view lands, and the dock area of
// an empty workspace shown for a drag.

#include "ObservatoryWindowCase.h"

#include "ShellActions.h"
#include "chrome/MenuBarOnAlt.h"
#include "chrome/TopBar.h"
#include "pages/FloatingImagesPage.h"
#include "pages/WorkspacePage.h"
#include "workspaces/RenameWorkspaceDialog.h"
#include "workspaces/WorkspaceHost.h"
#include "workspaces/WorkspaceToolbar.h"
#include "workspaces/WorkspaceWindow.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "commands/CommandIndex.h"
#include "components/AnBreadcrumb.h"
#include "components/AnFloatingToolbar.h"
#include "components/AnIconRail.h"
#include "components/RailItem.h"
#include "controls/ImageViewControl.h"
#include "controls/KDDockWidgetsHelper.h"
#include "core/ApplicationCore.h"
#include "views/DockHooks.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"
#include "views/WorkspaceModel.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using Window = an::ui::observatory::ObservatoryWindow;
namespace ids = an::ui::shell::ids;
namespace shellIds = an::ui::observatory::ids;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The item the rail shows under an id; a default item when it shows none.
[[nodiscard]] an::ui::RailItem railItem(const an::ui::AnIconRail& rail, const QString& id)
{
    const std::vector<an::ui::RailItem>& items = rail.items();
    const auto found =
        std::find_if(items.begin(), items.end(), [&id](const an::ui::RailItem& item) { return item.id == id; });
    return found != items.end() ? *found : an::ui::RailItem{};
}

/// The action of a menu by its text, looking into its submenus; null for none.
[[nodiscard]] QAction* entryOf(const QMenu& menu, const QString& text)
{
    for (QAction* action : menu.actions())
    {
        if (action->text() == text)
        {
            return action;
        }
    }
    return nullptr;
}

/// The windows' workspaces, each case ending with the first alone, attached and free.
class Workspaces : public ObservatoryWindowCase
{
protected:
    void TearDown() override
    {
        for (const QString& id : host().ids())
        {
            if (an::ui::observatory::WorkspaceWindow* const holder = window().workspaceWindow(id))
            {
                answerTheWindow(*holder, "returnWorkspaceButton");
            }
        }
        for (const QString& id : host().ids())
        {
            core().dockLayout().unlock(host().target(id));
            (void)host().model().setLocked(id, false);
        }
        ObservatoryWindowCase::TearDown();
        settle();
        EXPECT_EQ(host().model().count(), 1u) << "a case left more than one workspace";
    }

    [[nodiscard]] static an::ui::observatory::WorkspaceHost& host()
    {
        return window().workspaces();
    }

    /// Runs one of the shell's own actions, as its menu entry does.
    static void runShellAction(const std::string_view id)
    {
        QAction* const action = window().shellActions().action(id);
        ASSERT_NE(action, nullptr);
        ASSERT_TRUE(action->isEnabled()) << std::string(id) << " is not offered";
        action->trigger();
        settle();
    }

    /// Runs one of the catalogue's actions, as its menu entry does.
    static void runCatalogueAction(const std::string_view id)
    {
        QAction* const action = core().actions().action(id);
        ASSERT_NE(action, nullptr);
        action->trigger();
        settle();
    }

    /// Makes a second workspace as New Workspace does, and answers its id.
    [[nodiscard]] static QString newWorkspace()
    {
        const QStringList before = host().ids();
        runShellAction(shellIds::kNewWorkspace);
        for (const QString& id : host().ids())
        {
            if (!before.contains(id))
            {
                return id;
            }
        }
        ADD_FAILURE() << "New Workspace made no workspace";
        return {};
    }

    /// Shows a workspace as a click on its rail icon does.
    static void clickWorkspace(const QString& id)
    {
        an::ui::AnIconRail& rail = *window().rail();
        const QRect place = rail.itemRect(Window::railWorkspaceId(id));
        ASSERT_TRUE(place.isValid()) << "the rail shows no icon of workspace " << id.toStdString();
        QTest::mouseClick(&rail, Qt::LeftButton, Qt::NoModifier, place.center());
        settle();
    }

    /// Moves a view to another workspace by its tab's menu.
    static void moveByTheTabMenu(ImageViewControl& view, const QString& workspaceName)
    {
        QMenu menu;
        window().buildTabMenu(menu, view);
        QMenu* const move = menu.findChild<QMenu*>(QStringLiteral("menuMoveToWorkspace"));
        ASSERT_NE(move, nullptr) << "the tab's menu has no Move to Workspace";
        QAction* const entry = entryOf(*move, workspaceName);
        ASSERT_NE(entry, nullptr) << "Move to Workspace does not list " << workspaceName.toStdString();
        entry->trigger();
        settle();
    }

    /// Closes a workspace's window as a person does and answers its question with the button of that name.
    static void answerTheWindow(an::ui::observatory::WorkspaceWindow& holder, const char* button)
    {
        holder.close();
        settle();
        QMessageBox* const question = holder.question();
        ASSERT_NE(question, nullptr) << "closing the workspace's window asked nothing";
        auto* const answer = question->findChild<QPushButton*>(QString::fromLatin1(button));
        ASSERT_NE(answer, nullptr);
        answer->click();
        settle();
    }

    /// A view of a new image docked beside `neighbour`, as a process's result is.
    static ImageViewControl* openBeside(const QString& name, ImageViewControl* neighbour)
    {
        std::optional<an::ui::ImageDocument> document = an::tests::app::filledDocument(session(), name, 0.25f);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        ImageViewControl* const view = core().viewService().userViews().createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, name.toStdString(), ViewControlMode::standard, neighbour);
        settle();
        return view;
    }

    [[nodiscard]] static std::vector<ImageViewControl*> dockedIn(const QString& id)
    {
        return host().dockedViews(id, core().viewService().getListOfViewControls());
    }

    [[nodiscard]] static KDDockWidgets::Core::Group* groupOf(ImageViewControl* view)
    {
        return view->getDockWidget()->group();
    }
};

// New Workspace adds "Workspace 2" and shows it, empty, with its quiet page; the rail lists both, each with its digit,
// the new one marked; the Window menu holds the workspace's entries and the palette finds New Workspace.
TEST_F(Workspaces, ASecondWorkspaceAppearsInTheRailWithBadges)
{
    ImageViewControl* const view = openView(QStringLiteral("workspaces-badges"));
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(host().model().count(), 1u);
    const QString first = host().ids().front();
    EXPECT_TRUE(railItem(*window().rail(), Window::railWorkspaceId(first)).badgeText.isEmpty())
        << "one workspace carries no digit";

    const QString second = newWorkspace();
    ASSERT_FALSE(second.isEmpty());
    EXPECT_EQ(host().model().count(), 2u);
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_EQ(host().shown(), second);
    EXPECT_EQ(host().current(), second);
    const an::ui::RailItem firstIcon = railItem(*window().rail(), Window::railWorkspaceId(first));
    const an::ui::RailItem secondIcon = railItem(*window().rail(), Window::railWorkspaceId(second));
    EXPECT_EQ(firstIcon.badgeText, QStringLiteral("1"));
    EXPECT_EQ(secondIcon.badgeText, QStringLiteral("2"));
    EXPECT_EQ(secondIcon.text, host().workspaceName(second));
    EXPECT_TRUE(secondIcon.text.startsWith(QStringLiteral("Workspace ")));
    EXPECT_EQ(secondIcon.section, an::ui::RailSection::Place);
    EXPECT_TRUE(secondIcon.selected);
    EXPECT_FALSE(firstIcon.selected);
    an::ui::observatory::WorkspacePage* const page = host().page(second);
    ASSERT_NE(page, nullptr);
    EXPECT_TRUE(page->isFloatingImagesShown());
    EXPECT_EQ(page->floatingImages()->title(), QStringLiteral("This workspace is empty"));
    EXPECT_EQ(page->floatingImages()->hint(), QStringLiteral("or drag an image here"));
    EXPECT_FALSE(page->floatingImages()->dockAllButton()->isVisibleTo(page->floatingImages()));
    EXPECT_EQ(window().topBar()->breadcrumb()->parts(), QStringList{host().workspaceName(second)});
    EXPECT_EQ(window().workspacePage(), page);

    QMenu* const windowMenu = window().menuBarOnAlt().menu("menuWindow");
    ASSERT_NE(windowMenu, nullptr);
    for (const std::string_view id : {shellIds::kNewWorkspace, shellIds::kRenameWorkspace,
                                      shellIds::kMoveWorkspaceToNewWindow, shellIds::kCloseWorkspace})
    {
        EXPECT_TRUE(windowMenu->actions().contains(window().shellActions().action(id)))
            << "the Window menu lacks " << std::string(id);
    }
    core().commands().refresh();
    const QString newEntry =
        QStringLiteral("action:") +
        QString::fromLatin1(shellIds::kNewWorkspace.data(), static_cast<qsizetype>(shellIds::kNewWorkspace.size()));
    EXPECT_NE(core().commands().entry(newEntry), nullptr) << "the palette offers no New Workspace";
}

// A view moved by its tab's menu goes to the workspace named there - a new one, at first - which is shown; each dock
// area holds its own. Move to Workspace follows Float and lists the other workspaces, a separator and New Workspace.
TEST_F(Workspaces, AViewMovesByTheTabMenuAndEachDockAreaListsItsOwn)
{
    ImageViewControl* const kept = openView(QStringLiteral("workspaces-kept"));
    ImageViewControl* const moved = openView(QStringLiteral("workspaces-moved"));
    ASSERT_NE(moved, nullptr);
    const QString first = host().ids().front();
    ASSERT_EQ(dockedIn(first).size(), 2u);
    const QStringList before = host().ids();

    moveByTheTabMenu(*moved, QStringLiteral("New Workspace"));
    ASSERT_EQ(host().model().count(), 2u);
    QString second;
    for (const QString& id : host().ids())
    {
        if (!before.contains(id))
        {
            second = id;
        }
    }
    EXPECT_EQ(host().workspaceOf(*moved), second);
    EXPECT_EQ(host().workspaceOf(*kept), first);
    EXPECT_EQ(dockedIn(first), std::vector<ImageViewControl*>{kept});
    EXPECT_EQ(dockedIn(second), std::vector<ImageViewControl*>{moved});
    EXPECT_EQ(host().shown(), second) << "the person follows the view";
    EXPECT_TRUE(host().mainWindow(second)->isAncestorOf(moved->getDockWidget()));
    EXPECT_FALSE(host().mainWindow(first)->isAncestorOf(moved->getDockWidget()));

    // Back on the first workspace, the other workspace holds its view and stays.
    clickWorkspace(first);
    ASSERT_EQ(host().shown(), first);
    EXPECT_EQ(host().model().count(), 2u);
    QMenu menu;
    window().buildTabMenu(menu, *kept);
    const QList<QAction*> entries = menu.actions();
    qsizetype floatIndex = -1;
    for (qsizetype index = 0; index < entries.size(); ++index)
    {
        if (entries[index]->text() == QStringLiteral("Float"))
        {
            floatIndex = index;
        }
    }
    ASSERT_GE(floatIndex, 0);
    ASSERT_LT(floatIndex + 1, entries.size());
    QMenu* const move = entries[floatIndex + 1]->menu();
    ASSERT_NE(move, nullptr) << "Move to Workspace does not follow Float";
    EXPECT_EQ(entries[floatIndex + 1]->text(), QStringLiteral("Move to Workspace"));
    QStringList listed;
    for (const QAction* action : move->actions())
    {
        listed.append(action->isSeparator() ? QStringLiteral("-") : action->text());
    }
    EXPECT_EQ(listed,
              (QStringList{host().workspaceName(second), QStringLiteral("-"), QStringLiteral("New Workspace")}));
}

// A workspace emptied of its views closes once the person leaves it, and its icon goes; so does an empty one left for
// Home. The only workspace stays.
TEST_F(Workspaces, AnEmptiedSecondWorkspaceClosesWhenLeftAndItsIconGoes)
{
    ImageViewControl* const kept = openView(QStringLiteral("workspaces-stay"));
    ImageViewControl* const moved = openView(QStringLiteral("workspaces-goes-and-comes"));
    ASSERT_NE(moved, nullptr);
    const QString first = host().ids().front();
    moveByTheTabMenu(*moved, QStringLiteral("New Workspace"));
    ASSERT_EQ(host().model().count(), 2u);
    const QString second = host().workspaceOf(*moved);
    ASSERT_NE(second, first);
    ASSERT_EQ(host().shown(), second);

    moveByTheTabMenu(*moved, host().workspaceName(first));
    EXPECT_EQ(host().shown(), first);
    EXPECT_EQ(host().model().count(), 1u);
    EXPECT_EQ(host().model().find(second), nullptr);
    EXPECT_FALSE(window().rail()->itemRect(Window::railWorkspaceId(second)).isValid()) << "its icon stays";
    EXPECT_TRUE(railItem(*window().rail(), Window::railWorkspaceId(first)).badgeText.isEmpty());
    EXPECT_EQ(dockedIn(first).size(), 2u);
    EXPECT_NE(kept, nullptr);

    // An empty workspace left for Home closes too; the only one stays.
    const QString third = newWorkspace();
    ASSERT_EQ(host().model().count(), 2u);
    window().showHome();
    settle();
    EXPECT_EQ(host().model().count(), 1u);
    EXPECT_EQ(host().model().find(third), nullptr);
    window().showHome();
    settle();
    EXPECT_EQ(host().model().count(), 1u);
}

// With every image floating the only workspace lists them; a second workspace, empty beside one that holds a view, says
// it is empty instead.
TEST_F(Workspaces, TheOnlyWorkspaceWithEveryImageFloatingShowsTheFloatingPage)
{
    ImageViewControl* const view = openView(QStringLiteral("workspaces-floating"));
    ASSERT_NE(view, nullptr);
    const QString first = host().ids().front();
    view->getDockWidget()->setFloating(true);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() {
        settle();
        return host().page(first)->isFloatingImagesShown();
    })) << "every image floats, and the only workspace does not list them";
    EXPECT_EQ(host().page(first)->floatingImages()->title(),
              QStringLiteral("The images of this workspace are floating"));
    EXPECT_EQ(host().page(first)->floatingImages()->views(), std::vector<ImageViewControl*>{view});

    ImageViewControl* const docked = openView(QStringLiteral("workspaces-docked"));
    ASSERT_NE(docked, nullptr);
    ASSERT_EQ(host().workspaceOf(*docked), first);
    EXPECT_FALSE(host().page(first)->isFloatingImagesShown());
    const QString second = newWorkspace();
    ASSERT_EQ(host().model().count(), 2u);
    EXPECT_TRUE(host().page(second)->isFloatingImagesShown());
    EXPECT_EQ(host().page(second)->floatingImages()->title(), QStringLiteral("This workspace is empty"));
    EXPECT_TRUE(host().page(second)->floatingImages()->views().empty()) << "a workspace beside another lists them";
    view->getDockWidget()->setFloating(false);
    settle();
}

// With no image open Home is shown: the last image closed shows it, and the workspace's icon leaves it there while the
// window has one workspace.
TEST_F(Workspaces, WithNoImageHomeIsShown)
{
    const QString second = newWorkspace();
    ImageViewControl* const view = openView(QStringLiteral("workspaces-no-image"));
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(host().workspaceOf(*view), second) << "the view did not land in the workspace on screen";
    core().viewService().removeViewControl(view, true);
    settle();
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_EQ(host().model().count(), 1u) << "an empty workspace stayed";
    clickWorkspace(host().ids().front());
    EXPECT_TRUE(window().isHomeShown());
}

// Locking one workspace by its icon's menu locks its dock area alone: its drags are refused and a double click floats
// nothing there, while the other's views move freely; Lock Layout is offered as the workspace on screen is.
TEST_F(Workspaces, LockingOneWorkspaceLeavesTheOtherFree)
{
    ImageViewControl* const lockedView = openView(QStringLiteral("workspaces-locked"));
    ASSERT_NE(lockedView, nullptr);
    const QString first = host().ids().front();
    const QString second = newWorkspace();
    ImageViewControl* const freeView = openView(QStringLiteral("workspaces-free"));
    ASSERT_NE(freeView, nullptr);
    ASSERT_EQ(host().workspaceOf(*freeView), second);

    {
        QMenu menu;
        window().buildWorkspaceMenu(menu, first);
        QAction* const lock = menu.findChild<QAction*>(QStringLiteral("workspaceMenuLock"));
        ASSERT_NE(lock, nullptr);
        ASSERT_TRUE(lock->isCheckable());
        EXPECT_FALSE(lock->isChecked());
        lock->trigger();
        settle();
    }
    EXPECT_TRUE(core().dockLayout().isLocked(host().target(first)));
    EXPECT_FALSE(core().dockLayout().isLocked(host().target(second)));
    EXPECT_TRUE(host().model().find(first)->locked);
    using an::ui::shell::DockDrag;
    using an::ui::shell::DockHooks;
    EXPECT_FALSE(DockHooks::allowsDrag(
        DockDrag{.mainWindow = host().mainWindow(first), .dockWidget = lockedView->getDockWidget()}));
    EXPECT_TRUE(DockHooks::allowsDrag(
        DockDrag{.mainWindow = host().mainWindow(second), .dockWidget = freeView->getDockWidget()}));
    EXPECT_FALSE(DockHooks::dropIndicatorsInhibited()) << "one workspace's lock is not the whole layout's";
    EXPECT_TRUE(an::ui::layoutChangesLockedAt(*lockedView->getDockWidget()));
    EXPECT_FALSE(an::ui::layoutChangesLockedAt(*freeView->getDockWidget()));
    // The workspace on screen is free: Lock Layout is offered; on the locked one, Unlock Layout.
    EXPECT_TRUE(core().actions().action(ids::kLockLayout)->isEnabled());
    clickWorkspace(first);
    EXPECT_FALSE(core().actions().action(ids::kLockLayout)->isEnabled());
    EXPECT_TRUE(core().actions().action(ids::kUnlockLayout)->isEnabled());
    {
        QMenu menu;
        window().buildWorkspaceMenu(menu, first);
        EXPECT_TRUE(menu.findChild<QAction*>(QStringLiteral("workspaceMenuLock"))->isChecked());
    }
}

// Group All Images, Split All Images and Fit Images To Window act on the workspace on screen and leave the other as it
// is; the workspace icon's menu acts on its own workspace.
TEST_F(Workspaces, GroupSplitAndFitActOnTheWorkspaceOnScreen)
{
    ImageViewControl* const a1 = openView(QStringLiteral("workspaces-a1"));
    ImageViewControl* const a2 = openView(QStringLiteral("workspaces-a2"));
    const QString first = host().ids().front();
    const QString second = newWorkspace();
    ImageViewControl* const b1 = openView(QStringLiteral("workspaces-b1"));
    ImageViewControl* const b2 = openView(QStringLiteral("workspaces-b2"));
    ASSERT_NE(b2, nullptr);
    ASSERT_EQ(dockedIn(second).size(), 2u);
    ASSERT_NE(groupOf(a1), groupOf(a2));
    ASSERT_NE(groupOf(b1), groupOf(b2));

    runCatalogueAction(ids::kGroupAllImages);
    EXPECT_EQ(groupOf(b1), groupOf(b2));
    EXPECT_NE(groupOf(a1), groupOf(a2)) << "the other workspace's views were gathered";
    EXPECT_EQ(dockedIn(first).size(), 2u);

    runCatalogueAction(ids::kSplitAllImages);
    EXPECT_NE(groupOf(b1), groupOf(b2));
    EXPECT_EQ(dockedIn(first).size(), 2u);

    // Fit fits the views on screen; the other workspace's keep their zoom.
    b1->fitToWindow();
    const float fitted = b1->getCurrentZoom();
    a1->zoom100();
    b1->zoom100();
    settle();
    const float actualSize = a1->getCurrentZoom();
    ASSERT_NE(b1->getCurrentZoom(), fitted) << "the view's image fits its window at 100 %";
    runCatalogueAction(ids::kFitImagesToWindow);
    EXPECT_FLOAT_EQ(b1->getCurrentZoom(), fitted);
    EXPECT_FLOAT_EQ(a1->getCurrentZoom(), actualSize) << "a view of the workspace not on screen was fitted";

    // The other workspace's own menu groups its views.
    QMenu menu;
    window().buildWorkspaceMenu(menu, first);
    menu.findChild<QAction*>(QStringLiteral("workspaceMenuGroup"))->trigger();
    settle();
    EXPECT_EQ(groupOf(a1), groupOf(a2));
    EXPECT_NE(groupOf(b1), groupOf(b2));
}

// Move Workspace to New Window shows the workspace in a window of its own titled with its name: its views, its page's
// toolbar and strip, a header of layout icons; its rail icon stays, with the window glyph, and raises it. Closed, the
// window asks: Return brings the workspace back; Close Images closes them and the workspace.
TEST_F(Workspaces, AWorkspaceInItsOwnWindowKeepsItsViewsToolbarAndRailIconAndReturns)
{
    ImageViewControl* const stays = openView(QStringLiteral("workspaces-main"));
    ASSERT_NE(stays, nullptr);
    const QString first = host().ids().front();
    const QString second = newWorkspace();
    ImageViewControl* const away = openView(QStringLiteral("workspaces-away"));
    ASSERT_NE(away, nullptr);
    an::ui::observatory::WorkspacePage* const page = host().page(second);

    runShellAction(shellIds::kMoveWorkspaceToNewWindow);
    an::ui::observatory::WorkspaceWindow* const holder = window().workspaceWindow(second);
    ASSERT_NE(holder, nullptr);
    EXPECT_TRUE(holder->isVisible());
    EXPECT_TRUE(holder->isWindow());
    EXPECT_EQ(holder->windowTitle(), host().workspaceName(second));
    EXPECT_EQ(holder->page(), page);
    EXPECT_TRUE(holder->isAncestorOf(page));
    EXPECT_TRUE(page->isAncestorOf(page->toolbar()));
    EXPECT_EQ(host().workspaceOf(*away), second) << "the workspace's views did not go with it";
    ASSERT_NE(holder->header(), nullptr);
    EXPECT_NE(holder->header()->lockButton(), nullptr);
    EXPECT_TRUE(host().model().find(second)->detached);
    const an::ui::RailItem icon = railItem(*window().rail(), Window::railWorkspaceId(second));
    EXPECT_EQ(icon.id, Window::railWorkspaceId(second)) << "its rail icon went";
    EXPECT_TRUE(icon.floating);
    EXPECT_TRUE(icon.badgeText.isEmpty());
    EXPECT_EQ(host().shown(), first) << "the main window shows another workspace";
    EXPECT_FALSE(window().shellActions().action(shellIds::kMoveWorkspaceToNewWindow)->isEnabled());

    // Its rail icon raises the window, and makes its workspace the current one, which the main window's had been.
    host().setCurrent(first);
    ASSERT_EQ(host().current(), first);
    clickWorkspace(second);
    EXPECT_TRUE(holder->isVisible());
    EXPECT_EQ(host().current(), second);
    EXPECT_EQ(host().shown(), first);

    // Return brings it back.
    answerTheWindow(*holder, "returnWorkspaceButton");
    EXPECT_EQ(window().workspaceWindow(second), nullptr);
    EXPECT_FALSE(host().model().find(second)->detached);
    EXPECT_NE(host().stack()->indexOf(page), -1);
    EXPECT_EQ(host().shown(), second);
    EXPECT_EQ(host().workspaceOf(*away), second);

    // Close Images closes its images and the workspace.
    runShellAction(shellIds::kMoveWorkspaceToNewWindow);
    an::ui::observatory::WorkspaceWindow* const again = window().workspaceWindow(second);
    ASSERT_NE(again, nullptr);
    const QPointer<ImageViewControl> watched(away);
    // An image with unsaved changes is asked about, as Close asks: that question, which offers Yes, is answered Yes.
    const std::shared_ptr<an::tests::app::WindowPoll> poll = an::tests::app::answerWhenShown(
        [](QWidget* shown) {
            auto* const box = qobject_cast<QMessageBox*>(shown);
            if (box == nullptr || box->button(QMessageBox::Yes) == nullptr)
            {
                return false;
            }
            box->button(QMessageBox::Yes)->click();
            return true;
        },
        true);
    answerTheWindow(*again, "closeWorkspaceImagesButton");
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() {
        settle();
        return watched.isNull();
    })) << "the workspace's image stayed open";
    poll->ended = true;
    EXPECT_EQ(host().model().find(second), nullptr);
    EXPECT_EQ(window().workspaceWindow(second), nullptr);
    EXPECT_EQ(host().workspaceOf(*stays), first);
}

// Close Workspace closes its images as Close does, asking about unsaved changes: answered No the image and the
// workspace stay; answered Yes both go.
TEST_F(Workspaces, ClosingAWorkspaceAsksForUnsavedImages)
{
    ImageViewControl* const stays = openView(QStringLiteral("workspaces-first-image"));
    ASSERT_NE(stays, nullptr);
    const QString second = newWorkspace();
    ImageViewControl* const unsaved = openView(QStringLiteral("workspaces-unsaved"), 0.25f);
    ASSERT_NE(unsaved, nullptr);
    core().viewService().setActiveImageViewControl(unsaved);
    runCatalogueAction(ids::kNegativeTransformation);
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return std::abs(an::tests::app::viewSample(*unsaved) - 0.75) < 1e-6; }));
    ASSERT_TRUE(unsaved->hasPendingChanges());

    // Answered No.
    auto refused = std::make_shared<an::tests::app::Answered>();
    std::shared_ptr<an::tests::app::WindowPoll> poll = an::tests::app::answerWhenShown([refused](QWidget* shown) {
        auto* const box = qobject_cast<QMessageBox*>(shown);
        if (box == nullptr || box->button(QMessageBox::No) == nullptr)
        {
            return false;
        }
        refused->seen = true;
        refused->title = box->windowTitle();
        box->button(QMessageBox::No)->click();
        return true;
    });
    runShellAction(shellIds::kCloseWorkspace);
    poll->ended = true;
    EXPECT_TRUE(refused->seen) << "closing the workspace asked nothing about its unsaved image";
    EXPECT_EQ(refused->title, QStringLiteral("Close image with unsaved changes"));
    EXPECT_NE(host().model().find(second), nullptr);
    EXPECT_EQ(host().workspaceOf(*unsaved), second);

    // Answered Yes.
    auto answered = std::make_shared<an::tests::app::Answered>();
    poll = an::tests::app::answerWhenShown(
        [answered](QWidget* shown) { return an::tests::app::pressYesOrOk(shown, answered.get()); });
    const QPointer<ImageViewControl> watched(unsaved);
    runShellAction(shellIds::kCloseWorkspace);
    poll->ended = true;
    EXPECT_TRUE(answered->seen);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() {
        settle();
        return watched.isNull();
    }));
    EXPECT_EQ(host().model().find(second), nullptr);
    EXPECT_EQ(host().model().count(), 1u);
}

// Rename Workspace... asks for the name in a small dialog, OK withheld for a name of spaces; the rail and the
// breadcrumb say the new one.
TEST_F(Workspaces, RenameNamesTheWorkspaceEverywhere)
{
    const QString second = newWorkspace();
    runShellAction(shellIds::kRenameWorkspace);
    an::ui::observatory::RenameWorkspaceDialog* dialog = nullptr;
    for (QWidget* top : QApplication::topLevelWidgets())
    {
        if (auto* found = dynamic_cast<an::ui::observatory::RenameWorkspaceDialog*>(top);
            found != nullptr && found->isVisible())
        {
            dialog = found;
        }
    }
    ASSERT_NE(dialog, nullptr) << "Rename Workspace... opened no dialog";
    EXPECT_EQ(dialog->nameField()->text(), host().workspaceName(second));
    dialog->nameField()->setText(QStringLiteral("   "));
    EXPECT_FALSE(dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled());
    dialog->nameField()->setText(QStringLiteral("Masters"));
    ASSERT_TRUE(dialog->buttons()->button(QDialogButtonBox::Ok)->isEnabled());
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();
    settle();
    EXPECT_EQ(host().workspaceName(second), QStringLiteral("Masters"));
    EXPECT_EQ(railItem(*window().rail(), Window::railWorkspaceId(second)).text, QStringLiteral("Masters"));
    EXPECT_EQ(window().topBar()->breadcrumb()->parts(), QStringList{QStringLiteral("Masters")});
}

// A new view lands in the workspace on screen; one made beside another lands in that one's workspace, which is shown.
TEST_F(Workspaces, ANewViewLandsInTheWorkspaceOnScreenOrItsNeighbours)
{
    ImageViewControl* const neighbour = openView(QStringLiteral("workspaces-neighbour"));
    ASSERT_NE(neighbour, nullptr);
    const QString first = host().ids().front();
    const QString second = newWorkspace();
    ImageViewControl* const onScreen = openView(QStringLiteral("workspaces-on-screen"));
    ASSERT_NE(onScreen, nullptr);
    EXPECT_EQ(host().workspaceOf(*onScreen), second);

    ImageViewControl* const beside = openBeside(QStringLiteral("workspaces-beside"), neighbour);
    ASSERT_NE(beside, nullptr);
    EXPECT_EQ(host().workspaceOf(*beside), first);
    EXPECT_EQ(groupOf(beside), groupOf(neighbour)) << "the view is not its neighbour's tab";
    EXPECT_EQ(host().shown(), first);
}

// A drag about to start shows the dock area of a workspace that shows its quiet page, so the drag can land there; once
// it ends the page says again what the workspace holds.
TEST_F(Workspaces, ADragShowsTheDockAreaOfAQuietWorkspace)
{
    (void)openView(QStringLiteral("workspaces-dragged"));
    const QString second = newWorkspace();
    an::ui::observatory::WorkspacePage* const page = host().page(second);
    ASSERT_TRUE(page->isFloatingImagesShown());

    EXPECT_TRUE(an::ui::shell::DockHooks::allowsDrag(an::ui::shell::DockDrag{}));
    EXPECT_FALSE(page->isFloatingImagesShown()) << "the empty workspace kept its dock area out of the drag's reach";
    an::ui::shell::DockHooks::tellDragEnded(an::ui::shell::DockDrag{});
    settle();
    EXPECT_TRUE(page->isFloatingImagesShown());
}

} // namespace
} // namespace an::tests::observatory
