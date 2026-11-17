// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What a project keeps of a shell's workspaces and of the images open in them, and how both are put back when the
/// project is opened again.

#include "AfterNightShellCoreExport.h"

#include "core/IShell.h"
#include "projects/ProjectDocument.h"

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class ImageViewControl;

namespace an::ui::shell
{

class ApplicationCore;
class WorkspaceModel;

/// The name of the file a project's arrangement is kept in, in its folder of workspaces.
inline constexpr char kProjectLayoutFileName[] = "layout.json";

/// An image a project lists as open whose file is not where the project has it.
struct ProjectMissingImage
{
    QString imageId;

    /// Where the project has the file.
    QString path;

    [[nodiscard]] bool operator==(const ProjectMissingImage&) const = default;
};

/// What restoring a project's arrangement came to.
struct ProjectLayoutRestored
{
    /// The views opened for the project's images, in the order the project lists them.
    std::vector<QPointer<ImageViewControl>> views;

    /// The images whose files are not there: left out, their places closed up.
    std::vector<ProjectMissingImage> missing;

    /// The workspace used last; empty when the project marks none.
    QString lastWorkspace;

    /// Whether the saved arrangement was applied. False when the project keeps none that can be, or was closed while
    /// its images opened: each view was then docked as a new view is.
    bool arranged = false;

    /// Whether the project whose arrangement this was is still the open one.
    bool projectStillOpen = false;
};

/// A project's arrangement: saved as the project is left, restored as it is opened.
///
/// **What is kept.** In the project's document, the workspaces - each with its name, its lock, the images docked in
/// it and whether it is the one used last - and the images that floated in windows of their own; each view with the
/// name its dock widget had. Beside it, in `workspaces/layout.json`, one layout of the dock library for every
/// workspace's dock area and every floating image window: where each dock widget is, by that name. The document says
/// which images were open, the layout where each was: the library's own list of what a layout has open names only
/// what was on screen, not a tab behind another nor a view of a workspace that was not shown. Only a view of an image
/// of the project is recorded - one view of each - so an image no file holds, and one the project does not hold, is
/// not opened again. A task's window and the console's are in no layout: their dock widgets are made to be skipped by
/// one. Nothing is written with no project open.
///
/// **Restoring** makes the shell's workspaces the project's - each under the id it had, since a dock area is named by
/// it - then opens the listed images as the person's files are opened, on workers, each view made and left undocked;
/// once every one is shown or has failed, the layout is applied to them, the locks are set and the shell is told the
/// workspace used last. A name of the layout stands for the view opened for the image the document lists under that
/// name: a view's dock widget is named anew each time it is made. An image whose file is not at its recorded path nor
/// at its path from the project's folder is left out, the layout closes up its place, and the shell is told of it. A
/// view the layout does not place - a project with no layout, or one that cannot be applied - is docked in its
/// workspace as a new view is. A view that was on screen before, which the layout does not know, floats: it belongs
/// to none of the project's workspaces. The GUI thread waits for none of it. A project with no workspace recorded
/// restores nothing, and the shell's workspaces stay as they are.
///
/// It owns no widget and shows nothing: what the shell does for it is in its hooks. Used on the GUI thread only.
class AFTERNIGHTSHELLCORE_API ProjectLayoutStore final
{
public:
    /// What the store asks of the shell whose workspaces it keeps.
    struct Hooks
    {
        /// The dock area of a workspace.
        std::function<DockTarget(const QString& workspaceId)> dockTarget;

        /// Makes the shell's workspaces the ones listed, in their order, each under its id and with its name: one the
        /// shell has under an id is renamed, one it lacks is made, and one it has that is not listed is closed, its
        /// views floating.
        std::function<void(const std::vector<ProjectWorkspace>& workspaces)> setWorkspaces;

        /// Opens files as the person's files are opened, on workers, each in a view of its own, and tells `done` on
        /// the GUI thread once every one has been shown or has failed. It returns at once.
        std::function<void(const QStringList& files, std::function<void()> done)> openFiles;

        /// Locks or unlocks a workspace's arrangement.
        std::function<void(const QString& workspaceId, bool locked)> setLocked;

        /// Docks a view the arrangement does not place, in a workspace; the one on screen for an empty id.
        std::function<void(ImageViewControl& view, const QString& workspaceId)> dockView;

        /// The restore ended.
        std::function<void(const ProjectLayoutRestored& restored)> restored;
    };

    /// @param core the application's core, which outlives the store.
    /// @param workspaces the shell's workspaces, which outlive the store.
    /// @param hooks what the shell does for the store.
    ProjectLayoutStore(ApplicationCore& core, WorkspaceModel& workspaces, Hooks hooks);
    ~ProjectLayoutStore();

    ProjectLayoutStore(const ProjectLayoutStore&) = delete;
    ProjectLayoutStore& operator=(const ProjectLayoutStore&) = delete;
    ProjectLayoutStore(ProjectLayoutStore&&) = delete;
    ProjectLayoutStore& operator=(ProjectLayoutStore&&) = delete;

    /// The file a project's arrangement is kept in.
    [[nodiscard]] static QString layoutFilePath(const QString& projectFolder);

    /// Writes the open project's arrangement whole: the layout's file, then the document's record of the workspaces.
    ///
    /// @return false, with nothing written, with no project open or while an arrangement is being restored - one half
    ///         restored is not the one to keep; false too when the document could not be written.
    bool save();

    /// Restores the open project's arrangement. It returns at once; `Hooks::restored` is told when it ends.
    ///
    /// @return false, with nothing changed, with no project open, for a project that records no workspace, or while
    ///         an arrangement is already being restored.
    bool restore();

    /// Whether an arrangement is being restored: its images are still opening.
    [[nodiscard]] bool isRestoring() const noexcept;

    /// A view was made, and is about to be placed. True when it is one the restore asked for - it shows the file of an
    /// image the project lists as open - which the caller then leaves undocked, for the arrangement to place.
    [[nodiscard]] bool takes(ImageViewControl& view);

    /// The views of the open project's images that are on screen.
    [[nodiscard]] std::vector<ImageViewControl*> projectViews() const;

    /// The workspace of the open project that was used last: the one the person is in, which a restore makes the one
    /// the project recorded - and that one already while the project's images open. Empty with no project open.
    [[nodiscard]] QString lastWorkspace() const;

    /// The workspace the open project lists an image in; empty for an image it lists in none.
    [[nodiscard]] QString workspaceOf(const QString& imageId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
