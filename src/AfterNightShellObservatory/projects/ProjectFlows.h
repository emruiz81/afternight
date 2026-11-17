// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the Observatory window does about projects: what its project actions do, what the top bar's switcher shows,
/// and what a refusal of the project service is said as.

#include "AfterNightShellObservatoryExport.h"

#include "projects/ProjectService.h"

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

class QMenu;
class QMessageBox;
class QWidget;

namespace an::ui
{
class NewProjectDialog;
struct NewProjectChoice;
} // namespace an::ui

namespace an::ui::shell
{
class ApplicationCore;
} // namespace an::ui::shell

namespace an::ui::observatory
{

class ShellActions;
class TopBar;

/// The window's part in projects. The service (`an::ui::shell::ProjectService`) makes, opens and closes them and shows
/// nothing; this unit connects the shell's five project actions to it, keeps the top bar's switcher saying which
/// project is open, fills the switcher's menu, and says what the service refuses.
///
/// - **New Project...** opens the New Project dialog over the window, from the settings - the default location and
///   the history's compression - and the open images that show a file. Accepted, the project is made and opened, and
///   those images join it when the person kept the choice; a folder that is a project already is opened instead.
/// - **Open Project...** asks for a project's folder and opens it. **Switch Project...** opens the switcher's menu:
///   the open project, checked; the projects used last; then New project..., Open project... and Close project.
/// - **Close Project** closes the open project; the images stay open, with no project.
/// - **Project Settings...** opens the Settings at its Projects page.
///
/// A refusal is said in a message over the window. A project locked by a session elsewhere is said naming the machine
/// and the application that hold it, with `Open anyway`, which takes the lock. No dialog blocks: each is opened over
/// the window and answered when the person answers it.
///
/// While a project is open, an image opened from a file joins it. As the window starts, the project used last is
/// opened when the settings ask for it; one that cannot be opened is said in the window's transient message once the
/// window shows, with `Open...` to try it as Open Project does.
///
/// The window owns the unit and lets go of it before the top bar and the core's services go.
class AFTERNIGHTSHELLOBSERVATORY_API ProjectFlows final : public QObject
{
public:
    /// An open image a project can take: the file a view shows.
    struct OpenImage
    {
        QString filePath;
        bool sequence = false;
    };

    /// What the unit asks of the window.
    struct Hooks
    {
        /// The open views that show a file.
        std::function<std::vector<OpenImage>()> openImages;
        /// Shows a transient message; `act` is what its action does.
        std::function<void(const QString& text, const QString& detail, const QString& action,
                           std::function<void()> act)>
            snackbar;
        /// Opens the Settings at the page of a title.
        std::function<void(const QString& pageTitle)> openSettingsAt;
    };

    /// Asks for a folder: the dialog's parent, its caption and the folder it starts in; empty when cancelled.
    using FolderChooser = std::function<QString(QWidget* parent, const QString& caption, const QString& start)>;

    ProjectFlows(QWidget& window, an::ui::shell::ApplicationCore& core, ShellActions& actions, TopBar& topBar,
                 Hooks hooks);
    ~ProjectFlows() override;

    ProjectFlows(const ProjectFlows&) = delete;
    ProjectFlows& operator=(const ProjectFlows&) = delete;
    ProjectFlows(ProjectFlows&&) = delete;
    ProjectFlows& operator=(ProjectFlows&&) = delete;

    void newProject();
    void openProject();
    void switchProject();
    void closeProject();
    void projectSettings();

    /// Opens the project of a folder; what the service refuses is said.
    void openProjectFolder(const QString& folder,
                           an::ui::shell::ProjectLockPolicy policy = an::ui::shell::ProjectLockPolicy::Respect);

    /// Opens the project used last when the settings ask for it at the start; what kept it from opening is kept to
    /// be said.
    void restoreLastProject();

    /// Says what kept the start's project from opening, once; nothing when it opened or none was asked for.
    void sayRestoreRefusal();

    /// A view was added: while a project is open, the file it shows joins it.
    void viewAdded(const QString& filePath, bool sequence);

    /// Where Open Project... asks for a folder: the application's folder dialog, unless a suite gives its own.
    void setFolderChooser(FolderChooser chooser);

    /// The New Project dialog while it is open; null otherwise.
    [[nodiscard]] an::ui::NewProjectDialog* newProjectDialog() const noexcept;

    /// The message a refusal is said in while it is open; null otherwise.
    [[nodiscard]] QMessageBox* refusalMessage() const noexcept;

    /// Fills the switcher's menu: the open project, the ones used last, New project..., Open project... and Close
    /// project.
    void fillSwitcherMenu(QMenu& menu);

private:
    /// What the project service changed: the switcher, its menu, the actions and their commands follow.
    void refresh();
    /// Makes or opens what the New Project dialog was accepted with.
    void accepted(const an::ui::NewProjectChoice& choice);
    /// Says what the service refused.
    void say(const an::ui::shell::ProjectOutcome& outcome);

    QWidget& m_window;
    an::ui::shell::ApplicationCore& m_core;
    ShellActions& m_actions;
    TopBar& m_topBar;
    Hooks m_hooks;
    FolderChooser m_chooseFolder;
    QPointer<an::ui::NewProjectDialog> m_dialog;
    QPointer<QMessageBox> m_refusal;
    std::optional<an::ui::shell::ProjectOutcome> m_restoreRefusal;
    std::size_t m_subscription = 0;
};

} // namespace an::ui::observatory
