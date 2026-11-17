// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The project the application has open, if any: made, opened, closed, locked while it is open, found again among the
/// projects used last, the images it holds, and what it takes on disk.

#include "AfterNightShellCoreExport.h"

#include "projects/ProjectDocument.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QtGlobal>

namespace an::ui
{
class IGlobalSettingsProvider;
} // namespace an::ui

namespace an::ui::shell
{

/// The name of the file that is held while a project is open.
inline constexpr char kProjectLockFileName[] = ".lock";

/// What a project folder holds beside its document and its lock.
inline constexpr char kProjectHistoryFolder[] = "history";
inline constexpr char kProjectWorkspacesFolder[] = "workspaces";
inline constexpr char kProjectIntegrationFolder[] = "integration";
inline constexpr char kProjectThumbnailsFolder[] = "thumbnails";
inline constexpr char kProjectNotesFileName[] = "notes.md";

/// A project used lately: its folder, and the name it was last opened under.
struct RecentProject
{
    QString folder;
    QString name;

    [[nodiscard]] bool operator==(const RecentProject&) const = default;
};

/// Who holds a project's lock.
struct ProjectLockHolder
{
    QString machine;
    QString application;
    qint64 processId = 0;

    [[nodiscard]] bool operator==(const ProjectLockHolder&) const = default;
};

/// Why a project was not made or not opened.
enum class ProjectRefusal : std::uint8_t
{
    /// The name is not one a folder can take.
    InvalidName,

    /// The location is empty, is no folder's path, or the folder cannot be made there.
    InvalidLocation,

    /// The folder holds files and is no project.
    FolderNotEmpty,

    /// A project is asked to be made where one is already: it is offered to be opened.
    AlreadyAProject,

    /// The folder to open holds no project's document.
    NotAProject,

    /// The project's document was refused; the outcome says how.
    DocumentRefused,

    /// The project is open elsewhere; the outcome names who holds it, and `ProjectLockPolicy::TakeOver` opens it
    /// anyway.
    Locked,

    /// The folder, the lock or the document could not be written.
    WriteFailed,

    /// The project that is open was not left - what leaves it kept it open - so no other takes its place.
    OpenProjectKept
};

/// What making or opening a project came to.
struct ProjectOutcome
{
    /// Why it was refused; nothing when the project is open.
    std::optional<ProjectRefusal> refusal;

    /// How the document was refused, when it was.
    std::optional<ProjectDocumentRefusal> documentRefusal;

    /// What a person is told of a refusal; empty when the project is open.
    QString why;

    /// The project's folder: the one made or opened, or the one refused.
    QString folder;

    /// Who holds the lock, when the project is locked.
    ProjectLockHolder holder;

    [[nodiscard]] bool ok() const noexcept
    {
        return !refusal.has_value();
    }
};

/// What a lock held elsewhere does to an open.
enum class ProjectLockPolicy : std::uint8_t
{
    /// The open is refused, naming who holds the lock.
    Respect,

    /// The lock is removed and taken: the person said the other session is gone.
    TakeOver
};

/// What a new project starts as.
struct ProjectCreateRequest
{
    /// The project's name, which is its folder's.
    QString name;

    /// The folder the project's folder is made in.
    QString location;

    std::optional<ProjectObject> object;
    an::ui::history::StepCompression compression = an::ui::history::StepCompression::Lz4;
    ProjectOrigin origin = ProjectOrigin::User;
};

/// What the undo history of one image of a project takes on disk.
struct ProjectImageStorage
{
    QString imageId;

    /// The image's file, as the project records it.
    QString path;

    /// The steps its history keeps.
    std::size_t steps = 0;

    /// The bytes of its steps and their index.
    std::uint64_t historyBytes = 0;

    /// The bytes of the histories set aside because its file changed outside the application.
    std::uint64_t detachedBytes = 0;
};

/// What a project's folder takes on disk.
struct ProjectStorageReport
{
    /// The project's folder; empty when no project was open.
    QString folder;

    /// Each image's history, in the project's order.
    std::vector<ProjectImageStorage> images;

    /// The undo history of every image the project holds, the histories set aside included.
    std::uint64_t historyBytes = 0;

    /// The history folders no image of the project names: an image's record that was removed, or gave way.
    std::uint64_t unlistedHistoryBytes = 0;

    /// The integration data.
    std::uint64_t integrationBytes = 0;

    /// Everything else: the document, the layouts, the thumbnails and the notes.
    std::uint64_t otherBytes = 0;

    /// The bytes free on the folder's drive; negative when it could not be told.
    qint64 freeBytes = -1;
};

/// What a folder is to a project that would be made in it.
enum class ProjectFolderState : std::uint8_t
{
    /// Not there yet, or there and empty: a project can be made in it.
    Free,

    /// A project already.
    Project,

    /// It holds files and is no project, or is a file itself.
    Occupied
};

/// The project the application has open, and the ones it had.
///
/// **A project is a folder.** `create` makes it under a location - `project.anproj`, the folders `history`,
/// `workspaces`, `integration` and `thumbnails`, and `notes.md` - and opens it; a folder that holds files and is no
/// project is refused, and one that is a project already is answered as such, for the caller to offer opening it.
/// `open` reads a folder's document and opens it. One project is open at a time: making or opening another closes the
/// one that was, once the new one is open - a refusal leaves the open project as it was.
///
/// **Leaving a project.** What a shell does as the open project is left - keep its arrangement, close its images - is
/// asked through the leave guard: before `close()` closes the project, and before another takes its place, once that
/// one is read, locked and sure to open. A guard that answers false keeps the open project: `close()` answers false,
/// and the other project is refused as `ProjectRefusal::OpenProjectKept`, its lock released. The service's end asks
/// nothing.
///
/// **The lock.** While a project is open its `.lock` is held (`QLockFile`, with no stale time). The application runs
/// once on a machine, so a lock is met in two cases only. One left by a process that no longer runs on this machine -
/// a crash - is taken over without a question. Any other - a folder shared with another machine or another user - is
/// refused naming the machine and the application that hold it, and `ProjectLockPolicy::TakeOver` removes it and takes
/// it, because a crashed session on another machine cannot be told from a live one.
///
/// **Images.** A project holds the images opened in it, referenced where they are: `addImage` records a file, once,
/// with its path and its path from the project's folder, and stamps when it was opened; an image whose file moved with
/// the project is found again through that relative path, and its record follows it. `removeImage` forgets one, and
/// the views of it its workspaces listed.
///
/// **Workspaces.** A project records the workspaces it was left with - their names, their locks, the images each
/// showed and which was used last - and the images that floated (`setWorkspaces`): what a shell that keeps a project's
/// arrangement writes as the project is left and reads as it is opened.
///
/// **Storage.** What the open project takes on disk is measured on the thread pool (`storageReport`) and told on the
/// GUI thread: each image's undo history, the integration data, the rest and what is free on the drive.
///
/// **The projects used last** are kept in the settings (`projects/recent`, the last ten, most recent first, each with
/// the name it was opened under; `projects/last`, the one used last), and the project used last is opened at the start
/// when the settings ask for it (`projects/openLastOnStartup`).
///
/// It owns no widget and shows no dialog: a refusal is an answer, which the shell says. The document is written whole
/// at each change, before the change is told; a write that fails leaves the project as it was. Used on the GUI thread
/// only.
class AFTERNIGHTSHELLCORE_API ProjectService final
{
public:
    /// How many projects used last are kept.
    static constexpr std::size_t kRecentLimit = 10;

    /// What observers are told.
    enum class Change : std::uint8_t
    {
        /// A project was opened; it is `current()`.
        Opened,

        /// The open project was closed.
        Closed,

        /// The open project's images changed.
        Images,

        /// The projects used last changed.
        Recents,

        /// The open project's record of its workspaces changed.
        Workspaces
    };

    using Observer = std::function<void(Change change)>;

    ProjectService();
    /// Closes the open project: its lock is released.
    ~ProjectService();

    ProjectService(const ProjectService&) = delete;
    ProjectService& operator=(const ProjectService&) = delete;
    ProjectService(ProjectService&&) = delete;
    ProjectService& operator=(ProjectService&&) = delete;

    /// Where the projects used last are kept and the start-up choice is read; null keeps them for the run only. The
    /// settings are the caller's and outlive the service, or are taken back first.
    void setSettings(an::ui::IGlobalSettingsProvider* settings) noexcept;

    // ---- Folders

    /// The folder of a project named `name` under `location`.
    [[nodiscard]] static QString projectFolder(const QString& location, const QString& name);

    /// What a folder is to a project that would be made in it.
    [[nodiscard]] static ProjectFolderState folderState(const QString& folder);

    /// Whether a folder holds a project's document.
    [[nodiscard]] static bool isProjectFolder(const QString& folder);

    // ---- Making, opening, closing

    /// Makes a project's folder under the request's location, and opens it.
    [[nodiscard]] ProjectOutcome create(const ProjectCreateRequest& request);

    /// Opens the project of a folder; the one already open is answered as open.
    [[nodiscard]] ProjectOutcome open(const QString& folder, ProjectLockPolicy policy = ProjectLockPolicy::Respect);

    /// Opens the project used last when the settings ask for it at the start and name one; nothing otherwise.
    [[nodiscard]] std::optional<ProjectOutcome> openLastIfAsked();

    /// Closes the open project and releases its lock; false with none open, or when the leave guard kept it open.
    bool close();

    /// Asked before the open project is left; false keeps it open.
    using LeaveGuard = std::function<bool()>;

    /// What is asked before the open project is left - closed, or given way to another; empty asks nothing. The guard
    /// is the caller's and outlives the service, or is taken back first.
    void setLeaveGuard(LeaveGuard guard);

    [[nodiscard]] bool isOpen() const noexcept;

    /// The open project's document; null with none open.
    [[nodiscard]] const ProjectDocument* current() const noexcept;

    /// The open project's folder; empty with none open.
    [[nodiscard]] const QString& currentFolder() const noexcept;

    // ---- The projects used last

    /// The projects used last, most recent first.
    [[nodiscard]] std::vector<RecentProject> recent() const;

    /// The folder of the project used last; empty for none.
    [[nodiscard]] QString lastProject() const;

    // ---- Images

    /// Records a file as an image of the open project - once: a file it holds already, by its path or by its path from
    /// the project's folder, is that image, whose record then states where the file is now - and stamps it as opened
    /// now.
    ///
    /// @return the image; null with no project open, for an empty path, or when the document could not be written.
    const ProjectImage* addImage(const QString& filePath, ProjectImageKind kind);

    /// Forgets an image of the open project, and the views of it its workspaces listed; the file is not touched.
    ///
    /// @return false with no project open, for an id it does not hold, or when the document could not be written.
    bool removeImage(const QString& imageId);

    /// The image of the open project a file is, by its path or by its path from the project's folder; null for none.
    [[nodiscard]] const ProjectImage* imageFor(const QString& filePath) const;

    /// The image of the open project an id names; null for none.
    [[nodiscard]] const ProjectImage* image(const QString& imageId) const;

    /// Takes an image's record to another file: the image was saved under another name, and its history stays with
    /// it. An image of the project that already was that file gives way - its record goes, with the views of it the
    /// workspaces listed; its history's folder is not touched.
    ///
    /// @return false with no project open, for an id it does not hold, for an empty path, or when the document could
    ///         not be written.
    bool setImagePath(const QString& imageId, const QString& filePath);

    /// Where an image's file is now: its recorded path while a file is there; else its path from the project's folder,
    /// when a file is there - the record then follows it; empty when neither holds a file.
    [[nodiscard]] QString locateImage(const QString& imageId);

    /// The folder the steps of an image's history are kept in: `history/<image id>` in the project's folder. Empty with
    /// no project open or for an id that is none.
    [[nodiscard]] QString historyDirectoryFor(const QString& imageId) const;

    // ---- Workspaces

    /// Records the open project's workspaces - each with its name, its lock, the images it showed and whether it is the
    /// one used last - and the images that floated in windows of their own, in place of the ones it recorded. The
    /// document is written whole.
    ///
    /// @return false with no project open, when what is stated is not what a document takes - two workspaces of one
    ///         id, a view of an image the project does not hold - or when the document could not be written; the
    ///         record then stays as it was.
    bool setWorkspaces(std::vector<ProjectWorkspace> workspaces, std::vector<ProjectWorkspaceView> floatingViews);

    // ---- Storage

    /// Told on the GUI thread what the project takes on disk.
    using StorageDone = std::function<void(const ProjectStorageReport& report)>;

    /// Measures what the open project takes on disk, on the thread pool, and tells `done` on the GUI thread; with no
    /// project open the report names no folder. Never waits. A report still on its way when the service goes is
    /// dropped.
    void storageReport(StorageDone done) const;

    // ---- Observers

    /// Tells `observer` of every change from now on; the token `unsubscribe` takes.
    std::size_t subscribe(Observer observer);
    void unsubscribe(std::size_t token);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
