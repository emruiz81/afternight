// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The project service, with no widget: a project made under a temporary root has the folder's layout; a folder that
// holds files and is no project is refused, and one that is a project is answered as one to open; the lock is held
// while the project is open and released when it closes; the lock a process that no longer runs on this machine left
// is taken over, and one written on another machine is refused naming its holder until the person says to open
// anyway; the projects used last are kept; images are added, found again where they moved to, and removed; and a
// project closes.

#include "projects/ProjectService.h"

#include "ShellCoreTestSupport.h"

#include "controls/settings/ProjectSettings.h"
#include "controls/settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QSysInfo>
#include <QTemporaryDir>

#include <memory>
#include <vector>

namespace
{

using an::tests::shellcore::MemorySettings;
using an::ui::history::StepCompression;
using an::ui::shell::ProjectCreateRequest;
using an::ui::shell::ProjectDocument;
using an::ui::shell::ProjectFolderState;
using an::ui::shell::ProjectImage;
using an::ui::shell::ProjectImageKind;
using an::ui::shell::ProjectLockPolicy;
using an::ui::shell::ProjectObject;
using an::ui::shell::ProjectObjectSource;
using an::ui::shell::ProjectOutcome;
using an::ui::shell::ProjectRefusal;
using an::ui::shell::ProjectService;
using an::ui::shell::ProjectWorkspace;
using an::ui::shell::ProjectWorkspaceView;
using an::ui::shell::RecentProject;
using Change = an::ui::shell::ProjectService::Change;

/// A service over a root of the case's own and settings held in memory.
class ProjectServiceCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_service = std::make_unique<ProjectService>();
        m_service->setSettings(&m_settings);
        m_token = m_service->subscribe([this](const Change change) { m_changes.push_back(change); });
    }

    void TearDown() override
    {
        m_service.reset();
    }

    [[nodiscard]] QString root() const
    {
        return QDir::cleanPath(m_root.path());
    }

    [[nodiscard]] QString under(const QString& relative) const
    {
        return QDir(root()).filePath(relative);
    }

    [[nodiscard]] static ProjectCreateRequest requestFor(const QString& name, const QString& location)
    {
        ProjectCreateRequest request;
        request.name = name;
        request.location = location;
        return request;
    }

    /// Makes a project named `name` under the root, which must succeed.
    QString make(const QString& name)
    {
        const ProjectOutcome made = m_service->create(requestFor(name, root()));
        EXPECT_TRUE(made.ok()) << made.why.toStdString();
        return made.folder;
    }

    /// Writes a file of a few bytes, making its folder.
    static void touch(const QString& path)
    {
        ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("image");
    }

    /// Writes a lock as `QLockFile` writes one, for a process of a machine.
    static void writeLock(const QString& folder, const qint64 processId, const QString& application,
                          const QString& machine, const QByteArray& machineId, const QByteArray& bootId)
    {
        QFile lock(QDir(folder).filePath(QString::fromLatin1(an::ui::shell::kProjectLockFileName)));
        ASSERT_TRUE(lock.open(QIODevice::WriteOnly | QIODevice::Truncate));
        lock.write(QByteArray::number(processId) + '\n' + application.toUtf8() + '\n' + machine.toUtf8() + '\n' +
                   machineId + '\n' + bootId + '\n');
    }

    /// This machine's name as a lock states it: the name the lock's own library reads.
    [[nodiscard]] static QString thisMachine()
    {
#ifdef _WIN32
        return qEnvironmentVariable("COMPUTERNAME");
#else
        return QSysInfo::machineHostName();
#endif
    }

    /// The id of a process that ran on this machine and runs no more.
    [[nodiscard]] static qint64 exitedProcessId()
    {
        QProcess process;
#ifdef _WIN32
        process.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QStringLiteral("exit 0")});
#else
        process.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("exit 0")});
#endif
        if (!process.waitForStarted())
        {
            return 0;
        }
        const qint64 id = process.processId();
        process.waitForFinished();
        return id;
    }

    QTemporaryDir m_root;
    MemorySettings m_settings;
    std::unique_ptr<ProjectService> m_service;
    std::vector<Change> m_changes;
    std::size_t m_token = 0;
};

} // namespace

// A project made under a root is a folder named by it that holds the document, the lock while it is open, the folders
// of its history, workspaces, integrations and thumbnails, and its notes.
TEST_F(ProjectServiceCase, CreateMakesTheFoldersLayoutUnderTheRoot)
{
    ProjectCreateRequest request = requestFor(QStringLiteral("Carina Nebula LRGB"), root());
    request.object = ProjectObject{QStringLiteral("NGC 3372"), ProjectObjectSource::User};
    request.compression = StepCompression::None;

    const ProjectOutcome made = m_service->create(request);
    ASSERT_TRUE(made.ok()) << made.why.toStdString();
    EXPECT_TRUE(made.why.isEmpty());
    EXPECT_EQ(made.folder, under(QStringLiteral("Carina Nebula LRGB")));
    EXPECT_EQ(ProjectService::projectFolder(root(), QStringLiteral("Carina Nebula LRGB")), made.folder);

    const QDir folder(made.folder);
    QStringList entries = folder.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    entries.sort();
    EXPECT_EQ(entries, (QStringList{QStringLiteral(".lock"), QStringLiteral("history"), QStringLiteral("integration"),
                                    QStringLiteral("notes.md"), QStringLiteral("project.anproj"),
                                    QStringLiteral("thumbnails"), QStringLiteral("workspaces")}));
    for (const char* sub : {"history", "integration", "thumbnails", "workspaces"})
    {
        EXPECT_TRUE(QFileInfo(folder.filePath(QString::fromLatin1(sub))).isDir()) << sub;
    }
    EXPECT_TRUE(QFileInfo(folder.filePath(QStringLiteral("notes.md"))).isFile());

    // The project is open, and its document says what was asked.
    ASSERT_TRUE(m_service->isOpen());
    ASSERT_NE(m_service->current(), nullptr);
    EXPECT_EQ(m_service->currentFolder(), made.folder);
    const ProjectDocument& document = *m_service->current();
    EXPECT_EQ(document.name, QStringLiteral("Carina Nebula LRGB"));
    EXPECT_TRUE(an::ui::shell::isProjectId(document.id));
    EXPECT_EQ(document.origin, an::ui::shell::ProjectOrigin::User);
    ASSERT_TRUE(document.object.has_value());
    EXPECT_EQ(document.object->name, QStringLiteral("NGC 3372"));
    EXPECT_EQ(document.historyCompression, StepCompression::None);
    EXPECT_TRUE(document.images.empty());
    EXPECT_TRUE(document.created.isValid());
    EXPECT_EQ(document.lastOpened, document.created);

    // What is on disk is what the service holds.
    const an::ui::shell::ProjectDocumentRead stored =
        an::ui::shell::readProjectDocument(folder.filePath(QStringLiteral("project.anproj")));
    ASSERT_TRUE(stored.isRead()) << stored.why.toStdString();
    EXPECT_EQ(*stored.document, document);
    EXPECT_EQ(ProjectService::folderState(made.folder), ProjectFolderState::Project);
    EXPECT_TRUE(ProjectService::isProjectFolder(made.folder));
    EXPECT_EQ(m_changes, (std::vector<Change>{Change::Opened, Change::Recents}));
}

// A name no folder takes, and a location that is none, are refused and nothing is made.
TEST_F(ProjectServiceCase, ANameOrALocationThatIsNoneIsRefused)
{
    for (const QString& name : {QString(), QStringLiteral("  "), QStringLiteral("a/b"), QStringLiteral("a:b"),
                                QStringLiteral("NUL"), QStringLiteral("trailing."), QStringLiteral(" lead")})
    {
        const ProjectOutcome refused = m_service->create(requestFor(name, root()));
        ASSERT_TRUE(refused.refusal.has_value()) << name.toStdString();
        EXPECT_EQ(*refused.refusal, ProjectRefusal::InvalidName) << name.toStdString();
        EXPECT_FALSE(refused.why.isEmpty());
    }
    for (const QString& location : {QString(), QStringLiteral("relative/place")})
    {
        const ProjectOutcome refused = m_service->create(requestFor(QStringLiteral("M 31"), location));
        ASSERT_TRUE(refused.refusal.has_value()) << location.toStdString();
        EXPECT_EQ(*refused.refusal, ProjectRefusal::InvalidLocation) << location.toStdString();
    }
    // A location that is a file cannot hold a folder.
    touch(under(QStringLiteral("a-file")));
    const ProjectOutcome inFile =
        m_service->create(requestFor(QStringLiteral("M 31"), under(QStringLiteral("a-file"))));
    ASSERT_TRUE(inFile.refusal.has_value());
    EXPECT_EQ(*inFile.refusal, ProjectRefusal::InvalidLocation);

    EXPECT_FALSE(m_service->isOpen());
    EXPECT_TRUE(m_changes.empty());
    EXPECT_EQ(QDir(root()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList{QStringLiteral("a-file")});
}

// A folder that holds files and is no project is refused, untouched; an empty one is taken.
TEST_F(ProjectServiceCase, ANonEmptyFolderThatIsNoProjectIsRefused)
{
    touch(under(QStringLiteral("Lights/L_001.fits")));
    EXPECT_EQ(ProjectService::folderState(under(QStringLiteral("Lights"))), ProjectFolderState::Occupied);

    const ProjectOutcome refused = m_service->create(requestFor(QStringLiteral("Lights"), root()));
    ASSERT_TRUE(refused.refusal.has_value());
    EXPECT_EQ(*refused.refusal, ProjectRefusal::FolderNotEmpty);
    EXPECT_EQ(refused.folder, under(QStringLiteral("Lights")));
    EXPECT_FALSE(refused.why.isEmpty());
    EXPECT_FALSE(m_service->isOpen());
    EXPECT_EQ(QDir(under(QStringLiteral("Lights"))).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
              QStringList{QStringLiteral("L_001.fits")});

    // A file where the folder would be is refused the same way.
    touch(under(QStringLiteral("Taken")));
    EXPECT_EQ(ProjectService::folderState(under(QStringLiteral("Taken"))), ProjectFolderState::Occupied);
    const ProjectOutcome overFile = m_service->create(requestFor(QStringLiteral("Taken"), root()));
    ASSERT_TRUE(overFile.refusal.has_value());
    EXPECT_EQ(*overFile.refusal, ProjectRefusal::FolderNotEmpty);

    // A folder that is there and empty, and one that is not there, are free.
    ASSERT_TRUE(QDir(root()).mkpath(QStringLiteral("Empty")));
    EXPECT_EQ(ProjectService::folderState(under(QStringLiteral("Empty"))), ProjectFolderState::Free);
    EXPECT_EQ(ProjectService::folderState(under(QStringLiteral("Absent"))), ProjectFolderState::Free);
    const ProjectOutcome made = m_service->create(requestFor(QStringLiteral("Empty"), root()));
    EXPECT_TRUE(made.ok()) << made.why.toStdString();
}

// Making a project where one is answers that it is one, with its folder, for the caller to offer opening it; the
// project there is not touched, and opening it then works.
TEST_F(ProjectServiceCase, AnExistingProjectIsOffered)
{
    const QString folder = make(QStringLiteral("Orion"));
    ASSERT_NE(m_service->addImage(under(QStringLiteral("m42.fits")), ProjectImageKind::Image), nullptr);
    const ProjectDocument before = *m_service->current();
    ASSERT_TRUE(m_service->close());
    m_changes.clear();

    const ProjectOutcome again = m_service->create(requestFor(QStringLiteral("Orion"), root()));
    ASSERT_TRUE(again.refusal.has_value());
    EXPECT_EQ(*again.refusal, ProjectRefusal::AlreadyAProject);
    EXPECT_EQ(again.folder, folder);
    EXPECT_FALSE(m_service->isOpen());
    EXPECT_TRUE(m_changes.empty());

    const ProjectOutcome opened = m_service->open(again.folder);
    ASSERT_TRUE(opened.ok()) << opened.why.toStdString();
    ASSERT_NE(m_service->current(), nullptr);
    EXPECT_EQ(m_service->current()->id, before.id);
    EXPECT_EQ(m_service->current()->images, before.images);
    EXPECT_GE(m_service->current()->lastOpened, before.lastOpened);
}

// A folder with no document is no project; a document that is refused is refused as the document says.
TEST_F(ProjectServiceCase, OpenRefusesWhatIsNoProject)
{
    const ProjectOutcome absent = m_service->open(under(QStringLiteral("Nowhere")));
    ASSERT_TRUE(absent.refusal.has_value());
    EXPECT_EQ(*absent.refusal, ProjectRefusal::NotAProject);
    EXPECT_FALSE(absent.why.isEmpty());

    touch(under(QStringLiteral("Lights/L_001.fits")));
    const ProjectOutcome plain = m_service->open(under(QStringLiteral("Lights")));
    ASSERT_TRUE(plain.refusal.has_value());
    EXPECT_EQ(*plain.refusal, ProjectRefusal::NotAProject);

    // A newer project, and a document cut short: refused, with the document's reason, and no lock is left behind.
    const QString folder = make(QStringLiteral("Newer"));
    ASSERT_TRUE(m_service->close());
    const QString documentPath = QDir(folder).filePath(QStringLiteral("project.anproj"));
    QFile file(documentPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonObject stored = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    stored.insert(QStringLiteral("version"), 3);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray newer = QJsonDocument(stored).toJson();
    file.write(newer);
    file.close();

    const ProjectOutcome refused = m_service->open(folder);
    ASSERT_TRUE(refused.refusal.has_value());
    EXPECT_EQ(*refused.refusal, ProjectRefusal::DocumentRefused);
    ASSERT_TRUE(refused.documentRefusal.has_value());
    EXPECT_EQ(*refused.documentRefusal, an::ui::shell::ProjectDocumentRefusal::NewerVersion);
    EXPECT_TRUE(refused.why.contains(QStringLiteral("newer"))) << refused.why.toStdString();
    EXPECT_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral(".lock"))));

    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(newer.left(newer.size() / 2));
    file.close();
    const ProjectOutcome cut = m_service->open(folder);
    ASSERT_TRUE(cut.refusal.has_value());
    EXPECT_EQ(*cut.refusal, ProjectRefusal::DocumentRefused);
    EXPECT_EQ(*cut.documentRefusal, an::ui::shell::ProjectDocumentRefusal::NotJson);
    EXPECT_FALSE(m_service->isOpen());
}

// While a project is open its lock is held: nothing else takes it. Closing releases it, and it is gone.
TEST_F(ProjectServiceCase, TheLockIsHeldAndReleased)
{
    const QString folder = make(QStringLiteral("Held"));
    const QString lockPath = QDir(folder).filePath(QStringLiteral(".lock"));
    ASSERT_TRUE(QFileInfo::exists(lockPath));
    {
        QLockFile other(lockPath);
        other.setStaleLockTime(0);
        EXPECT_FALSE(other.tryLock(0)) << "the open project's lock was taken by another";
        qint64 processId = 0;
        QString machine;
        QString application;
        ASSERT_TRUE(other.getLockInfo(&processId, &machine, &application));
        EXPECT_EQ(processId, QCoreApplication::applicationPid());
        EXPECT_EQ(machine, thisMachine());
    }

    // A second service - another session on this machine, which the application never is - is refused naming it.
    ProjectService second;
    const ProjectOutcome refused = second.open(folder);
    ASSERT_TRUE(refused.refusal.has_value());
    EXPECT_EQ(*refused.refusal, ProjectRefusal::Locked);
    EXPECT_EQ(refused.holder.processId, QCoreApplication::applicationPid());
    EXPECT_EQ(refused.holder.machine, thisMachine());
    EXPECT_FALSE(second.isOpen());

    ASSERT_TRUE(m_service->close());
    EXPECT_FALSE(QFileInfo::exists(lockPath)) << "a closed project leaves no lock";
    {
        QLockFile other(lockPath);
        other.setStaleLockTime(0);
        EXPECT_TRUE(other.tryLock(0));
    }
    // The service that goes with a project open releases it too.
    const ProjectOutcome reopened = second.open(folder);
    ASSERT_TRUE(reopened.ok()) << reopened.why.toStdString();
    ASSERT_TRUE(second.close());
}

// The lock of a process that no longer runs on this machine - a crash - is taken over without a question.
TEST_F(ProjectServiceCase, TheLockOfAProcessThatNoLongerRunsHereIsTakenOver)
{
    const QString folder = make(QStringLiteral("Crashed"));
    ASSERT_TRUE(m_service->close());
    const qint64 gone = exitedProcessId();
    ASSERT_GT(gone, 0);
    writeLock(folder, gone, QStringLiteral("AfterNight"), thisMachine(), QSysInfo::machineUniqueId(),
              QSysInfo::bootUniqueId());

    const ProjectOutcome opened = m_service->open(folder);
    ASSERT_TRUE(opened.ok()) << opened.why.toStdString();
    EXPECT_TRUE(m_service->isOpen());

    // The lock is this process's now.
    QLockFile lock(QDir(folder).filePath(QStringLiteral(".lock")));
    qint64 processId = 0;
    QString machine;
    QString application;
    ASSERT_TRUE(lock.getLockInfo(&processId, &machine, &application));
    EXPECT_EQ(processId, QCoreApplication::applicationPid());
}

// A lock written on another machine is refused, and the reason names the machine and the application that hold it;
// the project stays closed and the lock untouched. Opening anyway takes it.
TEST_F(ProjectServiceCase, ALockWrittenOnAnotherMachineIsRefusedNamingItsHolderAndOpenAnywayTakesIt)
{
    const QString folder = make(QStringLiteral("Shared"));
    ASSERT_TRUE(m_service->close());
    m_changes.clear();
    writeLock(folder, 4242, QStringLiteral("AfterNight"), QStringLiteral("OBSERVATORY-PC"),
              QByteArrayLiteral("another-machine-id"), QByteArrayLiteral("another-boot-id"));
    const QString lockPath = QDir(folder).filePath(QStringLiteral(".lock"));

    const ProjectOutcome refused = m_service->open(folder);
    ASSERT_TRUE(refused.refusal.has_value());
    EXPECT_EQ(*refused.refusal, ProjectRefusal::Locked);
    EXPECT_EQ(refused.folder, folder);
    EXPECT_EQ(refused.holder.machine, QStringLiteral("OBSERVATORY-PC"));
    EXPECT_EQ(refused.holder.application, QStringLiteral("AfterNight"));
    EXPECT_EQ(refused.holder.processId, 4242);
    EXPECT_TRUE(refused.why.contains(QStringLiteral("OBSERVATORY-PC"))) << refused.why.toStdString();
    EXPECT_TRUE(refused.why.contains(QStringLiteral("AfterNight"))) << refused.why.toStdString();
    EXPECT_FALSE(m_service->isOpen());
    EXPECT_TRUE(m_changes.empty());
    EXPECT_TRUE(QFileInfo::exists(lockPath)) << "a refused open leaves the other session's lock";

    const ProjectOutcome taken = m_service->open(folder, ProjectLockPolicy::TakeOver);
    ASSERT_TRUE(taken.ok()) << taken.why.toStdString();
    EXPECT_TRUE(m_service->isOpen());
    QLockFile lock(lockPath);
    qint64 processId = 0;
    QString machine;
    QString application;
    ASSERT_TRUE(lock.getLockInfo(&processId, &machine, &application));
    EXPECT_EQ(processId, QCoreApplication::applicationPid());
    EXPECT_EQ(machine, thisMachine());
}

// A refusal leaves the project that was open as it was; a project that opens closes the one that was.
TEST_F(ProjectServiceCase, OneProjectIsOpenAtATimeAndARefusalKeepsTheOpenOne)
{
    const QString first = make(QStringLiteral("First"));
    const QString second = make(QStringLiteral("Second"));
    EXPECT_EQ(m_service->currentFolder(), second);
    EXPECT_FALSE(QFileInfo::exists(QDir(first).filePath(QStringLiteral(".lock")))) << "the first project was closed";
    EXPECT_TRUE(QFileInfo::exists(QDir(second).filePath(QStringLiteral(".lock"))));

    m_changes.clear();
    const ProjectOutcome refused = m_service->open(under(QStringLiteral("Nowhere")));
    EXPECT_FALSE(refused.ok());
    EXPECT_EQ(m_service->currentFolder(), second);
    EXPECT_TRUE(m_changes.empty());

    // Opening the project that is open is no change.
    const ProjectOutcome same = m_service->open(second);
    EXPECT_TRUE(same.ok());
    EXPECT_TRUE(m_changes.empty());

    const ProjectOutcome back = m_service->open(first);
    ASSERT_TRUE(back.ok()) << back.why.toStdString();
    EXPECT_EQ(m_service->currentFolder(), first);
    EXPECT_EQ(m_changes, (std::vector<Change>{Change::Closed, Change::Opened, Change::Recents}));
    EXPECT_FALSE(QFileInfo::exists(QDir(second).filePath(QStringLiteral(".lock"))));
}

// The projects used last: most recent first, each once, ten at most, with the name each was opened under; kept in
// the settings, where another service reads them; and the one used last is named.
TEST_F(ProjectServiceCase, RecentsAreKeptMostRecentFirstInTheSettings)
{
    EXPECT_TRUE(m_service->recent().empty());
    EXPECT_TRUE(m_service->lastProject().isEmpty());

    const QString first = make(QStringLiteral("First"));
    const QString second = make(QStringLiteral("Second"));
    EXPECT_EQ(m_service->recent(),
              (std::vector<RecentProject>{{second, QStringLiteral("Second")}, {first, QStringLiteral("First")}}));
    EXPECT_EQ(m_service->lastProject(), second);

    // Opening one again moves it to the front, once.
    ASSERT_TRUE(m_service->open(first).ok());
    EXPECT_EQ(m_service->recent(),
              (std::vector<RecentProject>{{first, QStringLiteral("First")}, {second, QStringLiteral("Second")}}));
    EXPECT_EQ(m_service->lastProject(), first);

    // Closing keeps both: the project used last is still the last.
    ASSERT_TRUE(m_service->close());
    EXPECT_EQ(m_service->recent().size(), 2u);
    EXPECT_EQ(m_service->lastProject(), first);

    // The settings hold them under the keys the application states, and a new service reads them.
    EXPECT_EQ(m_settings.select(QString::fromLatin1(SettingsKeys::PROJECTS_LAST)), first);
    const QJsonArray stored =
        QJsonDocument::fromJson(m_settings.select(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT)).toUtf8()).array();
    ASSERT_EQ(stored.size(), 2);
    EXPECT_EQ(stored.at(0).toObject().value(QStringLiteral("folder")).toString(), first);
    EXPECT_EQ(stored.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("First"));
    ProjectService another;
    another.setSettings(&m_settings);
    EXPECT_EQ(another.recent(), m_service->recent());
    EXPECT_EQ(another.lastProject(), first);

    // Ten at most.
    for (int number = 0; number < 12; ++number)
    {
        make(QStringLiteral("Project %1").arg(number));
    }
    const std::vector<RecentProject> recent = m_service->recent();
    ASSERT_EQ(recent.size(), ProjectService::kRecentLimit);
    EXPECT_EQ(recent.front().name, QStringLiteral("Project 11"));
    EXPECT_EQ(recent.back().name, QStringLiteral("Project 2"));

    // Settings that hold something else read as none.
    MemorySettings broken;
    broken.upsert(QString::fromLatin1(SettingsKeys::PROJECTS_RECENT), QStringLiteral("not a list"));
    ProjectService empty;
    empty.setSettings(&broken);
    EXPECT_TRUE(empty.recent().empty());
}

// With the setting on, the project used last is opened at the start; off, or with none, nothing is.
TEST_F(ProjectServiceCase, TheLastProjectIsOpenedAtTheStartOnlyWhenAsked)
{
    EXPECT_FALSE(m_service->openLastIfAsked().has_value()) << "no project was ever used";
    const QString folder = make(QStringLiteral("Last"));
    ASSERT_TRUE(m_service->close());

    ProjectService off;
    off.setSettings(&m_settings);
    EXPECT_FALSE(off.openLastIfAsked().has_value()) << "the setting is off by default";
    EXPECT_FALSE(off.isOpen());

    m_settings.upsert(QString::fromLatin1(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), QStringLiteral("1"));
    ProjectService on;
    on.setSettings(&m_settings);
    const std::optional<ProjectOutcome> opened = on.openLastIfAsked();
    ASSERT_TRUE(opened.has_value());
    EXPECT_TRUE(opened->ok()) << opened->why.toStdString();
    EXPECT_EQ(on.currentFolder(), folder);
    ASSERT_TRUE(on.close());

    // A last project that is gone is answered as a refusal, and nothing is open.
    ASSERT_TRUE(QDir(folder).removeRecursively());
    const std::optional<ProjectOutcome> gone = on.openLastIfAsked();
    ASSERT_TRUE(gone.has_value());
    ASSERT_TRUE(gone->refusal.has_value());
    EXPECT_EQ(*gone->refusal, ProjectRefusal::NotAProject);
    EXPECT_FALSE(on.isOpen());
}

// Images are added once, with where they are and where they are from the folder, stamped as opened; removed; and what
// the service holds is what the document on disk says.
TEST_F(ProjectServiceCase, ImagesAreAddedAndRemoved)
{
    EXPECT_EQ(m_service->addImage(under(QStringLiteral("L.fits")), ProjectImageKind::Image), nullptr)
        << "no project is open";
    EXPECT_FALSE(m_service->removeImage(QStringLiteral("none")));

    const QString folder = make(QStringLiteral("Images"));
    m_changes.clear();
    touch(QDir(folder).filePath(QStringLiteral("stacked/L.fits")));
    touch(under(QStringLiteral("Captures/jupiter.ser")));

    const ProjectImage* const added = m_service->addImage(
        QDir::toNativeSeparators(QDir(folder).filePath(QStringLiteral("stacked/L.fits"))), ProjectImageKind::Image);
    ASSERT_NE(added, nullptr);
    const QString firstId = added->id;
    EXPECT_TRUE(an::ui::shell::isProjectId(firstId));
    EXPECT_EQ(added->path, QDir(folder).filePath(QStringLiteral("stacked/L.fits")));
    EXPECT_EQ(added->relativePath, QStringLiteral("stacked/L.fits"));
    EXPECT_EQ(added->kind, ProjectImageKind::Image);
    EXPECT_TRUE(added->lastOpened.isValid());

    const ProjectImage* const sequence =
        m_service->addImage(under(QStringLiteral("Captures/jupiter.ser")), ProjectImageKind::Sequence);
    ASSERT_NE(sequence, nullptr);
    const QString secondId = sequence->id;
    EXPECT_NE(secondId, firstId);
    EXPECT_EQ(sequence->relativePath, QStringLiteral("../Captures/jupiter.ser"));
    EXPECT_EQ(sequence->kind, ProjectImageKind::Sequence);

    // The same file again is the same image.
    const ProjectImage* const again = m_service->addImage(
        QDir(folder).filePath(QStringLiteral("stacked/../stacked/L.fits")), ProjectImageKind::Image);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->id, firstId);
    EXPECT_EQ(m_service->current()->images.size(), 2u);
    EXPECT_EQ(m_changes, (std::vector<Change>{Change::Images, Change::Images, Change::Images}));

    const ProjectImage* const found = m_service->imageFor(under(QStringLiteral("Captures/jupiter.ser")));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, secondId);
    EXPECT_EQ(m_service->imageFor(under(QStringLiteral("Captures/saturn.ser"))), nullptr);
    EXPECT_EQ(m_service->addImage(QString(), ProjectImageKind::Image), nullptr);

    // An image's history is kept in a folder of the project named by its id.
    EXPECT_EQ(m_service->historyDirectoryFor(firstId), QDir(folder).filePath(QStringLiteral("history/") + firstId));
    EXPECT_TRUE(m_service->historyDirectoryFor(QStringLiteral("unknown")).isEmpty());

    const an::ui::shell::ProjectDocumentRead stored =
        an::ui::shell::readProjectDocument(QDir(folder).filePath(QStringLiteral("project.anproj")));
    ASSERT_TRUE(stored.isRead()) << stored.why.toStdString();
    EXPECT_EQ(*stored.document, *m_service->current());

    // Removing forgets the image; its file stays.
    EXPECT_FALSE(m_service->removeImage(QStringLiteral("unknown")));
    ASSERT_TRUE(m_service->removeImage(firstId));
    ASSERT_EQ(m_service->current()->images.size(), 1u);
    EXPECT_EQ(m_service->current()->images.front().id, secondId);
    EXPECT_TRUE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("stacked/L.fits"))));
    EXPECT_EQ(m_service->imageFor(QDir(folder).filePath(QStringLiteral("stacked/L.fits"))), nullptr);
    const an::ui::shell::ProjectDocumentRead after =
        an::ui::shell::readProjectDocument(QDir(folder).filePath(QStringLiteral("project.anproj")));
    ASSERT_TRUE(after.isRead());
    EXPECT_EQ(*after.document, *m_service->current());
}

// The workspaces a project was left with are recorded whole - their names, their locks, the images each showed, the
// one used last, the images that floated - in place of the ones recorded before, and read back when the project is
// opened again. What a document does not take is refused and the record stays; an image the project forgets leaves
// every view of it.
TEST_F(ProjectServiceCase, TheWorkspacesAreRecordedWholeAndReadBack)
{
    EXPECT_FALSE(m_service->setWorkspaces({}, {})) << "no project is open";

    const QString folder = make(QStringLiteral("Workspaces"));
    touch(under(QStringLiteral("L.fits")));
    touch(under(QStringLiteral("R.fits")));
    const QString luminance = m_service->addImage(under(QStringLiteral("L.fits")), ProjectImageKind::Image)->id;
    const QString red = m_service->addImage(under(QStringLiteral("R.fits")), ProjectImageKind::Image)->id;
    m_changes.clear();

    ProjectWorkspace first;
    first.id = QStringLiteral("1");
    first.name = QStringLiteral("Workspace 1");
    first.locked = true;
    first.views.push_back(ProjectWorkspaceView{luminance, QStringLiteral("an.view.a")});
    ProjectWorkspace second;
    second.id = QStringLiteral("2");
    second.name = QStringLiteral("Colour");
    second.last = true;
    const std::vector<ProjectWorkspace> workspaces{first, second};
    const std::vector<ProjectWorkspaceView> floating{ProjectWorkspaceView{red, QStringLiteral("an.view.b")}};

    ASSERT_TRUE(m_service->setWorkspaces(workspaces, floating));
    EXPECT_EQ(m_changes, std::vector<Change>{Change::Workspaces});
    EXPECT_EQ(m_service->current()->workspaces, workspaces);
    EXPECT_EQ(m_service->current()->floatingViews, floating);

    // Closed and opened again, the record is what was written.
    ASSERT_TRUE(m_service->close());
    ASSERT_TRUE(m_service->open(folder).ok());
    EXPECT_EQ(m_service->current()->workspaces, workspaces);
    EXPECT_EQ(m_service->current()->floatingViews, floating);

    // What a document does not take is refused, and the record stays as it was.
    m_changes.clear();
    std::vector<ProjectWorkspace> twice{first, first};
    EXPECT_FALSE(m_service->setWorkspaces(twice, {})) << "two workspaces of one id were recorded";
    EXPECT_FALSE(m_service->setWorkspaces(
        workspaces, {ProjectWorkspaceView{QStringLiteral("no-such-image"), QStringLiteral("an.view.c")}}))
        << "a floating view of an image the project does not hold was recorded";
    EXPECT_TRUE(m_changes.empty());
    EXPECT_EQ(m_service->current()->workspaces, workspaces);
    EXPECT_EQ(m_service->current()->floatingViews, floating);

    // An image the project forgets leaves the workspace that listed it, and floats no more.
    ASSERT_TRUE(m_service->removeImage(red));
    EXPECT_TRUE(m_service->current()->floatingViews.empty());
    ASSERT_TRUE(m_service->removeImage(luminance));
    EXPECT_TRUE(m_service->current()->workspaces.front().views.empty());
    EXPECT_EQ(m_service->current()->workspaces.size(), 2u) << "a workspace went with its last image";
}

// The open project is left only when its guard lets it go: asked before `close()` closes it, and before another takes
// its place once that one is sure to open - never for a project that is refused anyway. A guard that answers false
// keeps the project open: `close()` answers false, the other project is refused saying so and its lock is released.
TEST_F(ProjectServiceCase, TheOpenProjectIsLeftOnlyWhenItsGuardLetsItGo)
{
    int asked = 0;
    bool lets = false;
    QString openWhenAsked;
    m_service->setLeaveGuard([&]() {
        ++asked;
        openWhenAsked = m_service->currentFolder();
        return lets;
    });

    // With no project open there is nothing to leave.
    const QString first = make(QStringLiteral("First"));
    EXPECT_EQ(asked, 0);
    ProjectService other;
    const ProjectOutcome made = other.create(requestFor(QStringLiteral("Second"), root()));
    ASSERT_TRUE(made.ok()) << made.why.toStdString();
    const QString second = made.folder;
    ASSERT_TRUE(other.close());

    // Kept: the project stays open, whatever asked to leave it.
    EXPECT_FALSE(m_service->close());
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(openWhenAsked, first) << "the guard is asked while the project is still the open one";
    EXPECT_EQ(m_service->currentFolder(), first);

    const ProjectOutcome kept = m_service->open(second);
    ASSERT_FALSE(kept.ok());
    EXPECT_EQ(*kept.refusal, ProjectRefusal::OpenProjectKept);
    EXPECT_FALSE(kept.why.isEmpty());
    EXPECT_EQ(asked, 2);
    EXPECT_EQ(m_service->currentFolder(), first);
    EXPECT_FALSE(QFileInfo::exists(QDir(second).filePath(QStringLiteral(".lock"))))
        << "the project that did not open keeps its lock";

    const ProjectOutcome notMade = m_service->create(requestFor(QStringLiteral("Third"), root()));
    ASSERT_FALSE(notMade.ok());
    EXPECT_EQ(*notMade.refusal, ProjectRefusal::OpenProjectKept);
    EXPECT_EQ(asked, 3);
    EXPECT_FALSE(QFileInfo::exists(under(QStringLiteral("Third")))) << "a project that was not made left its folder";
    EXPECT_EQ(m_service->currentFolder(), first);

    // A project that is refused anyway asks nothing: the open one is not left for it.
    EXPECT_FALSE(m_service->open(under(QStringLiteral("No project here"))).ok());
    EXPECT_EQ(asked, 3);
    // The project that is open already is answered as open, and nothing is left.
    EXPECT_TRUE(m_service->open(first).ok());
    EXPECT_EQ(asked, 3);

    // Let go: the other project takes its place, and close closes.
    lets = true;
    ASSERT_TRUE(m_service->open(second).ok());
    EXPECT_EQ(asked, 4);
    EXPECT_EQ(openWhenAsked, first);
    EXPECT_EQ(m_service->currentFolder(), second);
    ASSERT_TRUE(m_service->close());
    EXPECT_EQ(asked, 5);
    EXPECT_FALSE(m_service->isOpen());

    // With the guard taken back, nothing is asked.
    ASSERT_TRUE(m_service->open(first).ok());
    m_service->setLeaveGuard({});
    lets = false;
    ASSERT_TRUE(m_service->close());
    EXPECT_EQ(asked, 5);
}

// A project moved with its images - the folder renamed, another drive - finds each image through its path from the
// folder, and the record follows the file.
TEST_F(ProjectServiceCase, AMovedImageIsFoundThroughItsRelativePath)
{
    // The project and its images live side by side under one folder that is then moved whole.
    const QString folder = make(QStringLiteral("Project"));
    ASSERT_TRUE(m_service->close());
    ASSERT_TRUE(QDir(root()).mkpath(QStringLiteral("Before")));
    ASSERT_TRUE(QDir().rename(folder, under(QStringLiteral("Before/Project"))));
    const QString before = under(QStringLiteral("Before/Project"));
    touch(under(QStringLiteral("Before/Captures/L.fits")));
    touch(under(QStringLiteral("Before/Captures/R.fits")));
    ASSERT_TRUE(m_service->open(before).ok());
    // An image is read before the next is added: the record an add returns lives in the project's list of images,
    // which the next add may move.
    const ProjectImage* const left =
        m_service->addImage(under(QStringLiteral("Before/Captures/L.fits")), ProjectImageKind::Image);
    ASSERT_NE(left, nullptr);
    const QString leftId = left->id;
    const ProjectImage* const right =
        m_service->addImage(under(QStringLiteral("Before/Captures/R.fits")), ProjectImageKind::Image);
    ASSERT_NE(right, nullptr);
    const QString rightId = right->id;
    EXPECT_EQ(m_service->locateImage(leftId), under(QStringLiteral("Before/Captures/L.fits")));
    ASSERT_TRUE(m_service->close());

    ASSERT_TRUE(QDir().rename(under(QStringLiteral("Before")), under(QStringLiteral("After"))));
    const QString after = under(QStringLiteral("After/Project"));
    ASSERT_TRUE(m_service->open(after).ok());
    ASSERT_EQ(m_service->current()->images.size(), 2u);
    EXPECT_EQ(m_service->current()->images.front().path, under(QStringLiteral("Before/Captures/L.fits")))
        << "the record still names where the file was";

    // Located: the file is where its relative path says, and the record follows.
    EXPECT_EQ(m_service->locateImage(leftId), under(QStringLiteral("After/Captures/L.fits")));
    EXPECT_EQ(m_service->current()->images.front().path, under(QStringLiteral("After/Captures/L.fits")));

    // Opened from its new place: it is the image the project held, not a new one.
    const ProjectImage* const reopened =
        m_service->addImage(under(QStringLiteral("After/Captures/R.fits")), ProjectImageKind::Image);
    ASSERT_NE(reopened, nullptr);
    EXPECT_EQ(reopened->id, rightId);
    EXPECT_EQ(reopened->path, under(QStringLiteral("After/Captures/R.fits")));
    EXPECT_EQ(m_service->current()->images.size(), 2u);

    // The records on disk follow too.
    const an::ui::shell::ProjectDocumentRead stored =
        an::ui::shell::readProjectDocument(QDir(after).filePath(QStringLiteral("project.anproj")));
    ASSERT_TRUE(stored.isRead());
    EXPECT_EQ(*stored.document, *m_service->current());

    // A file that is nowhere is located nowhere, and its record stays.
    ASSERT_TRUE(QFile::remove(under(QStringLiteral("After/Captures/L.fits"))));
    EXPECT_TRUE(m_service->locateImage(leftId).isEmpty());
    EXPECT_EQ(m_service->current()->images.front().path, under(QStringLiteral("After/Captures/L.fits")));
    EXPECT_TRUE(m_service->locateImage(QStringLiteral("unknown")).isEmpty());
}

// Closing: the project is closed, the observers are told once, and closing again does nothing.
TEST_F(ProjectServiceCase, Close)
{
    EXPECT_FALSE(m_service->close()) << "nothing is open";
    const QString folder = make(QStringLiteral("Closing"));
    m_changes.clear();

    ASSERT_TRUE(m_service->close());
    EXPECT_FALSE(m_service->isOpen());
    EXPECT_EQ(m_service->current(), nullptr);
    EXPECT_TRUE(m_service->currentFolder().isEmpty());
    EXPECT_EQ(m_changes, std::vector<Change>{Change::Closed});
    EXPECT_FALSE(m_service->close());
    EXPECT_EQ(m_changes.size(), 1u);

    // What closing leaves is a project that opens again; an observer that left is told nothing.
    m_service->unsubscribe(m_token);
    ASSERT_TRUE(m_service->open(folder).ok());
    EXPECT_EQ(m_changes.size(), 1u);

    // A service that goes with its project open releases the lock.
    m_service.reset();
    EXPECT_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral(".lock"))));
}

// The service is a model: what it includes makes no widget and shows no dialog.
TEST(ProjectServiceSource, OwnsNoWidgetAndShowsNoDialog)
{
    QString source;
    for (const QString& seed : {QDir::currentPath(), QCoreApplication::applicationDirPath()})
    {
        QDir folder(seed);
        do
        {
            if (QFileInfo(folder.filePath(QStringLiteral("src/AfterNightShellCore/projects/ProjectService.cpp")))
                    .isFile())
            {
                source = folder.filePath(QStringLiteral("src/AfterNightShellCore/projects"));
                break;
            }
        } while (folder.cdUp());
        if (!source.isEmpty())
        {
            break;
        }
    }
    ASSERT_FALSE(source.isEmpty()) << "the projects' sources are found from the working directory";
    for (const QString& name : QDir(source).entryList({QStringLiteral("*.h"), QStringLiteral("*.cpp")}, QDir::Files))
    {
        QFile file(QDir(source).filePath(name));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        for (const char* widget : {"<QWidget>", "<QDialog>", "<QMessageBox>", "<QFileDialog>", "<QtWidgets>",
                                   "QMessageBox::", "QFileDialog::", "IDialogService"})
        {
            EXPECT_FALSE(text.contains(QLatin1String(widget))) << name.toStdString() << " names " << widget;
        }
    }
}

// An image's record follows its file to another name, and an image that already was that file gives way to it.
TEST_F(ProjectServiceCase, AnImagesRecordFollowsItsFile)
{
    ASSERT_TRUE(m_service->create(requestFor(QStringLiteral("Follows"), root())).ok());
    const QString first = under(QStringLiteral("sky/M42.fits"));
    const QString second = under(QStringLiteral("sky/M31.fits"));
    const ProjectImage* added = m_service->addImage(first, ProjectImageKind::Image);
    ASSERT_NE(added, nullptr);
    const QString id = added->id;
    added = m_service->addImage(second, ProjectImageKind::Image);
    ASSERT_NE(added, nullptr);
    const QString otherId = added->id;
    ASSERT_NE(m_service->image(id), nullptr);
    EXPECT_EQ(m_service->image(id)->path, first);
    EXPECT_EQ(m_service->image(QStringLiteral("no-such-image")), nullptr);
    m_changes.clear();

    // Saved under another name: the same image, at another path, found by it and no longer by the one it left.
    const QString renamed = under(QStringLiteral("sky/final/M42-final.fits"));
    ASSERT_TRUE(m_service->setImagePath(id, renamed));
    EXPECT_EQ(m_changes, std::vector<Change>{Change::Images});
    ASSERT_NE(m_service->image(id), nullptr);
    EXPECT_EQ(m_service->image(id)->path, renamed);
    EXPECT_EQ(m_service->image(id)->relativePath,
              an::ui::shell::projectRelativePath(m_service->currentFolder(), renamed));
    ASSERT_NE(m_service->imageFor(renamed), nullptr);
    EXPECT_EQ(m_service->imageFor(renamed)->id, id);
    EXPECT_EQ(m_service->imageFor(first), nullptr);
    EXPECT_EQ(m_service->historyDirectoryFor(id),
              QDir(m_service->currentFolder()).filePath(QStringLiteral("history/") + id));
    EXPECT_EQ(m_service->current()->images.size(), 2u);

    // Saved over another image of the project: that image's record goes, and the file is this image.
    ASSERT_TRUE(m_service->setImagePath(id, second));
    ASSERT_EQ(m_service->current()->images.size(), 1u);
    EXPECT_EQ(m_service->image(otherId), nullptr);
    ASSERT_NE(m_service->imageFor(second), nullptr);
    EXPECT_EQ(m_service->imageFor(second)->id, id);

    // What is written is what a later session reads.
    const QString folder = m_service->currentFolder();
    ASSERT_TRUE(m_service->close());
    ASSERT_TRUE(m_service->open(folder).ok());
    ASSERT_NE(m_service->image(id), nullptr);
    EXPECT_EQ(m_service->image(id)->path, second);

    // Nothing follows nowhere, and no image is none.
    EXPECT_FALSE(m_service->setImagePath(id, QString()));
    EXPECT_FALSE(m_service->setImagePath(QStringLiteral("no-such-image"), first));
    ASSERT_TRUE(m_service->close());
    EXPECT_FALSE(m_service->setImagePath(id, first));
    EXPECT_EQ(m_service->image(id), nullptr);
}
