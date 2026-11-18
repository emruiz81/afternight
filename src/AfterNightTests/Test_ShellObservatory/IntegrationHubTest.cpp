// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Integration hub in the Observatory window as a person uses it: the rail's Integration place and the View menu's
// Integration show it; the three ways to stack, the guided one shown as planned and disabled, the other two launching
// Image Integration and its Lite front end; the four single steps launching their processes; the integration jobs -
// the sessions Image Integration keeps, outside any project and in the open project's folder - with their status, the
// running one first, searched and shown for a project; Open on a job opening Image Integration on that session, and
// Show on the running one bringing its task forward; and a view a process makes landing in a workspace without taking
// the person off the hub.

#include "ObservatoryWindowCase.h"
#include "ProjectCase.h"

#include "ShellActions.h"
#include "pages/IntegrationHubPage.h"
#include "tasks/TaskPresenter.h"
#include "workspaces/WorkspaceHost.h"

#include "actions/ActionCatalogue.h"
#include "commands/CommandIndex.h"
#include "components/AnBreadcrumb.h"
#include "components/AnIconRail.h"
#include "components/AnSnackbar.h"
#include "components/AnTag.h"
#include "components/RailItem.h"
#include "chrome/MenuBarOnAlt.h"
#include "chrome/TopBar.h"
#include "controls/ImageViewControl.h"
#include "controls/IAppContext.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"
#include "projects/ProjectService.h"
#include "services/OpenProcessRegistry.h"
#include "tasks/TaskModel.h"
#include "views/ViewService.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QLineEdit>
#include <QMenu>
#include <QSize>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::observatory::IntegrationHubPage;
using Window = an::ui::observatory::ObservatoryWindow;

constexpr int kOpenMs = 20000;
constexpr int kReadMs = 10000;

/// The Integration hub of a started window, over sessions a case writes into the session database Image Integration
/// keeps outside any project, each removed again when the case ends.
class IntegrationHub : public ProjectCase
{
protected:
    void SetUp() override
    {
        ProjectCase::SetUp();
        forgetProjects();
    }

    void TearDown() override
    {
        // No case leaves a project open. Image Integration saves the session it shows as it closes: its window goes
        // before the sessions are removed.
        (void)projects().close();
        forgetProjects();
        hub().jobSearch()->clear();
        hub().jobProjects()->setCurrentIndex(0);
        ProjectCase::TearDown();
        if (!m_sessions.empty())
        {
            IntegrationDatabase database(ImageIntegration::applicationSessionDatabasePath());
            for (const QString& name : m_sessions)
            {
                (void)database.deleteSession(name);
            }
            m_sessions.clear();
        }
        window().integrationJobs().refresh();
        (void)waitForTheJobs();
    }

    [[nodiscard]] static IntegrationHubPage& hub()
    {
        return *window().integrationHubPage();
    }

    /// A folder for the case's output folders, made once the suite's temporary directory is in force.
    [[nodiscard]] static QString outputs()
    {
        static QTemporaryDir folder(QDir::temp().filePath(QStringLiteral("observatory-hub-XXXXXX")));
        return folder.path();
    }

    /// Saves a session of `lights` light frames, whose runs write into a folder of its own, into a database; the one
    /// outside any project unless another is named. Its output folder.
    QString saveSession(const QString& name, const int lights, const QString& database = QString())
    {
        const QString output = QDir(outputs()).filePath(QString(name).replace(QLatin1Char(' '), QLatin1Char('_')));
        EXPECT_TRUE(QDir().mkpath(output));
        IntegrationDataModel model;
        std::vector<FrameInfo> frames;
        for (int index = 0; index < lights; ++index)
        {
            const QString file = QDir(output).filePath(QStringLiteral("light_%1.fit").arg(index));
            frames.emplace_back(FrameId(file), file,
                                FrameMetadata(FrameType::Light, QSize(64, 48), 120.0, -10.0, 800, 1,
                                              QStringLiteral("L"), QStringLiteral("M 42"),
                                              QDateTime::currentDateTimeUtc(), QString()));
        }
        model.addFrames(FrameType::Light, frames);
        model.setOutputDirectory(output);
        const QString path = database.isEmpty() ? ImageIntegration::applicationSessionDatabasePath() : database;
        IntegrationDatabase store(path);
        EXPECT_TRUE(store.saveSession(name, model.toJson())) << store.lastError().toStdString();
        if (database.isEmpty())
        {
            m_sessions.append(name);
        }
        return output;
    }

    /// Leaves the record of a run in an output folder.
    static void leaveRecord(const QString& output, const WorkflowRunEnd end, const QDateTime& ended,
                            const QString& stage = QString())
    {
        WorkflowRunRecord record;
        record.ended = ended;
        record.durationMs = 552000;
        record.end = end;
        record.stage = stage;
        record.masters = 2;
        ASSERT_TRUE(WorkflowRunStateStore(output).saveRunRecord(record));
    }

    /// Reads the jobs again, and waits until the read has landed.
    static bool readTheJobs()
    {
        window().integrationJobs().refresh();
        return waitForTheJobs();
    }

    static bool waitForTheJobs()
    {
        return an::tests::app::processEventsUntil([]() { return !window().integrationJobs().isReading(); }, kReadMs);
    }

    /// Shows the hub as the rail's Integration does.
    static void showTheHub()
    {
        const QRect place = window().rail()->itemRect(QString::fromLatin1(Window::kRailIntegration));
        ASSERT_TRUE(place.isValid()) << "the rail shows no Integration";
        QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, place.center());
        QCoreApplication::processEvents();
    }

    /// The open process a descriptor launched; null for none.
    [[nodiscard]] static TopLevelProcessBaseControl* openProcess(const char* descriptorId)
    {
        for (TopLevelProcessBaseControl* process : core().openProcesses().snapshot())
        {
            if (process != nullptr && process->descriptorId() == descriptorId)
            {
                return process;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static ImageIntegrationProcess* openIntegration()
    {
        return dynamic_cast<ImageIntegrationProcess*>(openProcess(IntegrationHubPage::kImageIntegration));
    }

    [[nodiscard]] static bool waitForProcess(const char* descriptorId)
    {
        return an::tests::app::processEventsUntil([descriptorId]() { return openProcess(descriptorId) != nullptr; },
                                                  kOpenMs);
    }

    [[nodiscard]] static QString applicationDatabase()
    {
        return ImageIntegration::applicationSessionDatabasePath();
    }

    QStringList m_sessions;
};

} // namespace

// The rail's Integration shows the hub: the place is selected, the breadcrumb says Integration and the column is
// hidden; Home is shown again from the rail.
TEST_F(IntegrationHub, TheRailsIntegrationPlaceShowsTheHub)
{
    ASSERT_NE(window().integrationHubPage(), nullptr);
    showTheHub();
    EXPECT_TRUE(window().isIntegrationHubShown());
    EXPECT_EQ(window().centre()->currentWidget(), window().integrationHubPage());
    EXPECT_FALSE(window().isHomeShown());
    EXPECT_EQ(window().topBar()->breadcrumb()->parts(), QStringList{QStringLiteral("Integration")});
    EXPECT_FALSE(window().rightColumn()->isVisible());
    const std::vector<an::ui::RailItem>& items = window().rail()->items();
    const auto integration = std::find_if(items.begin(), items.end(), [](const an::ui::RailItem& item) {
        return item.id == QLatin1String(Window::kRailIntegration);
    });
    ASSERT_NE(integration, items.end());
    EXPECT_TRUE(integration->selected);

    window().showHome();
    EXPECT_FALSE(window().isIntegrationHubShown());
}

// Integration is the shell's own action, in the View menu after Home and in the palette; it shows the hub.
TEST_F(IntegrationHub, ShowIntegrationHubIsTheShellsActionInTheViewMenuAndThePalette)
{
    QAction* const action = window().shellActions().action(an::ui::observatory::ids::kShowIntegrationHub);
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->text(), QStringLiteral("Integration"));
    EXPECT_EQ(action->parent(), &window());
    EXPECT_EQ(core().actions().action("actionShow_Integration_Hub"), nullptr) << "it is in the catalogue's base table";
    QMenu* const view = window().menuBarOnAlt().menu("menuView_2");
    ASSERT_NE(view, nullptr);
    const QList<QAction*> entries = view->actions();
    const qsizetype home = entries.indexOf(window().shellActions().action(an::ui::observatory::ids::kShowHome));
    ASSERT_GE(home, 0);
    EXPECT_EQ(entries.value(home + 1), action);

    bool offered = false;
    core().commands().refresh();
    for (const an::ui::shell::CommandEntry& entry : core().commands().entries())
    {
        offered = offered || (entry.id == QStringLiteral("action:actionShow_Integration_Hub") &&
                              entry.title == QStringLiteral("Integration"));
    }
    EXPECT_TRUE(offered) << "the palette does not offer Integration";

    action->trigger();
    QCoreApplication::processEvents();
    EXPECT_TRUE(window().isIntegrationHubShown());
}

// The three ways: the guided integration is shown as planned, its Start disabled; New integration and New quick
// integration launch Image Integration and Image Integration Lite.
TEST_F(IntegrationHub, ThreeWaysTheGuidedOneDisabledWithItsTag)
{
    showTheHub();
    ASSERT_NE(hub().guidedStart(), nullptr);
    EXPECT_FALSE(hub().guidedStart()->isEnabled());
    EXPECT_TRUE(hub().guidedStart()->isVisible());
    ASSERT_NE(hub().guidedTag(), nullptr);
    EXPECT_EQ(hub().guidedTag()->text(), QStringLiteral("Planned"));
    EXPECT_EQ(hub().newIntegration()->text(), QStringLiteral("New integration"));
    EXPECT_EQ(hub().newQuickIntegration()->text(), QStringLiteral("New quick integration"));

    QTest::mouseClick(hub().newIntegration(), Qt::LeftButton);
    EXPECT_TRUE(waitForProcess(IntegrationHubPage::kImageIntegration)) << "New integration opened no Image Integration";
    QTest::mouseClick(hub().newQuickIntegration(), Qt::LeftButton);
    EXPECT_TRUE(waitForProcess(IntegrationHubPage::kImageIntegrationLite))
        << "New quick integration opened no Image Integration Lite";
}

// The four single steps, in their order, each launching its process.
TEST_F(IntegrationHub, FourSingleStepsLaunchTheirDescriptors)
{
    showTheHub();
    const std::vector<QAbstractButton*> steps = hub().steps();
    ASSERT_EQ(steps.size(), 4u);
    const std::vector<std::pair<QString, const char*>> expected = {
        {QStringLiteral("Image Calibration"), IntegrationHubPage::kImageCalibration},
        {QStringLiteral("Image Registration"), IntegrationHubPage::kImageRegistration},
        {QStringLiteral("Image Stacking"), IntegrationHubPage::kImageStacking},
        {QStringLiteral("Image List Analyzer"), IntegrationHubPage::kImageListAnalyzer}};
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        SCOPED_TRACE(expected[index].first.toStdString());
        EXPECT_EQ(steps[index]->accessibleName(), expected[index].first);
        QTest::mouseClick(steps[index], Qt::LeftButton);
        EXPECT_TRUE(waitForProcess(expected[index].second)) << "the step launched nothing";
        if (TopLevelProcessBaseControl* const process = openProcess(expected[index].second))
        {
            (void)process->close();
            QCoreApplication::processEvents();
        }
    }
}

// The jobs are the sessions Image Integration keeps, each with its status from its last run; the count says how many.
TEST_F(IntegrationHub, TheJobsAreTheSessionsWithTheStatusTheirLastRunLeft)
{
    const QString orion = saveSession(QStringLiteral("Hub Orion"), 12);
    const QString carina = saveSession(QStringLiteral("Hub Carina"), 30);
    leaveRecord(orion, WorkflowRunEnd::Completed, QDateTime(QDate(2026, 11, 2), QTime(22, 3), QTimeZone::UTC));
    leaveRecord(carina, WorkflowRunEnd::Cancelled, QDateTime(QDate(2026, 10, 12), QTime(23, 3), QTimeZone::UTC),
                QStringLiteral("LightsRegistration"));
    showTheHub();
    ASSERT_TRUE(readTheJobs());

    const QStringList shown = hub().shownJobs();
    ASSERT_TRUE(shown.contains(QStringLiteral("Hub Orion"))) << shown.join(QStringLiteral(", ")).toStdString();
    ASSERT_TRUE(shown.contains(QStringLiteral("Hub Carina")));
    EXPECT_LT(shown.indexOf(QStringLiteral("Hub Orion")), shown.indexOf(QStringLiteral("Hub Carina")))
        << "the most recent run is not first";
    EXPECT_EQ(hub().jobStatusText(applicationDatabase(), QStringLiteral("Hub Orion")), QStringLiteral("Completed"));
    EXPECT_EQ(hub().jobStatusText(applicationDatabase(), QStringLiteral("Hub Carina")),
              QStringLiteral("Cancelled at Registration"));
    EXPECT_EQ(hub().jobsCountText(), QString::number(shown.size()));
    ASSERT_NE(hub().jobButton(applicationDatabase(), QStringLiteral("Hub Orion")), nullptr);
    EXPECT_EQ(hub().jobButton(applicationDatabase(), QStringLiteral("Hub Orion"))->text(), QStringLiteral("Open"));
}

// Open on a job opens Image Integration on that job's session; Open on another switches it to that one. The job it
// runs is first, reads Running, and its Show brings the task forward.
TEST_F(IntegrationHub, OpenOnAJobOpensImageIntegrationOnThatSession)
{
    (void)saveSession(QStringLiteral("Hub Vela"), 7);
    (void)saveSession(QStringLiteral("Hub Pleiades"), 3);
    showTheHub();
    ASSERT_TRUE(readTheJobs());
    QAbstractButton* const open = hub().jobButton(applicationDatabase(), QStringLiteral("Hub Vela"));
    ASSERT_NE(open, nullptr);
    QTest::mouseClick(open, Qt::LeftButton);
    ASSERT_TRUE(waitForProcess(IntegrationHubPage::kImageIntegration)) << "Open opened no Image Integration";
    ImageIntegrationProcess* const process = openIntegration();
    ASSERT_NE(process, nullptr);
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [process]() { return process->sessionName() == QStringLiteral("Hub Vela"); }, kOpenMs))
        << "Image Integration opened on " << process->sessionName().toStdString();
    EXPECT_TRUE(ImageIntegration::sameSessionDatabase(process->sessionDatabasePath(), applicationDatabase()));
    EXPECT_EQ(process->model()->lightFrames().size(), 7u);

    QAbstractButton* const other = hub().jobButton(applicationDatabase(), QStringLiteral("Hub Pleiades"));
    ASSERT_NE(other, nullptr);
    QTest::mouseClick(other, Qt::LeftButton);
    EXPECT_TRUE(an::tests::app::processEventsUntil(
        [process]() { return process->sessionName() == QStringLiteral("Hub Pleiades"); }, kOpenMs));
    EXPECT_EQ(process->model()->lightFrames().size(), 3u);

    // Its run: the job is first, running, and Show brings its task forward.
    process->setRunState(an::ui::runningState(QStringLiteral("Registration"), 0.5));
    QCoreApplication::processEvents();
    ASSERT_FALSE(hub().shownJobs().isEmpty());
    EXPECT_EQ(hub().shownJobs().first(), QStringLiteral("Hub Pleiades"));
    EXPECT_EQ(hub().jobStatusText(applicationDatabase(), QStringLiteral("Hub Pleiades")),
              QStringLiteral("Running · Registration 50 %"));
    EXPECT_TRUE(hub().jobsCountText().endsWith(QStringLiteral("1 running"))) << hub().jobsCountText().toStdString();
    QAbstractButton* const show = hub().jobButton(applicationDatabase(), QStringLiteral("Hub Pleiades"));
    ASSERT_NE(show, nullptr);
    EXPECT_EQ(show->text(), QStringLiteral("Show"));
    const an::ui::shell::Task* const task = window().taskPresenter()->model().taskFor(process);
    ASSERT_NE(task, nullptr);
    const an::ui::shell::TaskId id = task->id;
    window().showHome();
    showTheHub();
    QTest::mouseClick(hub().jobButton(applicationDatabase(), QStringLiteral("Hub Pleiades")), Qt::LeftButton);
    QCoreApplication::processEvents();
    EXPECT_EQ(window().taskPresenter()->model().current(), id) << "Show did not bring the task forward";

    process->setRunState(an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Cancelled});
    QCoreApplication::processEvents();
    EXPECT_NE(hub().jobStatusText(applicationDatabase(), QStringLiteral("Hub Pleiades")).left(7),
              QStringLiteral("Running"));
}

// Under the jobs' table a line says that a job run without a project makes one in its output folder, while the
// settings ask for it, and is not shown when they do not.
TEST_F(IntegrationHub, SaysThatAJobRunWithoutAProjectMakesOneWhileTheSettingsAsk)
{
    an::ui::IGlobalSettingsProvider* const settings = core().globalSettings();
    const QString key = QString::fromLatin1(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS);
    (void)settings->upsert(key, QStringLiteral("1"));
    showTheHub();
    EXPECT_TRUE(hub().jobsProjectLine().contains(QStringLiteral("creates one in its output folder")))
        << hub().jobsProjectLine().toStdString();
    const auto* const line = hub().findChild<QWidget*>(QStringLiteral("integrationJobsProjectLine"));
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(line->isVisible());

    (void)settings->upsert(key, QStringLiteral("0"));
    window().showHome();
    showTheHub();
    EXPECT_TRUE(hub().jobsProjectLine().isEmpty());
    EXPECT_FALSE(line->isVisible());
    (void)settings->upsert(key, QStringLiteral("1"));
}

// The jobs are searched by name or project, and shown for every project, for none, or for one: the open project's
// sessions are listed under its name.
TEST_F(IntegrationHub, TheJobsAreSearchedAndShownForAProject)
{
    (void)saveSession(QStringLiteral("Hub Andromeda"), 4);
    an::ui::shell::ProjectCreateRequest request;
    request.name = QStringLiteral("Hub project");
    request.location = root();
    ASSERT_TRUE(projects().create(request).ok());
    const QString projectDatabase =
        QDir(core().projects().currentFolder())
            .filePath(QStringLiteral("integration/") + QString::fromLatin1(ImageIntegration::kSessionDatabaseFileName));
    (void)saveSession(QStringLiteral("Hub Rosette"), 5, projectDatabase);
    showTheHub();
    ASSERT_TRUE(readTheJobs());
    EXPECT_TRUE(hub().shownJobs().contains(QStringLiteral("Hub Andromeda")));
    ASSERT_TRUE(hub().shownJobs().contains(QStringLiteral("Hub Rosette"))) << "the open project's session is missing";

    hub().jobSearch()->setText(QStringLiteral("rosette"));
    EXPECT_EQ(hub().shownJobs(), QStringList{QStringLiteral("Hub Rosette")});
    hub().jobSearch()->setText(QStringLiteral("hub proj"));
    EXPECT_EQ(hub().shownJobs(), QStringList{QStringLiteral("Hub Rosette")}) << "a search by project found nothing";
    hub().jobSearch()->clear();

    QComboBox* const projects = hub().jobProjects();
    ASSERT_GE(projects->findText(QStringLiteral("All projects")), 0);
    ASSERT_GE(projects->findText(QStringLiteral("No project")), 0);
    const int project = projects->findText(QStringLiteral("Hub project"));
    ASSERT_GE(project, 0);
    projects->setCurrentIndex(project);
    EXPECT_EQ(hub().shownJobs(), QStringList{QStringLiteral("Hub Rosette")});
    projects->setCurrentIndex(projects->findText(QStringLiteral("No project")));
    EXPECT_TRUE(hub().shownJobs().contains(QStringLiteral("Hub Andromeda")));
    EXPECT_FALSE(hub().shownJobs().contains(QStringLiteral("Hub Rosette")));
    projects->setCurrentIndex(projects->findText(QStringLiteral("All projects")));

    // The project closed, its sessions are no jobs of the hub's any more.
    ASSERT_TRUE(core().projects().close());
    ASSERT_TRUE(waitForTheJobs());
    EXPECT_FALSE(hub().shownJobs().contains(QStringLiteral("Hub Rosette")));
}

// A view a process makes while the hub is shown lands in a workspace; the person stays on the hub, and the snackbar's
// Show goes to the image.
TEST_F(IntegrationHub, AViewAProcessMakesDoesNotTakeThePersonOffTheHub)
{
    showTheHub();
    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(session(), QStringLiteral("hub_master"), 0.5f);
    ASSERT_TRUE(document.has_value());
    ImageViewControl* const made = core().viewService().createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "hub_master", ViewControlMode::standard, nullptr, false, false);
    ASSERT_NE(made, nullptr);
    QCoreApplication::processEvents();
    EXPECT_TRUE(window().isIntegrationHubShown()) << "the person was taken off the hub";
    EXPECT_FALSE(window().workspaces().workspaceOf(*made).isEmpty()) << "the image is in no workspace";
    an::ui::AnSnackbar* const snackbar = window().snackbar();
    ASSERT_TRUE(snackbar->isVisible());
    EXPECT_TRUE(snackbar->text().startsWith(QStringLiteral("hub_master opened in "))) << snackbar->text().toStdString();
    snackbar->actionButton()->click();
    QCoreApplication::processEvents();
    EXPECT_TRUE(window().isWorkspaceShown());
}

} // namespace an::tests::observatory
