// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration jobs over temporary session databases: a row for each session of each database, with its project,
/// its frames, the masters its last run left, when that run ended and how long it took; the status read from the record
/// the last run left - completed, with warnings, partly, failed or cancelled at a stage, or never run - and running for
/// the job an open Image Integration runs, which is listed first; the others by their last run; the jobs read off the
/// GUI thread; and a database that is not there giving none and left unmade.

#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSize>
#include <QTemporaryDir>
#include <QThread>
#include <QTime>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace
{

using ImageIntegration::IntegrationJob;
using ImageIntegration::IntegrationJobSource;
using ImageIntegration::IntegrationJobStatus;
using ImageIntegration::RunningIntegrationJob;

/// Session databases in a folder of their own, each session with its frames and its output folder, and the records
/// its runs left there.
class IntegrationJobsCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_folder.isValid());
    }

    [[nodiscard]] QString path(const QString& name) const
    {
        return QDir(m_folder.path()).filePath(name);
    }

    /// Saves a session of `lights` light frames and `darks` dark frames whose runs write into `output`.
    static void saveSession(const QString& database, const QString& name, const int lights, const int darks,
                            const QString& output)
    {
        IntegrationDataModel model;
        std::vector<FrameInfo> lightFrames;
        for (int index = 0; index < lights; ++index)
        {
            const QString file = QStringLiteral("C:/frames/%1/light_%2.fit").arg(name).arg(index);
            lightFrames.emplace_back(FrameId(file), file,
                                     FrameMetadata(FrameType::Light, QSize(64, 48), 300.0, -10.0, 800, 1,
                                                   QStringLiteral("L"), QStringLiteral("NGC 3372"),
                                                   QDateTime::currentDateTimeUtc(), QString()));
        }
        model.addFrames(FrameType::Light, lightFrames);
        std::vector<FrameInfo> darkFrames;
        for (int index = 0; index < darks; ++index)
        {
            const QString file = QStringLiteral("C:/frames/%1/dark_%2.fit").arg(name).arg(index);
            darkFrames.emplace_back(FrameId(file), file,
                                    FrameMetadata(FrameType::Dark, QSize(64, 48), 300.0, -10.0, 800, 1, QString(),
                                                  QString(), QDateTime::currentDateTimeUtc(), QString()));
        }
        model.addFrames(FrameType::Dark, darkFrames);
        model.setOutputDirectory(output);
        IntegrationDatabase store(database);
        ASSERT_TRUE(store.saveSession(name, model.toJson())) << store.lastError().toStdString();
    }

    /// Leaves the record of a run in an output folder.
    static void leaveRecord(const QString& output, const WorkflowRunEnd end, const QDateTime& ended,
                            const qint64 durationMs, const int masters, const QString& stage = QString(),
                            const int warnings = 0)
    {
        ASSERT_TRUE(QDir().mkpath(output));
        WorkflowRunRecord record;
        record.ended = ended;
        record.durationMs = durationMs;
        record.end = end;
        record.stage = stage;
        record.masters = masters;
        record.warnings = warnings;
        ASSERT_TRUE(WorkflowRunStateStore(output).saveRunRecord(record));
    }

    [[nodiscard]] static const IntegrationJob* jobNamed(const std::vector<IntegrationJob>& jobs, const QString& name)
    {
        const auto found =
            std::find_if(jobs.begin(), jobs.end(), [&name](const IntegrationJob& job) { return job.name == name; });
        return found != jobs.end() ? &*found : nullptr;
    }

    [[nodiscard]] static QStringList namesOf(const std::vector<IntegrationJob>& jobs)
    {
        QStringList names;
        for (const IntegrationJob& job : jobs)
        {
            names.append(job.name);
        }
        return names;
    }

    /// Runs the event loop until the model's read has landed.
    static bool waitForTheRead(const ImageIntegration::IntegrationJobsModel& model, const int milliseconds = 10000)
    {
        QElapsedTimer timer;
        timer.start();
        while (model.isReading() && timer.elapsed() < milliseconds)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
        return !model.isReading();
    }

    /// Two databases: the one outside any project, with a session cancelled at registration and one completed, and a
    /// project's, with a session completed with warnings and one never run.
    [[nodiscard]] std::vector<IntegrationJobSource> twoDatabases()
    {
        const QString outside = path(QStringLiteral("outside.db"));
        const QString project = path(QStringLiteral("project.db"));
        saveSession(outside, QStringLiteral("last_session"), 39, 0, path(QStringLiteral("out/last")));
        saveSession(outside, QStringLiteral("M 83 LRGB"), 120, 36, path(QStringLiteral("out/m83")));
        saveSession(project, QStringLiteral("Carina Nebula LRGB"), 700, 18, path(QStringLiteral("out/carina")));
        saveSession(project, QStringLiteral("Luminance only"), 226, 0, path(QStringLiteral("out/luminance")));
        leaveRecord(path(QStringLiteral("out/last")), WorkflowRunEnd::Cancelled,
                    QDateTime(QDate(2026, 10, 12), QTime(23, 3), QTimeZone::UTC), 61000, 0,
                    QStringLiteral("LightsRegistration"));
        leaveRecord(path(QStringLiteral("out/m83")), WorkflowRunEnd::Completed,
                    QDateTime(QDate(2026, 11, 2), QTime(22, 3), QTimeZone::UTC), 552000, 2);
        leaveRecord(path(QStringLiteral("out/carina")), WorkflowRunEnd::CompletedWithWarnings,
                    QDateTime(QDate(2026, 7, 27), QTime(23, 54), QTimeZone::UTC), 3060000, 4, QString(), 3);
        return {IntegrationJobSource{outside, QString()},
                IntegrationJobSource{project, QStringLiteral("Carina Nebula")}};
    }

    QTemporaryDir m_folder;
};

/// The cases of the jobs model.
class IntegrationJobsModel : public IntegrationJobsCase
{
};

} // namespace

// A row for each session of each database, with its project, its frames of every kind, the masters its last run left,
// when that run ended and how long it took.
TEST_F(IntegrationJobsModel, TheRowsAreTheSessionsOfEveryDatabase)
{
    const std::vector<IntegrationJob> jobs = ImageIntegration::readIntegrationJobs(twoDatabases());
    ASSERT_EQ(jobs.size(), 4u) << namesOf(jobs).join(QStringLiteral(", ")).toStdString();

    const IntegrationJob* const m83 = jobNamed(jobs, QStringLiteral("M 83 LRGB"));
    ASSERT_NE(m83, nullptr);
    EXPECT_TRUE(m83->project.isEmpty());
    EXPECT_EQ(m83->frames, 156);
    EXPECT_EQ(m83->masters, 2);
    EXPECT_EQ(m83->lastRun, QDateTime(QDate(2026, 11, 2), QTime(22, 3), QTimeZone::UTC));
    EXPECT_EQ(m83->durationMs, 552000);
    EXPECT_EQ(QDir::cleanPath(m83->outputDirectory), QDir::cleanPath(path(QStringLiteral("out/m83"))));
    EXPECT_TRUE(ImageIntegration::sameSessionDatabase(m83->databasePath, path(QStringLiteral("outside.db"))));

    const IntegrationJob* const carina = jobNamed(jobs, QStringLiteral("Carina Nebula LRGB"));
    ASSERT_NE(carina, nullptr);
    EXPECT_EQ(carina->project, QStringLiteral("Carina Nebula"));
    EXPECT_EQ(carina->frames, 718);
    EXPECT_EQ(carina->masters, 4);
    EXPECT_EQ(ImageIntegration::integrationJobDurationText(carina->durationMs), QStringLiteral("51m 0s"));

    const IntegrationJob* const never = jobNamed(jobs, QStringLiteral("Luminance only"));
    ASSERT_NE(never, nullptr);
    EXPECT_EQ(never->frames, 226);
    EXPECT_FALSE(never->lastRun.isValid());
    EXPECT_LT(never->durationMs, 0);
    EXPECT_EQ(ImageIntegration::integrationJobDurationText(never->durationMs), QStringLiteral("\u2014"));
}

// The status is what the last run left: completed, with warnings, cancelled at the stage it stopped at, and never run.
TEST_F(IntegrationJobsModel, TheStatusIsWhatTheLastRunLeft)
{
    const std::vector<IntegrationJob> jobs = ImageIntegration::readIntegrationJobs(twoDatabases());
    const auto statusOf = [&jobs](const QString& name) {
        const IntegrationJob* const job = jobNamed(jobs, name);
        return job != nullptr ? ImageIntegration::integrationJobStatusText(*job) : QString();
    };
    EXPECT_EQ(statusOf(QStringLiteral("M 83 LRGB")), QStringLiteral("Completed"));
    EXPECT_EQ(statusOf(QStringLiteral("Carina Nebula LRGB")), QStringLiteral("Completed with warnings"));
    EXPECT_EQ(statusOf(QStringLiteral("last_session")), QStringLiteral("Cancelled at Registration"));
    EXPECT_EQ(statusOf(QStringLiteral("Luminance only")), QStringLiteral("Not run"));
    ASSERT_NE(jobNamed(jobs, QStringLiteral("last_session")), nullptr);
    EXPECT_EQ(jobNamed(jobs, QStringLiteral("last_session"))->status, IntegrationJobStatus::Cancelled);

    // The other ends, as their tags say them.
    IntegrationJob job;
    job.status = IntegrationJobStatus::Failed;
    job.stage = QStringLiteral("LightsStacking");
    EXPECT_EQ(ImageIntegration::integrationJobStatusText(job), QStringLiteral("Failed at Stacking"));
    job.stage.clear();
    EXPECT_EQ(ImageIntegration::integrationJobStatusText(job), QStringLiteral("Failed"));
    job.status = IntegrationJobStatus::Partial;
    EXPECT_EQ(ImageIntegration::integrationJobStatusText(job), QStringLiteral("Partly completed"));
    job.status = IntegrationJobStatus::Running;
    EXPECT_EQ(ImageIntegration::integrationJobStatusText(job), QStringLiteral("Running"));
    job.runText = QStringLiteral("running");
    EXPECT_EQ(ImageIntegration::integrationJobStatusText(job), QStringLiteral("Running"))
        << "a run that says only that it runs added its word";
    job.runText = QStringLiteral("Registration");
    job.progress = 0.58;
    EXPECT_EQ(ImageIntegration::integrationJobStatusText(job), QStringLiteral("Running \u00b7 Registration 58 %"));
    EXPECT_EQ(ImageIntegration::integrationStageTitle(QStringLiteral("LightsCalibration")),
              QStringLiteral("Lights calibration"));
}

// The model reads off the GUI thread: asked, it has no rows yet and returns at once; the rows land on the GUI thread,
// the most recent run first and the jobs never run last.
TEST_F(IntegrationJobsModel, TheJobsAreReadOffTheGuiThreadAndListedByTheirLastRun)
{
    const std::vector<IntegrationJobSource> sources = twoDatabases();
    ImageIntegration::IntegrationJobsModel model;
    int changes = 0;
    QObject::connect(&model, &ImageIntegration::IntegrationJobsModel::jobsChanged, [&changes]() { ++changes; });
    model.setSources(sources);
    EXPECT_TRUE(model.isReading()) << "the jobs were read before the call returned";
    EXPECT_EQ(model.rowCount(), 0);
    ASSERT_TRUE(waitForTheRead(model));
    EXPECT_GE(changes, 1);
    ASSERT_EQ(model.rowCount(), 4);
    EXPECT_EQ(model.columnCount(), ImageIntegration::IntegrationJobsModel::ColumnCount);
    EXPECT_EQ(namesOf(model.jobs()),
              (QStringList{QStringLiteral("M 83 LRGB"), QStringLiteral("last_session"),
                           QStringLiteral("Carina Nebula LRGB"), QStringLiteral("Luminance only")}));
    EXPECT_EQ(model.data(model.index(0, ImageIntegration::IntegrationJobsModel::FramesColumn)).toInt(), 156);
    EXPECT_EQ(model.data(model.index(0, ImageIntegration::IntegrationJobsModel::StatusColumn)).toString(),
              QStringLiteral("Completed"));
    EXPECT_EQ(model.data(model.index(2, ImageIntegration::IntegrationJobsModel::ProjectColumn)).toString(),
              QStringLiteral("Carina Nebula"));
    EXPECT_EQ(model.data(model.index(0, ImageIntegration::IntegrationJobsModel::ProjectColumn)).toString(),
              QStringLiteral("No project"));
    EXPECT_EQ(model.data(model.index(2, ImageIntegration::IntegrationJobsModel::TookColumn)).toString(),
              QStringLiteral("51m 0s"));
}

// The job an open Image Integration runs is shown running, with what its run reports, and first; when it stops running
// it goes back to its place.
TEST_F(IntegrationJobsModel, ARunningJobIsFirst)
{
    const std::vector<IntegrationJobSource> sources = twoDatabases();
    ImageIntegration::IntegrationJobsModel model;
    model.setSources(sources);
    ASSERT_TRUE(waitForTheRead(model));
    ASSERT_EQ(model.rowCount(), 4);

    model.setRunningJobs({RunningIntegrationJob{sources[1].databasePath, QStringLiteral("Luminance only"),
                                                QStringLiteral("Registration"), 0.58}});
    EXPECT_EQ(model.runningCount(), 1);
    ASSERT_FALSE(model.jobs().empty());
    EXPECT_EQ(model.jobs().front().name, QStringLiteral("Luminance only"));
    EXPECT_EQ(model.jobs().front().status, IntegrationJobStatus::Running);
    EXPECT_EQ(model.data(model.index(0, ImageIntegration::IntegrationJobsModel::StatusColumn)).toString(),
              QStringLiteral("Running \u00b7 Registration 58 %"));
    EXPECT_DOUBLE_EQ(model.data(model.index(0, 0), ImageIntegration::IntegrationJobsModel::ProgressRole).toDouble(),
                     0.58);

    // A job of the same name in another database is not the one that runs.
    model.setRunningJobs({RunningIntegrationJob{sources[0].databasePath, QStringLiteral("Luminance only"), {}, {}}});
    EXPECT_EQ(model.runningCount(), 0);
    EXPECT_EQ(model.jobs().back().name, QStringLiteral("Luminance only"));
    EXPECT_EQ(model.jobs().back().status, IntegrationJobStatus::NotRun);

    model.setRunningJobs({});
    EXPECT_EQ(namesOf(model.jobs()).first(), QStringLiteral("M 83 LRGB"));
}

// A database that is not there gives no jobs, and is not made by being read.
TEST_F(IntegrationJobsModel, ADatabaseThatIsNotThereGivesNoJobsAndIsNotMade)
{
    const QString missing = path(QStringLiteral("nowhere/sessions.db"));
    const std::vector<IntegrationJob> jobs =
        ImageIntegration::readIntegrationJobs({IntegrationJobSource{missing, QString()}});
    EXPECT_TRUE(jobs.empty());
    EXPECT_FALSE(QFileInfo::exists(missing));
    EXPECT_FALSE(QFileInfo::exists(path(QStringLiteral("nowhere"))));
}

// The database outside any project is the one Image Integration keeps in the application's local data.
TEST_F(IntegrationJobsModel, TheDatabaseOutsideAnyProjectIsTheApplications)
{
    const QFileInfo database(ImageIntegration::applicationSessionDatabasePath());
    EXPECT_EQ(database.fileName(), QString::fromLatin1(ImageIntegration::kSessionDatabaseFileName));
    EXPECT_FALSE(database.path().isEmpty());
}
