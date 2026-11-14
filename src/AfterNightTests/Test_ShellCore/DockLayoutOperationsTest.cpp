// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The layout operations on a dock area: every view gathered into one tab group or into a grid of equal shares, the
/// active view kept active; the views sharing a view's window grouped, split or fitted there; and the lock, which
/// refuses a drag out of the locked main window and no other's - and in a shell with one dock area every drag and every
/// indicator, as the layout lock always did - and offers Unlock Layout in place of Lock Layout.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "core/IShell.h"
#include "views/DockHooks.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"

#include <gtest/gtest.h>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/Group.h>

#include <QAction>
#include <QCoreApplication>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::documentNamed;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::shell::ApplicationCore;
using an::ui::shell::DockDrag;
using an::ui::shell::DockDrop;
using an::ui::shell::DockHooks;
using an::ui::shell::DockLayoutOperations;
using an::ui::shell::DockTarget;
using MainWindow = KDDockWidgets::QtWidgets::MainWindow;
namespace ids = an::ui::shell::ids;

void settleDocks()
{
    an::tests::shellcore::settle();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

class DockLayoutOperationsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_session = an::tests::shellcore::openSession();
        ASSERT_NE(m_session, nullptr);
        m_core.openStores();
        m_core.setShell(&m_shell);
        m_core.setEngineSessionProvider(m_session.get());
        m_target = std::make_unique<an::ui::shell::ServiceEventTarget>(
            m_core, an::ui::shell::ServiceEventTarget::Dependencies{});
        m_core.setEventTarget(m_target.get());
        m_context = std::make_unique<an::ui::shell::ApplicationContext>(m_core, m_shell);
        an::ui::clearAppContext();
        an::ui::setAppContext(m_context.get());

        m_window = std::make_unique<QWidget>();
        m_window->resize(1000, 700);
        m_area = new MainWindow(QStringLiteral("layout.area"), {}, m_window.get());
        m_area->setGeometry(QRect(0, 0, 1000, 700));
        m_window->show();
        settleDocks();
    }

    void TearDown() override
    {
        // The docks go with the context still there: deleting a tab of a group makes the dock library show the next
        // tab, whose view the context makes the active one. The views then leave the list, the active one with them,
        // before any work queued meanwhile runs - as the window's going takes them out of it - so none reads a view
        // that went.
        for (const QPointer<QWidget>& dock : m_docks)
        {
            delete dock.data();
        }
        for (const QPointer<ImageViewControl>& view : m_views)
        {
            delete view.data();
        }
        m_core.viewService().manager().clear();
        settleDocks();
        m_window.reset();
        settleDocks();
        m_core.discardPendingEvents();
        m_core.setEventTarget(nullptr);
        an::ui::clearAppContext();
        m_core.setEngineSessionProvider(nullptr);
        m_core.setShell(nullptr);
        m_core.closeStores();
    }

    /// A view of a new document named `name`, listed by the core and docked on the left of `area`; the case deletes it.
    ImageViewControl* dockedView(const QString& name, MainWindow* area = nullptr)
    {
        std::optional<an::ui::ImageDocument> document = documentNamed(m_session->engineSession(), name);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        an::ui::IViewFactory& factory = m_core.viewService();
        ImageViewControl* const view = factory.createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "made", ViewControlMode::standard, nullptr, false, false);
        EXPECT_NE(view, nullptr);
        if (view == nullptr)
        {
            return nullptr;
        }
        m_views.emplace_back(view);
        m_docks.emplace_back(view->getDockWidget());
        (area != nullptr ? area : m_area)->addDockWidget(view->getDockWidget(), KDDockWidgets::Location_OnLeft);
        settleDocks();
        return view;
    }

    [[nodiscard]] DockTarget onlyArea() const
    {
        return DockTarget{.mainWindow = m_area, .workspaceId = {}};
    }

    [[nodiscard]] static KDDockWidgets::Core::Group* groupOf(ImageViewControl* view)
    {
        return view->getDockWidget()->group();
    }

    /// Where a view's dock widget is in the area, in the area's coordinates.
    [[nodiscard]] QRect placeOf(ImageViewControl* view) const
    {
        const QWidget* const dock = view->getDockWidget();
        return QRect(dock->mapTo(m_area, QPoint(0, 0)), dock->size());
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    RecordingShell m_shell;
    std::unique_ptr<SessionProvider> m_session;
    ApplicationCore m_core;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_context;
    std::unique_ptr<QWidget> m_window;
    MainWindow* m_area = nullptr;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
};

} // namespace

// Group All Images gathers every view into the area as one tab group, and the view that was active stays active.
TEST_F(DockLayoutOperationsTest, GroupMakesOneTabGroupAndKeepsTheActiveView)
{
    ImageViewControl* const first = dockedView(QStringLiteral("first"));
    ImageViewControl* const second = dockedView(QStringLiteral("second"));
    ImageViewControl* const third = dockedView(QStringLiteral("third"));
    ASSERT_NE(third, nullptr);
    m_core.viewService().setActiveImageViewControl(second);
    ASSERT_NE(groupOf(first), groupOf(second));

    m_core.dockLayout().groupAll(onlyArea());
    settleDocks();

    EXPECT_EQ(groupOf(first), groupOf(second));
    EXPECT_EQ(groupOf(second), groupOf(third));
    EXPECT_EQ(m_core.viewService().getActiveImageViewControl(), second);
}

// Split All Images gives each view a pane of its own in equal shares: of three, two one above the other on the left and
// one on the right, the two columns of one width and the two left panes of one height, within a pixel. Which view goes
// where follows the order the view list holds them in, which no person chose, so the case reads the panes alone.
TEST_F(DockLayoutOperationsTest, SplitGivesEachViewAnEqualPane)
{
    ImageViewControl* const first = dockedView(QStringLiteral("first"));
    ImageViewControl* const second = dockedView(QStringLiteral("second"));
    ImageViewControl* const third = dockedView(QStringLiteral("third"));
    ASSERT_NE(third, nullptr);
    m_core.dockLayout().groupAll(onlyArea());
    settleDocks();
    ASSERT_EQ(groupOf(first), groupOf(third));

    m_core.dockLayout().splitAllEqually(onlyArea());
    settleDocks();

    const std::set<KDDockWidgets::Core::Group*> groups{groupOf(first), groupOf(second), groupOf(third)};
    EXPECT_EQ(groups.size(), 3u);
    std::vector<QRect> panes{placeOf(first), placeOf(second), placeOf(third)};
    std::sort(panes.begin(), panes.end(), [](const QRect& left, const QRect& right) {
        return std::pair(left.left(), left.top()) < std::pair(right.left(), right.top());
    });
    const QRect& upper = panes[0];
    const QRect& lower = panes[1];
    const QRect& beside = panes[2];
    EXPECT_LE(std::abs(upper.left() - lower.left()), 1);
    EXPECT_LT(upper.top(), lower.top());
    EXPECT_GT(beside.left(), upper.right());
    EXPECT_LE(std::abs(upper.width() - beside.width()), 1) << upper.width() << " and " << beside.width();
    EXPECT_LE(std::abs(upper.height() - lower.height()), 1) << upper.height() << " and " << lower.height();
}

// Fit All Images to Window fits each view that shares the view's window, and leaves a view floating apart as it was.
TEST_F(DockLayoutOperationsTest, FitFitsEachViewSharingTheWindowOfTheView)
{
    ImageViewControl* const first = dockedView(QStringLiteral("first"));
    ImageViewControl* const second = dockedView(QStringLiteral("second"));
    ImageViewControl* const apart = dockedView(QStringLiteral("apart"));
    ASSERT_NE(apart, nullptr);
    apart->getDockWidget()->setFloating(true);
    settleDocks();

    std::vector<float> fitted;
    for (ImageViewControl* view : {first, second})
    {
        view->fitToWindow();
        fitted.push_back(view->getCurrentZoom());
        view->zoom100();
        ASSERT_NE(view->getCurrentZoom(), fitted.back());
    }
    apart->zoom100();
    const float apartZoom = apart->getCurrentZoom();

    m_core.dockLayout().fitAllToWindow(*first);

    EXPECT_FLOAT_EQ(first->getCurrentZoom(), fitted[0]);
    EXPECT_FLOAT_EQ(second->getCurrentZoom(), fitted[1]);
    EXPECT_FLOAT_EQ(apart->getCurrentZoom(), apartZoom);
    EXPECT_EQ(
        DockLayoutOperations::viewsSharingTheWindowOf(*first, m_core.viewService().getListOfViewControls()).size(), 2u);
}

// A lock of one of several dock areas refuses a drag out of that main window and an indicator into it, and nothing of
// another area or of a floating window; the indicators are not inhibited, and an unlock lifts the refusals.
TEST_F(DockLayoutOperationsTest, LockRefusesADragOfThatMainWindowsWidgetsAndNoOthers)
{
    auto* other = new MainWindow(QStringLiteral("layout.other"), {}, m_window.get());
    const DockTarget here{.mainWindow = m_area, .workspaceId = QStringLiteral("here")};
    const DockTarget there{.mainWindow = other, .workspaceId = QStringLiteral("there")};

    m_core.dockLayout().lock(here);

    EXPECT_TRUE(m_core.dockLayout().isLocked(here));
    EXPECT_FALSE(m_core.dockLayout().isLocked(there));
    EXPECT_FALSE(DockHooks::allowsDrag(DockDrag{.mainWindow = m_area}));
    EXPECT_TRUE(DockHooks::allowsDrag(DockDrag{.mainWindow = other}));
    EXPECT_TRUE(DockHooks::allowsDrag(DockDrag{.mainWindow = nullptr}));
    EXPECT_FALSE(DockHooks::allowsDrop(DockDrop{.location = KDDockWidgets::DropLocation_Left, .mainWindow = m_area}));
    EXPECT_TRUE(DockHooks::allowsDrop(DockDrop{.location = KDDockWidgets::DropLocation_Left, .mainWindow = other}));
    EXPECT_FALSE(DockHooks::dropIndicatorsInhibited());

    m_core.dockLayout().unlock(here);

    EXPECT_FALSE(m_core.dockLayout().isLocked(here));
    EXPECT_TRUE(DockHooks::allowsDrag(DockDrag{.mainWindow = m_area}));
    EXPECT_TRUE(DockHooks::allowsDrop(DockDrop{.location = KDDockWidgets::DropLocation_Left, .mainWindow = m_area}));
}

// The lock of a shell's only dock area is the whole layout's, as the layout lock always was: every drag is refused -
// a floating window's too - and the indicators are inhibited, which the decorations read to keep a double click from
// floating or docking a view.
TEST_F(DockLayoutOperationsTest, LockingTheOnlyDockAreaLocksTheWholeLayout)
{
    m_core.dockLayout().lock(onlyArea());

    EXPECT_FALSE(DockHooks::allowsDrag(DockDrag{.mainWindow = m_area}));
    EXPECT_FALSE(DockHooks::allowsDrag(DockDrag{.mainWindow = nullptr}));
    EXPECT_FALSE(
        DockHooks::allowsDrop(DockDrop{.location = KDDockWidgets::DropLocation_Center, .mainWindow = nullptr}));
    EXPECT_TRUE(DockHooks::dropIndicatorsInhibited());
    EXPECT_TRUE(KDDockWidgets::Config::self().dropIndicatorsInhibited());

    m_core.dockLayout().unlock(onlyArea());

    EXPECT_TRUE(DockHooks::allowsDrag(DockDrag{.mainWindow = nullptr}));
    EXPECT_FALSE(DockHooks::dropIndicatorsInhibited());
}

// The lock offers Unlock Layout in place of Lock Layout, and the unlock the other way round: the actions' state the
// table of the action state leaves to the lock.
TEST_F(DockLayoutOperationsTest, LockOffersUnlockInPlaceOfLock)
{
    QAction* const lockAction = m_core.actions().action(ids::kLockLayout);
    QAction* const unlockAction = m_core.actions().action(ids::kUnlockLayout);
    ASSERT_NE(lockAction, nullptr);
    ASSERT_NE(unlockAction, nullptr);

    m_core.dockLayout().lock(onlyArea());
    EXPECT_FALSE(lockAction->isEnabled());
    EXPECT_TRUE(unlockAction->isEnabled());

    m_core.dockLayout().unlock(onlyArea());
    EXPECT_TRUE(lockAction->isEnabled());
    EXPECT_FALSE(unlockAction->isEnabled());
}

// Operations that go take their locks with them: nothing they locked stays refused.
TEST_F(DockLayoutOperationsTest, OperationsThatGoTakeTheirLocksWithThem)
{
    {
        DockLayoutOperations operations(m_core);
        operations.lock(onlyArea());
        ASSERT_FALSE(DockHooks::allowsDrag(DockDrag{.mainWindow = m_area}));
    }
    EXPECT_TRUE(DockHooks::allowsDrag(DockDrag{.mainWindow = m_area}));
    EXPECT_FALSE(DockHooks::dropIndicatorsInhibited());
}
