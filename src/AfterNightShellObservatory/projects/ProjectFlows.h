// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the Observatory window does about projects: what its project actions do, what the top bar's switcher shows,
/// and what a refusal of the project service is said as.

#include "AfterNightShellObservatoryExport.h"

#include "projects/ProjectHistoryBinder.h"
#include "projects/ProjectLayoutStore.h"
#include "projects/ProjectService.h"

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

class ImageViewControl;
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
///   the history's compression - the open images that show a file, and the size of their undo history. Accepted, the
///   project is made and opened, and those images join it when the person kept the choice, their undo steps moved
///   into the project or left with the session as the person chose; a folder that is a project already is opened
///   instead.
/// - **Open Project...** asks for a project's folder and opens it. **Switch Project...** opens the switcher's menu:
///   the open project, checked; the projects used last; then New project..., Open project... and Close project.
/// - **Close Project** closes the open project, and its images with it; an image the project does not hold stays
///   open. What leaving a project does - its arrangement kept, its images closed, each asked about as Close asks - is
///   the window's, asked by the project service itself, so a project one of whose images the person keeps open stays
///   open, and so does the one that was to take its place.
/// - **Project Settings...** opens the Settings at its Projects page.
///
/// A refusal is said in a message over the window. A project locked by a session elsewhere is said naming the machine
/// and the application that hold it, with `Open anyway`, which takes the lock. No dialog blocks: each is opened over
/// the window and answered when the person answers it.
///
/// As the window starts, the project used last is opened when the settings ask for it; one that cannot be opened is
/// said in the window's transient message once the window shows, with `Open...` to try it as Open Project does. That
/// an image opened while a project is open joins it is the shell core's (`ProjectHistoryBinder`), which the window
/// tells of each view.
///
/// **A project for a job.** A job that runs in the application - an integration - reaches the projects through the
/// shell (`ObservatoryShell::jobProjects`, the core's `JobProjects`): a job about to run with no project open gets
/// one in its output folder when the settings ask for it. What the window adds is this unit's, while it lives (the
/// core's `JobProjects::Hooks`). One made there is said in the window's
/// transient message, with the way to the setting; one that was there already is opened as Open Project opens it,
/// its arrangement put back, and said the same way; one that cannot be made or opened is said as any refusal is, and
/// the job runs outside any project.
///
/// A project that opens gets its workspaces and its images back (`ProjectLayoutStore`). The images whose files are
/// gone are said once, in one message over the window: `Locate...` asks where each is now - the project's record
/// follows the file and the image opens in the workspace it was in - and `Remove from the project` forgets them, their
/// files and their undo history untouched.
///
/// The window owns the unit and lets go of it before the top bar and the core's services go.
class AFTERNIGHTSHELLOBSERVATORY_API ProjectFlows final : public QObject
{
public:
    /// An open image a project can take: the view, and the file it shows.
    struct OpenImage
    {
        QPointer<ImageViewControl> view;
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
        /// Puts back the workspaces and the images of the project that just opened; false when it records none.
        std::function<bool()> restoreArrangement;
        /// Opens an image of the open project, found again, in the workspace the project lists it in.
        std::function<void(const QString& file, const QString& imageId)> openImage;
    };

    /// Asks for a folder: the dialog's parent, its caption and the folder it starts in; empty when cancelled.
    using FolderChooser = std::function<QString(QWidget* parent, const QString& caption, const QString& start)>;

    /// Asks for a file: the dialog's parent, its caption and the file or the folder it starts at; empty when
    /// cancelled.
    using FileChooser = std::function<QString(QWidget* parent, const QString& caption, const QString& start)>;

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

    /// Where Open Project... asks for a folder: the application's folder dialog, unless a suite gives its own.
    void setFolderChooser(FolderChooser chooser);

    /// Where Locate... asks for a file: the application's file dialog, unless a suite gives its own.
    void setFileChooser(FileChooser chooser);

    /// Says, once, the images of the open project that were not opened because their files are gone, with Locate...
    /// and Remove from the project. Nothing for none.
    void reportMissingImages(const std::vector<an::ui::shell::ProjectMissingImage>& missing);

    /// The message the images that were not opened are said in while it is open; null otherwise.
    [[nodiscard]] QMessageBox* missingImagesMessage() const noexcept;

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

    /// Says that a job runs in a project made of its output folder, or in the one that was there.
    void sayProjectForJob(bool made);
    /// Asks where each image that was not found is now; one found is the project's again and opens.
    void locateMissingImages(const std::vector<an::ui::shell::ProjectMissingImage>& missing);
    /// What says, while steps are copied into a project on another volume, how far the move is.
    [[nodiscard]] an::ui::shell::ProjectHistoryBinder::Progress moveProgress();

    QWidget& m_window;
    an::ui::shell::ApplicationCore& m_core;
    ShellActions& m_actions;
    TopBar& m_topBar;
    Hooks m_hooks;
    FolderChooser m_chooseFolder;
    FileChooser m_chooseFile;
    QPointer<an::ui::NewProjectDialog> m_dialog;
    QPointer<QMessageBox> m_refusal;
    QPointer<QMessageBox> m_missing;
    std::optional<an::ui::shell::ProjectOutcome> m_restoreRefusal;
    std::size_t m_subscription = 0;
};

} // namespace an::ui::observatory
