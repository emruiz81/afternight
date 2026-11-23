// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The process menus as data: the categories, the favourite and the recent processes, and what the installed
/// extensions add; a menu is made from them when a shell asks for one.

#include "AfterNightShellCoreExport.h"

#include "services/ProcessLaunchService.h"

#include "app/ActionLookup.h"
#include "app/ProcessMenuState.h"
#include "theme/ComponentParts.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QAction;
class QMenu;

namespace an::ui::extensions
{
struct ExtensionLoadResult;
}

namespace an::ui::shell
{

class FavoriteProcessesModel;
class RecentProcessesModel;

/// What the process menus hold apart from the catalogue's actions: the favourite processes, the processes used last,
/// and what the installed extensions add - each of an extension's processes as an action in the menu of its category,
/// bound to its registry descriptor, each process an extension contributed while it ran as an action of its own, and
/// each extension once, by name, in the Extensions menu. A shell makes its menus from it on demand and makes them again
/// when it says it changed (`subscribe`). Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API ProcessMenuModel
{
public:
    /// One of the Process menu's categories: the menu that holds it and its title.
    struct Category
    {
        std::string_view menuId;
        QString title;
    };

    /// What changed: the shell makes the menus that show it again.
    enum class Change : std::uint8_t
    {
        RecentProcesses,
        FavoriteProcesses,
        Extensions,
    };

    /// What a family's menu of the Extensions menu holds.
    enum class FamilyEntries : std::uint8_t
    {
        /// Manage, and the family's homepage, repository and support pages.
        LinksOnly,
        /// The extension's processes, then a separator and the links.
        ProcessesAndLinks,
    };

    /// The property of an action of an extension's process that holds the quiet tag a menu shows after its text,
    /// "extension" (`process_registry::extensionProcessTag`). An action of the application has none.
    static constexpr const char* kMenuTagProperty = an::ui::parts::kMenuTagProperty;

    struct Dependencies
    {
        RecentProcessesModel* recentProcesses = nullptr;
        FavoriteProcessesModel* favoriteProcesses = nullptr;
        /// How a process is found by its id; empty, the process registry's.
        an::ui::ProcessDescriptorLookup lookup;
        /// The catalogue's actions, which an extension's process may be bound to in place of one of its own.
        an::ui::ActionLookup actions;
        /// Launches a registry descriptor; whether it was launched.
        std::function<bool(const std::string& descriptorId)> launch;
        /// Opens where the installed extensions are managed.
        std::function<void()> manageExtensions;
        /// Binds an action of an extension's process to its registry descriptor.
        std::function<void(QAction* action, const std::string& category, const std::string& descriptorId)>
            bindExtensionAction;
    };

    /// What the lists' own entries ask of the shell's windows; an empty one does nothing.
    struct Hooks
    {
        /// Clears the processes used last, asking first.
        std::function<void()> clearRecentProcesses;
        /// Lets a person choose the favourite processes.
        std::function<void()> manageFavoriteProcesses;
        /// Clears the favourite processes, asking first.
        std::function<void()> clearFavoriteProcesses;
    };

    explicit ProcessMenuModel(Dependencies dependencies);
    ~ProcessMenuModel();

    ProcessMenuModel(const ProcessMenuModel&) = delete;
    ProcessMenuModel& operator=(const ProcessMenuModel&) = delete;
    ProcessMenuModel(ProcessMenuModel&&) = delete;
    ProcessMenuModel& operator=(ProcessMenuModel&&) = delete;

    /// The sixteen categories of the Process menu, in its order: the menus of the menu tree's Process menu.
    [[nodiscard]] static std::vector<Category> categories();

    /// The menu of the Process menu an extension's process of a registry category joins; empty for a category no menu
    /// takes.
    [[nodiscard]] static std::string_view menuOfCategory(std::string_view category);

    /// Takes what the installed packages `discovered` add: an action for each process of each package that is not
    /// invalid - or the catalogue's action of the same name, bound in its place - in the menu of its category; an
    /// action of its own for each process its extension contributed (`ExtensionLoadResult::contributed_processes`),
    /// never a catalogue action, in the menu of the category it states, when it states one; each carrying the extension
    /// tag
    /// (`kMenuTagProperty`), and each extension's family. What an earlier call added goes, and a catalogue action it
    /// bound loses the tag. Says it changed.
    void setExtensions(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered);

    /// Binds again the action of each process the packages `discovered` provide, now that their environments changed.
    void rebindExtensions(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered);

    /// The actions an extension's processes add to the menu of an id, in the packages' order.
    [[nodiscard]] std::vector<QAction*> extensionActionsIn(std::string_view menuId) const;

    /// Every action that launches a descriptor other than its own: those the extensions added, and the catalogue's
    /// actions an extension's process was bound to.
    [[nodiscard]] std::vector<QAction*> extensionActions() const;

    /// The action an extension's process added under an object name; null when there is none.
    [[nodiscard]] QAction* extensionAction(std::string_view objectName) const;

    [[nodiscard]] const std::vector<ExtensionFamily>& extensionFamilies() const noexcept;

    /// The name of an installed extension's family, by the extension's id; empty for one the model does not hold.
    [[nodiscard]] QString familyOf(std::string_view extensionId) const;

    /// Makes `menu` the favourite processes: each by its name, in the order of the names, launching its process; then
    /// Manage Favorites and Clear favorites list, the latter offered when there is a favourite.
    void buildFavoriteProcessesMenu(QMenu& menu) const;

    /// Makes `menu` the processes used last, most recent first and no more than the limit, each launching its process -
    /// one the registry no longer holds offered disabled - then Clear recent processes list; with none the menu is
    /// empty and withdrawn.
    void buildRecentProcessesMenu(QMenu& menu) const;

    /// Adds to a category's menu, after a separator, the actions the extensions add to the menu of `menuId`; first
    /// takes away what an earlier call added.
    void appendExtensionProcesses(QMenu& menu, std::string_view menuId) const;

    /// Adds to the Extensions menu, after a separator, a menu for each extension's family: Manage, and its homepage,
    /// repository and support pages - after the extension's processes when `entries` asks for them, the very actions
    /// its category's menu holds - then, after a separator, the processes the extension contributed, in the order it
    /// stated them, each one stating a group in a submenu of that name; and first takes away what an earlier call
    /// added.
    void buildExtensionFamilyMenus(QMenu& extensionsMenu, FamilyEntries entries = FamilyEntries::LinksOnly) const;

    /// Replaces the hooks.
    void setHooks(Hooks hooks);

    /// Reads the favourite and the recent processes again from the settings - dropping the ones the registry no longer
    /// holds when that was asked for since the last refresh - keeps the recent ones to their limit, stores them, and
    /// says both changed.
    void refreshStoredLists();

    /// The next refresh drops the stored processes the registry no longer holds: the installed extensions changed.
    void pruneMissingOnNextRefresh() noexcept;

    /// Calls `listener` with each change until it is unsubscribed or the model goes.
    /// @return what unsubscribes it.
    std::size_t subscribe(std::function<void(Change change)> listener);

    void unsubscribe(std::size_t subscription);

    /// Tells the listeners a list changed.
    void notifyChanged(Change change) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
