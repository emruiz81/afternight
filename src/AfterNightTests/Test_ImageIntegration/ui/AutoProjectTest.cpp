// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Integration and the application's projects, which it reaches through one interface and knows nothing more
/// of. A run that starts with no project open asks for one in its output folder; the job then is the project's - its
/// session is moved into the project's own database, and it is recorded in the project by its name and its folder -
/// and what the run made are the project's images. A run in a project that is open asks for none and joins it. Where
/// the application's projects answer that the job gets none - the setting is off - or where there are none to ask,
/// the job stays outside any project, as it always was. And a window made while a project is open keeps its session in
/// the project's database and its job's folder under the project's.

#include "Test_ImageIntegration/ui/IntegrationPageCase.h"
#include "Test_ImageIntegration/workflow/FixturePipeline.h"

#include "app/JobProjects.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "processes/ImageIntegration/widgets/WorkflowRunView.h"

#include <QDir>
#include <QFileInfo>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>
#include <optional>

namespace an::tests::integration
{
namespace
{

using an::ui::JobProject;

/// The application's projects, as a case states them, keeping what a job asked and said.
class RecordingProjects final : public an::ui::IJobProjects
{
public:
    /// States itself as the projects of the application `context` stands for, for as long as it lives.
    explicit RecordingProjects(ProcessWindowContext& context) : m_context(context)
    {
        m_context.jobProjects = this;
    }

    ~RecordingProjects()
    {
        m_context.jobProjects = nullptr;
    }

    RecordingProjects(const RecordingProjects&) = delete;
    RecordingProjects& operator=(const RecordingProjects&) = delete;

    [[nodiscard]] std::optional<JobProject> openProject() const override
    {
        return open;
    }

    [[nodiscard]] std::optional<JobProject> projectForJob(const QString& outputFolder) override
    {
        asked.append(outputFolder);
        if (!makesProjects)
        {
            return std::nullopt;
        }
        // The folder is the project from now on.
        JobProject made;
        made.folder = outputFolder;
        made.name = QDir(outputFolder).dirName();
        made.jobsFolder = QDir(outputFolder).filePath(QStringLiteral("integration"));
        open = made;
        return open;
    }

    void recordJob(const QString& name, const QString& outputFolder) override
    {
        jobs.append(qMakePair(name, outputFolder));
    }

    void addJobImages(const QStringList& files) override
    {
        images.append(files);
    }

    /// The project that is open; none until a case states one, or a job is given one.
    std::optional<JobProject> open;

    /// Whether a job that asks gets a project of its folder: what the setting decides.
    bool makesProjects = true;

    QStringList asked;
    QList<QPair<QString, QString>> jobs;
    QStringList images;

private:
    ProcessWindowContext& m_context;
};

/// Whether a database holds a session of a name.
[[nodiscard]] bool holdsSession(const QString& databasePath, const QString& name)
{
    if (!QFileInfo(databasePath).isFile())
    {
        return false;
    }
    IntegrationDatabase database(databasePath);
    return database.initialize() && database.loadSessionResult(name).model.has_value();
}

[[nodiscard]] QString projectDatabase(const JobProject& project)
{
    return QDir(project.jobsFolder).filePath(QString::fromLatin1(ImageIntegration::kSessionDatabaseFileName));
}

/// The cases of a job that meets the application's projects: a process shown as a page, and the fixture pipeline.
class AutoProject : public IntegrationPageCase
{
protected:
    void TearDown() override
    {
        if (m_gate)
        {
            m_gate->open();
        }
        if (!m_process.isNull())
        {
            (void)pumpUntil([this]() { return !m_process->runState().running(); });
        }
        IntegrationPageCase::TearDown();
    }

    /// Starts the fixture pipeline with a stack from the process, into the case's output folder.
    bool startRun(ImageIntegrationProcess& process)
    {
        m_gate = std::make_shared<RunGate>();
        return process.startRun(fixtureManifestWithAStack(output()), fixturePipelineWithAStack(m_gate));
    }

    /// Lets the run go to its end and waits for it.
    [[nodiscard]] bool finish(ImageIntegrationProcess& process)
    {
        m_gate->open();
        return pumpUntil([&process]() { return !process.runState().running(); }) &&
               pumpUntil([&process]() { return process.runView() != nullptr && process.runView()->isFinished(); });
    }

    [[nodiscard]] QString output() const
    {
        return QDir::cleanPath(m_output.path());
    }

    QTemporaryDir m_output;
    std::shared_ptr<RunGate> m_gate;
};

/// The cases of where a window keeps its job.
class IntegrationPersistence : public AutoProject
{
};

} // namespace

// A run that starts with no project open asks for one in its output folder, once. The job is then the project's: its
// session is in the project's database and no longer in the one outside any project, it is recorded in the project by
// its name and its folder, and when the run ends what it made are the project's images.
TEST_F(AutoProject, ARunWithNoProjectOpenAsksForOneInItsOutputFolder)
{
    RecordingProjects projects(m_scope.context());
    ImageIntegrationProcess& process = openAsPage();
    ASSERT_TRUE(ImageIntegration::sameSessionDatabase(process.sessionDatabasePath(),
                                                      ImageIntegration::applicationSessionDatabasePath()));
    process.model()->setOutputDirectory(output());
    pump();

    ASSERT_TRUE(startRun(process));
    ASSERT_EQ(projects.asked.size(), 1);
    EXPECT_EQ(QDir::cleanPath(projects.asked.front()), output());
    ASSERT_TRUE(projects.open.has_value());

    // The job is the project's.
    const QString database = projectDatabase(*projects.open);
    EXPECT_TRUE(ImageIntegration::sameSessionDatabase(process.sessionDatabasePath(), database))
        << process.sessionDatabasePath().toStdString();
    EXPECT_TRUE(holdsSession(database, process.sessionName()));
    EXPECT_FALSE(holdsSession(ImageIntegration::applicationSessionDatabasePath(), kPageSession))
        << "the job is still listed outside any project";
    ASSERT_EQ(projects.jobs.size(), 1);
    EXPECT_EQ(projects.jobs.front().first, process.sessionName());
    EXPECT_EQ(QDir::cleanPath(projects.jobs.front().second), output());
    EXPECT_TRUE(projects.images.isEmpty()) << "nothing is the project's image before the run made it";

    ASSERT_TRUE(finish(process));
    ASSERT_TRUE(process.runView()->wasSuccessful());
    ASSERT_EQ(projects.images.size(), 1);
    EXPECT_EQ(QDir::cleanPath(projects.images.front()),
              QDir(output()).filePath(QString::fromLatin1(kFixtureStackedFile)));
    EXPECT_TRUE(QFileInfo(projects.images.front()).isFile());

    // What the page changes from here on is saved with the project's job.
    process.model()->addFrames(FrameType::Light, {lightFrame(output(), 1)});
    pump();
    ASSERT_TRUE(process.close());
    pump();
    IntegrationDatabase stored(database);
    ASSERT_TRUE(stored.initialize());
    const SessionLoadResult loaded = stored.loadSessionResult(kPageSession);
    ASSERT_TRUE(loaded.model.has_value());
    IntegrationDataModel read;
    read.fromJson(*loaded.model);
    EXPECT_EQ(read.lightFrames().size(), 1u);
}

// A second run of the job asks for no project: it runs in the one it was given, and is recorded there once more -
// which the project keeps as one job.
TEST_F(AutoProject, OnceTheJobRunsInAProjectItAsksForNoOther)
{
    RecordingProjects projects(m_scope.context());
    ImageIntegrationProcess& process = openAsPage();
    process.model()->setOutputDirectory(output());
    ASSERT_TRUE(startRun(process));
    ASSERT_TRUE(finish(process));
    ASSERT_EQ(projects.asked.size(), 1);

    ASSERT_TRUE(startRun(process));
    EXPECT_EQ(projects.asked.size(), 1) << "a project is open: none is asked for";
    ASSERT_TRUE(finish(process));
    EXPECT_EQ(projects.jobs.size(), 2);
    EXPECT_EQ(projects.jobs.back().first, projects.jobs.front().first);
    EXPECT_EQ(projects.images.size(), 2);
}

// A run in a project that is open asks for none: the job joins the open project, whatever folder it writes to.
TEST_F(AutoProject, ARunInAnOpenProjectJoinsItAndAsksForNone)
{
    QTemporaryDir projectFolder;
    ASSERT_TRUE(projectFolder.isValid());
    ImageIntegrationProcess& process = openAsPage();
    process.model()->setOutputDirectory(output());
    pump();
    // The project is opened after the window was made: the window's session is still the one outside any project.
    RecordingProjects projects(m_scope.context());
    projects.open = JobProject{QDir::cleanPath(projectFolder.path()), QStringLiteral("Open"),
                               QDir(projectFolder.path()).filePath(QStringLiteral("integration"))};

    ASSERT_TRUE(startRun(process));
    EXPECT_TRUE(projects.asked.isEmpty());
    EXPECT_TRUE(ImageIntegration::sameSessionDatabase(process.sessionDatabasePath(), projectDatabase(*projects.open)));
    ASSERT_EQ(projects.jobs.size(), 1);
    EXPECT_EQ(QDir::cleanPath(projects.jobs.front().second), output());
    ASSERT_TRUE(finish(process));
    EXPECT_EQ(projects.images.size(), 1);
}

// Where the application's projects give the job none - the setting is off - the job stays outside any project: its
// session where it was, nothing recorded, and the run as it always was.
TEST_F(AutoProject, AJobGivenNoProjectStaysOutsideAny)
{
    RecordingProjects projects(m_scope.context());
    projects.makesProjects = false;
    ImageIntegrationProcess& process = openAsPage();
    process.model()->setOutputDirectory(output());

    ASSERT_TRUE(startRun(process));
    EXPECT_EQ(projects.asked.size(), 1);
    EXPECT_TRUE(ImageIntegration::sameSessionDatabase(process.sessionDatabasePath(),
                                                      ImageIntegration::applicationSessionDatabasePath()));
    ASSERT_TRUE(finish(process));
    EXPECT_TRUE(process.runView()->wasSuccessful());
    EXPECT_TRUE(projects.jobs.isEmpty());
    EXPECT_TRUE(projects.images.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(QDir(output()).filePath(QStringLiteral("integration"))));
}

// A job of the same name that the project already holds, which writes to another folder, is another job: the one
// that joins takes a name of its own and writes over nothing.
TEST_F(AutoProject, AJobThatJoinsWritesOverNoJobOfTheProject)
{
    QTemporaryDir projectFolder;
    ASSERT_TRUE(projectFolder.isValid());
    const JobProject open{QDir::cleanPath(projectFolder.path()), QStringLiteral("Open"),
                          QDir(projectFolder.path()).filePath(QStringLiteral("integration"))};
    // The project holds a job named as the window's session, which writes elsewhere.
    ASSERT_TRUE(QDir().mkpath(open.jobsFolder));
    {
        IntegrationDataModel other;
        other.setOutputDirectory(QDir(projectFolder.path()).filePath(QStringLiteral("integration/other")));
        other.addFrames(FrameType::Light, {lightFrame(output(), 1), lightFrame(output(), 2)});
        IntegrationDatabase store(projectDatabase(open));
        ASSERT_TRUE(store.saveSession(kPageSession, other.toJson())) << store.lastError().toStdString();
    }

    ImageIntegrationProcess& process = openAsPage();
    process.model()->setOutputDirectory(output());
    RecordingProjects projects(m_scope.context());
    projects.open = open;
    ASSERT_TRUE(startRun(process));
    EXPECT_NE(process.sessionName(), kPageSession) << "the job took the name of one the project holds";
    EXPECT_TRUE(holdsSession(projectDatabase(open), process.sessionName()));
    ASSERT_EQ(projects.jobs.size(), 1);
    EXPECT_EQ(projects.jobs.front().first, process.sessionName());
    ASSERT_TRUE(finish(process));

    // The job that was there is as it was.
    IntegrationDatabase store(projectDatabase(open));
    ASSERT_TRUE(store.initialize());
    const SessionLoadResult kept = store.loadSessionResult(kPageSession);
    ASSERT_TRUE(kept.model.has_value());
    IntegrationDataModel read;
    read.fromJson(*kept.model);
    EXPECT_EQ(read.lightFrames().size(), 2u);
}

// A window made while a project is open keeps its session in the project's database and its job's folder under the
// project's; made with none open, in the database outside any project, with no folder of its own.
TEST_F(IntegrationPersistence, PathsComeFromTheProject)
{
    QTemporaryDir projectFolder;
    ASSERT_TRUE(projectFolder.isValid());
    const QString jobsFolder = QDir(QDir::cleanPath(projectFolder.path())).filePath(QStringLiteral("integration"));
    {
        RecordingProjects projects(m_scope.context());
        projects.open = JobProject{QDir::cleanPath(projectFolder.path()), QStringLiteral("Carina"), jobsFolder};
        ImageIntegrationProcess& process = openAsWindow();
        const QString database =
            QDir(jobsFolder).filePath(QString::fromLatin1(ImageIntegration::kSessionDatabaseFileName));
        EXPECT_TRUE(ImageIntegration::sameSessionDatabase(process.sessionDatabasePath(), database))
            << process.sessionDatabasePath().toStdString();
        EXPECT_TRUE(QFileInfo(database).isFile());
        EXPECT_EQ(QDir::cleanPath(process.model()->outputDirectory()),
                  QDir(jobsFolder).filePath(process.sessionName()));
        EXPECT_FALSE(QFileInfo::exists(process.model()->outputDirectory())) << "a job's folder is made by its run";

        // What the window holds is saved there, and nothing of it outside the project.
        process.model()->addFrames(FrameType::Light, {lightFrame(output(), 1), lightFrame(output(), 2)});
        pump();
        ASSERT_TRUE(process.close());
        pump();
        delete m_process.data();
        pump();
        EXPECT_TRUE(holdsSession(database, kPageSession));
        EXPECT_FALSE(holdsSession(ImageIntegration::applicationSessionDatabasePath(), kPageSession));
        EXPECT_TRUE(projects.asked.isEmpty());
    }

    // With no project open: the database outside any project, and no folder until the person names one.
    RecordingProjects none(m_scope.context());
    ImageIntegrationProcess& outside = openAsWindow();
    EXPECT_TRUE(ImageIntegration::sameSessionDatabase(outside.sessionDatabasePath(),
                                                      ImageIntegration::applicationSessionDatabasePath()));
    EXPECT_TRUE(outside.model()->outputDirectory().isEmpty());
}

} // namespace an::tests::integration
