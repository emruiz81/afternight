// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "projects/ProjectService.h"

#include "controls/IAppContext.h"
#include "controls/ImageHistoryIndex.h"
#include "controls/settings/ProjectSettings.h"
#include "controls/settings/SettingsKeys.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMetaObject>
#include <QSet>
#include <QStorageInfo>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <map>

namespace an::ui::shell
{

namespace
{

constexpr char kContext[] = "an::ui::shell::ProjectService";

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate(kContext, text);
}

/// A path as the service compares and records it: absolute separators forward, no `.` or `..` left in it.
[[nodiscard]] QString cleaned(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

/// Whether two paths name one place, as the host's file systems tell names apart.
[[nodiscard]] bool samePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

[[nodiscard]] QString native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

[[nodiscard]] QString documentPathOf(const QString& folder)
{
    return QDir(folder).filePath(QString::fromLatin1(kProjectFileName));
}

[[nodiscard]] QString lockPathOf(const QString& folder)
{
    return QDir(folder).filePath(QString::fromLatin1(kProjectLockFileName));
}

/// A new id: a UUID, which a folder can be named by.
[[nodiscard]] QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

/// Takes the views of an image out of a document: the ones its workspaces list, and the ones that floated.
void forgetViewsOf(ProjectDocument& document, const QString& imageId)
{
    const auto shows = [&imageId](const ProjectWorkspaceView& view) { return view.image == imageId; };
    for (ProjectWorkspace& workspace : document.workspaces)
    {
        workspace.views.erase(std::remove_if(workspace.views.begin(), workspace.views.end(), shows),
                              workspace.views.end());
    }
    document.floatingViews.erase(std::remove_if(document.floatingViews.begin(), document.floatingViews.end(), shows),
                                 document.floatingViews.end());
}

[[nodiscard]] ProjectOutcome refusedOutcome(const ProjectRefusal refusal, const QString& folder, const QString& why)
{
    ProjectOutcome outcome;
    outcome.refusal = refusal;
    outcome.folder = folder;
    outcome.why = why;
    return outcome;
}

/// The projects used last as the settings hold them: a JSON array of each one's folder and name.
[[nodiscard]] std::vector<RecentProject> recentsFromText(const QString& text)
{
    std::vector<RecentProject> recents;
    const QJsonDocument stored = QJsonDocument::fromJson(text.toUtf8());
    if (!stored.isArray())
    {
        return recents;
    }
    for (const QJsonValue& entry : stored.array())
    {
        const QJsonObject object = entry.toObject();
        const QString folder = cleaned(object.value(QLatin1String("folder")).toString());
        const bool listed = std::any_of(recents.begin(), recents.end(), [&folder](const RecentProject& recent) {
            return samePath(recent.folder, folder);
        });
        if (folder.isEmpty() || listed)
        {
            continue;
        }
        QString name = object.value(QLatin1String("name")).toString();
        if (name.trimmed().isEmpty())
        {
            name = QFileInfo(folder).fileName();
        }
        recents.push_back(RecentProject{folder, name});
        if (recents.size() == ProjectService::kRecentLimit)
        {
            break;
        }
    }
    return recents;
}

[[nodiscard]] QString recentsText(const std::vector<RecentProject>& recents)
{
    QJsonArray stored;
    for (const RecentProject& recent : recents)
    {
        stored.append(QJsonObject{{QStringLiteral("folder"), recent.folder}, {QStringLiteral("name"), recent.name}});
    }
    return QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact));
}

/// The bytes of everything below a directory.
[[nodiscard]] std::uint64_t bytesBelow(const QString& directory)
{
    std::uint64_t bytes = 0;
    QDirIterator entries(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (entries.hasNext())
    {
        entries.next();
        bytes += static_cast<std::uint64_t>(entries.fileInfo().size());
    }
    return bytes;
}

/// What a project's folder takes on disk. For a worker: it reads the folder.
[[nodiscard]] ProjectStorageReport measureStorage(const QString& folder, const std::vector<ProjectImage>& images)
{
    ProjectStorageReport report;
    report.folder = folder;
    if (folder.isEmpty())
    {
        return report;
    }
    const QDir project(folder);
    const QDir histories(project.filePath(QString::fromLatin1(kProjectHistoryFolder)));
    QSet<QString> named;
    for (const ProjectImage& image : images)
    {
        const an::ui::history::StoredHistorySize size =
            an::ui::history::measureStoredHistory(histories.filePath(image.id));
        ProjectImageStorage storage;
        storage.imageId = image.id;
        storage.path = image.path;
        storage.steps = size.steps;
        storage.historyBytes = size.bytes;
        storage.detachedBytes = size.detachedBytes;
        report.historyBytes += size.bytes + size.detachedBytes;
        report.images.push_back(std::move(storage));
        named.insert(image.id);
    }
    const QStringList folders = histories.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& name : folders)
    {
        if (!named.contains(name))
        {
            report.unlistedHistoryBytes += bytesBelow(histories.filePath(name));
        }
    }
    report.integrationBytes = bytesBelow(project.filePath(QString::fromLatin1(kProjectIntegrationFolder)));
    const std::uint64_t whole = bytesBelow(folder);
    const std::uint64_t counted = report.historyBytes + report.unlistedHistoryBytes + report.integrationBytes;
    report.otherBytes = whole > counted ? whole - counted : 0;
    const QStorageInfo drive(folder);
    report.freeBytes = drive.isValid() && drive.isReady() ? drive.bytesAvailable() : -1;
    return report;
}

/// What taking a project's lock came to.
struct LockAttempt
{
    std::unique_ptr<QLockFile> lock;
    std::optional<ProjectRefusal> refusal;
    QString why;
    ProjectLockHolder holder;
};

/// Takes the lock of a project's folder. A lock a process that no longer runs on this machine left is taken over by
/// the lock itself; any other is refused naming who holds it, unless the policy says to take it.
[[nodiscard]] LockAttempt takeLock(const QString& folder, const QString& projectName, const ProjectLockPolicy policy)
{
    LockAttempt attempt;
    auto lock = std::make_unique<QLockFile>(lockPathOf(folder));
    // No stale time: a lock is never taken for being old, only for its process being gone.
    lock->setStaleLockTime(0);
    if (lock->tryLock(0))
    {
        attempt.lock = std::move(lock);
        return attempt;
    }
    if (lock->error() != QLockFile::LockFailedError)
    {
        attempt.refusal = ProjectRefusal::WriteFailed;
        attempt.why = translated("The project '%1' cannot be opened: its folder '%2' cannot be written.")
                          .arg(projectName, native(folder));
        return attempt;
    }
    (void)lock->getLockInfo(&attempt.holder.processId, &attempt.holder.machine, &attempt.holder.application);
    if (policy == ProjectLockPolicy::TakeOver)
    {
        if (lock->removeStaleLockFile() && lock->tryLock(0))
        {
            attempt.holder = {};
            attempt.lock = std::move(lock);
            return attempt;
        }
        attempt.refusal = ProjectRefusal::Locked;
        attempt.why = translated("The project '%1' is in use and its lock could not be taken: the session that "
                                 "holds it is still running.")
                          .arg(projectName);
        return attempt;
    }
    attempt.refusal = ProjectRefusal::Locked;
    const QString machine = attempt.holder.machine.isEmpty() ? translated("another computer") : attempt.holder.machine;
    const QString application =
        attempt.holder.application.isEmpty() ? translated("another program") : attempt.holder.application;
    attempt.why = translated("The project '%1' is open in %2 on %3. AfterNight cannot tell a session that crashed "
                             "there from one that is running: open it anyway only if that session is closed.")
                      .arg(projectName, application, machine);
    return attempt;
}

} // namespace

struct ProjectService::Impl
{
    an::ui::IGlobalSettingsProvider* settings = nullptr;

    // The open project: its folder, its document and its lock; all empty with none open.
    QString folder;
    std::optional<ProjectDocument> document;
    std::unique_ptr<QLockFile> lock;

    // The projects used last and the one used last, as kept when there are no settings to keep them in.
    std::vector<RecentProject> recents;
    QString last;
    bool recentsRead = false;

    std::map<std::size_t, Observer> observers;
    std::size_t nextToken = 1;

    // What is asked before the open project is left, and whether it is being asked: a guard that closes the project
    // itself is not asked again.
    LeaveGuard leaveGuard;
    bool leaving = false;

    /// Whether the open project may be left: there is none, or its guard lets it go.
    [[nodiscard]] bool mayLeave()
    {
        if (!document.has_value() || !leaveGuard || leaving)
        {
            return true;
        }
        leaving = true;
        // The guard may take itself back while it answers.
        const LeaveGuard guard = leaveGuard;
        const bool left = guard();
        leaving = false;
        return left;
    }

    // Says, to a report measured on a worker, whether the service is still there to be told.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    void tell(const Change change) const
    {
        // An observer may leave while it is told: the ones to tell are taken first.
        std::vector<Observer> told;
        told.reserve(observers.size());
        for (const auto& [token, observer] : observers)
        {
            told.push_back(observer);
        }
        for (const Observer& observer : told)
        {
            if (observer)
            {
                observer(change);
            }
        }
    }

    void readRecents()
    {
        if (recentsRead || settings == nullptr)
        {
            return;
        }
        recents = recentsFromText(settings->select(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT)));
        last = cleaned(settings->select(QString::fromLatin1(SettingsKeys::PROJECTS_LAST)));
        recentsRead = true;
    }

    /// Puts a project at the front of the ones used last, and makes it the last.
    void remember(const QString& projectFolder, const QString& name)
    {
        readRecents();
        recents.erase(std::remove_if(recents.begin(), recents.end(),
                                     [&projectFolder](const RecentProject& recent) {
                                         return samePath(recent.folder, projectFolder);
                                     }),
                      recents.end());
        recents.insert(recents.begin(), RecentProject{projectFolder, name});
        if (recents.size() > kRecentLimit)
        {
            recents.resize(kRecentLimit);
        }
        last = projectFolder;
        if (settings != nullptr)
        {
            (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT), recentsText(recents));
            (void)settings->upsert(QString::fromLatin1(SettingsKeys::PROJECTS_LAST), last);
        }
    }

    /// Lets go of the open project: its lock is released and removed.
    bool release()
    {
        if (!document.has_value())
        {
            return false;
        }
        if (lock)
        {
            lock->unlock();
        }
        lock.reset();
        document.reset();
        folder.clear();
        return true;
    }

    /// Makes a project the open one, closing the one that was, and tells of it.
    void adopt(const QString& projectFolder, ProjectDocument&& opened, std::unique_ptr<QLockFile> taken)
    {
        if (release())
        {
            tell(Change::Closed);
        }
        folder = projectFolder;
        document = std::move(opened);
        lock = std::move(taken);
        remember(folder, document->name);
        tell(Change::Opened);
        tell(Change::Recents);
    }

    /// Writes a changed document whole and takes it as the open project's; false leaves the project as it was.
    bool commit(ProjectDocument&& changed)
    {
        const QString problem = writeProjectDocument(documentPathOf(folder), changed);
        if (!problem.isEmpty())
        {
            LOG_WARNING("{}", problem.toStdString());
            return false;
        }
        document = std::move(changed);
        return true;
    }

    /// The index of the image a file is, by its path or by its path from the project's folder; -1 for none.
    [[nodiscard]] qsizetype indexOfFile(const QString& path) const
    {
        if (!document.has_value() || path.isEmpty())
        {
            return -1;
        }
        const QString relative = projectRelativePath(folder, path);
        qsizetype byRelative = -1;
        for (std::size_t index = 0; index < document->images.size(); ++index)
        {
            const ProjectImage& image = document->images[index];
            if (samePath(image.path, path))
            {
                return static_cast<qsizetype>(index);
            }
            if (byRelative < 0 && !relative.isEmpty() && samePath(image.relativePath, relative))
            {
                byRelative = static_cast<qsizetype>(index);
            }
        }
        return byRelative;
    }

    [[nodiscard]] qsizetype indexOfId(const QString& imageId) const
    {
        if (!document.has_value())
        {
            return -1;
        }
        for (std::size_t index = 0; index < document->images.size(); ++index)
        {
            if (document->images[index].id == imageId)
            {
                return static_cast<qsizetype>(index);
            }
        }
        return -1;
    }
};

ProjectService::ProjectService() : m_impl(std::make_unique<Impl>()) {}

ProjectService::~ProjectService()
{
    m_impl->alive.reset();
    m_impl->observers.clear();
    (void)m_impl->release();
}

void ProjectService::setSettings(an::ui::IGlobalSettingsProvider* settings) noexcept
{
    m_impl->settings = settings;
    m_impl->recentsRead = false;
}

QString ProjectService::projectFolder(const QString& location, const QString& name)
{
    const QString place = cleaned(location);
    return place.isEmpty() ? QString() : QDir::cleanPath(QDir(place).filePath(name));
}

ProjectFolderState ProjectService::folderState(const QString& folder)
{
    const QString place = cleaned(folder);
    const QFileInfo info(place);
    if (!info.exists())
    {
        return ProjectFolderState::Free;
    }
    if (!info.isDir())
    {
        return ProjectFolderState::Occupied;
    }
    if (isProjectFolder(place))
    {
        return ProjectFolderState::Project;
    }
    const QDir directory(place);
    return directory.isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
               ? ProjectFolderState::Free
               : ProjectFolderState::Occupied;
}

bool ProjectService::isProjectFolder(const QString& folder)
{
    const QString place = cleaned(folder);
    return !place.isEmpty() && QFileInfo(documentPathOf(place)).isFile();
}

ProjectOutcome ProjectService::create(const ProjectCreateRequest& request)
{
    Impl& impl = *m_impl;
    if (!an::ui::isValidProjectName(request.name))
    {
        return refusedOutcome(ProjectRefusal::InvalidName, QString(),
                              translated("'%1' is not a name a project's folder can take.").arg(request.name));
    }
    const QString location = cleaned(request.location);
    if (location.isEmpty() || !QDir::isAbsolutePath(location) ||
        (QFileInfo::exists(location) && !QFileInfo(location).isDir()))
    {
        return refusedOutcome(ProjectRefusal::InvalidLocation, QString(),
                              translated("'%1' is not a folder a project can be made in.").arg(native(location)));
    }
    const QString folder = projectFolder(location, request.name);
    switch (folderState(folder))
    {
    case ProjectFolderState::Project:
        return refusedOutcome(ProjectRefusal::AlreadyAProject, folder,
                              translated("The folder '%1' is a project already.").arg(native(folder)));
    case ProjectFolderState::Occupied:
        return refusedOutcome(ProjectRefusal::FolderNotEmpty, folder,
                              translated("The folder '%1' holds files and is not a project. Choose another name or "
                                         "another location.")
                                  .arg(native(folder)));
    case ProjectFolderState::Free:
        break;
    }

    // The folder and what it holds beside the document; what was made is removed again if the project is not.
    const bool madeFolder = !QFileInfo::exists(folder);
    const auto undo = [&folder, madeFolder]() {
        QDir directory(folder);
        for (const char* sub :
             {kProjectHistoryFolder, kProjectWorkspacesFolder, kProjectIntegrationFolder, kProjectThumbnailsFolder})
        {
            (void)directory.rmdir(QString::fromLatin1(sub));
        }
        (void)QFile::remove(directory.filePath(QString::fromLatin1(kProjectNotesFileName)));
        (void)QFile::remove(documentPathOf(folder));
        if (madeFolder)
        {
            (void)QDir().rmdir(folder);
        }
    };
    const auto cannotWrite = [&folder]() {
        return refusedOutcome(ProjectRefusal::WriteFailed, folder,
                              translated("The project's folder '%1' could not be made.").arg(native(folder)));
    };
    if (!QDir().mkpath(folder))
    {
        return refusedOutcome(ProjectRefusal::InvalidLocation, folder,
                              translated("The folder '%1' could not be made.").arg(native(folder)));
    }
    QDir directory(folder);
    for (const char* sub :
         {kProjectHistoryFolder, kProjectWorkspacesFolder, kProjectIntegrationFolder, kProjectThumbnailsFolder})
    {
        if (!directory.mkpath(QString::fromLatin1(sub)))
        {
            undo();
            return cannotWrite();
        }
    }
    {
        QFile notes(directory.filePath(QString::fromLatin1(kProjectNotesFileName)));
        if (!notes.open(QIODevice::WriteOnly))
        {
            undo();
            return cannotWrite();
        }
    }

    ProjectDocument document;
    document.id = newId();
    document.name = request.name;
    document.created = QDateTime::currentDateTimeUtc();
    document.lastOpened = document.created;
    document.origin = request.origin;
    document.object = request.object;
    if (document.object.has_value() && document.object->name.trimmed().isEmpty())
    {
        document.object.reset();
    }
    document.historyCompression = request.compression;

    LockAttempt attempt = takeLock(folder, document.name, ProjectLockPolicy::Respect);
    if (!attempt.lock)
    {
        undo();
        ProjectOutcome outcome = refusedOutcome(*attempt.refusal, folder, attempt.why);
        outcome.holder = attempt.holder;
        return outcome;
    }
    if (const QString problem = writeProjectDocument(documentPathOf(folder), document); !problem.isEmpty())
    {
        attempt.lock->unlock();
        attempt.lock.reset();
        undo();
        return refusedOutcome(ProjectRefusal::WriteFailed, folder, problem);
    }
    // The new project is sure to open: the one that is open is left now, or kept, and then nothing was made.
    if (!impl.mayLeave())
    {
        attempt.lock->unlock();
        attempt.lock.reset();
        undo();
        return refusedOutcome(ProjectRefusal::OpenProjectKept, folder,
                              translated("The project that is open was not closed, so no project was made."));
    }

    impl.adopt(folder, std::move(document), std::move(attempt.lock));
    ProjectOutcome outcome;
    outcome.folder = folder;
    return outcome;
}

ProjectOutcome ProjectService::open(const QString& folder, const ProjectLockPolicy policy)
{
    Impl& impl = *m_impl;
    const QString place = cleaned(folder);
    if (impl.document.has_value() && samePath(place, impl.folder))
    {
        ProjectOutcome outcome;
        outcome.folder = impl.folder;
        return outcome;
    }
    if (!isProjectFolder(place))
    {
        return refusedOutcome(ProjectRefusal::NotAProject, place,
                              QFileInfo(place).isDir()
                                  ? translated("The folder '%1' holds no project.").arg(native(place))
                                  : translated("The project's folder '%1' is not there.").arg(native(place)));
    }
    ProjectDocumentRead read = readProjectDocument(documentPathOf(place));
    if (!read.isRead())
    {
        ProjectOutcome outcome = refusedOutcome(ProjectRefusal::DocumentRefused, place, read.why);
        outcome.documentRefusal = read.refusal;
        return outcome;
    }

    LockAttempt attempt = takeLock(place, read.document->name, policy);
    if (!attempt.lock)
    {
        ProjectOutcome outcome = refusedOutcome(*attempt.refusal, place, attempt.why);
        outcome.holder = attempt.holder;
        return outcome;
    }
    // The project says when it was last opened: a project that cannot be written is not opened.
    ProjectDocument document = std::move(*read.document);
    document.lastOpened = QDateTime::currentDateTimeUtc();
    if (const QString problem = writeProjectDocument(documentPathOf(place), document); !problem.isEmpty())
    {
        attempt.lock->unlock();
        return refusedOutcome(ProjectRefusal::WriteFailed, place, problem);
    }
    // The project is sure to open: the one that is open is left now, or kept.
    if (!impl.mayLeave())
    {
        attempt.lock->unlock();
        attempt.lock.reset();
        return refusedOutcome(
            ProjectRefusal::OpenProjectKept, place,
            translated("The project that is open was not closed, so '%1' was not opened.").arg(native(place)));
    }

    impl.adopt(place, std::move(document), std::move(attempt.lock));
    ProjectOutcome outcome;
    outcome.folder = place;
    return outcome;
}

std::optional<ProjectOutcome> ProjectService::openLastIfAsked()
{
    Impl& impl = *m_impl;
    if (impl.settings == nullptr)
    {
        return std::nullopt;
    }
    an::ui::IGlobalSettingsProvider* const settings = impl.settings;
    const an::ui::ProjectSettings stored = an::ui::readProjectSettings(
        [settings](const QString& key, const QString& fallback) { return settings->select(key, fallback); });
    const QString last = lastProject();
    if (!stored.openLastOnStartup || last.isEmpty())
    {
        return std::nullopt;
    }
    return open(last);
}

bool ProjectService::close()
{
    if (!m_impl->mayLeave() || !m_impl->release())
    {
        return false;
    }
    m_impl->tell(Change::Closed);
    return true;
}

void ProjectService::setLeaveGuard(LeaveGuard guard)
{
    m_impl->leaveGuard = std::move(guard);
}

bool ProjectService::isOpen() const noexcept
{
    return m_impl->document.has_value();
}

const ProjectDocument* ProjectService::current() const noexcept
{
    return m_impl->document.has_value() ? &*m_impl->document : nullptr;
}

const QString& ProjectService::currentFolder() const noexcept
{
    return m_impl->folder;
}

std::vector<RecentProject> ProjectService::recent() const
{
    m_impl->readRecents();
    return m_impl->recents;
}

QString ProjectService::lastProject() const
{
    m_impl->readRecents();
    return m_impl->last;
}

const ProjectImage* ProjectService::addImage(const QString& filePath, const ProjectImageKind kind)
{
    Impl& impl = *m_impl;
    const QString path = cleaned(filePath);
    if (!impl.document.has_value() || path.isEmpty())
    {
        return nullptr;
    }
    ProjectDocument changed = *impl.document;
    qsizetype index = impl.indexOfFile(path);
    if (index < 0)
    {
        ProjectImage image;
        image.id = newId();
        image.kind = kind;
        changed.images.push_back(std::move(image));
        index = static_cast<qsizetype>(changed.images.size()) - 1;
    }
    // The record states where the file is now, and where that is from the folder.
    ProjectImage& image = changed.images[static_cast<std::size_t>(index)];
    image.path = path;
    image.relativePath = projectRelativePath(impl.folder, path);
    image.lastOpened = QDateTime::currentDateTimeUtc();
    if (!impl.commit(std::move(changed)))
    {
        return nullptr;
    }
    impl.tell(Change::Images);
    return impl.document.has_value() && static_cast<std::size_t>(index) < impl.document->images.size()
               ? &impl.document->images[static_cast<std::size_t>(index)]
               : nullptr;
}

bool ProjectService::removeImage(const QString& imageId)
{
    Impl& impl = *m_impl;
    const qsizetype index = impl.indexOfId(imageId);
    if (index < 0)
    {
        return false;
    }
    ProjectDocument changed = *impl.document;
    changed.images.erase(changed.images.begin() + index);
    // No workspace lists a view of an image the project no longer holds, and none floats.
    forgetViewsOf(changed, imageId);
    if (!impl.commit(std::move(changed)))
    {
        return false;
    }
    impl.tell(Change::Images);
    return true;
}

const ProjectImage* ProjectService::imageFor(const QString& filePath) const
{
    const qsizetype index = m_impl->indexOfFile(cleaned(filePath));
    return index < 0 ? nullptr : &m_impl->document->images[static_cast<std::size_t>(index)];
}

const ProjectImage* ProjectService::image(const QString& imageId) const
{
    const qsizetype index = m_impl->indexOfId(imageId);
    return index < 0 ? nullptr : &m_impl->document->images[static_cast<std::size_t>(index)];
}

bool ProjectService::setImagePath(const QString& imageId, const QString& filePath)
{
    Impl& impl = *m_impl;
    const QString path = cleaned(filePath);
    qsizetype index = impl.indexOfId(imageId);
    if (index < 0 || path.isEmpty())
    {
        return false;
    }
    ProjectDocument changed = *impl.document;
    // An image that already was that file gives way to the one saved over it.
    const qsizetype other = impl.indexOfFile(path);
    if (other >= 0 && other != index)
    {
        const QString otherId = changed.images[static_cast<std::size_t>(other)].id;
        changed.images.erase(changed.images.begin() + other);
        forgetViewsOf(changed, otherId);
        if (other < index)
        {
            --index;
        }
    }
    ProjectImage& moved = changed.images[static_cast<std::size_t>(index)];
    moved.path = path;
    moved.relativePath = projectRelativePath(impl.folder, path);
    if (!impl.commit(std::move(changed)))
    {
        return false;
    }
    impl.tell(Change::Images);
    return true;
}

QString ProjectService::locateImage(const QString& imageId)
{
    Impl& impl = *m_impl;
    const qsizetype index = impl.indexOfId(imageId);
    if (index < 0)
    {
        return {};
    }
    const ProjectImage& image = impl.document->images[static_cast<std::size_t>(index)];
    if (QFileInfo(image.path).isFile())
    {
        return image.path;
    }
    if (image.relativePath.isEmpty())
    {
        return {};
    }
    const QString moved = QDir::cleanPath(QDir(impl.folder).filePath(image.relativePath));
    if (!QFileInfo(moved).isFile())
    {
        return {};
    }
    // The file moved with the project: the record follows it. The file is found whether or not that is written.
    ProjectDocument changed = *impl.document;
    changed.images[static_cast<std::size_t>(index)].path = moved;
    if (impl.commit(std::move(changed)))
    {
        impl.tell(Change::Images);
    }
    return moved;
}

bool ProjectService::setWorkspaces(std::vector<ProjectWorkspace> workspaces,
                                   std::vector<ProjectWorkspaceView> floatingViews)
{
    Impl& impl = *m_impl;
    if (!impl.document.has_value())
    {
        return false;
    }
    ProjectDocument changed = *impl.document;
    changed.workspaces = std::move(workspaces);
    changed.floatingViews = std::move(floatingViews);
    // What a document does not take is not written, and the record stays as it was.
    if (!impl.commit(std::move(changed)))
    {
        return false;
    }
    impl.tell(Change::Workspaces);
    return true;
}

QString ProjectService::historyDirectoryFor(const QString& imageId) const
{
    if (m_impl->indexOfId(imageId) < 0)
    {
        return {};
    }
    return QDir(m_impl->folder).filePath(QString::fromLatin1(kProjectHistoryFolder) + QLatin1Char('/') + imageId);
}

void ProjectService::storageReport(StorageDone done) const
{
    if (!done)
    {
        return;
    }
    const QString folder = m_impl->folder;
    std::vector<ProjectImage> images =
        m_impl->document.has_value() ? m_impl->document->images : std::vector<ProjectImage>();
    const std::weak_ptr<bool> stillThere = m_impl->alive;
    QThreadPool::globalInstance()->start([folder, images = std::move(images), done = std::move(done), stillThere]() {
        auto report = std::make_shared<ProjectStorageReport>(measureStorage(folder, images));
        QCoreApplication* const application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [report, done, stillThere]() {
                if (!stillThere.expired())
                {
                    done(*report);
                }
            },
            Qt::QueuedConnection);
    });
}

std::size_t ProjectService::subscribe(Observer observer)
{
    const std::size_t token = m_impl->nextToken++;
    m_impl->observers.emplace(token, std::move(observer));
    return token;
}

void ProjectService::unsubscribe(const std::size_t token)
{
    m_impl->observers.erase(token);
}

} // namespace an::ui::shell
