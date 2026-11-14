// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The menu tree: the 1.1 menu bar declared as data, which builds a menu bar of the catalogue's actions equal to the
/// record of 1.1's, builds one menu of it alone, and says which menus hold an action.

#include "ShellCoreRecords.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/MenuTree.h"

#include "fixtures/BaselineRecord.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace
{

using an::tests::shellcore::kActionRecordPath;
using an::tests::shellcore::menuBarEntries;
using an::ui::shell::ActionCatalogue;
using an::ui::shell::MenuTree;
namespace ids = an::ui::shell::ids;

[[nodiscard]] QList<QAction*> declaredBy(const ActionCatalogue& catalogue)
{
    QList<QAction*> declared;
    for (QAction* action : catalogue.all())
    {
        declared.append(action);
    }
    return declared;
}

} // namespace

// The menu bar the tree builds over the catalogue is the record's: every menu, action and separator in the order a
// person reads them, the menus that follow a person's history built empty.
TEST(MenuTree, EqualsTheRecordedMenuBar)
{
    QObject owner;
    const ActionCatalogue catalogue(owner);
    QMenuBar bar;
    MenuTree::menuBar().buildMenuBar(bar, catalogue.lookup());

    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(kActionRecordPath), {});
    EXPECT_TRUE(record.matches(QStringLiteral("menu bar"), menuBarEntries(bar, declaredBy(catalogue))));
}

// One menu built alone is the same tree the menu bar holds under its name; a name the tree does not hold builds
// nothing.
TEST(MenuTree, BuildsOneMenuAlone)
{
    QObject owner;
    const ActionCatalogue catalogue(owner);
    QMenuBar bar;
    MenuTree::menuBar().buildMenuBar(bar, catalogue.lookup());
    QMenu* inBar = bar.findChild<QMenu*>(QStringLiteral("menuImage"));
    ASSERT_NE(inBar, nullptr);

    QMenu alone;
    ASSERT_TRUE(MenuTree::menuBar().buildMenu(alone, "menuImage", catalogue.lookup()));
    EXPECT_EQ(alone.objectName(), QStringLiteral("menuImage"));
    EXPECT_EQ(alone.title(), QStringLiteral("Image"));

    QStringList barTree;
    QStringList aloneTree;
    an::tests::shellcore::appendMenuTree(inBar->actions(), 0, {}, barTree);
    an::tests::shellcore::appendMenuTree(alone.actions(), 0, {}, aloneTree);
    EXPECT_EQ(aloneTree, barTree);
    EXPECT_FALSE(aloneTree.isEmpty());

    QMenu none;
    EXPECT_FALSE(MenuTree::menuBar().buildMenu(none, "menuThatIsNoMenu", catalogue.lookup()));
    EXPECT_TRUE(none.actions().isEmpty());
}

// The tree says which menus hold an action, by the titles a person reads down to it: one menu, two, or none.
TEST(MenuTree, SaysWhichMenusHoldAnAction)
{
    const MenuTree& tree = MenuTree::menuBar();
    EXPECT_EQ(tree.pathsOf(ids::kOpen), std::vector<std::string>{"File"});
    EXPECT_EQ(tree.pathsOf(ids::kOverlayRed), std::vector<std::string>{"Image > Mask > Rendering Mode"});
    EXPECT_EQ(tree.pathsOf(ids::kGaussianBlur), (std::vector<std::string>{"Process > Details", "Process > Filters"}));
    EXPECT_TRUE(tree.pathsOf(ids::kAsd).empty());
    EXPECT_EQ(tree.topLevel(),
              (std::vector<std::string_view>{"menuFile", "menuEdit", "menuView_2", "menuImage", "menuProcess",
                                             "menuExtensions", "menuWindow", "menuHelp", "menuAbout"}));
}

// The menus 1.1's window made disabled start disabled: Open Recent and Recent Processes until there is something to
// list, and the Window menu.
TEST(MenuTree, BuildsTheMenusAsTheWindowStartedThem)
{
    QObject owner;
    const ActionCatalogue catalogue(owner);
    QMenuBar bar;
    MenuTree::menuBar().buildMenuBar(bar, catalogue.lookup());
    for (const char* name : {"menuOpen_Recent", "menuRecent_Processes", "menuWindow"})
    {
        QMenu* const menu = bar.findChild<QMenu*>(QString::fromLatin1(name));
        ASSERT_NE(menu, nullptr) << name;
        EXPECT_FALSE(menu->isEnabled()) << name;
    }
    for (const char* name : {"menuFile", "menuFavorite_Processes", "menuMask", "menuProcess"})
    {
        QMenu* const menu = bar.findChild<QMenu*>(QString::fromLatin1(name));
        ASSERT_NE(menu, nullptr) << name;
        EXPECT_TRUE(menu->isEnabled()) << name;
    }
}
