// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The command index: every command of the application - the catalogue's actions, the processes, the open views and
/// the recent files, the settings pages and their rows - searched as a person types, with no widget. A query ranks an
/// exact prefix over a word prefix over a subsequence, breaks ties by recency, is narrowed by `>`, `@` and `:`, and
/// shows its groups in their order, six rows each until a header is clicked; an empty query lists the favourites and
/// the commands run last. A command that cannot run says why and does not run. The commands run last are kept, twenty
/// of them, across a restart.

#include "ShellCoreRecords.h"
#include "ShellCoreTestSupport.h"

#include "actions/ActionBindings.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "commands/CommandIndex.h"
#include "commands/CommandProviders.h"
#include "core/ApplicationCore.h"
#include "services/FavoriteProcessesModel.h"
#include "services/RecentFilesModel.h"

#include "app/ProcessRegistry.h"
#include "controls/settings/CatalogsDataSettingsPage.h"
#include "controls/settings/ExportSettingsPage.h"
#include "controls/settings/ExternalToolsSettingsPage.h"
#include "controls/settings/GeneralSettingsPage.h"
#include "controls/settings/InterfaceSettingsPage.h"
#include "controls/settings/LoggingSettingsPage.h"
#include "controls/settings/PlateSolvingSettingsPage.h"
#include "controls/settings/ProcessingSettingsPage.h"
#include "controls/settings/ProjectsSettingsPage.h"
#include "controls/settings/RawImageSettingsPage.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsPage.h"
#include "controls/settings/SettingsSearchTable.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{

using an::tests::shellcore::kActionRecordPath;
using an::tests::shellcore::kProcessRecordPath;
using an::tests::shellcore::recordedRows;
using an::tests::shellcore::ScopedStorageRoot;
using an::ui::CommandRow;
using an::ui::CommandRowGroup;
using an::ui::SettingsSearchPage;
using an::ui::SettingsSearchRow;
using an::ui::process_registry::LaunchGuardFailureReason;
using an::ui::process_registry::LaunchGuardResult;
using an::ui::process_registry::ProcessAvailability;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessLaunchMode;
using an::ui::process_registry::ProcessRegistry;
using an::ui::shell::ActionCommands;
using an::ui::shell::ApplicationCore;
using an::ui::shell::CommandEntry;
using an::ui::shell::CommandGroup;
using an::ui::shell::ICommandProvider;
using an::ui::shell::ImageCommands;
using an::ui::shell::ProcessCommands;
using an::ui::shell::SettingsCommands;
namespace ids = an::ui::shell::ids;

/// The product's index; the suite's fixture is named after the group of its cases.
using Index = an::ui::shell::CommandIndex;

/// A provider of a fixed list.
class FixedCommands final : public ICommandProvider
{
public:
    explicit FixedCommands(std::vector<CommandEntry> entries) : m_entries(std::move(entries)) {}

    void collect(std::vector<CommandEntry>& entries) const override
    {
        entries.insert(entries.end(), m_entries.begin(), m_entries.end());
    }

private:
    std::vector<CommandEntry> m_entries;
};

/// The titles of a group's rows, in order.
QStringList titlesOf(const CommandRowGroup& group)
{
    QStringList titles;
    for (const CommandRow& row : group.rows)
    {
        titles.append(row.title);
    }
    return titles;
}

/// The ids of a query's groups, in order.
QStringList groupIdsOf(const std::vector<CommandRowGroup>& groups)
{
    QStringList idsOfGroups;
    for (const CommandRowGroup& group : groups)
    {
        idsOfGroups.append(group.id);
    }
    return idsOfGroups;
}

/// The row of an id among a query's groups; null for none.
const CommandRow* rowOf(const std::vector<CommandRowGroup>& groups, const QString& id)
{
    for (const CommandRowGroup& group : groups)
    {
        for (const CommandRow& row : group.rows)
        {
            if (row.id == id)
            {
                return &row;
            }
        }
    }
    return nullptr;
}

/// The group of an id among a query's groups; null for none.
const CommandRowGroup* groupOf(const std::vector<CommandRowGroup>& groups, const QString& id)
{
    const auto found =
        std::find_if(groups.begin(), groups.end(), [&id](const CommandRowGroup& group) { return group.id == id; });
    return found == groups.end() ? nullptr : &*found;
}

/// What a person types to find a text: the text without a menu's mnemonic marks.
QString typed(QString text)
{
    text.remove(QLatin1Char('&'));
    return text;
}

class CommandIndex : public ::testing::Test
{
protected:
    /// A command of a group that records its runs in the case.
    CommandEntry command(const QString& id, const QString& title, const CommandGroup group = CommandGroup::Processes,
                         const QStringList& keywords = {})
    {
        CommandEntry entry;
        entry.id = id;
        entry.title = title;
        entry.subtitle = QStringLiteral("subtitle of ") + id;
        entry.group = group;
        entry.keywords = keywords;
        entry.run = [this, id]() {
            m_runs.append(id);
            return true;
        };
        return entry;
    }

    /// An index of one provider holding `entries`, refreshed.
    std::unique_ptr<Index> indexOf(std::vector<CommandEntry> entries)
    {
        auto index = std::make_unique<Index>();
        index->addProvider(std::make_unique<FixedCommands>(std::move(entries)));
        index->refresh();
        return index;
    }

    QStringList m_runs;
};

// A text finds a command whose title starts with it first, then one each of whose words it starts - a keyword's
// included - then one whose title holds its letters in order; case is ignored, and a command it does not reach is not
// listed.
TEST_F(CommandIndex, RanksAnExactPrefixThenAWordPrefixThenASubsequence)
{
    const auto index = indexOf({
        command(QStringLiteral("subsequence"), QStringLiteral("Hide Stars")),
        command(QStringLiteral("keyword"), QStringLiteral("Stretch"), CommandGroup::Processes,
                {QStringLiteral("Histogram")}),
        command(QStringLiteral("word"), QStringLiteral("CLA Histogram Equalization")),
        command(QStringLiteral("none"), QStringLiteral("Show Info")),
        command(QStringLiteral("prefix"), QStringLiteral("Histogram Transformation")),
    });

    const std::vector<CommandRowGroup> groups = index->query(QStringLiteral("HIST"));
    ASSERT_EQ(groupIdsOf(groups), QStringList{QStringLiteral("processes")});
    EXPECT_EQ(titlesOf(groups.front()),
              (QStringList{QStringLiteral("Histogram Transformation"), QStringLiteral("CLA Histogram Equalization"),
                           QStringLiteral("Stretch"), QStringLiteral("Hide Stars")}));

    const CommandEntry* const prefix = index->entry(QStringLiteral("prefix"));
    ASSERT_NE(prefix, nullptr);
    EXPECT_EQ(Index::match(*prefix, QStringLiteral("histogram tr")), Index::Match::Prefix);
    EXPECT_EQ(Index::match(*prefix, QStringLiteral("hist trans")), Index::Match::WordPrefix);
    EXPECT_EQ(Index::match(*prefix, QStringLiteral("trans hist")), Index::Match::None) << "words in their order";
    EXPECT_EQ(Index::match(*prefix, QStringLiteral("htf")), Index::Match::Subsequence);
    EXPECT_EQ(Index::match(*prefix, QStringLiteral("xyz")), Index::Match::None);
    const CommandEntry* const word = index->entry(QStringLiteral("word"));
    ASSERT_NE(word, nullptr);
    EXPECT_EQ(Index::match(*word, QStringLiteral("hist eq")), Index::Match::WordPrefix);
}

// Commands that match alike are listed most recently run first, then the others by title.
TEST_F(CommandIndex, BreaksTiesByRecency)
{
    const auto index = indexOf({command(QStringLiteral("b"), QStringLiteral("Curves B")),
                                command(QStringLiteral("a"), QStringLiteral("Curves A")),
                                command(QStringLiteral("c"), QStringLiteral("Curves C")),
                                command(QStringLiteral("d"), QStringLiteral("Curves D"))});
    ASSERT_TRUE(index->run(QStringLiteral("c")));
    ASSERT_TRUE(index->run(QStringLiteral("b")));

    const std::vector<CommandRowGroup> groups = index->query(QStringLiteral("curves"));
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(titlesOf(groups.front()), (QStringList{QStringLiteral("Curves B"), QStringLiteral("Curves C"),
                                                     QStringLiteral("Curves A"), QStringLiteral("Curves D")}));
    EXPECT_EQ(m_runs, (QStringList{QStringLiteral("c"), QStringLiteral("b")}));
}

// `>` narrows a query to the processes, `@` to the images and `:` to the settings; the narrowed group is shown whole,
// and the prefix alone lists every command of it.
TEST_F(CommandIndex, ThePrefixesNarrowToOneGroup)
{
    std::vector<CommandEntry> entries;
    for (int number = 0; number < 8; ++number)
    {
        entries.push_back(command(QStringLiteral("p%1").arg(number), QStringLiteral("Star Process %1").arg(number)));
    }
    entries.push_back(command(QStringLiteral("image"), QStringLiteral("Star field.fits"), CommandGroup::Images));
    entries.push_back(command(QStringLiteral("view"), QStringLiteral("Star Mask Overlay"), CommandGroup::Views));
    entries.push_back(
        command(QStringLiteral("setting"), QStringLiteral("Plate Solving > Star Limit"), CommandGroup::Settings));
    const auto index = indexOf(std::move(entries));

    EXPECT_EQ(Index::parse(QStringLiteral(">star")),
              (Index::ParsedQuery{CommandGroup::Processes, QStringLiteral("star")}));
    EXPECT_EQ(Index::parse(QStringLiteral("  @ m20 ")),
              (Index::ParsedQuery{CommandGroup::Images, QStringLiteral("m20")}));
    EXPECT_EQ(Index::parse(QStringLiteral(":")), (Index::ParsedQuery{CommandGroup::Settings, QString()}));
    EXPECT_EQ(Index::parse(QStringLiteral("star")), (Index::ParsedQuery{std::nullopt, QStringLiteral("star")}));

    const std::vector<CommandRowGroup> all = index->query(QStringLiteral("star"));
    EXPECT_EQ(groupIdsOf(all), (QStringList{QStringLiteral("processes"), QStringLiteral("images"),
                                            QStringLiteral("views"), QStringLiteral("settings")}));

    const std::vector<CommandRowGroup> processes = index->query(QStringLiteral(">star"));
    ASSERT_EQ(groupIdsOf(processes), QStringList{QStringLiteral("processes")});
    EXPECT_EQ(processes.front().rows.size(), 8u);
    EXPECT_EQ(processes.front().limit, 0) << "a narrowed query shows its one group whole";

    const std::vector<CommandRowGroup> images = index->query(QStringLiteral("@star"));
    ASSERT_EQ(groupIdsOf(images), QStringList{QStringLiteral("images")});
    EXPECT_EQ(titlesOf(images.front()), QStringList{QStringLiteral("Star field.fits")});

    const std::vector<CommandRowGroup> settings = index->query(QStringLiteral(":star"));
    ASSERT_EQ(groupIdsOf(settings), QStringList{QStringLiteral("settings")});
    EXPECT_EQ(titlesOf(settings.front()), QStringList{QStringLiteral("Plate Solving > Star Limit")});

    const std::vector<CommandRowGroup> everyProcess = index->query(QStringLiteral(">"));
    ASSERT_EQ(groupIdsOf(everyProcess), QStringList{QStringLiteral("processes")});
    EXPECT_EQ(everyProcess.front().rows.size(), 8u);
    EXPECT_TRUE(index->query(QStringLiteral("@nothing")).empty());
}

// A typed query shows its groups in their order - processes, images, views, settings - each ranked and showing six
// rows until its header is clicked; a group with no match is left out.
TEST_F(CommandIndex, GroupsComeInTheirOrderEachShowingSixRows)
{
    std::vector<CommandEntry> entries;
    for (int number = 0; number < 8; ++number)
    {
        entries.push_back(command(QStringLiteral("s%1").arg(number), QStringLiteral("Alpha Setting %1").arg(number),
                                  CommandGroup::Settings));
        entries.push_back(command(QStringLiteral("p%1").arg(number), QStringLiteral("Alpha Process %1").arg(number)));
    }
    entries.push_back(command(QStringLiteral("v"), QStringLiteral("Alpha View"), CommandGroup::Views));
    entries.push_back(command(QStringLiteral("other"), QStringLiteral("Beta"), CommandGroup::Images));
    const auto index = indexOf(std::move(entries));

    const std::vector<CommandRowGroup> groups = index->query(QStringLiteral("alpha"));
    ASSERT_EQ(groupIdsOf(groups),
              (QStringList{QStringLiteral("processes"), QStringLiteral("views"), QStringLiteral("settings")}));
    EXPECT_EQ(groups[0].title, QStringLiteral("Processes"));
    EXPECT_EQ(groups[1].title, QStringLiteral("Views"));
    EXPECT_EQ(groups[2].title, QStringLiteral("Settings"));
    for (const CommandRowGroup& group : groups)
    {
        EXPECT_EQ(group.limit, Index::kGroupRowLimit) << group.id.toStdString();
    }
    EXPECT_EQ(Index::kGroupRowLimit, 6);
    EXPECT_EQ(groups[0].rows.size(), 8u) << "every match is in its group; the palette shows the first six";
    EXPECT_EQ(groups[0].rows.front().title, QStringLiteral("Alpha Process 0"));
    EXPECT_EQ(Index::groupTitle(CommandGroup::Images), QStringLiteral("Images"));
}

// With nothing typed the index lists the favourites, when there are any, by title, then the commands run last, most
// recent first and no more than eight.
TEST_F(CommandIndex, AnEmptyQueryListsTheFavouritesThenTheCommandsRunLast)
{
    std::vector<CommandEntry> entries;
    for (int number = 0; number < 10; ++number)
    {
        entries.push_back(command(QStringLiteral("c%1").arg(number), QStringLiteral("Command %1").arg(number)));
    }
    const auto quiet = indexOf(entries);
    EXPECT_TRUE(quiet->query(QString()).empty()) << "nothing favoured and nothing run lists nothing";

    CommandEntry zeta = command(QStringLiteral("zeta"), QStringLiteral("Zeta Favourite"));
    zeta.favorite = true;
    zeta.setFavorite = [](bool) {};
    CommandEntry alpha = command(QStringLiteral("alpha"), QStringLiteral("Alpha Favourite"));
    alpha.favorite = true;
    alpha.setFavorite = [](bool) {};
    entries.push_back(zeta);
    entries.push_back(alpha);
    const auto index = indexOf(entries);
    for (int number = 0; number < 10; ++number)
    {
        ASSERT_TRUE(index->run(QStringLiteral("c%1").arg(number)));
    }

    const std::vector<CommandRowGroup> groups = index->query(QString());
    ASSERT_EQ(groupIdsOf(groups), (QStringList{QStringLiteral("favourites"), QStringLiteral("recent")}));
    EXPECT_EQ(groups[0].title, QStringLiteral("Favourites"));
    EXPECT_EQ(titlesOf(groups[0]), (QStringList{QStringLiteral("Alpha Favourite"), QStringLiteral("Zeta Favourite")}));
    EXPECT_TRUE(groups[0].rows.front().favorite);
    EXPECT_EQ(groups[1].title, QStringLiteral("Recent"));
    ASSERT_EQ(groups[1].rows.size(), Index::kRecentShown);
    EXPECT_EQ(groups[1].rows.front().title, QStringLiteral("Command 9"));
    EXPECT_EQ(groups[1].rows.back().title, QStringLiteral("Command 2"));
    EXPECT_EQ(Index::kRecentShown, 8u);
}

// A command that cannot run now is listed dimmed, its reason for its subtitle, and running it runs nothing and
// records nothing.
TEST_F(CommandIndex, ACommandThatCannotRunIsDimmedWithItsReasonAndDoesNotRun)
{
    CommandEntry blocked = command(QStringLiteral("blocked"), QStringLiteral("Star Reduction"));
    blocked.available = false;
    blocked.unavailableReason = QStringLiteral("This extension must be installed before it can be launched.");
    const auto index = indexOf({blocked, command(QStringLiteral("open"), QStringLiteral("Star Alignment"))});

    const std::vector<CommandRowGroup> groups = index->query(QStringLiteral("star"));
    const CommandRow* const row = rowOf(groups, QStringLiteral("blocked"));
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->enabled);
    EXPECT_EQ(row->subtitle, blocked.unavailableReason);
    const CommandRow* const open = rowOf(groups, QStringLiteral("open"));
    ASSERT_NE(open, nullptr);
    EXPECT_TRUE(open->enabled);
    EXPECT_EQ(open->subtitle, QStringLiteral("subtitle of open"));

    EXPECT_FALSE(index->run(QStringLiteral("blocked")));
    EXPECT_FALSE(index->run(QStringLiteral("no such command")));
    EXPECT_TRUE(m_runs.isEmpty());
    EXPECT_TRUE(index->recentIds().empty());
}

// A provider removed takes its commands with it at once: a query finds none of them and an id of one runs nothing,
// while the other providers' commands stay; a provider the index does not hold changes nothing.
TEST_F(CommandIndex, ARemovedProviderTakesItsCommandsWithIt)
{
    Index index;
    index.addProvider(std::make_unique<FixedCommands>(
        std::vector<CommandEntry>{command(QStringLiteral("kept"), QStringLiteral("Kept Command"))}));
    auto owned = std::make_unique<FixedCommands>(
        std::vector<CommandEntry>{command(QStringLiteral("gone"), QStringLiteral("Gone Command"))});
    const FixedCommands* const removed = owned.get();
    index.addProvider(std::move(owned));
    index.refresh();
    ASSERT_NE(index.entry(QStringLiteral("gone")), nullptr);
    ASSERT_NE(rowOf(index.query(QStringLiteral("Command")), QStringLiteral("gone")), nullptr);

    const FixedCommands stranger({command(QStringLiteral("stranger"), QStringLiteral("Stranger"))});
    index.removeProvider(&stranger);
    EXPECT_NE(index.entry(QStringLiteral("gone")), nullptr) << "a provider the index does not hold changes nothing";
    EXPECT_NE(index.entry(QStringLiteral("kept")), nullptr);

    index.removeProvider(removed);
    EXPECT_EQ(index.entry(QStringLiteral("gone")), nullptr);
    EXPECT_EQ(rowOf(index.query(QStringLiteral("Command")), QStringLiteral("gone")), nullptr);
    EXPECT_FALSE(index.run(QStringLiteral("gone")));
    EXPECT_TRUE(m_runs.isEmpty());
    ASSERT_NE(index.entry(QStringLiteral("kept")), nullptr);
    EXPECT_NE(rowOf(index.query(QStringLiteral("Command")), QStringLiteral("kept")), nullptr);

    index.removeProvider(removed);
    EXPECT_NE(index.entry(QStringLiteral("kept")), nullptr) << "a provider removed twice is removed once";
}

// The commands run last are the last twenty, most recent first, each once; they are written to the store as they
// change and read back by another index over it, as the next start reads them.
TEST_F(CommandIndex, KeepsTheLastTwentyCommandsRunAndReadsThemBack)
{
    std::vector<CommandEntry> entries;
    for (int number = 0; number < 25; ++number)
    {
        entries.push_back(command(QStringLiteral("c%1").arg(number), QStringLiteral("Command %1").arg(number)));
    }
    QString stored;
    const Index::RecentStore store{[&stored]() { return stored; }, [&stored](const QString& ids) { stored = ids; }};
    const auto index = indexOf(entries);
    index->setStore(store);
    for (int number = 0; number < 25; ++number)
    {
        ASSERT_TRUE(index->run(QStringLiteral("c%1").arg(number)));
    }
    ASSERT_TRUE(index->run(QStringLiteral("c10")));

    ASSERT_EQ(index->recentIds().size(), Index::kRecentLimit);
    EXPECT_EQ(Index::kRecentLimit, 20u);
    EXPECT_EQ(index->recentIds().front(), QStringLiteral("c10"));
    EXPECT_EQ(index->recentIds()[1], QStringLiteral("c24"));
    EXPECT_EQ(std::count(index->recentIds().begin(), index->recentIds().end(), QStringLiteral("c10")), 1);
    EXPECT_EQ(std::find(index->recentIds().begin(), index->recentIds().end(), QStringLiteral("c4")),
              index->recentIds().end())
        << "the oldest went";
    EXPECT_EQ(stored.split(QLatin1Char('\n')).size(), 20);

    const auto next = indexOf(entries);
    next->setStore(store);
    next->loadRecents();
    EXPECT_EQ(next->recentIds(), index->recentIds());

    stored = QStringLiteral("a\nb\n\nb\n") + QStringList(30, QStringLiteral("x")).join(QLatin1Char('\n'));
    next->loadRecents();
    EXPECT_EQ(next->recentIds(), (std::deque<QString>{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("x")}))
        << "a stored list is read once each, without blanks";
}

// The star makes a favoritable command a favourite and a favourite no longer, telling the command; it does nothing to
// any other.
TEST_F(CommandIndex, TheStarTogglesAFavourite)
{
    std::vector<bool> told;
    CommandEntry process = command(QStringLiteral("process"), QStringLiteral("Curves Transformation"));
    process.setFavorite = [&told](const bool favorite) { told.push_back(favorite); };
    const auto index =
        indexOf({process, command(QStringLiteral("view"), QStringLiteral("Curves View"), CommandGroup::Views)});
    const CommandRow* const row = rowOf(index->query(QStringLiteral("curves")), QStringLiteral("process"));
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->favoritable);
    EXPECT_FALSE(row->favorite);

    index->toggleFavorite(QStringLiteral("process"));
    EXPECT_EQ(told, std::vector<bool>{true});
    const std::vector<CommandRowGroup> empty = index->query(QString());
    ASSERT_EQ(groupIdsOf(empty), QStringList{QStringLiteral("favourites")});
    EXPECT_EQ(titlesOf(empty.front()), QStringList{QStringLiteral("Curves Transformation")});

    index->toggleFavorite(QStringLiteral("process"));
    index->toggleFavorite(QStringLiteral("view"));
    EXPECT_EQ(told, (std::vector<bool>{true, false}));
    EXPECT_TRUE(index->query(QString()).empty());
    const CommandRow* const view = rowOf(index->query(QStringLiteral("curves")), QStringLiteral("view"));
    ASSERT_NE(view, nullptr);
    EXPECT_FALSE(view->favoritable);
}

// The catalogue's actions a menu holds are views' commands - opening a file is an image's, titled "Open file..." -
// each subtitled by its menus, with its shortcut and icon; a disabled one is dimmed. An action that launches a process
// is the process's, and an action no menu holds is not listed.
TEST_F(CommandIndex, ListsTheCataloguesActionsUnderTheirMenus)
{
    QObject owner;
    an::ui::shell::ActionCatalogue catalogue(owner);
    ActionCommands::Dependencies dependencies;
    dependencies.actions = catalogue.lookup();
    dependencies.launchesProcess = [](const std::string_view id) { return id == ids::kHistogramTransformation; };
    std::vector<CommandEntry> entries;
    ActionCommands(dependencies).collect(entries);
    const auto find = [&entries](const std::string_view id) -> const CommandEntry* {
        const QString wanted = ActionCommands::commandId(id);
        const auto found = std::find_if(entries.begin(), entries.end(),
                                        [&wanted](const CommandEntry& entry) { return entry.id == wanted; });
        return found == entries.end() ? nullptr : &*found;
    };

    const CommandEntry* const open = find(ids::kOpen);
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->id, QStringLiteral("action:actionOpen"));
    EXPECT_EQ(open->title, QStringLiteral("Open file..."));
    EXPECT_EQ(open->group, CommandGroup::Images);
    EXPECT_EQ(open->subtitle, QStringLiteral("File"));
    EXPECT_EQ(open->shortcut, QKeySequence(QStringLiteral("Ctrl+O")));
    EXPECT_EQ(open->iconKey, QStringLiteral("folder_open"));
    EXPECT_TRUE(open->available);

    const CommandEntry* const undo = find(ids::kUndo);
    ASSERT_NE(undo, nullptr);
    EXPECT_EQ(undo->group, CommandGroup::Views);
    EXPECT_EQ(undo->subtitle, QStringLiteral("Edit"));
    EXPECT_FALSE(undo->available) << "Undo with nothing to undo";

    const CommandEntry* const mask = find(ids::kShowMask);
    ASSERT_NE(mask, nullptr);
    EXPECT_TRUE(mask->subtitle.startsWith(QStringLiteral("Image > "))) << mask->subtitle.toStdString();

    EXPECT_EQ(find(ids::kHistogramTransformation), nullptr) << "a process action is its process's command";
    EXPECT_EQ(find(ids::kAsd), nullptr) << "no menu holds it";

    // A run triggers the action; a disabled one is not triggered.
    int triggered = 0;
    QObject::connect(catalogue.action(ids::kOpen), &QAction::triggered, [&triggered]() { ++triggered; });
    EXPECT_TRUE(open->run());
    EXPECT_EQ(triggered, 1);
}

// Each process of the registry is a process's command under its name and category, with its action's icon and
// shortcut, and runs through the launcher. One the registry says is unavailable is dimmed with its reason, unless its
// launch prepares its extension's environment; one of one image that cannot run on the image the actions act on is
// dimmed with what its guards say; with no image it runs, and the launcher says there is none.
TEST_F(CommandIndex, ListsTheProcessesWithWhatStandsInTheirWay)
{
    QObject owner;
    an::ui::shell::ActionCatalogue catalogue(owner);
    std::vector<ProcessDescriptor> descriptors(5);
    descriptors[0].id = "intensity_transformations.actionHistogram_Transformation";
    descriptors[0].display_name = "Histogram Transformation";
    descriptors[0].category = "Intensity Transformations";
    descriptors[0].action_object_name = std::string(ids::kHistogramTransformation);
    descriptors[1].id = "utilities.actionImage_Stacking";
    descriptors[1].display_name = "Image Stacking";
    descriptors[1].category = "Preprocess";
    descriptors[1].launch_mode = ProcessLaunchMode::TopLevel;
    descriptors[2].id = "extension.validation";
    descriptors[2].display_name = "Broken Extension";
    descriptors[2].is_extension_process = true;
    descriptors[2].extension_id = "broken";
    descriptors[2].availability = ProcessAvailability::ValidationFailed;
    descriptors[2].availability_reason = "The package failed validation.";
    descriptors[3].id = "extension.install";
    descriptors[3].display_name = "Installable Extension";
    descriptors[3].is_extension_process = true;
    descriptors[3].extension_id = "installable";
    descriptors[3].availability = ProcessAvailability::InstallNeeded;
    descriptors[3].availability_reason = "Its environment is installed at its first launch.";
    descriptors[4].id = "utilities.actionNoIcon";
    descriptors[4].display_name = "No Action";

    bool hasImage = true;
    std::vector<std::string> launched;
    std::set<std::string> favourites{descriptors[1].id};
    ProcessCommands::Dependencies dependencies;
    dependencies.descriptors = [&descriptors]() {
        std::vector<const ProcessDescriptor*> all;
        for (const ProcessDescriptor& descriptor : descriptors)
        {
            all.push_back(&descriptor);
        }
        return all;
    };
    dependencies.imageGuards = [&hasImage](const ProcessDescriptor& descriptor) {
        LaunchGuardResult result;
        if (hasImage && descriptor.id == "intensity_transformations.actionHistogram_Transformation")
        {
            result.reason = LaunchGuardFailureReason::UnsupportedActiveImageChannels;
            result.message = "This process does not support the selected image channel count.";
        }
        return result;
    };
    dependencies.launch = [&launched](const std::string& id) {
        launched.push_back(id);
        return true;
    };
    dependencies.isFavorite = [&favourites](const std::string& id) { return favourites.count(id) != 0u; };
    dependencies.setFavorite = [&favourites](const std::string& id, const bool favorite) {
        if (favorite)
        {
            favourites.insert(id);
        }
        else
        {
            favourites.erase(id);
        }
    };
    dependencies.actions = catalogue.lookup();
    const ProcessCommands provider(dependencies);

    std::vector<CommandEntry> entries;
    provider.collect(entries);
    ASSERT_EQ(entries.size(), 5u);
    const CommandEntry& histogram = entries[0];
    EXPECT_EQ(histogram.id, ProcessCommands::commandId(descriptors[0].id));
    EXPECT_EQ(histogram.id, QStringLiteral("process:intensity_transformations.actionHistogram_Transformation"));
    EXPECT_EQ(histogram.title, QStringLiteral("Histogram Transformation"));
    EXPECT_EQ(histogram.subtitle, QStringLiteral("Intensity Transformations"));
    EXPECT_TRUE(histogram.keywords.contains(QStringLiteral("Intensity Transformations")));
    EXPECT_EQ(histogram.group, CommandGroup::Processes);
    EXPECT_FALSE(histogram.available) << "the image has a channel count it does not take";
    EXPECT_EQ(histogram.unavailableReason,
              QStringLiteral("This process does not support the selected image channel count."));
    EXPECT_TRUE(static_cast<bool>(histogram.setFavorite));
    EXPECT_FALSE(histogram.favorite);

    EXPECT_TRUE(entries[1].available) << "a window of its own meets no image guard";
    EXPECT_TRUE(entries[1].favorite);
    EXPECT_FALSE(entries[2].available);
    EXPECT_EQ(entries[2].unavailableReason, QStringLiteral("The package failed validation."));
    EXPECT_TRUE(entries[3].available) << "its launch prepares its environment first";
    EXPECT_EQ(entries[4].iconKey, QStringLiteral("tune")) << "a process bound to no action shows the process glyph";

    ASSERT_TRUE(entries[3].run());
    EXPECT_EQ(launched, std::vector<std::string>{"extension.install"});
    entries[1].setFavorite(false);
    EXPECT_TRUE(favourites.empty());

    // With no image, a process of one image runs, and its launch says there is none.
    hasImage = false;
    entries.clear();
    provider.collect(entries);
    EXPECT_TRUE(entries[0].available);
    ASSERT_TRUE(entries[0].run());
    EXPECT_EQ(launched.back(), descriptors[0].id);
}

// The open views and the files opened last are images' commands: a view by its name, which running activates; a file
// by its name with its folder beside it, which running opens.
TEST_F(CommandIndex, ListsTheOpenViewsAndTheRecentFilesAsImages)
{
    QStringList activated;
    QStringList opened;
    ImageCommands::Dependencies dependencies;
    dependencies.views = []() {
        return std::vector<ImageCommands::OpenView>{{QStringLiteral("M20_FINAL"), QStringLiteral("D:/Astro/M20.fits")},
                                                    {QStringLiteral("integration_1"), QString()}};
    };
    dependencies.activateView = [&activated](const QString& name) { activated.append(name); };
    dependencies.recentFiles = []() {
        return std::vector<QString>{QStringLiteral("D:/Astro/Carina/Carina_Nebula_RGB.fit")};
    };
    dependencies.openFile = [&opened](const QString& path) { opened.append(path); };
    std::vector<CommandEntry> entries;
    ImageCommands(dependencies).collect(entries);

    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].id, ImageCommands::viewCommandId(QStringLiteral("M20_FINAL")));
    EXPECT_EQ(entries[0].title, QStringLiteral("M20_FINAL"));
    EXPECT_EQ(entries[0].subtitle, QDir::toNativeSeparators(QStringLiteral("D:/Astro/M20.fits")));
    EXPECT_EQ(entries[0].group, CommandGroup::Images);
    EXPECT_EQ(entries[1].title, QStringLiteral("integration_1"));
    EXPECT_FALSE(entries[1].subtitle.isEmpty()) << "an image no file holds still says what it is";
    EXPECT_EQ(entries[2].id, ImageCommands::fileCommandId(QStringLiteral("D:/Astro/Carina/Carina_Nebula_RGB.fit")));
    EXPECT_EQ(entries[2].title, QStringLiteral("Carina_Nebula_RGB.fit"));
    EXPECT_EQ(entries[2].subtitle, QDir::toNativeSeparators(QStringLiteral("D:/Astro/Carina")));
    EXPECT_EQ(entries[2].group, CommandGroup::Images);

    ASSERT_TRUE(entries[0].run());
    ASSERT_TRUE(entries[2].run());
    EXPECT_EQ(activated, QStringList{QStringLiteral("M20_FINAL")});
    EXPECT_EQ(opened, QStringList{QStringLiteral("D:/Astro/Carina/Carina_Nebula_RGB.fit")});
}

// A settings page is found by its title and its search words, and each of its rows as `Page > Row` and by its section;
// running either opens the Settings at the page.
TEST_F(CommandIndex, ListsTheSettingsPagesAndTheirRows)
{
    QStringList openedPages;
    SettingsCommands::Dependencies dependencies;
    dependencies.pages = []() {
        return std::vector<SettingsSearchPage>{{QStringLiteral("Plate Solving"),
                                                {QStringLiteral("Astrometry"), QStringLiteral("Solver")},
                                                {{QStringLiteral("Default solver"), QStringLiteral("Solver")},
                                                 {QStringLiteral("Search radius"), QStringLiteral("Hints")},
                                                 {QStringLiteral("Auto"), QStringLiteral("Threads")},
                                                 {QStringLiteral("Auto"), QStringLiteral("Memory")}}}};
    };
    dependencies.openPage = [&openedPages](const QString& title) { openedPages.append(title); };
    auto index = std::make_unique<Index>();
    index->addProvider(std::make_unique<SettingsCommands>(dependencies));
    index->refresh();

    ASSERT_EQ(index->entries().size(), 5u);
    const CommandEntry& page = index->entries()[0];
    EXPECT_EQ(page.id, SettingsCommands::pageCommandId(QStringLiteral("Plate Solving")));
    EXPECT_EQ(page.title, QStringLiteral("Plate Solving"));
    EXPECT_EQ(page.group, CommandGroup::Settings);
    const CommandEntry& row = index->entries()[1];
    EXPECT_EQ(row.id, SettingsCommands::rowCommandId(QStringLiteral("Plate Solving"), QStringLiteral("Solver"),
                                                     QStringLiteral("Default solver")));
    EXPECT_EQ(row.title, QStringLiteral("Plate Solving > Default solver"));
    EXPECT_EQ(row.group, CommandGroup::Settings);

    EXPECT_NE(rowOf(index->query(QStringLiteral(":astrom")), page.id), nullptr) << "a page by its search words";
    EXPECT_NE(rowOf(index->query(QStringLiteral("default sol")), row.id), nullptr) << "a row by its words";
    const QString radius = SettingsCommands::rowCommandId(QStringLiteral("Plate Solving"), QStringLiteral("Hints"),
                                                          QStringLiteral("Search radius"));
    EXPECT_NE(rowOf(index->query(QStringLiteral("hints")), radius), nullptr) << "a row by its section";

    // A label shown in two sections is two rows, each titled by its section.
    EXPECT_EQ(index->entries()[3].title, QStringLiteral("Plate Solving > Threads > Auto"));
    EXPECT_EQ(index->entries()[4].title, QStringLiteral("Plate Solving > Memory > Auto"));
    EXPECT_NE(index->entries()[3].id, index->entries()[4].id);

    ASSERT_TRUE(index->run(row.id));
    ASSERT_TRUE(index->run(page.id));
    EXPECT_EQ(openedPages, (QStringList{QStringLiteral("Plate Solving"), QStringLiteral("Plate Solving")}));
}

/// The rows a settings page shows: each labelled field of its forms, its label without the colon, and each check box,
/// in the order the page makes them, each with the title of the group it sits in.
std::vector<SettingsSearchRow> rowsShownBy(QWidget& page)
{
    QSet<const QWidget*> formLabels;
    for (const QFormLayout* form : page.findChildren<QFormLayout*>())
    {
        for (int row = 0; row < form->rowCount(); ++row)
        {
            if (const QLayoutItem* const item = form->itemAt(row, QFormLayout::LabelRole);
                item != nullptr && item->widget() != nullptr)
            {
                formLabels.insert(item->widget());
            }
        }
    }
    const auto sectionOf = [&page](const QWidget* widget) {
        for (const QWidget* above = widget->parentWidget(); above != nullptr && above != &page;
             above = above->parentWidget())
        {
            if (const auto* const group = qobject_cast<const QGroupBox*>(above))
            {
                return typed(group->title());
            }
        }
        return QString();
    };
    std::vector<SettingsSearchRow> rows;
    for (const QWidget* widget : page.findChildren<QWidget*>())
    {
        QString label;
        if (const auto* const check = qobject_cast<const QCheckBox*>(widget))
        {
            label = typed(check->text());
        }
        else if (const auto* const text = qobject_cast<const QLabel*>(widget);
                 text != nullptr && formLabels.contains(text))
        {
            label = typed(text->text());
            if (label.endsWith(QLatin1Char(':')))
            {
                label.chop(1);
            }
        }
        label = label.trimmed();
        if (!label.isEmpty())
        {
            rows.push_back(SettingsSearchRow{label, sectionOf(widget)});
        }
    }
    return rows;
}

/// A row as the table writes it, for a failure to show what to write.
std::string written(const std::vector<SettingsSearchRow>& rows)
{
    std::string text;
    for (const SettingsSearchRow& row : rows)
    {
        text += "            {QStringLiteral(\"" + row.label.toStdString() + "\"), QStringLiteral(\"" +
                row.section.toStdString() + "\")},\n";
    }
    return text;
}

// The table the palette finds settings in is the Settings dialog's pages as they are: each page's title and search
// words as the page answers them, and its rows - every labelled field and every check box it shows, in its order, with
// the group each sits in. A page that gains a row, loses one or renames one fails here until the table says so.
TEST_F(CommandIndex, TheSettingsTableIsWhatThePagesShow)
{
    std::vector<std::unique_ptr<SettingsPage>> pages;
    pages.push_back(std::make_unique<GeneralSettingsPage>(nullptr));
    pages.push_back(std::make_unique<an::ui::ProjectsSettingsPage>(nullptr));
    pages.push_back(std::make_unique<RawImageSettingsPage>(nullptr));
    pages.push_back(std::make_unique<ExternalToolsSettingsPage>(nullptr));
    pages.push_back(std::make_unique<InterfaceSettingsPage>(nullptr));
    pages.push_back(std::make_unique<ExportSettingsPage>(nullptr));
    pages.push_back(std::make_unique<ProcessingSettingsPage>(nullptr));
    pages.push_back(std::make_unique<LoggingSettingsPage>(nullptr));
    pages.push_back(std::make_unique<CatalogsDataSettingsPage>(nullptr));
    pages.push_back(std::make_unique<PlateSolvingSettingsPage>(nullptr));

    const std::vector<SettingsSearchPage> table = an::ui::settingsSearchPages();
    ASSERT_EQ(table.size(), pages.size()) << "one entry for each page of the Settings dialog, in its order";
    for (std::size_t index = 0; index < pages.size(); ++index)
    {
        SettingsPage& page = *pages[index];
        const SettingsSearchPage& entry = table[index];
        EXPECT_EQ(entry.title, page.pageTitle());
        EXPECT_EQ(entry.keywords, page.searchKeywords()) << page.pageTitle().toStdString();
        const std::vector<SettingsSearchRow> shown = rowsShownBy(page);
        EXPECT_EQ(entry.rows, shown) << page.pageTitle().toStdString() << " shows these rows:\n" << written(shown);
    }
}

/// Where a file of the repository is: the first of that path at or above the working directory or the executable's.
QString repositoryFile(const QString& path)
{
    for (const QString& seed : {QDir::currentPath(), QCoreApplication::applicationDirPath()})
    {
        QDir folder(seed);
        do
        {
            if (QFileInfo(folder.filePath(path)).isFile())
            {
                return folder.filePath(path);
            }
        } while (folder.cdUp());
    }
    return {};
}

// The index is searched with no widget: what it includes, and what that includes in turn of the repository's own,
// names no header of Qt but its core and gui modules' few it uses.
TEST_F(CommandIndex, IncludesNoWidgetHeader)
{
    const QSet<QString> allowedQt = {QStringLiteral("QKeySequence"), QStringLiteral("QString"),
                                     QStringLiteral("QStringList"),  QStringLiteral("QHash"),
                                     QStringLiteral("QChar"),        QStringLiteral("QStringView"),
                                     QStringLiteral("QtGlobal")};
    const QStringList roots = {QStringLiteral("src/AfterNightShellCore/"), QStringLiteral("src/AfterNightControls/")};
    QStringList pending = {QStringLiteral("src/AfterNightShellCore/commands/CommandIndex.h"),
                           QStringLiteral("src/AfterNightShellCore/commands/CommandIndex.cpp")};
    QSet<QString> read;
    const QRegularExpression include(QStringLiteral(R"(^\s*#\s*include\s*([<"])([^>"]+)[>"])"),
                                     QRegularExpression::MultilineOption);
    while (!pending.isEmpty())
    {
        const QString path = pending.takeFirst();
        if (read.contains(path))
        {
            continue;
        }
        read.insert(path);
        const QString file = repositoryFile(path);
        ASSERT_FALSE(file.isEmpty()) << path.toStdString() << " was not found";
        QFile source(file);
        ASSERT_TRUE(source.open(QIODevice::ReadOnly)) << file.toStdString();
        const QString text = QString::fromUtf8(source.readAll());
        QRegularExpressionMatchIterator matches = include.globalMatch(text);
        while (matches.hasNext())
        {
            const QRegularExpressionMatch match = matches.next();
            const QString name = match.captured(2);
            if (match.captured(1) == QStringLiteral("<"))
            {
                if (name.startsWith(QLatin1Char('Q')))
                {
                    EXPECT_TRUE(allowedQt.contains(name))
                        << path.toStdString() << " includes <" << name.toStdString() << ">";
                }
                continue;
            }
            bool found = false;
            for (const QString& root : roots)
            {
                if (!repositoryFile(root + name).isEmpty())
                {
                    pending.append(root + name);
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found) << path.toStdString() << " includes \"" << name.toStdString()
                               << "\", which is no file of the shell core or the Controls library";
        }
    }
    EXPECT_TRUE(read.contains(QStringLiteral("src/AfterNightControls/components/ICommandSource.h")));
}

// A keystroke over a thousand commands answers within 16 ms: the query is a search of a list in memory, asserted in
// Release with a wide margin on any host.
TEST_F(CommandIndex, AKeystrokeOverAThousandCommandsAnswersWithinSixteenMilliseconds)
{
#ifndef NDEBUG
    GTEST_SKIP() << "the time of a keystroke is asserted in Release";
#else
    const QStringList words = {
        QStringLiteral("Histogram"), QStringLiteral("Transformation"), QStringLiteral("Star"),
        QStringLiteral("Mask"),      QStringLiteral("Curves"),         QStringLiteral("Noise"),
        QStringLiteral("Reduction"), QStringLiteral("Color"),          QStringLiteral("Calibration"),
        QStringLiteral("Gradient"),  QStringLiteral("Removal"),        QStringLiteral("Stretch")};
    std::vector<CommandEntry> entries;
    for (int number = 0; number < 1000; ++number)
    {
        const QString title = words[number % words.size()] + QLatin1Char(' ') + words[(number / 7) % words.size()] +
                              QLatin1Char(' ') + words[(number / 49) % words.size()] +
                              QStringLiteral(" %1").arg(number);
        CommandEntry entry = command(QStringLiteral("c%1").arg(number), title, static_cast<CommandGroup>(number % 4),
                                     {words[(number / 3) % words.size()]});
        entries.push_back(std::move(entry));
    }
    const auto index = indexOf(std::move(entries));
    for (int number = 0; number < 20; ++number)
    {
        ASSERT_TRUE(index->run(QStringLiteral("c%1").arg(number * 37)));
    }

    const QString typedText = QStringLiteral("histogram trans");
    std::chrono::nanoseconds slowest{0};
    std::size_t rows = 0u;
    for (int round = 0; round < 3; ++round)
    {
        for (int length = 0; length <= typedText.size(); ++length)
        {
            const auto start = std::chrono::steady_clock::now();
            const std::vector<CommandRowGroup> groups = index->query(typedText.left(length));
            const auto took = std::chrono::steady_clock::now() - start;
            slowest = (std::max)(slowest, std::chrono::duration_cast<std::chrono::nanoseconds>(took));
            for (const CommandRowGroup& group : groups)
            {
                rows += group.rows.size();
            }
        }
    }
    EXPECT_GT(rows, 0u);
    // The time is written into the suite's report, for the record of the budget.
    RecordProperty("slowestKeystrokeMicroseconds",
                   static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(slowest).count()));
    EXPECT_LT(slowest, std::chrono::milliseconds(16))
        << "the slowest keystroke took " << std::chrono::duration<double, std::milli>(slowest).count() << " ms";
#endif
}

/// A core over a storage root of the case's own, its stores open, its registry holding the processes of the record of
/// 1.1's registry and its process actions bound to them, as the application's are.
class CommandIndexInTheApplication : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ProcessRegistry::instance().clear();
    }

    void TearDown() override
    {
        ProcessRegistry::instance().clear();
    }

    /// Registers the record's processes as the processes library does, named after their actions until the menu
    /// names them.
    static void registerTheRecordedProcesses()
    {
        for (const QStringList& process : recordedRows(kProcessRecordPath, "processes"))
        {
            ASSERT_GE(process.size(), 6);
            ProcessDescriptor descriptor;
            descriptor.id = process[0].toStdString();
            descriptor.category = process[3].toStdString();
            descriptor.action_object_name = process[5].toStdString();
            descriptor.display_name = descriptor.action_object_name;
            descriptor.process_name =
                process[1] == process[2] ? descriptor.action_object_name : process[2].toStdString();
            descriptor.launch_top_level = []() {};
            descriptor.launch_non_interactive = []() {};
            descriptor.launch_single_image = [](ImageViewControl*) {};
            ASSERT_TRUE(ProcessRegistry::instance().registerProcess(std::move(descriptor)));
        }
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
};

// Every action of the record of 1.1's main window a menu holds is found by its text - as its own command, or as its
// process's when it launches one - and every process of the record of 1.1's registry by its name; an action no menu
// holds is not listed.
TEST_F(CommandIndexInTheApplication, FindsEveryRecordedActionAndProcessByItsText)
{
    registerTheRecordedProcesses();
    ApplicationCore core;
    core.openStores();
    core.actionBindings().connectProcessActions();
    Index& index = core.commands();
    index.refresh();

    const std::vector<QStringList> actions = recordedRows(kActionRecordPath, "actions");
    ASSERT_GT(actions.size(), 150u) << "the record of the actions was not found";
    int listed = 0;
    for (const QStringList& action : actions)
    {
        ASSERT_GE(action.size(), 8) << action.join(QLatin1Char('|')).toStdString();
        const std::string name = action[0].toStdString();
        QString expected = ActionCommands::commandId(name);
        for (const ProcessDescriptor& descriptor : ProcessRegistry::instance().all())
        {
            if (descriptor.action_object_name == name)
            {
                expected = ProcessCommands::commandId(descriptor.id);
            }
        }
        const bool inAMenu = !action[7].isEmpty();
        const bool launchesAProcess = expected.startsWith(QStringLiteral("process:"));
        const std::vector<CommandRowGroup> found = index.query(typed(action[1]));
        if (inAMenu || launchesAProcess)
        {
            EXPECT_NE(rowOf(found, expected), nullptr) << name << " by \"" << action[1].toStdString() << "\"";
            ++listed;
        }
        else
        {
            EXPECT_EQ(index.entry(expected), nullptr) << name << " is in no menu";
        }
    }
    EXPECT_GT(listed, 150);

    const std::vector<QStringList> processes = recordedRows(kProcessRecordPath, "processes");
    ASSERT_GT(processes.size(), 100u);
    for (const QStringList& process : processes)
    {
        const QString expected = ProcessCommands::commandId(process[0].toStdString());
        const CommandRow* const row = rowOf(index.query(process[1]), expected);
        EXPECT_NE(row, nullptr) << process[0].toStdString() << " by \"" << process[1].toStdString() << "\"";
    }
}

// The commands run last are stored in the settings and read again by the next start of the application; a process
// starred in the palette is a favourite process of the menus.
TEST_F(CommandIndexInTheApplication, KeepsTheCommandsRunLastAcrossARestartAndStarsTheFavouriteProcesses)
{
    registerTheRecordedProcesses();
    const QString about = ActionCommands::commandId(ids::kAboutQt);
    const std::string process = "tone_brightness.actionHistogram_Transformation";
    {
        ApplicationCore core;
        core.openStores();
        core.commands().refresh();
        ASSERT_NE(core.commands().entry(about), nullptr);
        ASSERT_TRUE(core.commands().run(about));
        EXPECT_EQ(core.globalSettings()->select(QString::fromLatin1(SettingsKeys::INTERFACE_PALETTE_RECENT_COMMANDS)),
                  about);

        ASSERT_NE(core.commands().entry(ProcessCommands::commandId(process)), nullptr) << process;
        core.commands().toggleFavorite(ProcessCommands::commandId(process));
        EXPECT_EQ(core.favoriteProcesses().ids().count(process), 1u);
    }

    ApplicationCore next;
    next.openStores();
    EXPECT_EQ(next.commands().recentIds(), std::deque<QString>{about});
    // A shell reads the favourite processes as it starts.
    next.favoriteProcesses().load();
    next.commands().refresh();
    const std::vector<CommandRowGroup> empty = next.commands().query(QString());
    ASSERT_EQ(groupIdsOf(empty), (QStringList{QStringLiteral("favourites"), QStringLiteral("recent")}));
    EXPECT_EQ(empty.front().rows.front().id, ProcessCommands::commandId(process));
    EXPECT_EQ(empty.back().rows.front().id, about);
}

// The files opened last are the core's recent files, and the settings are the Settings dialog's pages, which running
// one opens at.
TEST_F(CommandIndexInTheApplication, ListsTheRecentFilesAndTheSettingsPages)
{
    ApplicationCore core;
    core.openStores();
    const std::string file = QDir(m_root.path()).filePath(QStringLiteral("Carina_Nebula_RGB.fit")).toStdString();
    core.recentFiles().entriesForEdit() = {file};
    core.commands().refresh();

    const CommandRow* const recent = rowOf(core.commands().query(QStringLiteral("@carina")),
                                           ImageCommands::fileCommandId(QString::fromStdString(file)));
    ASSERT_NE(recent, nullptr);
    EXPECT_EQ(recent->title, QStringLiteral("Carina_Nebula_RGB.fit"));

    const std::vector<SettingsSearchPage> pages = an::ui::settingsSearchPages();
    ASSERT_FALSE(pages.empty());
    for (const SettingsSearchPage& page : pages)
    {
        EXPECT_NE(
            rowOf(core.commands().query(QStringLiteral(":") + page.title), SettingsCommands::pageCommandId(page.title)),
            nullptr)
            << page.title.toStdString();
        for (const SettingsSearchRow& row : page.rows)
        {
            EXPECT_NE(core.commands().entry(SettingsCommands::rowCommandId(page.title, row.section, row.label)),
                      nullptr)
                << page.title.toStdString() << " > " << row.label.toStdString();
        }
    }
}

} // namespace
