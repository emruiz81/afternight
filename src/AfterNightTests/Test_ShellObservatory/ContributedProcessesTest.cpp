// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A process an extension contributed while it ran is found and started as any process is: in the palette's processes
/// under its family and its group with the extension's tag; in its family's menu of the Extensions menu, in its group's
/// submenu after the family's own entries; and in the menu of the category it states. Its task and its rail item carry
/// its name, not its extension's; once started it is among the recent processes; made a favourite it is started from
/// the favourites; and withdrawn it is gone from all of them.

#include "Test_ShellObservatory/ObservatoryWindowCase.h"

#include "actions/ProcessMenuModel.h"
#include "chrome/MenuBarOnAlt.h"
#include "components/AnCommandPalette.h"
#include "components/AnIconRail.h"
#include "components/AnTaskHeader.h"
#include "pages/TaskPage.h"
#include "services/FavoriteProcessesModel.h"
#include "services/ExtensionService.h"
#include "services/RecentProcessesModel.h"
#include "tasks/TaskPresenter.h"

#include "app/ExtensionHostManager.h"
#include "app/ExtensionManager.h"
#include "app/ProcessRegistry.h"
#include "controls/ExtensionHostedProcess.h"
#include "controls/ProcessPageLayout.h"
#include "theme/ComponentParts.h"

#include <QAction>
#include <QCoreApplication>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPointer>
#include <QTest>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace an::tests::observatory
{

namespace
{

using an::ui::extensions::ContributedProcessEntry;
using an::ui::extensions::ExtensionHostedProcess;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::observatory::ObservatoryWindow;
using an::ui::shell::Task;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// Lets the work the application handed its pool at start - finding the installed extensions - land first.
void settleThePool()
{
    QThreadPool::globalInstance()->waitForDone();
    settle();
}

[[nodiscard]] ContributedProcessEntry entryOf(std::string idSuffix, std::string displayName, std::string group = {},
                                              std::string category = {})
{
    ContributedProcessEntry entry;
    entry.id_suffix = std::move(idSuffix);
    entry.display_name = std::move(displayName);
    entry.group = std::move(group);
    entry.category = std::move(category);
    return entry;
}

/// An extension of the cases' own, "Script Tools", whose manifest's process contributed two more as it ran - "M42
/// Tone" in the group "Nebulae" and the category of the Adjustments menu, and "Star Fix" in no group - listed as the
/// application lists the packages it found: registered, bound and put in the menus. Starting an entry makes the page
/// the launch coordinator makes for it, under the binding the entry is launched with, before any host starts. It goes
/// with the case.
class ContributingExtension
{
public:
    static constexpr char kExtensionId[] = "tests.scripts";
    static constexpr char kToneId[] = "tests.scripts.m42_tone";
    static constexpr char kStarsId[] = "tests.scripts.star_fix";

    explicit ContributingExtension(an::ui::shell::ApplicationCore& core) : m_core(core)
    {
        settleThePool();
        an::ui::extensions::ExtensionManifest manifest;
        manifest.id = kExtensionId;
        manifest.name = "Script Tools";
        an::ui::extensions::ExtensionProcessManifest runner;
        runner.id_suffix = "runner";
        runner.id = std::string(kExtensionId) + ".runner";
        runner.class_name = "Runner";
        runner.display_name = "Script Runner";
        runner.process_name = "Script Runner";
        runner.category = "filters";
        runner.action_object_name = an::ui::extensions::default_extension_action_object_name(kExtensionId, "runner");
        manifest.processes.push_back(runner);
        m_result.status = an::ui::extensions::ExtensionLoadStatus::Ready;
        m_result.manifest = manifest;
        // Installed and prepared, as a package whose processes start is.
        m_result.environment = an::ui::extensions::ExtensionEnvironmentSnapshot{};
        m_result.environment->state = an::ui::extensions::ExtensionEnvironmentState::Ready;
        contribute({entryOf("m42_tone", "M42 Tone", "Nebulae", "tone_brightness"), entryOf("star_fix", "Star Fix")});
    }

    ~ContributingExtension()
    {
        for (const QPointer<ExtensionHostedProcess>& page : m_pages)
        {
            if (!page.isNull())
            {
                page->close();
            }
        }
        settle();
        m_core.processMenus().setExtensions({});
        (void)an::ui::process_registry::ProcessRegistry::instance().removeExtensionProcesses();
        m_core.processMenus().pruneMissingOnNextRefresh();
        m_core.processMenus().refreshStoredLists();
        settle();
    }

    ContributingExtension(const ContributingExtension&) = delete;
    ContributingExtension& operator=(const ContributingExtension&) = delete;

    /// The extension states its list again, as one kept is registered and listed: what it no longer states is gone
    /// from the menus, and from the stored lists at their next refresh.
    void contribute(std::vector<ContributedProcessEntry> entries)
    {
        m_result.contributed_processes = std::move(entries);
        auto& registry = an::ui::process_registry::ProcessRegistry::instance();
        an::ui::extensions::ExtensionManager().registerDiscoveredProcesses({m_result}, registry);

        an::ui::extensions::ExtensionHostLaunchBinding host;
        host.descriptor_id = m_result.manifest->processes.front().id;
        host.extension_id = kExtensionId;
        host.display_name = "Script Runner";
        host.sdk_backend = "runtime";
        host.process_capabilities.interface_kind = an::ui::extensions::ExtensionInterface::Panel;
        for (const ContributedProcessEntry& entry : m_result.contributed_processes)
        {
            const an::ui::extensions::ExtensionHostLaunchBinding binding =
                an::ui::extensions::contributedProcessBinding(host, kExtensionId, entry);
            auto* const descriptor = registry.findMutable(binding.descriptor_id);
            ASSERT_NE(descriptor, nullptr) << binding.descriptor_id;
            descriptor->launch_single_image_checked = [this, binding](ImageViewControl* target, std::string*) {
                m_launched.push_back(binding.descriptor_id);
                ExtensionHostedProcess* const page = m_core.extensions().launchCoordinator().openStartingPage(
                    binding.descriptor_id, binding.display_name, binding.process_capabilities, target,
                    binding.sdk_backend);
                m_pages.push_back(page);
                return page != nullptr;
            };
        }
        m_core.processMenus().setExtensions({m_result});
        m_core.processMenus().pruneMissingOnNextRefresh();
        m_core.processMenus().refreshStoredLists();
        settle();
    }

    [[nodiscard]] const std::vector<std::string>& launched() const noexcept
    {
        return m_launched;
    }

    /// The page the last start made; null when it made none.
    [[nodiscard]] ExtensionHostedProcess* lastPage() const
    {
        return m_pages.empty() ? nullptr : m_pages.back().data();
    }

private:
    an::ui::shell::ApplicationCore& m_core;
    ExtensionLoadResult m_result;
    std::vector<std::string> m_launched;
    std::vector<QPointer<ExtensionHostedProcess>> m_pages;
};

/// The entry of a menu by its text; null for none.
[[nodiscard]] QAction* entryNamed(const QMenu& menu, const QString& text)
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

class ContributedProcesses : public ObservatoryWindowCase
{
protected:
    /// The page a start made is a task named for the entry - its header and its rail item - over the image.
    static void expectATaskNamed(const ExtensionHostedProcess* page, const QString& name, const QString& image)
    {
        ASSERT_NE(page, nullptr);
        an::ui::observatory::TaskPresenter* const presenter = window().taskPresenter();
        ASSERT_NE(presenter, nullptr);
        const Task* const task = presenter->model().taskFor(page);
        ASSERT_NE(task, nullptr) << "the start made no task";
        an::ui::observatory::TaskPage* const shown = window().taskPage(task->id);
        ASSERT_NE(shown, nullptr);
        EXPECT_EQ(shown->header()->processName(), name);
        const QString railId = ObservatoryWindow::railTaskId(task->id);
        const std::vector<an::ui::RailItem>& items = window().rail()->items();
        const auto item = std::find_if(items.begin(), items.end(),
                                       [&railId](const an::ui::RailItem& candidate) { return candidate.id == railId; });
        ASSERT_NE(item, items.end()) << "the rail shows no item of the task";
        EXPECT_TRUE(item->text.startsWith(name)) << item->text.toStdString();
        EXPECT_TRUE(window().rail()->itemToolTip(railId).contains(name));
        EXPECT_TRUE(window().rail()->itemToolTip(railId).contains(image));
        EXPECT_FALSE(window().rail()->itemToolTip(railId).contains(QStringLiteral("Script Runner")))
            << "named for the entry, not for the process that runs it";
    }

    /// The first recent process, as the stored list holds it.
    [[nodiscard]] static std::string mostRecent()
    {
        const auto& entries = core().recentProcesses().entries();
        return entries.empty() ? std::string() : entries.front().id;
    }

    /// Opens the palette on a query.
    static an::ui::AnCommandPalette& paletteOn(const QString& query)
    {
        window().openPalette();
        an::ui::AnCommandPalette& palette = *window().palette();
        EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return palette.isVisible(); }, 3000));
        palette.queryField()->clear();
        QTest::keyClicks(palette.queryField(), query);
        settle();
        return palette;
    }

    [[nodiscard]] static const an::ui::CommandRow* rowOf(const an::ui::AnCommandPalette& palette, const QString& id)
    {
        for (const an::ui::CommandRowGroup& group : palette.groups())
        {
            for (const an::ui::CommandRow& row : group.rows)
            {
                if (row.id == id)
                {
                    return &row;
                }
            }
        }
        return nullptr;
    }
};

} // namespace

// The palette lists an entry under its family and its group, with the extension's tag; run from there it starts, its
// task named for it, and it is the first of the recent processes.
TEST_F(ContributedProcesses, AnEntryIsFoundInThePaletteAndStartedFromIt)
{
    ContributingExtension extension(core());
    ImageViewControl* const view = openView(QStringLiteral("contributed-palette"));
    ASSERT_NE(view, nullptr);

    an::ui::AnCommandPalette& palette = paletteOn(QStringLiteral("M42 Tone"));
    const QString id = QStringLiteral("process:") + QLatin1String(ContributingExtension::kToneId);
    ASSERT_TRUE(palette.shownRowIds().contains(id)) << palette.shownRowIds().join(QStringLiteral(", ")).toStdString();
    const an::ui::CommandRow* const row = rowOf(palette, id);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->title, QStringLiteral("M42 Tone"));
    EXPECT_EQ(row->subtitle, QStringLiteral("Script Tools - Nebulae"));
    EXPECT_EQ(row->tag, QStringLiteral("extension"));

    // An entry in no group is under its family alone.
    palette.queryField()->clear();
    QTest::keyClicks(palette.queryField(), QStringLiteral("Star Fix"));
    settle();
    const an::ui::CommandRow* const stars =
        rowOf(palette, QStringLiteral("process:") + QLatin1String(ContributingExtension::kStarsId));
    ASSERT_NE(stars, nullptr);
    EXPECT_EQ(stars->subtitle, QStringLiteral("Script Tools"));

    palette.queryField()->clear();
    QTest::keyClicks(palette.queryField(), QStringLiteral("M42 Tone"));
    settle();
    palette.setSelectedId(id);
    QTest::keyClick(palette.queryField(), Qt::Key_Return);
    settle();
    ASSERT_EQ(extension.launched(), std::vector<std::string>{ContributingExtension::kToneId});
    expectATaskNamed(extension.lastPage(), QStringLiteral("M42 Tone"), QStringLiteral("contributed-palette"));
    // Its column says its family over its name, as an extension's task's column does, not the menu of the category it
    // states.
    ASSERT_NE(extension.lastPage()->pageLayout(), nullptr);
    EXPECT_EQ(extension.lastPage()->pageLayout()->category().toStdString(), "Script Tools");
    EXPECT_EQ(mostRecent(), ContributingExtension::kToneId);
    EXPECT_EQ(core().recentProcesses().entries().front().displayName, "M42 Tone");
}

// The Extensions menu lists an entry in its family's menu after the family's own entries - in its group's submenu when
// it states one - and the menu of the category it states lists it too, the same action, tagged; started from the
// family's menu, its task is named for it.
TEST_F(ContributedProcesses, AnEntryIsFoundInTheExtensionsMenuAndStartedFromIt)
{
    ContributingExtension extension(core());
    ImageViewControl* const view = openView(QStringLiteral("contributed-menu"));
    ASSERT_NE(view, nullptr);

    an::ui::observatory::MenuBarOnAlt& menus = window().menuBarOnAlt();
    QMenu* const extensions = menus.menu("menuExtensions");
    ASSERT_NE(extensions, nullptr);
    QAction* const familyEntry = entryNamed(*extensions, QStringLiteral("Script Tools"));
    ASSERT_NE(familyEntry, nullptr);
    QMenu* const family = familyEntry->menu();
    ASSERT_NE(family, nullptr);
    QStringList texts;
    for (const QAction* action : family->actions())
    {
        texts.append(action->isSeparator() ? QStringLiteral("-") : action->text());
    }
    EXPECT_EQ(texts, (QStringList{QStringLiteral("Script Runner"), QStringLiteral("-"), QStringLiteral("Manage..."),
                                  QStringLiteral("-"), QStringLiteral("Nebulae"), QStringLiteral("Star Fix")}))
        << "its processes, its links, then what it contributed";
    QAction* const nebulae = entryNamed(*family, QStringLiteral("Nebulae"));
    ASSERT_NE(nebulae, nullptr);
    ASSERT_NE(nebulae->menu(), nullptr);
    QAction* const tone = entryNamed(*nebulae->menu(), QStringLiteral("M42 Tone"));
    ASSERT_NE(tone, nullptr);
    EXPECT_EQ(tone->property(an::ui::parts::kMenuTagProperty).toString(), QStringLiteral("extension"));

    QMenu* const adjustments = menus.menu("menuAdjustments");
    ASSERT_NE(adjustments, nullptr);
    EXPECT_EQ(entryNamed(*adjustments, QStringLiteral("M42 Tone")), tone) << "the category's menu holds the same";
    QMenu* const filters = menus.menu("menuFilters");
    ASSERT_NE(filters, nullptr);
    EXPECT_EQ(entryNamed(*filters, QStringLiteral("Star Fix")), nullptr) << "an entry stating no category is in none";

    QAction* const starFix = entryNamed(*family, QStringLiteral("Star Fix"));
    ASSERT_NE(starFix, nullptr);
    starFix->trigger();
    settle();
    ASSERT_EQ(extension.launched(), std::vector<std::string>{ContributingExtension::kStarsId});
    expectATaskNamed(extension.lastPage(), QStringLiteral("Star Fix"), QStringLiteral("contributed-menu"));
    EXPECT_EQ(mostRecent(), ContributingExtension::kStarsId);
}

// Made a favourite from the palette's star, an entry is among the favourites and starts from there; withdrawn by its
// extension it is gone from the menus and the palette, and from the favourites and the recent processes at their next
// refresh, while the entry it still states stays.
TEST_F(ContributedProcesses, AFavouriteEntryStartsFromTheFavouritesAndAWithdrawnOneIsGoneFromEveryList)
{
    ContributingExtension extension(core());
    ImageViewControl* const view = openView(QStringLiteral("contributed-favourite"));
    ASSERT_NE(view, nullptr);
    core().favoriteProcesses().setIds({});

    an::ui::AnCommandPalette& palette = paletteOn(QStringLiteral("M42 Tone"));
    const QString id = QStringLiteral("process:") + QLatin1String(ContributingExtension::kToneId);
    const QRect star = palette.starRect(id);
    ASSERT_FALSE(star.isEmpty());
    QWidget* const viewport = palette.list()->viewport();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, viewport->mapFrom(&palette, star.center()));
    settle();
    core().favoriteProcesses().load();
    EXPECT_EQ(core().favoriteProcesses().ids(), std::set<std::string>{ContributingExtension::kToneId});
    palette.close();
    settle();

    QMenu favourites;
    core().processMenus().buildFavoriteProcessesMenu(favourites);
    QAction* const favourite = entryNamed(favourites, QStringLiteral("M42 Tone"));
    ASSERT_NE(favourite, nullptr);
    favourite->trigger();
    settle();
    ASSERT_EQ(extension.launched(), std::vector<std::string>{ContributingExtension::kToneId});
    expectATaskNamed(extension.lastPage(), QStringLiteral("M42 Tone"), QStringLiteral("contributed-favourite"));
    EXPECT_EQ(mostRecent(), ContributingExtension::kToneId);

    // Withdrawn: only Star Fix is stated now.
    extension.contribute({entryOf("star_fix", "Star Fix")});
    QMenu* const extensions = window().menuBarOnAlt().menu("menuExtensions");
    ASSERT_NE(extensions, nullptr);
    QAction* const familyEntry = entryNamed(*extensions, QStringLiteral("Script Tools"));
    ASSERT_NE(familyEntry, nullptr);
    EXPECT_EQ(entryNamed(*familyEntry->menu(), QStringLiteral("Nebulae")), nullptr);
    EXPECT_NE(entryNamed(*familyEntry->menu(), QStringLiteral("Star Fix")), nullptr);
    EXPECT_EQ(entryNamed(*window().menuBarOnAlt().menu("menuAdjustments"), QStringLiteral("M42 Tone")), nullptr);
    core().favoriteProcesses().load();
    EXPECT_TRUE(core().favoriteProcesses().ids().empty());
    for (const auto& recent : core().recentProcesses().entries())
    {
        EXPECT_NE(recent.id, ContributingExtension::kToneId);
    }
    an::ui::AnCommandPalette& searched = paletteOn(QStringLiteral("M42 Tone"));
    EXPECT_FALSE(searched.shownRowIds().contains(id));
    searched.close();
    settle();
}

} // namespace an::tests::observatory
