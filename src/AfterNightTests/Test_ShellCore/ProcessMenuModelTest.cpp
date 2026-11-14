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
