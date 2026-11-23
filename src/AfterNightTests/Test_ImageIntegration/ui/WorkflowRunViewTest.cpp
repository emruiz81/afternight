// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A run as Image Integration shows it, in the process: the same view in a window of its own and on a page - the run's
/// stages as a list whose stage on its way carries the progress and its parts, what the run does, its log, its times,
/// Cancel, and Open Report and Close once it has ended - and no window opens over the application. The process's own
/// acts wait for the run, its stages can still be looked at, a page's header's primary act is the way back to the
/// progress, and the process's run state says the step the run is at and how far the whole has come. The view's own
/// look follows the theme's tokens and the palette.

#include "Test_ImageIntegration/ui/IntegrationPageCase.h"
#include "Test_ImageIntegration/workflow/FixturePipeline.h"

#include "components/AnStageList.h"
#include "components/AnTaskHeader.h"
#include "controls/ProcessPageLayout.h"
#include "processes/ImageIntegration/widgets/StageListNavigator.h"
#include "processes/ImageIntegration/widgets/StageNavigator.h"
#include "processes/ImageIntegration/widgets/WorkflowRunView.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStanding.h"
#include "theme/AnDesignTokens.h"

#include <QLabel>
#include <QMenu>
#include <QPalette>
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

/// Puts the application's palette back as it was when it goes.
class ScopedAppPalette
{
public:
    ScopedAppPalette() : m_original(QApplication::palette()) {}

    ~ScopedAppPalette()
    {
        QApplication::setPalette(m_original);
    }

    ScopedAppPalette(const ScopedAppPalette&) = delete;
    ScopedAppPalette& operator=(const ScopedAppPalette&) = delete;

private:
    QPalette m_original;
};

[[nodiscard]] QPalette testPalette(const QColor& window, const QColor& base, const QColor& text, const QColor& button)
{
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, button);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    return palette;
}

/// Deletes a widget, when it still is, as the case leaves its scope.
class DeleteOnLeave
{
public:
    explicit DeleteOnLeave(QWidget* widget) : m_widget(widget) {}

    ~DeleteOnLeave()
    {
        delete m_widget.data();
    }

    DeleteOnLeave(const DeleteOnLeave&) = delete;
    DeleteOnLeave& operator=(const DeleteOnLeave&) = delete;

private:
    QPointer<QWidget> m_widget;
};

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

// A process in a window of its own and one on a page show a run alike, each in itself: given the same run, they show
// the same stages with the same marks and lines, the same parts under the stage on its way, the same words of what the
// run does, the same log and the same times, and offer the same acts at its end.
TEST_F(WorkflowRunViewCase, ShowsWhatTheWindowShows)
{
    IntegrationDataModel model;
    WorkflowProgressController windowController(&model, nullptr);
    ImageIntegrationProcess& windowed = openAsWindow();
    ASSERT_FALSE(windowed.hasPageLayout());
    // The window is this case's: the fixture's process is the page's, made next.
    m_process = nullptr;
    const DeleteOnLeave window(&windowed);
    auto* const inWindow = new WorkflowRunView(&windowController);
    ASSERT_TRUE(windowed.showWorkflowRun(inWindow)) << "a window shows a run in itself";
    EXPECT_EQ(windowed.runView(), inWindow);
    EXPECT_TRUE(windowed.isAncestorOf(inWindow));
    EXPECT_TRUE(windowed.isRunShown());

    ImageIntegrationProcess& process = openAsPage();
    WorkflowProgressController pageController(&model, nullptr);
    auto* const inPage = new WorkflowRunView(&pageController);
    ASSERT_TRUE(process.showWorkflowRun(inPage)) << "a page shows a run in itself";
    EXPECT_EQ(process.runView(), inPage);
    EXPECT_TRUE(m_window.centre->isAncestorOf(inPage));
    EXPECT_FALSE(aDialogIsOpen()) << "no window opens over the application";
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

// In a window of its own a run is shown in the process, as on a page: its view takes the place of the stage's page
// beside the list, no window opens over the application, the process's acts wait for the run, and the view stays when
// the run has ended until it is closed, the stage that was shown then shown again.
TEST_F(WorkflowRunViewCase, InAWindowARunIsShownInTheProcess)
{
    ImageIntegrationProcess& process = openAsWindow();
    ASSERT_FALSE(process.hasPageLayout());
    ASSERT_NE(process.stageNavigator(), nullptr);
    process.stageNavigator()->setCurrentPage(StagePage::Lights);
    pump();
    QWidget* const lights = process.stageNavigator()->page(StagePage::Lights);
    ASSERT_NE(lights, nullptr);
    ASSERT_TRUE(lights->isVisible());
    QAction* const import = action(QStringLiteral("Import Project..."));
    ASSERT_NE(import, nullptr);
    ASSERT_TRUE(import->isEnabled());

    const std::shared_ptr<RunGate> gate = startFixtureRun(process);
    WorkflowRunView* const view = process.runView();
    ASSERT_NE(view, nullptr) << "a window shows its run in itself";
    pump();
    EXPECT_FALSE(aDialogIsOpen()) << "no window opens over the application";
    EXPECT_TRUE(process.isAncestorOf(view));
    EXPECT_TRUE(view->isVisible());
    EXPECT_TRUE(process.isRunShown());
    EXPECT_FALSE(lights->isVisible()) << "the run takes the place of the stage's page";
    EXPECT_TRUE(process.runState().running());
    EXPECT_FALSE(import->isEnabled()) << "the process's acts wait for the run";
    EXPECT_FALSE(lights->isEnabled());

    gate->open();
    ASSERT_TRUE(pumpUntil([view]() { return view->isFinished(); }));
    ASSERT_TRUE(pumpUntil([&process]() { return !process.runState().running(); }));
    EXPECT_TRUE(view->wasSuccessful()) << process.runState().text.toStdString() << '\n'
                                       << logLines(*view).toStdString();
    EXPECT_EQ(process.runState().phase, an::ui::ProcessRunState::Phase::Succeeded);
    const std::vector<StageRow> rows = view->stageRows();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].status, StageStatus::Done);
    EXPECT_EQ(rows[1].status, StageStatus::Done);
    EXPECT_EQ(process.runView(), view) << "the run stays in the process until it is closed";
    EXPECT_TRUE(view->isVisible());
    EXPECT_TRUE(import->isEnabled()) << "the acts are given back";
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
    EXPECT_FALSE(aDialogIsOpen()) << "no window opens over the application";
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

// While a run is on its way the page's own acts wait for it: the stages' pages, the list's foot and the menus' entries
// - but the stages can still be looked at, and the header's primary act, in place of Execute, is the way back to the
// run's progress. When the run ends they are given back.
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

// The time line says the time the run has taken and the stage it is at, and no estimate of its end.
TEST_F(WorkflowRunViewCase, TimeDisplayShowsElapsedAndCurrentStageWithoutEta)
{
    IntegrationDataModel model;
    WorkflowProgressController controller(&model, nullptr);
    WorkflowRunView view(&controller);
    const QRegularExpression clock(QStringLiteral("^Elapsed: \\d{2}:\\d{2}:\\d{2}"));
    EXPECT_TRUE(clock.match(labelText(view, "lblElapsed")).hasMatch()) << labelText(view, "lblElapsed").toStdString();

    view.setStages({QStringLiteral("Bias"), QStringLiteral("LightsStacking")});
    WorkflowProgress progress;
    progress.currentStage = WorkflowStage::LightsStacking;
    progress.currentGroup = 1;
    progress.totalGroups = 1;
    progress.overallProgress = 0.25f;
    progress.currentOperation = QStringLiteral("Stacking");
    view.onProgressUpdated(progress);

    const QString elapsed = labelText(view, "lblElapsed");
    EXPECT_TRUE(elapsed.contains(QStringLiteral("Elapsed:"))) << elapsed.toStdString();
    EXPECT_FALSE(elapsed.contains(QStringLiteral("ETA:"))) << elapsed.toStdString();
    EXPECT_TRUE(elapsed.contains(QStringLiteral("Stage: Stacking"))) << elapsed.toStdString();
}

// Open Report is offered only when the run left a report, and opens the one it left.
TEST_F(WorkflowRunViewCase, OpenReportButtonFollowsHtmlReportPath)
{
    IntegrationDataModel model;
    WorkflowProgressController noReportController(&model, nullptr);
    WorkflowRunView noReport(&noReportController);
    QPushButton* const noReportButton = button(noReport, "btnOpenReport");
    ASSERT_NE(noReportButton, nullptr);
    WorkflowExecutor::Result noReportResult;
    noReportResult.success = true;
    noReport.onFinished(noReportResult);
    EXPECT_TRUE(noReportButton->isHidden());
    EXPECT_TRUE(noReportButton->property("reportPath").toString().isEmpty());

    WorkflowProgressController reportController(&model, nullptr);
    WorkflowRunView report(&reportController);
    QPushButton* const reportButton = button(report, "btnOpenReport");
    ASSERT_NE(reportButton, nullptr);
    WorkflowExecutor::Result reportResult;
    reportResult.success = true;
    reportResult.htmlReportPath = QStringLiteral("C:/tmp/workflow_report.html");
    report.onFinished(reportResult);
    EXPECT_FALSE(reportButton->isHidden());
    EXPECT_EQ(reportButton->property("reportPath").toString(), reportResult.htmlReportPath);
    EXPECT_EQ(report.reportPath(), reportResult.htmlReportPath);
}

// A run that failed or was stopped keeps the bar where the run last got to.
TEST_F(WorkflowRunViewCase, FailureAndCancellationKeepLastRealProgress)
{
    WorkflowProgress progress;
    progress.currentStage = WorkflowStage::LightsRegistration;
    progress.overallProgress = 0.42f;
    progress.currentOperation = QStringLiteral("Registering");

    IntegrationDataModel model;
    WorkflowProgressController failureController(&model, nullptr);
    WorkflowRunView failure(&failureController);
    failure.onProgressUpdated(progress);
    const auto* const failureBar = failure.findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(failureBar, nullptr);
    ASSERT_EQ(failureBar->value(), 42);
    WorkflowExecutor::Result failed;
    failed.errorMessage = QStringLiteral("Registration failed");
    failure.onFinished(failed);
    EXPECT_EQ(failureBar->value(), 42);

    WorkflowProgressController cancelController(&model, nullptr);
    WorkflowRunView cancel(&cancelController);
    cancel.onProgressUpdated(progress);
    const auto* const cancelBar = cancel.findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(cancelBar, nullptr);
    WorkflowExecutor::Result cancelled;
    cancelled.cancelled = true;
    cancel.onFinished(cancelled);
    EXPECT_EQ(cancelBar->value(), 42);
}

// The log's background follows the palette: a dark palette's and a light one's differ.
TEST_F(WorkflowRunViewCase, AppliesThemeAwareStylesForDarkAndLightPalettes)
{
    const ScopedAppPalette paletteGuard;
    IntegrationDataModel model;

    QApplication::setPalette(
        testPalette(QColor(24, 28, 34), QColor(20, 24, 30), QColor(216, 224, 236), QColor(30, 36, 44)));
    WorkflowProgressController darkController(&model, nullptr);
    WorkflowRunView dark(&darkController);
    const auto* const darkLog = dark.findChild<QTextEdit*>(QStringLiteral("workflowLogText"));
    ASSERT_NE(darkLog, nullptr);
    ASSERT_NE(dark.findChild<QProgressBar*>(QStringLiteral("stageProgressBar")), nullptr);
    const QColor darkBackground = darkLog->palette().color(QPalette::Base);

    QApplication::setPalette(
        testPalette(QColor(245, 247, 252), QColor(252, 253, 255), QColor(32, 40, 54), QColor(238, 242, 248)));
    WorkflowProgressController lightController(&model, nullptr);
    WorkflowRunView light(&lightController);
    const auto* const lightLog = light.findChild<QTextEdit*>(QStringLiteral("workflowLogText"));
    ASSERT_NE(lightLog, nullptr);
    ASSERT_NE(light.findChild<QProgressBar*>(QStringLiteral("stageProgressBar")), nullptr);
    const QColor lightBackground = lightLog->palette().color(QPalette::Base);

    EXPECT_NE(darkBackground, lightBackground);
    EXPECT_TRUE(darkBackground.isValid());
    EXPECT_TRUE(lightBackground.isValid());
}

// What the run does and the log's title are set in the third heading, the times in the body's type, the log in the
// monospace type in a frame of the theme's small radius.
TEST_F(WorkflowRunViewCase, UsesTokenFontsAndRadiusForCoreWidgets)
{
    IntegrationDataModel model;
    WorkflowProgressController controller(&model, nullptr);
    WorkflowRunView view(&controller);
    const auto* const operation = view.findChild<QLabel*>(QStringLiteral("lblCurrentOperation"));
    const auto* const logTitle = view.findChild<QLabel*>(QStringLiteral("lblLogTitle"));
    const auto* const elapsed = view.findChild<QLabel*>(QStringLiteral("lblElapsed"));
    const auto* const log = view.findChild<QTextEdit*>(QStringLiteral("workflowLogText"));
    ASSERT_NE(operation, nullptr);
    ASSERT_NE(logTitle, nullptr);
    ASSERT_NE(elapsed, nullptr);
    ASSERT_NE(log, nullptr);

    EXPECT_EQ(operation->font(), an::ui::tokens().typography.heading3);
    EXPECT_EQ(logTitle->font(), an::ui::tokens().typography.heading3);
    EXPECT_EQ(elapsed->font(), an::ui::tokens().typography.body);
    EXPECT_EQ(log->font(), an::ui::tokens().typography.monospace);
    EXPECT_EQ(log->property("anFrameRadius").toInt(), an::ui::tokens().radius.sm);
}

// The run's bar is as tall as the theme's largest spacing.
TEST_F(WorkflowRunViewCase, UsesTokenSizedProgressBar)
{
    IntegrationDataModel model;
    WorkflowProgressController controller(&model, nullptr);
    WorkflowRunView view(&controller);
    const auto* const bar = view.findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->minimumHeight(), an::ui::tokens().spacing.xl);
}

} // namespace an::tests::integration
