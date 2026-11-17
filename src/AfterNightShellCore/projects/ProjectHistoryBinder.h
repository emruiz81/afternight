// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What gives the undo history of a project's image its place in the project: the views are heard, and the history of
/// the one whose file is the project's image is kept in the project's folder for that image.

#include "AfterNightShellCoreExport.h"

#include "controls/ImageHistoryIndex.h"

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class ImageViewControl;

namespace an::ui::shell
{

class ProjectService;

/// The histories of a project's images.
///
/// A view's history is the session's until the file the view shows is an image of the open project; from then on its
/// steps are written into the project's folder for that image (`ProjectService::historyDirectoryFor`), under the
/// project's compression, and stay there when the view closes (`ImageHistoryIndex.h`). That happens in three ways:
///
/// - **An image opened from a file while a project is open** joins the project, once the view holds its image. If the
///   project already keeps a history for that file it is attached around the image - the step the file equals is the
///   one shown, the earlier steps behind it, the ones left unsaved ahead - and when the file changed since, that
///   history is set aside and the person is told.
/// - **An image saved while a project is open** that is not the project's yet joins it then: a process's result that
///   was never a file is the project's from its first save, and the steps it has go into the project.
/// - **The images open when a project is made** join it when the person says so (`join`), their steps moved into the
///   project or left with the session as the person chose.
///
/// A view that was open before a project was opened is not that project's until it is saved. A sequence is recorded
/// as the project's and keeps its history in the session: its states are its file and the frames changed in it, not
/// files that stand alone. A file of the session's temporary folder is no project's.
///
/// Saving a project's image under another name takes the project's record of it to the new file, as the view takes
/// the new path; an image that already was that file gives way. Closing the project leaves its histories in its
/// folder: each view goes on with a history of the session's, and the steps the project keeps are still what its undo
/// restores.
///
/// It shows nothing itself: what the person is told is handed to `setNotice`, or said through the application's
/// dialog service without blocking. Used on the GUI thread only; nothing here waits for a worker.
class AFTERNIGHTSHELLCORE_API ProjectHistoryBinder final
{
public:
    /// Says something to the person.
    using Notice = std::function<void(const QString& text)>;

    /// Whether a file is one of the session's temporary files.
    using TemporaryTest = std::function<bool(const QString& filePath)>;

    using Progress = an::ui::history::IHistoryPlacement::MoveProgress;

    /// Told on the GUI thread what a compaction removes, or removed.
    using CompactionDone = std::function<void(const an::ui::history::HistoryCompactionResult& result)>;

    /// @param projects the project service, which outlives the binder.
    explicit ProjectHistoryBinder(ProjectService& projects);
    ~ProjectHistoryBinder();

    ProjectHistoryBinder(const ProjectHistoryBinder&) = delete;
    ProjectHistoryBinder& operator=(const ProjectHistoryBinder&) = delete;
    ProjectHistoryBinder(ProjectHistoryBinder&&) = delete;
    ProjectHistoryBinder& operator=(ProjectHistoryBinder&&) = delete;

    /// How the person is told; empty, through the application's dialog service.
    void setNotice(Notice notice);

    /// What says a file is the session's own; empty, no file is.
    void setTemporaryTest(TemporaryTest isTemporary);

    /// A view was made: its saves are heard from now on, and while a project is open the file it shows joins the
    /// project once the view holds its image.
    void watch(ImageViewControl& view);

    /// Makes the file a view shows an image of the open project and keeps the view's history in the project's folder
    /// for it.
    ///
    /// @param view the view.
    /// @param move what becomes of the steps the history already has.
    /// @param progress told, on a worker, how much of those steps has gone into the project; may be empty.
    /// @return the image's id; empty when nothing joined - no project is open, the view shows no file or a temporary
    ///         one, or the project could not be written.
    QString join(ImageViewControl& view, an::ui::history::StepsMove move = an::ui::history::StepsMove::Move,
                 Progress progress = {});

    /// The image of the open project a view is; empty for none.
    [[nodiscard]] QString imageOf(const ImageViewControl& view) const;

    /// The bytes the files of the views' undo steps take: what moving their history into a project moves.
    [[nodiscard]] static std::uint64_t historyBytesOf(const std::vector<ImageViewControl*>& views);

    /// What compacting the history of an image of the open project would remove: asked of the view that has the image
    /// open, or measured in the project's folder on a worker.
    void planCompaction(const QString& imageId, const an::ui::history::HistoryCompaction& what, CompactionDone done);

    /// Compacts the history of an image of the open project. It cannot be undone.
    void compact(const QString& imageId, const an::ui::history::HistoryCompaction& what, CompactionDone done);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
