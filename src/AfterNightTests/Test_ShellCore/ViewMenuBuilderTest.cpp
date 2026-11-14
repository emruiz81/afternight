// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The view's menus: the context menu a view shows on a right click, entry for entry the record of 1.1's - the menus it
/// shares with the menu bar, Current Window, Show In File Browser - with Group and Split withdrawn for a maximized
/// view; and the menu of a view's tab, which acts on the tab's view: the file actions of the catalogue over it made
/// active, its tab group closed, its window floated.

#include "ShellCoreRecords.h"
#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/MenuTree.h"
#include "actions/ViewMenuBuilder.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "views/ViewService.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"

#include "fixtures/BaselineRecord.h"

#include <gtest/gtest.h>

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>

#include <QAction>
#include <QCoreApplication>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QWidget>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::documentNamed;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::shell::ApplicationCore;
using an::ui::shell::MenuTree;
using MainWindow = KDDockWidgets::QtWidgets::MainWindow;
namespace ids = an::ui::shell::ids;

void settleDocks()
{
    an::tests::shellcore::settle();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

/// An entry of a menu by the text it shows, or null.
QAction* entryNamed(const QMenu& menu, const QString& text)
{
    for (QAction* entry : menu.actions())
    {
        if (entry->text() == text)
        {
            return entry;
        }
        if (entry->menu() != nullptr)
        {
            if (QAction* const inside = entryNamed(*entry->menu(), text))
            {
                return inside;
            }
        }
    }
    return nullptr;
}

class ViewMenuBuilderTest : public ::testing::Test
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

        // The shell's menu bar, whose menus the view's menu shares.
        MenuTree::menuBar().buildMenuBar(m_bar, m_core.actions().lookup());

        m_window = std::make_unique<QWidget>();
        m_window->resize(900, 600);
        m_area = new MainWindow(QStringLiteral("menus.area"), {}, m_window.get());
        m_area->setGeometry(QRect(0, 0, 900, 600));
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

    /// A listed view of a new document named `name`, docked in the area beside the others or, given `tabOf`, as a tab
    /// of that view; the case deletes it.
    ImageViewControl* view(const QString& name, ImageViewControl* tabOf = nullptr)
    {
        std::optional<an::ui::ImageDocument> document = documentNamed(m_session->engineSession(), name);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        an::ui::IViewFactory& factory = m_core.viewService();
        ImageViewControl* const made = factory.createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "made", ViewControlMode::standard, nullptr, false, false);
        EXPECT_NE(made, nullptr);
        if (made == nullptr)
        {
            return nullptr;
        }
        m_views.emplace_back(made);
        m_docks.emplace_back(made->getDockWidget());
        if (tabOf != nullptr)
        {
            tabOf->getDockWidget()->addDockWidgetAsTab(made->getDockWidget());
        }
        else
        {
            m_area->addDockWidget(made->getDockWidget(), KDDockWidgets::Location_OnRight);
        }
        settleDocks();
        return made;
    }

    /// The shell's menus by id: the menus of its menu bar.
    [[nodiscard]] an::ui::shell::ViewMenuBuilder::MenuLookup menus()
    {
        QMenuBar* const bar = &m_bar;
        return [bar](std::string_view id) {
            return bar->findChild<QMenu*>(QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size())));
        };
    }

    [[nodiscard]] QList<QAction*> declared()
    {
        QList<QAction*> actions;
        for (QAction* action : m_core.actions().all())
        {
            actions.append(action);
        }
        return actions;
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    RecordingShell m_shell;
    std::unique_ptr<SessionProvider> m_session;
    ApplicationCore m_core;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_context;
    QMenuBar m_bar;
    std::unique_ptr<QWidget> m_window;
    MainWindow* m_area = nullptr;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
};

} // namespace

// The menu a view shows is the record's tree, entry for entry: the menus it shares with the menu bar by name, Current
// Window with its three entries, a separator and Show In File Browser.
TEST_F(ViewMenuBuilderTest, EqualsTheRecordedTree)
{
    ImageViewControl* const shown = view(QStringLiteral("shown"));
    ASSERT_NE(shown, nullptr);

    QMenu menu;
    an::ui::shell::ViewMenuBuilder(m_core).buildViewMenu(menu, *shown, menus());

    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::shellcore::kViewMenuRecordPath), {});
    EXPECT_TRUE(
        record.matches(QStringLiteral("view menu"), an::tests::shellcore::viewMenuEntries(menu, m_bar, declared())));
}

// Group All Images and Split All Images are withdrawn while the view is maximized, and offered again once it is not;
// Fit All Images to Window stays offered.
TEST_F(ViewMenuBuilderTest, WithdrawsGroupAndSplitForAMaximizedView)
{
    ImageViewControl* const shown = view(QStringLiteral("shown"));
    ASSERT_NE(shown, nullptr);
    shown->switchMaximizedExternal();
    ASSERT_TRUE(shown->isMaximized());

    QMenu maximized;
    an::ui::shell::ViewMenuBuilder(m_core).buildViewMenu(maximized, *shown, menus());
    ASSERT_NE(entryNamed(maximized, QStringLiteral("Group All Images")), nullptr);
    EXPECT_FALSE(entryNamed(maximized, QStringLiteral("Group All Images"))->isEnabled());
    EXPECT_FALSE(entryNamed(maximized, QStringLiteral("Split All Images"))->isEnabled());
    EXPECT_TRUE(entryNamed(maximized, QStringLiteral("Fit All Images to Window"))->isEnabled());

    shown->switchMaximizedExternal();
    QMenu restored;
    an::ui::shell::ViewMenuBuilder(m_core).buildViewMenu(restored, *shown, menus());
    EXPECT_TRUE(entryNamed(restored, QStringLiteral("Group All Images"))->isEnabled());
    EXPECT_TRUE(entryNamed(restored, QStringLiteral("Split All Images"))->isEnabled());
}

// Current Window's entries act on the views that share the view's window: Group All Images makes them one tab group.
TEST_F(ViewMenuBuilderTest, CurrentWindowGroupsTheViewsSharingTheViewsWindow)
{
    ImageViewControl* const first = view(QStringLiteral("first"));
    ImageViewControl* const second = view(QStringLiteral("second"));
    ASSERT_NE(second, nullptr);
    ASSERT_NE(first->getDockWidget()->group(), second->getDockWidget()->group());

    QMenu menu;
    an::ui::shell::ViewMenuBuilder(m_core).buildViewMenu(menu, *first, menus());
    QAction* const group = entryNamed(menu, QStringLiteral("Group All Images"));
    ASSERT_NE(group, nullptr);
    group->trigger();
    settleDocks();

    EXPECT_EQ(first->getDockWidget()->group(), second->getDockWidget()->group());
}

// The menu of a view's tab: Close, Close All Others, Close Tab Group; Float; Duplicate, Rename, Save File...; Show In
// File Browser - the catalogue's actions where the catalogue has one, each part apart.
TEST_F(ViewMenuBuilderTest, TabMenuOffersTheTabsEntriesInOrder)
{
    ImageViewControl* const tab = view(QStringLiteral("tab"));
    ASSERT_NE(tab, nullptr);

    QMenu menu;
    an::ui::shell::ViewMenuBuilder(m_core).buildTabMenu(menu, *tab);

    QStringList entries;
    for (const QAction* entry : menu.actions())
    {
        entries.append(entry->isSeparator()            ? QStringLiteral("separator")
                       : entry->objectName().isEmpty() ? QStringLiteral("entry ") + entry->text()
                                                       : QStringLiteral("action ") + entry->objectName());
    }
    EXPECT_EQ(entries,
              (QStringList{QStringLiteral("action actionClose"), QStringLiteral("action actionClose_All_Others"),
                           QStringLiteral("entry Close Tab Group"), QStringLiteral("separator"),
                           QStringLiteral("entry Float"), QStringLiteral("separator"),
                           QStringLiteral("action actionDuplicate"), QStringLiteral("action actionRename"),
                           QStringLiteral("action actionSave_File"), QStringLiteral("separator"),
                           QStringLiteral("action actionShow_In_File_Browser")}))
        << entries.join(QStringLiteral(", ")).toStdString();
}

// The catalogue's actions act on the active view: the tab's menu makes its view the active one.
TEST_F(ViewMenuBuilderTest, TabMenuMakesItsViewTheActiveOne)
{
    ImageViewControl* const tab = view(QStringLiteral("tab"));
    ImageViewControl* const other = view(QStringLiteral("other"));
    ASSERT_NE(other, nullptr);
    m_core.viewService().setActiveImageViewControl(other);

    QMenu menu;
    an::ui::shell::ViewMenuBuilder(m_core).buildTabMenu(menu, *tab);

    EXPECT_EQ(m_core.viewService().getActiveImageViewControl(), tab);
}

// Float takes the tab's view out of its dock area into a window of its own.
TEST_F(ViewMenuBuilderTest, TabMenuFloatsItsView)
{
    ImageViewControl* const tab = view(QStringLiteral("tab"));
    ASSERT_NE(tab, nullptr);
    ASSERT_FALSE(tab->getDockWidget()->isFloating());

    QMenu menu;
    an::ui::shell::ViewMenuBuilder(m_core).buildTabMenu(menu, *tab);
    QAction* const floatEntry = entryNamed(menu, QStringLiteral("Float"));
    ASSERT_NE(floatEntry, nullptr);
    floatEntry->trigger();
    settleDocks();

    EXPECT_TRUE(tab->getDockWidget()->isFloating());
}

// Close Tab Group closes each view of the tab's group as Close does - each made active, then closed - and no other.
TEST_F(ViewMenuBuilderTest, TabMenuClosesEachViewOfItsTabGroup)
{
    ImageViewControl* const tab = view(QStringLiteral("tab"));
    ImageViewControl* const sibling = view(QStringLiteral("sibling"), tab);
    ImageViewControl* const apart = view(QStringLiteral("apart"));
    ASSERT_NE(apart, nullptr);
    ASSERT_EQ(tab->getDockWidget()->group(), sibling->getDockWidget()->group());

    QStringList closed;
    QAction* const close = m_core.actions().action(ids::kClose);
    ASSERT_NE(close, nullptr);
    an::ui::shell::ViewService* const views = &m_core.viewService();
    const QMetaObject::Connection noted = QObject::connect(close, &QAction::triggered, close, [&closed, views]() {
        ImageViewControl* const active = views->getActiveImageViewControl();
        closed.append(active != nullptr ? QString::fromStdString(active->getUniqueName()) : QStringLiteral("none"));
    });

    QMenu menu;
    an::ui::shell::ViewMenuBuilder(m_core).buildTabMenu(menu, *tab);
    QAction* const closeGroup = entryNamed(menu, QStringLiteral("Close Tab Group"));
    ASSERT_NE(closeGroup, nullptr);
    closeGroup->trigger();
    QObject::disconnect(noted);

    closed.sort();
    QStringList expected{QString::fromStdString(tab->getUniqueName()),
                         QString::fromStdString(sibling->getUniqueName())};
    expected.sort();
    EXPECT_EQ(closed, expected);
}
