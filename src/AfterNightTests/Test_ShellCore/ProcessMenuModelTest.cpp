// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process menus as data: the sixteen categories of the Process menu in its order; the favourite processes by
/// name, then Manage and Clear; the processes used last, most recent first and no more than the limit, then Clear; an
/// extension's processes in the menu of their category, and its family in the Extensions menu, both gone when the
/// extension is.

#include "ShellCoreTestSupport.h"

#include "actions/ActionCatalogue.h"
#include "actions/MenuTree.h"
#include "actions/ProcessMenuModel.h"
#include "services/FavoriteProcessesModel.h"
#include "services/RecentProcessesModel.h"

#include "app/ExtensionManager.h"
#include "app/ProcessRegistry.h"
#include "controls/settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QList>
#include <QMenu>
#include <QObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::MemorySettings;
using an::tests::shellcore::ProcessCatalogue;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionLoadStatus;
using an::ui::shell::ActionCatalogue;
using an::ui::shell::FavoriteProcessesModel;
using an::ui::shell::MenuTree;
using an::ui::shell::ProcessMenuModel;
using an::ui::shell::RecentProcessesModel;

/// A menu's entries by text, a separator as `-`.
[[nodiscard]] QStringList textsOf(const QMenu& menu)
{
    QStringList texts;
    for (const QAction* action : menu.actions())
    {
        texts.append(action->isSeparator() ? QStringLiteral("-") : action->text());
    }
    return texts;
}

/// A discovered package of an extension `id` named `name` that provides one process of a category.
[[nodiscard]] ExtensionLoadResult packageOf(const std::string& id, const std::string& name,
                                            const std::string& processId, const std::string& category)
{
    ExtensionLoadResult result;
    result.status = ExtensionLoadStatus::Ready;
    an::ui::extensions::ExtensionManifest manifest;
    manifest.id = id;
    manifest.name = name;
    manifest.repository_metadata.homepage_url = "https://example.org/" + id;
    an::ui::extensions::ExtensionProcessManifest process;
    process.id = processId;
    process.display_name = name + " process";
    process.action_object_name = "actionExtension_" + id;
    process.category = category;
    manifest.processes.push_back(process);
    result.manifest = manifest;
    return result;
}

class ProcessMenuModelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        an::ui::process_registry::ProcessRegistry::instance().clear();
        m_recent.setSettings(&m_settings);
        m_favourites.setSettings(&m_settings);
    }

    void TearDown() override
    {
        an::ui::process_registry::ProcessRegistry::instance().clear();
    }

    [[nodiscard]] ProcessMenuModel::Dependencies dependencies()
    {
        ProcessMenuModel::Dependencies deps;
        deps.recentProcesses = &m_recent;
        deps.favoriteProcesses = &m_favourites;
        deps.lookup = m_processes.lookup();
        deps.actions = m_catalogue.lookup();
        deps.launch = [this](const std::string& id) {
            m_launched.push_back(id);
            return true;
        };
        deps.manageExtensions = [this]() { m_asked.emplace_back("manage extensions"); };
        deps.bindExtensionAction = [this](QAction* action, const std::string&, const std::string& descriptorId) {
            m_bound.emplace_back(action->objectName().toStdString() + " -> " + descriptorId);
        };
        return deps;
    }

    /// The model the cases build menus from, its hooks recording what they ask.
    [[nodiscard]] std::unique_ptr<ProcessMenuModel> model()
    {
        auto made = std::make_unique<ProcessMenuModel>(dependencies());
        ProcessMenuModel::Hooks hooks;
        hooks.clearRecentProcesses = [this]() { m_asked.emplace_back("clear recent"); };
        hooks.manageFavoriteProcesses = [this]() { m_asked.emplace_back("manage favourites"); };
        hooks.clearFavoriteProcesses = [this]() { m_asked.emplace_back("clear favourites"); };
        made->setHooks(std::move(hooks));
        return made;
    }

    MemorySettings m_settings;
    ProcessCatalogue m_processes{{"filters.actionGaussian_Blur", "Gaussian Blur"},
                                 {"color.actionSCNR", "SCNR Filter"},
                                 {"denoising.actionDeepSNR", "DeepSNR"},
                                 {"star_object.actionStarnet", "Starnet++"}};
    RecentProcessesModel m_recent{m_processes.lookup()};
    FavoriteProcessesModel m_favourites{m_processes.lookup()};
    QObject m_owner;
    ActionCatalogue m_catalogue{m_owner};
    std::vector<std::string> m_launched;
    std::vector<std::string> m_asked;
    std::vector<std::string> m_bound;
};

} // namespace

// The Process menu's sixteen categories, in its order, under their titles and menus - the menu tree's.
TEST_F(ProcessMenuModelTest, HoldsTheSixteenCategoriesInTheMenusOrder)
{
    const std::vector<ProcessMenuModel::Category>& categories = ProcessMenuModel::categories();
    QStringList titles;
    for (const ProcessMenuModel::Category& category : categories)
    {
        titles.append(category.title);
    }
    EXPECT_EQ(
        titles,
        (QStringList{QStringLiteral("Adjustments"), QStringLiteral("Background Modelization"),
                     QStringLiteral("Color Calibration"), QStringLiteral("Color Spaces"), QStringLiteral("Details"),
                     QStringLiteral("Filters"), QStringLiteral("Geometry"), QStringLiteral("Intensity Transformations"),
                     QStringLiteral("Mask Generation"), QStringLiteral("Morphology"), QStringLiteral("Multiscale"),
                     QStringLiteral("Noise Reduction"), QStringLiteral("Pixel Math"), QStringLiteral("Preprocess"),
                     QStringLiteral("Star Processing"), QStringLiteral("Utilities")}));

    // Each is the menu of that title the menu tree's Process menu holds, after the favourite and recent processes.
    QMenu process;
    ASSERT_TRUE(MenuTree::menuBar().buildMenu(process, "menuProcess", m_catalogue.lookup()));
    QStringList submenus;
    for (const QAction* entry : process.actions())
    {
        if (entry->menu() != nullptr)
        {
            submenus.append(entry->menu()->objectName());
        }
    }
    ASSERT_EQ(submenus.size(), 18);
    EXPECT_EQ(submenus.mid(0, 2),
              (QStringList{QStringLiteral("menuFavorite_Processes"), QStringLiteral("menuRecent_Processes")}));
    for (qsizetype index = 0; index < 16; ++index)
    {
        EXPECT_EQ(submenus[index + 2].toStdString(), categories[static_cast<std::size_t>(index)].menuId) << index;
    }
}

// A process's category is the menu a person reaches it from, whatever category the registry files it under: Channel
// Composition is filed with the colour processes and listed under Color Spaces, Binarize with the sensor's and under
// Intensity Transformations. Of two menus that list an action, the one its registry category joins: Gaussian Blur,
// under Details and Filters, is a filter. An action listed outside the Process menu says the menus down to it; one no
// menu lists, its registry category's menu; one of neither, nothing.
TEST_F(ProcessMenuModelTest, AProcesssCategoryIsTheMenuItIsReachedFrom)
{
    const auto categoryOf = [](const std::string& category, const std::string& action) {
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.category = category;
        descriptor.action_object_name = action;
        descriptor.id = category + "." + action;
        return ProcessMenuModel::categoryOf(descriptor).toStdString();
    };
    EXPECT_EQ(categoryOf("color", "actionChannelComposition"), "Color Spaces");
    EXPECT_EQ(categoryOf("color", "actionNBColorMapper"), "Color Spaces");
    EXPECT_EQ(categoryOf("sensor_calibration", "actionBinarize"), "Intensity Transformations");
    EXPECT_EQ(categoryOf("multi_image_stacking", "actionImage_Compare"), "Utilities");
    EXPECT_EQ(categoryOf("color", "actionColor_Calibration_2"), "Color Calibration");
    EXPECT_EQ(categoryOf("filters", "actionGaussian_Blur"), "Filters")
        << "listed under Details and Filters, it is filed as a filter";
    EXPECT_EQ(categoryOf("multi_image_stacking", "actionExtract_Sequence"), "Image > Sequence");
    EXPECT_EQ(categoryOf("denoising", "actionExtension_fancy"), "Noise Reduction")
        << "an extension's process no menu of the tree lists is in its category's menu";
    EXPECT_EQ(categoryOf("unknown", "actionNone"), "");
}

// The favourite processes are listed by the names the registry gives, in their order, then Manage and Clear; with none
// Clear is not offered; each launches its process and Manage and Clear ask what they ask.
TEST_F(ProcessMenuModelTest, ListsTheFavouritesByNameThenManageAndClear)
{
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    QMenu empty;
    model->buildFavoriteProcessesMenu(empty);
    EXPECT_EQ(textsOf(empty),
              (QStringList{QStringLiteral("Manage Favorites..."), QStringLiteral("Clear favorites list")}));
    EXPECT_FALSE(empty.actions().back()->isEnabled());

    m_favourites.setIds({"color.actionSCNR", "filters.actionGaussian_Blur", "gone.actionNothing"});
    QMenu menu;
    model->buildFavoriteProcessesMenu(menu);
    EXPECT_EQ(textsOf(menu),
              (QStringList{QStringLiteral("Gaussian Blur"), QStringLiteral("SCNR Filter"), QStringLiteral("-"),
                           QStringLiteral("Manage Favorites..."), QStringLiteral("Clear favorites list")}));
    EXPECT_TRUE(menu.actions().back()->isEnabled());

    menu.actions().at(1)->trigger();
    EXPECT_EQ(m_launched, std::vector<std::string>{"color.actionSCNR"});
    menu.actions().at(3)->trigger();
    menu.actions().at(4)->trigger();
    EXPECT_EQ(m_asked, (std::vector<std::string>{"manage favourites", "clear favourites"}));

    // Built again, the menu holds the list once.
    model->buildFavoriteProcessesMenu(menu);
    EXPECT_EQ(menu.actions().size(), 5);
}

// The processes used last are listed most recent first and no more than the limit, then Clear; one the registry no
// longer holds is offered disabled; with none the menu is withdrawn.
TEST_F(ProcessMenuModelTest, ListsTheRecentProcessesUpToTheLimit)
{
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    QMenu menu;
    model->buildRecentProcessesMenu(menu);
    EXPECT_TRUE(menu.actions().isEmpty());
    EXPECT_FALSE(menu.isEnabled());

    ASSERT_TRUE(
        m_settings.upsert(QString::fromLatin1(SettingsKeys::GENERAL_RECENT_PROCESSES_LIMIT), QStringLiteral("2")));
    ASSERT_TRUE(m_recent.record("filters.actionGaussian_Blur", "Gaussian Blur"));
    ASSERT_TRUE(m_recent.record("color.actionSCNR", "SCNR Filter"));
    ASSERT_TRUE(m_recent.record("gone.actionNothing", "Gone"));

    model->buildRecentProcessesMenu(menu);
    EXPECT_TRUE(menu.isEnabled());
    EXPECT_EQ(textsOf(menu), (QStringList{QStringLiteral("Gone"), QStringLiteral("SCNR Filter"), QStringLiteral("-"),
                                          QStringLiteral("Clear recent processes list")}));
    EXPECT_FALSE(menu.actions().at(0)->isEnabled()) << "the registry no longer holds it";
    EXPECT_TRUE(menu.actions().at(1)->isEnabled());
    EXPECT_EQ(menu.actions().at(1)->toolTip(), QStringLiteral("color.actionSCNR"));

    menu.actions().at(1)->trigger();
    EXPECT_EQ(m_launched, std::vector<std::string>{"color.actionSCNR"});
    menu.actions().at(3)->trigger();
    EXPECT_EQ(m_asked, std::vector<std::string>{"clear recent"});
}

// An extension's process joins the menu of its category after a separator, as an action bound to its descriptor, and
// its family is listed in the Extensions menu; when the extension is gone, both are.
TEST_F(ProcessMenuModelTest, AnExtensionsProcessAndFamilyAppearAndDisappear)
{
    m_processes.add("extensions.blur", "Fancy Blur process");
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    model->setExtensions({packageOf("fancy", "Fancy Blur", "extensions.blur", "filters"),
                          packageOf("nowhere", "Nowhere", "extensions.none", "a_category_no_menu_has")});

    const std::vector<QAction*> inFilters = model->extensionActionsIn("menuFilters");
    ASSERT_EQ(inFilters.size(), 1u);
    EXPECT_EQ(inFilters.front()->objectName(), QStringLiteral("actionExtension_fancy"));
    EXPECT_EQ(inFilters.front()->text(), QStringLiteral("Fancy Blur process"));
    EXPECT_EQ(inFilters.front()->property("anRegistryDescriptorId").toString(), QStringLiteral("extensions.blur"));
    EXPECT_EQ(m_bound, std::vector<std::string>{"actionExtension_fancy -> extensions.blur"});
    EXPECT_EQ(model->extensionAction("actionExtension_fancy"), inFilters.front());

    QMenu filters;
    filters.addAction(QStringLiteral("Gaussian Blur"));
    model->appendExtensionProcesses(filters, "menuFilters");
    EXPECT_EQ(textsOf(filters), (QStringList{QStringLiteral("Gaussian Blur"), QStringLiteral("-"),
                                             QStringLiteral("Fancy Blur process")}));

    ASSERT_EQ(model->extensionFamilies().size(), 2u);
    QMenu extensions;
    extensions.addAction(QStringLiteral("Manage Extensions..."));
    model->buildExtensionFamilyMenus(extensions);
    EXPECT_EQ(textsOf(extensions), (QStringList{QStringLiteral("Manage Extensions..."), QStringLiteral("-"),
                                                QStringLiteral("Fancy Blur"), QStringLiteral("Nowhere")}));
    QMenu* const family = extensions.actions().at(2)->menu();
    ASSERT_NE(family, nullptr);
    EXPECT_EQ(textsOf(*family), (QStringList{QStringLiteral("Manage..."), QStringLiteral("Homepage")}));
    family->actions().at(0)->trigger();
    EXPECT_EQ(m_asked, std::vector<std::string>{"manage extensions"});

    // The extension goes: its process and its family go from the model and from the menus built again.
    model->setExtensions({});
    EXPECT_TRUE(model->extensionActionsIn("menuFilters").empty());
    EXPECT_TRUE(model->extensionFamilies().empty());
    model->appendExtensionProcesses(filters, "menuFilters");
    model->buildExtensionFamilyMenus(extensions);
    EXPECT_EQ(textsOf(filters), QStringList{QStringLiteral("Gaussian Blur")});
    EXPECT_EQ(textsOf(extensions), QStringList{QStringLiteral("Manage Extensions...")});
}

// An extension's process carries the quiet tag "extension" wherever its action is listed - the action it adds, and a
// catalogue action bound in its place - and a catalogue action no extension binds any more carries none.
TEST_F(ProcessMenuModelTest, AnExtensionsProcessCarriesTheExtensionTag)
{
    m_processes.add("extensions.blur", "Fancy Blur process");
    m_processes.add("extensions.histogram", "Histogram process");
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    an::ui::extensions::ExtensionLoadResult rebinding =
        packageOf("rebinding", "Rebinding", "extensions.histogram", "transforms");
    rebinding.manifest->processes.front().action_object_name = "actionHistogram_Transformation";
    model->setExtensions({packageOf("fancy", "Fancy Blur", "extensions.blur", "filters"), rebinding});

    const std::vector<QAction*> inFilters = model->extensionActionsIn("menuFilters");
    ASSERT_EQ(inFilters.size(), 1u);
    EXPECT_EQ(inFilters.front()->property(ProcessMenuModel::kMenuTagProperty).toString(), QStringLiteral("extension"));

    QAction* const histogram = m_catalogue.action("actionHistogram_Transformation");
    ASSERT_NE(histogram, nullptr);
    EXPECT_EQ(histogram->property(ProcessMenuModel::kMenuTagProperty).toString(), QStringLiteral("extension"))
        << "a catalogue action an extension's process is bound to carries the tag";

    QAction* const open = m_catalogue.action("actionOpen");
    ASSERT_NE(open, nullptr);
    EXPECT_FALSE(open->property(ProcessMenuModel::kMenuTagProperty).isValid()) << "an action of the application";

    model->setExtensions({});
    EXPECT_FALSE(histogram->property(ProcessMenuModel::kMenuTagProperty).isValid())
        << "the extension went, and the catalogue's action is the application's again";
}

// Asked to, a family's menu lists the extension's processes first - the very actions its category's menu holds, so
// they are bound and tagged as there - then its links; by default it holds the links alone, as 1.1's window's did.
TEST_F(ProcessMenuModelTest, AFamilysMenuListsItsProcessesFirstWhenAsked)
{
    m_processes.add("extensions.blur", "Fancy Blur process");
    m_processes.add("extensions.sharpen", "Fancy Sharpen process");
    m_processes.add("other.thing", "Other process");
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    an::ui::extensions::ExtensionLoadResult fancy = packageOf("fancy", "Fancy Tools", "extensions.blur", "filters");
    an::ui::extensions::ExtensionProcessManifest sharpen = fancy.manifest->processes.front();
    sharpen.id = "extensions.sharpen";
    sharpen.display_name = "Fancy Sharpen process";
    sharpen.action_object_name = "actionExtension_fancy_sharpen";
    fancy.manifest->processes.push_back(sharpen);
    model->setExtensions({fancy, packageOf("other", "Other", "other.thing", "filters")});

    QMenu extensions;
    extensions.addAction(QStringLiteral("Manage Extensions..."));
    model->buildExtensionFamilyMenus(extensions, ProcessMenuModel::FamilyEntries::ProcessesAndLinks);
    ASSERT_EQ(textsOf(extensions), (QStringList{QStringLiteral("Manage Extensions..."), QStringLiteral("-"),
                                                QStringLiteral("Fancy Tools"), QStringLiteral("Other")}));
    QMenu* const family = extensions.actions().at(2)->menu();
    ASSERT_NE(family, nullptr);
    EXPECT_EQ(textsOf(*family),
              (QStringList{QStringLiteral("Fancy Blur process"), QStringLiteral("Fancy Sharpen process"),
                           QStringLiteral("-"), QStringLiteral("Manage..."), QStringLiteral("Homepage")}));
    const std::vector<QAction*> inFilters = model->extensionActionsIn("menuFilters");
    ASSERT_EQ(inFilters.size(), 3u);
    EXPECT_EQ(family->actions().at(0), inFilters.at(0)) << "the category's very action";
    EXPECT_EQ(family->actions().at(1), inFilters.at(1));

    // Built again, each family's menu holds its processes once.
    model->buildExtensionFamilyMenus(extensions, ProcessMenuModel::FamilyEntries::ProcessesAndLinks);
    QMenu* const again = extensions.actions().at(2)->menu();
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(textsOf(*again).count(QStringLiteral("Fancy Blur process")), 1);

    // The links alone by default.
    model->buildExtensionFamilyMenus(extensions);
    QMenu* const links = extensions.actions().at(2)->menu();
    ASSERT_NE(links, nullptr);
    EXPECT_EQ(textsOf(*links), (QStringList{QStringLiteral("Manage..."), QStringLiteral("Homepage")}));
}

// An extension's process of the category of the processes that run with no window, `non_interactive`, has no menu of
// the Process menu: its extension's family menu lists it, as it lists the extension's other processes, bound to its
// descriptor, and no category's menu does.
TEST_F(ProcessMenuModelTest, AProcessThatRunsWithNoWindowIsListedInItsFamilysMenu)
{
    m_processes.add("extensions.blur", "Fancy Blur process");
    m_processes.add("extensions.batch", "Fancy Batch process");
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    an::ui::extensions::ExtensionLoadResult fancy = packageOf("fancy", "Fancy Tools", "extensions.blur", "filters");
    an::ui::extensions::ExtensionProcessManifest batch = fancy.manifest->processes.front();
    batch.id = "extensions.batch";
    batch.display_name = "Fancy Batch process";
    batch.action_object_name = "actionExtension_fancy_batch";
    batch.category = "non_interactive";
    fancy.manifest->processes.push_back(batch);
    model->setExtensions({fancy});

    QMenu extensions;
    model->buildExtensionFamilyMenus(extensions, ProcessMenuModel::FamilyEntries::ProcessesAndLinks);
    ASSERT_EQ(textsOf(extensions), (QStringList{QStringLiteral("-"), QStringLiteral("Fancy Tools")}));
    QMenu* const family = extensions.actions().at(1)->menu();
    ASSERT_NE(family, nullptr);
    EXPECT_EQ(textsOf(*family),
              (QStringList{QStringLiteral("Fancy Blur process"), QStringLiteral("Fancy Batch process"),
                           QStringLiteral("-"), QStringLiteral("Manage..."), QStringLiteral("Homepage")}));

    QAction* const listed = model->extensionAction("actionExtension_fancy_batch");
    ASSERT_NE(listed, nullptr);
    EXPECT_EQ(family->actions().at(1), listed) << "the family's menu holds the process's action";
    EXPECT_EQ(listed->property("anRegistryDescriptorId").toString(), QStringLiteral("extensions.batch"));
    EXPECT_NE(std::find(m_bound.begin(), m_bound.end(), "actionExtension_fancy_batch -> extensions.batch"),
              m_bound.end())
        << "bound to its descriptor, which the launch runs";
    for (const ProcessMenuModel::Category& category : ProcessMenuModel::categories())
    {
        const std::vector<QAction*> inCategory = model->extensionActionsIn(category.menuId);
        EXPECT_EQ(std::find(inCategory.begin(), inCategory.end(), listed), inCategory.end())
            << "no menu of the Process menu lists it: " << category.menuId;
    }
}

// The model says what changed, so a shell builds the menus that show it again: the extensions when they change, both
// stored lists when they are read again - dropping, after the extensions changed, the processes the registry no longer
// holds - and nothing to a listener that unsubscribed.
TEST_F(ProcessMenuModelTest, SaysWhatChanged)
{
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    std::vector<ProcessMenuModel::Change> told;
    const std::size_t subscription =
        model->subscribe([&told](const ProcessMenuModel::Change change) { told.push_back(change); });
    model->setExtensions({});
    EXPECT_EQ(told, std::vector<ProcessMenuModel::Change>{ProcessMenuModel::Change::Extensions});

    ASSERT_TRUE(m_recent.record("color.actionSCNR", "SCNR Filter"));
    ASSERT_TRUE(m_recent.record("gone.actionNothing", "Gone"));
    m_favourites.setIds({"color.actionSCNR", "gone.actionNothing"});
    told.clear();
    model->refreshStoredLists();
    EXPECT_EQ(told, (std::vector<ProcessMenuModel::Change>{ProcessMenuModel::Change::RecentProcesses,
                                                           ProcessMenuModel::Change::FavoriteProcesses}));
    EXPECT_EQ(m_recent.entries().size(), 2u) << "nothing is dropped unless the extensions changed";

    model->pruneMissingOnNextRefresh();
    model->refreshStoredLists();
    ASSERT_EQ(m_recent.entries().size(), 1u);
    EXPECT_EQ(m_recent.entries().front().id, "color.actionSCNR");
    EXPECT_EQ(m_favourites.ids(), std::set<std::string>{"color.actionSCNR"});
    model->refreshStoredLists();
    EXPECT_EQ(m_recent.entries().size(), 1u);

    told.clear();
    model->unsubscribe(subscription);
    model->notifyChanged(ProcessMenuModel::Change::Extensions);
    EXPECT_TRUE(told.empty());
}

namespace
{

/// The submenu of a menu under a text; null for none.
[[nodiscard]] QMenu* submenuOf(const QMenu& menu, const QString& text)
{
    for (const QAction* action : menu.actions())
    {
        if (action->menu() != nullptr && action->text() == text)
        {
            return action->menu();
        }
    }
    return nullptr;
}

/// An entry an extension contributed, as its list states it.
[[nodiscard]] an::ui::extensions::ContributedProcessEntry
contributedEntry(std::string idSuffix, std::string displayName, std::string group = {}, std::string category = {})
{
    an::ui::extensions::ContributedProcessEntry entry;
    entry.id_suffix = std::move(idSuffix);
    entry.display_name = std::move(displayName);
    entry.group = std::move(group);
    entry.category = std::move(category);
    return entry;
}

} // namespace

// What an extension contributed is listed in its family's menu after the family's own entries, in the order the
// extension stated it: an entry stating a group in a submenu of that name, made where the group's first entry is, one
// stating none in the family's menu itself; each an action of its own, bound to its descriptor and tagged, its summary
// its tooltip. Both families' menus list them: the one that lists the processes first and the one of links alone.
TEST_F(ProcessMenuModelTest, ContributedEntriesJoinTheirFamilysMenuByGroupInTheOrderStated)
{
    m_processes.add("extensions.blur", "Fancy Blur process");
    for (const auto& [id, name] :
         {std::pair{"fancy.m42", "M42 Tone"}, std::pair{"fancy.stars", "Star Fix"},
          std::pair{"fancy.clusters", "Open Clusters"}, std::pair{"fancy.arms", "Spiral Arms"}})
    {
        m_processes.add(id, name);
    }
    ExtensionLoadResult fancy = packageOf("fancy", "Fancy Tools", "extensions.blur", "filters");
    auto m42 = contributedEntry("m42", "M42 Tone", "Nebulae");
    m42.summary = "Brings out the faint arms";
    fancy.contributed_processes = {m42, contributedEntry("stars", "Star Fix"),
                                   contributedEntry("clusters", "Open Clusters", "Clusters"),
                                   contributedEntry("arms", "Spiral Arms", "Nebulae")};
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    model->setExtensions({fancy});

    QMenu extensions;
    model->buildExtensionFamilyMenus(extensions, ProcessMenuModel::FamilyEntries::ProcessesAndLinks);
    QMenu* const family = submenuOf(extensions, QStringLiteral("Fancy Tools"));
    ASSERT_NE(family, nullptr);
    EXPECT_EQ(textsOf(*family),
              (QStringList{QStringLiteral("Fancy Blur process"), QStringLiteral("-"), QStringLiteral("Manage..."),
                           QStringLiteral("Homepage"), QStringLiteral("-"), QStringLiteral("Nebulae"),
                           QStringLiteral("Star Fix"), QStringLiteral("Clusters")}));
    QMenu* const nebulae = submenuOf(*family, QStringLiteral("Nebulae"));
    ASSERT_NE(nebulae, nullptr);
    EXPECT_EQ(textsOf(*nebulae), (QStringList{QStringLiteral("M42 Tone"), QStringLiteral("Spiral Arms")}));
    QMenu* const clusters = submenuOf(*family, QStringLiteral("Clusters"));
    ASSERT_NE(clusters, nullptr);
    EXPECT_EQ(textsOf(*clusters), QStringList{QStringLiteral("Open Clusters")});

    ASSERT_FALSE(nebulae->actions().isEmpty());
    QAction* const tone = nebulae->actions().front();
    EXPECT_EQ(tone->objectName(), QStringLiteral("actionExtension_fancy_m42"));
    EXPECT_EQ(tone->property("anRegistryDescriptorId").toString(), QStringLiteral("fancy.m42"));
    EXPECT_EQ(tone->property(ProcessMenuModel::kMenuTagProperty).toString(), QStringLiteral("extension"));
    EXPECT_EQ(tone->toolTip(), QStringLiteral("Brings out the faint arms"));
    EXPECT_EQ(model->extensionAction("actionExtension_fancy_m42"), tone);
    EXPECT_NE(std::find(m_bound.begin(), m_bound.end(), "actionExtension_fancy_m42 -> fancy.m42"), m_bound.end());

    // The links alone come before what it contributed too, and the menus built again hold each entry once.
    model->buildExtensionFamilyMenus(extensions);
    QMenu* const links = submenuOf(extensions, QStringLiteral("Fancy Tools"));
    ASSERT_NE(links, nullptr);
    EXPECT_EQ(textsOf(*links),
              (QStringList{QStringLiteral("Manage..."), QStringLiteral("Homepage"), QStringLiteral("-"),
                           QStringLiteral("Nebulae"), QStringLiteral("Star Fix"), QStringLiteral("Clusters")}));
    QMenu* const nebulaeAgain = submenuOf(*links, QStringLiteral("Nebulae"));
    ASSERT_NE(nebulaeAgain, nullptr);
    EXPECT_EQ(textsOf(*nebulaeAgain), (QStringList{QStringLiteral("M42 Tone"), QStringLiteral("Spiral Arms")}));
}

// An entry stating a category joins that menu of the Process menu, with an action of its own named for the entry even
// where a catalogue action would answer for its name; one stating none joins none.
TEST_F(ProcessMenuModelTest, AnEntryWithACategoryJoinsThatMenuWithAnActionOfItsOwn)
{
    m_processes.add("extensions.blur", "Fancy Blur process");
    m_processes.add("fancy.levels", "Levels Script");
    m_processes.add("fancy.stars", "Star Fix");
    ExtensionLoadResult fancy = packageOf("fancy", "Fancy Tools", "extensions.blur", "filters");
    fancy.contributed_processes = {contributedEntry("levels", "Levels Script", {}, "transforms"),
                                   contributedEntry("stars", "Star Fix")};
    ProcessMenuModel::Dependencies deps = dependencies();
    // A lookup that answers a catalogue action for every name: a manifest process may be bound to one, an entry never.
    QAction* const histogram = m_catalogue.action("actionHistogram_Transformation");
    ASSERT_NE(histogram, nullptr);
    deps.actions = [histogram](std::string_view) { return histogram; };
    ProcessMenuModel model(std::move(deps));
    model.setExtensions({fancy});

    const std::vector<QAction*> transforms = model.extensionActionsIn("menuIntensity_Transformations");
    ASSERT_EQ(transforms.size(), 1u);
    EXPECT_NE(transforms.front(), histogram);
    EXPECT_EQ(transforms.front()->objectName(), QStringLiteral("actionExtension_fancy_levels"));
    EXPECT_EQ(transforms.front()->text(), QStringLiteral("Levels Script"));
    EXPECT_FALSE(histogram->property("anRegistryDescriptorId").toString().contains(QStringLiteral("fancy.levels")));

    QMenu intensity;
    model.appendExtensionProcesses(intensity, "menuIntensity_Transformations");
    EXPECT_EQ(textsOf(intensity), (QStringList{QStringLiteral("-"), QStringLiteral("Levels Script")}));
    for (const auto& category : ProcessMenuModel::categories())
    {
        for (const QAction* action : model.extensionActionsIn(category.menuId))
        {
            EXPECT_NE(action->text(), QStringLiteral("Star Fix")) << "listed in " << category.menuId;
        }
    }
    EXPECT_NE(model.extensionAction("actionExtension_fancy_stars"), nullptr) << "it has its action all the same";
}

// An entry the extension withdrew leaves its family's menu and its category's at once, and the recent processes and the
// favourites at their next refresh, as a removed extension's process does; the entry it still states stays.
TEST_F(ProcessMenuModelTest, AWithdrawnEntryLeavesTheMenusAndAtTheNextRefreshTheStoredLists)
{
    m_processes.add("extensions.blur", "Fancy Blur process");
    m_processes.add("fancy.m42", "M42 Tone");
    m_processes.add("fancy.stars", "Star Fix");
    ExtensionLoadResult fancy = packageOf("fancy", "Fancy Tools", "extensions.blur", "filters");
    fancy.contributed_processes = {contributedEntry("m42", "M42 Tone", "Nebulae", "tone_brightness"),
                                   contributedEntry("stars", "Star Fix")};
    const std::unique_ptr<ProcessMenuModel> model = this->model();
    model->setExtensions({fancy});
    ASSERT_TRUE(m_recent.record("fancy.m42", "M42 Tone"));
    ASSERT_TRUE(m_recent.record("fancy.stars", "Star Fix"));
    m_favourites.setIds({"fancy.m42", "fancy.stars"});
    ASSERT_EQ(model->extensionActionsIn("menuAdjustments").size(), 1u);

    // Withdrawn: the registry holds the entry no more, and the extension states Star Fix alone.
    m_processes.remove("fancy.m42");
    fancy.contributed_processes = {contributedEntry("stars", "Star Fix")};
    model->setExtensions({fancy});
    EXPECT_TRUE(model->extensionActionsIn("menuAdjustments").empty());
    EXPECT_EQ(model->extensionAction("actionExtension_fancy_m42"), nullptr);
    QMenu extensions;
    model->buildExtensionFamilyMenus(extensions, ProcessMenuModel::FamilyEntries::ProcessesAndLinks);
    QMenu* const family = submenuOf(extensions, QStringLiteral("Fancy Tools"));
    ASSERT_NE(family, nullptr);
    EXPECT_EQ(textsOf(*family),
              (QStringList{QStringLiteral("Fancy Blur process"), QStringLiteral("-"), QStringLiteral("Manage..."),
                           QStringLiteral("Homepage"), QStringLiteral("-"), QStringLiteral("Star Fix")}));

    model->refreshStoredLists();
    EXPECT_EQ(m_recent.entries().size(), 2u) << "nothing is dropped until the extensions are said to have changed";
    model->pruneMissingOnNextRefresh();
    model->refreshStoredLists();
    ASSERT_EQ(m_recent.entries().size(), 1u);
    EXPECT_EQ(m_recent.entries().front().id, "fancy.stars");
    EXPECT_EQ(m_favourites.ids(), std::set<std::string>{"fancy.stars"});
}
