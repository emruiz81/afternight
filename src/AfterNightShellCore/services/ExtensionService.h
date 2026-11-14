// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The installed extensions: finding and registering them, watching for installs and removals, and the extension host
/// sessions their processes run in.

#include "AfterNightShellCoreExport.h"

#include <QString>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ImageViewControl;

namespace an::ui::extensions
{
class ExtensionHostLaunchCoordinator;
struct ExtensionOperationCancellationToken;
struct ExtensionLoadResult;
} // namespace an::ui::extensions

namespace an::ui::shell
{

class ApplicationCore;

/// What the application does about its extensions, with no window of its own: it registers the installed packages'
/// processes - the ones the start-up catalogue names at once, every one once a worker has found them - binds each to
/// the extension host that runs it, and puts them in the process menus (`ProcessMenuModel`); it prepares their
/// environments and removes the ones no package uses on the worker; it watches the extension folders and finds the
/// packages again after an install or a removal. It owns the extension host's launch coordinator, which runs the
/// sessions, and tells it of the application's views and theme; the SDK the sessions talk to reads the views, opens
/// images, sequences, questions and an extension's settings through it.
///
/// The questions and an extension's settings are asked through the shell's windows (`Hooks`). Used on the GUI thread;
/// its own work on workers comes back through the GUI thread's event loop and is dropped once the service is gone.
class AFTERNIGHTSHELLCORE_API ExtensionService
{
public:
    /// What only a shell's windows can do for the extensions; an empty one refuses.
    struct Hooks
    {
        /// Asks a yes-or-no question without waiting for the answer, which `answered` is given - `QMessageBox::Yes` or
        /// not - once the person answers.
        std::function<void(const QString& title, const QString& text, bool warn, std::function<void(int)> answered)>
            askYesNo;
        /// Opens an extension's settings; false, saying why in `diagnostic`, when it cannot.
        std::function<bool(std::string_view extensionId, QString* diagnostic)> openExtensionSettings;
    };

    explicit ExtensionService(ApplicationCore& core);
    ~ExtensionService();

    ExtensionService(const ExtensionService&) = delete;
    ExtensionService& operator=(const ExtensionService&) = delete;
    ExtensionService(ExtensionService&&) = delete;
    ExtensionService& operator=(ExtensionService&&) = delete;

    void setHooks(Hooks hooks);

    /// Registers the installed packages and puts their processes in the menus - those the start-up catalogue names now,
    /// then every one once a worker has found them - and starts watching the extension folders. Once.
    void start();

    /// Finds the installed packages again a moment from now: what an install or a removal changed.
    void refreshSoon();

    /// Tells the SDK the sessions talk to what it reads of the application, once the stores are open, and reads the
    /// rate a native extension window previews at.
    void configureSdk();

    /// The sessions of the extension host that are running.
    [[nodiscard]] std::size_t activeSessionCount() const;

    /// Stops every session and the pump that serves them.
    void stopSessions();

    /// Stops the work the service runs on workers - finding, preparing and cleaning the environments - and the launch
    /// preparations.
    void cancelBackgroundWork();

    /// What stops that work.
    [[nodiscard]] const an::ui::extensions::ExtensionOperationCancellationToken& cancellationToken() const noexcept;

    /// The theme changed: the hosts' windows follow it.
    void themeChanged();

    /// The settings were applied: the rate a native window previews at is read again.
    void settingsApplied();

    // A view of the application was made, changed, renamed or went: the sessions are told.
    void viewCreated(const ImageViewControl* view);
    void viewUpdated(const ImageViewControl* view);
    void viewRenamed(const std::string& newName, const std::string& oldName, const ImageViewControl* view);
    void viewDestroyed(const ImageViewControl* view);

    /// The packages are as `discovered` says now that an extension's environment was prepared: their launches are bound
    /// again, the actions take what they are, and the stored process lists drop what the registry no longer holds.
    void extensionsPrepared(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered);

    /// The extension host's launch coordinator, which runs the sessions.
    [[nodiscard]] an::ui::extensions::ExtensionHostLaunchCoordinator& launchCoordinator() noexcept;

    /// The titles of the extensions' window's pages: the installed extensions' status, the manager, and the manager's
    /// two tabs.
    [[nodiscard]] static QString statusPageTitle();
    [[nodiscard]] static QString managerPageTitle();

    /// Whether a page of the settings asked for by its title is one of the extensions' window.
    [[nodiscard]] static bool isExtensionsPageTitle(const QString& pageName);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
