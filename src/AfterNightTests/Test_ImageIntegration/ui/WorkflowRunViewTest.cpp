// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A run as Image Integration shows it: the same view in the Processing window and in the page - the run's stages as a
/// list whose stage on its way carries the progress and its parts, what the run does, its log, its times, Cancel, and
/// Open Report and Close once it has ended. In a window of its own a run opens the Processing window over the
/// application, as it always has; as a page the run is shown in the page and no window opens: the page's own acts wait
/// for it, its stages can still be looked at, the header's primary act is the way back to the progress, and the
/// process's run state says the step the run is at and how far the whole has come.

#include "Test_ImageIntegration/ui/IntegrationPageCase.h"
#include "Test_ImageIntegration/workflow/FixturePipeline.h"

#include "components/AnStageList.h"
#include "components/AnTaskHeader.h"
#include "controls/ProcessPageLayout.h"
#include "processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.h"
#include "processes/ImageIntegration/widgets/StageListNavigator.h"
#include "processes/ImageIntegration/widgets/StageNavigator.h"
#include "processes/ImageIntegration/widgets/WorkflowRunView.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStanding.h"

#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextEdit>

#include <memory>
#include <vector>

namespace an::tests::integration
{
namespace
{

using an::ui::StageRow;
using an::ui::StageStatus;
using ImageIntegration::StagePage;

/// A text of the view by its label's name; empty for none.
[[nodiscard]] QString labelText(const QWidget& view, const char* name)
{
    const auto* const label = view.findChild<QLabel*>(QString::fromLatin1(name));
    return label != nullptr ? label->text() : QString();
}

[[nodiscard]] QPushButton* button(const QWidget& view, const char* name)
{
    return view.findChild<QPushButton*>(QString::fromLatin1(name));
}

/// The log as a person reads it, less the time each line was written at.
[[nodiscard]] QString logLines(const QWidget& view)
{
    const auto* const log = view.findChild<QTextEdit*>(QStringLiteral("workflowLogText"));
    QString text = log != nullptr ? log->toPlainText() : QString();
    text.remove(QRegularExpression(QStringLiteral(R"(\d{2}:\d{2}:\d{2}\s)")));
    return text;
}

[[nodiscard]] WorkflowProgress progressAt(const WorkflowStage stage, const float stageFraction, const int group,
                                          const int groups, const QString& operation, const QString& file,
                                          const int subStep = 0, const int subSteps = 0,
                                          const QString& subStepName = {})
{
    WorkflowProgress progress =
        scriptedReport(stageFraction, group, groups, operation, file, subStep, subSteps, subStepName);
    progress.currentStage = stage;
    return progress;
}

/// The cases of a run shown by Image Integration.
class WorkflowRunViewCase : public IntegrationPageCase
{
protected:
    void TearDown() override
    {
        // A run still on its way is let go to its end before its front end goes.
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

    /// Starts the fixture pipeline from the process; the gate that lets it go on.
    std::shared_ptr<RunGate> startFixtureRun(ImageIntegrationProcess& process)
    {
        m_gate = std::make_shared<RunGate>();
        EXPECT_TRUE(m_output.isValid());
        EXPECT_TRUE(process.startRun(fixtureManifest(m_output.path()), fixturePipeline(m_gate)));
        return m_gate;
    }

    /// An entry of the process's Navigate menu by its text; null for none.
    [[nodiscard]] QAction* navigateEntry(const QString& text) const
    {
        for (const QMenu* menu : m_process->findChildren<QMenu*>())
        {
            if (menu->title().remove(QLatin1Char('&')) != QStringLiteral("Navigate"))
            {
                continue;
            }
            for (QAction* entry : menu->actions())
            {
                if (entryText(*entry) == text)
                {
                    return entry;
                }
            }
        }
        return nullptr;
    }

    QTemporaryDir m_output;
    std::shared_ptr<RunGate> m_gate;
};

} // namespace

// The Processing window and the page hold the same view: given the same run, they show the same stages with the same
// marks and lines, the same parts under the stage on its way, the same words of what the run does, the same log and
// the same times, and offer the same acts at its end.
TEST_F(WorkflowRunViewCase, ShowsWhatTheWindowShows)
{
    IntegrationDataModel model;
    WorkflowProgressController windowController(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&windowController);
    WorkflowRunView* const inWindow = dialog.runView();
    ASSERT_NE(inWindow, nullptr);

    ImageIntegrationProcess& process = openAsPage();
    WorkflowProgressController pageController(&model, nullptr);
    auto* const inPage = new WorkflowRunView(&pageController);
    ASSERT_TRUE(process.showWorkflowRun(inPage)) << "a page shows a run in itself";
    EXPECT_EQ(process.runView(), inPage);
    EXPECT_TRUE(m_window.centre->isAncestorOf(inPage));
    const std::vector<WorkflowRunView*> views = {inWindow, inPage};

    const QStringList stages = {QStringLiteral("Bias"), QStringLiteral("LightsRegistration"),
                                QStringLiteral("LightsStacking")};
    for (WorkflowRunView* view : views)
    {
        view->setStages(stages);
        view->onProgressUpdated(progressAt(WorkflowStage::Bias, 0.5f, 1, 1, QStringLiteral("Stacking bias"),
                                           QStringLiteral("C:/frames/bias_01.fit")));
        view->onLogMessage(WorkflowLogLevel::Info, QStringLiteral("Bias: 12 frames"));
        view->onProgressUpdated(progressAt(WorkflowStage::LightsRegistration, 0.5f, 2, 3,
                                           QStringLiteral("Detecting stars"), QStringLiteral("C:/frames/light_02.fit"),
                                           1, 2, QStringLiteral("Detect")));
        view->onProgressUpdated(progressAt(WorkflowStage::LightsRegistration, 0.5f, 2, 3,
                                           QStringLiteral("Matching stars"), QStringLiteral("C:/frames/light_02.fit"),
                                           2, 2, QStringLiteral("Match")));
        view->onLogMessage(WorkflowLogLevel::Warning, QStringLiteral("light_07.fit: eccentricity above threshold"));
    }

    // The run on its way: the stage it passed is done, the one it is at carries the progress and its parts.
    const std::vector<StageRow> rows = inWindow->stageRows();
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].title, QStringLiteral("Bias"));
    EXPECT_EQ(rows[0].status, StageStatus::Done);
    EXPECT_EQ(rows[1].title, QStringLiteral("Registration"));
    EXPECT_EQ(rows[1].status, StageStatus::Running);
    ASSERT_TRUE(rows[1].progress.has_value());
    EXPECT_EQ(*rows[1].progress, 50);
    ASSERT_EQ(rows[1].steps.size(), 2u);
    EXPECT_EQ(rows[1].steps[0].name, QStringLiteral("Detect"));
    EXPECT_EQ(rows[1].steps[0].status, StageStatus::Done);
    EXPECT_EQ(rows[1].steps[1].name, QStringLiteral("Match"));
    EXPECT_EQ(rows[1].steps[1].status, StageStatus::Running);
    EXPECT_EQ(rows[2].title, QStringLiteral("Stacking"));
    EXPECT_EQ(rows[2].status, StageStatus::Pending);
    EXPECT_TRUE(rows[2].steps.empty());
    EXPECT_EQ(labelText(*inWindow, "lblCurrentOperation"), QStringLiteral("[2/3] - Matching stars  [Step 2/2: Match]"));
    EXPECT_EQ(labelText(*inWindow, "lblCurrentFile"), QStringLiteral("light_02.fit"));
    EXPECT_EQ(inWindow->standing().step, 2);
    EXPECT_EQ(inWindow->standing().steps, 3);
    EXPECT_NEAR(inWindow->standing().fraction, 0.5, 1e-6);
    const auto* const windowBar = inWindow->findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    const auto* const pageBar = inPage->findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(windowBar, nullptr);
    ASSERT_NE(pageBar, nullptr);
    EXPECT_EQ(windowBar->value(), 50) << "the bar is the whole run's";
    EXPECT_TRUE(logLines(*inWindow).contains(QStringLiteral("Bias: 12 frames")));
    EXPECT_TRUE(logLines(*inWindow).contains(QStringLiteral("light_07.fit: eccentricity above threshold")));

    EXPECT_EQ(inPage->stageRows(), rows);
    EXPECT_EQ(labelText(*inPage, "lblCurrentOperation"), labelText(*inWindow, "lblCurrentOperation"));
    EXPECT_EQ(labelText(*inPage, "lblCurrentFile"), labelText(*inWindow, "lblCurrentFile"));
    EXPECT_EQ(pageBar->value(), windowBar->value());
    EXPECT_EQ(logLines(*inPage), logLines(*inWindow));
    EXPECT_EQ(inPage->standing(), inWindow->standing());
    for (const WorkflowRunView* view : views)
    {
        EXPECT_FALSE(button(*view, "btnCancel")->isHidden());
        EXPECT_TRUE(button(*view, "btnClose")->isHidden());
        EXPECT_TRUE(button(*view, "btnOpenReport")->isHidden());
        EXPECT_TRUE(labelText(*view, "lblElapsed").startsWith(QStringLiteral("Elapsed: ")));
        EXPECT_TRUE(labelText(*view, "lblElapsed").contains(QStringLiteral("Stage: Registration")));
    }

    // The run's end: every stage done with the time the run says it took, the run's own time, Open Report and Close.
    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 754'000;
    result.stageResults = {{QStringLiteral("BiasStacking"), QStringLiteral("completed"), 12'000, 1},
                           {QStringLiteral("LightsRegistration"), QStringLiteral("completed"), 600'000, 3},
                           {QStringLiteral("LightsStacking"), QStringLiteral("completed"), 142'000, 1}};
    result.htmlReportPath = QStringLiteral("C:/tmp/run/workflow_report.html");
    for (WorkflowRunView* view : views)
    {
        view->onFinished(result);
    }
    pump();
    const std::vector<StageRow> ended = inWindow->stageRows();
    ASSERT_EQ(ended.size(), 3u);
    for (const StageRow& row : ended)
    {
        EXPECT_EQ(row.status, StageStatus::Done) << row.title.toStdString();
        EXPECT_FALSE(row.progress.has_value());
        EXPECT_TRUE(row.steps.empty());
    }
    EXPECT_TRUE(ended[0].line.contains(QStringLiteral("12s"))) << ended[0].line.toStdString();
    EXPECT_TRUE(ended[1].line.contains(QStringLiteral("10m 0s"))) << ended[1].line.toStdString();
    EXPECT_TRUE(ended[2].line.contains(QStringLiteral("2m 22s"))) << ended[2].line.toStdString();
    EXPECT_EQ(inPage->stageRows(), ended);
    for (const WorkflowRunView* view : views)
    {
        EXPECT_TRUE(view->isFinished());
        EXPECT_TRUE(view->wasSuccessful());
        EXPECT_EQ(labelText(*view, "lblCurrentOperation"), QStringLiteral("Workflow Completed Successfully"));
        EXPECT_TRUE(labelText(*view, "lblElapsed").startsWith(QStringLiteral("Elapsed: 00:12:34")))
            << labelText(*view, "lblElapsed").toStdString();
        EXPECT_TRUE(button(*view, "btnCancel")->isHidden());
        EXPECT_FALSE(button(*view, "btnClose")->isHidden());
        EXPECT_FALSE(button(*view, "btnOpenReport")->isHidden());
        EXPECT_EQ(view->reportPath(), result.htmlReportPath);
        EXPECT_EQ(view->findChild<QProgressBar*>(QStringLiteral("stageProgressBar"))->value(), 100);
    }
    EXPECT_EQ(logLines(*inPage), logLines(*inWindow));
}

// A run that failed marks the stage it stopped at as failed and says why; one that was stopped marks it as stopped.
TEST_F(WorkflowRunViewCase, AFailedOrAStoppedRunSaysWhereItEnded)
{
    IntegrationDataModel model;
    WorkflowProgressController controller(&model, nullptr);
    const QStringList stages = {QStringLiteral("Bias"), QStringLiteral("LightsRegistration")};

    WorkflowRunView failed(&controller);
    failed.setStages(stages);
    failed.onProgressUpdated(
        progressAt(WorkflowStage::LightsRegistration, 0.42f, 1, 1, QStringLiteral("Registering"), QString()));
    WorkflowExecutor::Result failure;
    failure.errorMessage = QStringLiteral("No stars were found");
    failed.onFinished(failure);
    EXPECT_TRUE(failed.wasFailed());
    ASSERT_EQ(failed.stageRows().size(), 2u);
    EXPECT_EQ(failed.stageRows()[0].status, StageStatus::Done);
    EXPECT_EQ(failed.stageRows()[1].status, StageStatus::Failed);
    EXPECT_EQ(labelText(failed, "lblCurrentOperation"), QStringLiteral("Workflow Failed"));
    EXPECT_TRUE(button(failed, "btnOpenReport")->isHidden()) << "a run that left no report offers none";

    WorkflowRunView stopped(&controller);
    stopped.setStages(stages);
    stopped.onProgressUpdated(progressAt(WorkflowStage::Bias, 0.1f, 1, 1, QStringLiteral("Stacking"), QString()));
    WorkflowExecutor::Result cancelled;
    cancelled.cancelled = true;
    stopped.onFinished(cancelled);
    EXPECT_TRUE(stopped.wasCancelled());
    EXPECT_EQ(stopped.stageRows()[0].status, StageStatus::Warning);
    EXPECT_EQ(stopped.stageRows()[1].status, StageStatus::Pending);
    EXPECT_EQ(labelText(stopped, "lblCurrentOperation"), QStringLiteral("Workflow Cancelled"));
}

// The time a run has left is said beside the time it has taken once it can tell, in a line of its own, and is taken
// away when the run ends.
TEST_F(WorkflowRunViewCase, TheTimeLeftHasALineOfItsOwn)
{
    IntegrationDataModel model;
    WorkflowProgressController controller(&model, nullptr);
    WorkflowRunView view(&controller);
    auto* const remaining = view.findChild<QLabel*>(QStringLiteral("lblRemaining"));
    ASSERT_NE(remaining, nullptr);
    EXPECT_TRUE(remaining->isHidden()) << "a run that has only begun cannot tell";
    EXPECT_FALSE(labelText(view, "lblElapsed").contains(QStringLiteral("Remaining")));

    view.showRemaining(1'050'000);
    EXPECT_FALSE(remaining->isHidden());
    EXPECT_EQ(remaining->text(), QStringLiteral("Remaining: about 17m 30s"));
    view.showRemaining(std::nullopt);
    EXPECT_TRUE(remaining->isHidden());

    view.showRemaining(90'000);
    WorkflowExecutor::Result result;
    result.success = true;
    view.onFinished(result);
    EXPECT_TRUE(remaining->isHidden());
}

// In a window of its own a run opens the Processing window over the application, as it always has, holding the view.
TEST_F(WorkflowRunViewCase, InAWindowARunOpensTheProcessingWindow)
{
    ImageIntegrationProcess& process = openAsWindow();
    const std::shared_ptr<RunGate> gate = startFixtureRun(process);
    auto* const dialog = process.findChild<WorkflowProgressDialogV2*>(QStringLiteral("workflowProgressDialogV2"));
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_TRUE(dialog->isModal());
    EXPECT_EQ(process.runView(), nullptr) << "a window shows no run in itself";
    ASSERT_NE(dialog->runView(), nullptr);
    EXPECT_TRUE(process.runState().running());

    gate->open();
    ASSERT_TRUE(pumpUntil([dialog]() { return dialog->runView()->isFinished(); }));
    EXPECT_TRUE(dialog->wasSuccessful()) << process.runState().text.toStdString() << '\n'
                                         << logLines(*dialog->runView()).toStdString();
    EXPECT_EQ(process.runState().phase, an::ui::ProcessRunState::Phase::Succeeded);
    const std::vector<StageRow> rows = dialog->runView()->stageRows();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].status, StageStatus::Done);
    EXPECT_EQ(rows[1].status, StageStatus::Done);
    dialog->close();
    pump();
}

// As a page a run is shown in the page and no window opens. The process's run state is the run's: the step it is at
// among its stages and how far the whole has come. The view stays when the run has ended, until it is closed, and the
// stage that was shown is shown again.
TEST_F(WorkflowRunViewCase, OnAPageARunIsShownInThePageAndNoWindowOpens)
{
    ImageIntegrationProcess& process = openAsPage();
    ASSERT_NE(process.stageNavigator(), nullptr);
    process.stageNavigator()->setCurrentPage(StagePage::Lights);
    pump();
    QWidget* const lights = process.stageNavigator()->page(StagePage::Lights);
    ASSERT_NE(lights, nullptr);
    ASSERT_TRUE(lights->isVisible());

    const std::shared_ptr<RunGate> gate = startFixtureRun(process);
    WorkflowRunView* const view = process.runView();
    ASSERT_NE(view, nullptr);
    pump();
    EXPECT_EQ(process.findChild<WorkflowProgressDialogV2*>(), nullptr) << "no Processing window";
    EXPECT_FALSE(aDialogIsOpen());
    EXPECT_TRUE(m_window.centre->isAncestorOf(view));
    EXPECT_TRUE(view->isVisible());
    EXPECT_FALSE(lights->isVisible()) << "the run takes the place of the stage's page";
    EXPECT_TRUE(process.runState().running());

    gate->allow(1);
    ASSERT_TRUE(pumpUntil([view]() { return view->standing().step == 1; }));
    EXPECT_EQ(process.runState().text, QStringLiteral("Step 1/2: Bias"));
    ASSERT_TRUE(process.runState().progress.has_value());
    EXPECT_NEAR(*process.runState().progress, 0.25, 0.011);

    gate->allow(2);
    ASSERT_TRUE(pumpUntil([view]() { return view->standing().step == 2; }));
    EXPECT_EQ(process.runState().text, QStringLiteral("Step 2/2: Registration"));
    EXPECT_NEAR(*process.runState().progress, 0.625, 0.011);
    // The job's own list follows the run beside the view: the stage's bar is the stage's, the summary's the whole's.
    const auto* const list = dynamic_cast<ImageIntegration::StageListNavigator*>(process.stageNavigator());
    ASSERT_NE(list, nullptr);
    for (const StageRow& row : list->list()->rows())
    {
        if (row.id == QStringLiteral("registration"))
        {
            EXPECT_EQ(row.status, StageStatus::Running);
            ASSERT_TRUE(row.progress.has_value());
            EXPECT_EQ(*row.progress, 25);
        }
        if (row.id == QStringLiteral("summary"))
        {
            EXPECT_EQ(row.status, StageStatus::Running);
            ASSERT_TRUE(row.progress.has_value());
            EXPECT_EQ(*row.progress, 63) << "the first stage, and a quarter of the second of two";
        }
    }

    gate->allow(1);
    ASSERT_TRUE(pumpUntil([view]() {
        const std::vector<StageRow> rows = view->stageRows();
        return rows.size() == 2 && rows[1].steps.size() == 2;
    }));
    EXPECT_EQ(view->stageRows()[1].steps[0].name, QStringLiteral("Detect"));
    EXPECT_EQ(view->stageRows()[1].steps[0].status, StageStatus::Running);

    gate->open();
    ASSERT_TRUE(pumpUntil([view]() { return view->isFinished(); }));
    ASSERT_TRUE(pumpUntil([&process]() { return !process.runState().running(); }));
    EXPECT_TRUE(view->wasSuccessful());
    EXPECT_EQ(process.runState().phase, an::ui::ProcessRunState::Phase::Succeeded);
    EXPECT_EQ(process.runView(), view) << "the run stays in the page until it is closed";
    EXPECT_TRUE(view->isVisible());
    EXPECT_FALSE(aDialogIsOpen());

    QPushButton* const close = button(*view, "btnClose");
    ASSERT_NE(close, nullptr);
    ASSERT_FALSE(close->isHidden());
    const QPointer<WorkflowRunView> gone(view);
    close->click();
    pump();
    EXPECT_EQ(process.runView(), nullptr);
    EXPECT_TRUE(gone.isNull());
    EXPECT_TRUE(lights->isVisible()) << "the stage that was shown is shown again";
    EXPECT_EQ(process.stageNavigator()->currentPage(), StagePage::Lights);
}

// While a run is on its way the page's own acts wait for it, as its window waits behind the Processing window: the
// stages' pages, the list's foot and the menus' entries - but the stages can still be looked at, and the header's
// primary act, in place of Execute, is the way back to the run's progress. When the run ends they are given back.
TEST_F(WorkflowRunViewCase, WhileARunIsOnItsWayThePageHoldsItsActsAndOffersTheWayBack)
{
    ImageIntegrationProcess& process = openAsPage();
    an::ui::ProcessPageLayout* const page = process.pageLayout();
    ASSERT_NE(page, nullptr);
    ImageIntegration::StageNavigator* const navigator = process.stageNavigator();
    ASSERT_NE(navigator, nullptr);
    QAction* const execute = action(QStringLiteral("Execute Integration"));
    QAction* const import = action(QStringLiteral("Import Project..."));
    QAction* const toLights = navigateEntry(QStringLiteral("Lights"));
    ASSERT_NE(execute, nullptr);
    ASSERT_NE(import, nullptr);
    ASSERT_NE(toLights, nullptr);
    ASSERT_TRUE(import->isEnabled());
    ASSERT_NE(page->jobHeading(), nullptr);
    ASSERT_EQ(page->jobHeading()->primary.data(), execute);

    const std::shared_ptr<RunGate> gate = startFixtureRun(process);
    WorkflowRunView* const view = process.runView();
    ASSERT_NE(view, nullptr);
    gate->allow(1);
    ASSERT_TRUE(pumpUntil([view]() { return view->standing().step == 1; }));
    pump();

    // What waits, and what does not.
    EXPECT_FALSE(execute->isEnabled());
    EXPECT_FALSE(import->isEnabled());
    EXPECT_TRUE(toLights->isEnabled()) << "the Navigate menu still moves between the stages";
    for (int at = 0; at < ImageIntegration::kStagePageCount; ++at)
    {
        EXPECT_FALSE(navigator->page(static_cast<StagePage>(at))->isEnabled()) << "page " << at;
    }
    EXPECT_FALSE(process.triggerPrimaryAction()) << "a run on its way is not started again";

    // The header's primary act is the way back to the progress, and says how far the run has come.
    ASSERT_NE(page->jobHeading(), nullptr);
    QAction* const showProgress = page->jobHeading()->primary.data();
    ASSERT_NE(showProgress, nullptr);
    EXPECT_NE(showProgress, execute);
    EXPECT_TRUE(showProgress->isEnabled());
    EXPECT_TRUE(entryText(*showProgress).startsWith(QStringLiteral("Show progress")))
        << showProgress->text().toStdString();
    EXPECT_TRUE(entryText(*showProgress).endsWith(QStringLiteral("25 %"))) << showProgress->text().toStdString();

    // A stage looked at during the run takes the view's place; the primary act brings the run back.
    toLights->trigger();
    pump();
    EXPECT_TRUE(navigator->page(StagePage::Lights)->isVisible());
    EXPECT_FALSE(view->isVisible());
    EXPECT_FALSE(process.isRunShown());
    showProgress->trigger();
    pump();
    EXPECT_TRUE(view->isVisible());
    EXPECT_TRUE(process.isRunShown());
    EXPECT_FALSE(navigator->page(StagePage::Lights)->isVisible());

    // The end of a run brings it forward, and gives the page its acts back.
    toLights->trigger();
    pump();
    ASSERT_FALSE(view->isVisible());
    gate->open();
    ASSERT_TRUE(pumpUntil([view]() { return view->isFinished(); }));
    ASSERT_TRUE(pumpUntil([&process]() { return !process.runState().running(); }));
    pump();
    EXPECT_TRUE(view->isVisible()) << "how the run ended is shown";
    EXPECT_TRUE(import->isEnabled());
    for (int at = 0; at < ImageIntegration::kStagePageCount; ++at)
    {
        EXPECT_TRUE(navigator->page(static_cast<StagePage>(at))->isEnabled()) << "page " << at;
    }
    ASSERT_TRUE(pumpUntil([page, execute]() {
        return page->jobHeading() != nullptr && page->jobHeading()->primary.data() == execute;
    })) << "Execute is the primary act again";
}

// Cancel in the page stops the run: the button says it is stopping, the run ends as cancelled, and the process's run
// state says so.
TEST_F(WorkflowRunViewCase, CancelInThePageStopsTheRun)
{
    ImageIntegrationProcess& process = openAsPage();
    const std::shared_ptr<RunGate> gate = startFixtureRun(process);
    WorkflowRunView* const view = process.runView();
    ASSERT_NE(view, nullptr);
    gate->allow(1);
    ASSERT_TRUE(pumpUntil([view]() { return view->standing().step == 1; }));

    QPushButton* const cancel = button(*view, "btnCancel");
    ASSERT_NE(cancel, nullptr);
    ASSERT_TRUE(cancel->isEnabled());
    cancel->click();
    EXPECT_FALSE(cancel->isEnabled());
    EXPECT_EQ(cancel->text(), QStringLiteral("Cancelling..."));
    ASSERT_TRUE(pumpUntil([view]() { return view->isFinished(); }));
    ASSERT_TRUE(pumpUntil([&process]() { return !process.runState().running(); }));
    EXPECT_TRUE(view->wasCancelled());
    EXPECT_EQ(process.runState().phase, an::ui::ProcessRunState::Phase::Cancelled);
    EXPECT_EQ(labelText(*view, "lblCurrentOperation"), QStringLiteral("Workflow Cancelled"));
    EXPECT_EQ(view->stageRows()[0].status, StageStatus::Warning);
}

} // namespace an::tests::integration
