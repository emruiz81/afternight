// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The action catalogue: the actions of the 1.1 interface, made from one table, each once under its object name, with
/// the text, tool tip, shortcut, icon and checkability the record of 1.1 holds; a group lists its actions in the
/// catalogue's order; and a shell's window hosts them while it lives and gives them back.

#include "ShellCoreRecords.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/MenuTree.h"

#include "fixtures/BaselineRecord.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{

using an::tests::shellcore::kActionRecordPath;
using an::tests::shellcore::recordedRows;
using an::ui::shell::ActionCatalogue;
using an::ui::shell::ActionGroup;
using an::ui::shell::ActionSpec;
using an::ui::shell::MenuTree;
namespace ids = an::ui::shell::ids;

[[nodiscard]] QString contextName(const Qt::ShortcutContext context)
{
    switch (context)
    {
    case Qt::WidgetShortcut:
        return QStringLiteral("widget");
    case Qt::WidgetWithChildrenShortcut:
        return QStringLiteral("widget and children");
    case Qt::WindowShortcut:
        return QStringLiteral("window");
    case Qt::ApplicationShortcut:
        return QStringLiteral("application");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString nameOf(std::string_view id)
{
    return QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

} // namespace

// The catalogue's actions are the record's, entry for entry and in its order: the object name, the text, the tool tip,
// the shortcut and its context, the icon, whether it can be checked, and the menus of the menu tree that hold it.
TEST(ActionCatalogue, HoldsTheRecordedInventory)
{
    QObject owner;
    const ActionCatalogue catalogue(owner);

    QStringList entries;
    for (QAction* action : catalogue.all())
    {
        const ActionSpec* spec = ActionCatalogue::specOf(action->objectName().toStdString());
        ASSERT_NE(spec, nullptr) << action->objectName().toStdString();
        QStringList paths;
        for (const std::string& path : MenuTree::menuBar().pathsOf(spec->id))
        {
            paths.append(QString::fromStdString(path));
        }
        entries.append(an::tests::fixtures::baselineEntry({
            action->objectName(),
            action->text(),
            action->toolTip(),
            action->shortcut().toString(QKeySequence::PortableText),
            contextName(action->shortcutContext()),
            QString::fromLatin1(spec->iconKey),
            action->isCheckable() ? QStringLiteral("checkable") : QStringLiteral("plain"),
            paths.join(QStringLiteral("; ")),
        }));
    }

    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(kActionRecordPath), {});
    EXPECT_TRUE(record.matches(QStringLiteral("actions"), entries));
}

// Each id has one action, made once, named by the id and asked for by it every time; the catalogue answers no action
// for a name it does not hold, and its lookup is the catalogue.
TEST(ActionCatalogue, OneObjectPerId)
{
    QObject owner;
    const ActionCatalogue catalogue(owner);

    std::set<const QAction*> objects;
    std::set<std::string> names;
    for (const ActionSpec& spec : ActionCatalogue::specs())
    {
        QAction* const action = catalogue.action(spec.id);
        ASSERT_NE(action, nullptr) << spec.id;
        EXPECT_EQ(action->objectName(), nameOf(spec.id));
        EXPECT_EQ(catalogue.action(spec.id), action);
        EXPECT_EQ(action->parent(), &owner) << spec.id;
        EXPECT_TRUE(objects.insert(action).second) << "two ids share the action of " << spec.id;
        EXPECT_TRUE(names.insert(std::string(spec.id)).second) << spec.id << " is listed twice";
    }
    EXPECT_EQ(objects.size(), 186u);
    EXPECT_EQ(catalogue.all().size(), 186u);
    EXPECT_EQ(owner.findChildren<QAction*>(QString(), Qt::FindDirectChildrenOnly).size(), 186);
    EXPECT_EQ(catalogue.action("actionThatIsNoAction"), nullptr);
    EXPECT_EQ(catalogue.lookup()(ids::kOpen), catalogue.action(ids::kOpen));
    EXPECT_EQ(catalogue.lookup()("actionThatIsNoAction"), nullptr);

    // Another catalogue makes actions of its own.
    QObject other;
    const ActionCatalogue second(other);
    EXPECT_NE(second.action(ids::kOpen), catalogue.action(ids::kOpen));
}

// The actions start as 1.1's window made them: the image's actions that need an image offered disabled, the red overlay
// checked, the user guide answering from every window.
TEST(ActionCatalogue, StartsAsTheWindowMadeThem)
{
    QObject owner;
    const ActionCatalogue catalogue(owner);

    for (const std::string_view id :
         {ids::kUndo, ids::kRedo, ids::kShowInFileBrowser, ids::kImageHistory, ids::kStatistics, ids::kHistogram,
          ids::kImageEXIFViewer, ids::kFITSHeader, ids::kAstrometryInfo, ids::kPlateSolve, ids::kAnnotateImage})
    {
        ASSERT_NE(catalogue.action(id), nullptr) << id;
        EXPECT_FALSE(catalogue.action(id)->isEnabled()) << id;
    }
    EXPECT_TRUE(catalogue.action(ids::kOpen)->isEnabled());
    EXPECT_TRUE(catalogue.action(ids::kOverlayRed)->isChecked());
    EXPECT_FALSE(catalogue.action(ids::kOverlayGreen)->isChecked());
    EXPECT_EQ(catalogue.action(ids::kUserGuide)->shortcutContext(), Qt::ApplicationShortcut);
    EXPECT_EQ(catalogue.action(ids::kUserGuide)->statusTip(), QStringLiteral("Open the AfterNight User Guide."));
    EXPECT_EQ(catalogue.action(ids::kManageExtensions)->whatsThis(),
              QStringLiteral("Install, update, remove, and configure extensions."));
}

// A group lists its actions in the catalogue's order: the File group is the File menu's actions, Help the Help and
// About menus', and the actions no menu holds are a group of their own.
TEST(ActionCatalogue, ListsAGroupInTheCataloguesOrder)
{
    QObject owner;
    const ActionCatalogue catalogue(owner);

    const auto namesIn = [&catalogue](const ActionGroup group) {
        QStringList names;
        for (const QAction* action : catalogue.inGroup(group))
        {
            names.append(action->objectName());
        }
        return names;
    };
    EXPECT_EQ(namesIn(ActionGroup::File),
              (QStringList{QStringLiteral("actionOpen"), QStringLiteral("actionSave_as"), QStringLiteral("actionExit"),
                           QStringLiteral("actionClose"), QStringLiteral("actionClose_All"),
                           QStringLiteral("actionClose_All_Others"), QStringLiteral("actionNew_Sequence")}));
    EXPECT_EQ(namesIn(ActionGroup::Help),
              (QStringList{QStringLiteral("actionAbout_Qt"), QStringLiteral("actionAbout_AfterNight"),
                           QStringLiteral("actionSystem_Information"), QStringLiteral("actionUser_Guide")}));
    EXPECT_EQ(namesIn(ActionGroup::Window),
              (QStringList{QStringLiteral("actionLock_Layout"), QStringLiteral("actionUnlock_Layout"),
                           QStringLiteral("actionFit_Images_To_Window"), QStringLiteral("actionGroup_All_Images"),
                           QStringLiteral("actionSplit_All_Images")}));
    EXPECT_EQ(
        namesIn(ActionGroup::None),
        (QStringList{QStringLiteral("action"), QStringLiteral("actionAutomatic_MTF_Transform"),
                     QStringLiteral("actionasd"), QStringLiteral("actionasd_2"), QStringLiteral("actionFilters")}));

    std::size_t grouped = 0u;
    for (const ActionGroup group :
         {ActionGroup::File, ActionGroup::Edit, ActionGroup::View, ActionGroup::Image, ActionGroup::Process,
          ActionGroup::Extensions, ActionGroup::Window, ActionGroup::Help, ActionGroup::None})
    {
        grouped += catalogue.inGroup(group).size();
    }
    EXPECT_EQ(grouped, catalogue.all().size()) << "every action is in one group";
}

// A shell's window hosts the actions while it lives - they are its children, in the catalogue's order, as the actions
// of 1.1's window were - and gives them back as it goes: the catalogue keeps every one.
TEST(ActionCatalogue, AWindowHostsTheActionsAndGivesThemBack)
{
    QObject owner;
    ActionCatalogue catalogue(owner);
    const std::vector<QAction*> actions = catalogue.all();
    {
        QWidget window;
        catalogue.host(window);
        const QList<QAction*> held = window.findChildren<QAction*>(QString(), Qt::FindDirectChildrenOnly);
        ASSERT_EQ(static_cast<std::size_t>(held.size()), actions.size());
        for (std::size_t index = 0u; index < actions.size(); ++index)
        {
            EXPECT_EQ(held[static_cast<qsizetype>(index)], actions[index]) << index;
        }
        EXPECT_EQ(catalogue.holder(), &window);
        catalogue.release();
        EXPECT_EQ(catalogue.holder(), &owner);
    }
    ASSERT_EQ(catalogue.all().size(), actions.size()) << "the window took actions with it";
    for (QAction* action : catalogue.all())
    {
        EXPECT_EQ(action->parent(), &owner) << action->objectName().toStdString();
    }
}

// A window that goes while it still hosts the actions takes them with it, and the catalogue then answers none rather
// than one that is gone.
TEST(ActionCatalogue, AWindowThatGoesHostingTakesTheActionsWithIt)
{
    QObject owner;
    ActionCatalogue catalogue(owner);
    QPointer<QAction> open = catalogue.action(ids::kOpen);
    {
        QWidget window;
        catalogue.host(window);
    }
    EXPECT_TRUE(open.isNull());
    EXPECT_EQ(catalogue.action(ids::kOpen), nullptr);
    EXPECT_TRUE(catalogue.all().empty());
}
