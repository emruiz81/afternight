// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A project for a job, with no widget. A job about to run into a folder with no project open gets a project there:
// made of the folder, whatever the job keeps in it, named as the folder, stating that an integration job made it and
// about the object its frames' OBJECT cards agree on; once for a folder, the project it is being opened from then on;
// none with the setting off, and none while a project is open. What a job is and what its run made are recorded in the
// open project. And what such a project takes on disk: the job's files in the project's own folder are its integration
// data, its runs' archives among them, the frames a run can make again are counted apart, and cleaning them frees
// exactly what the report counts.

#include "projects/JobProjects.h"
#include "projects/ProjectService.h"

#include "ShellCoreTestSupport.h"

#include "app/JobProjects.h"
#include "controls/settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdint>
#include <memory>
#include <optional>

namespace
{

using an::tests::shellcore::MemorySettings;
using an::ui::JobProject;
using an::ui::shell::JobProjects;
using an::ui::shell::ProjectCreateRequest;
using an::ui::shell::ProjectDocument;
using an::ui::shell::ProjectObjectSource;
using an::ui::shell::ProjectOrigin;
using an::ui::shell::ProjectOutcome;
using an::ui::shell::ProjectRefusal;
using an::ui::shell::ProjectService;
using an::ui::shell::ProjectStorageReport;

/// The bytes of every file below a folder.
[[nodiscard]] std::uint64_t bytesBelow(const QString& folder)
{
    std::uint64_t bytes = 0;
    QDirIterator files(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        files.next();
        bytes += static_cast<std::uint64_t>(files.fileInfo().size());
    }
    return bytes;
}

/// A project service over a root of the case's own, settings held in memory, and what answers a job from them.
class JobProjectsCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_service = std::make_unique<ProjectService>();
        m_service->setSettings(&m_settings);
        m_jobs = std::make_unique<JobProjects>(*m_service);
        m_jobs->setSettings(&m_settings);
        JobProjects::Hooks hooks;
        hooks.made = [this]() { ++m_made; };
        hooks.opened = [this]() { ++m_opened; };
        hooks.refused = [this](const ProjectOutcome& outcome) { m_refused.push_back(outcome); };
        m_jobs->setHooks(std::move(hooks));
    }

    void TearDown() override
    {
        m_jobs.reset();
        m_service.reset();
    }

    [[nodiscard]] QString under(const QString& relative) const
    {
        return QDir(QDir::cleanPath(m_root.path())).filePath(relative);
    }

    /// Writes a file of a size under the root; its path.
    QString write(const QString& relative, const qint64 size) const
    {
        const QString path = under(relative);
        EXPECT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        EXPECT_EQ(file.write(QByteArray(static_cast<int>(size), 'x')), size);
        return path;
    }

    /// An output folder as a run leaves it: a master, a stacked image, the frames it made on its way, the record of
    /// its last run and the archive of the run.
    QString jobFolder(const QString& name) const
    {
        (void)write(name + QStringLiteral("/masters/master_dark.fit"), 4000);
        (void)write(name + QStringLiteral("/stacked_L.fit"), 3000);
        (void)write(name + QStringLiteral("/calibrated_lights/light_001_cal.fit"), 7000);
        (void)write(name + QStringLiteral("/registered_lights/light_001_reg.fit"), 5000);
        (void)write(name + QStringLiteral("/last_run.json"), 100);
        (void)write(name + QStringLiteral("/history/20261118_010203_000/workflow_manifest.json"), 600);
        (void)write(name + QStringLiteral("/history/20261118_010203_000/workflow_report.html"), 900);
        return under(name);
    }

    /// Measures what the open project takes on disk, and waits for the report.
    [[nodiscard]] ProjectStorageReport measure() const
    {
        std::optional<ProjectStorageReport> measured;
        m_service->storageReport([&measured](const ProjectStorageReport& report) { measured = report; });
        QElapsedTimer waited;
        waited.start();
        while (!measured.has_value() && waited.elapsed() < 20000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        EXPECT_TRUE(measured.has_value()) << "the storage was not measured";
        return measured.value_or(ProjectStorageReport{});
    }

    QTemporaryDir m_root;
    MemorySettings m_settings;
    std::unique_ptr<ProjectService> m_service;
    std::unique_ptr<JobProjects> m_jobs;
    int m_made = 0;
    int m_opened = 0;
    std::vector<ProjectOutcome> m_refused;
};

} // namespace

// A job about to run with no project open gets one in its output folder: the folder becomes the project, named as the
// folder, stating that an integration job made it, and nothing of what the job keeps there is touched.
TEST_F(JobProjectsCase, AJobRunWithNoProjectGetsOneInItsOutputFolder)
{
    const QString folder = jobFolder(QStringLiteral("Carina Nebula"));
    const std::uint64_t before = bytesBelow(folder);
    EXPECT_FALSE(m_jobs->openProject().has_value());

    const std::optional<JobProject> project = m_jobs->projectForJob(folder, {});
    ASSERT_TRUE(project.has_value());
    EXPECT_EQ(QDir::cleanPath(project->folder), QDir::cleanPath(folder));
    EXPECT_EQ(project->name, QStringLiteral("Carina Nebula"));
    EXPECT_EQ(QDir::cleanPath(project->jobsFolder), QDir(folder).filePath(QStringLiteral("integration")));
    EXPECT_EQ(m_made, 1);
    EXPECT_EQ(m_opened, 0);
    EXPECT_TRUE(m_refused.empty());

    ASSERT_TRUE(m_service->isOpen());
    const ProjectDocument* const open = m_service->current();
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->name, QStringLiteral("Carina Nebula"));
    EXPECT_EQ(open->origin, ProjectOrigin::IntegrationJob);
    EXPECT_TRUE(ProjectService::isProjectFolder(folder));
    EXPECT_TRUE(QFileInfo(QDir(folder).filePath(QStringLiteral("integration"))).isDir());
    EXPECT_TRUE(QFileInfo(QDir(folder).filePath(QStringLiteral("workspaces"))).isDir());
    EXPECT_EQ(m_jobs->openProject(), project);

    // What the job kept there is as it was.
    EXPECT_EQ(QFileInfo(QDir(folder).filePath(QStringLiteral("stacked_L.fit"))).size(), 3000);
    EXPECT_EQ(QFileInfo(QDir(folder).filePath(QStringLiteral("calibrated_lights/light_001_cal.fit"))).size(), 7000);
    EXPECT_EQ(
        QFileInfo(QDir(folder).filePath(QStringLiteral("history/20261118_010203_000/workflow_report.html"))).size(),
        900);
    EXPECT_GE(bytesBelow(folder), before);
}

// A project made for a job is about the object its frames' OBJECT cards agree on, read from those cards and kept in
// its document: blanks around and inside a card and its case do not make another object, and a frame that states none
// says nothing either way. Opened again for another job, the project keeps the object it states.
TEST_F(JobProjectsCase, AProjectMadeForAJobIsAboutTheObjectItsFramesAgreeOn)
{
    const QString folder = jobFolder(QStringLiteral("Carina"));
    ASSERT_TRUE(m_jobs
                    ->projectForJob(folder, {QStringLiteral("NGC 3372"), QString(), QStringLiteral(" ngc  3372 "),
                                             QStringLiteral("NGC 3372")})
                    .has_value());
    ASSERT_NE(m_service->current(), nullptr);
    ASSERT_TRUE(m_service->current()->object.has_value()) << "the project states no object";
    EXPECT_EQ(m_service->current()->object->name, QStringLiteral("NGC 3372"));
    EXPECT_EQ(m_service->current()->object->source, ProjectObjectSource::ObjectCard);

    // The document says so: the project opened again reads it, and a job of other frames does not change it.
    ASSERT_TRUE(m_service->close());
    ASSERT_TRUE(m_jobs->projectForJob(folder, {QStringLiteral("M 42")}).has_value());
    ASSERT_NE(m_service->current(), nullptr);
    ASSERT_TRUE(m_service->current()->object.has_value());
    EXPECT_EQ(m_service->current()->object->name, QStringLiteral("NGC 3372"));
    EXPECT_EQ(m_service->current()->object->source, ProjectObjectSource::ObjectCard);
}

// Frames that name different objects give a project made for their job no object - it is about several - and so do
// frames none of which states one, and a job with no frames.
TEST_F(JobProjectsCase, FramesThatDisagreeOrStateNoObjectGiveTheProjectNone)
{
    const QString mixed = jobFolder(QStringLiteral("Mixed"));
    ASSERT_TRUE(m_jobs->projectForJob(mixed, {QStringLiteral("M 42"), QStringLiteral("M 43"), QString()}).has_value());
    ASSERT_NE(m_service->current(), nullptr);
    EXPECT_FALSE(m_service->current()->object.has_value()) << m_service->current()->object->name.toStdString();
    ASSERT_TRUE(m_service->close());

    const QString none = jobFolder(QStringLiteral("None"));
    ASSERT_TRUE(m_jobs->projectForJob(none, {QString(), QStringLiteral("  ")}).has_value());
    ASSERT_NE(m_service->current(), nullptr);
    EXPECT_FALSE(m_service->current()->object.has_value()) << m_service->current()->object->name.toStdString();
    ASSERT_TRUE(m_service->close());

    const QString noFrames = jobFolder(QStringLiteral("No Frames"));
    ASSERT_TRUE(m_jobs->projectForJob(noFrames, {}).has_value());
    ASSERT_NE(m_service->current(), nullptr);
    EXPECT_FALSE(m_service->current()->object.has_value()) << m_service->current()->object->name.toStdString();
}

// A folder gets one project, however many runs go into it: the project it already is is opened.
TEST_F(JobProjectsCase, AFolderGetsOneProjectAndItIsOpenedFromThenOn)
{
    const QString folder = jobFolder(QStringLiteral("Once"));
    ASSERT_TRUE(m_jobs->projectForJob(folder, {}).has_value());
    const QString id = m_service->current()->id;
    ASSERT_TRUE(m_service->close());
    ASSERT_FALSE(m_service->isOpen());

    const std::optional<JobProject> again = m_jobs->projectForJob(folder, {});
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(m_service->current()->id, id) << "a second project was made of the folder";
    EXPECT_EQ(m_made, 1);
    EXPECT_EQ(m_opened, 1);

    // A folder a person made a project of is used the same way.
    ASSERT_TRUE(m_service->close());
    ProjectCreateRequest request;
    request.name = QStringLiteral("Made By Hand");
    request.location = under(QString());
    const ProjectOutcome made = m_service->create(request);
    ASSERT_TRUE(made.ok()) << made.why.toStdString();
    const QString handMade = made.folder;
    ASSERT_TRUE(m_service->close());
    const std::optional<JobProject> used = m_jobs->projectForJob(handMade, {});
    ASSERT_TRUE(used.has_value());
    EXPECT_EQ(m_service->current()->origin, ProjectOrigin::User) << "the project that was there is the one used";
    EXPECT_EQ(m_made, 1);
    EXPECT_EQ(m_opened, 2);
}

// With the setting off no project is made, and the folder is left as it is; a job that names no folder gets none.
TEST_F(JobProjectsCase, NoneIsMadeWithTheSettingOff)
{
    const QString folder = jobFolder(QStringLiteral("Off"));
    (void)m_settings.upsert(QString::fromLatin1(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS),
                            QStringLiteral("0"));
    EXPECT_FALSE(m_jobs->projectForJob(folder, {}).has_value());
    EXPECT_FALSE(m_service->isOpen());
    EXPECT_FALSE(ProjectService::isProjectFolder(folder));
    EXPECT_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("integration"))));
    EXPECT_EQ(m_made, 0);
    EXPECT_TRUE(m_refused.empty()) << "a setting that asks for none is no refusal";

    (void)m_settings.upsert(QString::fromLatin1(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS),
                            QStringLiteral("1"));
    EXPECT_FALSE(m_jobs->projectForJob(QString(), {}).has_value());
    EXPECT_FALSE(m_service->isOpen());
    EXPECT_TRUE(m_jobs->projectForJob(folder, {}).has_value());
}

// A job that runs while a project is open runs in that project: none is made of its folder.
TEST_F(JobProjectsCase, NoneIsMadeWhileAProjectIsOpen)
{
    ProjectCreateRequest request;
    request.name = QStringLiteral("Open Already");
    request.location = under(QString());
    ASSERT_TRUE(m_service->create(request).ok());
    const QString open = m_service->currentFolder();
    const QString folder = jobFolder(QStringLiteral("Elsewhere"));

    EXPECT_FALSE(m_jobs->projectForJob(folder, {}).has_value());
    EXPECT_EQ(m_service->currentFolder(), open);
    EXPECT_FALSE(ProjectService::isProjectFolder(folder));
    const std::optional<JobProject> seen = m_jobs->openProject();
    ASSERT_TRUE(seen.has_value());
    EXPECT_EQ(seen->name, QStringLiteral("Open Already"));
    EXPECT_EQ(QDir::cleanPath(seen->jobsFolder), QDir(open).filePath(QStringLiteral("integration")));
}

// A folder no project can be made of - a file's path - is told as a refusal, and the job gets no project.
TEST_F(JobProjectsCase, AFolderThatCannotHoldAProjectIsToldAndTheJobGetsNone)
{
    const QString file = write(QStringLiteral("not-a-folder.fit"), 10);
    EXPECT_FALSE(m_jobs->projectForJob(file, {}).has_value());
    EXPECT_FALSE(m_service->isOpen());
    ASSERT_EQ(m_refused.size(), 1u);
    ASSERT_TRUE(m_refused.front().refusal.has_value());
    EXPECT_EQ(*m_refused.front().refusal, ProjectRefusal::InvalidLocation);
    EXPECT_FALSE(m_refused.front().why.isEmpty());
}

// A job is recorded in the open project once, by its name and the folder its runs write to, and what its run made
// are the project's images, each once; a file the run names and did not leave is none.
TEST_F(JobProjectsCase, TheJobAndWhatItsRunMadeAreRecordedInTheProject)
{
    const QString folder = jobFolder(QStringLiteral("Recorded"));
    ASSERT_TRUE(m_jobs->projectForJob(folder, {}).has_value());

    m_jobs->recordJob(QStringLiteral("last_session"), folder);
    m_jobs->recordJob(QStringLiteral("last_session"), folder);
    ASSERT_EQ(m_service->current()->integrations.size(), 1u);
    EXPECT_EQ(m_service->current()->integrations.front().name, QStringLiteral("last_session"));
    EXPECT_EQ(QDir::cleanPath(m_service->current()->integrations.front().folder), QDir::cleanPath(folder));
    EXPECT_FALSE(m_service->current()->integrations.front().id.isEmpty());

    const QString stacked = QDir(folder).filePath(QStringLiteral("stacked_L.fit"));
    const QString master = QDir(folder).filePath(QStringLiteral("masters/master_dark.fit"));
    m_jobs->addJobImages({stacked, master, QDir(folder).filePath(QStringLiteral("never_written.fit")), QString()});
    m_jobs->addJobImages({stacked});
    ASSERT_EQ(m_service->current()->images.size(), 2u);
    EXPECT_NE(m_service->imageFor(stacked), nullptr);
    EXPECT_NE(m_service->imageFor(master), nullptr);

    // The record is the document's: a project opened again holds it.
    ASSERT_TRUE(m_service->close());
    ASSERT_TRUE(m_service->open(folder).ok());
    EXPECT_EQ(m_service->current()->integrations.size(), 1u);
    EXPECT_EQ(m_service->current()->images.size(), 2u);

    // With no project open nothing is recorded, and nothing fails.
    ASSERT_TRUE(m_service->close());
    m_jobs->recordJob(QStringLiteral("last_session"), folder);
    m_jobs->addJobImages({stacked});
    EXPECT_FALSE(m_service->isOpen());
}

// A project made of a folder that holds files is made beside them, a folder that is a project already is answered as
// one to open, and a project that is not made leaves the folder as it was.
TEST_F(JobProjectsCase, CreateInFolderMakesAProjectOfAFolderThatHoldsFiles)
{
    const QString folder = jobFolder(QStringLiteral("Held"));
    const QString notes = write(QStringLiteral("Held/notes.md"), 42);
    ProjectCreateRequest request;
    request.name = QStringLiteral("Held");
    request.origin = ProjectOrigin::IntegrationJob;
    const ProjectOutcome made = m_service->createInFolder(folder, request);
    ASSERT_TRUE(made.ok()) << made.why.toStdString();
    EXPECT_EQ(QDir::cleanPath(made.folder), QDir::cleanPath(folder));
    EXPECT_EQ(QFileInfo(notes).size(), 42) << "notes that were there were written over";
    EXPECT_EQ(m_service->current()->origin, ProjectOrigin::IntegrationJob);

    ASSERT_TRUE(m_service->close());
    const ProjectOutcome again = m_service->createInFolder(folder, request);
    ASSERT_FALSE(again.ok());
    EXPECT_EQ(*again.refusal, ProjectRefusal::AlreadyAProject);

    // A folder that is not there is made; a name that is none is refused and nothing is made.
    const QString fresh = under(QStringLiteral("Fresh Output"));
    request.name = QString();
    const ProjectOutcome unnamed = m_service->createInFolder(fresh, request);
    ASSERT_FALSE(unnamed.ok());
    EXPECT_EQ(*unnamed.refusal, ProjectRefusal::InvalidName);
    EXPECT_FALSE(QFileInfo::exists(fresh));
    request.name = QStringLiteral("Fresh Output");
    const ProjectOutcome freshMade = m_service->createInFolder(fresh, request);
    ASSERT_TRUE(freshMade.ok()) << freshMade.why.toStdString();
    EXPECT_TRUE(ProjectService::isProjectFolder(fresh));
}

// What a project made of a job's folder takes on disk: the job's files in the project's own folder are its
// integration data, its runs' archives among them and not undo history; the frames a run can make again are counted
// apart; and the parts add up to the folder.
TEST_F(JobProjectsCase, TheStorageOfAProjectMadeOfAJobsFolderCountsTheJobsFiles)
{
    const QString folder = jobFolder(QStringLiteral("Counted"));
    ASSERT_TRUE(m_jobs->projectForJob(folder, {}).has_value());
    // A job made in the project keeps its folder under the project's integration folder.
    (void)write(QStringLiteral("Counted/integration/second/stacked_R.fit"), 2000);
    (void)write(QStringLiteral("Counted/integration/second/ca_corrected_lights/light_001_ca.fit"), 1000);

    const ProjectStorageReport report = measure();
    EXPECT_FALSE(report.measuredOnGuiThread);
    EXPECT_EQ(report.intermediateBytes, 7000u + 5000u + 1000u);
    EXPECT_EQ(report.integrationBytes, 4000u + 3000u + 100u + 600u + 900u + 2000u);
    EXPECT_EQ(report.unlistedHistoryBytes, 0u) << "a run's archive was counted as undo history";
    EXPECT_EQ(report.historyBytes, 0u);
    EXPECT_EQ(report.historyBytes + report.unlistedHistoryBytes + report.integrationBytes + report.intermediateBytes +
                  report.otherBytes,
              bytesBelow(folder));
}

// In a project a person made, what lies in its own folder beside its integration folder is not a job's.
TEST_F(JobProjectsCase, AProjectMadeByAPersonCountsOnlyItsIntegrationFolder)
{
    ProjectCreateRequest request;
    request.name = QStringLiteral("By Hand");
    request.location = under(QString());
    ASSERT_TRUE(m_service->create(request).ok());
    (void)write(QStringLiteral("By Hand/calibrated_lights/stray.fit"), 500);
    (void)write(QStringLiteral("By Hand/integration/job/registered_lights/light_reg.fit"), 800);
    (void)write(QStringLiteral("By Hand/integration/job/stacked.fit"), 300);

    const ProjectStorageReport report = measure();
    EXPECT_EQ(report.intermediateBytes, 800u);
    EXPECT_EQ(report.integrationBytes, 300u);
    EXPECT_GE(report.otherBytes, 500u);
}

// Cleaning the intermediate frames removes the folders the report counts as them, and nothing else, and frees exactly
// what the report said.
TEST_F(JobProjectsCase, CleaningTheIntermediateFramesFreesWhatTheReportCounts)
{
    const QString folder = jobFolder(QStringLiteral("Cleaned"));
    ASSERT_TRUE(m_jobs->projectForJob(folder, {}).has_value());
    (void)write(QStringLiteral("Cleaned/integration/second/registered_lights/deep/light_reg.fit"), 1500);
    (void)write(QStringLiteral("Cleaned/integration/second/stacked_R.fit"), 2000);
    const ProjectStorageReport before = measure();
    ASSERT_EQ(before.intermediateBytes, 7000u + 5000u + 1500u);

    std::optional<std::uint64_t> freed;
    m_service->cleanIntermediateFrames([&freed](const std::uint64_t bytes) { freed = bytes; });
    EXPECT_FALSE(freed.has_value()) << "the frames were removed before the call returned";
    QElapsedTimer waited;
    waited.start();
    while (!freed.has_value() && waited.elapsed() < 20000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    ASSERT_TRUE(freed.has_value());
    EXPECT_EQ(*freed, before.intermediateBytes);

    EXPECT_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("calibrated_lights"))));
    EXPECT_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("registered_lights"))));
    EXPECT_FALSE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("integration/second/registered_lights"))));
    // What a run keeps is where it was.
    EXPECT_EQ(QFileInfo(QDir(folder).filePath(QStringLiteral("stacked_L.fit"))).size(), 3000);
    EXPECT_EQ(QFileInfo(QDir(folder).filePath(QStringLiteral("masters/master_dark.fit"))).size(), 4000);
    EXPECT_EQ(QFileInfo(QDir(folder).filePath(QStringLiteral("integration/second/stacked_R.fit"))).size(), 2000);
    EXPECT_TRUE(
        QFileInfo::exists(QDir(folder).filePath(QStringLiteral("history/20261118_010203_000/workflow_report.html"))));

    const ProjectStorageReport after = measure();
    EXPECT_EQ(after.intermediateBytes, 0u);
    EXPECT_EQ(after.integrationBytes, before.integrationBytes);
}
