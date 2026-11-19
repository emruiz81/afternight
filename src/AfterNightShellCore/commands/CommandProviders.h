// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the command index lists, provider by provider: the catalogue's actions, the processes the registry holds, the
/// open views and the recent files, the settings pages and their rows. Each provider reads the application through the
/// functions it is given, so that a suite gives it a fixed world and the application its own (`ApplicationCore`).

#include "AfterNightShellCoreExport.h"

#include "commands/CommandIndex.h"

#include "app/ActionLookup.h"
#include "app/ProcessRegistry.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace an::ui
{
struct SettingsSearchPage;
}

namespace an::ui::shell
{

class MenuTree;

/// The catalogue's actions a menu of the menu bar holds, each once, titled by its text, its subtitle and keywords the
/// menus a person reads down to it (`Image > Mask`). An action that launches a process is left to the processes, and an
/// action no menu holds is no command a person reaches. Opening a file is an image's command, titled "Open file...";
/// every other action is a view's. A disabled action is listed dimmed - with what it says of why when it says - and
/// does not run; a running one is triggered.
class AFTERNIGHTSHELLCORE_API ActionCommands final : public ICommandProvider
{
public:
    struct Dependencies
    {
        /// The catalogue's action of an id.
        an::ui::ActionLookup actions;
        /// The menu bar the actions are found in; null, the application's.
        const MenuTree* menus = nullptr;
        /// Whether an action launches a process of the registry, which the processes list in its place.
        std::function<bool(std::string_view actionId)> launchesProcess;
    };

    explicit ActionCommands(Dependencies dependencies);

    void collect(std::vector<CommandEntry>& entries) const override;

    /// The id of an action's command.
    [[nodiscard]] static QString commandId(std::string_view actionId);

private:
    Dependencies m_dependencies;
};

/// Every process the registry holds, titled by its name, its category as its subtitle and a keyword, with its action's
/// icon and shortcut. A process the registry says is unavailable carries the reason and does not run - but one whose
/// extension's environment its launch prepares runs, and is prepared first. A process of one image that cannot run on
/// the image the actions act on is listed dimmed with what its guards say; with no image it runs, and its launch says
/// there is none, as the menu's does. A process can be a favourite. An extension's process carries the quiet tag
/// "extension" and is listed under its family - its extension's name - as its subtitle, and found by it too.
class AFTERNIGHTSHELLCORE_API ProcessCommands final : public ICommandProvider
{
public:
    struct Dependencies
    {
        /// The processes; empty, the registry's.
        std::function<std::vector<const an::ui::process_registry::ProcessDescriptor*>()> descriptors;
        /// What the guards of a process of one image say of the image it would act on; a result that is `ok()` when
        /// there is no image. Empty, every process passes.
        std::function<an::ui::process_registry::LaunchGuardResult(
            const an::ui::process_registry::ProcessDescriptor& descriptor)>
            imageGuards;
        /// Launches a process by its id; whether it was launched.
        std::function<bool(const std::string& descriptorId)> launch;
        std::function<bool(const std::string& descriptorId)> isFavorite;
        std::function<void(const std::string& descriptorId, bool favorite)> setFavorite;
        /// The catalogue's actions, whose icon and shortcut a process takes from the action it is bound to.
        an::ui::ActionLookup actions;
        /// The family of an extension by its id - the name its processes are listed under; empty for one not known, or
        /// with no lookup, and an extension's process is then listed under its category.
        std::function<QString(const std::string& extensionId)> extensionFamily;
    };

    explicit ProcessCommands(Dependencies dependencies);

    void collect(std::vector<CommandEntry>& entries) const override;

    /// The id of a process's command.
    [[nodiscard]] static QString commandId(const std::string& descriptorId);

private:
    Dependencies m_dependencies;
};

/// The open views, titled by their names, and the files opened last, titled by their file names with their folders
/// as subtitles: running a view activates it, running a file opens it again.
class AFTERNIGHTSHELLCORE_API ImageCommands final : public ICommandProvider
{
public:
    /// A view as the provider lists it.
    struct OpenView
    {
        QString name;
        QString filePath; ///< Empty for an image no file holds.
    };

    struct Dependencies
    {
        std::function<std::vector<OpenView>()> views;
        std::function<void(const QString& name)> activateView;
        /// The files opened last, most recent first.
        std::function<std::vector<QString>()> recentFiles;
        std::function<void(const QString& path)> openFile;
    };

    explicit ImageCommands(Dependencies dependencies);

    void collect(std::vector<CommandEntry>& entries) const override;

    [[nodiscard]] static QString viewCommandId(const QString& name);
    [[nodiscard]] static QString fileCommandId(const QString& path);

private:
    Dependencies m_dependencies;
};

/// The settings pages, each found by its title and its search words, and each page's rows, titled `Page > Row` - or
/// `Page > Section > Row` for a label the page shows in more than one section - and found by their sections too:
/// running one opens the Settings at its page.
class AFTERNIGHTSHELLCORE_API SettingsCommands final : public ICommandProvider
{
public:
    struct Dependencies
    {
        /// The pages; empty, the Settings dialog's (`an::ui::settingsSearchPages`).
        std::function<std::vector<an::ui::SettingsSearchPage>()> pages;
        /// Opens the Settings at the page of a title.
        std::function<void(const QString& pageTitle)> openPage;
    };

    explicit SettingsCommands(Dependencies dependencies);

    void collect(std::vector<CommandEntry>& entries) const override;

    [[nodiscard]] static QString pageCommandId(const QString& pageTitle);
    [[nodiscard]] static QString rowCommandId(const QString& pageTitle, const QString& section,
                                              const QString& rowLabel);

private:
    Dependencies m_dependencies;
};

} // namespace an::ui::shell
