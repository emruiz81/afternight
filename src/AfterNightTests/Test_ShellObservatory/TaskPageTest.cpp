// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A task's page in the Observatory window, as a person meets it with Curves Transformation: the page opened in the
/// centre with its rail item selected and the breadcrumb naming it; the same page found again after leaving it; a run's
/// bar, row and Cancel; a run left running while another place is shown; the snackbar of a run that succeeded and its
/// Undo; Esc and Ctrl+Enter; the image's floor beside each column; the compact readout's figures; a new image a run
/// makes landing in a workspace while the page stays. And what the window's actions act on while a page is shown: the
/// looking actions on its canvas, the image actions on its target, Close and Maximize View withdrawn, Apply from the
/// rail.

#include "ObservatoryWindowCase.h"

#include "ShellActions.h"
#include "chrome/TopBar.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"
#include "workspaces/WorkspaceHost.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "app/ProcessRegistry.h"
#include "components/AnBreadcrumb.h"
#include "components/AnCompactReadout.h"
#include "components/AnIconButton.h"
#include "components/AnIconRail.h"
#include "components/AnSnackbar.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/ApplicationCore.h"
#include "processes/CurvesTransformationProcess.h"
#include "readout/ReadoutController.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "views/ViewChromeBinder.h"
#include "views/ViewService.h"
#include "widgets/CurvesWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ToneCurveModel.h"

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStackedWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::ProcessRunState;
using an::ui::shell::Task;
using an::ui::shell::TaskId;
using Phase = ProcessRunState::Phase;
namespace shellIds = an::ui::observatory::ids;
namespace actionIds = an::ui::shell::ids;

constexpr std::string_view kCurves = "transforms.actionCurves_Transformation";

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The item the rail shows under an id; a default item when it shows none.
[[nodiscard]] an::ui::RailItem railItem(const an::ui::AnIconRail& rail, const QString& id)
{
    for (const an::ui::RailItem& item : rail.items())
    {
        if (item.id == id)
        {
            return item;
        }
    }
    return an::ui::RailItem{};
}

/// The curve's widget of Curves Transformation.
[[nodiscard]] CurvesWidget* curvesOf(const QWidget& process)
{
    for (QWidget* widget : process.findChildren<QWidget*>())
    {
        if (auto* curves = dynamic_cast<CurvesWidget*>(widget))
        {
            return curves;
        }
    }
    return nullptr;
}

/// The cases of a task's page: each opens Curves Transformation over a view of its own.
class TaskPageCase : public ObservatoryWindowCase
{
protected:
    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        an::ui::observatory::TaskPresenter* presenter = window().taskPresenter();
        EXPECT_NE(presenter, nullptr);
        return *presenter;
    }

    /// Opens a process over the active view as its menu entry does; the process opened, null when none did.
    static TopLevelProcessBaseControl* launch(std::string_view descriptorId)
    {
        const std::size_t before = tasks().model().tasks().size();
        EXPECT_TRUE(core().processLaunch().launch(std::string(descriptorId)));
        settle();
        const std::vector<Task>& now = tasks().model().tasks();
        EXPECT_EQ(now.size(), before + 1u);
        return now.size() > before ? now.back().process.data() : nullptr;
    }

    /// The process's page widget in the centre; null for none.
    [[nodiscard]] static an::ui::observatory::TaskPage* pageOf(const TopLevelProcessBaseControl* process)
    {
        const Task* task = tasks().model().taskFor(process);
        return task != nullptr ? window().taskPage(task->id) : nullptr;
    }

    /// Bends the curve, which offers Apply.
    static void bend(TopLevelProcessBaseControl& process)
    {
        CurvesWidget* curves = curvesOf(process);
        ASSERT_NE(curves, nullptr);
        curves->setCurve(an::ui::ToneCurveModel(std::vector<an::CurveControlPoint>{{0.0, 0.0}, {0.3, 0.6}, {1.0, 1.0}},
                                                curves->getCurve().interpolation()));
        settle();
    }

    /// Waits for a process's runs and its preview's work to land.
    [[nodiscard]] static bool idle(TopLevelProcessBaseControl* process)
    {
        QPointer<TopLevelProcessBaseControl> guard(process);
        return an::tests::app::processEventsUntil([&]() {
            auto* preview = dynamic_cast<ImageRTPreviewControl*>(guard.data());
            return guard.isNull() || preview == nullptr || preview->isProcessIdle();
        });
    }

    /// The number of the step the view's history shows.
    [[nodiscard]] static std::size_t currentStep(const ImageViewControl& view)
    {
        return view.viewHistory() != nullptr ? view.viewHistory()->steps().current : 0u;
    }

    void TearDown() override
    {
        // A run state a case set by hand is ended, so its page closes.
        for (const Task& task : tasks().model().tasks())
        {
            if (!task.process.isNull() && task.process->runState().running())
            {
                task.process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
            }
        }
        for (const Task& task : std::vector<Task>(tasks().model().tasks()))
        {
            if (!task.process.isNull())
            {
                (void)task.process->close();
            }
        }
        settle();
        ObservatoryWindowCase::TearDown();
    }
};

class TaskPage : public TaskPageCase
{
};

class TaskSubject : public TaskPageCase
{
};

} // namespace

// Curves Transformation opens as a page of the centre: its rail item selected and not floating, the breadcrumb naming
// it and its image, its header naming them, the readout's column of the workspace hidden.
TEST_F(TaskPage, OpeningCurvesTransformationShowsItsPage)
{
    ImageViewControl* const view = openView(QStringLiteral("page-opens"));
    ASSERT_NE(view, nullptr);
    window().showViews();
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    EXPECT_TRUE(process->isPresentedAsPage());
    EXPECT_FALSE(process->isWindow());
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().centre()->currentWidget(), page);
    EXPECT_TRUE(page->isAncestorOf(process));
    const Task* task = tasks().model().taskFor(process);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(tasks().model().current(), task->id);
    EXPECT_EQ(task->presentation, an::ui::shell::TaskPresentation::Page);
    const an::ui::RailItem item =
        railItem(*window().rail(), an::ui::observatory::ObservatoryWindow::railTaskId(task->id));
    EXPECT_TRUE(item.selected);
    EXPECT_FALSE(item.floating);
    EXPECT_EQ(core().viewService().subjectView(), view);
    EXPECT_FALSE(window().rightColumn()->isVisible()) << "the page has its own column";
    EXPECT_EQ(page->header()->processName(), QStringLiteral("Curves Transformation"));
    EXPECT_EQ(page->header()->targetName(), QStringLiteral("page-opens"));
    ASSERT_NE(process->pageLayout(), nullptr);
    EXPECT_EQ(process->pageLayout()->category(), QStringLiteral("Intensity Transformations"))
        << "the eyebrow is the process's menu";
}

// The breadcrumb names the process and the image while the page is shown.
TEST_F(TaskPage, TheBreadcrumbNamesTheProcessAndTheImage)
{
    ImageViewControl* const view = openView(QStringLiteral("page-breadcrumb"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(window().topBar()->breadcrumb()->parts(),
              (QStringList{QStringLiteral("Curves Transformation"), QStringLiteral("page-breadcrumb")}));
}

// Leaving the page and coming back by the rail shows the same page, with the same process in it.
TEST_F(TaskPage, LeavingAndReturningShowsTheSameWidget)
{
    ImageViewControl* const view = openView(QStringLiteral("page-return"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    const TaskId id = tasks().model().taskFor(process)->id;

    window().showViews();
    settle();
    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_EQ(tasks().model().current(), 0u) << "no task is looked at";
    EXPECT_FALSE(core().viewService().hasTaskSubject());

    const QRect place = window().rail()->itemRect(an::ui::observatory::ObservatoryWindow::railTaskId(id));
    ASSERT_TRUE(place.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, place.center());
    settle();
    EXPECT_EQ(window().centre()->currentWidget(), page);
    EXPECT_TRUE(page->isAncestorOf(process));
    EXPECT_EQ(tasks().model().current(), id);
}

// During a run the header's bar shows the progress, the page's run row and Cancel are shown and its controls disabled.
TEST_F(TaskPage, ARunShowsTheBarTheRowTheDisabledControlsAndCancel)
{
    ImageViewControl* const view = openView(QStringLiteral("page-run"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    an::ui::ProcessPageLayout* const layout = process->pageLayout();
    ASSERT_NE(layout, nullptr);

    process->setRunState(an::ui::runningState({}, 0.42));
    settle();
    EXPECT_EQ(page->header()->state(), an::ui::AnTaskHeader::State::Running);
    EXPECT_TRUE(page->header()->stateText().contains(QStringLiteral("42 %")));
    EXPECT_FALSE(page->header()->runBarRect().isEmpty());
    EXPECT_TRUE(layout->runBanner()->isVisible());
    EXPECT_TRUE(layout->cancelButton()->isVisible());
    EXPECT_FALSE(layout->bodyContent()->isEnabled());
    EXPECT_FALSE(layout->toggles()->isEnabled());
    EXPECT_FALSE(page->header()->closeButton()->isEnabled()) << "a running task is not closed";

    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    settle();
    EXPECT_FALSE(layout->runBanner()->isVisible());
    EXPECT_TRUE(layout->bodyContent()->isEnabled());
    EXPECT_TRUE(page->header()->runBarRect().isEmpty());
}

// A run left by showing another place goes on, and ends with one step in its image's history.
TEST_F(TaskPage, LeavingDuringARunLeavesItRunning)
{
    ImageViewControl* const view = openView(QStringLiteral("page-leave-run"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);

    process->applyProcess();
    window().showViews();
    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_TRUE(process->runState().running()) << "the run goes on when its page is left";
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_EQ(currentStep(*view), before + 1u) << "one step in the image's history";
    EXPECT_FALSE(view->isLockedForProcessing());
}

// A run that succeeds says so in the snackbar, with how long it took and Undo, which undoes the image's step.
TEST_F(TaskPage, SuccessShowsTheSnackbarAndUndoUndoes)
{
    ImageViewControl* const view = openView(QStringLiteral("page-success"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);

    process->applyProcess();
    ASSERT_TRUE(idle(process));
    settle();
    an::ui::AnSnackbar* const snackbar = window().snackbar();
    ASSERT_NE(snackbar, nullptr);
    EXPECT_TRUE(snackbar->isVisible());
    EXPECT_EQ(snackbar->text(), QStringLiteral("Curves Transformation applied"));
    EXPECT_TRUE(snackbar->detail().endsWith(QStringLiteral(" s")));
    EXPECT_EQ(snackbar->actionText(), QStringLiteral("Undo"));
    ASSERT_EQ(currentStep(*view), before + 1u);
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->header()->state(), an::ui::AnTaskHeader::State::Applied);
    EXPECT_TRUE(page->header()->stateText().startsWith(QStringLiteral("applied")));

    snackbar->actionButton()->click();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return currentStep(*view) == before; }));
    EXPECT_FALSE(snackbar->isVisible()) << "the action dismisses the message";
}

// Esc closes an idle page and does nothing during a run; the place shown before comes back.
TEST_F(TaskPage, EscClosesWhenIdleAndNotDuringARun)
{
    ImageViewControl* const view = openView(QStringLiteral("page-esc"));
    ASSERT_NE(view, nullptr);
    window().showViews();
    settle();
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    const QPointer<TopLevelProcessBaseControl> guard(process);
    QWidget* const focus = process->pageLayout()->footerButton(QDialogButtonBox::Reset);
    ASSERT_NE(focus, nullptr);

    process->setRunState(an::ui::runningState());
    QTest::keyClick(focus, Qt::Key_Escape);
    settle();
    EXPECT_FALSE(guard.isNull());
    EXPECT_TRUE(window().isTaskPageShown()) << "not during a run";

    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    QTest::keyClick(focus, Qt::Key_Escape);
    settle();
    EXPECT_TRUE(guard.isNull()) << "Esc closed the idle page";
    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_TRUE(window().isWorkspaceShown()) << "the place shown before the task";
}

// Ctrl+Enter inside the page applies.
TEST_F(TaskPage, CtrlEnterApplies)
{
    ImageViewControl* const view = openView(QStringLiteral("page-ctrl-enter"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);
    QWidget* const focus = process->pageLayout()->footerButton(QDialogButtonBox::Reset);
    ASSERT_NE(focus, nullptr);
    focus->setFocus(Qt::OtherFocusReason);
    settle();

    QTest::keyClick(focus, Qt::Key_Return, Qt::ControlModifier);
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_EQ(currentStep(*view), before + 1u);
}

// In a window 1536 px wide the image keeps 1130 px beside a standard column, and 970 px beside a wide one.
TEST_F(TaskPage, TheImageKeepsItsFloor)
{
    ImageViewControl* const view = openView(QStringLiteral("page-floor"));
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(window().width(), 1536);

    // Curves Transformation's own column is the wide one.
    TopLevelProcessBaseControl* const wide = launch(kCurves);
    ASSERT_NE(wide, nullptr);
    settle();
    ASSERT_NE(wide->pageLayout(), nullptr);
    EXPECT_EQ(wide->pageLayout()->columnWidth(), 520);
    EXPECT_GE(wide->pageLayout()->canvas()->width(), 970);
    (void)wide->close();
    settle();

    // The same process made inside a launch that states a standard column.
    const an::ui::process_registry::ProcessDescriptor* registered =
        an::ui::process_registry::ProcessRegistry::instance().find(std::string(kCurves));
    ASSERT_NE(registered, nullptr);
    an::ui::process_registry::ProcessDescriptor standard = *registered;
    standard.columnWidth = an::ui::ProcessColumnWidth::Standard;
    {
        const an::ui::ProcessLaunchScope launched(standard);
        auto* const made = new CurvesTransformationProcess(view);
        made->show();
    }
    settle();
    ASSERT_FALSE(tasks().model().tasks().empty());
    TopLevelProcessBaseControl* const narrow = tasks().model().tasks().back().process.data();
    ASSERT_NE(narrow, nullptr);
    ASSERT_NE(narrow->pageLayout(), nullptr);
    EXPECT_EQ(narrow->pageLayout()->columnWidth(), 360);
    EXPECT_GE(narrow->pageLayout()->canvas()->width(), 1130);
}

// The compact readout shows the settled preview's figures and their change against the image.
TEST_F(TaskPage, TheCompactReadoutShowsSettledFiguresAndTheirChange)
{
    ImageViewControl* const view = openView(QStringLiteral("page-readout"), 0.3f);
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    an::ui::observatory::TaskPage* const page = pageOf(process);
    ASSERT_NE(page, nullptr);
    EXPECT_TRUE(process->pageLayout()->readoutSlot()->isAncestorOf(page->compactReadout()))
        << "the compact readout heads the process's column";
    an::ui::shell::ReadoutController& readout = window().readoutController();
    EXPECT_TRUE(readout.followsTask());
    EXPECT_EQ(readout.followedView(), view);
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return process->previewFramesShown() > 0u; }, 10000));
    readout.updateNow();
    ASSERT_TRUE(an::tests::app::processEventsUntil(
        [&]() { return readout.isIdle() && readout.shownFigures().has_value() && !readout.isStale(); }, 10000));
    ASSERT_TRUE(readout.originalFigures().has_value()) << "the change is against the image's figures";
    EXPECT_FALSE(page->compactReadout()->isStale());
    EXPECT_FALSE(page->compactReadout()->modeText().isEmpty());

    // Leaving the page, the readout follows the active view again.
    window().showViews();
    settle();
    EXPECT_FALSE(readout.followsTask());
}

// A new image a run makes while the page is shown lands in the workspace; the page stays, and the snackbar's Show goes
// to the image.
TEST_F(TaskPage, AResultThatIsANewImageDoesNotLeaveThePage)
{
    ImageViewControl* const view = openView(QStringLiteral("page-new-image"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(window().isTaskPageShown());

    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(session(), QStringLiteral("stars_only"), 0.6f);
    ASSERT_TRUE(document.has_value());
    ImageViewControl* const made = core().viewService().createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "stars_only", ViewControlMode::standard, view, false, false);
    ASSERT_NE(made, nullptr);
    settle();
    EXPECT_TRUE(window().isTaskPageShown()) << "the person stays on the page";
    EXPECT_FALSE(window().workspaces().workspaceOf(*made).isEmpty()) << "the image is in a workspace";
    an::ui::AnSnackbar* const snackbar = window().snackbar();
    ASSERT_TRUE(snackbar->isVisible());
    EXPECT_TRUE(snackbar->text().startsWith(QStringLiteral("stars_only opened in "))) << snackbar->text().toStdString();
    EXPECT_EQ(snackbar->actionText(), QStringLiteral("Show"));

    snackbar->actionButton()->click();
    settle();
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_EQ(core().viewService().getActiveImageViewControl(), made);
}

// Zoom Out on a page zooms the canvas and leaves the image's view as it was: the zoom is the canvas's.
TEST_F(TaskSubject, ZoomInZoomsTheCanvas)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-zoom"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(idle(process));
    ImageViewControl* const canvas = process->previewView();
    ASSERT_NE(canvas, nullptr);
    EXPECT_EQ(core().viewService().lookingView(), canvas);
    const double canvasBefore = canvas->getViewWidget()->getCurrentZoom();
    const double targetBefore = view->getViewWidget()->getCurrentZoom();

    // The small image fills the canvas at the largest zoom there is: the canvas zooms out.
    core().actions().action(actionIds::kZoomOut)->trigger();
    settle();
    EXPECT_LT(canvas->getViewWidget()->getCurrentZoom(), canvasBefore);
    EXPECT_DOUBLE_EQ(view->getViewWidget()->getCurrentZoom(), targetBefore);
}

// Undo on a page undoes the image's step.
TEST_F(TaskSubject, UndoUndoesTheTarget)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-undo"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);
    process->applyProcess();
    ASSERT_TRUE(idle(process));
    settle();
    ASSERT_EQ(currentStep(*view), before + 1u);

    EXPECT_EQ(core().viewService().subjectView(), view);
    core().actions().action(actionIds::kUndo)->trigger();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return currentStep(*view) == before; }));
}

// Save as on a page saves the image the task works on.
TEST_F(TaskSubject, SaveAsSavesTheTarget)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-save"), 0.4f);
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("observatory-task-saved.fits"));
    QFile::remove(path);
    an::tests::app::Answered dialog;
    an::tests::app::chooseInTheNextFileDialog({path}, &dialog);
    core().actions().action(actionIds::kSaveAs)->trigger();
    EXPECT_TRUE(dialog.seen);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() { return QFileInfo::exists(path); }, 20000));
    EXPECT_TRUE(dialog.title.contains(QStringLiteral("subject-save")))
        << "the target is saved: " << dialog.title.toStdString();
    QFile::remove(path);
}

// On a page Close, Close All Others and Maximize View are withdrawn, and offered again when the page is left.
TEST_F(TaskSubject, CloseAndMaximizeViewAreWithdrawn)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-close"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kCurves);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_FALSE(core().actions().action(actionIds::kClose)->isEnabled());
    EXPECT_FALSE(core().actions().action(actionIds::kCloseAllOthers)->isEnabled());
    EXPECT_FALSE(core().actions().action(actionIds::kMaximizeView)->isEnabled());
    EXPECT_TRUE(core().actions().action(actionIds::kSaveAs)->isEnabled());

    window().showViews();
    settle();
    EXPECT_TRUE(core().actions().action(actionIds::kClose)->isEnabled());
    EXPECT_TRUE(core().actions().action(actionIds::kMaximizeView)->isEnabled());
}

// Apply with the focus on the rail applies the current task.
TEST_F(TaskSubject, ApplyTaskWithTheFocusOnTheRail)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-apply"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    bend(*process);
    ASSERT_TRUE(idle(process));
    const std::size_t before = currentStep(*view);
    // A window shortcut is looked for in the active window. The suites' platform does not always hand the activation
    // back after a modal dialog of an earlier case: then the action is triggered as its shortcut would.
    window().activateWindow();
    const bool active = QTest::qWaitForWindowActive(&window());
    window().rail()->setFocus(Qt::OtherFocusReason);
    settle();
    EXPECT_FALSE(process->isAncestorOf(QApplication::focusWidget())) << "the focus is outside the page";

    QAction* const apply = window().shellActions().action(shellIds::kApplyTask);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(apply->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Return)));
    EXPECT_TRUE(apply->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Enter)));
    if (active)
    {
        QTest::keyClick(window().rail(), Qt::Key_Return, Qt::ControlModifier);
    }
    else
    {
        apply->trigger();
    }
    ASSERT_TRUE(idle(process));
    settle();
    EXPECT_EQ(currentStep(*view), before + 1u);
}

// Leaving the page gives the actions back to the active view.
TEST_F(TaskSubject, LeavingThePageGivesTheActionsBack)
{
    ImageViewControl* const view = openView(QStringLiteral("subject-leave"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kCurves));
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(idle(process));
    EXPECT_TRUE(core().viewService().hasTaskSubject());

    window().showViews();
    settle();
    EXPECT_FALSE(core().viewService().hasTaskSubject());
    EXPECT_EQ(core().viewService().lookingView(), core().viewService().getActiveImageViewControl());
    EXPECT_EQ(core().viewService().subjectView(), core().viewService().getActiveImageViewControl());
}

} // namespace an::tests::observatory
