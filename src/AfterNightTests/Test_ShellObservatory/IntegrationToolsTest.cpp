// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Integration Lite, the single steps and Image List Analyzer in the Observatory window, as a person meets them:
/// each is a task whose page it fills whole, under a header that names it and shows no menu button; a run a single
/// step states is shown at the head of its page with Cancel, in the header and in the rail, and its end is said as a
/// job's is - finished, never applied to an image, with no Undo; Image Integration Lite shows its run in itself; and
/// the Analyzer's preview lies inside its page.

#include "ObservatoryWindowCase.h"

#include "pages/IntegrationHubPage.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"

#include "components/AnIconButton.h"
#include "components/AnIconRail.h"
#include "components/AnSnackbar.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ProcessPresentation.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/ApplicationCore.h"
#include "processes/ImageIntegrationLiteProcess.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"

#include <QAbstractButton>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using an::ui::ProcessRunState;
using an::ui::observatory::IntegrationHubPage;
using an::ui::shell::Task;
using an::ui::shell::TaskId;
using Phase = ProcessRunState::Phase;
using Window = an::ui::observatory::ObservatoryWindow;

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The cases of the integration's tools as tasks: each launched as its menu entry launches it, on no image.
class IntegrationTools : public ObservatoryWindowCase
{
protected:
    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        return *window().taskPresenter();
    }

    /// Opens a tool as its menu entry does, and answers it once it is a task and shown.
    static TopLevelProcessBaseControl* launch(const char* descriptorId)
    {
        const std::size_t before = tasks().model().tasks().size();
        EXPECT_TRUE(core().processLaunch().launch(std::string(descriptorId)));
        TopLevelProcessBaseControl* made = nullptr;
        EXPECT_TRUE(an::tests::app::processEventsUntil([&]() {
            const std::vector<Task>& now = tasks().model().tasks();
            if (now.size() > before && !now.back().process.isNull() && now.back().process->isVisible())
            {
                made = now.back().process.data();
            }
            return made != nullptr;
        }));
        settle();
        return made;
    }

    [[nodiscard]] static TaskId taskOf(const TopLevelProcessBaseControl* process)
    {
        const Task* const task = tasks().model().taskFor(process);
        EXPECT_NE(task, nullptr) << "the process is no task";
        return task != nullptr ? task->id : 0u;
    }

    [[nodiscard]] static an::ui::RailItem railItem(const TaskId id)
    {
        const QString railId = Window::railTaskId(id);
        for (const an::ui::RailItem& item : window().rail()->items())
        {
            if (item.id == railId)
            {
                return item;
            }
        }
        return an::ui::RailItem{};
    }

    /// Holds a tool's task to a page it fills whole, under a header that names it and shows no menu button, and to its
    /// rail item: named as the tool, not floating.
    static void expectATaskThatFillsItsPage(TopLevelProcessBaseControl& process, const QString& name)
    {
        EXPECT_TRUE(process.isPresentedAsPage()) << "the tool is a page of the centre";
        EXPECT_FALSE(process.isWindow());
        const an::ui::ProcessPageLayout* const page = process.pageLayout();
        ASSERT_NE(page, nullptr);
        ASSERT_NE(page->fullArea(), nullptr) << "the page is the tool whole";
        EXPECT_EQ(page->column(), nullptr);
        const TaskId id = taskOf(&process);
        ASSERT_NE(id, 0u);
        const Task* const task = tasks().model().task(id);
        ASSERT_NE(task, nullptr);
        EXPECT_EQ(task->presentation, an::ui::shell::TaskPresentation::Page);
        EXPECT_TRUE(window().isTaskPageShown());
        EXPECT_EQ(window().shownTask(), id);
        an::ui::observatory::TaskPage* const taskPage = window().taskPage(id);
        ASSERT_NE(taskPage, nullptr);
        EXPECT_TRUE(taskPage->isAncestorOf(&process));
        EXPECT_TRUE(taskPage->canvasArea().isEmpty()) << "a full page has no canvas";
        const an::ui::AnTaskHeader* const header = taskPage->header();
        EXPECT_EQ(header->processName(), name);
        ASSERT_NE(header->menuButton(), nullptr);
        EXPECT_FALSE(header->menuButton()->isVisibleTo(header)) << "a tool has no menus of its own";
        const an::ui::RailItem item = railItem(id);
        EXPECT_EQ(item.text, name);
        EXPECT_FALSE(item.floating);
        EXPECT_TRUE(item.thumbnail.isNull()) << "a tool works on no single image";
    }
};

} // namespace

// Image Integration Lite is a task whose page it fills whole. It shows its run in itself, so its page has no row and no
// Cancel at its head; and the page's primary action is its Run Integration button, which nothing presses while there
// is nothing to integrate.
TEST_F(IntegrationTools, ImageIntegrationLiteIsATaskThatShowsItsRunInItself)
{
    auto* const lite = dynamic_cast<ImageIntegrationLiteProcess*>(launch(IntegrationHubPage::kImageIntegrationLite));
    ASSERT_NE(lite, nullptr);
    expectATaskThatFillsItsPage(*lite, QStringLiteral("Image Integration Lite"));
    const an::ui::ProcessPageLayout* const page = lite->pageLayout();
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->cancelButton(), nullptr) << "its run's view has its own Cancel";
    EXPECT_EQ(page->runBanner(), nullptr);
    EXPECT_EQ(lite->runView(), nullptr) << "no run yet";

    auto* const run = lite->findChild<QPushButton*>(QStringLiteral("liteRunIntegrationButton"));
    ASSERT_NE(run, nullptr);
    EXPECT_TRUE(page->fullArea()->isAncestorOf(run));
    ASSERT_FALSE(run->isEnabled()) << "nothing to integrate";
    EXPECT_FALSE(lite->triggerPrimaryAction());
    EXPECT_FALSE(lite->runState().running());
}

// Image Calibration, Image Registration and Image Stacking are tasks whose pages they fill whole. A run one of them
// states is shown at the head of its page with Cancel, in the task's header and in the rail.
TEST_F(IntegrationTools, TheSingleStepsAreTasksWhoseRunIsShownAtTheHeadOfTheirPage)
{
    const std::vector<std::pair<const char*, QString>> steps = {
        {IntegrationHubPage::kImageCalibration, QStringLiteral("Image Calibration")},
        {IntegrationHubPage::kImageRegistration, QStringLiteral("Image Registration")},
        {IntegrationHubPage::kImageStacking, QStringLiteral("Image Stacking")}};
    for (const auto& [descriptorId, name] : steps)
    {
        SCOPED_TRACE(name.toStdString());
        const QPointer<TopLevelProcessBaseControl> process(launch(descriptorId));
        ASSERT_FALSE(process.isNull());
        expectATaskThatFillsItsPage(*process, name);
        const an::ui::ProcessPageLayout* const page = process->pageLayout();
        ASSERT_NE(page, nullptr);
        const TaskId id = taskOf(process.data());
        QAbstractButton* const cancel = page->cancelButton();
        ASSERT_NE(cancel, nullptr) << "the page's Cancel is at its head";
        EXPECT_EQ(page->runBars(), 0);
        EXPECT_FALSE(cancel->isVisibleTo(process.data()));

        process->setRunState(an::ui::runningState(QStringLiteral("working"), 0.4));
        settle();
        EXPECT_EQ(page->runBars(), 1);
        EXPECT_TRUE(cancel->isVisibleTo(process.data()));
        const an::ui::AnTaskHeader* const header = window().taskPage(id)->header();
        EXPECT_EQ(header->state(), an::ui::AnTaskHeader::State::Running);
        EXPECT_EQ(header->stateText(), QStringLiteral("working - 40 %"));
        const an::ui::RailItem item = railItem(id);
        EXPECT_EQ(item.runPhase, an::ui::RailRunPhase::Running);
        ASSERT_TRUE(item.progress.has_value());
        EXPECT_EQ(*item.progress, 40);
        const QString toolTip = window().rail()->itemToolTip(Window::railTaskId(id));
        EXPECT_TRUE(toolTip.startsWith(name)) << toolTip.toStdString();
        EXPECT_TRUE(toolTip.contains(QStringLiteral("working 40 %"))) << toolTip.toStdString();

        process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
        settle();
        EXPECT_EQ(page->runBars(), 0);
        (void)process->close();
        settle();
        EXPECT_TRUE(process.isNull());
    }
}

// The end of a tool's run is said as a job's: in front of the person nothing says an image was applied to and nothing
// offers Undo; while another place is shown, the window's transient message says the tool finished and offers the way
// to its task.
TEST_F(IntegrationTools, TheEndOfAToolsRunIsSaidAsFinishedNotAsApplied)
{
    const QPointer<TopLevelProcessBaseControl> process(launch(IntegrationHubPage::kImageStacking));
    ASSERT_FALSE(process.isNull());
    const TaskId id = taskOf(process.data());
    ASSERT_TRUE(window().isTaskPageShown());

    process->setRunState(an::ui::runningState(QStringLiteral("stacking"), 0.5));
    process->setRunState(ProcessRunState{.phase = Phase::Succeeded});
    settle();
    const an::ui::AnTaskHeader* const header = window().taskPage(id)->header();
    EXPECT_NE(header->state(), an::ui::AnTaskHeader::State::Applied) << "a tool applies nothing to an image";
    EXPECT_FALSE(window().snackbar()->isVisible()) << "a run that ends in front of the person needs no message";

    process->setRunState(an::ui::runningState(QStringLiteral("stacking"), 0.5));
    window().showIntegrationHub();
    settle();
    ASSERT_TRUE(window().isIntegrationHubShown());
    process->setRunState(ProcessRunState{.phase = Phase::Succeeded});
    settle();
    an::ui::AnSnackbar* const snackbar = window().snackbar();
    ASSERT_TRUE(snackbar->isVisible());
    EXPECT_EQ(snackbar->text(), QStringLiteral("Image Stacking finished"));
    EXPECT_EQ(snackbar->actionText(), QStringLiteral("Show"));
    EXPECT_TRUE(window().isIntegrationHubShown()) << "the end of a run takes the person nowhere";
    QAbstractButton* const show = snackbar->actionButton();
    ASSERT_NE(show, nullptr);
    show->click();
    settle();
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().shownTask(), id);
}

// Image List Analyzer is a task whose page it fills whole: its list and its preview - a view of its own, none the
// application makes - lie inside the page.
TEST_F(IntegrationTools, TheAnalyzerIsATaskWithItsPreviewInsideItsPage)
{
    const std::size_t viewsBefore = core().viewService().getListOfViewControls().size();
    auto* const analyzer = dynamic_cast<ImageListAnalyzerProcess*>(launch(IntegrationHubPage::kImageListAnalyzer));
    ASSERT_NE(analyzer, nullptr);
    expectATaskThatFillsItsPage(*analyzer, QStringLiteral("Image List Analyzer"));
    const an::ui::ProcessPageLayout* const page = analyzer->pageLayout();
    ASSERT_NE(page, nullptr);
    ASSERT_NE(analyzer->previewView(), nullptr);
    EXPECT_TRUE(page->fullArea()->isAncestorOf(analyzer->previewView())) << "its preview is in its page";
    EXPECT_EQ(core().viewService().getListOfViewControls().size(), viewsBefore)
        << "the preview is none of the application's views";
    ASSERT_NE(page->cancelButton(), nullptr) << "its optimization is shown at the page's head, with Cancel";
    EXPECT_FALSE(analyzer->triggerPrimaryAction()) << "nothing to optimize with no file listed";
}

} // namespace an::tests::observatory
