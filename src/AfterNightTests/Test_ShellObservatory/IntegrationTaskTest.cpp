// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A run of Image Integration in the Observatory window, as a person meets it: the run is shown in the task's page and
/// no window opens over the application; the task says the job it holds; its rail item carries the run's arc - how far
/// the whole run has come - and its tooltip the step the run is at; leaving the page leaves the run running, and
/// coming back shows it where it is; and a run that ends while another place is shown is said in the window's
/// transient message, which offers the way to it. The runs are the suite's fixture pipeline's, which a case lets go on
/// one report at a time.

#include "ObservatoryWindowCase.h"

#include "Test_ImageIntegration/workflow/FixturePipeline.h"

#include "pages/IntegrationHubPage.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"

#include "components/AnIconRail.h"
#include "components/AnSnackbar.h"
#include "components/AnStageList.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/ApplicationCore.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "processes/ImageIntegration/widgets/WorkflowRunView.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStanding.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"

#include <QAbstractButton>
#include <QDialog>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>

namespace an::tests::observatory
{
namespace
{

using an::tests::integration::fixtureManifest;
using an::tests::integration::fixturePipeline;
using an::tests::integration::RunGate;
using an::ui::observatory::IntegrationHubPage;
using an::ui::shell::Task;
using an::ui::shell::TaskId;

constexpr int kWaitMs = 20000;

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The cases of a run of Image Integration as a task: each launches it as its menu entry does, on the session it keeps
/// outside any project, emptied before and after, and runs the fixture pipeline from it.
class IntegrationTask : public ObservatoryWindowCase
{
protected:
    void SetUp() override
    {
        ObservatoryWindowCase::SetUp();
        forgetTheSession();
    }

    void TearDown() override
    {
        // A run still on its way goes to its end before its page closes.
        if (m_gate)
        {
            m_gate->open();
        }
        if (!m_process.isNull())
        {
            (void)an::tests::app::processEventsUntil([this]() { return !m_process->runState().running(); }, kWaitMs);
        }
        ObservatoryWindowCase::TearDown();
        settle();
        forgetTheSession();
        window().integrationJobs().refresh();
        (void)an::tests::app::processEventsUntil([]() { return !window().integrationJobs().isReading(); }, kWaitMs);
    }

    static void forgetTheSession()
    {
        IntegrationDatabase database(ImageIntegration::applicationSessionDatabasePath());
        if (database.initialize())
        {
            (void)database.deleteSession(QStringLiteral("last_session"));
        }
    }

    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        return *window().taskPresenter();
    }

    /// Saves the session a launched Image Integration opens on, as a job of no frame: the hub lists it.
    void saveTheSession()
    {
        IntegrationDataModel model;
        model.setOutputDirectory(m_output.path());
        IntegrationDatabase store(ImageIntegration::applicationSessionDatabasePath());
        EXPECT_TRUE(store.saveSession(QStringLiteral("last_session"), model.toJson()))
            << store.lastError().toStdString();
    }

    /// Launches Image Integration as its menu entry does and starts the fixture pipeline from it.
    ImageIntegrationProcess* launchAndRun()
    {
        EXPECT_TRUE(core().processLaunch().launch(IntegrationHubPage::kImageIntegration));
        settle();
        for (TopLevelProcessBaseControl* process : core().openProcesses().snapshot())
        {
            if (auto* const integration = dynamic_cast<ImageIntegrationProcess*>(process))
            {
                m_process = integration;
            }
        }
        if (m_process.isNull())
        {
            ADD_FAILURE() << "Image Integration did not open";
            return nullptr;
        }
        m_gate = std::make_shared<RunGate>();
        EXPECT_TRUE(m_output.isValid());
        EXPECT_TRUE(m_process->startRun(fixtureManifest(m_output.path()), fixturePipeline(m_gate)));
        settle();
        return m_process.data();
    }

    [[nodiscard]] TaskId taskId() const
    {
        const Task* const task = tasks().model().taskFor(m_process.data());
        return task != nullptr ? task->id : 0u;
    }

    [[nodiscard]] an::ui::RailItem railItem() const
    {
        const QString id = Window::railTaskId(taskId());
        for (const an::ui::RailItem& item : window().rail()->items())
        {
            if (item.id == id)
            {
                return item;
            }
        }
        return an::ui::RailItem{};
    }

    /// Waits for the run to reach a step of its stages; whether it did.
    [[nodiscard]] bool reaches(const int step) const
    {
        return an::tests::app::processEventsUntil(
            [this, step]() { return m_process->runView() != nullptr && m_process->runView()->standing().step == step; },
            kWaitMs);
    }

    [[nodiscard]] bool ends() const
    {
        return an::tests::app::processEventsUntil([this]() { return !m_process->runState().running(); }, kWaitMs);
    }

    using Window = an::ui::observatory::ObservatoryWindow;

    QTemporaryDir m_output;
    std::shared_ptr<RunGate> m_gate;
    QPointer<ImageIntegrationProcess> m_process;
};

} // namespace

// The run is shown in the task's page - no window opens over the application - and the task's rail item follows it:
// the arc is how far the whole run has come, and the tooltip says the process, the job and the step the run is at.
TEST_F(IntegrationTask, RailFollowsTheRun)
{
    ImageIntegrationProcess* const process = launchAndRun();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(window().isTaskPageShown());
    ASSERT_EQ(window().shownTask(), taskId());
    WorkflowRunView* const view = process->runView();
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(window().taskPage(taskId())->isAncestorOf(view));
    EXPECT_TRUE(view->isVisible());
    EXPECT_EQ(process->findChild<WorkflowProgressDialogV2*>(), nullptr) << "no Processing window on a page";

    // The task is named by its job.
    const Task* const task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->targetName, process->sessionName());
    EXPECT_EQ(railItem().text, QStringLiteral("Image Integration - last_session"));
    EXPECT_EQ(railItem().runPhase, an::ui::RailRunPhase::Running);

    m_gate->allow(1);
    ASSERT_TRUE(reaches(1));
    settle();
    ASSERT_TRUE(railItem().progress.has_value());
    EXPECT_EQ(*railItem().progress, 25) << "half of the first of two stages";
    EXPECT_EQ(window().rail()->itemToolTip(Window::railTaskId(taskId())),
              QStringLiteral("Image Integration - last_session - Step 1/2: Bias 25 %"));

    m_gate->allow(2);
    ASSERT_TRUE(reaches(2));
    settle();
    EXPECT_EQ(*railItem().progress, 63);
    EXPECT_EQ(window().rail()->itemToolTip(Window::railTaskId(taskId())),
              QStringLiteral("Image Integration - last_session - Step 2/2: Registration 63 %"));
    // The task's header says the same, and its bar is the run's.
    const an::ui::AnTaskHeader* const header = window().taskPage(taskId())->header();
    EXPECT_EQ(header->state(), an::ui::AnTaskHeader::State::Running);

    m_gate->open();
    ASSERT_TRUE(ends());
    settle();
    EXPECT_EQ(process->runState().phase, an::ui::ProcessRunState::Phase::Succeeded);
    EXPECT_NE(railItem().runPhase, an::ui::RailRunPhase::Running);
    EXPECT_FALSE(railItem().progress.has_value());
    EXPECT_TRUE(view->isFinished());
}

// Leaving the page leaves the run running: on another place the run goes on and the rail still follows it; coming back
// to the task shows the run where it is.
TEST_F(IntegrationTask, RunContinuesWhenThePageIsLeft)
{
    saveTheSession();
    ImageIntegrationProcess* const process = launchAndRun();
    ASSERT_NE(process, nullptr);
    const TaskId id = taskId();
    m_gate->allow(1);
    ASSERT_TRUE(reaches(1));

    window().showIntegrationHub();
    settle();
    ASSERT_TRUE(window().isIntegrationHubShown());
    ASSERT_FALSE(window().isTaskPageShown());
    EXPECT_TRUE(process->runState().running()) << "the run is not the page's to stop";

    // It goes on while another place is shown.
    m_gate->allow(2);
    ASSERT_TRUE(reaches(2));
    settle();
    EXPECT_TRUE(window().isIntegrationHubShown()) << "a run that moves on takes the person nowhere";
    EXPECT_EQ(process->runState().text, QStringLiteral("Step 2/2: Registration"));
    EXPECT_EQ(*railItem().progress, 63);

    // The hub's row of the job says the same.
    window().integrationJobs().refresh();
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        []() {
            for (const ImageIntegration::IntegrationJob& job : window().integrationJobs().jobs())
            {
                if (job.status == ImageIntegration::IntegrationJobStatus::Running)
                {
                    return ImageIntegration::integrationJobStatusText(job).contains(
                        QStringLiteral("Step 2/2: Registration"));
                }
            }
            return false;
        },
        kWaitMs));

    // Coming back shows the run where it is.
    ASSERT_TRUE(tasks().show(id));
    settle();
    ASSERT_TRUE(window().isTaskPageShown());
    WorkflowRunView* const view = process->runView();
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->isVisible());
    EXPECT_FALSE(view->isFinished());
    EXPECT_EQ(view->standing().step, 2);
    const std::vector<an::ui::StageRow> rows = view->stageRows();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].status, an::ui::StageStatus::Done);
    EXPECT_EQ(rows[1].status, an::ui::StageStatus::Running);

    m_gate->open();
    ASSERT_TRUE(ends());
    settle();
    EXPECT_TRUE(view->isFinished());
    EXPECT_FALSE(window().snackbar()->isVisible()) << "a run that ends in front of the person needs no message";
}

// A run that ends while another place is shown is said in the window's transient message, with the job's name and
// the way to the task; the page then shows how the run ended.
TEST_F(IntegrationTask, TheEndOfARunIsSaidWhereAnotherPlaceIsShown)
{
    ImageIntegrationProcess* const process = launchAndRun();
    ASSERT_NE(process, nullptr);
    const TaskId id = taskId();
    m_gate->allow(1);
    ASSERT_TRUE(reaches(1));
    window().showIntegrationHub();
    settle();
    ASSERT_TRUE(window().isIntegrationHubShown());

    m_gate->open();
    ASSERT_TRUE(ends());
    settle();
    an::ui::AnSnackbar* const snackbar = window().snackbar();
    ASSERT_TRUE(snackbar->isVisible());
    EXPECT_EQ(snackbar->text(), QStringLiteral("Image Integration finished"));
    EXPECT_EQ(snackbar->detail(), QStringLiteral("last_session"));
    EXPECT_EQ(snackbar->actionText(), QStringLiteral("Show"));
    EXPECT_TRUE(window().isIntegrationHubShown()) << "the end of a run takes the person nowhere";

    QAbstractButton* const show = snackbar->actionButton();
    ASSERT_NE(show, nullptr);
    show->click();
    settle();
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().shownTask(), id);
    ASSERT_NE(process->runView(), nullptr);
    EXPECT_TRUE(process->runView()->isVisible());
    EXPECT_TRUE(process->runView()->wasSuccessful());
}

// A run stopped from the page ends as stopped, and the task can be closed again.
TEST_F(IntegrationTask, ARunStoppedFromThePageEndsAndTheTaskCloses)
{
    ImageIntegrationProcess* const process = launchAndRun();
    ASSERT_NE(process, nullptr);
    const TaskId id = taskId();
    m_gate->allow(1);
    ASSERT_TRUE(reaches(1));
    EXPECT_FALSE(tasks().canClose(id)) << "a task is not closed under its run";

    auto* const cancel = process->runView()->findChild<QPushButton*>(QStringLiteral("btnCancel"));
    ASSERT_NE(cancel, nullptr);
    cancel->click();
    ASSERT_TRUE(ends());
    settle();
    EXPECT_EQ(process->runState().phase, an::ui::ProcessRunState::Phase::Cancelled);
    EXPECT_TRUE(process->runView()->wasCancelled());
    EXPECT_TRUE(tasks().canClose(id));
}

} // namespace an::tests::observatory
