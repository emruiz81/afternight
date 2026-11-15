// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory workspace as a person uses it: the active view's floating toolbar and history strip, the layout
// icons of the classic dock tool bar, the view's and the tab's menus where they are asked for, a view maximized and
// restored with the lock offered again, the page of floating images, the files dropped on the window, and changing
// place with images open, which asks the engine nothing.

#include "ObservatoryWindowCase.h"

#include "chrome/MenuBarOnAlt.h"
#include "chrome/StatusLine.h"
#include "pages/FloatingImagesPage.h"
#include "pages/HomePage.h"
#include "pages/WorkspacePage.h"
#include "workspaces/FloatingViewChrome.h"
#include "workspaces/WorkspaceHost.h"
#include "workspaces/WorkspaceToolbar.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/ViewMenuBuilder.h"
#include "components/AnFloatingToolbar.h"
#include "components/AnHistoryStrip.h"
#include "components/AnIconRail.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ViewChrome.h"
#include "core/ApplicationCore.h"
#include "readout/ReadoutController.h"
#include "views/DockHooks.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewChromeBinder.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QThreadPool>
#include <QToolButton>
#include <QUrl>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using WorkspacePage = ObservatoryWindowCase;
using LayoutIcons = ObservatoryWindowCase;
using FloatingImagesPage = ObservatoryWindowCase;
using ChangingPlace = ObservatoryWindowCase;

/// Lets the window settle: posted work delivered, the deletions of closed docks done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The entries' texts of a menu, separators left out.
[[nodiscard]] QStringList entriesOf(const QMenu& menu)
{
    QStringList texts;
    for (const QAction* action : menu.actions())
    {
        if (action != nullptr && !action->isSeparator())
        {
            texts.append(action->text());
        }
    }
    return texts;
}

/// Reads the next menu that opens, where it opened and its entries, then closes it.
struct ShownMenu
{
    QPoint at;
    QStringList entries;
    bool seen = false;
};

[[nodiscard]] std::shared_ptr<ShownMenu> readTheNextMenu()
{
    auto shown = std::make_shared<ShownMenu>();
    (void)an::tests::app::answerWhenShown([shown](QWidget* widget) {
        auto* const menu = qobject_cast<QMenu*>(widget);
        if (menu == nullptr || !menu->isVisible())
        {
            return false;
        }
        shown->seen = true;
        shown->at = menu->geometry().topLeft();
        shown->entries = entriesOf(*menu);
        menu->close();
        return true;
    });
    return shown;
}

// The active view's toolbar floats centred over the workspace, 12 px above its bottom, and follows a resize of the
// window.
TEST_F(WorkspacePage, TheToolbarFloatsOverTheDockAreaAndFollowsAResize)
{
    ImageViewControl* const view = openView(QStringLiteral("toolbar-floats"));
    ASSERT_NE(view, nullptr);
    settle();
    an::ui::observatory::WorkspacePage& page = *window().workspacePage();
    an::ui::AnFloatingToolbar* const toolbar = page.toolbar();
    ASSERT_TRUE(toolbar->isVisible());
    EXPECT_EQ(view->chromeMode(), an::ui::ViewChromeMode::External) << "the view still draws its own toolbar";
    const auto placedRight = [&]() {
        const QRect area = page.canvasArea();
        const QRect bar = toolbar->geometry();
        // The bar's body is 12 px above the area's edge; its 1 px edge line lies under the body.
        const int bodyBottom = bar.top() + toolbar->barHeight() - 1;
        return bar == toolbar->placementIn(area) && area.bottom() - bodyBottom == 12 &&
               std::abs(bar.center().x() - area.center().x()) <= 1;
    };
    const auto describe = [&]() {
        const QRect bar = toolbar->geometry();
        const QRect placed = toolbar->placementIn(page.canvasArea());
        const QRect area = page.canvasArea();
        return QStringLiteral("toolbar %1,%2 %3x%4; placed %5,%6 %7x%8; area %9,%10 %11x%12")
            .arg(bar.x())
            .arg(bar.y())
            .arg(bar.width())
            .arg(bar.height())
            .arg(placed.x())
            .arg(placed.y())
            .arg(placed.width())
            .arg(placed.height())
            .arg(area.x())
            .arg(area.y())
            .arg(area.width())
            .arg(area.height())
            .toStdString();
    };
    EXPECT_TRUE(placedRight()) << describe();

    const QSize before = window().size();
    window().resize(before.width() - 200, before.height() - 100);
    settle();
    EXPECT_TRUE(placedRight()) << "the toolbar did not follow the resize: " << describe();
    window().resize(before);
    settle();
    EXPECT_TRUE(placedRight());
}

// The toolbar shows the active view's own actions, and with two views it names the one it acts on.
TEST_F(WorkspacePage, TheToolbarActsOnTheActiveViewAndNamesItWithTwo)
{
    ImageViewControl* const first = openView(QStringLiteral("toolbar-first"));
    ASSERT_NE(first, nullptr);
    settle();
    an::ui::AnFloatingToolbar* const toolbar = window().workspacePage()->toolbar();
    EXPECT_EQ(toolbar->actions().zoomIn, first->chromeActions().zoomIn);
    EXPECT_FALSE(toolbar->shownParts().contains(QStringLiteral("name"))) << "one view is named";

    ImageViewControl* const second = openView(QStringLiteral("toolbar-second"));
    ASSERT_NE(second, nullptr);
    settle();
    EXPECT_EQ(toolbar->actions().zoomIn, second->chromeActions().zoomIn);
    EXPECT_TRUE(toolbar->shownParts().contains(QStringLiteral("name")));

    core().viewService().setActiveImageViewControl(first);
    settle();
    EXPECT_EQ(toolbar->actions().zoomIn, first->chromeActions().zoomIn);
}

// The history strip under the workspace shows the active view's history, and follows when another view is made the
// active one.
TEST_F(WorkspacePage, TheHistoryStripFollowsTheActiveView)
{
    ImageViewControl* const first = openView(QStringLiteral("strip-first"));
    ImageViewControl* const second = openView(QStringLiteral("strip-second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    settle();
    an::ui::observatory::WorkspacePage& page = *window().workspacePage();
    EXPECT_TRUE(page.historyStrip()->isVisible());
    EXPECT_EQ(page.binder().historyView(), second);
    core().viewService().setActiveImageViewControl(first);
    settle();
    EXPECT_EQ(page.binder().historyView(), first);
    EXPECT_FALSE(page.historyStrip()->steps().steps.empty());
}

// The layout icons are the classic dock tool bar's: the lock's button, Group All Images, Split All Images, a separator
// and Fit Images To Window, the three the catalogue's own actions with their icons and tooltips; shown while the
// workspace is.
TEST_F(LayoutIcons, AreTheClassicDockToolbarsFour)
{
    an::ui::observatory::WorkspaceToolbar* const icons = window().layoutIcons();
    ASSERT_NE(icons, nullptr);
    EXPECT_FALSE(icons->isVisible()) << "the layout icons show on Home";
    EXPECT_EQ(icons->lockButton()->toolTip(), QStringLiteral("Lock layout"));
    EXPECT_EQ(icons->groupButton()->defaultAction(), core().actions().action(an::ui::shell::ids::kGroupAllImages));
    EXPECT_EQ(icons->splitButton()->defaultAction(), core().actions().action(an::ui::shell::ids::kSplitAllImages));
    EXPECT_EQ(icons->fitButton()->defaultAction(), core().actions().action(an::ui::shell::ids::kFitImagesToWindow));
    EXPECT_EQ(icons->groupButton()->toolTip(), icons->groupButton()->defaultAction()->toolTip());

    // In the classic bar's order, the separator before Fit.
    std::vector<QWidget*> order;
    for (QWidget* child : icons->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        order.push_back(child);
    }
    std::sort(order.begin(), order.end(), [](const QWidget* a, const QWidget* b) { return a->x() < b->x(); });
    ASSERT_EQ(order.size(), 5u);
    EXPECT_EQ(order[0], icons->lockButton());
    EXPECT_EQ(order[1], icons->groupButton());
    EXPECT_EQ(order[2], icons->splitButton());
    EXPECT_NE(qobject_cast<QFrame*>(order[3]), nullptr);
    EXPECT_EQ(order[4], icons->fitButton());

    ASSERT_NE(openView(QStringLiteral("layout-icons-shown")), nullptr);
    settle();
    EXPECT_TRUE(icons->isVisible());
}

// The lock's button locks the layout, which then refuses a drag, and its icon and tooltip turn to Unlock; a second
// click unlocks it.
TEST_F(LayoutIcons, TheLockTogglesItsIconAndRefusesDrags)
{
    ImageViewControl* const view = openView(QStringLiteral("layout-lock"));
    ASSERT_NE(view, nullptr);
    settle();
    an::ui::observatory::WorkspaceToolbar* const icons = window().layoutIcons();
    const an::ui::shell::DockDrag drag{.mainWindow = shell().viewHost().dockTarget(view).mainWindow,
                                       .dockWidget = view->getDockWidget()};
    ASSERT_TRUE(an::ui::shell::DockHooks::allowsDrag(drag));
    const QIcon lockIcon = icons->lockButton()->icon();

    QTest::mouseClick(icons->lockButton(), Qt::LeftButton);
    EXPECT_TRUE(core().dockLayout().isLocked(shell().viewHost().dockTarget()));
    EXPECT_FALSE(an::ui::shell::DockHooks::allowsDrag(drag));
    EXPECT_EQ(icons->lockButton()->toolTip(), QStringLiteral("Unlock layout"));
    EXPECT_NE(icons->lockButton()->icon().cacheKey(), lockIcon.cacheKey());

    QTest::mouseClick(icons->lockButton(), Qt::LeftButton);
    EXPECT_FALSE(core().dockLayout().isLocked(shell().viewHost().dockTarget()));
    EXPECT_TRUE(an::ui::shell::DockHooks::allowsDrag(drag));
    EXPECT_EQ(icons->lockButton()->toolTip(), QStringLiteral("Lock layout"));
}

// A right click on a view opens the view's menu where the pointer is, entry for entry the menu every shell shows: it
// opens where a menu of those entries asked to open at the pointer opens, which on the suites' small screen is the
// place the screen leaves for it.
TEST_F(WorkspacePage, ViewMenuOpensAtTheCursor)
{
    ImageViewControl* const view = openView(QStringLiteral("view-menu"));
    ASSERT_NE(view, nullptr);
    settle();
    const std::shared_ptr<ShownMenu> shown = readTheNextMenu();
    const QPoint at = view->rect().center();
    const QPoint global = view->mapToGlobal(at);
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, global);
    QCoreApplication::sendEvent(view, &event);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&shown]() { return shown->seen; }, 3000));
    ASSERT_FALSE(shown->entries.isEmpty());
    EXPECT_TRUE(shown->entries.contains(QStringLiteral("Show in File Browser")))
        << shown->entries.join(QStringLiteral(", ")).toStdString();

    QMenu reference;
    an::ui::shell::ViewMenuBuilder(core()).buildViewMenu(
        reference, *view, [](std::string_view menuId) { return window().menuBarOnAlt().menu(menuId); });
    EXPECT_EQ(entriesOf(reference), shown->entries);
    reference.popup(global);
    QCoreApplication::processEvents();
    EXPECT_EQ(reference.geometry().topLeft(), shown->at) << "the menu did not open where the pointer was";
    reference.close();
}

// A right click on a view's tab opens the tab's menu, over that view: Close, Close All Others, Close Tab Group, Float,
// Duplicate, Rename, Save File and Show In File Browser.
TEST_F(WorkspacePage, TabMenu)
{
    ImageViewControl* const first = openView(QStringLiteral("tab-menu-first"));
    ImageViewControl* const second = openView(QStringLiteral("tab-menu-second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    settle();
    AN_TabBar* const tabs = first->getTabBar();
    ASSERT_NE(tabs, nullptr);
    int index = -1;
    for (int tab = 0; tab < tabs->count(); ++tab)
    {
        if (tabs->tabBar()->dockWidgetAt(tab) == first->getDockWidget()->dockWidget())
        {
            index = tab;
        }
    }
    ASSERT_GE(index, 0);
    const std::shared_ptr<ShownMenu> shown = readTheNextMenu();
    const QPoint at = tabs->tabRect(index).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, tabs->mapToGlobal(at));
    QCoreApplication::sendEvent(tabs, &event);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&shown]() { return shown->seen; }, 3000));
    for (const QString& entry : {QStringLiteral("Close"), QStringLiteral("Close All Others"),
                                 QStringLiteral("Close Tab Group"), QStringLiteral("Show in File Browser")})
    {
        EXPECT_TRUE(shown->entries.contains(entry))
            << entry.toStdString() << " is not in " << shown->entries.join(QStringLiteral(", ")).toStdString();
    }
    EXPECT_EQ(core().viewService().getActiveImageViewControl(), first) << "the tab's menu is not over its view";
}

// F11 maximizes the view over the workspace's page: the tab strips are out of sight, the toolbar and the history strip
// stay; Esc restores it.
TEST_F(WorkspacePage, MaximizeAndRestore)
{
    ImageViewControl* const view = openView(QStringLiteral("maximize-restore"));
    ASSERT_NE(view, nullptr);
    settle();
    an::ui::observatory::WorkspacePage& page = *window().workspacePage();
    AN_TabBar* const tabs = view->getTabBar();
    // The keys a view takes for itself reach its canvas, which has the focus.
    ImageViewWidget* canvas = nullptr;
    for (QWidget* child : view->findChildren<QWidget*>())
    {
        if (auto* const widget = dynamic_cast<ImageViewWidget*>(child); widget != nullptr && canvas == nullptr)
        {
            canvas = widget;
        }
    }
    ASSERT_NE(canvas, nullptr);
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_F11);
    settle();
    ASSERT_TRUE(view->isMaximized());
    EXPECT_EQ(page.maximizedView(), view);
    EXPECT_TRUE(tabs == nullptr || !tabs->isVisible()) << "a tab strip shows over the maximized view";
    EXPECT_TRUE(page.toolbar()->isVisible());
    EXPECT_TRUE(page.historyStrip()->isVisible());

    QTest::keyClick(canvas, Qt::Key_Escape);
    settle();
    EXPECT_FALSE(view->isMaximized());
    EXPECT_EQ(page.maximizedView(), nullptr);
    EXPECT_TRUE(page.toolbar()->isVisible());
}

// After a maximize the lock offers what the layout is not - Lock while it is free, Unlock while it is locked - and
// withdraws both while the view is maximized.
TEST_F(WorkspacePage, TheLockIsOfferedAgainAfterAMaximize)
{
    ImageViewControl* const view = openView(QStringLiteral("lock-after-maximize"));
    ASSERT_NE(view, nullptr);
    settle();
    QAction* const lock = core().actions().action(an::ui::shell::ids::kLockLayout);
    QAction* const unlock = core().actions().action(an::ui::shell::ids::kUnlockLayout);
    ASSERT_NE(lock, nullptr);
    ASSERT_NE(unlock, nullptr);

    view->switchMaximized();
    settle();
    EXPECT_FALSE(lock->isEnabled());
    EXPECT_FALSE(unlock->isEnabled());
    view->switchMaximized();
    settle();
    EXPECT_TRUE(lock->isEnabled());
    EXPECT_FALSE(unlock->isEnabled());

    lock->trigger();
    settle();
    view->switchMaximized();
    settle();
    view->switchMaximized();
    settle();
    EXPECT_FALSE(lock->isEnabled());
    EXPECT_TRUE(unlock->isEnabled());
    unlock->trigger();
    settle();
}

// The only workspace with every image floating lists the floating images with Dock here, and an image docked from the
// list is back in the workspace's dock area, its tools the workspace's again.
TEST_F(FloatingImagesPage, ListsFloatingImagesAndDocksThem)
{
    ImageViewControl* const view = openView(QStringLiteral("floating-image"));
    ASSERT_NE(view, nullptr);
    settle();
    view->getDockWidget()->setFloating(true);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() {
        settle();
        return window().workspacePage()->isFloatingImagesShown();
    })) << "every image floats, and the workspace does not list them";
    an::ui::observatory::FloatingImagesPage* const list = window().workspacePage()->floatingImages();
    ASSERT_EQ(list->views().size(), 1u);
    EXPECT_EQ(list->rowName(view), QString::fromUtf8(view->getUniqueName()));
    EXPECT_NE(window().floatingViewChrome().toolbarOf(view), nullptr) << "the floating image has no toolbar of its own";

    QPushButton* const dockHere = list->dockHereButton(view);
    ASSERT_NE(dockHere, nullptr);
    dockHere->click();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() {
        settle();
        return !view->isInFloatingWindow() && !window().workspacePage()->isFloatingImagesShown();
    }));
    EXPECT_EQ(an::ui::shell::DockHooks::mainWindowHolding(view->getDockWidget()), window().workspaces().mainWindow());
    EXPECT_EQ(window().floatingViewChrome().toolbarOf(view), nullptr);
}

// A file dropped on the window opens in a view of the workspace.
TEST_F(WorkspacePage, DroppedFilesOpen)
{
    const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("observatory-dropped.fits"));
    std::optional<an::consumer::ImageAsset> asset = an::tests::app::filledAsset(session(), 0.5f);
    ASSERT_TRUE(asset.has_value());
    const an::Result<an::OutputIntent> intent =
        an::ui::appContext()->getOutputIntentProvider()->buildOutputIntent(*asset->descriptor(), an::CodecFormat::Fits);
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    ASSERT_TRUE(an::ui::writeViewImage(session(), *asset, path, *intent, {}).ok());

    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPoint(200, 200), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    enter.ignore();
    QCoreApplication::sendEvent(&window(), &enter);
    ASSERT_TRUE(enter.isAccepted()) << "the window refuses a file dragged over it";
    QDropEvent drop(QPointF(200, 200), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    drop.ignore();
    QCoreApplication::sendEvent(&window(), &drop);
    EXPECT_TRUE(drop.isAccepted());
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [&]() {
            for (ImageViewControl* view : views())
            {
                if (view->hasImage() && QDir::cleanPath(view->document().names().sourcePath) == QDir::cleanPath(path))
                {
                    return window().isWorkspaceShown();
                }
            }
            return false;
        },
        20000));
    QFile::remove(path);
}

// The toolbar over the workspace carries the active view's own tools: Fit, Zoom Out, Zoom In, Zoom 100 %, Maximize,
// the channel, the display mode and its Autostretch Settings, the statistics overlay, Show Astrometry and More.
TEST_F(WorkspacePage, TheToolbarCarriesTheViewsTools)
{
    ImageViewControl* const view = openView(QStringLiteral("toolbar-tools"));
    ASSERT_NE(view, nullptr);
    settle();
    an::ui::AnFloatingToolbar* const toolbar = window().workspacePage()->toolbar();
    const an::ui::ViewChromeActions& actions = view->chromeActions();
    // The tools every image has; the others are offered as the image allows, and are on the toolbar when they are.
    for (const QAction* action : {actions.fit, actions.zoomOut, actions.zoomIn, actions.zoom100, actions.maximize})
    {
        ASSERT_NE(action, nullptr);
        EXPECT_NE(toolbar->buttonFor(action), nullptr) << action->text().toStdString() << " is not on the toolbar";
    }
    for (const QAction* action : {actions.autostretchSettings, actions.statisticsOverlay, actions.showAstrometry})
    {
        if (action != nullptr)
        {
            EXPECT_NE(toolbar->buttonFor(action), nullptr) << action->text().toStdString() << " is not on the toolbar";
        }
    }
    EXPECT_NE(toolbar->channelField(), nullptr);
    EXPECT_NE(toolbar->displayModeSegments(), nullptr);
    EXPECT_NE(toolbar->moreButton(), nullptr);
}

// Zoom In on the toolbar enlarges the active view, as the view's own button did.
TEST_F(WorkspacePage, TheToolbarZoomsTheActiveView)
{
    ImageViewControl* const view = openView(QStringLiteral("toolbar-zoom"));
    ASSERT_NE(view, nullptr);
    settle();
    view->zoom100();
    settle();
    const float before = view->getCurrentZoom();
    QAbstractButton* const zoomIn = window().workspacePage()->toolbar()->buttonFor(view->chromeActions().zoomIn);
    ASSERT_NE(zoomIn, nullptr);
    zoomIn->click();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return view->getCurrentZoom() > before; }, 3000));
}

// The display mode's segments on the toolbar switch the active view between Linear and Autostretch.
TEST_F(WorkspacePage, TheToolbarSwitchesTheDisplayMode)
{
    ImageViewControl* const view = openView(QStringLiteral("toolbar-display-mode"));
    ASSERT_NE(view, nullptr);
    settle();
    an::ui::AnFloatingToolbar* const toolbar = window().workspacePage()->toolbar();
    QComboBox* const mode = view->chromeActions().displayMode;
    ASSERT_NE(mode, nullptr);
    const int before = mode->currentIndex();
    QAbstractButton* const other = toolbar->displayModeSegment(before == 0 ? 1 : 0);
    ASSERT_NE(other, nullptr);
    other->click();
    settle();
    EXPECT_NE(mode->currentIndex(), before) << "the toolbar's segment did not change the view's display mode";
}

// Save as writes the active view to the file the person chooses.
TEST_F(WorkspacePage, SaveAsWritesTheActiveView)
{
    ImageViewControl* const view = openView(QStringLiteral("save-as"), 0.625f);
    ASSERT_NE(view, nullptr);
    settle();
    const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("observatory-saved-as.fits"));
    QFile::remove(path);
    an::tests::app::Answered dialog;
    an::tests::app::chooseInTheNextFileDialog({path}, &dialog);
    QAction* const saveAs = core().actions().action(an::ui::shell::ids::kSaveAs);
    ASSERT_NE(saveAs, nullptr);
    saveAs->trigger();
    EXPECT_TRUE(dialog.seen);
    EXPECT_FALSE(dialog.refused) << dialog.refusal.toStdString();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return QFileInfo::exists(path); }, 20000));
    QFile::remove(path);
}

/// The admissions the session has made and refused since it opened.
[[nodiscard]] std::uint64_t admissionsSoFar(const an::consumer::EngineSession& engine)
{
    const an::consumer::AdmissionSnapshot admission = engine.admission();
    return admission.admitted + admission.refusedBusy;
}

/// Waits until the work the open views started has ended: longer than the readout gathers changes for, the pool
/// drained after it, twice.
void drain()
{
    for (int round = 0; round < 2; ++round)
    {
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 2 * an::ui::shell::ReadoutController::kDebounce.count() + 100)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        QThreadPool::globalInstance()->waitForDone();
        settle();
    }
}

// Changing place - Home, the workspace, Home again - with four images open asks the engine for nothing: the pages are
// kept alive, and nothing is measured again.
TEST_F(ChangingPlace, CausesNoEngineAdmission)
{
    for (int index = 0; index < 4; ++index)
    {
        ASSERT_NE(openView(QStringLiteral("changing-place-%1").arg(index)), nullptr);
    }
    drain();
    const std::uint64_t before = admissionsSoFar(session());
    for (int round = 0; round < 3; ++round)
    {
        window().showHome();
        settle();
        window().showViews();
        settle();
    }
    drain();
    EXPECT_EQ(admissionsSoFar(session()), before) << "changing place asked the engine";
}

/// Sees whether a widget has painted since it was last asked.
class Painted final : public QObject
{
public:
    bool painted = false;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event != nullptr && event->type() == QEvent::Paint)
        {
            painted = true;
        }
        return QObject::eventFilter(watched, event);
    }
};

// The time from a click on the rail to the painted page, with four 60-megapixel images open, the workspace and Home in
// turn, the median of twenty changes. Measured, never asserted, and only when AFTERNIGHT_TEST_TIMINGS=1 asks for it.
TEST_F(ChangingPlace, Timing)
{
    if (qEnvironmentVariable("AFTERNIGHT_TEST_TIMINGS") != QStringLiteral("1"))
    {
        GTEST_SKIP() << "a time is measured only when AFTERNIGHT_TEST_TIMINGS=1 asks for it";
    }
    for (int index = 0; index < 4; ++index)
    {
        std::optional<an::consumer::ImageAsset> asset = an::tests::app::filledAsset(session(), 0.25f, 7746u, 7746u);
        ASSERT_TRUE(asset.has_value());
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(*asset), an::ui::DocumentNames{.displayName = QStringLiteral("large-%1").arg(index)});
        ASSERT_TRUE(document.hasValue());
        ASSERT_NE(core().viewService().userViews().createNewImageViewControlFromImage(nullptr, std::move(*document),
                                                                                      nullptr, "large"),
                  nullptr);
    }
    drain();
    an::ui::AnIconRail& rail = *window().rail();
    Painted home;
    Painted workspace;
    window().homePage()->installEventFilter(&home);
    window().workspacePage()->installEventFilter(&workspace);
    std::vector<double> times;
    for (int change = 0; change < 20; ++change)
    {
        const bool toHome = change % 2 == 0;
        Painted& target = toHome ? home : workspace;
        target.painted = false;
        const QRect item =
            rail.itemRect(QString::fromLatin1(toHome ? an::ui::observatory::ObservatoryWindow::kRailHome
                                                     : an::ui::observatory::ObservatoryWindow::kRailWorkspace));
        QElapsedTimer timer;
        timer.start();
        QTest::mouseClick(&rail, Qt::LeftButton, Qt::NoModifier, item.center());
        while (!target.painted && timer.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        }
        times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
    }
    window().homePage()->removeEventFilter(&home);
    window().workspacePage()->removeEventFilter(&workspace);
    std::sort(times.begin(), times.end());
    const double median = times[times.size() / 2];
    std::printf("\nChanging place with four 60-megapixel images open, median of 20 changes: %.2f ms (max %.2f ms)\n",
                median, times.back());
    ::testing::Test::RecordProperty("medianMs", std::to_string(median));
}

} // namespace
} // namespace an::tests::observatory
